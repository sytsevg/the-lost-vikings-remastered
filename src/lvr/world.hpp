#pragma once

// A world: the live state of a level or cutscene: its map, its entities and placements, and what the
// engine's services need. Creating and removing entities, and the active area, are here; the other services are
// in their own files (terrain.*, hits.*, movement.*, camera.*). The original\'s rules:
// docs/original/objects.md.

#include <array>
#include <bitset>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "assets/objects.hpp"
#include "lvr/camera.hpp"
#include "lvr/dialogue.hpp"
#include "lvr/entity.hpp"
#include "lvr/inventory.hpp"
#include "lvr/level.hpp"
#include "lvr/places.hpp"
#include "lvr/sound.hpp"
#include "lvr/sprites.hpp"

namespace lvr {

// The rules a game is played by (ADR-0005). Vanilla: the original's, exactly. Remaster: ours on top of them (ADR-0011).
enum class Rules : std::uint8_t { kVanilla, kRemaster };

inline constexpr int kSlots = 20;  // Vanilla rules: the original's 20 entities at a time
// Remaster rules: no limit a level could reach (the largest has 83 placements, and what they create).
inline constexpr int kRemasterSlots = 512;
inline constexpr int kVikingSlots = 3;  // the Vikings are in the first three
inline constexpr std::size_t kKinds = assets::kObjectTypeCount;
inline constexpr std::size_t kMostPlacements = 128;
inline constexpr std::size_t kWorlds = 6;

// What every entity of a kind starts with: from the world's object database (docs/original/objects.md#records).
struct KindRecord {
    static constexpr std::uint16_t kNoSprites = 0xffff;
    static constexpr std::uint16_t kPackedSprites = 0xfffe;  // drawn from packed sets: pieces from the middle pool

