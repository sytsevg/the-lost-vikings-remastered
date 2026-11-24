// Tests for src/assets/graphics.*.
#include <algorithm>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

#include "assets/data_dat.hpp"
#include "assets/graphics.hpp"
#include "assets/hash.hpp"

using namespace lvr::assets;

namespace {

// A byte vector from integer literals: bytes({0x01, 0xff}).
std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (const int v : values) {
        out.push_back(static_cast<std::byte>(v));
    }
    return out;
}

}  // namespace

// --- Palettes ---

TEST_CASE("palette: 6-bit VGA values expand to 8 bits, starting at the given colour") {
    Palette palette{};
    REQUIRE(load_palette(palette, bytes({0, 32, 63, 1, 2, 3}), 10).has_value());
    CHECK(palette[9] == Rgb{});
    CHECK(palette[10] == Rgb{.r = 0, .g = 130, .b = 255});
    CHECK(palette[11] == Rgb{.r = 4, .g = 8, .b = 12});
}

TEST_CASE("palette: colours past index 255 are ignored, bad sizes rejected") {
    Palette palette{};
    REQUIRE(load_palette(palette, bytes({63, 63, 63, 63, 63, 63}), 255).has_value());
    CHECK(palette[255] == Rgb{.r = 255, .g = 255, .b = 255});
    CHECK(load_palette(palette, bytes({1, 2}), 0).error() == Error::kBadSize);
}

// --- Planar images ---

TEST_CASE("planar: plane 0 holds x % 4 == 0, then plane 1, ...") {
    // 4x2 image: planes of 2 bytes each => pixel values 0..7 in row-major order
    const auto image = decode_planar(bytes({0, 4, 1, 5, 2, 6, 3, 7}), 4, 2);
    REQUIRE(image.has_value());
    CHECK(image->pixels == std::vector<std::uint8_t>{0, 1, 2, 3, 4, 5, 6, 7});
}

TEST_CASE("planar: bad sizes are rejected") {
    CHECK(decode_planar(bytes({0, 1, 2}), 3, 1).error() == Error::kBadSize);
    CHECK(decode_planar(bytes({0, 1, 2}), 4, 1).error() == Error::kTruncated);
    CHECK(decode_tileset(std::vector<std::byte>(65)).error() == Error::kBadSize);
}

// --- Tiles and metatiles ---

namespace {

// Tile whose pixel value is its x coordinate + 8 * y (so flips are visible).
std::vector<IndexedImage> gradient_tileset() {
    IndexedImage tile = make_image(kTileSize, kTileSize);
    for (std::size_t y = 0; y < kTileSize; ++y) {
        for (std::size_t x = 0; x < kTileSize; ++x) {
            tile.set(x, y, static_cast<std::uint8_t>(x + (8 * y)));
        }
    }
    return {tile};
}

}  // namespace

TEST_CASE("metatiles: bits 15-6 are the tile, bits 5-0 the flags") {
    // tile 5, flip h + flip v (0x30) => 0x0170
    const auto metatiles = decode_metatiles(bytes({0x70, 0x01, 0x08, 0x00, 0x00, 0x00, 0x40, 0x00}));
    REQUIRE(metatiles.has_value());
    REQUIRE(metatiles->size() == 1);
    const Metatile& m = (*metatiles)[0];
    CHECK(m.parts[0].tile == 5);
    CHECK(m.parts[0].flip_h());
    CHECK(m.parts[0].flip_v());
    CHECK(m.parts[1].foreground());
    CHECK(m.parts[3].tile == 1);
    CHECK(decode_metatiles(bytes({1, 2, 3})).error() == Error::kBadSize);
}

