#pragma once

// A level or cutscene as the engine has it (docs/architecture.md): the map with its tile types, the
// placements, and what the level starts with. Built from the decoders' data (assets::LevelData / SceneData) when a
// scene is loaded; nothing here reads a file. A World starts as a copy of these and changes its own.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "assets/assets.hpp"
#include "assets/graphics.hpp"
#include "assets/level.hpp"

namespace lvr {

inline constexpr int kCellSize = 16;  // a map cell, in pixels
// The column or row of the map a pixel is in (the original takes the coordinate as an unsigned word).
constexpr int cell_of(int pixel) noexcept {
    return static_cast<std::uint16_t>(pixel) >> 4U;
}

// --- Tile types ---

// What a cell is to the entities that touch it: a number 0-63. The engine's own code gives a few of them a
// meaning (Terrain, from docs/original/terrain.md); what the others do is up to the kinds that test for them. They
// get their names here as their behaviour is translated; the ones below are named from the code or from where the
// levels have them (the level dump's collision maps; 0x0a-0x17 by the user, 2026-11-18). 0x0d is still unnamed.
namespace tile {
// What a cell of the map is to the entities (the original's numbers). An entity can act as one of these too
// (Entity::acts_as): the others' terrain tests then find it like a cell; the numbers from 0x29 on are only that.
inline constexpr std::uint8_t kFree = 0x00;
inline constexpr std::uint8_t kGround = 0x01;  // solid: a floor, a wall; also what lies left and right of the map
inline constexpr std::uint8_t kGround02 =
    0x02;                                      // solid too, and Erik's head breaks it; what else sets it apart is open
inline constexpr std::uint8_t kLadder = 0x03;  // free to walk through
inline constexpr std::uint8_t kLadderTop = 0x04;       // ground that an entity put on the ground steps up onto
inline constexpr std::uint8_t kPlatform = 0x05;        // stood on, but no wall: lifts, moving platforms, plates
inline constexpr std::uint8_t kShieldOverhead = 0x06;  // Olaf's shield held up: stood on, and things land on it
inline constexpr std::uint8_t kShield = 0x07;          // his shield held before him: it stops enemies and shots
inline constexpr std::uint8_t kPlatformStop = 0x0a;    // where a moving platform's way ends
inline constexpr std::uint8_t kHiddenPlatform = 0x0b;  // a platform that appears when its button is shot
inline constexpr std::uint8_t kBarrier = 0x0c;         // free to the engine; enemies turn round at it
inline constexpr std::uint8_t kLadder0D = 0x0d;        // in the ladders' list; levels 35 and 36 only; open
inline constexpr std::uint8_t kWater = 0x10;           // a Viking drowns in it
inline constexpr std::uint8_t kLava11 = 0x11;          // listed with the lava; no level seen using it
inline constexpr std::uint8_t kLava = 0x12;            // the prehistoric world's
inline constexpr std::uint8_t kQuicksand = 0x13;       // a Viking drowns in it
inline constexpr std::uint8_t kSpikes = 0x14;
inline constexpr std::uint8_t kElectricity = 0x15;     // deadly to fall into
inline constexpr std::uint8_t kSpring = 0x16;          // bounces
inline constexpr std::uint8_t kRotatingSpikes = 0x17;  // deadly to fall into
inline constexpr std::uint8_t kGround20 = 0x20;     // solid, and Erik's head breaks it; what else sets it apart is open
inline constexpr std::uint8_t kBreakable21 = 0x21;  // only in the list of what Erik's head breaks
inline constexpr std::uint8_t kBreakable22 = 0x22;
inline constexpr std::uint8_t kThing = 0x29;  // an entity a Viking can act on: a switch, a hint, someone to talk to
inline constexpr std::uint8_t kTaker = 0x2a;  // an entity that takes an item the level asks for
inline constexpr std::uint8_t kLock = 0x2c;   // an entity that takes a key
inline constexpr std::uint8_t kBody = 0x2d;   // a dead Viking, an enemy's remains
inline constexpr std::uint8_t kEnemy = 0x2e;
inline constexpr std::uint8_t kViking = 0x2f;
inline constexpr std::uint8_t kFirstSlope = 0x30;  // 0x30-0x3f: the surface's height from the slope table
inline constexpr std::uint8_t kLastSlope = 0x3f;
inline constexpr std::uint8_t kCount = 64;
}  // namespace tile

enum class Terrain : std::uint8_t {
    kFree,     // nothing stops an entity: 0x00, 0x03, 0x0c
    kGround,   // solid: 0x01, 0x02, 0x04, 0x05, 0x20
    kSlope,    // 0x30-0x3f
    kUnknown,  // the engine's own code doesn't say; the kinds' tests decide
};

Terrain terrain(std::uint8_t tile_type) noexcept;

// A name for the level dump's legend: "free", "ground", "ladder", "slope 0" ...; empty when the type has none yet.
std::string tile_type_name(std::uint8_t tile_type);

// --- The map ---

struct Cell {
    std::uint16_t metatile = 0;  // its picture
    std::uint8_t tile_type = 0;

    bool operator==(const Cell&) const = default;
};

struct Map {
    int width = 0;  // in cells
    int height = 0;
    std::vector<Cell> cells;  // row by row

