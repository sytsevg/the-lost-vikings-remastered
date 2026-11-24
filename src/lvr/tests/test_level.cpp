// Tests for src/lvr/level.*: the level model built from the decoders' data, with [real-data] checks of every level
// and cutscene.
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "assets/assets.hpp"
#include "assets/catalog.hpp"
#include "assets/graphics.hpp"
#include "assets/level.hpp"
#include "lvr/level.hpp"

using namespace lvr;

namespace {

// The user's own game files in gamedata/original/. [real-data] tests skip when the folder is missing.
const assets::GameFiles& game_or_skip() {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    static const auto files = assets::GameFiles::open(LVR_ORIGINAL_DIR);
    if (!files) {
        FAIL(files.error());
    }
    return *files;
}

// Three tables like VIKINGS.EXE's, with values that tell them apart.
std::array<assets::VikingStartTable, 3> sample_starts() {
    std::array<assets::VikingStartTable, 3> tables{};
    for (std::size_t t = 0; t < tables.size(); ++t) {
        for (std::size_t v = 0; v < 3; ++v) {
            tables.at(t).at(v) = assets::VikingStart{.x = static_cast<std::uint16_t>((100 * (t + 1)) + v),
                                                     .y = static_cast<std::uint16_t>(50 + v),
                                                     .flags = static_cast<std::uint16_t>(v == 2 ? 0x40 : 0)};
        }
    }
    return tables;
}

assets::Tilemap sample_tilemap() {
    // 3 x 2 cells: free, ground, a slope / a step, an unnamed type, a metatile past 511.
    assets::Tilemap tilemap{.width = 3, .height = 2, .cells = {}, .stored_cells = 6};
    const auto cell = [](std::uint16_t metatile, std::uint8_t type) {
        return assets::MapCell{.metatile = static_cast<std::uint16_t>(metatile & 0x1ffU),
                               .attributes = static_cast<std::uint8_t>((type << 1U) | (metatile >> 9U))};
    };
    tilemap.cells = {cell(5, 0x00), cell(6, 0x01), cell(7, 0x31), cell(8, 0x04), cell(9, 0x14), cell(0x205, 0x20)};
    return tilemap;
}

}  // namespace

TEST_CASE("tile types: what the engine's own code names") {
    CHECK(terrain(0x00) == Terrain::kFree);
    CHECK(terrain(0x03) == Terrain::kFree);
    CHECK(terrain(0x0c) == Terrain::kFree);
    for (const std::uint8_t type : std::array<std::uint8_t, 5>{0x01, 0x02, 0x04, 0x05, 0x20}) {
        CHECK(terrain(type) == Terrain::kGround);
    }
    CHECK(terrain(0x30) == Terrain::kSlope);
    CHECK(terrain(0x3f) == Terrain::kSlope);
    CHECK(terrain(0x14) == Terrain::kUnknown);
    CHECK(terrain(0x2f) == Terrain::kUnknown);

    CHECK(tile_type_name(0x00) == "free");
    CHECK(tile_type_name(0x01) == "ground");
    CHECK(tile_type_name(0x02) == "ground");
    CHECK(tile_type_name(0x03) == "ladder");
    CHECK(tile_type_name(0x04) == "ladder top");
    CHECK(tile_type_name(0x0c) == "free");
    CHECK(tile_type_name(0x12) == "lava");
    CHECK(tile_type_name(0x35) == "slope 5");
    CHECK(tile_type_name(0x0a) == "platform stop");
    CHECK(tile_type_name(0x0b) == "hidden platform");
    CHECK(tile_type_name(0x10) == "water");
    CHECK(tile_type_name(0x13) == "quicksand");
    CHECK(tile_type_name(0x14) == "spikes");
    CHECK(tile_type_name(0x15) == "electricity");
    CHECK(tile_type_name(0x16) == "spring");
    CHECK(tile_type_name(0x17) == "rotating spikes");
    CHECK(tile_type_name(0x0d).empty());
}

