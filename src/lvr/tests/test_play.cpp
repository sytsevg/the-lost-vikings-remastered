// Tests for src/lvr/play.*: a level loaded from the game's files, its world at the start, and its view.
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <vector>

#include "assets/assets.hpp"
#include "lvr/kinds.hpp"
#include "lvr/play.hpp"
#include "lvr/render.hpp"
#include "lvr/sprites.hpp"
#include "lvr/world.hpp"

using namespace lvr;

TEST_CASE("real data: level 1 loaded, its world at the start and what it shows", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    const auto files = assets::GameFiles::open(LVR_ORIGINAL_DIR);
    REQUIRE(files);
    CHECK_FALSE(load_level(*files, 0));
    CHECK_FALSE(load_level(*files, 38));
    const auto loaded = load_level(*files, 1);
    REQUIRE(loaded);
    CHECK(loaded->level.name == "level-01");
    CHECK(loaded->graphics.tile_masks.size() == loaded->graphics.tileset.size() * 8);
    CHECK(loaded->sprite_sets.size() > 3);
    CHECK(loaded->sprite_sets[0].base == 1);
    CHECK(loaded->sprite_sets[1].base > 1);
    CHECK(loaded->sprites.packed_sets.size() == loaded->packed_entries.size());
    CHECK(loaded->level.palette[16] == assets::Rgb{});
    CHECK(loaded->slope_heights != std::array<std::uint8_t, 256>{});

    World world = start_world(*loaded);
    // The Vikings in the first three slots, three points of health each; Erik is played.
    REQUIRE(world.entities[0]);
    REQUIRE(world.entities[2]);
    CHECK(world.entities[0]->kind == 1);
    CHECK(world.entities[1]->kind == 0);
    CHECK(world.entities[2]->kind == 2);
    CHECK(world.entities[0]->health == 6);
    CHECK(world.current_viking == 0);
    CHECK(world.has_vikings);
    // The camera on Erik, at the bottom left of the level.
    CHECK(world.camera.x == 0);
    CHECK(world.camera.y == 304);
    // The level's always-active entity, and the placements in view at the start.
    REQUIRE(world.entities[3]);
    CHECK(world.entities[3]->kind == 0x48);
    CHECK(world.entities[3]->placement == 0);
    REQUIRE(world.entities[6]);
    CHECK(world.entities[6]->kind == 0x1d);
    REQUIRE(world.entities[7]);
    CHECK(world.entities[7]->kind == 0x03);  // Olaf's shield, which he creates
    CHECK_FALSE(world.entities[8]);
    CHECK(world.gravity == 0xf8);

    PiecePainter painter;
    std::vector<PiecePicture> pictures;
    const View view = view_of(world, *loaded, painter, pictures);
    CHECK(view.camera_x == 0);
    CHECK(view.camera_y == 304);
    CHECK(view.map == &world.map);
    REQUIRE(view.hud);
    CHECK(view.hud->vikings[0].portrait == HudView::Portrait::kChosen);
    CHECK(view.hud->vikings[1].portrait == HudView::Portrait::kWaiting);
    CHECK(view.hud->vikings[0].health == 3);
    CHECK(view.hud->picker[0]);
    CHECK(view.hud->picker[4]);
    CHECK(view.hud->icons[11] == 0);
    CHECK(view.sprites.size() == pictures.size());

    // The shake moves the view, not past the camera's limit; a dead Viking's portrait.
    world.shake_y = {.amount = 2, .offset = 2, .ticks = 3};
    world.shake_x = {.amount = 2, .offset = 2, .ticks = 3};
    world.entities[1]->dead = true;
    const View shaken = view_of(world, *loaded, painter, pictures);
    CHECK(shaken.camera_x == 2);
    CHECK(shaken.camera_y == 302);  // 306 would pass the limit
    CHECK(shaken.text_x == -2);
    CHECK(shaken.hud->vikings[1].portrait == HudView::Portrait::kDead);

    // A few ticks with the right key held: Erik walks.
    for (int tick = 0; tick < 5; ++tick) {
        for (int part = 0; part < 3; ++part) {
            world.step(part, 0x0100);
        }
    }
    CHECK(world.entities[0]->x > 32);
}

