#pragma once

// The level dump's pictures (docs/architecture.md, "First deliverable"): the collision map (a colour per tile type,
// with a legend) and the world picture (the map with every placement's box and kind on it). Plain C++;
// lv-export writes them as PNG next to the level's JSON.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "assets/assets.hpp"
#include "assets/graphics.hpp"
#include "assets/level.hpp"
#include "lvr/level.hpp"

namespace lvr::exporter {

// A box in a level, in pixels; the edges are inside it.
struct Box {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    bool operator==(const Box&) const = default;
};

// The game's font (DATA.DAT entry 2) has the characters 0x20-0x5f: capitals, digits, some punctuation. Others are
// drawn as a space, small letters as capitals. A glyph's pixels are 3 where nothing is drawn, 2 for the letter
// (drawn in `ink`) and 1 for its shadow (drawn in `shadow`).
inline constexpr std::uint8_t kFontInk = 2;
inline constexpr std::uint8_t kFontShadow = 1;
void draw_text(assets::IndexedImage& image, std::span<const assets::IndexedImage> font, int x, int y,
               std::string_view text, std::uint8_t ink, std::uint8_t shadow);

// The outline of a box, clipped to the image.
void draw_outline(assets::IndexedImage& image, const Box& box, std::uint8_t colour);

// --- The collision map ---

struct TileTypeUse {
    std::uint8_t tile_type = 0;
    std::string name;  // empty: not named yet
    std::size_t cells = 0;
    assets::Rgb colour{};
};

// An entity that is terrain to the others: its box, and the tile type it acts as.
struct TerrainEntity {
    Box box;
    std::uint8_t tile_type = 0;
};

struct CollisionMap {
    // The map, a pixel value per tile type (the type's own number); the entities that are terrain to others as
    // boxes outlined in kOutlineColour, with the colour of the type they act as inside the outline; and under it
    // the legend: a swatch, the type's number in hex, its name, its cells.
    assets::IndexedImage image;
    assets::Palette palette{};
    std::vector<TileTypeUse> legend;  // the types the map uses, in order
};

inline constexpr std::uint8_t kOutlineColour = 0x40;
inline constexpr std::uint8_t kTextColour = 0x41;
inline constexpr std::uint8_t kLegendBackground = 0x42;

assets::Rgb tile_type_colour(std::uint8_t tile_type) noexcept;

CollisionMap collision_map(const Map& map, std::span<const TerrainEntity> terrain_entities,
                           std::span<const assets::IndexedImage> font);

// --- The world picture ---

// The level's map (or a scene's picture) with every placement on it: its box outlined, with its kind's number in
// hex. Pixel values are the level's palette indices.
assets::IndexedImage world_picture(const Level& level, const assets::IndexedImage& map_image,
                                   std::span<const assets::IndexedImage> font);

// --- The SNES backdrop behind the map (Remaster rules' parallax, lvr/backdrop.hpp) ---

struct BackdropPreview {
    assets::IndexedImage image;  // pixel values are the level's palette indices
    std::size_t shown = 0;       // how many of the map's pixels show the backdrop
};

// The whole level with the backdrop where its map shows it, in screens: every 320 x 176 part of the level as the
// original's view shows it with the camera there (within the camera's limits), so the backdrop jumps where two
// parts meet. A backdrop that moves by itself is where it is at the level's start.
BackdropPreview backdrop_preview(const Map& map, const assets::IndexedImage& map_image,
                                 const assets::IndexedImage& backdrop, std::uint16_t scroll_x, std::uint16_t scroll_y);

}  // namespace lvr::exporter
