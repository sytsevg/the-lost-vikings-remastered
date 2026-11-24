// Tests for src/lvr/movement.*: by hand.
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <vector>

#include "assets/objects.hpp"
#include "lvr/entity.hpp"
#include "lvr/movement.hpp"

using namespace lvr;

namespace {

// An entity with no behaviour.
class Thing : public Entity {
  public:
    void update(World& /*world*/) override {}
};

}  // namespace

TEST_CASE("movement: speed into the push, with gravity and the limit") {
    Thing thing;
    thing.flags = entity_flag::kFalls;
    thing.speed_x = px(1.5);
    thing.speed_y = px(15);
    thing.max_step_x = 0x800;
    thing.max_step_y = 0x1000;
    add_speed(thing, 0xf8, 0x100);
    CHECK(thing.speed_y == px(15.96875));
    CHECK(thing.speed_x == px(1.5));  // the level's push isn't for it
    CHECK(thing.push_x == 0x180);
    CHECK(thing.push_y == 0xff8);
    add_speed(thing, 0xf8, 0x100);
    CHECK(thing.speed_y == px(16));  // no faster than its limit
    CHECK(thing.push_y == 0xff8 + 0x1000);

    Thing left;
    left.flags = entity_flag::kFacingLeft | entity_flag::kUpsideDown | entity_flag::kPushed;
    left.speed_x = px(-9);
    left.speed_y = px(2);
    left.max_step_x = 0x800;
    left.max_step_y = 0x800;
    add_speed(left, 0xf8, 0x100);
    CHECK(left.speed_x == px(-8));
    CHECK(left.push_x == 0x800);  // the other way for one facing left
    CHECK(left.push_y == -0x200);

    Thing carried;
    carried.moved_by = 3;
    carried.flags = entity_flag::kFalls;
    add_speed(carried, 0xf8, 0);
    CHECK(carried.speed_y == 0);
}

TEST_CASE("movement: whole pixels, the rest kept; the box goes along") {
    Thing thing;
    thing.x = 100;
    thing.y = 50;
    thing.box = {.left = 92, .top = 36, .right = 107, .bottom = 63};
    thing.max_step_x = 0x800;
    thing.max_step_y = 0x1000;
    thing.push_x = 0x180;  // 1.5 pixels
    thing.push_y = -0x040;
    move(thing);
    CHECK(thing.previous_x == 100);
    CHECK(thing.x == 101);
    CHECK(thing.fraction_x == 0x80);
    CHECK(thing.push_x == 1);
    CHECK(thing.y == 49);  // -0.25 is -1 and 0.75 over
    CHECK(thing.fraction_y == 0xc0);
    CHECK(thing.box == Box{.left = 93, .top = 35, .right = 108, .bottom = 62});
    thing.push_x = 0x180;
    thing.push_y = 0x2000;
    move(thing);
    CHECK(thing.x == 103);  // the half pixel kept made a whole one
    CHECK(thing.fraction_x == 0);
    CHECK(thing.y == 65);  // 16 at most; the 0.75 over stays over
    CHECK(thing.previous_y == 49);

    Thing carried;
    carried.x = 7;
    carried.moved_by = kMovedFreely;
    carried.push_x = 0x300;
    move(carried);
    CHECK(carried.x == 7);
    CHECK(carried.previous_x == 7);
    CHECK(carried.push_x == 0x300);
}
