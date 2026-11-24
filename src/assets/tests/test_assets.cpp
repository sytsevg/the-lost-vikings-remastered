// Tests for src/assets/assets.*: building levels, scenes and images from DATA.DAT. Most run on a small hand-made
// DATA.DAT; the [real-data] ones load everything from the user's own game files.
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "assets/assets.hpp"
#include "assets/catalog.hpp"
#include "assets/data_dat.hpp"
#include "assets/hash.hpp"

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

void put_u16(std::vector<std::byte>& out, std::uint16_t value) {
    out.push_back(static_cast<std::byte>(value & 0xffU));
    out.push_back(static_cast<std::byte>(value >> 8U));
}

// LZSS with literals only: the size header, then groups of a 0xff flag byte and up to 8 literal bytes.
std::vector<std::byte> lzss_literals(std::span<const std::byte> data) {
    std::vector<std::byte> out;
    put_u16(out, static_cast<std::uint16_t>(data.size() - 1));
    for (std::size_t i = 0; i < data.size(); i += 8) {
        out.push_back(std::byte{0xff});
        out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(i),
                   data.begin() + static_cast<std::ptrdiff_t>(std::min(i + 8, data.size())));
    }
    return out;
}

// A DATA.DAT holding `entries`: an offset table, then each entry, LZSS-compressed unless it is a stored one.
std::vector<std::byte> make_data_dat(const std::vector<std::vector<std::byte>>& entries) {
    std::vector<std::vector<std::byte>> packed;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        packed.push_back(is_stored_entry(i) ? entries[i] : lzss_literals(entries[i]));
    }
    std::vector<std::byte> out;
    std::uint32_t offset = static_cast<std::uint32_t>(entries.size() * 4);
    for (const auto& entry : packed) {
        for (const unsigned shift : {0U, 8U, 16U, 24U}) {
            out.push_back(static_cast<std::byte>((offset >> shift) & 0xffU));
        }
        offset += static_cast<std::uint32_t>(entry.size());
    }
    for (const auto& entry : packed) {
        out.insert(out.end(), entry.begin(), entry.end());
    }
    return out;
}

struct HeaderSpec {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t map = 0;
    std::uint16_t tileset = kNoEntry;
    std::uint16_t metatiles = kNoEntry;
    std::uint16_t palette = 0;            // loaded at colour 0
    std::uint16_t sprite_set = kNoEntry;  // one sprite set, or none
};

// A level header (docs/formats/level.md) with no objects and no palette animations.
std::vector<std::byte> level_header(const HeaderSpec& spec) {
    std::vector<std::byte> out(0x29);
    for (const std::uint16_t value : {spec.width, spec.height}) {
        put_u16(out, value);
    }
    out.push_back(std::byte{1});  // 0x2d
    for (const std::uint16_t value : {spec.map, spec.tileset, spec.metatiles}) {
        put_u16(out, value);
    }
    out.resize(0x43);
    put_u16(out, 0xffff);  // no objects
    put_u16(out, spec.palette);
    out.push_back(std::byte{0});
    put_u16(out, 0xffff);         // end of the palettes
    put_u16(out, 0);              // palette animation flags
    out.push_back(std::byte{0});  // no palette animations
    if (spec.sprite_set != kNoEntry) {
        for (const std::uint16_t value : {spec.sprite_set, std::uint16_t{0}, std::uint16_t{0x0101}}) {
            put_u16(out, value);
        }
    }
    put_u16(out, 0xffff);  // end of the sprite sets
    put_u16(out, 0xffff);  // no packed sets
    return out;
}

// Table rows for the hand-made DATA.DAT (every field given: designated initializers may not skip fields).
LevelInfo level_row(std::uint8_t number, std::uint16_t header_entry) {
    return LevelInfo{.number = number, .world = "test", .header_entry = header_entry, .object_db_entry = kNoEntry};
}

SceneInfo scene_row(std::string_view name, std::uint16_t header_entry, std::uint16_t hud_entry = kNoEntry) {
    return SceneInfo{.name = name, .header_entry = header_entry, .hud_entry = hud_entry, .description = "test"};
}

ImageInfo image_row(std::string_view name, std::uint16_t image_entry, std::uint16_t palette_entry) {
    return ImageInfo{.name = name, .image_entry = image_entry, .palette_entry = palette_entry, .description = "test"};
}

constexpr std::uint8_t kTileColour = 5;
constexpr std::uint8_t kCellAttributes = 3;