TEST_CASE("map: the tile type is split from the picture at bit 10") {
    const Map map = make_map(sample_tilemap());
    REQUIRE(map.width == 3);
    REQUIRE(map.height == 2);
    CHECK(map.pixel_width() == 48);
    CHECK(map.pixel_height() == 32);
    CHECK(map.at(0, 0) == Cell{.metatile = 5, .tile_type = 0x00});
    CHECK(map.at(2, 0) == Cell{.metatile = 7, .tile_type = 0x31});
    CHECK(map.at(1, 1) == Cell{.metatile = 9, .tile_type = 0x14});
    CHECK(map.at(2, 1) == Cell{.metatile = 0x205, .tile_type = 0x20});
    CHECK_THROWS(map.at(0, 2));
}

TEST_CASE("map: the tile type at a pixel; ground beside the map, free above and below") {
    const Map map = make_map(sample_tilemap());
    CHECK(map.tile_type_at(0, 0) == 0x00);
    CHECK(map.tile_type_at(15, 15) == 0x00);
    CHECK(map.tile_type_at(16, 15) == 0x01);
    CHECK(map.tile_type_at(47, 0) == 0x31);
    CHECK(map.tile_type_at(0, 16) == 0x04);
    CHECK(map.tile_type_at(47, 31) == 0x20);
    CHECK(map.tile_type_at(-1, 0) == tile::kGround);
    CHECK(map.tile_type_at(48, 0) == tile::kGround);
    CHECK(map.tile_type_at(0, -1) == tile::kFree);
    CHECK(map.tile_type_at(0, 32) == tile::kFree);
    CHECK(map.tile_type_at(-1, -1) == tile::kGround);  // the column is looked at first
    CHECK(Map{}.tile_type_at(0, 0) == tile::kGround);
}

TEST_CASE("start: the Vikings in a row behind the first") {
    assets::LevelHeader header;
    header.start_mode = assets::start_mode::kAtPosition;
    header.start_x = 112;
    header.start_y = 210;
    header.start_flags = 0x2f;
    const std::vector<Placement> start = make_start(header, sample_starts());
    REQUIRE(start.size() == 3);
    CHECK(start.at(0) == Placement{.x = 112, .y = 210, .kind = 1, .flags = 0x2f});
    CHECK(start.at(1) == Placement{.x = 80, .y = 210, .kind = 0, .flags = 0x2f});
    CHECK(start.at(2) == Placement{.x = 48, .y = 210, .kind = 2, .flags = 0x2f});
    CHECK_FALSE(start.at(0).facing_left());

    header.start_flags = 0x6f;  // facing left: the others stand to the right
    const std::vector<Placement> left = make_start(header, sample_starts());
    CHECK(left.at(1).x == 144);
    CHECK(left.at(2).x == 176);
    CHECK(left.at(2).facing_left());
}

TEST_CASE("start: dropped in from above the map, the second and third higher") {
    assets::LevelHeader header;
    header.start_mode = assets::start_mode::kDropIn;
    header.start_x = 80;
    header.start_y = 0xff50;  // -176
    const std::vector<Placement> start = make_start(header, sample_starts());
    REQUIRE(start.size() == 3);
    CHECK(start.at(0).y == -176);
    CHECK(start.at(1).y == -192);
    CHECK(start.at(2).y == -184);
    CHECK(start.at(2).x == 16);
}

TEST_CASE("start: the places from VIKINGS.EXE's tables, by mode") {
    assets::LevelHeader header;
    header.start_flags = 0x2f;
    const std::array<std::uint8_t, 3> modes{2, 4, 5};
    for (std::size_t t = 0; t < modes.size(); ++t) {
        header.start_mode = modes.at(t);
        const std::vector<Placement> start = make_start(header, sample_starts());
        REQUIRE(start.size() == 3);
        CHECK(start.at(0) == Placement{.x = static_cast<int>(100 * (t + 1)), .y = 50, .kind = 1, .flags = 0x2f});
        CHECK(start.at(2).x == static_cast<int>((100 * (t + 1)) + 2));
        CHECK(start.at(2).flags == 0x6f);  // the table's flags with the header's
        CHECK(start.at(1).kind == 0);
    }
}

TEST_CASE("start: a cutscene's one entity") {
    assets::LevelHeader header;
    header.start_mode = assets::start_mode::kScene;
    header.start_x = 80;
    header.start_y = 80;
    header.start_type = 0xda;
    header.start_flags = 0x1000;
    header.start_arg = 0x50;
    const std::vector<Placement> start = make_start(header, sample_starts());
    REQUIRE(start.size() == 1);
    CHECK(start.at(0) == Placement{.x = 80, .y = 80, .kind = 0xda, .flags = 0x1000, .argument = 0x50});
}

