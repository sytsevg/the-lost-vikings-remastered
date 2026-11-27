// Tests for src/lvr/handler.* (docs/original/input.md): the player's input between steps as the window gives it.
// The keyboard word as the original's handler keeps it; a key during a demo; Escape; what the menu leaves behind.
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <memory>

#include "input/input.hpp"
#include "lvr/handler.hpp"
#include "lvr/names.hpp"
#include "lvr/session.hpp"

using namespace lvr;
using input::Action;

namespace {

std::unique_ptr<Session> opened(int slot, Rules rules) {
    auto session = Session::open(LVR_ORIGINAL_DIR, slot, {}, rules);
    REQUIRE(session);
    return std::move(*session);
}

std::uint16_t bits(Action action) {
    return input::game_bits(input::bit(action));
}

bool nothing_held(std::string_view /*key*/) {
    return false;
}

}  // namespace

TEST_CASE(
    "real data: handler: a press sets its key's bits, a release clears them though another key with them is "
    "held, the keyboard's repeat sets them again",
    "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    auto session = opened(0, Rules::kVanilla);
    InputHandler hands{input::original_controls(), false};
    (void)hands.pressed("Left", false, false, *session);
    (void)hands.pressed("Keypad 4", false, false, *session);  // (left too)
    CHECK((hands.keyboard() & bits(Action::kLeft)) != 0);
    hands.released("Keypad 4");
    CHECK((hands.keyboard() & bits(Action::kLeft)) == 0);  // as the original's handler: Left is held, all the same
    (void)hands.pressed("Left", true, false, *session);    // the keyboard's own repeat of the held Left
    CHECK((hands.keyboard() & bits(Action::kLeft)) != 0);
    CHECK(hands.tick(nothing_held, *session).keys == hands.keyboard());
    hands.released("Left");
    CHECK(hands.keyboard() == 0);
}

TEST_CASE("real data: handler: a key during a demo ends it; under Remaster rules an action key, Enter or Escape only",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    auto vanilla = opened(scene::kIntro, Rules::kVanilla);
    REQUIRE(vanilla->demo());
    InputHandler any{input::original_controls(), false};
    (void)any.pressed("Q", false, false, *vanilla);  // (a key nothing is bound to)
    CHECK(any.keyboard() == 0xFFFF);

    auto remaster = opened(scene::kIntro, Rules::kRemaster);
    REQUIRE(remaster->demo());
    InputHandler some{input::remaster_controls(), true};
    (void)some.pressed("Q", false, false, *remaster);
    CHECK(some.keyboard() != 0xFFFF);
    (void)some.pressed("Space", false, false, *remaster);  // (the F ability's: an action key)
    CHECK(some.keyboard() == 0xFFFF);
}

TEST_CASE(
    "real data: handler: under Remaster rules Escape opens the menu in a level being played, and is the "
    "original's pause elsewhere; what the menu asked of the original's boxes waits for a step to take it",
    "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    auto level = opened(0, Rules::kRemaster);
    REQUIRE(level->takes_play_keys());
    InputHandler hands{input::remaster_controls(), true};
    CHECK(hands.pressed("Escape", false, false, *level) == InputHandler::Asks::kMenu);
    CHECK(hands.keyboard() == 0);
    CHECK_FALSE(hands.tick(nothing_held, *level).escaped);

    // At the title Escape isn't the original's pause, which its title takes as Enter: it goes
    // back from the password, and at the menu does nothing. Enter is the original's Enter there, as Space chooses too.
    auto title = opened(scene::kTitle, Rules::kRemaster);
    InputHandler at_title{input::remaster_controls(), true};
    CHECK(at_title.pressed("Escape", false, false, *title) == InputHandler::Asks::kNothing);
    CHECK(at_title.keyboard() == 0);
    CHECK(at_title.tick(nothing_held, *title).escaped);
    CHECK_FALSE(at_title.tick(nothing_held, *title).escaped);  // (once)
    at_title.released("Escape");
    (void)at_title.pressed("Return", false, false, *title);
    CHECK(at_title.keyboard() == key::kEnter);
    at_title.released("Return");
    CHECK(at_title.keyboard() == 0);
    // Elsewhere outside a level, Escape is the original's pause as it was (the intro).
    auto intro = opened(scene::kIntro, Rules::kRemaster);
    InputHandler in_intro{input::remaster_controls(), true};
    (void)in_intro.pressed("Escape", false, false, *intro);
    CHECK((in_intro.keyboard() & bits(Action::kPause)) != 0);

    hands.give_up();
    hands.quit_box();
    CHECK((hands.tick(nothing_held, *level).keys & bits(Action::kPause)) != 0);
    hands.stepped(false, *level);  // (online: the step didn't take this machine's input)
    const Tick kept = hands.tick(nothing_held, *level);
    CHECK((kept.keys & bits(Action::kPause)) != 0);
    CHECK(kept.menu_keys.quit);
    hands.stepped(true, *level);
    const Tick taken = hands.tick(nothing_held, *level);
    CHECK((taken.keys & bits(Action::kPause)) == 0);
    CHECK_FALSE(taken.menu_keys.quit);

    // Enter (Remaster rules): the boxes' word shown chosen, kept until a step takes it; not the original's.
    (void)hands.pressed("Return", false, false, *level);
    hands.stepped(false, *level);
    CHECK(hands.tick(nothing_held, *level).menu_keys.choose);
    hands.stepped(true, *level);
    CHECK_FALSE(hands.tick(nothing_held, *level).menu_keys.choose);
    InputHandler original{input::original_controls(), false};
    auto vanilla = opened(0, Rules::kVanilla);
    (void)original.pressed("Return", false, false, *vanilla);
    CHECK_FALSE(original.tick(nothing_held, *vanilla).menu_keys.choose);
}

TEST_CASE("handler: what a gamepad holds as the menu closes is quiet until it is let go") {
    InputHandler hands{input::remaster_controls(), true};
    const std::size_t a = input::pad_input_index("a").value_or(0);
    input::PadReading pushed{};
    pushed.at(a) = 1;
    hands.read_pads(pushed);
    CHECK(hands.pads().at(a) == 1);
    hands.pads_read();
    hands.menu_closed();
    hands.read_pads(pushed);  // (still held as it closed)
    CHECK(hands.pads().at(a) == 0);
    hands.pads_read();
    hands.read_pads(input::PadReading{});  // let go ...
    hands.pads_read();
    hands.read_pads(pushed);  // ... and pushed again
    CHECK(hands.pads().at(a) == 1);
}
