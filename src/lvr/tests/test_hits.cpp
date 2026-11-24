// Tests for src/lvr/hits.*: by hand.
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <vector>

#include "assets/objects.hpp"
#include "lvr/entity.hpp"
#include "lvr/hits.hpp"
#include "lvr/terrain.hpp"
#include "lvr/world.hpp"

using namespace lvr;

namespace {

class Thing : public Entity {
  public:
    void update(World& /*world*/) override {}
};

Thing& add(World& world, int slot, Box box) {
    auto thing = make_entity<Thing>();
    thing->slot = slot;
    thing->box = box;
    Thing& placed = *thing;
    world.entities.at(static_cast<std::size_t>(slot)) = std::move(thing);
    return placed;
}

}  // namespace

TEST_CASE("hits: the first entity whose box overlaps, by class or by a bit; edges count") {
    World world;
    Thing& viking = add(world, 0, {.left = 100, .top = 50, .right = 115, .bottom = 77});
    viking.hit_class = 1;
    Thing& arrow = add(world, 6, {.left = 116, .top = 60, .right = 131, .bottom = 63});
    arrow.hit_class = 4;
    arrow.hit_mask = 0x0010;
    Thing& enemy = add(world, 4, {.left = 131, .top = 40, .right = 160, .bottom = 60});
    enemy.hit_class = 9;
    enemy.hit_mask = 0x0003;
    CHECK(find_touching(world, arrow, 9) == 4);        // their edges share a pixel
    CHECK(find_touching(world, arrow, 1) == kNobody);  // the Viking is one pixel away
    CHECK(find_touching(world, enemy, 4) == 6);
    CHECK(find_touching(world, enemy, 9) == kNobody);  // not itself
    CHECK(find_touching_any(world, arrow, 0x0002) == 4);
    CHECK(find_touching_any(world, arrow, 0x0010) == kNobody);
    CHECK(find_touching_any(world, enemy, 0x0010) == 6);
    arrow.box.left = 115;
    CHECK(find_touching(world, arrow, 1) == 0);
    CHECK(find_touching(world, arrow, 1, true) == 0);
    CHECK(find_touching(world, arrow, 9, true) == kNobody);  // the enemy isn't in a Viking's slot
}

TEST_CASE("hits: who stands on an entity, one after the other") {
    World world;
    const TileTypes vikings = tile_types({0x2e});
    Thing& lift = add(world, 7, {.left = 100, .top = 200, .right = 163, .bottom = 207});
    Thing& erik = add(world, 0, {.left = 104, .top = 172, .right = 115, .bottom = 199});
    erik.acts_as = tile::kEnemy;
    Thing& olaf = add(world, 2, {.left = 150, .top = 172, .right = 165, .bottom = 199});
    olaf.acts_as = tile::kEnemy;
    Thing& baleog = add(world, 1, {.left = 300, .top = 172, .right = 315, .bottom = 199});
    baleog.acts_as = tile::kEnemy;
    // Among the Vikings: the row just above the lift must be inside the Viking's box.
    CHECK(find_in_row(world, lift, Row::kAbove, vikings, true) == 0);
    CHECK(find_in_row(world, lift, Row::kAbove, vikings, true, 1) == 2);
    CHECK(find_in_row(world, lift, Row::kAbove, vikings, true, 3) == kNobody);
    CHECK(find_in_row(world, lift, Row::kBelow, vikings, true) == kNobody);
    erik.box.top = 150;
    erik.box.bottom = 190;  // jumped
    CHECK(find_in_row(world, lift, Row::kAbove, vikings, true) == 2);
    // Among all: anything whose bottom edge is above that row.
    CHECK(find_in_row(world, lift, Row::kAbove, vikings, false) == 0);
    CHECK(find_in_row(world, lift, Row::kAbove, tile_types({0x01}), false) == kNobody);
    CHECK(find_in_row(world, *world.entities[0], Row::kBelow, tile_types({0x00}), false) == kNobody);
    (void)baleog;
}
