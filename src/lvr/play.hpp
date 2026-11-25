#pragma once

// Playing a level: what is loaded for it from the game's files, its world as the level starts, and what a picture
// of that world shows. The translation layer's last step (ADR-0014): everything the engine needs, in its own terms.

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "assets/assets.hpp"
#include "assets/objects.hpp"
#include "assets/snes.hpp"
#include "lvr/level.hpp"
#include "lvr/render.hpp"
#include "lvr/sprites.hpp"
#include "lvr/world.hpp"

namespace lvr {

// A level with everything its world draws and plays from. Loaded once; worlds refer to it.
struct LoadedLevel {
    Level level;
    SceneGraphics graphics;
    SpriteMemory sprites;
    std::vector<LoadedSpriteSet> sprite_sets;
    std::vector<std::uint16_t> packed_entries;
    assets::ObjectDatabase database;  // the kinds' records and the animation programs
    Cell cleared_cell{};              // the world's empty cell and its solid cell, for the kinds that open and build
    Cell filled_cell{};
    std::uint16_t broken_picture = 0;
    AnimationPlaces places;
    std::array<AnimationPlaces, kWorlds> places_from;  // the same from each other world's database as home
    int database_entry = database::kSpaceship;  // which world's it is           // where it keeps the programs the
                                                // kinds name; empty in the spaceship world
    AnimationTables animation_tables{};
    std::array<std::uint8_t, 256> slope_heights{};
    std::vector<std::uint8_t> texts;                     // the game's texts, as VIKINGS.EXE has them
    std::array<std::uint8_t, 4> password{};              // the level's
    int slot = 0;                                        // which scene it is, by the original's number
    std::vector<std::array<std::uint8_t, 4>> passwords;  // every level's
};

// Level 1-37.
std::expected<LoadedLevel, std::string> load_level(const assets::GameFiles& files, int number);
// The game-over scene, with the object database of the world it follows (its entities' kinds are in each).
std::expected<LoadedLevel, std::string> load_game_over(const assets::GameFiles& files, int database_entry);

// The scenes by the original's slot number: 0-36 the levels, 37 the game-over scene (it keeps `database_entry`,
// the world's it follows), 38-47 the title, the logos, the galaxy, the village's three scenes, the starfield and
// the ship. With the user's SNES image (`snes`; Remaster rules only), a scene that has a backdrop there has it in
// its graphics (backdrop.hpp).
inline constexpr int kSceneSlots = 48;
// The original's pause menu and quit box show the picture this much darker (World::tint, of the VGA's 64 levels
// a colour); Remaster rules' menu (menu.hpp) does it too.
inline constexpr std::array<std::uint8_t, 3> kMenuTint{8, 8, 8};
std::expected<LoadedLevel, std::string> load_slot(const assets::GameFiles& files, int slot, int database_entry,
                                                  const assets::SnesRom* snes = nullptr);

// The level's world before its first tick, for as many players (ADR-0012), each one's view on his Viking.
// Under Remaster rules a level's world is made with the views the players' zoom asks for (`views`,
// World::view_for).
World prepare_world(const LoadedLevel& loaded, const Map* map_before = nullptr, Rules rules = Rules::kVanilla,
                    const Views& views = kOriginalViews, int players = 1);

// The level's world as it starts: the Vikings where they start, the camera on the first, the placements that are
// always active or in view, and the one tick the original runs before the first picture. `loaded` must outlive it.
World start_world(const LoadedLevel& loaded, Rules rules = Rules::kVanilla);

// A level being played, a step of input at a time, as the original's main loop takes them: a tick (three pictures)
// and the conversation its entities started; or, while a line waits for a key, a pass of that wait.
class Play {
  public:
    // A level played for itself: it can't go on to another scene.
    explicit Play(const LoadedLevel& loaded) : world{start_world(loaded)}, loaded_{&loaded} {}
    // One that goes on: to the game-over scene when the Vikings are all dead, and from there back into the level.
    // The scenes are loaded from `files`, which must outlive it.
    Play(const assets::GameFiles& files, std::unique_ptr<LoadedLevel> first, int slot, Rules rules = Rules::kVanilla)
        : world{start_world(*first, rules)}, files_{&files}, loaded_{first.get()}, owned_{std::move(first)} {
        world.scene = slot;
        world.next_scene = loaded_->level.next_slot;
        start_demo(slot);
    }

