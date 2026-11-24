#pragma once

// Levels (docs/formats/level.md): the level header entry that ties a level's tilemap,
// tileset, metatiles, palettes and objects together.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

#include "assets/data_dat.hpp"
#include "assets/reader.hpp"

namespace lvr::assets {

// --- Level header ---

struct PaletteRef {
    std::uint16_t entry = 0;       // palette entry in DATA.DAT
    std::uint8_t first_color = 0;  // where it goes in the 256-colour palette
};

// An object placed in the level. Its type indexes the world's object database (script.hpp).
struct ObjectPlacement {
    std::uint16_t x = 0;  // centre, in pixels
    std::uint16_t y = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t type = 0;
    std::uint16_t flags = 0;
    std::uint16_t arg = 0;  // object-specific, e.g. which item, or which objects a button controls
};

// Colour cycling over a range of palette colours (decoded in M2). Stored as 3 bytes plus a list of u16 values ended
// by 0xffff; the list is empty in all but one animation.
struct PaletteAnimation {
    std::uint8_t delay = 0;  // a count-down start, per liblv; 0 would end the list
    std::uint8_t from_color = 0;
    std::uint8_t to_color = 0;  // lower than from_color in 24 of 164 animations
    std::vector<std::uint16_t> values;
};

// A sprite entry the level loads into the sprite memory that objects draw from. The two u16 after the entry are
// not read by the game's level loader. Our reading of them: `slot` is where the
// set goes in a numbering where [slot, slot + sheet columns) never overlaps another set of the same level, and
// `sheet` is the size of the artist's sprite sheet in 8-pixel blocks (high byte rows, low byte columns).
struct SpriteSetRef {
    std::uint16_t entry = 0;
    std::uint16_t slot = 0;
    std::uint16_t sheet = 0;
};

// A sprite entry from the header's second list: mostly packed32 sets (the Vikings, enemies), but also the game-over
// scene's lightning (entry 368, unpacked). The game loads these into a separate buffer. The three bytes are 4, 4, then
// 0 for the Vikings' sets and 1 for all others.
struct PackedSetRef {
    std::uint16_t entry = 0;
    std::array<std::uint8_t, 3> fields{};
};

// How the level starts (header byte 0x07, docs/original/objects.md#level-start).
namespace start_mode {
inline constexpr std::uint8_t kScene = 0;       // one object of `start_type` at the start position
inline constexpr std::uint8_t kAtPosition = 1;  // the Vikings at the start position, 32 pixels apart
inline constexpr std::uint8_t kDropIn = 0x10;   // as kAtPosition, the second and third 16 and 8 pixels higher
// 2, 4, 5: the Vikings at the places in VIKINGS.EXE's tables (VikingStartTable)
}  // namespace start_mode

struct LevelHeader {
    std::uint16_t push = 0;           // 0x00: added to field 12 of objects with flag 0x4000, per tick (8.8 pixels)
    std::uint16_t gravity = 0;        // 0x02: added to field 14 of objects with flag 0x8000, per tick (8.8 pixels)
    std::uint8_t music_at_start = 0;  // 0x04: 0 play the tune, 1 / 4 keep the current music, 3 load the tune only
    std::uint8_t tune = 0;            // 0x05: index into kTunes
    std::uint8_t music_at_end = 0;    // 0x06: 2 fade out, 1 keep playing
    std::uint8_t start_mode = 0;      // 0x07: see start_mode
    std::uint16_t start_x = 0;        // 0x08
    std::uint16_t start_y = 0;        // 0x0a
    std::uint16_t start_type = 0;     // 0x0c: a scene's object type (mode 0)
    std::uint16_t start_flags = 0;    // 0x0e: flags of the started objects
    std::uint16_t start_arg = 0;      // 0x10: field 16 of a scene's object (mode 0)
    std::uint16_t switches = 0;       // 0x12: the level's switches at the start, a bit each
    std::uint16_t word_14 = 0;        // 0x14: read by the scripts of database 453 (docs/formats/unknowns.md)
    std::uint16_t next_slot = 0;      // 0x16: the slot that follows
    std::uint8_t flags = 0;           // 0x1c: docs/original/game-flow.md#header-flags-offset-0x1c-read-by-the-loader
    std::uint16_t width = 0;          // in metatiles
    std::uint16_t height = 0;         // in metatiles
    std::uint16_t map_entry = 0;
    std::uint16_t tileset_entry = 0;  // the tile transparency mask is the entry right after it
    std::uint16_t metatiles_entry = 0;
    std::vector<ObjectPlacement> objects;
    std::vector<PaletteRef> palettes;  // apply in order: later ones overwrite earlier colours
    std::uint16_t palette_animation_flags = 0;
    std::vector<PaletteAnimation> palette_animations;
    std::vector<SpriteSetRef> sprite_sets;
    std::vector<PackedSetRef> packed_sets;
    std::size_t end = 0;  // bytes parsed: the whole entry for every level and scene header

    // False for image scenes, whose map entry is a full-screen image instead of a tilemap.
    bool is_tiled() const noexcept { return tileset_entry != kNoEntry && metatiles_entry != kNoEntry; }
};

// --- Viking start positions (VIKINGS.EXE ds:8508) ---

// Where a level that starts in mode 2, 4 or 5 (header byte 7) puts the three Vikings: in spawn order, object types
// 1, 0 and 2. The flags are or-ed with the header's (docs/original/objects.md#level-start).
struct VikingStart {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t flags = 0;
};

using VikingStartTable = std::array<VikingStart, 3>;

// Three tables of three (x, y, flags) words: modes 2, 4 and 5, in that order.
std::expected<std::array<VikingStartTable, 3>, Error> parse_viking_starts(std::span<const std::byte> data);

// Parses the header (see docs/formats/level.md). The bytes no code reads (0x18-0x1b, 0x1d-0x28, 0x2d) are skipped.
std::expected<LevelHeader, Error> parse_level_header(std::span<const std::byte> data);

}  // namespace lvr::assets
