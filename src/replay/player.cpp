#include "replay/player.hpp"

#include <format>
#include <utility>

#include "assets/assets.hpp"

namespace lvr::replay {

namespace {

// The original's level start takes this many refreshes: the fade out (70), the start's tick (3 and one more draw),
// the fade in (71).
constexpr std::size_t kStartRefreshes = 145;

}  // namespace

LvrPlayer::LvrPlayer(Replay replay, assets::GameFiles files, std::unique_ptr<LoadedLevel> loaded, SharedGraphics shared,
                     int slot)
    : replay_{std::move(replay)},
      files_{std::move(files)},
      shared_{std::move(shared)},
      play_{files_, std::move(loaded), slot} {
    // The start is shown as its last picture for as long as the original's load takes; its sounds with the last.
    Refresh start = picture();
    for (std::size_t i = 0; i + 1 < kStartRefreshes; ++i) {
        queued_.push_back(start);
    }
    start.sounds = take_sounds();
    last_ = start;
    last_->sounds.clear();
    queued_.push_back(std::move(start));
}

std::expected<std::unique_ptr<LvrPlayer>, std::string> LvrPlayer::open(const std::filesystem::path& original_dir,
                                                                       Replay replay) {
    auto files = assets::GameFiles::open(original_dir);
    if (!files) {
        return std::unexpected{files.error()};
    }
    if (replay.slot >= kSceneSlots) {
        return std::unexpected{std::format("no scene {}", replay.slot)};
    }
    auto loaded = load_slot(*files, replay.slot, database::kSpaceship);
    auto shared = load_shared_graphics(files->archive());
    if (!loaded || !shared) {
        return std::unexpected{!loaded ? loaded.error() : shared.error()};
    }
    const int slot = replay.slot;
    return std::unique_ptr<LvrPlayer>{new LvrPlayer{std::move(replay), std::move(*files),
                                                    std::make_unique<LoadedLevel>(std::move(*loaded)),
                                                    std::move(*shared), slot}};
}

Refresh LvrPlayer::picture() {
    const LoadedLevel& loaded = play_.loaded();
    // (The painter is the same from scene to scene: the original never clears the buffer its packed frames are
    // unpacked into, so a piece that hasn't had a frame of its own yet shows what the last one left there.)
    View view = view_of(play_.world, loaded, painter_, pictures_);
    if (play_.fade) {  // between scenes every colour is darkened
        view.darken = {*play_.fade, *play_.fade, *play_.fade};
        view.darken_all = true;
    }
    draw(screen_, view, play_.graphics(), shared_);
    return Refresh{.pixels = screen_.pixels, .palette = screen_.palette, .sounds = {}};
}

// What the world reported to the sound side since the last time.
std::vector<Sound> LvrPlayer::take_sounds() {
    std::vector<Sound> sounds;
    for (const SoundCue& cue : play_.world.cues) {
        Sound::Kind kind = Sound::Kind::kEffect;
        switch (cue.kind) {
            case SoundCue::Kind::kEffect:
                break;
            case SoundCue::Kind::kStopEffect:
                kind = Sound::Kind::kStopEffect;
                break;
            case SoundCue::Kind::kMusicAction:
                kind = Sound::Kind::kMusicAction;
                break;
            case SoundCue::Kind::kRestartMusic:
                kind = Sound::Kind::kRestartMusic;
                break;
            case SoundCue::Kind::kFadeMusic:
                kind = Sound::Kind::kFadeMusic;
                break;
            case SoundCue::Kind::kStopAll:
                kind = Sound::Kind::kStopAll;
                break;
            case SoundCue::Kind::kEffectsOn:
                kind = Sound::Kind::kEffectsOn;
                break;
            case SoundCue::Kind::kMusicOn:
                kind = Sound::Kind::kMusicOn;
                break;
        }
        sounds.push_back(Sound{.kind = kind, .value = cue.value, .tune = cue.tune});
    }
    play_.world.cues.clear();
    return sounds;
}

// One step of the replay: its pictures as the refreshes it takes. A step that shows fewer pictures than refreshes
// starts with the last picture shown before it; one that shows more keeps its last ones.
void LvrPlayer::play_step() {
    std::vector<Refresh> frames;
    if (const std::optional<std::uint16_t> typed = replay_.steps[step_].typed) {
        play_.world.typed = *typed;
    }
    if (const auto& place = replay_.steps[step_].place) {
        for (std::size_t viking = 0; viking < kVikingSlots; ++viking) {
            if (Entity* entity = play_.world.entities.at(viking).get()) {
                const auto dx = static_cast<std::int16_t>((*place)[0] - entity->x);
                const auto dy = static_cast<std::int16_t>((*place)[1] - entity->y);
                entity->x = (*place)[0];
                entity->y = (*place)[1];
                entity->box.left = static_cast<std::int16_t>(entity->box.left + dx);
                entity->box.right = static_cast<std::int16_t>(entity->box.right + dx);
                entity->box.top = static_cast<std::int16_t>(entity->box.top + dy);
                entity->box.bottom = static_cast<std::int16_t>(entity->box.bottom + dy);
            }
        }
    }
    Play::MenuKeys menu_keys;
    bool alt = false;
    bool x_or_q = false;
    bool s = false;
    bool m = false;
    for (const std::uint8_t scancode : replay_.steps[step_].held) {
        s = s || scancode == 0x1f;
        m = m || scancode == 0x32;
        menu_keys.yes = menu_keys.yes || scancode == 0x15;
        menu_keys.no = menu_keys.no || scancode == 0x31;
        alt = alt || scancode == 0x38;
        x_or_q = x_or_q || scancode == 0x2d || scancode == 0x10;
        menu_keys.quit = menu_keys.quit || scancode == 0x44;                      // F10
        menu_keys.previous_level = menu_keys.previous_level || scancode == 0x3f;  // F5
        menu_keys.next_level = menu_keys.next_level || scancode == 0x40;          // F6
    }
    menu_keys.quit = menu_keys.quit || (alt && x_or_q);
    menu_keys.effects = alt && s;  // a tick acts on them while they are held, as the original does
    menu_keys.music = alt && m;
    play_.menu_keys = menu_keys;
    const int refreshes = play_.step(replay_.steps[step_].keyboard, [&] {
        frames.push_back(picture());
        frames.back().sounds = take_sounds();
    });
    ++step_;
    std::vector<Sound> late = take_sounds();  // reported after the step's last picture: heard with it
    const auto wanted = static_cast<std::size_t>(refreshes);
    const std::size_t first = frames.size() > wanted ? frames.size() - wanted : 0;
    std::vector<Sound> early = std::move(carried_);  // of the pictures that aren't shown: heard with the first that is
    carried_.clear();
    for (std::size_t i = 0; i < first; ++i) {
        early.insert(early.end(), frames[i].sounds.begin(), frames[i].sounds.end());
    }
    const std::size_t queued_before = queued_.size();
    for (std::size_t shown = frames.size() - first; shown < wanted && last_; ++shown) {
        queued_.push_back(*last_);
    }
    if (queued_.size() > queued_before) {  // the step starts with the picture before it: what was early is heard there
        std::vector<Sound>& sounds = queued_[queued_before].sounds;
        sounds.insert(sounds.end(), early.begin(), early.end());
        early.clear();
    }
    for (std::size_t i = first; i < frames.size(); ++i) {
        if (i == first) {
            frames[i].sounds.insert(frames[i].sounds.begin(), early.begin(), early.end());
        }
        if (i + 1 == frames.size()) {
            frames[i].sounds.insert(frames[i].sounds.end(), late.begin(), late.end());
        }
        queued_.push_back(frames[i]);
    }
    if (wanted == 0) {  // a step without a refresh (a menu opening): its sounds are heard with the next
        carried_ = std::move(early);
        carried_.insert(carried_.end(), late.begin(), late.end());
    }
    if (!frames.empty()) {
        last_ = frames.back();
        last_->sounds.clear();
    }
}

std::expected<std::optional<Refresh>, std::string> LvrPlayer::next() {
    while (queued_.empty()) {
        if (play_.ended && !play_.error.empty()) {
            return std::unexpected{std::format("step {}: {}", step_, play_.error)};
        }
        if (step_ >= replay_.steps.size() || play_.ended) {
            return std::nullopt;
        }
        play_step();
    }
    Refresh refresh = std::move(queued_.front());
    queued_.pop_front();
    return refresh;
}

std::string LvrPlayer::entities() {
    std::string lines = std::format("step {}\ncamera {},{}\n", step_, play_.world.camera.x, play_.world.camera.y);
    // As the reference's dump has them: a Viking who is gone shows his field 1A, FFFF.
    for (const auto& entity : play_.world.entities) {
        if (entity) {
            lines += std::format("{:2} kind {:02X} at {},{} flags {:04X} health {} speed {},{} frame {:X} msg {:X}\n",
                                 entity->slot, entity->dead && entity->slot < kVikingSlots ? 0x7fff : entity->kind,
                                 entity->x, entity->y, entity->flags, entity->health, entity->speed_x, entity->speed_y,
                                 entity->sprite.packed_frame, entity->message);
        }
    }
    return lines;
}

}  // namespace lvr::replay
