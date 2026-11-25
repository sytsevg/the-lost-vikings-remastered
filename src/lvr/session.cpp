#include "lvr/session.hpp"

#include <algorithm>
#include <format>
#include <print>
#include <utility>

#include "lvr/names.hpp"

namespace lvr {

namespace {

// The original's level start takes this many refreshes: the fade out (70), the start's tick (3 and one more draw),
// the fade in (71).
constexpr std::size_t kStartRefreshes = 145;

}  // namespace

std::expected<std::unique_ptr<Session>, std::string> Session::open(const std::filesystem::path& dir, int slot,
                                                                   const SessionSound& sound, Rules rules,
                                                                   Draw draw_with, const Online& online, int player) {
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
                                                 std::move(modern), std::move(gm), online}};
    session->player_ = static_cast<std::uint8_t>(std::clamp(player, 0, kMostPlayers - 1));
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
                 std::unique_ptr<audio::GmSynth> gm, const Online& online)
    : files_{std::move(files)},
      shared_{std::move(shared)},
      play_{files_, std::move(loaded), slot, rules, online},
      sound_{std::move(sound)},
      modern_sound_{std::move(modern)},
      gm_{std::move(gm)} {
    opl_synth_ = std::make_unique<audio::OplSynth>(sound_.opl, chip_);
    classic_ = std::make_unique<audio::Player>(sound_, *opl_synth_, opl_synth_.get(), true, true);
    if (rules == Rules::kRemaster) {
        // The Classic sound's fire jets' roar is very loud (the user): under Remaster rules at 60% of its loudness,
        // which the chip gives at 87% of the volume (measured: its levels are steps of a decibel and less, not
        // shares). Vanilla rules' is the original's.
        constexpr int kRoarVolume = 87;
        classic_->set_effect_loudness(effect::kRoar, kRoarVolume);
    }
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
constexpr std::array<std::array<int, 2>, Session::kZoomSteps> kZoomViews{{{320, 176}, {480, 360}, {640, 480}}};
constexpr int kZoomPictures = 20;
// The rewind reaches this far back, in seconds (Session::kRewindSteps: a step is a tick, three refreshes of the
// display, which shows 59.7 a second).
constexpr std::size_t kRewindSeconds = 10;

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

bool Session::zoom(int by) {
    if (!takes_play_keys()) {
        return false;
    }
    zoom_ = std::clamp(zoom_ + by, 0, static_cast<int>(kZoomViews.size()) - 1);
    play_.choose_view(player_, kZoomViews[static_cast<std::size_t>(zoom_)]);  // (clamped above)
    return true;
}

void Session::set_zoom_level(int level) {
    if (play_.world.rules != Rules::kRemaster) {
        return;
    }
    zoom_ = std::clamp(level, 0, static_cast<int>(kZoomViews.size()) - 1);
    play_.choose_view(player_, kZoomViews[static_cast<std::size_t>(zoom_)]);  // (clamped above)
}

void Session::set_volume(int music, int effects) {
    const std::scoped_lock lock{sound_mutex_};
    const auto percent = [](int volume) { return std::clamp(volume, 0, kLoudest) * 100 / kLoudest; };
    players([&](audio::Player& p) { p.set_loudness(percent(music), percent(effects)); });
}

// The view the zoom asks for here.
std::array<int, 2> Session::wanted_view() const {
    return whole_view_ ? *whole_view_ : play_.world.view_for(player_, kZoomViews.at(static_cast<std::size_t>(zoom_)));
}

bool Session::hold_view(int x, int y, int width, int height) {
    if (!takes_play_keys()) {
        return false;
    }
    whole_view_ = {width, height + kHudRows};
    play_.world.hold_camera(x, y, width, height + kHudRows);
    grows_from_.reset();
    shrinking_ = false;
    scrolled_.reset();
    return true;
}

bool Session::put_viking(int viking, int x, int y, std::uint16_t health) {
    if (!takes_play_keys() || viking < 0 || viking >= kVikingSlots) {
        return false;
    }
    Entity* entity = play_.world.entities.at(static_cast<std::size_t>(viking)).get();
    if (entity == nullptr || entity->dead) {
        return false;
    }
    entity->move_to(x, y);
    entity->health = health;
    return true;
}

bool Session::go_to(int slot) {
    if (!level_played() || slot < 0 || slot > scene::kLastLevel) {
        return false;
    }
    play_.world.next_scene = slot;
    play_.world.level_done = true;
    whole_view_.reset();
    return true;
}

bool Session::view_whole_level() {
    if (!takes_play_keys()) {
        return false;
    }
    World& world = play_.world;
    whole_view_ = {world.map.pixel_width(), world.map.pixel_height() + kHudRows};
    world.set_view(player_, (*whole_view_)[0], (*whole_view_)[1]);
    grows_from_.reset();
    shrinking_ = false;
    return true;
}

// Before a step: the world's view becomes the wanted one. A larger one at once (its pictures then open up from
// the old view); a smaller one when the pictures have closed in on it. Given to the step as the player's view
// (StepInput: online every machine has it at the same step), or `at_once` set on the world, which is held (the
// menu: nothing steps).
void Session::change_view(bool at_once) {
    World& world = play_.world;
    const auto [width, height] = wanted_view();
    const Camera& camera = world.players.at(static_cast<std::size_t>(player_)).camera;
    if (width == camera.view_width && height == camera.view_height) {
        shrinking_ = false;
        return;
    }
    if (width > camera.view_width || height > camera.view_height) {
        grows_from_ = Screen::Part{.x = static_cast<float>(camera.x),
                                   .y = static_cast<float>(camera.y),
                                   .width = static_cast<float>(camera.view_width),
                                   .height = static_cast<float>(camera.view_height)};
        set_view(width, height, at_once);
        zoom_pictures_ = 0;
        shrinking_ = false;
    } else if (!shrinking_) {
        shrinking_ = true;
        grows_from_.reset();
        zoom_pictures_ = 0;
    } else if (zoom_pictures_ >= kZoomPictures) {
        set_view(width, height, at_once);
        shrinking_ = false;
    }
}

void Session::set_view(int width, int height, bool at_once) {
    if (at_once) {
        play_.world.set_view(player_, width, height);
    } else {
        view_now_ = {width, height};
    }
}

StepInput Session::input_of(std::uint16_t keyboard, const Play::MenuKeys& keys, Pace pace) {
    StepInput input;
    input.menu = keys;
    input.typed = std::exchange(typed_, 0);
    PlayerInput& own = input.players.at(player_);  // this machine's player's (offline: the only one, the first)
    own.keys = keyboard;
    own.pace = pace;
    const auto& chosen = kZoomViews.at(static_cast<std::size_t>(zoom_));
    own.zoom = {static_cast<std::int16_t>(chosen[0]), static_cast<std::int16_t>(chosen[1])};
    if (view_now_) {
        own.view = {static_cast<std::int16_t>((*view_now_)[0]), static_cast<std::int16_t>((*view_now_)[1])};
        view_now_.reset();
    }
    return input;
}

void Session::zoom_back() {
    play_.zoom_back(player_, kZoomViews.at(static_cast<std::size_t>(zoom_)));
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
        const Camera to = play_.world.camera_for_view(player_, width, height);
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
void Session::fade_in() noexcept {
    fade_in_left_ = static_cast<std::uint8_t>(kFadeInRefreshes);
}

void Session::drop_carried_effects() {
    std::erase_if(carried_, [](const SoundCue& cue) {
        return cue.kind == SoundCue::Kind::kEffect || cue.kind == SoundCue::Kind::kStopEffect;
    });
}

std::vector<std::uint8_t> Session::state_bytes(const Play::Snapshot& snapshot) {
    std::vector<std::uint8_t> bytes;
    Writer to{bytes, Widths::kVariable};  // (for the network: mostly small numbers)
    to(kStateVersion);
    to(snapshot.world.scene);
    to(snapshot.loaded != nullptr ? snapshot.loaded->database_entry : 0);
    to.measured([&snapshot](Writer& part) { snapshot.world.save(part); });
    to.measured([&snapshot](Writer& part) { part(snapshot.state); });
    return bytes;
}

std::optional<int> Session::scene_of(std::span<const std::uint8_t> bytes) {
    Reader from{bytes, Widths::kVariable};
    std::uint32_t version = 0;
    int scene = 0;
    from(version);
    from(scene);
    return from.ok() && version == kStateVersion && scene >= 0 && scene < kSceneSlots ? std::optional{scene}
                                                                                      : std::nullopt;
}

std::expected<void, std::string> Session::take_state(std::span<const std::uint8_t> bytes) {
    Reader from{bytes, Widths::kVariable};
    std::uint32_t version = 0;
    int scene = 0;
    int database_entry = 0;
    from(version);
    from(scene);
    from(database_entry);
    if (!from.ok() || version != kStateVersion || scene < 0 || scene >= kSceneSlots) {
        return std::unexpected{std::string{"the game's state is of another version of the game"}};
    }
    auto loaded = load_slot(files_, scene, database_entry, play_.snes);
    if (!loaded) {
        return std::unexpected{loaded.error()};
    }
    Play::Snapshot snapshot;
    snapshot.owner = std::make_shared<LoadedLevel>(std::move(*loaded));
    snapshot.loaded = snapshot.owner.get();
    snapshot.world = prepare_world(*snapshot.loaded, nullptr, play_.world.rules);
    bool read = false;
    from.measured([&](Reader& part) { read = snapshot.world.load(part) && part.done(); });
    from.measured([&](Reader& part) { part(snapshot.state); });
    if (!read || !from.done()) {
        return std::unexpected{std::string{"the game's state can't be read"}};
    }
    if (play_.restore(snapshot)) {
        play_.scene_sound();
    }
    start_frames.clear();  // (the pictures of the scene's start, as it was opened: not the game under way)
    cues.clear();
    rewind_.clear();
    effects_.clear();
    scrolled_.reset();
    grows_from_.reset();
    shrinking_ = false;
    return {};
}

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
    reveal(seen.seen, world, player_);  // (what was kept for a map of another size is dropped there)
    minimap_ = draw_minimap(world, seen.seen);
}

std::vector<Screen> Session::numbered(std::vector<Screen> shown) {
    for (Screen& screen : shown) {
        screen.refresh = refreshes_++;
    }
    return shown;
}

Screen Session::picture(bool dimmed) {
    const LoadedLevel& loaded = play_.loaded();
    // (The painter is the same from scene to scene: the original never clears the buffer its packed frames are
    // unpacked into, so a piece that hasn't had a frame of its own yet shows what the last one left there.)
    const World& world = play_.shown();
    View view = view_of(world, loaded, painter_, pictures_, player_);
    if (view.hud && world.online) {
        // Under each portrait the name of the player who plays him; above it those watching, in their line's order,
        // as one name: A/B (ADR-0012; the user: two rows of them didn't fit).
        for (int viking = 0; viking < kVikingSlots; ++viking) {
            if (const int who = world.player_of(viking); who >= 0 && std::cmp_less(who, player_names_.size())) {
                view.hud->names.at(static_cast<std::size_t>(viking)) = player_names_.at(static_cast<std::size_t>(who));
            }
            std::vector<std::pair<std::uint16_t, int>> line;
            for (int who = 0; who < world.player_count; ++who) {
                const Player& player = world.players.at(static_cast<std::size_t>(who));
                if (!player.controls && player.viking == viking && std::cmp_less(who, player_names_.size())) {
                    line.emplace_back(player.in_line, who);
                }
            }
            std::ranges::sort(line);
            std::string watching;
            for (const auto& [place, who] : line) {
                watching += (watching.empty() ? "" : "/") + player_names_.at(static_cast<std::size_t>(who));
            }
            if (!watching.empty()) {
                view.hud->watching.at(static_cast<std::size_t>(viking)).push_back(std::move(watching));
            }
        }
    }
    if ((dimmed || dimmed_) && view.darken == std::array<std::uint8_t, 3>{}) {
        view.darken = kMenuTint;
    }
    if (play_.fade) {  // between scenes every colour is darkened
        view.darken = {*play_.fade, *play_.fade, *play_.fade};
        view.darken_all = true;
    } else if (fade_in_left_ > 0) {  // (one joining under way: the game as it is, coming in)
        constexpr int kDarkest = 63;
        const auto dark = static_cast<std::uint8_t>(kDarkest * fade_in_left_ / kFadeInRefreshes);
        view.darken = {dark, dark, dark};
        view.darken_all = true;
        --fade_in_left_;
    }
    Screen screen;
    if (effects_on_) {
        // (While a scene fades every colour is that much darker, of the VGA's 63 levels a colour: the lights too.)
        constexpr float kLevels = 63;
        const float brightness =
            play_.fade ? std::max(0.0F, kLevels - static_cast<float>(*play_.fade)) / kLevels : 1.0F;
        // (Under the original's pause box or quit box the world holds, and so does what it gives off: the user.)
        effects_.add(world, play_.graphics(), view, screen, moving_ && !play_.fade && !play_.in_menu(), brightness);
    }
    draw_(screen, view, play_.graphics(), shared_);
    if (view.hud && gauge_shown(world) && !play_.fade) {  // (not laid over a scene's fades, as the minimap isn't)
        // (A scene that was just started, still in its first step: all of its rewind, as the next step has it.)
        const int left = play_.scenes_started == rewind_scene_ ? rewind_left_ : kRewindSteps;
        screen.rewind_left = static_cast<float>(left) / static_cast<float>(kRewindSteps);
    }
    screen.part = part_shown(view);
    screen.level_x = view.camera_x;
    screen.level_y = view.camera_y;
    screen.fading = play_.fade.has_value();
    const Camera& camera = world.players.at(static_cast<std::size_t>(player_)).camera;
    screen.motion.scroll = {camera.x, camera.y};
    screen.motion.view = {view.width, view.height};
    screen.motion.sprites.reserve(view.sprites.size());
    for (const DrawnSprite& sprite : view.sprites) {
        screen.motion.sprites.push_back(sprite.moves);
    }
    for (int who = 0; who < kVikingSlots && world.has_vikings; ++who) {
        if (const Entity* viking = world.at(who); viking != nullptr && !viking->dead) {
            screen.vikings.at(static_cast<std::size_t>(who)) = {
                .x = viking->x,
                .y = viking->y,
                .there = true,
                .played = who == world.followed_by(world.players.at(static_cast<std::size_t>(player_)))};
        }
    }
    return screen;
}

void Session::hear(const std::vector<SoundCue>& asked) {
    for (const SoundCue& cue : asked) {
        if (cue.heard_by >= 0 && std::cmp_not_equal(cue.heard_by, player_)) {
            continue;  // online: another player's alone (his inventory's)
        }
        switch (cue.kind) {
            case SoundCue::Kind::kEffect:
                classic_->play_effect(cue.value);  // (the original's: as it is)
                if (modern_effects_ != nullptr) {
                    modern_effects_->play_effect(cue.value, placed(cue));
                }
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

audio::Placement Session::placed(const SoundCue& cue) const {
    // How far to either side the view's edges are heard (of 63): not all the way, as nothing on screen is that far
    // off to one ear.
    constexpr int kEdge = 48;
    const Camera& camera = play_.world.players.at(static_cast<std::size_t>(player_)).camera;
    // One sound for many things across the level (the fire jets' roar, for all that are on): not where the one that
    // started it is.
    if (cue.entity < 0 || cue.value == effect::kRoar || camera.view_width <= 0 || camera.view_height <= 0) {
        return {};
    }
    const int across = std::clamp(cue.x - camera.x, 0, camera.view_width);
    const int pan = (((across * 2) - camera.view_width) * kEdge) / camera.view_width;
    // How far outside the view, the nearer edge's way.
    const int outside = std::max({camera.x - cue.x, cue.x - (camera.x + camera.view_width), camera.y - cue.y,
                                  cue.y - (camera.y + camera.view_height), 0});
    const int fades_over = std::max(camera.view_width / 2, 1);
    return {.pan = pan, .loudness = std::max(0, 100 - (outside * 100 / fades_over))};
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

bool Session::gauge_shown(const World& world) const {
    return world.rules == Rules::kRemaster && world.scene <= scene::kLastLevel && world.has_vikings && !world.attract &&
           !play_.playing_demo() && !world.online;  // (online there is no rewind: ADR-0012)
}

std::vector<Screen> Session::back_to(const Kept& step) {
    (void)play_.restore(step.before);  // (of this scene: what is kept is let go when another starts)
    zoom_back();
    // The step again, for its pictures only: what it asks of the sound isn't heard, and the play is put back.
    const bool ended = play_.ended;
    std::vector<Screen> shown;
    StepInput again = StepInput::one(step.keyboard, step.pace);
    again.menu = step.keys;
    const int refreshes = play_.step(again, [&] { shown.push_back(picture()); });
    const bool plain = refreshes == 3 && shown.size() == 3 && !play_.scene_loaded && !play_.in_menu();
    (void)play_.restore(step.before);  // (the same scene as a moment ago)
    zoom_back();
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
// tick, the pictures there are.) Each step undone is one less of the level's rewind.
std::vector<Screen> Session::step_back() {
    effects_.clear();
    std::vector<Screen> shown;
    const auto undone = [this] {
        rewind_.pop_back();
        --rewind_left_;
    };
    const std::vector<Screen> later = back_to(rewind_.back());
    if (rewind_.size() >= 2) {
        if (!later.empty()) {
            shown.push_back(later.at(0));
        }
        undone();
        const std::vector<Screen> earlier = back_to(rewind_.back());
        if (!earlier.empty()) {
            shown.push_back(earlier.at(1));
        }
    } else if (!later.empty()) {
        shown.push_back(later.at(1));
        shown.push_back(later.at(0));
    }
    undone();
    const Screen now = picture();
    shown.resize(2, now);  // (a picture that couldn't be made again: the play as it is now instead)
    shown.push_back(now);
    for (Screen& screen : shown) {
        screen.rewinding = true;
    }
    cues.clear();
    for (std::size_t refresh = 0; refresh < shown.size(); ++refresh) {
        // (What the play brought back still asked of the sound, with the first: the quicksave's scene's music.)
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

std::vector<Screen> Session::held(bool dimmed) {
    constexpr int kTick = 3;
    scrolled_.reset();  // (the next step's pictures aren't moved from where the camera was before)
    change_view(true);
    cues.clear();
    std::vector<Screen> shown;
    for (int refresh = 0; refresh < kTick; ++refresh) {
        shown.push_back(picture(dimmed));
        cues.emplace_back();
    }
    last_ = shown.back();
    update_minimap();
    return numbered(std::move(shown));
}

// A step that shows fewer pictures than refreshes starts with the last picture shown before it; one that shows
// more keeps its last ones (what the pictures that aren't shown asked of the sound is heard with the first that
// is). The same rule as the pixel-match harness's player (src/replay/player.cpp).
std::vector<Screen> Session::step(std::uint16_t keyboard, Play::MenuKeys keys, Pace pace) {
    play_.scene_loaded = false;
    // The quicksave, with the cheat on only: kept in a level, brought back from anywhere.
    const bool remaster = play_.world.rules == Rules::kRemaster;
    const auto within = [](std::uint8_t part) { return std::clamp(part, std::uint8_t{1}, kFullPace); };
    pace = remaster ? Pace{.across = within(pace.across), .up_down = within(pace.up_down)} : Pace{};
    if (std::exchange(save_wanted_, false) && remaster && play_.world.title.cheat && level_played()) {
        quick_ = play_.snapshot();
        (void)write_quicksave();  // (a profile that can't be written is said when the game is closed)
    }
    if (std::exchange(load_wanted_, false) && remaster && play_.world.title.cheat && quick_ && !play_.in_menu()) {
        if (play_.restore(*quick_)) {
            play_.scene_sound();
        }
        zoom_back();
        play_.world.title.cheat = true;
        rewind_.clear();  // what led up to here is no longer what came before
        effects_.clear();
        scrolled_.reset();
        grows_from_.reset();  // (brought back at the view the zoom has: no change of view to show)
        shrinking_ = false;
    }
    // The rewind, in a level being played only: back while it is held and there is something to go back to. With
    // nothing left (or none of the level's rewind) it is over, and the play goes on until the key is let go and held
    // again. Else the play before this step is kept: as many of the last steps as the level's rewind has left.
    if (!rewind_held_) {
        rewind_over_ = false;
    }
    const bool back = rewind_held_ && !rewind_over_ && remaster && level_played();
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
    if (remaster && level_played() && rewind_left_ > 0) {
        rewind_.push_back({.before = play_.snapshot(), .keyboard = keyboard, .keys = keys, .pace = pace});
        if (std::cmp_greater(rewind_.size(), rewind_left_)) {
            rewind_.pop_front();
        }
    }
    return run(take_input(keyboard, keys, pace));
}

StepInput Session::take_input(std::uint16_t keyboard, Play::MenuKeys keys, Pace pace) {
    const auto within = [](std::uint8_t part) { return std::clamp(part, std::uint8_t{1}, kFullPace); };
    pace = play_.world.rules == Rules::kRemaster ? Pace{.across = within(pace.across), .up_down = within(pace.up_down)}
                                                 : Pace{};
    change_view(false);
    return input_of(keyboard, keys, pace);
}

void Session::run_again(const StepInput& input, std::uint32_t step) {
    std::vector<SoundCue> asked;
    const auto take_cues = [&] {  // (when a shown step takes them: the world comes out the same)
        asked.insert(asked.end(), play_.world.cues.begin(), play_.world.cues.end());
        play_.world.cues.clear();
    };
    (void)play_.step(input, take_cues);
    take_cues();
    const CueLedger::Heard heard = ledger_.heard(step, asked);
    carried_.insert(carried_.end(), heard.play.begin(), heard.play.end());  // heard with the next picture
    for (const SoundCue& undone : heard.undone) {
        if (undone.kind == SoundCue::Kind::kEffect) {
            carried_.push_back({.kind = SoundCue::Kind::kStopEffect,
                                .value = undone.value,
                                .tune = 0,
                                .entity = undone.entity,
                                .x = undone.x,
                                .y = undone.y,
                                .heard_by = undone.heard_by});
        }
    }
}

std::vector<Screen> Session::run(const StepInput& input, std::optional<std::uint32_t> step) {
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
    const bool remaster = play_.world.rules == Rules::kRemaster;
    cues.clear();
    moving_ = true;
    const int refreshes = play_.step(input, [&] { frames.push_back({.screen = picture(), .cues = take_cues()}); });
    moving_ = false;
    // A step that shows fewer pictures than its refreshes (a wait for a key: a text box, a password) shows the last
    // one again for the rest. While the zoom changes the view that held its change up: one stage a step, each shown
    // three times (slow and not smooth, the user). Then the rest are pictures too, the view going on.
    while (std::cmp_less(frames.size(), refreshes) && !play_.scene_loaded && (grows_from_ || shrinking_)) {
        frames.push_back({.screen = picture(), .cues = {}});
    }
    if (play_.scenes_started != rewind_scene_) {
        effects_.clear();
        // A scene started (a level again too): what was kept is of the one before, and the new one has all of its
        // rewind.
        rewind_.clear();
        rewind_left_ = kRewindSteps;
        rewind_scene_ = play_.scenes_started;
    }
    const std::vector<SoundCue> late = take_cues();  // asked after the step's last picture: heard with it
    if (step) {                                      // online: the step's sounds, for a rollback that runs it again
        std::vector<SoundCue> all;
        for (const Frame& frame : frames) {
            all.insert(all.end(), frame.cues.begin(), frame.cues.end());
        }
        all.insert(all.end(), late.begin(), late.end());
        (void)ledger_.heard(*step, all);
    }
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
