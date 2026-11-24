// Tests for src/lvr/minimap.*: what a view has shown is seen; the minimap shows that much of the map by what its
// cells are, the view's outline, and the entities there are.
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "lvr/entity.hpp"
#include "lvr/level.hpp"
#include "lvr/minimap.hpp"
#include "lvr/world.hpp"

using namespace lvr;

namespace {

struct Still : Entity {
    void update(World& /*world*/) override {}
};

std::array<int, 4> at(const Minimap& map, int x, int y) {
    const std::size_t i =
        ((static_cast<std::size_t>(y) * static_cast<std::size_t>(map.width)) + static_cast<std::size_t>(x)) * 4;
    return {map.pixels.at(i), map.pixels.at(i + 1), map.pixels.at(i + 2), map.pixels.at(i + 3)};
}

// 60 x 40 cells (960 x 640 pixels), open, with a floor along row 30, a ladder in column 5 and spikes in cell
// (40, 29); the view (the original's size) at its top left corner.
World sample_world() {
    World world;
    world.map = Map{.width = 60, .height = 40, .cells = std::vector<Cell>(2400)};
    for (int column = 0; column < 60; ++column) {
        world.map.cells.at((30 * 60) + static_cast<std::size_t>(column)).tile_type = tile::kGround;
    }
    for (int row = 0; row < 30; ++row) {
        world.map.cells.at((static_cast<std::size_t>(row) * 60) + 5).tile_type = tile::kLadder;
    }
    world.map.cells.at((29 * 60) + 40).tile_type = tile::kSpikes;
    world.has_vikings = true;
    return world;
}

}  // namespace

TEST_CASE("minimap: what a view shows is seen, to the minimap's pixel, and stays seen") {
    World world = sample_world();
    const std::size_t across = 60 * kMinimapCell;
    const auto was_seen = [&](const std::vector<bool>& seen, std::size_t x, std::size_t y) {
        return seen.at((y * across) + x);
    };
    std::vector<bool> seen;
    reveal(seen, world);
    REQUIRE(seen.size() == across * 40 * kMinimapCell);
    CHECK(was_seen(seen, 0, 0));
    CHECK(was_seen(seen, 79, 43));  // the original's view: 320 x 176 of the level, 80 x 44 here
    CHECK_FALSE(was_seen(seen, 80, 43));
    CHECK_FALSE(was_seen(seen, 79, 44));
    // A view moved by less than a cell shows that much more, not a cell.
    world.camera.x = 6;
    reveal(seen, world);
    CHECK(was_seen(seen, 80, 43));  // level pixels 320-323
    CHECK(was_seen(seen, 81, 43));  // the view reaches 2 of its 4
    CHECK_FALSE(was_seen(seen, 82, 43));
    world.camera.x = 400;
    world.camera.y = 380;
    reveal(seen, world);
    CHECK(was_seen(seen, 0, 0));  // what was seen before
    CHECK(was_seen(seen, 100, 95));
    CHECK(was_seen(seen, 179, 138));
    CHECK_FALSE(was_seen(seen, 180, 138));
    CHECK_FALSE(was_seen(seen, 179, 139));
    // A view past the map's edges shows what there is.
    world.camera.x = 900;
    world.camera.y = 600;
    world.camera.view_width = 640;
    world.camera.view_height = 480;
    reveal(seen, world);
    CHECK(was_seen(seen, 239, 159));
    // Another map: nothing of it was seen.
    world.map = Map{.width = 4, .height = 4, .cells = std::vector<Cell>(16)};
    world.camera = {};
    world.camera.view_width = 16;
    world.camera.view_height = 16;
    reveal(seen, world);
    CHECK(seen.size() == 16 * 16);
    CHECK(seen.at(0));
    CHECK(seen.at((3 * 16) + 3));
    CHECK_FALSE(seen.at((3 * 16) + 4));
}

TEST_CASE("minimap: the whole map by what its cells are, a ladder with its rungs, and what of it was seen") {
    World world = sample_world();
    world.camera.x = 320;
    world.camera.y = 320;
    std::vector<bool> seen;
    reveal(seen, world);
    world.entities.at(0) = std::make_unique<Still>();  // it is only a map: no Viking or enemy is on it
    world.entities.at(0)->x = 400;
    world.entities.at(0)->y = 400;
    world.entities.at(5) = std::make_unique<Still>();
    world.entities.at(5)->x = 560;
    world.entities.at(5)->y = 440;
    world.entities.at(5)->acts_as = tile::kEnemy;

    // Four pixels to a cell: a place of the level is at a quarter of its numbers. A pixel's fourth number says
    // whether it was seen.
    const Minimap map = draw_minimap(world, seen);
    REQUIRE(map.width == 60 * kMinimapCell);
    REQUIRE(map.height == 40 * kMinimapCell);
    REQUIRE(map.pixels.size() == static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height) * 4);
    const auto colour = [](const std::array<int, 4>& pixel) {
        return std::array<int, 3>{pixel.at(0), pixel.at(1), pixel.at(2)};
    };
    const std::array<int, 4> open = at(map, 120, 90);
    const std::array<int, 4> ground = at(map, 120, 121);  // row 30
    CHECK(open.at(3) == 255);
    CHECK(ground.at(3) == 255);
    CHECK(colour(ground) != colour(open));
    CHECK(colour(at(map, 162, 118)) != colour(ground));  // the spikes, in their own colour
    CHECK(colour(at(map, 162, 118)) != colour(open));
    CHECK(at(map, 100, 100) == open);  // where the Viking is
    CHECK(at(map, 140, 110) == open);  // and the enemy
    // What wasn't seen is there all the same, marked so.
    CHECK(at(map, 40, 20).at(3) == 0);
    CHECK(colour(at(map, 40, 20)) == colour(open));
    CHECK(at(map, 40, 121).at(3) == 0);
    CHECK(colour(at(map, 40, 121)) == colour(ground));
    // The ladder: dark, with a lighter rung to a cell, its upper half.
    const std::array<int, 4> rung = at(map, 21, 100);
    const std::array<int, 4> ladder = at(map, 21, 102);
    CHECK(colour(ladder) != colour(ground));
    CHECK(colour(ladder) != colour(open));
    CHECK(rung.at(0) > ladder.at(0));
    CHECK(at(map, 21, 101) == rung);
    CHECK(at(map, 21, 103) == ladder);
    CHECK(at(map, 21, 104) == rung);

    // Nothing seen at all.
    const Minimap dark = draw_minimap(world, {});
    CHECK(at(dark, 120, 121).at(3) == 0);
    CHECK(colour(at(dark, 120, 121)) == colour(ground));
}
