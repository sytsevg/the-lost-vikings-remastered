// Tests for src/assets/level.*, including [real-data] checks of all 37 levels.
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <set>
#include <span>
#include <vector>

#include "assets/assets.hpp"
#include "assets/catalog.hpp"
#include "assets/data_dat.hpp"
#include "assets/graphics.hpp"
#include "assets/hash.hpp"
#include "assets/level.hpp"

using namespace lvr::assets;

namespace {

// The user's own game files in gamedata/original/, opened the way the game opens them. [real-data] tests skip when
// the folder is missing.
const lvr::assets::GameFiles& game_or_skip() {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    static const auto files = lvr::assets::GameFiles::open(LVR_ORIGINAL_DIR);
    if (!files) {
        FAIL(files.error());
    }
    return *files;
}

}  // namespace

namespace {

void put_u16(std::vector<std::byte>& data, std::size_t offset, std::uint16_t value) {
    if (data.size() < offset + 2) {
        data.resize(offset + 2);
    }
    data[offset] = static_cast<std::byte>(value & 0xffU);
    data[offset + 1] = static_cast<std::byte>(value >> 8U);
}

std::vector<std::byte> sample_header() {
    std::vector<std::byte> data(0x43);
    put_u16(data, 0x29, 30);   // width
    put_u16(data, 0x2b, 20);   // height
    put_u16(data, 0x2e, 199);  // map
    put_u16(data, 0x30, 195);  // tileset
    put_u16(data, 0x32, 197);  // metatiles
    // one object: x, y, half width, half height, type, flags, arg
    const std::uint16_t object[] = {100, 200, 16, 18, 7, 0x40, 3};
    for (std::size_t i = 0; i < 7; ++i) {
        put_u16(data, 0x43 + (i * 2), object[i]);
    }
    put_u16(data, 0x51, 0xffff);
    // one palette: entry 172 at colour 0, then a terminator
    put_u16(data, 0x53, 172);
    data.push_back(std::byte{0});
    put_u16(data, 0x56, 0xffff);
    // palette animations: flags, one animation (delay 3, colours 120..117, one value), then delay 0
    put_u16(data, 0x58, 0x0001);
    for (const int b : {3, 120, 117}) {
        data.push_back(static_cast<std::byte>(b));
    }
    put_u16(data, 0x5d, 0x1234);
    put_u16(data, 0x5f, 0xffff);
    data.push_back(std::byte{0});
    // one sprite set (entry 303, slot 0xc0, sheet 0x410), then a terminator
    put_u16(data, 0x62, 303);
    put_u16(data, 0x64, 0xc0);
    put_u16(data, 0x66, 0x410);
    put_u16(data, 0x68, 0xffff);
    // one packed set (entry 221, bytes 4 4 0), then a terminator
    put_u16(data, 0x6a, 221);
    for (const int b : {4, 4, 0}) {
        data.push_back(static_cast<std::byte>(b));
    }
    put_u16(data, 0x6f, 0xffff);
    return data;
}

}  // namespace

TEST_CASE("level header: map info, objects and palettes") {
    const auto header = parse_level_header(sample_header());
    REQUIRE(header.has_value());
    CHECK(header->width == 30);
    CHECK(header->height == 20);
    CHECK(header->map_entry == 199);
    CHECK(header->tileset_entry == 195);
    CHECK(header->metatiles_entry == 197);
    REQUIRE(header->objects.size() == 1);
    CHECK(header->objects[0].x == 100);
    CHECK(header->objects[0].width == 32);  // stored as half width
    CHECK(header->objects[0].height == 36);
    CHECK(header->objects[0].arg == 3);
    REQUIRE(header->palettes.size() == 1);
    CHECK(header->palettes[0].entry == 172);
}

TEST_CASE("level header: palette animations and the two sprite lists") {
    const auto data = sample_header();
    const auto header = parse_level_header(data);
    REQUIRE(header.has_value());
    CHECK(header->palette_animation_flags == 1);
    REQUIRE(header->palette_animations.size() == 1);
    CHECK(header->palette_animations[0].delay == 3);
    CHECK(header->palette_animations[0].from_color == 120);
    CHECK(header->palette_animations[0].to_color == 117);
    CHECK(header->palette_animations[0].values == std::vector<std::uint16_t>{0x1234});
    REQUIRE(header->sprite_sets.size() == 1);
    CHECK(header->sprite_sets[0].entry == 303);
    CHECK(header->sprite_sets[0].slot == 0xc0);
    CHECK(header->sprite_sets[0].sheet == 0x410);
    REQUIRE(header->packed_sets.size() == 1);
    CHECK(header->packed_sets[0].entry == 221);
    CHECK(header->packed_sets[0].fields == std::array<std::uint8_t, 3>{4, 4, 0});
    CHECK(header->end == data.size());
}