    std::uint16_t sprite_entry = kNoSprites;  // the DATA.DAT entry of its sprite set
    int pieces = 0;
    Pool pool = Pool::kBack;
    std::uint16_t health = 0;
    std::int16_t width = 0;
    std::int16_t height = 0;
    std::uint16_t kind_value = 0;
    std::uint16_t hit_class = 0;
    std::uint16_t hit_mask = 0;
    std::int16_t max_step_x = 0;
    std::int16_t max_step_y = 0;
};

std::array<KindRecord, kKinds> kind_records(const assets::ObjectDatabase& database);

// A sprite set the level loads: its DATA.DAT entry, and where it starts in the sprite memory, + 1.
struct LoadedSpriteSet {
    std::uint16_t entry = 0;
    std::uint16_t base = 0;
};

// What a new entity is created with.
struct Creation {
    std::uint16_t kind = 0;
    int x = 0;
    int y = 0;
    std::uint16_t flags = 0;
    std::uint16_t argument = 0;
    int placement = -1;   // the placement it comes from
    int half_width = -1;  // the size it leaves the active area by; -1: half its kind's
    int half_height = 0;
    int creator = kNobody;
};

// The part of the view placements are looked for in: all of it, or the strip that came into view.
enum class ViewPart : std::uint8_t { kWhole, kLeft, kRight, kBottom, kTop };

// What a world holds as plain data: everything of it that can be written out as it is (lvr/archive.hpp). The world
// itself adds its entities, and what it has of the level it was made for (the programs, the texts, how a kind is
// made), which a world read back gets from that level again.
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding): its members are grouped by meaning, not by size
struct WorldState {
    Rules rules = Rules::kVanilla;  // a game's, not a scene's: it goes on from scene to scene
    Map map;                        // the level's, as the entities have changed it
    Cell cleared_cell{};            // what a cell becomes where a wall is broken away: the world's empty cell
    Cell filled_cell{.metatile = 0, .tile_type = tile::kGround};  // and where a floor is built: its solid cell
    std::uint16_t broken_picture = 0;  // the picture of a cell where a target was shot away: the world's
    std::uint16_t shot_picture = 0;    // and where another kind of target was (the wacky world's)
    Pieces pieces;                     // as many, laid out, as the rules give (set_rules)
    Camera camera;
    // The height of a slope's surface at each of a cell's 16 columns, by slope (tile type - 0x30): from VIKINGS.EXE.
    std::array<std::uint8_t, 256> slope_heights{};
    std::array<KindRecord, kKinds> kinds{};
    std::vector<LoadedSpriteSet> sprite_sets;  // at most 32
    // The level's placements; entities that leave the active area write where they were back into theirs.
    std::vector<Placement> placements;
    std::bitset<kMostPlacements> gone;     // placements that don't come back: collected, destroyed
    int first_placed_slot = kVikingSlots;  // the slots before it aren't for placements (a cutscene: 1)
    int strip_x = 0;                       // the camera, in cells, when placements last came into view
    int strip_y = 0;
    // Remaster rules' zoom changed the view: the placements it shows are created at the next tick's start (set_view)
    bool view_to_fill = false;
    int gravity = 0;                 // the level's, added to a falling entity's speed every tick
    int level_push = 0;              // the same across
    std::uint16_t keys_before = 0;   // the keys held at the last tick's start
    int updating = kNobody;          // the slot of the entity whose turn it is
    std::vector<int> created_below;  // entities created this turn in a lower slot: their turn comes right after
    bool ending = false;             // the scene ends: what is removed now isn't created again
    std::vector<int> created_late;   // placements created again in a slot the tick's turns have passed
    // What the animations asked for that this world doesn't have, by kind (for the spawn sweep test): a broken
    // program (one not in this world's database, or not paired with it), a packed set the level doesn't load.
    struct Missing {
        std::uint16_t kind;
        std::uint16_t what;  // the program where it broke, or the packed set's DATA.DAT entry
        bool program;
        bool operator==(const Missing&) const = default;
    };
    std::vector<Missing> missing;
    int database = database::kSpaceship;  // this world's
    AnimationTables animation_tables{};
    std::vector<std::uint16_t> packed_entries;  // the level's packed sets, as DATA.DAT entries
    std::vector<SoundCue> cues;                 // what was reported to the sound side since it was last taken
    // Random numbers, as the original draws them: every kind draws from the same sequence, so the order of the
    // draws is part of how a level plays. A demo has a sequence of its own, so it plays the same every time.
    std::uint32_t seed = 0;
    std::uint16_t demo_seed = 30;
    int level = 1;           // 1-37
    int scene = 0;           // the scene, as the original numbers them: 0-36 the levels, 37 game over, 38-47 the others
    int previous_scene = 0;  // the scene before it
    int next_scene = 0;      // what follows it; a scene's entities can say otherwise
    bool leave = false;      // the scene is over: on to the next
    std::array<std::uint8_t, 4> password{};              // the level's
    std::vector<std::array<std::uint8_t, 4>> passwords;  // every level's, in order: typed at the title, one leads there
    // What the title's scene leaves for the game around it.
    struct Title {
        bool demo_wanted = false;     // it was left alone: a demo follows
        bool cheat = false;           // the cheat's password was typed: F5 and F6 change the level
        bool quit = false;            // its menu: the game ends
        bool screen_cleared = false;  // its menu cleared the scene's picture; what that showed below is at the top
    } title;
    std::array<std::uint8_t, 3> tint{};   // how much darker red, green and blue are shown (the title's menu, the alarm)
    std::array<std::uint8_t, 3> tint2{};  // a second one on top of it: a bomb's flash
    int retries = 0;                      // how often the Vikings have lost this level in a row
    bool after_game_over = false;         // the level starts again after a game over
    bool attract = false;                 // the level is played by a recording (the title's demos)
    std::uint16_t switches = 0;           // the level's 16 switches: buttons flip them; doors, barriers ... read theirs
    std::uint16_t more_switches = 0;      // a second set of 16, for the levels that need more
    std::uint16_t hints_seen = 0;         // a bit per hint the Vikings have been shown
    struct Beam {  // where the Vikings told so (message 0x20, the ship's last level) are beamed to
        int x = 0;
        int y = 0;
        bool faces_right = false;
    } beam;
    std::array<std::uint16_t, 8> values{};  // eight numbers some levels' kinds count with; set as a level starts
    bool pushing_sound = false;             // the scrape of a block being pushed is playing
    std::uint16_t vikings_arriving = 0;     // a bit per Viking still on his way in, where a level starts with that
    std::uint16_t vikings_falling_in = 0;   // a bit per Viking that starts the level falling in, until he lands
    // The factory: its cranes, magnets, furnaces and fire jets.
    struct Factory {
        bool part_fitted = false;                  // the machine that takes item 0D has it
        std::array<std::uint16_t, 3> cranes{};     // its three cranes: what the Viking at the controls asks
        std::array<std::uint16_t, 3> burn_wait{};  // the glowing furnaces: ticks until each Viking burns again
        int magnets_on = 0;                        // how many magnets hum
        int jets_on = 0;                           // how many fire jets are on: their sound plays while any is
    } factory;
    // Tomator's level.
    struct Tomator {
        int force_field = kNobody;  // the slot of his force field while it is up
    } tomator;
    // The game-over scene: the health each Viking was left with (0: dead; its figures show by it, and its
    // lightning gives a dead one his back), and how it ends.
    struct GameOverScene {
        std::array<std::uint16_t, kVikingSlots> kept_health{};
        bool letters_fall = false;  // the players gave up: GAME OVER drops in
        bool try_again = false;     // ... or the level starts again
        bool give_up = false;       // ... or the game goes back to the title
    } game_over_scene;
    static constexpr int kSquares = 4;                                      // a Viking's squares in the HUD
    std::array<std::array<std::uint16_t, kSquares>, kVikingSlots> items{};  // by Viking, by square; 0: nothing
    std::array<std::uint16_t, kVikingSlots> armour{};  // the shield item's extra point of health, x 2, per Viking
    std::array<int, kVikingSlots> chosen_square{};     // the square each Viking uses from
    std::array<std::uint16_t, std::size_t{kSquares} * kVikingSlots> shown_items{};  // as the HUD last drew them
    bool has_hud = false;  // the level shows the HUD: the inventory can be opened
    Inventory inventory;
    std::vector<Line> lines;  // what the entities said this tick: shown after it, line by line
    std::size_t next_line = 0;
    TextLayer text;
    bool waiting_for_key = false;  // a line holds the game until an action key is pressed
    // A range of colours that goes round by one every `delay` ticks: lights, lava, water. From a lower to a
    // higher colour they move up and the last comes round to the first; the other way round otherwise.
    struct ColourCycle {
        std::uint8_t delay = 0;
        std::uint8_t left = 0;  // ticks until the next turn
        std::uint8_t from = 0;
        std::uint8_t to = 0;
        bool running = false;
    };
    assets::Palette palette{};        // as the cycles have turned it
    std::vector<ColourCycle> cycles;  // at most 8
    bool has_vikings = true;          // a level, not a cutscene
    std::uint16_t held = 0;           // this tick's input: the keys held, and of them the ones newly pressed
    std::uint16_t pressed = 0;        // (docs/original/input.md)
    // Remaster rules: how fast the Vikings walk and climb this tick (a gamepad's stick pushed part way).
    Pace pace;
    int current_viking = 0;      // the slot of the Viking being played; kNobody when all are dead
    int shown_viking = kNobody;  // the one that last blinked as it was chosen
    int blink = 0;               // ticks of that blinking left
    bool game_over = false;      // the last Viking died: the scene ends
    bool level_done = false;     // the Vikings have all reached the exit: the scene ends
    std::uint16_t typed = 0;     // the last character typed (Y and N answer the scene's question)
    // The screen's shake, per axis: the view is off by `offset`, which flips between 0 and `amount` every tick
    // while `ticks` last.
    struct Shake {
        std::uint16_t amount = 0;
        std::uint16_t offset = 0;
        std::uint16_t ticks = 0;