// A report, not a check (run it by its tag: lvr_tests "[unwritten]"): the kinds each scene places whose behaviour
// isn't written yet.
TEST_CASE("real data: the kinds each scene places that aren't written yet", "[.][unwritten][real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    const auto files = assets::GameFiles::open(LVR_ORIGINAL_DIR);
    REQUIRE(files);
    std::string report;
    for (int slot = 0; slot < kSceneSlots; ++slot) {
        const auto loaded = load_slot(*files, slot, database::kSpaceship);
        if (!loaded) {
            continue;
        }
        std::vector<std::uint16_t> unwritten;
        const auto note = [&](std::uint16_t kind) {
            if (make_kind(kind, loaded->database_entry) == nullptr &&
                std::ranges::find(unwritten, kind) == unwritten.end()) {
                unwritten.push_back(kind);
            }
        };
        for (const Placement& placement : loaded->level.start) {
            note(placement.kind);
        }
        for (const Placement& placement : loaded->level.placements) {
            note(placement.kind);
        }
        std::ranges::sort(unwritten);
        if (!unwritten.empty()) {
            report += std::format("\nscene {:2} (database {}):", slot, loaded->database_entry);
            for (const std::uint16_t kind : unwritten) {
                report += std::format(" {:02X}", kind);
            }
        }
    }
    WARN(report);
    SUCCEED();
}

TEST_CASE("real data: the pause menu holds the game, and giving up is the game over", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    const auto files = assets::GameFiles::open(LVR_ORIGINAL_DIR);
    REQUIRE(files);
    auto loaded = load_level(*files, 2);
    REQUIRE(loaded);
    Play play{*files, std::make_unique<LoadedLevel>(std::move(*loaded)), 1};
    int pictures = 0;
    const auto step = [&](std::uint16_t keys) { return play.step(keys, [&] { ++pictures; }); };
    constexpr std::uint16_t kFire = 0x8000;
    constexpr std::uint16_t kPause = 0x1000;
    for (int i = 0; i < 4; ++i) {  // the password's box, answered
        (void)step(i == 2 ? kFire : std::uint16_t{0});
    }
    REQUIRE_FALSE(play.world.waiting_for_key);
    const int erik_x = play.world.entities[0]->x;

    // Esc: the menu is set up without a refresh; the picture a little darker, a box on the text layer.
    CHECK(step(kPause) == 0);
    CHECK(play.world.tint == std::array<std::uint8_t, 3>{8, 8, 8});
    CHECK(std::ranges::any_of(play.world.text.cells, [](auto cell) { return cell != 0; }));
    // A pass is three refreshes, and nothing moves, whatever is held.
    CHECK(step(0x0100) == 3);
    CHECK(play.world.entities[0]->x == erik_x);
    // Fire on "no": two more refreshes, the menu gone, and the tick it held runs.
    (void)step(0);
    CHECK(step(kFire) == 3 + 2 + 3);
    CHECK(play.world.tint == std::array<std::uint8_t, 3>{});
    CHECK(std::ranges::none_of(play.world.text.cells, [](auto cell) { return cell != 0; }));
    CHECK_FALSE(play.world.game_over);

    // The quit keys after a tick: the box; the N key closes it and the game goes on.
    play.menu_keys.quit = true;
    CHECK(step(0) == 3);
    play.menu_keys.quit = false;
    CHECK(std::ranges::any_of(play.world.text.cells, [](auto cell) { return cell != 0; }));
    play.menu_keys.no = true;
    CHECK(step(0) == 5);
    play.menu_keys.no = false;
    CHECK_FALSE(play.ended);
    CHECK(play.world.tint == std::array<std::uint8_t, 3>{});

    // Again, and "yes" by its key: the game ends.
    play.menu_keys.quit = true;
    (void)step(0);
    play.menu_keys.quit = false;
    play.menu_keys.yes = true;
    (void)step(0);
    play.menu_keys.yes = false;
    CHECK(play.ended);
    CHECK(play.error.empty());
}