TEST_CASE("level header: truncated input is an error at every length") {
    const auto full = sample_header();
    for (std::size_t length = 0; length < full.size(); ++length) {
        INFO("length " << length);
        CHECK_FALSE(parse_level_header(std::span{full}.first(length)).has_value());
    }
}

// --- Real data ---

TEST_CASE("real data: all 37 levels decode and render", "[real-data]") {
    const Archive* const archive = &game_or_skip().archive();

    for (const LevelInfo& info : kLevels) {
        INFO("level " << int{info.number});
        const auto header = parse_level_header(*decode_entry(*archive, info.header_entry));
        REQUIRE(header.has_value());

        Palette palette{};
        for (const PaletteRef& ref : header->palettes) {
            const auto palette_data = decode_entry(*archive, ref.entry);
            REQUIRE(palette_data.has_value());
            CHECK(load_palette(palette, *palette_data, ref.first_color).has_value());
        }

        const auto map = decode_tilemap(*decode_entry(*archive, header->map_entry), header->width, header->height);
        const auto tileset = decode_tileset(*decode_entry(*archive, header->tileset_entry));
        const auto mask = decode_entry(*archive, header->tileset_entry + 1U);
        const auto metatiles = decode_metatiles(*decode_entry(*archive, header->metatiles_entry));
        REQUIRE(map.has_value());
        REQUIRE(tileset.has_value());
        REQUIRE(metatiles.has_value());
        CHECK(map->stored_cells == map->cells.size());            // no level has a short map
        CHECK(mask->size() * 8 == tileset->size() * kTileBytes);  // 1 transparency bit per tile pixel

        const auto image = render_tilemap(*map, *metatiles, *tileset);
        REQUIRE(image.has_value());

        if (info.number == 1) {
            // Reference value from an independent Python implementation (docs/formats/graphics.md).
            CHECK(lvr::assets::fnv1a64(std::as_bytes(std::span{image->pixels})) == 0xb90894783fdf4107ULL);
        }
    }
}

TEST_CASE("real data: every level and scene header parses to its last byte", "[real-data]") {
    const Archive* const archive = &game_or_skip().archive();

    std::vector<std::uint16_t> headers;
    for (const LevelInfo& info : kLevels) {
        headers.push_back(info.header_entry);
    }
    for (const SceneInfo& info : kScenes) {
        headers.push_back(info.header_entry);
    }
    std::size_t animations = 0;
    for (const std::uint16_t entry : headers) {
        INFO("header " << entry);
        const auto bytes = decode_entry(*archive, entry);
        const auto header = parse_level_header(*bytes);
        REQUIRE(header.has_value());
        CHECK(header->end == bytes->size());
        animations += header->palette_animations.size();
        for (const PackedSetRef& set : header->packed_sets) {
            CHECK(set.fields[0] == 4);
            CHECK(set.fields[1] == 4);
            CHECK(set.fields[2] <= 1);
        }
    }
    CHECK(animations == 164);

    // Level 1 (from the survey in docs/formats/level.md)
    const auto level1 = parse_level_header(*decode_entry(*archive, kLevels[0].header_entry));
    REQUIRE(level1.has_value());
    CHECK(level1->palette_animations.size() == 6);
    REQUIRE(level1->sprite_sets.size() == 12);
    CHECK(level1->sprite_sets[0].entry == 303);
    CHECK(level1->sprite_sets[0].slot == 0xc0);
    CHECK(level1->sprite_sets[0].sheet == 0x410);
    REQUIRE(level1->packed_sets.size() == 10);
    CHECK(level1->packed_sets[0].entry == 221);
}

TEST_CASE("viking starts: three tables of three (x, y, flags)") {
    std::vector<std::byte> data;
    for (std::uint16_t i = 0; i < 27; ++i) {
        put_u16(data, data.size(), static_cast<std::uint16_t>(i * 3));
    }
    const auto tables = parse_viking_starts(data);
    REQUIRE(tables.has_value());
    CHECK((*tables)[0][0].x == 0);
    CHECK((*tables)[0][1].y == 12);
    CHECK((*tables)[2][2].flags == 78);
    data.pop_back();
    CHECK(parse_viking_starts(data).error() == Error::kBadSize);
}