        bool operator==(const Shake&) const = default;
    };
    Shake shake_x;
    Shake shake_y;
    int last_nearest_viking = 0;
};

// The world: its state (above), its entities, and the engine's services to them.
class World : public WorldState {
  public:
    // An entity is created in the lowest free slot; they are updated and drawn in slot order.
    // As many slots as the rules give (set_rules); they are not added to while a world runs.
    std::vector<OwnedEntity> entities = std::vector<OwnedEntity>(kSlots);
    int slots() const noexcept { return static_cast<int>(entities.size()); }
    // The entity in a slot; nullptr if the slot is empty, or is no slot (kNobody).
    Entity* at(int slot) noexcept {
        return slot >= 0 && slot < slots() ? entities[static_cast<std::size_t>(slot)].get() : nullptr;
    }
    const Entity* at(int slot) const noexcept {
        return slot >= 0 && slot < slots() ? entities[static_cast<std::size_t>(slot)].get() : nullptr;
    }
    // The rules, and the room for entities and sprite pieces they give. Before anything is created.
    void set_rules(Rules now) {
        rules = now;
        entities.resize(now == Rules::kRemaster ? kRemasterSlots : kSlots);
        pieces = Pieces{now == Rules::kRemaster ? kRemasterPieces : kOriginalPieces};
    }
    // The piece of a number; the numbers go round (a sprite's pieces may wrap past the last).
    Piece& piece_at(int index) { return pieces.at(static_cast<std::size_t>(index) % pieces.size()); }