    // What the world shown now was loaded from.
    const LoadedLevel& loaded() const noexcept { return *loaded_; }
    // The world a picture shows: `world`, but while a scene fades out the world as it was before the scene's end
    // removed everyone (the original's fade only darkens what it last showed: everyone stays there, still).
    const World& shown() const noexcept { return fading_out_ ? *fading_out_ : world; }
    // The pictures to draw it with: the scene's, or (once the title's menu has cleared the screen) a black screen
    // with what the scene showed in the HUD's place a little below its top.
    const SceneGraphics& graphics();

    // One step. `picture` is called each time the world is to be shown. Returns how many refreshes of the display
    // the step takes (a tick's three, three for a wait's pass; a change of scene: 145 more); a step can show fewer
    // pictures than that.
    // The players' input: each one's keys and stick (ADR-0012); the first player's drive the menus and the texts.
    template <typename Picture>
    int step(StepInput input, const Picture& picture) {
        constexpr int kTick = 3;
        int refreshes = kTick;
        bool said = false;
        scene_loaded = false;
        take(input);
        if (world.waiting_for_key) {
            world.read_keys(input);
            if ((world.host().pressed & key::kAction) == 0) {
                world.turn_colours();
                picture();
                return refreshes;
            }
            world.read_keys(input);
            world.waiting_for_key = false;
            said = true;
        } else {
            refreshes = 0;
            const std::function<void()> shown = [&picture] { picture(); };
            if (state_.menu == Menu::kQuitBox) {
                if (menu_pass(input, shown, refreshes) && world.game_over) {  // "yes", or the game was over anyway
                    ended = true;
                }
                return refreshes;
            }
            if (state_.menu == Menu::kPause) {
                if (!menu_pass(input, shown, refreshes)) {
                    return refreshes;
                }
                // The tick it held goes on from here.
            } else {
                world.read_keys(with_demo(input));
                play_demo();
                if (world.title.quit) {
                    ended = true;
                    return refreshes;
                }
                if (pause_wanted()) {  // the menu is set up and shown; its passes are the steps after this
                    open_menu(false);
                    picture();
                    return refreshes;
                }
            }
            if (leaving()) {
                // The scene ends here, in the middle of the tick; the rest of it is the new scene's, without keys.
                refreshes += change_scene([&picture] { picture(); });
                if (ended) {
                    return refreshes;
                }
                // Loading a scene lets go of every key: for what is left of this step none is down. One still
                // held is read again in the next step, and is newly pressed there if a wait for a key read
                // the empty keyboard in between.
                for (PlayerInput& player : input.players) {
                    player.keys = 0;
                }
                menu_keys.previous_level = menu_keys.next_level = false;
            }
            if (!world.online && (world.host().inventory.open || inventory_wanted(world, 0))) {
                // The inventory screen holds the tick: a pass and a picture a step, one refresh each. Opening
                // it shows the playfield once more, and its first pass follows at once. (Online it doesn't hold
                // the game: World::tick_part.)
                if (!world.host().inventory.open) {
                    open_inventory(world, 0);
                    count_picture(world.pieces);
                    picture();
                    ++refreshes;
                    world.read_keys(input);
                }
                const bool closed = inventory_pass(world, 0);
                world.update_hud();
                picture();
                ++refreshes;
                if (!closed) {
                    return refreshes;
                }
            }
            for (int part = 0; part < kTick; ++part) {
                world.tick_part(part);
                world.text.draw();
                picture();
            }
            refreshes += kTick;
            switch_sound();
            // After the tick: Alt+X, Alt+Q or F10, unless something is being said.
            if (menu_keys.quit && world.lines.empty()) {
                if (state_.playback || loaded_->level.quits_at_once) {  // a demo, or a scene that isn't played: at once
                    ended = true;
                    return refreshes;
                }
                open_menu(true);
                picture();
                return refreshes;
            }
            change_level();
            said = world.next_line < world.lines.size();
        }
        if (world.show_lines(input)) {
            refreshes += kTick;  // the wait's first pass
        }
        if (said) {
            picture();
        }
        return refreshes;
    }
    // One player's: the original's keyboard word, with the menu keys as they are set (menu_keys).
    template <typename Picture>
    int step(std::uint16_t keys, const Picture& picture) {
        StepInput input = StepInput::one(keys);
        input.menu = menu_keys;
        return step(input, picture);
    }

