// Tests for src/input/input.*: the actions, the original's keys and joystick, a keys file, Remaster rules' controls,
// a gamepad's reading, and the characters keys type.
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "input/input.hpp"

using namespace lvr::input;

namespace {

std::uint16_t key_word(const Controls& controls, std::string_view key) {
    return game_bits(key_actions(controls, key));
}
std::uint16_t pad_word(const Controls& controls, std::string_view input) {
    return game_bits(pad_actions(controls, input));
}
const std::vector<std::string>& keys_of(const Controls& controls, Action action) {
    return controls.keys.at(static_cast<std::size_t>(action));
}
std::size_t at(std::string_view input) {
    return pad_input_index(input).value();
}

}  // namespace

TEST_CASE("input: the actions by name, label and the input word's bits") {
    CHECK(name_of(Action::kAbilityF) == "ability-f");
    CHECK(label_of(Action::kAbilityF) == "ABILITY 1");
    CHECK(label_of(Action::kPreviousViking) == "PREVIOUS VIKING");
    CHECK(action_named("zoom-out") == Action::kZoomOut);
    CHECK_FALSE(action_named("jump"));
    CHECK_FALSE(action_named("none"));
    for (std::size_t i = 0; i < kActionCount; ++i) {  // every name goes back to its action
        const auto action = static_cast<Action>(i);
        CHECK(action_named(name_of(action)) == action);
        CHECK((game_bits(bit(action)) == 0) == (action > Action::kPause));  // Remaster's own have none
    }
    // docs/original/input.md
    CHECK(game_bits(bit(Action::kRight)) == 0x0100);
    CHECK(game_bits(bit(Action::kInventory)) == 0x2000);
    CHECK(game_bits(bit(Action::kNextViking)) == 0x0010);
    CHECK(game_bits(bit(Action::kLeft) | bit(Action::kUp) | bit(Action::kRewind)) == 0x0A00);
}

TEST_CASE("input: the original's default keys and joystick") {
    const Controls original = original_controls();
    CHECK(key_word(original, "Right") == 0x0100);
    CHECK(key_word(original, "Keypad 5") == 0x0400);
    CHECK(key_word(original, "Return") == 0x8000);
    CHECK(key_word(original, "Left Ctrl") == 0x0020);
    CHECK(key_word(original, "Escape") == 0x1000);
    CHECK(key_word(original, "Q") == 0);
    // The joystick (0000:2EF8): buttons 1-4 as F, D, E, S; shoulders switch Vikings, Start pauses, Back the
    // inventory, the d-pad and the left stick as the arrows. Nothing of Remaster's own.
    CHECK(pad_word(original, "a") == 0x8000);
    CHECK(pad_word(original, "b") == 0x4000);
    CHECK(pad_word(original, "x") == 0x0040);
    CHECK(pad_word(original, "y") == 0x0080);
    CHECK(pad_word(original, "leftshoulder") == 0x0020);
    CHECK(pad_word(original, "rightshoulder") == 0x0010);
    CHECK(pad_word(original, "start") == 0x1000);
    CHECK(pad_word(original, "back") == 0x2000);
    CHECK(pad_word(original, "dpleft") == 0x0200);
    CHECK(pad_word(original, "lefty+") == 0x0400);
    for (const std::string_view input : pad_inputs()) {
        CHECK((game_bits(pad_actions(original, input)) != 0 || pad_actions(original, input) == 0));
    }
    // The stick counts from half way and always walks at full speed.
    PadReading reading{};
    reading.at(at("leftx+")) = 0.45F;
    CHECK(read_pad(original, reading, false).held == 0);
    reading.at(at("leftx+")) = 0.55F;
    const PadActions read = read_pad(original, reading, false);
    CHECK(game_bits(read.held) == 0x0100);
    CHECK(read.walk == 1);
}