    // Makes an entity of a kind, with its behaviour. Kinds whose behaviour isn't written yet stand still.
    std::function<std::unique_ptr<Entity>(std::uint16_t kind)> make;

    // The world written out (lvr/archive.hpp): its state, then each slot's entity, by its kind; each with its
    // length. And read back, into a world made for the same level (play.hpp's prepare_world: it has the level's
    // programs and texts and makes its kinds); false if what was read isn't a whole world as this engine would
    // write it (the state or a kind has another length by now).
    void save(Writer& to) const;
    bool load(Reader& from);

    // --- The tick ---

    // Tells the entity in a slot something (message::), if one is there; with a number that goes with it, or
    // leaving that as it is.
    void tell(int slot, std::uint16_t what) {
        if (Entity* whom = at(slot)) {
            whom->message = what;
        }
    }
    void tell(int slot, std::uint16_t what, int with) {
        if (Entity* whom = at(slot)) {
            whom->message = what;
            whom->message_value = static_cast<std::uint16_t>(with);
        }
    }

    // A tick is three pictures. Part 0 is the tick itself (the keys read, every entity's turn, the moves, what
    // stops them) up to its first picture; parts 1 and 2 are what happens before the second and third.
    void step(int part, std::uint16_t keys = 0);
    // The same with the keys read already (read_keys): the inventory screen opens between the two.
    void tick_part(int part);
    // The same without what comes before the entities' turns (the keys, the Viking switch, placements scrolled
    // in). `starting`: the one tick a level runs as it starts, in which no Viking falls back and nothing shakes.
    void run(int part, bool starting = false);
    // An entity creates another: at (x, y), with its own flags (the engine's bits of them) and `flags`. Returns
    // nullptr if it can't be created. One created in a lower slot has its turn in this tick, after its creator.
    Entity* create_by(Entity& creator, std::uint16_t kind, int x, int y, std::uint16_t argument = 0,
                      std::uint16_t flags = 0);
    // The same, for a creator that has no more to do with what it made.
    void spawn(Entity& creator, std::uint16_t kind, int x, int y, std::uint16_t argument = 0, std::uint16_t flags = 0) {
        (void)create_by(creator, kind, x, y, argument, flags);
    }
    // One tick of an entity's animation, then its speed into its push.
    void animate(Entity& entity);
    // The same for one that walks: after this tick's move it is put onto the ground under it.
    void animate_on_ground(Entity& entity) {
        entity.set_flag(entity_flag::kPutOnGround);
        animate(entity);
    }
    void note_missing(const Missing& what);
    std::span<const std::byte> programs;  // the animation programs (the world's object database)
    // Where this world keeps the programs the kinds name by their place in the spaceship's database; none: the
    // same places.
    const AnimationPlaces* places = nullptr;
    // The same for what a kind names by its place in another world's database (a hazard the spaceship doesn't
    // have, a kind of that world): by home database, 449 first.
    const std::array<AnimationPlaces, kWorlds>* places_from = nullptr;
    // Starts the entity's animation.
    void play(Entity& entity, Animation animation) const;
    // Where `animation` begins in this world's programs (an animation named in another world's database is found
    // where this world has it): what a sprite's `started` says when it plays it.
    std::uint16_t program_of(Animation animation) const;
    // An entity makes a sound: one of the original's effects, by its number. And stops one.
    void sound(const Entity& from, std::uint16_t effect) {
        cues.push_back({.kind = SoundCue::Kind::kEffect, .value = effect, .tune = 0, .entity = from.slot});
    }
    void stop_sound(const Entity& from, std::uint16_t effect) {
        cues.push_back({.kind = SoundCue::Kind::kStopEffect, .value = effect, .tune = 0, .entity = from.slot});
    }
    // The same for a sound that is nobody's: a menu's, or one that several entities share.
    void sound(std::uint16_t effect) {
        cues.push_back({.kind = SoundCue::Kind::kEffect, .value = effect, .tune = 0, .entity = kNobody});
    }
    void stop_sound(std::uint16_t effect) {
        cues.push_back({.kind = SoundCue::Kind::kStopEffect, .value = effect, .tune = 0, .entity = kNobody});
    }

