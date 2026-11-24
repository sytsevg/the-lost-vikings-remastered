// Tests for src/lvr/camera.*: the camera by hand.
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "assets/objects.hpp"
#include "lvr/camera.hpp"

using namespace lvr;

TEST_CASE("camera: the limits of a map, and where it starts") {
    Camera camera;
    set_camera_limits(camera, 30, 30);
    CHECK(camera.max_x == 160);
    CHECK(camera.max_y == 304);
    start_camera(camera, 32, 418);  // level 1's first Viking
    CHECK(camera.x == 0);
    CHECK(camera.y == 304);
    start_camera(camera, 300, 200);
    CHECK(camera.x == 140);
    CHECK(camera.y == 112);
    start_camera(camera, 470, -176);  // dropped in from above
    CHECK(camera.x == 160);
    CHECK(camera.y == 0);
}

TEST_CASE("camera: still while the entity is in the box, after it otherwise, a tick's move in three shares") {
    Camera camera;
    set_camera_limits(camera, 100, 100);
    camera.x = 500;
    camera.y = 500;
    follow(camera, 500 + 160, 500 + 88, 0);
    CHECK(camera.x == 500);
    CHECK(camera.moved_x == 0);
    // 7 pixels right of the box, 30 above it (16 at most).
    follow(camera, 500 + 176 + 7, 500 + 80 - 30, 0);
    CHECK(camera.out_right == 7);
    CHECK(camera.out_up == 16);
    CHECK(camera.x == 503);
    CHECK(camera.y == 494);
    CHECK(camera.moved_x == 3);
    CHECK(camera.moved_y == 6);
    follow(camera, 0, 0, 1);  // the position is only read in the first picture
    follow(camera, 0, 0, 2);
    CHECK(camera.x == 507);
    CHECK(camera.y == 484);

    // Not past the edges; not at all when locked.
    camera.x = 2;
    camera.y = camera.max_y - 1;
    follow(camera, 0, camera.y + 176, 0);
    CHECK(camera.x == 0);
    CHECK(camera.moved_x == 2);
    CHECK(camera.y == camera.max_y);
    CHECK(camera.moved_y == 1);
    camera.locked_x = camera.locked_y = true;
    camera.x = 100;
    camera.y = 100;
    follow(camera, 0, 0, 0);
    CHECK(camera.x == 100);
    CHECK(camera.y == 100);
}
