// Tests for pairing up the animation programs' places between two worlds' object databases (src/lvr/places.*).
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <vector>

#include "assets/assets.hpp"
#include "lvr/places.hpp"

using namespace lvr;

namespace {

// A database with room for the records and some scripts after them.
struct Database {
    std::vector<std::byte> bytes = std::vector<std::byte>(0x2000, std::byte{0x10});  // "remove" everywhere

    void script_of(std::size_t kind, std::uint16_t at) {
        bytes.at((kind * 0x15) + 3) = static_cast<std::byte>(at & 0xffU);
        bytes.at((kind * 0x15) + 4) = static_cast<std::byte>(at >> 8U);
    }
    void put(std::size_t at, std::initializer_list<int> code) {
        for (const int byte : code) {
            bytes.at(at++) = static_cast<std::byte>(byte);
        }
    }
};

}  // namespace

TEST_CASE("places: animations started by the same instructions of two databases are paired") {
    Database ours;
    Database theirs;
    for (std::size_t kind = 0; kind < 235; ++kind) {
        ours.script_of(kind, 0x1ff0);
        theirs.script_of(kind, 0x1ff0);
    }
    // Kind 1: the jump every script starts with, then: an animation, a call, a yield, a test with a jump, a
    // store through a byte of kinds, and a jump back.
    ours.script_of(1, 0x1400);
    ours.put(0x1400, {0x03, 0xf0, 0x1f});
    ours.put(0x1403, {0x19, 0x11, 0x11, 0x05, 0x40, 0x14, 0x00, 0x23, 0x17, 0x50, 0x14, 0x26,
                      0x0a, 0x1e, 0x06, 0x02, 0x12, 0x06, 0x02, 0x08, 0x02, 0x03, 0x03, 0x14});
    ours.put(0x1440, {0x19, 0x22, 0x22, 0x06});
    ours.put(0x1450, {0x19, 0x33, 0x33, 0x10});
    theirs.script_of(1, 0x1600);
    theirs.put(0x1600, {0x03, 0xf0, 0x1f});
    theirs.put(0x1603, {0x19, 0xaa, 0x1a, 0x05, 0x80, 0x16, 0x00, 0x23, 0x17, 0x90, 0x16, 0x26,
                        0x0a, 0x1e, 0x06, 0x02, 0x12, 0x06, 0x02, 0x08, 0x02, 0x03, 0x03, 0x16});
    theirs.put(0x1680, {0x19, 0xbb, 0x1b, 0x06});
    theirs.put(0x1690, {0x19, 0xcc, 0x1c, 0x10});
    // Kind 5 is another kind in their world: its scripts part at once, and nothing of it is paired.
    ours.script_of(5, 0x1700);
    ours.put(0x1700, {0x03, 0xf0, 0x1f, 0x19, 0x44, 0x44, 0x00});
    theirs.script_of(5, 0x1700);
    theirs.put(0x1700, {0x03, 0xf0, 0x1f, 0x00, 0x19, 0x55, 0x55});
    // Kind 6 names the first animation again, at another place of theirs: the first pairing stands.
    ours.script_of(6, 0x1800);
    ours.put(0x1800, {0x03, 0xf0, 0x1f, 0x19, 0x11, 0x11, 0x00});
    theirs.script_of(6, 0x1800);
    theirs.put(0x1800, {0x03, 0xf0, 0x1f, 0x19, 0xee, 0x1e, 0x00});

    const AnimationPlaces places = pair_animation_places(ours.bytes, theirs.bytes);
    CHECK(places.size() == 3);
    REQUIRE(places.contains(0x1111));
    CHECK(places.at(0x1111) == 0x1aaa);
    CHECK(places.at(0x2222) == 0x1bbb);  // through the call
    CHECK(places.at(0x3333) == 0x1ccc);  // through the test's jump
    CHECK_FALSE(places.contains(0x4444));

    // Databases that end early, or scripts that run past the end, pair nothing more.
    CHECK(pair_animation_places({}, {}).empty());
    Database cut;
    cut.script_of(1, 0x1ffe);
    cut.bytes[0x1ffe] = std::byte{0x19};
    CHECK(pair_animation_places(cut.bytes, cut.bytes).empty());
    cut.bytes[0x1ffe] = std::byte{0xf0};  // not an instruction
    CHECK(pair_animation_places(cut.bytes, cut.bytes).empty());
    cut.bytes[0x1ffe] = std::byte{0x26};  // its byte of kinds is the last byte
    CHECK(pair_animation_places(cut.bytes, cut.bytes).empty());
    cut.bytes[0x1fff] = std::byte{0x26};
    CHECK(pair_animation_places(std::span{cut.bytes}.first(0x1fff), cut.bytes).empty());
}

TEST_CASE("real data: the Vikings' animations are found in every world's database", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    const auto files = assets::GameFiles::open(LVR_ORIGINAL_DIR);
    REQUIRE(files);
    const auto spaceship = assets::load_object_database(files->archive(), 449);
    REQUIRE(spaceship);
    for (const int entry : {450, 451, 452, 453, 454}) {
        const auto other = assets::load_object_database(files->archive(), static_cast<std::uint16_t>(entry));
        REQUIRE(other);
        const AnimationPlaces places = pair_animation_places(spaceship->bytes, other->bytes);
        CHECK(places.size() > 150);
        // Standing: Erik, Baleog, Olaf. Walking, the ladder, the fruit.
        for (const int program : {0x4c7d, 0x403a, 0x587d, 0x4c89, 0x4dd5, 0x7f45}) {
            CHECK(places.contains(static_cast<std::uint16_t>(program)));
        }
    }
    // The same database against itself: every place is its own.
    for (const auto& [ours, theirs] : pair_animation_places(spaceship->bytes, spaceship->bytes)) {
        CHECK(ours == theirs);
    }
}