TEST_CASE("metatiles: drawing applies flips per quarter") {
    const auto tileset = gradient_tileset();
    Metatile m;
    m.parts[1].flags = TileRef::kFlipH;
    m.parts[2].flags = TileRef::kFlipV;
    IndexedImage target = make_image(kMetatileSize, kMetatileSize);
    REQUIRE(draw_metatile(target, 0, 0, m, tileset).has_value());
    CHECK(target.at(0, 0) == 0);   // top-left, unflipped
    CHECK(target.at(8, 0) == 7);   // top-right, flipped horizontally: x=0 shows x=7
    CHECK(target.at(0, 8) == 56);  // bottom-left, flipped vertically: y=0 shows y=7
    CHECK(target.at(15, 15) == 63);

    m.parts[3].tile = 1;  // doesn't exist
    CHECK(draw_metatile(target, 0, 0, m, tileset).error() == Error::kBadReference);
    CHECK(draw_metatile(target, 1, 0, Metatile{}, tileset).error() == Error::kOutOfBounds);
}

// --- Tilemaps ---

TEST_CASE("tilemap: bits 8-0 are the metatile, bits 15-9 the attributes") {
    // metatile 3, attributes 2 => 0x0403
    const auto map = decode_tilemap(bytes({0x03, 0x04, 0x00, 0x00}), 2, 1);
    REQUIRE(map.has_value());
    CHECK(map->at(0, 0).metatile == 3);
    CHECK(map->at(0, 0).attributes == 2);
    CHECK(decode_tilemap(bytes({0, 0, 0, 0, 0, 0}), 2, 1).error() == Error::kBadSize);  // too long
    CHECK(decode_tilemap(bytes({0, 0, 0}), 2, 1).error() == Error::kBadSize);           // odd size
}

TEST_CASE("tilemap: a short map keeps its stored cells and renders the rest as colour 0") {
    const auto tileset = gradient_tileset();
    const std::vector<Metatile> metatiles(1);
    const auto map = decode_tilemap(bytes({0x00, 0x00}), 2, 1);
    REQUIRE(map.has_value());
    CHECK(map->stored_cells == 1);
    CHECK(map->cells.size() == 2);
    const auto image = render_tilemap(*map, metatiles, tileset);
    REQUIRE(image.has_value());
    CHECK(image->at(15, 15) == 63);  // first metatile drawn
    CHECK(image->at(31, 15) == 0);   // second one missing
}

TEST_CASE("tilemap: rendering checks metatile references") {
    const auto tileset = gradient_tileset();
    const std::vector<Metatile> metatiles(1);
    const auto map = decode_tilemap(bytes({0x00, 0x00, 0x01, 0x00}), 2, 1);
    REQUIRE(map.has_value());
    CHECK(render_tilemap(*map, metatiles, tileset).error() == Error::kBadReference);
    const auto ok = decode_tilemap(bytes({0x00, 0x00, 0x00, 0x00}), 2, 1);
    const auto image = render_tilemap(*ok, metatiles, tileset);
    REQUIRE(image.has_value());
    CHECK(image->width == 32);
    CHECK(image->height == 16);
}

// --- Full-screen images ---

TEST_CASE("screen image: u16 plane size, then a 344-pixel-wide planar image") {
    // plane size 86 => 344 pixels => one row
    std::vector<std::byte> data = bytes({86, 0});
    for (std::size_t i = 0; i < 344; ++i) {
        data.push_back(static_cast<std::byte>(i % 4 == 0 ? 7 : 0));  // plane 0 bytes first: pixels x = 0, 4, 8...
    }
    const auto image = decode_screen_image(data);
    REQUIRE(image.has_value());
    CHECK(image->width == kScreenWidth);
    CHECK(image->height == 1);
    CHECK(image->at(0, 0) == 7);
    CHECK(image->at(4, 0) == 0);  // x = 4 is plane 0's second byte, which is data byte 1 (0 here)
    CHECK(image->at(1, 0) == 0);  // plane 1
}

TEST_CASE("screen image: sizes must match the plane size exactly and fill whole rows") {
    CHECK(decode_screen_image(bytes({1})).error() == Error::kTruncated);
    CHECK(decode_screen_image(bytes({1, 0, 0, 0, 0})).error() == Error::kBadSize);     // 4 pixels expected, 3 given
    CHECK(decode_screen_image(bytes({1, 0, 0, 0, 0, 0})).error() == Error::kBadSize);  // 4 pixels: not a whole row
}
