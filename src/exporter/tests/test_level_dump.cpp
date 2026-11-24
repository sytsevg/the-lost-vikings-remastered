// Tests for src/exporter/level_dump.*: the level dump's pictures, with a [real-data] check of level 1's.
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "assets/assets.hpp"
#include "assets/catalog.hpp"
#include "assets/graphics.hpp"
#include "exporter/level_dump.hpp"
#include "lvr/level.hpp"

using namespace lvr;
using namespace lvr::exporter;

namespace {

// 80 glyphs like the game's font, filled with 3; the one for 'A' (0x41 - 0x10) has a pixel of the letter and one
// of its shadow.
std::vector<assets::IndexedImage> sample_font() {
    assets::IndexedImage blank = assets::make_image(8, 8);
    std::ranges::fill(blank.pixels, std::uint8_t{3});
    std::vector<assets::IndexedImage> font(80, blank);
    font.at(0x31).set(0, 0, kFontInk);
    font.at(0x31).set(7, 7, kFontShadow);
    font.at(0x20).set(3, 3, kFontInk);  // '0'
    return font;
}

std::size_t count(const assets::IndexedImage& image, std::uint8_t colour) {
    return static_cast<std::size_t>(std::ranges::count(image.pixels, colour));
}

Map sample_map() {
    // 2 x 2 cells: free, ground / an unnamed type, a slope.
    return Map{.width = 2,
               .height = 2,
               .cells = {Cell{.metatile = 0, .tile_type = 0x00}, Cell{.metatile = 1, .tile_type = 0x01},
                         Cell{.metatile = 2, .tile_type = 0x0d}, Cell{.metatile = 3, .tile_type = 0x31}}};
}

}  // namespace

TEST_CASE("draw_text: the letters and their shadows, small letters as capitals, clipped") {
    const auto font = sample_font();
    assets::IndexedImage image = assets::make_image(24, 8);
    draw_text(image, font, 0, 0, "Aa~", 7, 8);
    CHECK(image.at(0, 0) == 7);
    CHECK(image.at(7, 7) == 8);
    CHECK(image.at(8, 0) == 7);   // 'a' as 'A'
    CHECK(count(image, 7) == 2);  // '~' has no glyph; a glyph's background isn't drawn
    CHECK(count(image, 8) == 2);
    draw_text(image, font, 20, 4, "A", 5, 6);  // partly outside
    CHECK(image.at(20, 4) == 5);
    CHECK(count(image, 5) == 1);
    CHECK(count(image, 6) == 0);
    draw_text(image, font, -100, -100, "A 0", 5, 6);
    CHECK(count(image, 5) == 1);
}

TEST_CASE("draw_outline: the box's edges, clipped") {
    assets::IndexedImage image = assets::make_image(8, 8);
    draw_outline(image, {.left = 1, .top = 2, .right = 4, .bottom = 5}, 9);
    CHECK(count(image, 9) == 12);
    CHECK(image.at(1, 2) == 9);
    CHECK(image.at(4, 5) == 9);
    CHECK(image.at(2, 3) == 0);
    draw_outline(image, {.left = -5, .top = -5, .right = 20, .bottom = 0}, 4);
    CHECK(count(image, 4) == 8);  // only its bottom edge is inside
}

TEST_CASE("tile type colours: one each, the same in every map") {
    std::vector<assets::Rgb> colours;
    for (std::uint8_t type = 0; type < tile::kCount; ++type) {
        colours.push_back(tile_type_colour(type));
    }
    for (std::size_t a = 0; a < colours.size(); ++a) {
        for (std::size_t b = a + 1; b < colours.size(); ++b) {
            CAPTURE(a, b);
            CHECK_FALSE(colours.at(a) == colours.at(b));
        }
    }
}

TEST_CASE("collision map: a colour per tile type, terrain entities outlined, the legend under it") {
    const auto font = sample_font();
    const std::array<TerrainEntity, 1> entities{
        TerrainEntity{.box = {.left = 4, .top = 4, .right = 11, .bottom = 9}, .tile_type = 0x01}};
    const CollisionMap collision = collision_map(sample_map(), entities, font);

    REQUIRE(collision.legend.size() == 4);
    CHECK(collision.legend.at(0).tile_type == 0x00);
    CHECK(collision.legend.at(0).name == "free");
    CHECK(collision.legend.at(0).cells == 1);
    CHECK(collision.legend.at(2).tile_type == 0x0d);
    CHECK(collision.legend.at(2).name.empty());
    CHECK(collision.legend.at(3).name == "slope 1");
    CHECK(collision.legend.at(3).colour == tile_type_colour(0x31));

    // The pixel values are the tile types.
    CHECK(collision.image.at(15, 15) == 0x00);
    CHECK(collision.image.at(16, 0) == 0x01);
    CHECK(collision.image.at(0, 16) == 0x0d);
    CHECK(collision.image.at(31, 31) == 0x31);
    CHECK(collision.palette.at(0x0d) == tile_type_colour(0x0d));
    // The outline.
    CHECK(collision.image.at(4, 4) == kOutlineColour);
    CHECK(collision.image.at(11, 9) == kOutlineColour);
    CHECK(collision.image.at(5, 5) == 0x01);  // inside the outline: the type it acts as
    CHECK(collision.image.at(6, 6) == 0x00);
    // The legend: wider than this small map, a line per type and one for the outline, below the map.
    CHECK(collision.image.width == 232);
    CHECK(collision.image.height == 32 + 8 + (5 * 10));
    CHECK(collision.image.at(100, 36) == kLegendBackground);
    CHECK(collision.image.at(5, 37) == 0x00);             // the first swatch
    CHECK(collision.image.at(5, 37 + 30) == 0x31);        // the fourth
    CHECK(count(collision.image, kTextColour) > 4 * 28);  // the swatches' frames and the text ('0', 'A')
    CHECK(collision.image.at(232 - 1, 36) == kLegendBackground);

    // No entities: no line for them.
    const CollisionMap plain = collision_map(sample_map(), {}, font);
    CHECK(plain.image.height == 32 + 8 + (4 * 10));
    CHECK(count(plain.image, kOutlineColour) == 0);
}

TEST_CASE("collision map: a wide map has the legend in columns") {
    Map map{.width = 30, .height = 1, .cells = {}};
    for (int i = 0; i < 30; ++i) {
        map.cells.push_back(Cell{.metatile = 0, .tile_type = static_cast<std::uint8_t>(i < 5 ? i : 0)});
    }
    const CollisionMap collision = collision_map(map, {}, sample_font());
    REQUIRE(collision.legend.size() == 5);
    CHECK(collision.legend.at(0).cells == 26);
    CHECK(collision.image.width == 480);
    CHECK(collision.image.height == 16 + 8 + (3 * 10));  // 5 lines in 2 columns
    CHECK(collision.image.at(232 + 5, 16 + 5) == 0x03);  // the second column's first swatch
}
