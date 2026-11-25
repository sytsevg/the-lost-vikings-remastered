#include "lvr/session.hpp"

#include <algorithm>
#include <format>
#include <print>
#include <utility>

namespace lvr {

namespace {

// The original's level start takes this many refreshes: the fade out (70), the start's tick (3 and one more draw),
// the fade in (71).
constexpr std::size_t kStartRefreshes = 145;

}  // namespace

std::expected<std::unique_ptr<Session>, std::string> Session::open(const std::filesystem::path& dir, int slot,
                                                                   const SessionSound& sound, Rules rules,
                                                                   Draw draw_with) {
    auto files = assets::GameFiles::open(dir);
    if (!files) {
        return std::unexpected{files.error()};
    }
    if (slot < 0 || slot >= kSceneSlots) {
        return std::unexpected{std::format("no scene {}", slot)};
    }
    // Remaster rules' backdrops come from the user's SNES image beside the game's files, when it is there.
    std::optional<assets::SnesRom> snes;
    if (rules == Rules::kRemaster) {
        if (auto rom = assets::SnesRom::open(dir)) {
            snes = std::move(*rom);
        } else {
            std::println(stderr, "no SNES backdrops: {}", rom.error());
        }
    }
    auto loaded = load_slot(*files, slot, database::kSpaceship, snes ? &*snes : nullptr);
    auto shared = load_shared_graphics(files->archive());
    auto classic = audio::load_sound_data(files->archive(), audio::Version::kClassic);
    if (!loaded || !shared || !classic) {
        return std::unexpected{!loaded ? loaded.error() : (!shared ? shared.error() : classic.error())};
    }
    std::optional<audio::SoundData> modern;
    std::unique_ptr<audio::GmSynth> gm;
    if (!sound.soundfont.empty()) {
        auto data = audio::load_sound_data(files->archive(), audio::Version::kModern);
        auto synth = audio::GmSynth::open(sound.soundfont, audio::Opl3::kSampleRate);
        if (!data || !synth) {
            std::println(stderr, "no Modern sound: {}", !data ? data.error() : synth.error());
        } else {
            modern = std::move(*data);
            gm = std::move(*synth);
        }
    }
    std::unique_ptr<Session> session{new Session{std::move(*files), std::make_unique<LoadedLevel>(std::move(*loaded)),
                                                 std::move(*shared), slot, rules, std::move(*classic),
                                                 std::move(modern), std::move(gm)}};
    session->snes_ = std::move(snes);
    session->play_.snes = session->snes_ ? &*session->snes_ : nullptr;
    session->draw_ = std::move(draw_with);
    session->live_ = sound.live;
    session->set_modern(sound.modern);
    // The start is shown as its last picture for as long as the original's load takes; its sounds with the last.
    const Screen start = session->picture();
    for (std::size_t i = 0; i < kStartRefreshes; ++i) {
        std::vector<SoundCue> asked;
        if (i + 1 == kStartRefreshes) {
            asked = std::move(session->play_.world.cues);
            session->play_.world.cues.clear();
        }
        session->sound_of_refresh(std::move(asked));
        session->start_frames.push_back(start);
        session->start_frames.back().refresh = session->refreshes_++;
    }
    session->last_ = start;
    return session;
}

Session::Session(assets::GameFiles files, std::unique_ptr<LoadedLevel> loaded, SharedGraphics shared, int slot,
                 Rules rules, audio::SoundData sound, std::optional<audio::SoundData> modern,
                 std::unique_ptr<audio::GmSynth> gm)
    : files_{std::move(files)},
      shared_{std::move(shared)},
      play_{files_, std::move(loaded), slot, rules},
      sound_{std::move(sound)},
      modern_sound_{std::move(modern)},
      gm_{std::move(gm)} {
    opl_synth_ = std::make_unique<audio::OplSynth>(sound_.opl, chip_);
    classic_ = std::make_unique<audio::Player>(sound_, *opl_synth_, opl_synth_.get(), true, true);
    if (gm_ != nullptr && modern_sound_) {
        gm_effects_ = gm_->copy();
        if (gm_effects_ != nullptr) {
            modern_ = std::make_unique<audio::Player>(*modern_sound_, *gm_, nullptr, true, false);
            modern_effects_ = std::make_unique<audio::Player>(*modern_sound_, *gm_effects_, nullptr, false, true,
                                                              audio::Player::kMostEffects);
        } else {  // no second synthesizer: one player for both, as the original
            modern_ = std::make_unique<audio::Player>(*modern_sound_, *gm_, nullptr, true, true);
        }
    }
}

namespace {

// The views the zoom steps through: the original's, with the HUD under it; then one and a half times and twice the
// original's whole screen (320 x 240), with the HUD laid over the view's bottom. And how many pictures a change
// takes (a third of a second).
constexpr std::array<std::array<int, 2>, 3> kZoomViews{{{320, 176}, {480, 360}, {640, 480}}};
constexpr int kZoomPictures = 20;
// The rewind reaches this far back: in seconds, and in steps (a step is a tick: three refreshes of the display,
// which shows 59.7 a second).
constexpr std::size_t kRewindSeconds = 10;
constexpr std::size_t kRewindSteps = 199;

// From `from` to `to`, slow at both ends.
Screen::Part between(const Screen::Part& from, const Screen::Part& to, int pictures) {
    const float linear = static_cast<float>(std::min(pictures, kZoomPictures)) / static_cast<float>(kZoomPictures);
    const float t = linear * linear * (3.0F - (2.0F * linear));
    const auto mix = [t](float a, float b) { return a + ((b - a) * t); };
    return {.x = mix(from.x, to.x),
            .y = mix(from.y, to.y),
            .width = mix(from.width, to.width),
            .height = mix(from.height, to.height)};
}

}  // namespace

bool Session::zoom(int by) noexcept {
    if (!takes_play_keys()) {
        return false;
    }
    zoom_ = std::clamp(zoom_ + by, 0, static_cast<int>(kZoomViews.size()) - 1);
    play_.view = kZoomViews[static_cast<std::size_t>(zoom_)];  // (clamped above)
    return true;
}

// The view the zoom asks for here.
std::array<int, 2> Session::wanted_view() const {
    return play_.world.view_for(kZoomViews.at(static_cast<std::size_t>(zoom_)));
}

// Before a step: the world's view becomes the wanted one. A larger one at once (its pictures then open up from
// the old view); a smaller one when the pictures have closed in on it.
void Session::change_view() {
    World& world = play_.world;
    const auto [width, height] = wanted_view();
    const Camera& camera = world.camera;
    if (width == camera.view_width && height == camera.view_height) {
        shrinking_ = false;
        return;
    }
    if (width > camera.view_width || height > camera.view_height) {
        grows_from_ = Screen::Part{.x = static_cast<float>(camera.x),
                                   .y = static_cast<float>(camera.y),
                                   .width = static_cast<float>(camera.view_width),
                                   .height = static_cast<float>(camera.view_height)};
        world.set_view(width, height);
        zoom_pictures_ = 0;
        shrinking_ = false;
    } else if (!shrinking_) {
        shrinking_ = true;
        grows_from_.reset();
        zoom_pictures_ = 0;
    } else if (zoom_pictures_ >= kZoomPictures) {
        world.set_view(width, height);
        shrinking_ = false;
    }
}

// The part of this picture's playfield that is shown: all of it, or on the way between two views.
Screen::Part Session::part_shown(const View& view) {
    const Screen::Part whole{
        .x = 0, .y = 0, .width = static_cast<float>(view.width), .height = static_cast<float>(view.height)};
    Screen::Part part = whole;
    if (grows_from_) {
        const Screen::Part from{.x = grows_from_->x - static_cast<float>(view.camera_x),
                                .y = grows_from_->y - static_cast<float>(view.camera_y),
                                .width = grows_from_->width,
                                .height = grows_from_->height};
        part = between(from, whole, ++zoom_pictures_);
        if (zoom_pictures_ >= kZoomPictures) {
            grows_from_.reset();
        }
    } else if (shrinking_) {
        const auto [width, height] = wanted_view();
        const Camera to = play_.world.camera_for_view(width, height);
        part = between(whole,
                       {.x = static_cast<float>(to.x - view.camera_x),
                        .y = static_cast<float>(to.y - view.camera_y),
                        .width = static_cast<float>(width),
                        .height = static_cast<float>(height)},
                       ++zoom_pictures_);
    }
    part.width = std::min(part.width, whole.width);
    part.height = std::min(part.height, whole.height);
    part.x = std::clamp(part.x, 0.0F, whole.width - part.width);
    part.y = std::clamp(part.y, 0.0F, whole.height - part.height);
    return part;
}

std::expected<void, std::string> Session::use_profile(const std::filesystem::path& folder) {
    if (play_.world.rules != Rules::kRemaster) {
        return {};
    }
    profile_ = folder;
    auto seen = load_seen(folder);
    if (!seen) {
        return std::unexpected{seen.error()};
    }
    // What this run has seen so far stays, with what was kept added to it where the size is the same.
    for (auto& [level, kept] : *seen) {
        SeenLevel& now = seen_[level];
        if (now.seen.size() != kept.seen.size() || now.width != kept.width) {
            now = std::move(kept);
            continue;
        }
        for (std::size_t pixel = 0; pixel < now.seen.size(); ++pixel) {
            now.seen[pixel] = now.seen[pixel] || kept.seen[pixel];
        }
    }
    update_minimap();
    return read_quicksave();
}

std::expected<void, std::string> Session::write_quicksave() const {
    if (profile_.empty() || !quick_) {
        return {};
    }
    SavedPlay play{.remaster = quick_->world.rules == Rules::kRemaster,
                   .scene = quick_->world.scene,
                   .database_entry = quick_->loaded->database_entry,
                   .world = {}};
    Writer to{play.world};
    quick_->world.save(to);
    return save_quicksave(profile_, play);
}

// The quicksave's file: its level is loaded again, a world made for it, and what was written read into that.
std::expected<void, std::string> Session::read_quicksave() {
    auto kept = load_quicksave(profile_);
    if (!kept) {
        return std::unexpected{kept.error()};
    }
    if (!*kept) {
        return {};
    }
    const SavedPlay& play = **kept;
    const Rules rules = play.remaster ? Rules::kRemaster : Rules::kVanilla;
    if (rules != play_.world.rules || play.scene < 0 || play.scene >= kSceneSlots) {
        return std::unexpected{std::string{"the quicksave is of another game than this"}};
    }
    auto loaded = load_slot(files_, play.scene, play.database_entry, play_.snes);
    if (!loaded) {
        return std::unexpected{loaded.error()};
    }
    Play::Snapshot snapshot;
    snapshot.owner = std::make_shared<LoadedLevel>(std::move(*loaded));
    snapshot.loaded = snapshot.owner.get();
    snapshot.world = prepare_world(*snapshot.loaded, nullptr, rules);
    Reader from{play.world};
    if (!snapshot.world.load(from) || !from.done()) {
        return std::unexpected{
            std::string{"the quicksave was written by another version of the game: it can't be read"}};
    }
    quick_ = std::move(snapshot);
    return {};
}

std::expected<void, std::string> Session::save_profile() const {
    if (profile_.empty()) {
        return {};
    }
    return save_seen(profile_, seen_);
}

// After a step: what the view shows now is seen, and the minimap is drawn again.
void Session::update_minimap() {
    const World& world = play_.world;
    if (world.rules != Rules::kRemaster || world.scene > scene::kLastLevel || !world.has_vikings ||
        !play_.loaded().level.hud || world.map.cells.empty()) {
        minimap_ = {};
        return;
    }
    SeenLevel& seen = seen_[world.level];
    seen.width = world.map.width * kMinimapCell;
    seen.height = world.map.height * kMinimapCell;
    reveal(seen.seen, world);  // (what was kept for a map of another size is dropped there)
    minimap_ = draw_minimap(world, seen.seen);
}

std::vector<Screen> Session::numbered(std::vector<Screen> shown) {
    for (Screen& screen : shown) {
        screen.refresh = refreshes_++;
    }
    return shown;
}

Screen Session::picture() {
    const LoadedLevel& loaded = play_.loaded();
    // (The painter is the same from scene to scene: the original never clears the buffer its packed frames are
    // unpacked into, so a piece that hasn't had a frame of its own yet shows what the last one left there.)
    const World& world = play_.shown();
    View view = view_of(world, loaded, painter_, pictures_);
    if (play_.fade) {  // between scenes every colour is darkened
        view.darken = {*play_.fade, *play_.fade, *play_.fade};
        view.darken_all = true;
    }
    Screen screen;
    draw_(screen, view, play_.graphics(), shared_);
    screen.part = part_shown(view);
    screen.level_x = view.camera_x;
    screen.level_y = view.camera_y;
    screen.fading = play_.fade.has_value();
    screen.motion.scroll = {world.camera.x, world.camera.y};
    screen.motion.view = {view.width, view.height};
    screen.motion.sprites.reserve(view.sprites.size());
    for (const DrawnSprite& sprite : view.sprites) {
        screen.motion.sprites.push_back(sprite.moves);
    }
    for (int who = 0; who < kVikingSlots && world.has_vikings; ++who) {
        if (const Entity* viking = world.at(who); viking != nullptr && !viking->dead) {
            screen.vikings.at(static_cast<std::size_t>(who)) = {
                .x = viking->x, .y = viking->y, .there = true, .played = who == world.current_viking};
        }
    }
    return screen;
}

void Session::hear(const std::vector<SoundCue>& asked) {
    for (const SoundCue& cue : asked) {
        switch (cue.kind) {
            case SoundCue::Kind::kEffect:
                effect_players([&cue](audio::Player& p) { p.play_effect(cue.value); });
                break;
            case SoundCue::Kind::kStopEffect:
                effect_players([&cue](audio::Player& p) { p.stop_effect(cue.value); });
                break;
            case SoundCue::Kind::kMusicAction:
                music_players([&cue](audio::Player& p) { p.music_action(cue.value, cue.tune); });
                break;
            case SoundCue::Kind::kRestartMusic:
                music_players([](audio::Player& p) { p.restart_music(); });
                break;
            case SoundCue::Kind::kFadeMusic:
                music_players([](audio::Player& p) { p.fade_music(); });
                break;
            case SoundCue::Kind::kStopAll:
                players([&cue](audio::Player& p) { p.stop_all(cue.value != 0); });
                break;
            case SoundCue::Kind::kEffectsOn:
                effect_players([&cue](audio::Player& p) { p.set_effects(cue.value != 0); });
                break;
            case SoundCue::Kind::kMusicOn:
                music_players([&cue](audio::Player& p) { p.set_music(cue.value != 0); });
                break;
        }
    }
}

void Session::refresh_sound() {
    half_refresh_sound(samples);
    half_refresh_sound(samples);
}

void Session::sound_of_refresh(std::vector<SoundCue> asked) {
    if (live_) {
        cues.push_back(std::move(asked));
        return;
    }
    hear(asked);
    refresh_sound();
}

void Session::hear_now(const std::vector<SoundCue>& asked) {
    const std::scoped_lock lock{sound_mutex_};
    hear(asked);
}

void Session::render_live(std::size_t frames, std::vector<std::int16_t>& out) {
    const std::scoped_lock lock{sound_mutex_};
    if (rewinding_) {
        // Going back: what was heard, backwards (the last frame first, left before right as ever) and at twice
        // the speed, as the pictures go: every second frame. Silence when that is used up. The players hold still
        // meanwhile.
        for (std::size_t frame = 0; frame < frames; ++frame) {
            if (heard_.size() >= 2) {
                out.push_back(heard_[heard_.size() - 2]);
                out.push_back(heard_.back());
                heard_.resize(heard_.size() - std::min<std::size_t>(heard_.size(), 4));
            } else {
                out.insert(out.end(), 2, std::int16_t{0});
            }
        }
        return;
    }
    // Half a refresh at a time, as the driver is serviced; what is made past the asking waits for the next.
    while (rendered_.size() < frames * 2) {
        half_refresh_sound(rendered_);
    }
    const auto end = rendered_.begin() + static_cast<std::ptrdiff_t>(frames * 2);
    out.insert(out.end(), rendered_.begin(), end);
    // Kept for going back, the last ten seconds of it.
    const std::size_t kept = std::size_t{sample_rate()} * kRewindSeconds * 2;
    heard_.insert(heard_.end(), rendered_.begin(), end);
    if (heard_.size() > kept + (kept / 4)) {  // (trimmed in one go now and then, not at every call)
        heard_.erase(heard_.begin(), heard_.end() - static_cast<std::ptrdiff_t>(kept));
    }
    rendered_.erase(rendered_.begin(), end);
}

// A level is being played: one of the 37 (not a cutscene, the village, the title or the game-over scene), not a
// demo, and no menu is open.
bool Session::level_played() const {
    const World& world = play_.world;
    return world.scene <= scene::kLastLevel && world.has_vikings && play_.loaded().level.hud &&
           !world.map.cells.empty() && !world.attract && !play_.playing_demo() && !play_.in_menu();
}

std::vector<Screen> Session::back_to(const Kept& step, bool& another) {
    another = play_.restore(step.before) || another;
    // The step again, for its pictures only: what it asks of the sound isn't heard, and the play is put back.
    const bool ended = play_.ended;
    std::vector<Screen> shown;
    play_.menu_keys = step.keys;
    const int refreshes = play_.step(step.keyboard, [&] { shown.push_back(picture()); });
    const bool plain = refreshes == 3 && shown.size() == 3 && !play_.scene_loaded && !play_.in_menu();
    (void)play_.restore(step.before);  // (the same scene as a moment ago)
    play_.ended = ended;
    play_.scene_loaded = false;
    if (!plain) {
        shown.clear();
    }
    return shown;
}

// A step back in time, at twice the speed it went forwards: the last two steps that were kept are undone, and of
// their six pictures, backwards, every second one is shown: the first of the later step, the second of the
// earlier one, and the play as it was before that. (With one step left, or a step that was more than a plain
// tick, the pictures there are.)
std::vector<Screen> Session::step_back() {
    bool another = false;
    std::vector<Screen> shown;
    const std::vector<Screen> later = back_to(rewind_.back(), another);
    if (rewind_.size() >= 2) {
        if (!later.empty()) {
            shown.push_back(later.at(0));
        }
        rewind_.pop_back();
        const std::vector<Screen> earlier = back_to(rewind_.back(), another);
        if (!earlier.empty()) {
            shown.push_back(earlier.at(1));
        }
    } else if (!later.empty()) {
        shown.push_back(later.at(1));
        shown.push_back(later.at(0));
    }
    rewind_.pop_back();
    if (another) {
        play_.scene_sound();
    }
    const Screen now = picture();
    shown.resize(2, now);  // (a picture that couldn't be made again: the play as it is now instead)
    shown.push_back(now);
    for (Screen& screen : shown) {
        screen.rewinding = true;
    }
    cues.clear();
    for (std::size_t refresh = 0; refresh < shown.size(); ++refresh) {
        // (Another scene's music, if this went back into one, with the first.)
        sound_of_refresh(refresh == 0 ? std::exchange(play_.world.cues, {}) : std::vector<SoundCue>{});
    }
    last_ = now;
    update_minimap();
    return shown;
}

// Half a display refresh of the sound: the driver serviced once (twice a refresh, docs/original/timing.md#audio),
// then the chip and the synthesizers rendering that long.
void Session::half_refresh_sound(std::vector<std::int16_t>& out) {
    players([](audio::Player& p) { p.service(); });
    chip_.render(0.5 / kDisplayHz, modern_on_ ? unheard_ : out);
    if (gm_ != nullptr) {
        std::vector<std::int16_t>& heard = modern_on_ ? out : unheard_;
        const std::size_t from = heard.size();
        gm_->render(0.5 / kDisplayHz, heard);
        if (gm_effects_ != nullptr) {  // the effects' synthesizer, mixed in
            effects_mix_.clear();
            gm_effects_->render(0.5 / kDisplayHz, effects_mix_);
            for (std::size_t i = 0; i < effects_mix_.size() && from + i < heard.size(); ++i) {
                heard[from + i] =
                    static_cast<std::int16_t>(std::clamp(heard[from + i] + effects_mix_[i], -32768, 32767));
            }
        }
    }
    unheard_.clear();
}

// A step that shows fewer pictures than refreshes starts with the last picture shown before it; one that shows
// more keeps its last ones (what the pictures that aren't shown asked of the sound is heard with the first that
// is). The same rule as the pixel-match harness's player (src/replay/player.cpp).
std::vector<Screen> Session::step(std::uint16_t keyboard, Play::MenuKeys keys) {
    struct Frame {
        Screen screen;
        std::vector<SoundCue> cues;
    };
    std::vector<Frame> frames;
    const auto take_cues = [this] {
        std::vector<SoundCue> asked = std::move(play_.world.cues);
        play_.world.cues.clear();
        return asked;
    };
    play_.scene_loaded = false;
    // The quicksave, with the cheat on only: kept in a level, brought back from anywhere.
    const bool remaster = play_.world.rules == Rules::kRemaster;
    if (std::exchange(save_wanted_, false) && remaster && play_.world.title.cheat && level_played()) {
        quick_ = play_.snapshot();
        (void)write_quicksave();  // (a profile that can't be written is said when the game is closed)
    }
    if (std::exchange(load_wanted_, false) && remaster && play_.world.title.cheat && quick_ && !play_.in_menu()) {
        if (play_.restore(*quick_)) {
            play_.scene_sound();
        }
        play_.world.title.cheat = true;
        rewind_.clear();  // what led up to here is no longer what came before
        scrolled_.reset();
        grows_from_.reset();  // (brought back at the view the zoom has: no change of view to show)
        shrinking_ = false;
    }
    // The rewind: back while it is held and there is something to go back to. With nothing left it is over, and
    // the play goes on until the key is let go and held again. Else the play before this step is kept, the last
    // ten seconds of them.
    if (!rewind_held_) {
        rewind_over_ = false;
    }
    // Only in a level, and from the game-over scene back into the one that was lost: what was kept of a level is
    // let go anywhere else (the title, a cutscene, a demo), so that no other scene goes back into it. (Not in a
    // level's pause menu: the play goes on after it.)
    const bool game_over = play_.world.scene == scene::kGameOver && !play_.in_menu();
    if (!level_played() && !game_over && !play_.in_menu()) {
        rewind_.clear();
    }
    const bool back = rewind_held_ && !rewind_over_ && remaster && (level_played() || game_over);
    if (back && rewind_.empty()) {
        rewind_over_ = true;
    }
    {
        const std::scoped_lock lock{sound_mutex_};
        rewinding_ = back && !rewind_.empty();
    }
    if (rewinding_) {
        scrolled_.reset();    // (the pictures going back aren't moved between)
        grows_from_.reset();  // (nor is the view changed: each step is brought back at the zoom's view)
        shrinking_ = false;
        return numbered(step_back());
    }
    if (remaster && level_played()) {
        rewind_.push_back({.before = play_.snapshot(), .keyboard = keyboard, .keys = keys});
        if (rewind_.size() > kRewindSteps) {
            rewind_.pop_front();
        }
    }
    cues.clear();
    play_.menu_keys = keys;
    change_view();
    const int refreshes = play_.step(keyboard, [&] { frames.push_back({.screen = picture(), .cues = take_cues()}); });
    const std::vector<SoundCue> late = take_cues();  // asked after the step's last picture: heard with it
    const auto wanted = static_cast<std::size_t>(refreshes);
    const std::size_t first = frames.size() > wanted ? frames.size() - wanted : 0;
    std::vector<SoundCue> early = std::move(carried_);
    carried_.clear();
    for (std::size_t i = 0; i < first; ++i) {
        early.insert(early.end(), frames[i].cues.begin(), frames[i].cues.end());
    }
    std::vector<Screen> shown;
    const bool tick = refreshes == 3 && frames.size() == 3 && !play_.scene_loaded && !play_.in_menu();
    if (tick && remaster) {
        // Smooth movement: the tick's three pictures, and where the camera was at the end of the last one.
        const Motion& end = frames.back().screen.motion;
        for (int phase = 0; phase < 3; ++phase) {
            Motion& motion = frames.at(static_cast<std::size_t>(phase)).screen.motion;
            motion.phase = phase;
            motion.scroll_to = end.scroll;
            if (scrolled_ && scrolled_->view == end.view) {
                motion.scroll_from = scrolled_->scroll;
            }
        }
        scrolled_ = Scrolled{.scroll = end.scroll, .view = end.view};
    } else {
        scrolled_.reset();
    }
    if (last_.has_value()) {
        Screen before = *last_;
        before.motion.phase = -1;  // shown again: as it was at the end of its tick, not moved back into it
        for (std::size_t padded = frames.size() - first; padded < wanted; ++padded) {
            sound_of_refresh(std::exchange(early, {}));
            shown.push_back(before);
        }
    }
    for (std::size_t i = first; i < frames.size(); ++i) {
        std::vector<SoundCue> asked = std::exchange(early, {});
        asked.insert(asked.end(), frames[i].cues.begin(), frames[i].cues.end());
        if (i + 1 == frames.size()) {
            asked.insert(asked.end(), late.begin(), late.end());
        }
        sound_of_refresh(std::move(asked));
        shown.push_back(std::move(frames[i].screen));
    }
    if (wanted == 0) {  // a step without a refresh (a menu opening): its sounds are heard with the next
        carried_ = std::move(early);
        carried_.insert(carried_.end(), late.begin(), late.end());
        if (!frames.empty()) {
            last_ = std::move(frames.back().screen);
        }
    } else if (!shown.empty()) {
        last_ = shown.back();
    }
    update_minimap();
    if (play_.scene_loaded) {  // a good moment to write the profile: nothing moves
        (void)save_profile();
    }
    return numbered(std::move(shown));
}

}  // namespace lvr