TEST_CASE("input: a keys file rebinds, adds and unbinds keys") {
    auto keys = parse_bindings(
        "# my keys\n"
        "\n"
        "Right Shift = ability-f\n"
        "  W = up , ability-f  \r\n"
        "Space = none\n"
        "Z = zoom-out\n",
        original_controls());
    REQUIRE(keys);
    CHECK(key_word(*keys, "Right Shift") == 0x8000);
    CHECK(key_word(*keys, "W") == 0x8800);
    CHECK(key_word(*keys, "Space") == 0);
    CHECK(key_word(*keys, "F") == 0x8000);  // the rest stays
    CHECK(key_actions(*keys, "Z") == bit(Action::kZoomOut));

    CHECK(parse_bindings("W up\n", {}).error() == "line 1: expected 'KEY = action'");
    CHECK(parse_bindings("\n = up\n", {}).error() == "line 2: expected 'KEY = action'");
    CHECK(parse_bindings("W = jump\n", {}).error() == "line 1: unknown action 'jump'");
}

TEST_CASE("input: Remaster rules' default controls") {
    const Controls controls = remaster_controls();
    const Controls original = original_controls();
    // A key an action; Escape no longer pauses: it opens the menu.
    const std::vector<std::vector<std::string>> game_keys{{"Left"},      {"Right"},      {"Up"},  {"Down"},
                                                          {"Space"},     {"D"},          {"E"},   {"S"},
                                                          {"Left Ctrl"}, {"Right Ctrl"}, {"Tab"}, {"P"}};
    for (std::size_t action = 0; action < game_keys.size(); ++action) {
        CHECK(controls.keys.at(action) == game_keys.at(action));
    }
    for (const std::vector<std::string>& keys : controls.keys) {
        CHECK(keys.size() == 1);
    }
    CHECK(key_actions(controls, "Escape") == 0);
    CHECK(key_actions(controls, "Keypad 6") == 0);
    CHECK(key_actions(controls, "Backspace") == bit(Action::kRewind));
    CHECK(key_actions(controls, "-") == bit(Action::kZoomOut));
    CHECK(key_actions(controls, "M") == bit(Action::kMinimap));
    CHECK(key_actions(controls, "F11") == 0);  // (the screen's look is the menu's)
    CHECK(key_actions(controls, "F8") == bit(Action::kQuickload));
    // The joystick, but Start opens the menu and nothing pauses (a stick's press is too easily pressed while
    // walking); Remaster's own on what was free.
    CHECK(pad_word(controls, "a") == pad_word(original, "a"));
    CHECK(pad_word(controls, "back") == pad_word(original, "back"));
    CHECK(pad_actions(controls, "start") == 0);
    CHECK(pad_actions(controls, "leftstick") == 0);
    CHECK(controls.pad.at(static_cast<std::size_t>(Action::kPause)).empty());
    CHECK(pad_actions(controls, "lefttrigger") == bit(Action::kRewind));
    CHECK(pad_actions(controls, "righty-") == bit(Action::kZoomOut));
    CHECK(pad_actions(controls, "righty+") == bit(Action::kZoomIn));
    CHECK(pad_actions(controls, "rightstick") == bit(Action::kMinimap));
    CHECK(pad_actions(controls, "righttrigger") == 0);
}

TEST_CASE("input: binding a key or a pad's input moves it") {
    Controls controls = remaster_controls();
    REQUIRE(bind_key(controls, Action::kUseItem, "Space"));  // Space was the first ability's
    CHECK(key_actions(controls, "Space") == bit(Action::kUseItem));
    CHECK(keys_of(controls, Action::kAbilityF).empty());
    CHECK(keys_of(controls, Action::kUseItem) == std::vector<std::string>{"Space"});  // in place of E
    CHECK(key_actions(controls, "E") == 0);
    CHECK_FALSE(bind_key(controls, Action::kPause, "Escape"));  // fixed: the menu's
    CHECK_FALSE(bind_key(controls, Action::kPause, "F10"));
    CHECK_FALSE(bind_key(controls, Action::kPause, "Left Alt"));
    CHECK_FALSE(bind_key(controls, Action::kPause, ""));
    REQUIRE(bind_pad(controls, Action::kRewind, "righttrigger"));
    CHECK(pad_actions(controls, "righttrigger") == bit(Action::kRewind));
    CHECK(pad_actions(controls, "lefttrigger") == bit(Action::kRewind));  // it keeps the one it had
    REQUIRE(bind_pad(controls, Action::kZoomIn, "a"));
    CHECK(pad_actions(controls, "a") == bit(Action::kZoomIn));
    CHECK_FALSE(bind_pad(controls, Action::kPause, "start"));
    CHECK_FALSE(bind_pad(controls, Action::kPause, "guide"));
    CHECK_FALSE(bind_pad(controls, Action::kPause, "jump"));
}

