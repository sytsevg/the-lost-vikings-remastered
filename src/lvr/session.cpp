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
                                                                   const SessionSound& sound) {
    auto files = assets::GameFiles::open(dir);
    if (!files) {
        return std::unexpected{files.error()};
    }
    if (slot < 0 || slot >= kSceneSlots) {
        return std::unexpected{std::format("no scene {}", slot)};
    }
    auto loaded = load_slot(*files, slot, database::kSpaceship);
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
                                                 std::move(*shared), slot, std::move(*classic), std::move(modern),
                                                 std::move(gm)}};
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
    }
    session->last_ = start;
    return session;
}

Session::Session(assets::GameFiles files, std::unique_ptr<LoadedLevel> loaded, SharedGraphics shared, int slot,
                 audio::SoundData sound, std::optional<audio::SoundData> modern, std::unique_ptr<audio::GmSynth> gm)
    : files_{std::move(files)},
      shared_{std::move(shared)},
      play_{files_, std::move(loaded), slot},
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

Screen Session::picture() {
    const LoadedLevel& loaded = play_.loaded();
    // (The painter is the same from scene to scene: the original never clears the buffer its packed frames are
    // unpacked into, so a piece that hasn't had a frame of its own yet shows what the last one left there.)
    View view = view_of(play_.world, loaded, painter_, pictures_);
    if (play_.fade) {  // between scenes every colour is darkened
        view.darken = {*play_.fade, *play_.fade, *play_.fade};
        view.darken_all = true;
    }
    Screen screen;
    draw(screen, view, play_.graphics(), shared_);
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
    // Half a refresh at a time, as the driver is serviced; what is made past the asking waits for the next.
    while (rendered_.size() < frames * 2) {
        half_refresh_sound(rendered_);
    }
    const auto end = rendered_.begin() + static_cast<std::ptrdiff_t>(frames * 2);
    out.insert(out.end(), rendered_.begin(), end);
    rendered_.erase(rendered_.begin(), end);
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
    cues.clear();
    play_.menu_keys = keys;
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
    if (last_.has_value()) {
        const Screen before = *last_;
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
    return shown;
}

}  // namespace lvr
