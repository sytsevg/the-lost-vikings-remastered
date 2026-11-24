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
// the ship.
inline constexpr int kSceneSlots = 48;
std::expected<LoadedLevel, std::string> load_slot(const assets::GameFiles& files, int slot, int database_entry);

// The level's world before its first tick.
World prepare_world(const LoadedLevel& loaded, const Map* map_before = nullptr);

// The level's world as it starts: the Vikings where they start, the camera on the first, the placements that are
// always active or in view, and the one tick the original runs before the first picture. `loaded` must outlive it.
World start_world(const LoadedLevel& loaded);

// A level being played, a step of input at a time, as the original's main loop takes them: a tick (three pictures)
// and the conversation its entities started; or, while a line waits for a key, a pass of that wait.
class Play {
  public:
    // A level played for itself: it can't go on to another scene.
    explicit Play(const LoadedLevel& loaded) : world{start_world(loaded)}, loaded_{&loaded} {}
    // One that goes on: to the game-over scene when the Vikings are all dead, and from there back into the level.
    // The scenes are loaded from `files`, which must outlive it.
    Play(const assets::GameFiles& files, std::unique_ptr<LoadedLevel> first, int slot)
        : world{start_world(*first)}, files_{&files}, loaded_{first.get()}, owned_{std::move(first)} {
        world.scene = slot;
        world.next_scene = loaded_->level.next_slot;
        start_demo(slot);
    }

    // What the world shown now was loaded from.
    const LoadedLevel& loaded() const noexcept { return *loaded_; }
    // The pictures to draw it with: the scene's, or (once the title's menu has cleared the screen) a black screen
    // with what the scene showed in the HUD's place a little below its top.
    const SceneGraphics& graphics();

    // One step. `picture` is called each time the world is to be shown. Returns how many refreshes of the display
    // the step takes (a tick's three, three for a wait's pass; a change of scene: 145 more); a step can show fewer
    // pictures than that.
    template <typename Picture>
    int step(std::uint16_t keys, const Picture& picture) {
        constexpr int kTick = 3;
        int refreshes = kTick;
        bool said = false;
        scene_loaded = false;
        if (world.waiting_for_key) {
            world.read_keys(keys);
            if ((world.pressed & key::kAction) == 0) {
                world.turn_colours();
                picture();
                return refreshes;
            }
            world.read_keys(keys);
            world.waiting_for_key = false;
            said = true;
        } else {
            refreshes = 0;
            const std::function<void()> shown = [&picture] { picture(); };
            if (menu_ == Menu::kQuitBox) {
                if (menu_pass(keys, shown, refreshes) && world.game_over) {  // "yes", or the game was over anyway
                    ended = true;
                }
                return refreshes;
            }
            if (menu_ == Menu::kPause) {
                if (!menu_pass(keys, shown, refreshes)) {
                    return refreshes;
                }
                // The tick it held goes on from here.
            } else {
                world.read_keys(static_cast<std::uint16_t>(keys | demo_keys_));
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
                keys = 0;
            }
            if (world.inventory.open || inventory_wanted(world)) {
                // The inventory screen holds the tick: a pass and a picture a step, one refresh each. Opening
                // it shows the playfield once more, and its first pass follows at once.
                if (!world.inventory.open) {
                    open_inventory(world);
                    count_picture(world.pieces);
                    picture();
                    ++refreshes;
                    world.read_keys(keys);
                }
                const bool closed = inventory_pass(world);
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
                if (playback_ || loaded_->level.quits_at_once) {  // a demo, or a scene that isn't played: at once
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
        if (world.show_lines(keys)) {
            refreshes += kTick;  // the wait's first pass
        }
        if (said) {
            picture();
        }
        return refreshes;
    }

    World world;
    // The keys the menus read as held, apart from the keyboard word: set before a step.
    struct MenuKeys {
        bool yes = false;   // Y
        bool no = false;    // N
        bool quit = false;  // Alt+X, Alt+Q or F10
        // Alt+S, Alt+M: the key was pressed (or repeated) with Alt held, and hasn't been acted on yet. A tick acts
        // on it after its pictures, and clears it.
        bool effects = false;
        bool music = false;
        // F5, F6: the level keys, which only do something once the title was given the cheat's password.
        bool previous_level = false;
        bool next_level = false;
    };
    MenuKeys menu_keys;
    // While a scene fades out or in: how much darker every colour is shown (0-70).
    std::optional<std::uint8_t> fade;
    // The last step loaded a scene. The original's level start clears its keyboard word then: a key held across
    // the change counts only when it is pressed again, and what a demo's end left in the word is gone.
    bool scene_loaded = false;
    bool ended = false;  // the game went on to a scene the engine doesn't have yet, or one couldn't be loaded
    std::string error;   // why, if it couldn't

  private:
    // After a tick (the original's 08C8): Alt+S switches the effects off or on, Alt+M the music.
    void switch_sound() {
        if (menu_keys.effects) {
            effects_on_ = !effects_on_;
            world.cues.push_back(
                {.kind = SoundCue::Kind::kEffectsOn, .value = static_cast<std::uint16_t>(effects_on_ ? 1 : 0)});
            menu_keys.effects = false;
        }
        if (menu_keys.music) {
            music_on_ = !music_on_;
            world.cues.push_back(
                {.kind = SoundCue::Kind::kMusicOn, .value = static_cast<std::uint16_t>(music_on_ ? 1 : 0)});
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
    bool effects_on_ = true;
    bool music_on_ = true;
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

    // The pause menu (Esc or P: text 2) and the quit box (Alt+X, Alt+Q, F10: text 3 with the level's
    // password): the game holds while the chosen word blinks. Left and right choose, fire takes the choice, Esc
    // or P and the N key are "no", the Y key "yes". Giving up is the game over; quitting ends the game.
    enum class Menu : std::uint8_t { kNone, kPause, kQuitBox };
    bool pause_wanted() const noexcept;
    void open_menu(bool quit_box);
    // One pass: three refreshes. True when a choice closed the menu.
    bool menu_pass(std::uint16_t keys, const std::function<void()>& picture, int& refreshes);
    void menu_text(int number, int column);

    Menu menu_ = Menu::kNone;
    std::uint16_t menu_choice_ = 1;  // 0 yes, 1 no
    std::uint16_t menu_blink_ = 0;   // counts down a pass; every 16th changes the chosen word
    bool menu_tinted_ = false;       // the menu darkened the picture itself

    const assets::GameFiles* files_ = nullptr;
    const LoadedLevel* loaded_;
    std::unique_ptr<LoadedLevel> owned_;
    std::optional<SceneGraphics> cleared_;  // the scene's graphics after the menu's clear
    // Demos: the scenes played from a recording (the village's, the ending's, and the levels the title shows).
    bool playback_ = false;
    bool demo_wanted_ = false;              // the next scene is one: the title asked, or the scene before it
    int demo_index_ = 0;                    // which of the title's demo levels is next
    std::vector<std::uint16_t> recording_;  // pairs: for how many ticks, which keys
    std::size_t demo_at_ = 0;
    std::uint16_t demo_count_ = 0;
    std::uint16_t demo_word_ = 0;
    std::uint16_t demo_keys_ = 0;
};

// What a picture of the world shows. The sprites' pictures are made into `pictures`, which the view points into.
View view_of(const World& world, const LoadedLevel& loaded, PiecePainter& painter, std::vector<PiecePicture>& pictures);

}  // namespace lvr