TEST_CASE("level: built from a decoded level") {
    assets::LevelData data;
    data.info = assets::LevelInfo{.number = 7, .world = "prehistoric", .header_entry = 44, .object_db_entry = 450};
    data.header.start_mode = assets::start_mode::kAtPosition;
    data.header.gravity = 0xf8;
    data.header.push = 0x100;
    data.header.switches = 0x20f;
    data.header.word_14 = 0x1615;
    data.header.flags = 0x01;
    data.header.tune = 3;
    data.header.music_at_end = 2;
    data.header.next_slot = 7;
    data.header.palette_animation_flags = 0x3e;
    data.header.palette_animations.push_back(
        assets::PaletteAnimation{.delay = 4, .from_color = 16, .to_color = 20, .values = {}});
    data.header.objects = {
        assets::ObjectPlacement{.x = 168, .y = 248, .width = 48, .height = 48, .type = 79, .flags = 0x22, .arg = 0},
        assets::ObjectPlacement{.x = 0, .y = 0, .width = 32, .height = 32, .type = 72, .flags = 0x828, .arg = 3},
        assets::ObjectPlacement{.x = 9, .y = 9, .width = 16, .height = 16, .type = 5, .flags = 0x100, .arg = 0},
    };
    data.palette.at(1) = assets::Rgb{1, 2, 3};
    data.map.map = sample_tilemap();
    data.map.tileset.resize(4);
    data.map.metatiles.resize(2);

    const Level level = make_level(data, sample_starts());
    CHECK(level.name == "level-07");
    CHECK(level.number == 7);
    CHECK(level.world == "prehistoric");
    CHECK(level.map.width == 3);
    CHECK(level.start.size() == 3);
    REQUIRE(level.placements.size() == 3);
    CHECK(level.placements.at(0) ==
          Placement{.x = 168, .y = 248, .width = 48, .height = 48, .kind = 79, .flags = 0x22, .argument = 0});
    CHECK_FALSE(level.placements.at(0).always_active());
    CHECK(level.placements.at(1).always_active());
    CHECK(level.placements.at(1).argument == 3);
    CHECK(level.placements.at(2).created_again_at_once());
    CHECK(level.switches == 0x20f);
    CHECK(level.extra == 0x1615);
    CHECK(level.gravity == 0xf8);
    CHECK(level.push == 0x100);
    CHECK(level.hud);
    CHECK(level.music.tune == 3);
    CHECK(level.music.at_end == 2);
    CHECK(level.next_slot == 7);
    CHECK(level.palette_animations_running == 0x3e);
    CHECK(level.palette_animations.size() == 1);
    CHECK(level.palette.at(1) == assets::Rgb{1, 2, 3});
    CHECK(level.tileset.size() == 4);
    CHECK(level.metatiles.size() == 2);
    CHECK_FALSE(level.picture);
}

TEST_CASE("cutscene: with a map, or as one picture") {
    assets::SceneData data;
    data.info = assets::SceneInfo{.name = "galaxy", .header_entry = 402, .description = "galaxy"};
    data.header.start_mode = assets::start_mode::kScene;
    data.header.start_type = 0xda;
    data.map = assets::TiledMap{};
    data.map->map = sample_tilemap();
    const Level galaxy = make_cutscene(data, sample_starts());
    CHECK(galaxy.name == "galaxy");
    CHECK(galaxy.number == 0);
    CHECK(galaxy.world.empty());
    CHECK(galaxy.map.height == 2);
    CHECK(galaxy.start.size() == 1);
    CHECK_FALSE(galaxy.hud);
    CHECK_FALSE(galaxy.picture);

    data.map.reset();
    data.image = assets::make_image(320, 240);
    const Level starfield = make_cutscene(data, sample_starts());
    CHECK(starfield.map.cells.empty());
    REQUIRE(starfield.picture);
    CHECK(starfield.picture->width == 320);
}