TEST_CASE("real data: giving up in the pause menu leads to the game-over scene with the Vikings alive", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    const auto files = assets::GameFiles::open(LVR_ORIGINAL_DIR);
    REQUIRE(files);
    auto loaded = load_level(*files, 2);
    REQUIRE(loaded);
    Play play{*files, std::make_unique<LoadedLevel>(std::move(*loaded)), 1};
    const auto step = [&](std::uint16_t keys) { return play.step(keys, [] {}); };
    for (int i = 0; i < 4; ++i) {
        (void)step(i == 2 ? std::uint16_t{0x8000} : std::uint16_t{0});
    }
    (void)step(0x1000);
    (void)step(0x0200);  // left: "yes"
    (void)step(0);
    CHECK(step(0x8000) > 145);  // the choice taken, and the change of scene
    CHECK(play.world.scene == scene::kGameOver);
    CHECK(
        std::ranges::none_of(play.world.game_over_scene.kept_health, [](std::uint16_t health) { return health == 0; }));
}

TEST_CASE("real data: the game-over scene knows its slot; under Remaster rules its letters are drawn in front",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    const auto files = assets::GameFiles::open(LVR_ORIGINAL_DIR);
    REQUIRE(files);
    // It keeps the database of the level lost: each level's.
    for (int level = 1; level <= scene::kLastLevel + 1; ++level) {
        const auto lost = load_level(*files, level);
        REQUIRE(lost);
        const auto loaded = load_slot(*files, scene::kGameOver, lost->database_entry);
        REQUIRE(loaded);
        CHECK(loaded->slot == scene::kGameOver);
        const World vanilla = start_world(*loaded);
        CHECK(vanilla.scene == scene::kGameOver);
        CHECK(vanilla.kinds.at(game_over_kind::kLetters).pool == Pool::kMiddle);
        const World remaster = start_world(*loaded, Rules::kRemaster);
        CHECK(remaster.scene == scene::kGameOver);
        CHECK(remaster.kinds.at(game_over_kind::kLetters).pool == Pool::kFront);
    }
}

TEST_CASE("real data: a scene that is one picture takes the camera's limits from its own header", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    const auto files = assets::GameFiles::open(LVR_ORIGINAL_DIR);
    REQUIRE(files);
    // The title and the starfield have no map; their pictures are shown from where the camera starts.
    const auto title = load_slot(*files, scene::kTitle, database::kSpaceship);
    REQUIRE(title);
    CHECK(title->graphics.picture.has_value());
    CHECK(title->level.map.cells.empty());
    const World at_title = start_world(*title);
    CHECK(at_title.camera.x == 0);
    CHECK(at_title.camera.y == 32);
    CHECK(at_title.scene == scene::kTitle);

    const auto starfield = load_slot(*files, scene::kStarfield, database::kSpaceship);
    REQUIRE(starfield);
    const World in_the_stars = start_world(*starfield);
    CHECK(in_the_stars.camera.y == 24);
    CHECK(in_the_stars.text.direct);  // its text is drawn straight onto the picture
    // The ship talks by itself: after its first wait a box is up, without a key.
    CHECK(in_the_stars.entities[0]->kind == 0xe7);
}

TEST_CASE("the text layer of a scene that draws its text directly shows only what was there four draws after a clear") {
    TextLayer layer;
    layer.direct = true;
    const std::array<std::uint8_t, 8> texts{};  // no texts: single characters will do
    layer.show({.kind = Line::Kind::kClear}, texts);
    layer.show({.kind = Line::Kind::kCharacter, .column = 3, .row = 2, .value = 'A'}, texts);
    CHECK(layer.seen().at((2 * kTextLayerColumns) + 3) == 'A');
    layer.draw();
    layer.draw();
    layer.draw();  // the fourth draw since the clear
    layer.show({.kind = Line::Kind::kCharacter, .column = 4, .row = 2, .value = 'B'}, texts);
    layer.draw();
    CHECK(layer.cells.at((2 * kTextLayerColumns) + 4) == 'B');
    CHECK(layer.seen().at((2 * kTextLayerColumns) + 4) == 0);  // too late: not seen
    layer.show({.kind = Line::Kind::kClear}, texts);
    layer.show({.kind = Line::Kind::kCharacter, .column = 4, .row = 2, .value = 'B'}, texts);
    CHECK(layer.seen().at((2 * kTextLayerColumns) + 4) == 'B');
    // An ordinary scene shows its cells as they are.
    TextLayer plain;
    plain.show({.kind = Line::Kind::kCharacter, .column = 1, .row = 1, .value = 'C'}, texts);
    CHECK(plain.seen().at(kTextLayerColumns + 1) == 'C');
}