    // The pause menu (Esc or P: text 2) and the quit box (Alt+X, Alt+Q, F10: text 3 with the level's
    // password): the game holds while the chosen word blinks. Left and right choose, fire takes the choice, Esc
    // or P and the N key are "no", the Y key "yes". Giving up is the game over; quitting ends the game.
    enum class Menu : std::uint8_t { kNone, kPause, kQuitBox };
    // What the play keeps from step to step beside its world, as plain data: with the world it is all a step
    // starts from (ADR-0012: what rollback goes back to). The pause menu or the quit box, the original's sound
    // switches, a demo being played, and the view each player has chosen (Remaster rules' zoom: a scene starts
    // with it).
    struct PlayState {
        Menu menu = Menu::kNone;
        std::uint16_t menu_choice = 1;  // 0 yes, 1 no
        std::uint16_t menu_blink = 0;   // counts down a pass; every 16th changes the chosen word
        bool menu_tinted = false;       // the menu darkened the picture itself
        bool effects_on = true;
        bool music_on = true;
        // Demos: the scenes played from a recording (the village's, the ending's, and the levels the title shows).
        bool playback = false;
        bool demo_wanted = false;              // the next scene is one: the title asked, or the scene before it
        int demo_index = 0;                    // which of the title's demo levels is next
        std::vector<std::uint16_t> recording;  // pairs: for how many ticks, which keys
        std::size_t demo_at = 0;
        std::uint16_t demo_count = 0;
        std::uint16_t demo_word = 0;
        std::uint16_t demo_keys = 0;
        Views views = kOriginalViews;
    };

    // The play as it is now, to come back to: a copy of the world and the play's own state, and the level it is a
    // world of (kept loaded for as long as the snapshot is). For Remaster rules' rewind and the quicksave, and
    // online for rollback.
    struct Snapshot {
        World world;
        PlayState state;
        const LoadedLevel* loaded = nullptr;
        std::shared_ptr<LoadedLevel> owner;
    };
    Snapshot snapshot() const { return {.world = world, .state = state_, .loaded = loaded_, .owner = owned_}; }
    // Back to a snapshot, exactly. True if it is of another scene than the one played now: the caller then has
    // that scene's sound start again (scene_sound) once it is where it stays.
    bool restore(const Snapshot& from);
    // A player's chosen view becomes `zoom`, at once (the zoom is the player's, not the play's: after a rewind or a
    // quicksave brought back, the one he has now).
    void zoom_back(int player, std::array<int, 2> zoom);
    // A player's chosen view, for the next scene's start (a step's input says it too: StepInput).
    void choose_view(int player, std::array<int, 2> zoom) { state_.views.at(static_cast<std::size_t>(player)) = zoom; }
    const PlayState& state() const noexcept { return state_; }
    // The play's state written out (the world, World::save, then its PlayState) into `buffer`, and its 64-bit FNV-1a:
    // what two machines compare to find one out of step, and the sync test compares (ADR-0012).
    std::uint64_t state_hash(std::vector<std::uint8_t>& buffer) const;
    // What is heard stops and the scene's music starts, as when the scene was loaded; the step counts as one that
    // loaded a scene.
    void scene_sound();
    // The pause menu or the quit box is open; a scene is played from a recording.
    bool in_menu() const noexcept { return state_.menu != Menu::kNone; }
    bool playing_demo() const noexcept { return state_.playback; }

    World world;

  private:
    std::optional<World> fading_out_;  // while the scene fades out: what it fades (shown()); beside `world`, as aligned