TEST_CASE("real data: every level builds; its tile types and placements", "[real-data]") {
    const assets::GameFiles& files = game_or_skip();
    const auto starts = assets::load_viking_starts(files.exe(), files.release());
    REQUIRE(starts);
    std::set<std::uint8_t> types;
    std::size_t placements = 0;
    std::size_t always_active = 0;
    for (const assets::LevelInfo& info : assets::kLevels) {
        const auto data = assets::load_level(files.archive(), info);
        REQUIRE(data);
        const Level level = make_level(*data, *starts);
        CAPTURE(level.name);
        CHECK(level.map.cells.size() == static_cast<std::size_t>(level.map.width * level.map.height));
        CHECK(level.start.size() == 3);
        CHECK(level.hud);
        CHECK(level.gravity == 0xf8);
        CHECK(level.push == 0x100);
        for (const Cell& cell : level.map.cells) {
            types.insert(cell.tile_type);
            CHECK(cell.metatile < level.metatiles.size());
        }
        for (const Placement& start : level.start) {  // the Vikings start inside the map across, or above it
            CHECK(start.x > 0);
            CHECK(start.x < level.map.pixel_width());
            CHECK(start.y < level.map.pixel_height());
        }
        placements += level.placements.size();
        always_active += static_cast<std::size_t>(std::ranges::count_if(level.placements, &Placement::always_active));
    }
    // The 24 tile types the levels use.
    CHECK(types == std::set<std::uint8_t>{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x0a, 0x0b, 0x0c, 0x0d, 0x10, 0x12,
                                          0x13, 0x14, 0x15, 0x16, 0x17, 0x20, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35});
    CHECK(placements > 1300);
    CHECK(always_active > 40);
}

TEST_CASE("real data: where the Vikings start", "[real-data]") {
    const assets::GameFiles& files = game_or_skip();
    const auto starts = assets::load_viking_starts(files.exe(), files.release());
    REQUIRE(starts);
    const auto level = [&](std::size_t number) {
        const auto data = assets::load_level(files.archive(), assets::kLevels.at(number - 1));
        REQUIRE(data);
        return make_level(*data, *starts);
    };
    // Level 1 is the one level that places them from the table in VIKINGS.EXE.
    const Level first = level(1);
    CHECK(first.start.at(0).kind == 1);
    CHECK(first.start.at(0).x == starts->at(0).at(0).x);
    CHECK(first.start.at(2).y == starts->at(0).at(2).y);
    CHECK(first.next_slot == 1);
    // Level 2: in a row at the header's place; its switches start as 0x20f.
    const Level second = level(2);
    CHECK(second.start.at(0).x == 112);
    CHECK(second.start.at(1).x == 80);
    CHECK(second.start.at(2).x == 48);
    CHECK(second.start.at(0).y == 210);
    CHECK(second.switches == 0x20f);
    // Level 5, the first of its world: dropped in from 176 pixels above the map.
    const Level fifth = level(5);
    CHECK(fifth.start.at(0).y == -176);
    CHECK(fifth.start.at(1).y == -192);
    // Level 13 starts facing left, the others to the right of the first.
    const Level thirteenth = level(13);
    CHECK(thirteenth.start.at(0).facing_left());
    CHECK(thirteenth.start.at(1).x == thirteenth.start.at(0).x + 32);
}

TEST_CASE("real data: every cutscene builds", "[real-data]") {
    const assets::GameFiles& files = game_or_skip();
    const auto starts = assets::load_viking_starts(files.exe(), files.release());
    REQUIRE(starts);
    std::size_t found = 0;
    for (const assets::SceneInfo& info : assets::kScenes) {
        if (std::ranges::find(kCutscenes, info.name) == kCutscenes.end()) {
            continue;
        }
        ++found;
        const auto data = assets::load_scene(files.archive(), info);
        REQUIRE(data);
        const Level cutscene = make_cutscene(*data, *starts);
        CAPTURE(cutscene.name);
        CHECK(cutscene.number == 0);
        CHECK(cutscene.map.cells.empty() == cutscene.picture.has_value());
        // The village's intro and ending are played with the Vikings; the others start with one entity.
        const bool vikings = cutscene.name == "home-intro" || cutscene.name == "home-ending";
        CHECK(cutscene.start.size() == (vikings ? 3U : 1U));
        CHECK(cutscene.hud == (cutscene.name.starts_with("home-")));
    }
    CHECK(found == kCutscenes.size());
}