// Entries: 0 config, 1 a 344x1 screen image (stored), 2 the font (catalogued sprites, all transparent), 3 a
// 16-colour palette, 4 a tileset of two tiles, 5 its mask, 6 one metatile, 7 a level header, 8 its 2x1 map,
// 9 an image-scene header, 10 a header with a sprite set the catalogue doesn't know.
std::vector<std::byte> small_game() {
    std::vector<std::vector<std::byte>> entries(11);
    entries[0] = {std::byte{'C'}, std::byte{'F'}, std::byte{'G'}};
    entries[1] = {std::byte{86}, std::byte{0}};  // plane size: 4 x 86 = 344 pixels, one row
    entries[1].resize(2 + 344, std::byte{7});
    entries[2].resize(80 * 72);  // the font: 80 unpacked 8x8 frames, every mask 0
    for (std::size_t i = 0; i < 48; ++i) {
        entries[3].push_back(static_cast<std::byte>(i));
    }
    entries[4].resize(2 * 64);
    std::fill(entries[4].begin() + 64, entries[4].end(), std::byte{kTileColour});  // tile 1: one colour
    entries[5].resize(2 * 8);
    for (int quarter = 0; quarter < 4; ++quarter) {
        put_u16(entries[6], 1U << 6U);  // tile 1, no flags
    }
    entries[7] =
        level_header({.width = 2, .height = 1, .map = 8, .tileset = 4, .metatiles = 6, .palette = 3, .sprite_set = 2});
    put_u16(entries[8], kCellAttributes << 9U);  // metatile 0 with attributes
    put_u16(entries[8], 0);
    entries[9] = level_header({.map = 1, .palette = 3});
    entries[10] =
        level_header({.width = 2, .height = 1, .map = 8, .tileset = 4, .metatiles = 6, .palette = 3, .sprite_set = 3});
    return make_data_dat(entries);
}

}  // namespace

// --- The original files ---

TEST_CASE("game files: both files are needed, found in any letter case, and must be a known release") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "lvr_test_game_files";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto missing = GameFiles::open(dir);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().find("VIKINGS.EXE and DATA.DAT must both be in") != std::string::npos);

    std::ofstream{dir / "vikings.exe", std::ios::binary} << "not the game";
    const auto data = small_game();
    std::ofstream{dir / "Data.Dat", std::ios::binary}.write(reinterpret_cast<const char*>(data.data()),
                                                            static_cast<std::streamsize>(data.size()));
    const auto unknown = GameFiles::open(dir);
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().starts_with("unknown release: VIKINGS.EXE fnv1a64="));
    std::filesystem::remove_all(dir);

    const std::string_view not_the_game = "not the game";
    const auto broken = GameFiles::from_bytes(std::as_bytes(std::span{not_the_game.data(), not_the_game.size()}),
                                              {std::byte{4}, std::byte{0}, std::byte{0}});
    REQUIRE_FALSE(broken.has_value());
    CHECK(broken.error().find("content fnv1a64=input is truncated") != std::string::npos);
}

// --- Building blocks, on a hand-made DATA.DAT ---

TEST_CASE("level: header, palette, map and sprite sets come together") {
    const auto data = small_game();
    const auto archive = Archive::parse(data);
    REQUIRE(archive.has_value());
    const auto level = lvr::assets::load_level(*archive, level_row(1, 7));
    REQUIRE(level.has_value());
    CHECK(level->info.header_entry == 7);
    CHECK(level->palette[1] == Rgb{.r = 12, .g = 16, .b = 20});  // VGA 3, 4, 5 expanded to 8 bits
    CHECK(level->map.tileset.size() == 2);
    CHECK(level->map.map.at(0, 0).attributes == kCellAttributes);
    REQUIRE(level->map.image.width == 32);
    CHECK(level->map.image.height == 16);
    CHECK(std::ranges::all_of(level->map.image.pixels, [](std::uint8_t p) { return p == kTileColour; }));
    REQUIRE(level->sprite_sets.size() == 1);
    CHECK(level->sprite_sets[0].info.name == "font");
    CHECK(level->sprite_sets[0].frames.size() == 80);
    CHECK(level->packed_sets.empty());
}

