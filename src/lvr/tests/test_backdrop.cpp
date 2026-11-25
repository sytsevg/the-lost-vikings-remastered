// Tests for src/lvr/backdrop.*: which pixels of the map show the backdrop, and where the backdrop is for a camera.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>

#include "lvr/backdrop.hpp"

using namespace lvr;

namespace {

// As the SNES game shows it: whole pixels.
int whole(std::uint16_t scroll, int camera, int refreshes, int size) {
    return static_cast<int>(std::floor(backdrop_offset(scroll, camera, refreshes, size)));
}

}  // namespace

TEST_CASE("backdrop: colour 0 of the palette rows 0-7 shows it, nothing else does") {
    for (int row = 0; row < 8; ++row) {
        CHECK(shows_backdrop(static_cast<std::uint8_t>(row * 16)));
        CHECK_FALSE(shows_backdrop(static_cast<std::uint8_t>((row * 16) + 1)));
        CHECK_FALSE(shows_backdrop(static_cast<std::uint8_t>((row * 16) + 15)));
    }
    CHECK_FALSE(shows_backdrop(0x80));  // the rows from 8 on are not the SNES level's colours
    CHECK_FALSE(shows_backdrop(0xf0));
}

TEST_CASE("backdrop: it follows the camera at its speed in 256ths, and repeats") {
    CHECK(whole(0x40, 0, 0, 256) == 0);
    CHECK(whole(0x40, 100, 0, 256) == 25);    // a quarter
    CHECK(whole(0x40, 103, 0, 256) == 25);    // the 256ths left over are not shown
    CHECK(whole(0x100, 300, 0, 128) == 44);   // with the camera, repeated: 300 - 256
    CHECK(whole(0xc0, 1000, 0, 128) == 110);  // three quarters: 750 - 5 x 128
    CHECK(whole(0x40, 100, 9, 256) == 25);    // the refreshes don't matter on such an axis
    CHECK(whole(0x40, -8, 0, 256) == 0);      // never left of the level's edge
    CHECK(whole(0x40, 100, 0, 0) == 0);       // no backdrop
    // Between whole pixels of the camera (smooth movement) it moves on evenly.
    CHECK(backdrop_offset(0x40, 102.0, 0, 256) == Catch::Approx(25.5));
    CHECK(backdrop_offset(0x80, 10.5, 0, 256) == Catch::Approx(5.25));
}

TEST_CASE("backdrop: with bit 15 it moves by itself, whatever the camera does") {
    CHECK(whole(0x8500, 64, 0, 256) == 0);  // the spaceship's stars: 5 pixels a refresh
    CHECK(whole(0x8500, 64, 3, 256) == 15);
    CHECK(whole(0x8500, 0, 52, 256) == 4);                               // 260, repeated
    CHECK(whole(0x8080, 0, 3, 256) == 1);                                // half a pixel a refresh
    CHECK(backdrop_offset(0x8500, 0, 2.5, 256) == Catch::Approx(12.5));  // between two refreshes
}