TEST_CASE("input: a gamepad's inputs, labels and readings") {
    CHECK(pad_inputs().size() == kPadInputCount);
    CHECK(pad_input_index("a") == 0);
    CHECK(pad_input_index("dpright") == 14);  // SDL_GAMEPAD_BUTTON_DPAD_RIGHT
    CHECK(pad_input_index("misc6") == 25);    // the last of SDL's buttons
    CHECK(pad_input_index("righttrigger") == kPadInputCount - 1);
    CHECK_FALSE(pad_input_index("jump"));
    CHECK(pad_label("leftshoulder") == "LB");
    CHECK(pad_label("lefty-") == "LS UP");
    CHECK(pad_label("jump") == "?");

    const Controls controls = remaster_controls();
    PadReading reading{};
    CHECK(read_pad(controls, reading, true).held == 0);
    CHECK(read_pad(controls, reading, true).walk == 1);
    reading.at(at("a")) = 1;
    reading.at(at("righty+")) = 0.4F;  // under half way: no zoom
    CHECK(read_pad(controls, reading, true).held == bit(Action::kAbilityF));
    reading.at(at("righty+")) = 0.6F;
    CHECK(read_pad(controls, reading, true).held == (bit(Action::kAbilityF) | bit(Action::kZoomIn)));

    // Walking: from the dead zone on, as fast as the stick is pushed; full speed near its end, or with the d-pad.
    reading = {};
    reading.at(at("leftx-")) = 0.15F;
    CHECK(read_pad(controls, reading, true).held == 0);
    reading.at(at("leftx-")) = 0.55F;
    PadActions read = read_pad(controls, reading, true);
    CHECK(read.held == bit(Action::kLeft));
    CHECK(read.walk > 0.49F);
    CHECK(read.walk < 0.51F);
    CHECK(read_pad(controls, reading, false).walk == 1);  // (without: at full speed)
    reading.at(at("leftx-")) = 0.95F;
    CHECK(read_pad(controls, reading, true).walk == 1);
    // A stick's direction counts from the dead zone where it is the way the stick is pushed most; else from half
    // way, as without.
    reading.at(at("leftx-")) = 0.3F;
    reading.at(at("lefty-")) = 0.25F;  // a little up while walking: not up
    read = read_pad(controls, reading, true);
    CHECK(read.held == bit(Action::kLeft));
    CHECK(read.walk < 0.2F);
    CHECK(read.climb == 1);
    reading.at(at("lefty-")) = 0.55F;  // up a ladder, a little to the side: not left
    read = read_pad(controls, reading, true);
    CHECK(read.held == bit(Action::kUp));
    CHECK(read.climb > 0.49F);
    CHECK(read.climb < 0.51F);
    CHECK(read.walk == 1);
    reading.at(at("leftx-")) = 0.55F;  // as far both ways: both
    CHECK(read_pad(controls, reading, true).held == (bit(Action::kLeft) | bit(Action::kUp)));
    reading.at(at("lefty-")) = 0.3F;
    reading.at(at("dpleft")) = 1;
    CHECK(read_pad(controls, reading, true).walk == 1);
    reading = {};
    reading.at(at("dpup")) = 1;  // the d-pad climbs at full speed
    CHECK(read_pad(controls, reading, true).climb == 1);
    CHECK(read_pad(controls, reading, true).held == bit(Action::kUp));
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