// The spawn sweep: in every scene, each placement created by itself in a fresh start and left to run a while.
// What its animations ask for that the scene doesn't have is reported: a broken program (one of another world's
// that the scene's animation places don't pair), a packed set the level doesn't load (the original then shows the
// level's first set). A broken program fails the test; the sets are a report.
TEST_CASE("real data: the spawn sweep: every placement of every scene created and run", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    const auto files = assets::GameFiles::open(LVR_ORIGINAL_DIR);
    REQUIRE(files);
    constexpr int kTicks = 40;
    std::string broken;
    std::string unloaded;
    int created = 0;
    for (int slot = 0; slot < kSceneSlots; ++slot) {
        const auto loaded = load_slot(*files, slot, database::kSpaceship);
        if (!loaded) {
            continue;
        }
        std::vector<World::Missing> found;
        for (int index = 0; index < static_cast<int>(loaded->level.placements.size()); ++index) {
            World world = start_world(*loaded);
            const bool there = std::ranges::any_of(
                world.entities, [index](const auto& entity) { return entity && entity->placement == index; });
            if (!there && world.create_placed(index) == nullptr) {
                continue;
            }
            ++created;
            for (int tick = 0; tick < kTicks; ++tick) {
                for (int part = 0; part < 3; ++part) {
                    world.step(part);
                }
            }
            for (const World::Missing& what : world.missing) {
                if (std::ranges::find(found, what) == found.end()) {
                    found.push_back(what);
                }
            }
        }
        for (const World::Missing& what : found) {
            (what.program ? broken : unloaded) +=
                std::format("\nscene {:2} (database {}): kind {:02X} {} {:04X}", slot, loaded->database_entry,
                            what.kind, what.program ? "program" : "packed set", what.what);
        }
    }
    CHECK(created > 1000);
    if (!unloaded.empty()) {
        WARN("packed sets the scene doesn't load:" + unloaded);
    }
    INFO("broken programs:" << broken);
    CHECK(broken.empty());
}

TEST_CASE("real data: a world written out reads back the same; its size is this version of the state's",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    const auto files = assets::GameFiles::open(LVR_ORIGINAL_DIR);
    REQUIRE(files);
    const auto written = [](const World& world) {
        std::vector<std::uint8_t> bytes;
        Writer to{bytes};
        world.save(to);
        return bytes;
    };
    std::vector<std::size_t> sizes;
    for (const int slot : {0, 28}) {
        const auto loaded = load_slot(*files, slot, database::kSpaceship);
        REQUIRE(loaded);
        for (const Rules rules : {Rules::kVanilla, Rules::kRemaster}) {
            const World world = start_world(*loaded, rules);
            const std::vector<std::uint8_t> bytes = written(world);
            sizes.push_back(bytes.size());
            // Into a world made anew for the level: all of it is read, and written again it is the same.
            World read = prepare_world(*loaded, nullptr, rules);
            Reader from{bytes};
            REQUIRE(read.load(from));
            CHECK(from.done());
            CHECK(written(read) == bytes);
            CHECK(read.slots() == world.slots());
            CHECK(read.at(0) != nullptr);
            // Cut short, or of a world of the other rules' size for a kind: not a world.
            World cut = prepare_world(*loaded, nullptr, rules);
            Reader from_less{std::span{bytes}.first(bytes.size() - 5)};
            CHECK_FALSE(cut.load(from_less));
        }
    }
    // What is written changes when a kind's Saved, EntityState or WorldState does. When these sizes change, an
    // older quicksave no longer fits: raise kStateVersion (archive.hpp), and put the new sizes and version here.
    CHECK(kStateVersion == 1);
    CHECK(sizes == std::vector<std::size_t>{24689, 71261, 41673, 88245});
}
