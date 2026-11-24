#pragma once

// The engine as a game to play: the scenes one after another from the game's files, a step of input at a
// time, with a picture for every refresh of the display and the sound of those refreshes. What a front end needs
// (src/lvr/main.cpp is one); nothing here knows about windows or devices.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "assets/assets.hpp"
#include "audio/gm.hpp"
#include "audio/opl3.hpp"
#include "audio/player.hpp"
#include "lvr/minimap.hpp"
#include "lvr/play.hpp"
#include "lvr/profile.hpp"
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
    // The rules are the whole session's, and so is what draws its pictures.
    static std::expected<std::unique_ptr<Session>, std::string> open(const std::filesystem::path& dir, int slot,
                                                                     const SessionSound& sound = {},
                                                                     Rules rules = Rules::kVanilla,
                                                                     Draw draw_with = draw);
    Rules rules() const noexcept { return play_.world.rules; }

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

    // Remaster rules' zoom (ADR-0011): a step out (`by` > 0) or back in, between the original's view and twice
    // it. The next step's world has the view; the pictures go from the one to the other over a moment.
    void zoom(int by) noexcept;
    // Remaster rules' minimap, as the last step left it: the level as far as a view has shown it since the game
    // was started. None in a scene that isn't a level, under Vanilla rules, or while it is switched off.
    const Minimap* minimap() const noexcept { return minimap_on_ && !minimap_.pixels.empty() ? &minimap_ : nullptr; }
    // The player's profile is kept in `folder` from here on (Remaster rules; Vanilla keeps none): what is there is
    // read now, and written by save_profile() and whenever a scene is loaded. An error says what couldn't be read;
    // the profile is used all the same, and the file that couldn't be read is written over at the next save.
    std::expected<void, std::string> use_profile(const std::filesystem::path& folder);
    std::expected<void, std::string> save_profile() const;
    // The quicksave as the profile's file: written when one is made, read when the profile is taken into use.
    std::expected<void, std::string> write_quicksave() const;
    std::expected<void, std::string> read_quicksave();
    void toggle_minimap() noexcept {
        if (!minimap_.pixels.empty()) {  // only where there is one: in a level
            minimap_on_ = !minimap_on_;
        }
    }

    // Remaster rules' rewind (ADR-0011): while it is held, every step goes two steps back instead of one on (time
    // runs backwards at twice its speed, with the pictures in between as they were), as far as the last ten
    // seconds of play in levels reach; there it is over and the play goes on, until it is let go and held again.
    // Live, the sound of those seconds is heard backwards meanwhile, twice as fast. What the minimap has seen is
    // not taken back.
    void rewind(bool held) noexcept { rewind_held_ = held; }
    bool rewinding() const noexcept { return rewinding_; }
    // The quicksave, only with the game's cheat on (the title was given its password, or `cheat` below): the play
    // as it is now is kept, in a level; and brought back, from wherever the game is by then. One is kept, until
    // the game is closed. Acted on at the next step.
    void quicksave() noexcept { save_wanted_ = true; }
    void quickload() noexcept { load_wanted_ = true; }
    bool has_quicksave() const noexcept { return quick_.has_value(); }
    // The cheat on without its password: F5 and F6 change the level, and under Remaster rules the quicksave.
    void cheat() noexcept { play_.world.title.cheat = true; }

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
    Session(assets::GameFiles files, std::unique_ptr<LoadedLevel> loaded, SharedGraphics shared, int slot, Rules rules,
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
    Draw draw_ = draw;
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
    // The zoom: which view is wanted; while the view changes, how many pictures into that it is, and (growing)
    // the view it grows from, in the level. A view that is to shrink does so when its pictures have closed in.
    std::array<int, 2> wanted_view() const;
    void change_view();
    Screen::Part part_shown(const View& view);
    int zoom_ = 0;
    // Smooth movement: where the camera was at the end of the last tick, and the view's size then. None after
    // what wasn't a plain tick.
    struct Scrolled {
        std::array<int, 2> scroll{};
        std::array<int, 2> view{};
    };
    std::optional<Scrolled> scrolled_;
    void update_minimap();
    // Rewind and quicksave.
    bool level_played() const;
    std::vector<Screen> step_back();
    // A step that can be gone back over: the play before it, and the keys it was given.
    struct Kept {
        Play::Snapshot before;
        std::uint16_t keyboard = 0;
        Play::MenuKeys keys;
    };
    // The play back at `step`'s start, and the pictures the step showed, made again; none if it was more than a
    // plain tick. True in `another` if that was another scene than the one played.
    std::vector<Screen> back_to(const Kept& step, bool& another);
    std::deque<Kept> rewind_;  // the last steps in levels, the oldest first
    std::optional<Play::Snapshot> quick_;
    bool rewind_held_ = false;
    bool rewinding_ = false;    // the last step went back
    bool rewind_over_ = false;  // held, but there was nothing left to go back to: not again until it is let go
    bool save_wanted_ = false;
    bool load_wanted_ = false;
    std::vector<std::int16_t>
        heard_;        // live: the sound of the last ten seconds as it was heard, for hearing it backwards
    SeenLevels seen_;  // by level: what a view has shown of it (the minimap's pixels)
    std::filesystem::path profile_;  // where the player's profile is kept; empty: nowhere
    Minimap minimap_;
    bool minimap_on_ = true;
    int zoom_pictures_ = 0;
    std::optional<Screen::Part> grows_from_;
    bool shrinking_ = false;
    std::mutex sound_mutex_;              // live: the players, between the front end and the sound card
    std::vector<std::int16_t> rendered_;  // live: sound made past what was asked for (up to half a refresh)
};

}  // namespace lvr