    std::uint16_t random() noexcept;

    // --- The game around the level ---

    // --- What only some worlds' kinds need ---

    // --- What the Vikings carry ---

    // The HUD draws the items that changed. After one it goes on from the square after Olaf's chosen one (a quirk
    // of the original), so a change can take another tick to show.
    void update_hud();

    // --- Conversations ---

    Texts texts;  // the game's texts (VIKINGS.EXE)

    // A framed box with text `number` at a point of the level (its anchor cell there), or at a cell of the screen.
    void say_at_point(int number, int anchor, int x, int y);
    void say_at_cell(int number, int anchor, int column, int row);
    void say(const Line& line) { lines.push_back(line); }
    void colour_boxes(std::uint16_t colour) { say({.kind = Line::Kind::kColour, .value = colour}); }  // per speaker
    void wait_for_key() { say({.kind = Line::Kind::kWaitForKey}); }  // the game holds until an action key
    void clear_text() { say({.kind = Line::Kind::kClear}); }
    // One spoken line: the box in the speaker's colour, held until a key is pressed, then cleared.
    void speak_at_point(std::uint16_t colour, int number, int anchor, int x, int y);
    void speak_at_cell(std::uint16_t colour, int number, int anchor, int column, int row);
    // The lines not shown yet, up to the next wait for a key. True when the conversation holds the game.
    bool show_lines(std::uint16_t keys);
    // Whether an entity may be spoken to now: it is the Viking being played, well inside the view, and the camera
    // has come to rest.
    bool can_be_spoken_to(const Entity& entity) const noexcept;
    bool well_in_view(const Entity& entity) const noexcept;
    // The keys as they are now: held, and newly pressed since they were last read.
    void read_keys(std::uint16_t keys) noexcept;

    // --- The palette ---

    void turn_colours();
    // Sixteen colours from the world's database at `offset` become colours 192-207 and 224-239 (a scene's
    // creature brings its own).
    void load_colours(std::size_t offset);

    // --- A level's rules ---

