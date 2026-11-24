// Tests for src/input/input.*: the default keys, a bindings file, the gamepad's bits.
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "input/input.hpp"

using namespace lvr::input;

TEST_CASE("input: the original's default keys") {
    const Bindings keys = default_bindings();
    CHECK(key_bits(keys, "Right") == 0x0100);
    CHECK(key_bits(keys, "Keypad 5") == 0x0400);
    CHECK(key_bits(keys, "Return") == 0x8000);
    CHECK(key_bits(keys, "Left Ctrl") == 0x0020);
    CHECK(key_bits(keys, "Q") == 0);
}

TEST_CASE("input: a bindings file rebinds, adds and unbinds keys") {
    auto keys = parse_bindings(
        "# my keys\n"
        "\n"
        "Right Shift = ability-f\n"
        "  W = up , ability-f  \r\n"
        "Space = none\n",
        default_bindings());
    REQUIRE(keys);
    CHECK(key_bits(*keys, "Right Shift") == 0x8000);
    CHECK(key_bits(*keys, "W") == 0x8800);
    CHECK(key_bits(*keys, "Space") == 0);
    CHECK(key_bits(*keys, "F") == 0x8000);  // the rest stays

    CHECK(parse_bindings("W up\n", {}).error() == "line 1: expected 'KEY = action'");
    CHECK(parse_bindings("\n = up\n", {}).error() == "line 2: expected 'KEY = action'");
    CHECK(parse_bindings("W = jump\n", {}).error() == "line 1: unknown action 'jump'");
    CHECK(action_bits("inventory") == 0x2000);
}

TEST_CASE("input: the gamepad as the original's joystick") {
    CHECK(pad_bits(Pad{}) == 0);
    CHECK(pad_bits(Pad{.south = true}) == 0x8000);
    CHECK(pad_bits(Pad{.east = true, .west = true, .north = true}) == 0x40C0);
    CHECK(pad_bits(Pad{.left = true, .up = true}) == 0x0A00);
    CHECK(pad_bits(Pad{.right = true, .down = true}) == 0x0500);
    CHECK(pad_bits(Pad{.left_shoulder = true, .right_shoulder = true, .start = true, .back = true}) == 0x3030);
}

TEST_CASE("input: the character a key types for the scripts") {
    // The handler's table (ds:8E68): digits and consonants, Backspace, and four keys that confirm.
    CHECK(typed_character("P") == 'P');
    CHECK(typed_character("0") == '0');
    CHECK(typed_character("9") == '9');
    CHECK(typed_character("Z") == 'Z');
    for (const char* vowel : {"A", "E", "I", "O", "U"}) {
        CHECK(typed_character(vowel) == 0);
    }
    CHECK(typed_character("Backspace") == 0x80);
    for (const char* confirm : {"Return", "Space", "[", "]"}) {
        CHECK(typed_character(confirm) == 0x81);
    }
    for (const char* other : {"Left", "F1", "Keypad 5", "-", "", "Escape"}) {
        CHECK(typed_character(other) == 0);
    }
}
