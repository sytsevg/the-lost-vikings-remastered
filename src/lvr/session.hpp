#pragma once

// The engine as a game to play: the scenes one after another from the game's files, a step of input at a
// time, with a picture for every refresh of the display and the sound of those refreshes. What a front end needs
// (src/lvr/main.cpp is one); nothing here knows about windows or devices.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "assets/assets.hpp"
#include "audio/gm.hpp"
#include "audio/opl3.hpp"
#include "audio/player.hpp"
#include "lvr/play.hpp"
#include "lvr/render.hpp"
#include "lvr/sprites.hpp"

namespace lvr {

// The display the original runs on: Mode X, 527 lines (docs/original/timing.md). A tick is three refreshes.
inline constexpr double kDisplayHz = 25'175'000.0 / (800.0 * 527.0);

// The sound's versions (ADR-0015): which one is heard, and the Modern one's SoundFont. Without a SoundFont there
// is only the Classic one.
struct SessionSound {
    bool modern = false;
    std::filesystem::path soundfont;
    // Live: the sound is made when the sound card asks for it (render_live), and each picture's requests are
    // heard as the picture is shown (hear_now), instead of a step's sound being made with the step. For a front
    // end with a device; without one (a dump, a test) a step's sound is in `samples`.
    bool live = false;
};

class Session {
  public:
    // The game from `dir`, started in scene `slot` (0-36 the levels, 39 the first logo: the game's own start).
    static std::expected<std::unique_ptr<Session>, std::string> open(const std::filesystem::path& dir, int slot,
                                                                     const SessionSound& sound = {});

    // One step of input: the pictures of the refreshes it takes (three for a tick or a wait's pass, one in the
    // inventory, 145 more for a change of scene; none when a menu only opens). Their sound is added to `samples`;
    // live, what each picture asks of the sound is in `cues` instead, one entry a picture.
    std::vector<Screen> step(std::uint16_t keyboard, Play::MenuKeys keys = {});

    // Live sound. Safe to call from two threads: the front end's (hear_now, set_modern) and the sound card's
    // (render_live); nothing else here touches the players then.
    // A picture is being shown: what it asks of the sound starts with the next sound made.
    void hear_now(const std::vector<SoundCue>& asked);
    // `frames` more sample frames (left and right each) onto `out`: the players run on by that much.
    void render_live(std::size_t frames, std::vector<std::int16_t>& out);
    // A key was pressed: its character, for what reads typed keys (passwords, yes and no).
    void key_pressed(std::uint16_t character) noexcept { play_.world.typed = character; }

    // The game is over: it was quit, or a scene couldn't be loaded (then `error` says why).
    bool quit() const noexcept { return play_.ended; }
    // The menu keys as the last step left them (Alt+S and Alt+M stay until a tick acts on them).
    const Play::MenuKeys& menu_keys() const noexcept { return play_.menu_keys; }
    const std::string& error() const noexcept { return play_.error; }
    // A demo is playing: any key ends it.
    bool demo() const noexcept { return play_.world.attract; }
    // The last step loaded a scene: the front end clears its keyboard word, as the original's level start does.
    bool scene_loaded() const noexcept { return play_.scene_loaded; }
    int scene() const noexcept { return play_.world.scene; }

    static std::uint32_t sample_rate() noexcept { return audio::Opl3::kSampleRate; }
    bool has_modern() const noexcept { return modern_ != nullptr; }
    bool modern() const noexcept { return modern_on_; }
    void set_modern(bool on) {
        const std::scoped_lock lock{sound_mutex_};
        modern_on_ = on && has_modern();
    }

    std::vector<Screen> start_frames;   // the start's pictures, shown first
    std::vector<std::int16_t> samples;  // rendered sound not yet played: left, right in turn
    // Live: what the pictures of the last step (at first: the start's) ask of the sound, one entry a picture.
    std::vector<std::vector<SoundCue>> cues;

  private:
    Session(assets::GameFiles files, std::unique_ptr<LoadedLevel> loaded, SharedGraphics shared, int slot,
            audio::SoundData sound, std::optional<audio::SoundData> modern, std::unique_ptr<audio::GmSynth> gm);
    Screen picture();
    void hear(const std::vector<SoundCue>& asked);
    void refresh_sound();
    void half_refresh_sound(std::vector<std::int16_t>& out);
    // A refresh's sound: made at once into `samples`, or (live) its requests kept in `cues` for when it is shown.
    void sound_of_refresh(std::vector<SoundCue> asked);
    // The players the music's cues are for, and the ones the effects' are for. The Classic sound's one player
    // has both, as the original's driver; the Modern sound has one for each (below).
    template <typename F>
    void music_players(F f) {
        f(*classic_);
        if (modern_ != nullptr) {
            f(*modern_);
        }
    }
    template <typename F>
    void effect_players(F f) {
        f(*classic_);
        if (modern_effects_ != nullptr) {
            f(*modern_effects_);
        }
    }
    template <typename F>
    void players(F f) {
        music_players(f);
        if (modern_effects_ != nullptr) {
            f(*modern_effects_);
        }
    }

    assets::GameFiles files_;
    SharedGraphics shared_;
    Play play_;
    PiecePainter painter_;
    std::vector<PiecePicture> pictures_;
    std::optional<Screen> last_;     // the last picture shown: a step that shows fewer than its refreshes
                                     // starts with it
    std::vector<SoundCue> carried_;  // what a step without a refresh asked of the sound: heard with the next

    audio::SoundData sound_;
    audio::Opl3 chip_;
    std::unique_ptr<audio::OplSynth> opl_synth_;
    std::unique_ptr<audio::Player> classic_;
    std::optional<audio::SoundData> modern_sound_;
    // The Modern sound is ours to shape: the effects have a synthesizer of their own, so they don't take channels
    // from the music while they sound (in the original, and in the Classic sound, they do), and more than four
    // can sound at a time.
    std::unique_ptr<audio::GmSynth> gm_;
    std::unique_ptr<audio::GmSynth> gm_effects_;
    std::unique_ptr<audio::Player> modern_;          // the music
    std::unique_ptr<audio::Player> modern_effects_;  // the effects
    std::vector<std::int16_t> effects_mix_;
    std::vector<std::int16_t> unheard_;  // the version not heard: rendered all the same, and dropped
    bool modern_on_ = false;
    bool live_ = false;
    std::mutex sound_mutex_;              // live: the players, between the front end and the sound card
    std::vector<std::int16_t> rendered_;  // live: sound made past what was asked for (up to half a refresh)
};

}  // namespace lvr
