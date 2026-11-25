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
#include "assets/snes.hpp"
#include "audio/gm.hpp"
#include "audio/opl3.hpp"
#include "audio/player.hpp"
#include "lvr/effects.hpp"
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
                                                                     Draw draw_with = draw, const Online& online = {},
                                                                     int player = 0);
    Rules rules() const noexcept { return play_.world.rules; }

    // One step of input: the pictures of the refreshes it takes (three for a tick or a wait's pass, one in the
    // inventory, 145 more for a change of scene; none when a menu only opens). Their sound is added to `samples`;
    // live, what each picture asks of the sound is in `cues` instead, one entry a picture. `pace`: under Remaster
    // rules, how fast the Vikings walk and climb (World::pace: a gamepad's stick pushed part way).
    std::vector<Screen> step(std::uint16_t keyboard, Play::MenuKeys keys = {}, Pace pace = {});

    // --- Online (ADR-0012): what rollback drives (lvr/coop.hpp) ---

    // This machine's player's part of the next step's input: his keys, stick and zoom, the character typed, the
    // menu keys (step() takes it this way too).
    StepInput take_input(std::uint16_t keyboard, Play::MenuKeys keys, Pace pace);
    // One step with every player's input, shown: its pictures, with its sounds. `step`: its number online, for the
    // cue ledger.
    std::vector<Screen> run(const StepInput& input, std::optional<std::uint32_t> step = std::nullopt);
    // The same step again, after a rollback went back before it: nothing shown, and of its sounds only what it
    // hadn't asked for before is heard (with the next picture), what it no longer asks for stopped.
    void run_again(const StepInput& input, std::uint32_t step);
    // The steps before `step` won't run again: the ledger lets go of them.
    void confirmed(std::uint32_t step) { ledger_.confirm(step); }
    Play::Snapshot snapshot() const { return play_.snapshot(); }
    // Back to a snapshot exactly (rollback: the views too).
    void restore(const Play::Snapshot& from) { (void)play_.restore(from); }
    std::uint64_t state_hash() { return play_.state_hash(hashed_); }
    bool changing_scene() const noexcept { return play_.changing_scene(); }
    bool online() const noexcept { return play_.world.online; }
    int player() const noexcept { return player_; }
    const World& world() const noexcept { return play_.world; }
    // Instead of a step, while the play is held (Remaster rules' menu is open): a tick's three pictures of the world
    // as it is, a little darker as under the original's menus. Nothing of the play moves and nothing is kept for the
    // rewind; only a change of view the zoom asks for goes on, as while the world holds for a key (what it brings into
    // view is created at the next step). No sound.
    std::vector<Screen> held();

    // Live sound. Safe to call from two threads: the front end's (hear_now, set_modern) and the sound card's
    // (render_live); nothing else here touches the players then.
    // A picture is being shown: what it asks of the sound starts with the next sound made.
    void hear_now(const std::vector<SoundCue>& asked);
    // `frames` more sample frames (left and right each) onto `out`: the players run on by that much.
    void render_live(std::size_t frames, std::vector<std::int16_t>& out);
    // A key was pressed: its character, for what reads typed keys (passwords, yes and no).
    void key_pressed(std::uint16_t character) noexcept { typed_ = character; }  // for the next step

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

    // Remaster rules' own keys (zoom, minimap, quicksave) are taken only where they act: in a level being played
    // (not a scene, a demo, or the pause menu or quit box). Each says whether it took its key; one it didn't is a
    // key like any other, for the game (the original's: any key ends a demo). So nothing pressed in a scene is
    // kept for the next level.
    bool takes_play_keys() const noexcept { return play_.world.rules == Rules::kRemaster && level_played(); }
    // Remaster rules' zoom (ADR-0011): a step out (`by` > 0) or back in, between the original's view and twice
    // it. The next step's world has the view; the pictures go from the one to the other over a moment.
    bool zoom(int by);
    // The zoom's step (0 the original's view, up to kZoomSteps - 1: twice it), and it set to one from anywhere (the
    // profile's, at the start): a level played from then on is shown so.
    int zoom_level() const noexcept { return zoom_; }
    void set_zoom_level(int level);
    static constexpr int kZoomSteps = 3;
    // A development tool's (vikings-remaster --dump --whole): the view becomes the whole level at once, with the
    // HUD's rows under it, and stays so; what is in it is created at the next tick's start. Remaster rules, in a
    // level being played.
    bool view_whole_level();
    // Development tools' too (vikings-remaster --showcase). The view becomes `width` x `height` with its corner at
    // (x, y), with the HUD's rows under it, and stays there; Remaster rules, in a level being played.
    bool hold_view(int x, int y, int width, int height);
    // And: the Viking in slot `viking` put at (x, y) in the level with `health` (x 2, as the world keeps it), to be
    // seen dying there; Remaster rules, in a level being played, and only if he is in it.
    bool put_viking(int viking, int x, int y, std::uint16_t health);
    // The level of scene `slot` is started at the next step, as the cheat's F5 and F6 do; from a level (not a menu).
    bool go_to(int slot);
    // The world holds until a key is pressed (a level's password, a text).
    bool waiting_for_key() const noexcept { return play_.world.waiting_for_key; }
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
    // Whether the minimap is shown in a level, set from anywhere (the menu's, the profile's).
    bool minimap_wanted() const noexcept { return minimap_on_; }
    void set_minimap(bool on) noexcept { minimap_on_ = on; }
    bool toggle_minimap() noexcept {
        if (!takes_play_keys() || minimap_.pixels.empty()) {  // only where there is one: in a level
            return false;
        }
        minimap_on_ = !minimap_on_;
        return true;
    }

    // Remaster rules' rewind (ADR-0011): while it is held, every step goes two steps back instead of one on (time
    // runs backwards at twice its speed, with the pictures in between as they were). Only in a level being played,
    // and never back past its start. A level has ten seconds of it, shown in a bar in the minimap, used up as they are
    // gone back over and given again only when a level is started (that one again too). With nothing left to go
    // back to, or none of them, the rewind is over and the play goes on, until it is let go and held again. Live,
    // the sound of those seconds is heard backwards meanwhile, twice as fast. What the minimap has seen is not
    // taken back.
    void rewind(bool held) noexcept { rewind_held_ = held && !play_.world.online; }  // (none online: ADR-0012)
    bool rewinding() const noexcept { return rewinding_; }
    static constexpr int kRewindSteps = 199;  // a level's ten seconds, in steps (ticks: three refreshes each)
    int rewind_left() const noexcept { return rewind_left_; }  // of the level's steps of rewind
    // The quicksave, only with the game's cheat on (the title was given its password, or `cheat` below): the play
    // as it is now is kept, in a level; and brought back, from wherever the game is by then but a menu (the
    // game-over scene, for one). One is kept, until the game is closed. Acted on at the next step.
    // None online (ADR-0012).
    bool quicksave() noexcept {
        save_wanted_ = takes_play_keys() && play_.world.title.cheat && !play_.world.online;
        return save_wanted_;
    }
    bool quickload() noexcept {
        load_wanted_ = play_.world.rules == Rules::kRemaster && play_.world.title.cheat && quick_.has_value() &&
                       !play_.in_menu() && !play_.world.online;
        return load_wanted_;
    }
    bool has_quicksave() const noexcept { return quick_.has_value(); }
    // The cheat on without its password: F5 and F6 change the level, and under Remaster rules the quicksave.
    void cheat() noexcept { play_.world.title.cheat = true; }
    // Remaster rules' Modern visuals: the effects (effects.hpp) in the pictures from here on. Off by default: for what
    // shows the pictures on the GPU renderer.
    void set_effects(bool on) noexcept { effects_on_ = on && play_.world.rules == Rules::kRemaster; }
    bool effects() const noexcept { return effects_on_; }

    // The game's font (SharedGraphics::font): for what the front end draws in the game's own letters (the menu).
    const std::vector<assets::IndexedImage>& font() const noexcept { return shared_.font; }

    static std::uint32_t sample_rate() noexcept { return audio::Opl3::kSampleRate; }
    bool has_modern() const noexcept { return modern_ != nullptr; }
    bool modern() const noexcept { return modern_on_; }
    void set_modern(bool on) {
        const std::scoped_lock lock{sound_mutex_};
        modern_on_ = on && has_modern();
    }
    // Remaster rules' volume, the player's: how loud the music and the effects are, each 0 (not heard) to
    // kLoudest (as the game asks for them; profile.hpp), in both versions of the sound (audio::Player::set_loudness).
    void set_volume(int music, int effects);

    std::vector<Screen> start_frames;   // the start's pictures, shown first
    std::vector<std::int16_t> samples;  // rendered sound not yet played: left, right in turn
    // Live: what the pictures of the last step (at first: the start's) ask of the sound, one entry a picture.
    std::vector<std::vector<SoundCue>> cues;

  private:
    Session(assets::GameFiles files, std::unique_ptr<LoadedLevel> loaded, SharedGraphics shared, int slot, Rules rules,
            audio::SoundData sound, std::optional<audio::SoundData> modern, std::unique_ptr<audio::GmSynth> gm,
            const Online& online);
    // `dimmed`: as the original's menus show the picture (kMenuTint), unless it is darkened already.
    Screen picture(bool dimmed = false);
    void hear(const std::vector<SoundCue>& asked);
    // Where the Modern sound plays an effect: across the view from left to right by where it happened, and quieter
    // the further outside the view (silent half a view away). One of the scene's: in the middle.
    audio::Placement placed(const SoundCue& cue) const;
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
    std::optional<assets::SnesRom> snes_;  // the user's SNES image, for the backdrops (Remaster rules)
    SharedGraphics shared_;
    Play play_;
    Draw draw_ = draw;
    PiecePainter painter_;
    std::vector<PiecePicture> pictures_;
    // A step's pictures, each numbered with the refresh it is shown for (refreshes_).
    std::vector<Screen> numbered(std::vector<Screen> shown);
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
    bool effects_on_ = false;
    bool moving_ = false;  // the pictures being made are of the play going on (not made again going back)
    // The zoom: which view is wanted; while the view changes, how many pictures into that it is, and (growing)
    // the view it grows from, in the level. A view that is to shrink does so when its pictures have closed in.
    std::array<int, 2> wanted_view() const;
    void change_view(bool at_once);
    void set_view(int width, int height, bool at_once);
    // The step's input from this machine's player: his keys, stick and zoom, the character typed, the menu keys.
    StepInput input_of(std::uint16_t keyboard, const Play::MenuKeys& keys, Pace pace);
    CueLedger ledger_;  // online: what the steps run again asked of the sound, heard once
    // After a rewind's or a quicksave's snapshot is brought back: the view the zoom has now.
    void zoom_back();
    std::optional<std::array<int, 2>> view_now_;  // the view change_view gives the next step
    std::uint16_t typed_ = 0;                     // the character typed since the last step
    std::vector<std::uint8_t> hashed_;            // (the state hash's buffer, kept)
    Screen::Part part_shown(const View& view);
    int zoom_ = 0;
    std::uint32_t refreshes_ = 0;  // the refreshes the pictures handed out so far are for (Screen::refresh)
    std::optional<std::array<int, 2>> whole_view_;  // view_whole_level()'s
    Effects effects_;
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
        Pace pace;
    };
    // The play back at `step`'s start, and the pictures the step showed, made again; none if it was more than a
    // plain tick.
    std::vector<Screen> back_to(const Kept& step);
    // Whether a picture of `world` has the rewind's bar (in the minimap): in a level being played.
    bool gauge_shown(const World& world) const;
    std::deque<Kept> rewind_;  // the last steps of the level, the oldest first: no more than can be gone back over
    int rewind_left_ = kRewindSteps;
    std::uint32_t rewind_scene_ = 0;  // the scene (Play::scenes_started) those are of
    std::optional<Play::Snapshot> quick_;
    bool rewind_held_ = false;
    bool rewinding_ = false;    // the last step went back
    bool rewind_over_ = false;  // held, but there was nothing left to go back to: not again until it is let go
    bool save_wanted_ = false;
    bool load_wanted_ = false;
    // Which of the world's players this machine shows and plays (co-op online, ADR-0012): the first, so far.
    std::uint8_t player_ = 0;
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