  public:
    // The user's SNES image, for the scenes' backdrops (Remaster rules; load_slot). Must outlive the play.
    const assets::SnesRom* snes = nullptr;
    // The keys the menus read as held, apart from the keyboard word: the step's (StepInput::menu), as the step has
    // left them (it clears Alt+S and Alt+M once it acted on them).
    using MenuKeys = lvr::MenuKeys;
    MenuKeys menu_keys;
    // While a scene fades out or in: how much darker every colour is shown (0-70).
    std::optional<std::uint8_t> fade;
    // The last step loaded a scene. The original's level start clears its keyboard word then: a key held across
    // the change counts only when it is pressed again, and what a demo's end left in the word is gone.
    bool scene_loaded = false;
    bool ended = false;  // the game went on to a scene the engine doesn't have yet, or one couldn't be loaded
    // How many scenes were started after the first: one more as `world` becomes the next scene's (a level started
    // again too). Not the world's, so a snapshot brought back doesn't change it.
    std::uint32_t scenes_started = 0;
    std::string error;  // why, if it couldn't

  private:
    // After a tick (the original's 08C8): Alt+S switches the effects off or on, Alt+M the music.
    void switch_sound() {
        if (menu_keys.effects) {
            state_.effects_on = !state_.effects_on;
            world.cues.push_back(
                {.kind = SoundCue::Kind::kEffectsOn, .value = static_cast<std::uint16_t>(state_.effects_on ? 1 : 0)});
            menu_keys.effects = false;
        }
        if (menu_keys.music) {
            state_.music_on = !state_.music_on;
            world.cues.push_back(
                {.kind = SoundCue::Kind::kMusicOn, .value = static_cast<std::uint16_t>(state_.music_on ? 1 : 0)});
            menu_keys.music = false;
        }
    }
    // After a tick (the original's 00F7), in a level, once the title was given the cheat's password: F5 ends the
    // level for the one before it (the first stays the first), F6 for the one that follows it anyway.
    void change_level() {
        if (!world.title.cheat || world.scene >= scene::kGameOver) {
            return;
        }
        if (menu_keys.previous_level) {
            world.level_done = true;
            world.next_scene = world.scene > 0 ? world.scene - 1 : 0;
        } else if (menu_keys.next_level) {
            world.level_done = true;
        }
    }
    bool leaving() const noexcept {
        return world.game_over || world.level_done || world.game_over_scene.try_again ||
               world.game_over_scene.give_up || world.leave;
    }
    // A scene played from a recording: its keys for the next tick; the recording's end, or a key, ends it.
    void play_demo();
    // Whether the scene in `slot` is played from a recording; the recording then. The title's demos replace the
    // slot with the level they show.
    void start_demo(int& slot);
    void start_demo(const int& slot) {
        int same = slot;
        start_demo(same);
    }
    // The scene fades out, the next is loaded and runs its first tick, and fades in. Returns the refreshes.
    int change_scene(const std::function<void()>& picture);

    bool pause_wanted() const noexcept;
    void open_menu(bool quit_box);
    // One pass: three refreshes. True when a choice closed the menu.
    bool menu_pass(const StepInput& input, const std::function<void()>& picture, int& refreshes);
    // The input with a recording's keys (a demo's) as the first player's too.
    StepInput with_demo(StepInput input) const noexcept {
        input.players[0].keys = static_cast<std::uint16_t>(input.players[0].keys | state_.demo_keys);
        return input;
    }
    void menu_text(int number, int column);
    // What a step's input sets before anything runs: the menu keys, the character typed, the players' views.
    void take(const StepInput& input);

    PlayState state_;
    const assets::GameFiles* files_ = nullptr;
    const LoadedLevel* loaded_;
    std::shared_ptr<LoadedLevel> owned_;    // shared with the snapshots of it
    std::optional<SceneGraphics> cleared_;  // the scene's graphics after the menu's clear
};

// What a picture of the world shows, in a player's view (the first player's: the original's). The sprites' pictures
// are made into `pictures`, which the view points into.
View view_of(const World& world, const LoadedLevel& loaded, PiecePainter& painter, std::vector<PiecePicture>& pictures,
             int player = 0);

}  // namespace lvr