    // The living Viking nearest to the entity (the distances across and down added up); with none alive, the one
    // found the last time anyone looked.
    int nearest_viking(const Entity& entity);
    // How far the entity in `slot` is from this one: ahead of it and below it are positive.
    struct Offset {
        int ahead = 0;
        int below = 0;
    };
    Offset offset_to(const Entity& entity, int slot) const;

    // The keys that choose the previous or the next Viking: the next one in that direction that has health left.
    void switch_viking();
    // The Viking being played, if it is out of view, takes no input this tick. One chosen since the last tick
    // blinks for 21 ticks.
    void check_viking_in_view();
    // When the Viking being played has died: on to the next living one; with none left the game is over.
    void fall_back_viking();
    void count_down_shake() noexcept;

    // Turns an entity round, or upside down: its box and its pieces mirrored around its position.
    void flip_across(Entity& entity);
    void flip_down(Entity& entity);
    // Its pieces not drawn, and drawn again.
    void hide_pieces(const Entity& entity);
    void show_pieces(const Entity& entity);
    // After the tick's own moves: the entities another one moves. One attached to an entity faces as it does and
    // goes to its place plus its own push; one moved freely goes by its push as it is.
    void move_attached();
    // One picture's share of every moved entity's sprite following it (picture 0, 1 or 2 of the tick).
    void shift_sprites(int picture);

    // A new entity in the lowest free slot, or nullptr: no slot, its placement is gone, the level doesn't load its
    // kind's sprites, or its pool has no room for its pieces.
    Entity* create(const Creation& creation);
    // Removes the entity; one whose placement is created again at once comes back if that is in view. As in the
    // original its slot is free at once (the next entity created may take it, in this same turn). The object itself
    // is kept until the turn that removed it is over: an entity mostly removes itself, in the middle of its own
    // turn, and what it still does on its way out must find it there.
    void remove(int slot);
    // The removed ones are let go of: at the end of a turn.
    void forget_removed() noexcept { removed_.clear(); }

    // The entity of placement `index`, as that placement says.
    Entity* create_placed(int index);
    // At the start: every placement that is always active.
    void create_always_active();
    // The placements in that part of the view that have no entity yet.
    void create_in_view(ViewPart part);
    // Remaster rules' zoom: the view shows `width` x `height` pixels of the level from now on. A larger view
    // grows around the middle of the one before; a smaller one has the Viking being played in its middle. Both
    // within the level, which must be at least that large. The placements the view now shows are created at the
    // next tick's start, as the original creates what comes into view: right before their first turn. (Made at
    // once while the world holds, for a text that waits for a key, they would be drawn before any of their
    // animations put their pieces anywhere: where the pieces' last owners left them.)
    void set_view(int width, int height);
    // Where the camera is put by that, without doing it.
    Camera camera_for_view(int width, int height) const noexcept;
    // A development tool's (vikings-remaster --showcase): the view becomes `width` x `height` with its corner at
    // (x, y) in the level, as near as the level allows, and stays there whatever the Vikings do. What it shows is
    // created at the next tick's start. Not part of the world's state that is written out.
    void hold_camera(int x, int y, int width, int height);
    bool camera_held = false;
    // The view a zoom that asks for `asked` gives here (Remaster rules, ADR-0011): in a level (not a scene with
    // a map of its own: the village) as large as the original's view at least, never more than the level;
    // elsewhere the view as it is.
    std::array<int, 2> view_for(std::array<int, 2> asked) const noexcept;
    // When the camera has crossed a cell's edge since the last time: the placements in the strip that came in.
    void create_scrolled_in();
    // Entities from the fourth slot on that are outside the active area, and not always active, are told so.
    void mark_outside_active_area();

  private:
    std::vector<OwnedEntity> removed_;  // removed in the turn that is running

    struct View {
        int left = 0;
        int right = 0;
        int top = 0;
        int bottom = 0;
    };
    Entity* create_if_in(int index, const View& view);
};

}  // namespace lvr