    int pixel_width() const noexcept { return width * kCellSize; }
    int pixel_height() const noexcept { return height * kCellSize; }
    const Cell& at(int column, int row) const {
        return cells.at((static_cast<std::size_t>(row) * static_cast<std::size_t>(width)) +
                        static_cast<std::size_t>(column));
    }
    // The tile type at a pixel. Left and right of the map it is ground, above and below it free (the original's
    // 0000:4199, which looks at the column first).
    std::uint8_t tile_type_at(int x, int y) const noexcept;
    // The cell in that column and row, for changing it; nullptr outside the map.
    Cell* cell(int column, int row) noexcept {
        if (column < 0 || column >= width || row < 0 || row >= height) {
            return nullptr;
        }
        return &cells[(static_cast<std::size_t>(row) * static_cast<std::size_t>(width)) +
                      static_cast<std::size_t>(column)];
    }

    bool operator==(const Map&) const = default;
};

// --- Placements ---

namespace placement_flag {
inline constexpr std::uint16_t kFacingLeft = 0x0040;
inline constexpr std::uint16_t kFlippedVertically = 0x0080;
inline constexpr std::uint16_t kCreatedAgainAtOnce = 0x0100;  // when removed while its placement is in the view
inline constexpr std::uint16_t kAlwaysActive = 0x0800;        // created at the start, never leaves
}  // namespace placement_flag

// Creates an entity when it enters the active area, or at the start. `kind` is the original's object type, a
// number within the level's world, until the kinds have names.
struct Placement {
    int x = 0;  // the entity's centre
    int y = 0;
    int width = 0;  // its box; 0 x 0: the kind's own size
    int height = 0;
    std::uint16_t kind = 0;
    std::uint16_t flags = 0;     // placement_flag, and bits the kinds read
    std::uint16_t argument = 0;  // the kind's own: which item, which switch, which text ...

    bool facing_left() const noexcept { return (flags & placement_flag::kFacingLeft) != 0; }
    bool always_active() const noexcept { return (flags & placement_flag::kAlwaysActive) != 0; }
    bool created_again_at_once() const noexcept { return (flags & placement_flag::kCreatedAgainAtOnce) != 0; }

    bool operator==(const Placement&) const = default;
};

// --- Levels and cutscenes ---

// The original's music instructions, as the header has them (docs/formats/level.md).
struct Music {
    std::uint8_t tune = 0;
    std::uint8_t at_start = 0;  // 0 play the tune, 1 / 4 keep what plays, 3 load the tune only
    std::uint8_t at_end = 0;    // 2 fade out, 1 keep playing
};

struct Level {
    std::string name;   // "level-01" ... "level-37"; a cutscene's own: "galaxy", "ship" ...
    int number = 0;     // 1-37 in the order they are played; 0 for a cutscene
    std::string world;  // "spaceship", "prehistoric" ...; empty for a cutscene

    Map map;                            // empty where the scene is one picture (the starfield)
    std::vector<Placement> start;       // created when the level starts, in this order: the Vikings, or a
                                        // cutscene's one entity
    std::vector<Placement> placements;  // in the level data's order, which the entities' slots follow

    std::uint16_t switches = 0;  // the level's 16 switches at the start: buttons and keyholes flip them, doors,
                                 // lasers, bridges ... each read their own (named by their argument)
    std::uint16_t extra = 0;     // header word 0x14: read by a few kinds of the wacky world; meaning open
    int gravity = 0;             // added to a falling entity's speed every tick, in 256ths of a pixel
    int push = 0;                // the same across, for the entities that ask for it
    bool hud = false;            // the HUD is shown
    int width = 0;  // the header's own size in cells: the camera's limits, also where the scene is one picture
    int height = 0;
    bool text_direct = false;    // the text is drawn straight onto the picture (header flags 0xE0)
    bool quits_at_once = false;  // the quit keys end the game without asking
    Music music;
    int next_slot = 0;  // what follows, as the original's slot number (the galaxy picks its own)

    // The pictures.
    assets::Palette palette{};
    std::vector<assets::PaletteAnimation> palette_animations;
    std::uint16_t palette_animations_running = 0;  // bit i: animation i runs
    std::vector<assets::IndexedImage> tileset;     // 8 x 8 tiles
    std::vector<assets::Metatile> metatiles;       // a cell's picture: 2 x 2 tiles
    std::optional<assets::IndexedImage> picture;   // a scene without a map: its screen
};

// The scenes of assets::kScenes that are cutscenes: worlds with entities. The others (the logos, the title) are
// plain scene code.
inline constexpr std::array<std::string_view, 7> kCutscenes{"home-intro", "home-demo", "home-ending", "galaxy",
                                                            "starfield",  "ship",      "game-over"};

// The map with the tile type split from the picture: a cell's word has the metatile in bits 0-9, the type in 10-15.
Map make_map(const assets::Tilemap& tilemap);

// The entities a level starts with (the original's 0000:1446): the Vikings by the header's start mode, at the places
// from VIKINGS.EXE (`starts`) or in a row at the header's position; a cutscene's one entity.
std::vector<Placement> make_start(const assets::LevelHeader& header,
                                  const std::array<assets::VikingStartTable, 3>& starts);

Level make_level(const assets::LevelData& data, const std::array<assets::VikingStartTable, 3>& starts);
Level make_cutscene(const assets::SceneData& data, const std::array<assets::VikingStartTable, 3>& starts);

}  // namespace lvr
