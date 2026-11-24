// Tests for src/lvr/terrain.*: a few cases by hand.
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "assets/objects.hpp"
#include "lvr/entity.hpp"
#include "lvr/terrain.hpp"
#include "lvr/world.hpp"

using namespace lvr;

namespace {

class Thing : public Entity {
  public:
    void update(World& /*world*/) override {}
};

// A 10 x 6 map: free, with ground along the bottom row and a wall in column 6.
World sample_world() {
    World world;
    world.map = Map{.width = 10, .height = 6, .cells = std::vector<Cell>(60)};
    for (int column = 0; column < 10; ++column) {
        world.map.cells[static_cast<std::size_t>((5 * 10) + column)].tile_type = tile::kGround;
    }
    for (int row = 0; row < 5; ++row) {
        world.map.cells[static_cast<std::size_t>((row * 10) + 6)].tile_type = tile::kGround;
    }
    return world;
}

// An entity 16 wide and 28 high with its feet at `bottom`.
Thing standing(int x, int bottom) {
    Thing thing;
    thing.x = static_cast<std::int16_t>(x);
    thing.y = static_cast<std::int16_t>(bottom - 13);
    thing.box = {.left = static_cast<std::int16_t>(x - 8),
                 .top = static_cast<std::int16_t>(bottom - 27),
                 .right = static_cast<std::int16_t>(x + 7),
                 .bottom = static_cast<std::int16_t>(bottom)};
    thing.previous_x = thing.x;
    thing.previous_y = thing.y;
    return thing;
}

}  // namespace

TEST_CASE("terrain: what is beside an entity; the map's edges") {
    const World world = sample_world();
    const TileTypes solid = tile_types({tile::kGround});
    const Thing viking = standing(88, 79);  // on the ground, its right edge at 95: against the wall at 96
    CHECK(find_beside(world, viking, Side::kBelow, solid) == Found{.tile_type = tile::kGround});
    CHECK_FALSE(find_beside(world, viking, Side::kAbove, solid));
    CHECK(find_beside(world, viking, Side::kAhead, solid) == Found{.tile_type = tile::kGround});
    CHECK_FALSE(find_beside(world, viking, Side::kBehind, solid));
    Thing turned = viking;
    turned.flags = entity_flag::kFacingLeft;
    CHECK_FALSE(find_beside(world, turned, Side::kAhead, solid));
    CHECK(find_beside(world, turned, Side::kBehind, solid));
    CHECK(find_ahead_below(world, viking, solid));  // (96, 80): the ground under the wall
    CHECK(find_at(world, viking, 100, 10, solid));
    CHECK_FALSE(find_at(world, viking, 50, 10, solid));
    // Left and right of the map is ground, above and below it nothing.
    CHECK(find_at(world, viking, -5, 10, solid));
    CHECK(find_at(world, viking, 160, 10, solid));
    CHECK_FALSE(find_at(world, viking, 50, -5, solid));
    CHECK_FALSE(find_at(world, viking, 50, 96, solid));
    CHECK(can_walk_on(world, turned));        // free at its feet 16 to the left, ground under that
    CHECK_FALSE(can_walk_on(world, viking));  // the wall
}

TEST_CASE("terrain: other entities act as cells; the Vikings alone") {
    World world = sample_world();
    world.entities[0] = make_entity<Thing>(standing(40, 79));  // a Viking
    world.entities[0]->slot = 0;
    world.entities[0]->acts_as = tile::kEnemy;
    world.entities[5] = make_entity<Thing>(standing(64, 79));  // a door
    world.entities[5]->slot = 5;
    world.entities[5]->acts_as = tile::kGround;
    Thing walker = standing(48 + 8, 79);  // between them: its box 48-63
    walker.slot = 7;
    const TileTypes solid = tile_types({tile::kGround});
    CHECK(find_beside(world, walker, Side::kAhead, solid) == Found{.tile_type = tile::kGround, .entity = 5});
    CHECK_FALSE(find_beside(world, walker, Side::kBehind, solid));
    CHECK(find_beside(world, walker, Side::kBehind, tile_types({0x2e})) == Found{.tile_type = 0x2e, .entity = 0});
    CHECK(find_viking_beside(world, walker, Side::kBehind, tile_types({0x2e})));
    CHECK_FALSE(find_viking_beside(world, walker, Side::kAhead, solid));  // the door isn't a Viking
    CHECK_FALSE(find_viking_beside(world, walker, Side::kBelow, solid));  // nor is the ground
    CHECK(find_at(world, walker, 70, 60, solid) == Found{.tile_type = tile::kGround, .entity = 5});
    // An entity doesn't find itself.
    CHECK_FALSE(find_at(world, *world.entities[5], 70, 60, tile_types({0x2e})));
}

TEST_CASE("terrain: pushed out of a wall, onto the floor, under a ceiling") {
    const World world = sample_world();
    const TileTypes solid = tile_types({tile::kGround});
    Thing runner = standing(88 + 5, 79);  // 5 pixels into the wall
    runner.previous_x = 88;
    runner.fraction_x = 0x80;
    REQUIRE(stop_at_wall(world, runner, solid));
    CHECK(runner.box.right == 95);
    CHECK(runner.x == 88);
    CHECK(runner.fraction_x == 0);
    CHECK_FALSE(stop_at_wall(world, runner, solid));  // it didn't move this time

    Thing faller = standing(40, 79 + 6);  // 6 pixels into the ground
    faller.previous_y = static_cast<std::int16_t>(faller.y - 6);
    Thing lander = faller;
    REQUIRE(stop_at_floor_or_ceiling(world, faller, solid));
    CHECK(faller.box.bottom == 79);
    REQUIRE(land(world, lander, solid));  // it crossed into the ground's cell row
    CHECK(lander.box.bottom == 79);
    CHECK(lander.y == 79 - 13);
    CHECK_FALSE(land(world, lander, solid));

    World low = sample_world();
    low.map.cells[2].tile_type = tile::kGround;  // a ceiling cell at column 2, row 0
    Thing jumper = standing(40, 12 + 27);        // its top at row 12, inside it
    jumper.previous_y = static_cast<std::int16_t>(jumper.y + 4);
    REQUIRE(stop_at_floor_or_ceiling(low, jumper, solid));
    CHECK(jumper.box.top == 16);
}

TEST_CASE("terrain: put on the ground: down onto it, up a ladder's top, not while rising") {
    World world = sample_world();
    Thing above = standing(40, 79 - 5);  // 5 pixels above the ground
    above.flags = entity_flag::kPutOnGround;
    above.push_y = 3;
    put_on_ground(world, above);
    CHECK(above.box.bottom == 79);
    CHECK(above.push_y == 0);
    CHECK((above.flags & entity_flag::kPutOnGround) == 0);

    world.map.cells[static_cast<std::size_t>((4 * 10) + 2)].tile_type = tile::kLadderTop;
    Thing in_top = standing(40, 64 + 3);  // 4 pixels into the ladder top's cell
    put_on_ground(world, in_top);
    CHECK(in_top.box.bottom == 63);

    Thing rising = standing(40, 79 - 5);
    rising.speed_y = px(-1);
    put_on_ground(world, rising);
    CHECK(rising.box.bottom == 74);
}

namespace {

struct Random {
    std::uint32_t seed;
    int below(int limit) {
        seed = (seed * 1664525U) + 1013904223U;
        return static_cast<int>((seed >> 9U) % static_cast<std::uint32_t>(limit));
    }
};

}  // namespace