TEST_CASE("scenes: tiled ones render their map, image ones stack the HUD-area image below") {
    const auto data = small_game();
    const auto archive = Archive::parse(data);
    REQUIRE(archive.has_value());

    const auto tiled = lvr::assets::load_scene(*archive, scene_row("tiled", 7));
    REQUIRE(tiled.has_value());
    CHECK(tiled->map.has_value());
    CHECK(tiled->image.width == 32);

    const auto image = lvr::assets::load_scene(*archive, scene_row("image", 9, 1));
    REQUIRE(image.has_value());
    CHECK_FALSE(image->map.has_value());
    CHECK(image->image.width == kScreenWidth);
    CHECK(image->image.height == 2);  // the 1-line image twice
    CHECK(image->image.at(0, 1) == 7);

    const auto alone = lvr::assets::load_scene(*archive, scene_row("image", 9));
    REQUIRE(alone.has_value());
    CHECK(alone->image.height == 1);
}

TEST_CASE("images: a screen image with its palette at colour 0") {
    const auto data = small_game();
    const auto archive = Archive::parse(data);
    REQUIRE(archive.has_value());
    const auto image = lvr::assets::load_image(*archive, image_row("panel", 1, 3));
    REQUIRE(image.has_value());
    CHECK(image->image.width == kScreenWidth);
    CHECK(image->palette[0] == Rgb{.r = 0, .g = 4, .b = 8});
}

TEST_CASE("errors name the level, scene, image or entry that failed") {
    const auto data = small_game();
    const auto archive = Archive::parse(data);
    REQUIRE(archive.has_value());
    const auto uncatalogued = lvr::assets::load_level(*archive, level_row(9, 10));
    REQUIRE_FALSE(uncatalogued.has_value());
    CHECK(uncatalogued.error() == "level 9: sprite entry 3 is not in the sprite catalogue");
    CHECK(lvr::assets::load_level(*archive, level_row(9, 99)).error() == "level 9: entry 99: entry index out of range");
    CHECK(lvr::assets::load_scene(*archive, scene_row("x", 99)).error() ==
          "scene x: entry 99: entry index out of range");
    CHECK(lvr::assets::load_scene(*archive, scene_row("x", 9, 3)).error() ==
          "scene x: image 3: data size does not fit the structure");
    CHECK(lvr::assets::load_image(*archive, image_row("x", 1, 1)).error() ==
          "image x: palette 1: data size does not fit the structure");

    LevelHeader header;
    header.width = 2;
    header.height = 1;
    header.map_entry = 99;
    header.tileset_entry = 4;
    header.metatiles_entry = 6;
    CHECK(lvr::assets::load_tiled_map(*archive, header).error() == "entry 99: entry index out of range");
    header.map_entry = 8;
    header.tileset_entry = 0;  // "CFG" is not a whole tile
    CHECK(lvr::assets::load_tiled_map(*archive, header).error() == "tileset 0: data size does not fit the structure");
    CHECK(lvr::assets::load_sprites(*archive, 99).error() == "sprite entry 99 is not in the sprite catalogue");
}

// --- Real data ---

TEST_CASE("real data: the game files open as the reference release", "[real-data]") {
    const GameFiles& files = game_or_skip();
    CHECK(files.release().name == "vanilla-dos");
    CHECK(files.archive().entry_count() == 535);
}

TEST_CASE("real data: every level, scene and image loads", "[real-data]") {
    const Archive& archive = game_or_skip().archive();

    for (const LevelInfo& info : kLevels) {
        INFO("level " << int{info.number});
        const auto level = lvr::assets::load_level(archive, info);
        REQUIRE(level.has_value());
        CHECK(level->map.image.width == level->header.width * kMetatileSize);
        if (info.number == 1) {
            // The same reference value as the level-1 render test (docs/formats/graphics.md).
            CHECK(lvr::assets::fnv1a64(std::as_bytes(std::span{level->map.image.pixels})) == 0xb90894783fdf4107ULL);
            CHECK(level->sprite_sets.size() == 12);
            CHECK(level->packed_sets.size() == 10);
        }
    }
    for (const SceneInfo& info : kScenes) {
        INFO("scene " << info.name);
        const auto scene = lvr::assets::load_scene(archive, info);
        REQUIRE(scene.has_value());
        CHECK(scene->map.has_value() == scene->header.is_tiled());
        if (!scene->header.is_tiled()) {
            CHECK(scene->image.height == 240);  // 176 + 64: a full screen
        }
    }
    for (const ImageInfo& info : kImages) {
        INFO("image " << info.name);
        const auto image = lvr::assets::load_image(archive, info);
        REQUIRE(image.has_value());
        if (info.image_entry == 1) {
            CHECK(lvr::assets::fnv1a64(std::as_bytes(std::span{image->image.pixels})) == 0x7e693ab0a32542ceULL);
        }
    }
}
