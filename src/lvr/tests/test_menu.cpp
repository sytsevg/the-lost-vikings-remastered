// Tests for src/lvr/menu.*: Remaster rules' menu, its pages and what its keys change, and its picture.
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "lvr/menu.hpp"
#include "lvr/render.hpp"

using namespace lvr;
using Key = Menu::Key;
using input::Action;

namespace {

Menu::Choices all_choices() {
    return {.has_modern = true, .screens = {"none", "crt", "xbr"}};
}

// To line `line` of the page, from its first.
void go_to(Menu& menu, std::size_t line) {
    while (menu.chosen() != line) {
        (void)menu.press(Key::kDown);
    }
}

std::vector<std::string> labels(const Menu& menu) {
    std::vector<std::string> out;
    for (const Menu::Line& line : menu.lines()) {
        out.push_back(line.label);
    }
    return out;
}

}  // namespace

TEST_CASE("menu: the first page leads to the others, and back resumes") {
    Menu menu{Settings{}, all_choices()};
    CHECK(menu.title() == "MENU");
    CHECK(labels(menu) == std::vector<std::string>{"OPTIONS", "KEYBOARD", "GAMEPAD", "GIVE UP LEVEL", "QUIT GAME"});
    CHECK(menu.press(Key::kBack) == Menu::Asked::kResume);
    CHECK(menu.press(Key::kUp) == Menu::Asked::kNothing);  // round to the last
    CHECK(menu.chosen() == 4);
    CHECK(menu.press(Key::kChoose) == Menu::Asked::kQuit);
    (void)menu.press(Key::kUp);
    CHECK(menu.press(Key::kChoose) == Menu::Asked::kGiveUp);
    go_to(menu, 0);
    CHECK(menu.press(Key::kChoose) == Menu::Asked::kNothing);
    CHECK(menu.title() == "OPTIONS");
    CHECK(menu.press(Key::kBack) == Menu::Asked::kNothing);
    CHECK(menu.title() == "MENU");
    CHECK(menu.chosen() == 0);  // where it was
    CHECK(menu.press(Key::kBack) == Menu::Asked::kResume);
}

TEST_CASE("menu: the gamepad's page binds only with a gamepad there, and says whether one is") {
    Menu menu{Settings{}, all_choices()};
    go_to(menu, 2);
    (void)menu.press(Key::kChoose);
    CHECK(menu.title() == "GAMEPAD");
    CHECK(menu.note().front() == "NO GAMEPAD CONNECTED");
    CHECK(menu.note_flashes());
    CHECK(menu.lines().front().dim);
    CHECK_FALSE(menu.lines().back().dim);  // back
    (void)menu.press(Key::kChoose);
    CHECK(menu.waiting() == Menu::Waiting::kNothing);
    (void)menu.press(Key::kClear);
    CHECK(menu.settings().controls.pad == input::remaster_controls().pad);
    menu.set_has_pad(true);  // plugged in
    CHECK(menu.note().front() == "GAMEPAD FOUND");
    CHECK_FALSE(menu.note_flashes());
    CHECK_FALSE(menu.lines().front().dim);
    (void)menu.press(Key::kChoose);
    CHECK(menu.waiting() == Menu::Waiting::kPad);
    menu.set_has_pad(false);  // and out again while it waits
    CHECK(menu.waiting() == Menu::Waiting::kNothing);
}

TEST_CASE("menu: the options change the settings") {
    Menu menu{Settings{}, all_choices()};
    go_to(menu, 0);
    (void)menu.press(Key::kChoose);
    CHECK(labels(menu) ==
          std::vector<std::string>{"SOUND", "MUSIC", "SOUND FX", "SCREEN", "ZOOM", "MINIMAP", "VISUAL FX", "BACK"});
    CHECK(menu.lines().at(0).value == "CLASSIC");
    (void)menu.press(Key::kRight);
    CHECK(menu.settings().modern_sound);
    CHECK(menu.lines().at(0).value == "MODERN");
    go_to(menu, 1);  // the music: from the loudest, down, never past 0
    CHECK(menu.lines().at(1).slider == kLoudest);
    (void)menu.press(Key::kRight);
    CHECK(menu.settings().music == kLoudest);
    for (int i = 0; i < kLoudest + 2; ++i) {
        (void)menu.press(Key::kLeft);
    }
    CHECK(menu.settings().music == 0);
    go_to(menu, 2);
    (void)menu.press(Key::kLeft);
    CHECK(menu.settings().sound_fx == kLoudest - 1);
    go_to(menu, 3);  // the screen: round through the looks
    CHECK(menu.lines().at(3).value == "OFF");
    (void)menu.press(Key::kLeft);
    CHECK(menu.settings().screen == "xbr");
    (void)menu.press(Key::kChoose);
    CHECK(menu.settings().screen == "none");
    go_to(menu, 4);  // the zoom: as far as it goes
    (void)menu.press(Key::kRight);
    (void)menu.press(Key::kRight);
    (void)menu.press(Key::kRight);
    CHECK(menu.settings().zoom == 2);
    CHECK(menu.lines().at(4).value == "2X");
    go_to(menu, 5);
    (void)menu.press(Key::kLeft);
    CHECK_FALSE(menu.settings().minimap);
    go_to(menu, 6);
    (void)menu.press(Key::kRight);
    CHECK_FALSE(menu.settings().visual_fx);
    CHECK(menu.lines().at(6).value == "OFF");
    go_to(menu, 7);
    (void)menu.press(Key::kChoose);
    CHECK(menu.title() == "MENU");

    // Without a SoundFont there is only the Classic sound.
    Settings modern;
    modern.modern_sound = true;
    Menu without{modern, {.has_modern = false, .screens = {"none"}}};
    go_to(without, 0);
    (void)without.press(Key::kChoose);
    CHECK(without.lines().at(0).value == "CLASSIC");
    CHECK(without.lines().at(0).dim);
    CHECK_FALSE(without.lines().at(3).changes);  // one look only
    (void)without.press(Key::kRight);
    CHECK_FALSE(without.settings().modern_sound);
}

TEST_CASE("menu: an action is bound to the next key pressed, cleared, and the defaults come back") {
    Menu menu{Settings{}, all_choices()};
    go_to(menu, 1);
    (void)menu.press(Key::kChoose);
    CHECK(menu.title() == "KEYBOARD");
    CHECK(menu.lines().size() == input::kActionCount + 2);
    const auto at = [](Action action) { return static_cast<std::size_t>(action); };
    CHECK(menu.lines().at(at(Action::kAbilityF)).label == "ABILITY 1");
    CHECK(menu.lines().at(at(Action::kAbilityF)).value == "SPACE");
    CHECK(menu.lines().at(at(Action::kPreviousViking)).value == "LEFT CTRL");
    Settings many;  // (more than one, from a file written by hand: as many as fit)
    many.controls.keys.at(at(Action::kAbilityF)) = {"F", "Space", "Return", "Keypad +"};
    Menu listed{many, all_choices()};
    go_to(listed, 1);
    (void)listed.press(Key::kChoose);
    CHECK(listed.lines().at(at(Action::kAbilityF)).value == "F, SPACE, RETURN +1");
    go_to(menu, at(Action::kZoomIn));
    (void)menu.press(Key::kChoose);
    CHECK(menu.waiting() == Menu::Waiting::kKey);
    CHECK(menu.note().front() == "PRESS A KEY FOR ZOOM IN");
    CHECK(menu.press(Key::kDown) == Menu::Asked::kNothing);  // (the keys go to the wait)
    CHECK(menu.chosen() == at(Action::kZoomIn));
    menu.take_key("Space");  // its key from now on, in place of the one it had
    CHECK(menu.waiting() == Menu::Waiting::kNothing);
    CHECK(input::key_actions(menu.settings().controls, "Space") == input::bit(Action::kZoomIn));
    CHECK(menu.lines().at(at(Action::kZoomIn)).value == "SPACE");
    CHECK(menu.lines().at(at(Action::kAbilityF)).value == "-");
    CHECK(menu.note().front() == "SPACE NO LONGER DOES ABILITY 1");
    (void)menu.press(Key::kChoose);
    menu.take_key("Escape");
    CHECK(menu.note().front() == "ESCAPE CAN NOT BE CHANGED");
    CHECK(menu.lines().at(at(Action::kZoomIn)).value == "SPACE");
    (void)menu.press(Key::kChoose);
    menu.stop_waiting();
    CHECK(menu.waiting() == Menu::Waiting::kNothing);
    (void)menu.press(Key::kClear);
    CHECK(menu.lines().at(at(Action::kZoomIn)).value == "-");
    CHECK(menu.settings().controls.keys.at(at(Action::kZoomIn)).empty());
    go_to(menu, input::kActionCount);
    CHECK(menu.lines().at(input::kActionCount).label == "DEFAULTS");
    (void)menu.press(Key::kChoose);
    CHECK(menu.settings().controls.keys == input::remaster_controls().keys);
    (void)menu.press(Key::kDown);
    (void)menu.press(Key::kChoose);
    CHECK(menu.title() == "MENU");

    // The gamepad's the same way, by its buttons' names, a button added to the others.
    menu.set_has_pad(true);
    go_to(menu, 2);
    (void)menu.press(Key::kChoose);
    CHECK(menu.lines().at(at(Action::kLeft)).value == "DPAD LEFT, LS LEFT");
    go_to(menu, at(Action::kQuicksave));
    (void)menu.press(Key::kChoose);
    CHECK(menu.waiting() == Menu::Waiting::kPad);
    menu.take_key("Space");  // (a key doesn't end a gamepad's wait)
    CHECK(menu.waiting() == Menu::Waiting::kPad);
    menu.take_pad("righttrigger");
    CHECK(menu.lines().at(at(Action::kQuicksave)).value == "RT");
    (void)menu.press(Key::kChoose);
    menu.take_pad("start");
    CHECK(menu.note().front() == "START CAN NOT BE CHANGED");
    (void)menu.press(Key::kChoose);
    menu.take_pad("a");
    CHECK(menu.note().front() == "A NO LONGER DOES ABILITY 1");
    CHECK(menu.settings().controls.pad.at(at(Action::kQuicksave)) == std::vector<std::string>{"righttrigger", "a"});
}

TEST_CASE("menu: its picture is a box over the screen, the rest clear") {
    Menu menu{Settings{}, all_choices()};
    // A font of solid characters, to see where the text goes: the letter's colour (2) all over.
    const assets::IndexedImage solid{.width = 8, .height = 8, .pixels = std::vector<std::uint8_t>(64, 2)};
    const std::vector<assets::IndexedImage> font(80, solid);
    const std::vector<std::uint8_t> picture = menu_picture(menu, font);
    REQUIRE(picture.size() == std::size_t{kScreenWidth} * kScreenHeight * 4);
    const auto pixel = [&picture](int x, int y) {
        const std::size_t at = ((static_cast<std::size_t>(y) * kScreenWidth) + static_cast<std::size_t>(x)) * 4;
        return std::vector<std::uint8_t>(picture.begin() + static_cast<std::ptrdiff_t>(at),
                                         picture.begin() + static_cast<std::ptrdiff_t>(at + 4));
    };
    CHECK(pixel(0, 0).at(3) == 0);  // clear around it
    CHECK(pixel(kScreenWidth / 2, kScreenHeight / 2).at(3) > 0);
    // The box's frame across its middle: white, the box's dark inside it; its corners are cut off.
    int left = 0;
    while (pixel(left, kScreenHeight / 2).at(3) == 0) {
        ++left;
    }
    CHECK(pixel(left, kScreenHeight / 2) == std::vector<std::uint8_t>{251, 251, 251, 255});
    CHECK(pixel(left + 1, kScreenHeight / 2) == std::vector<std::uint8_t>{4, 4, 4, 235});  // black, hardly see-through
    int top = 0;
    while (pixel(kScreenWidth / 2, top).at(3) == 0) {
        ++top;
    }
    CHECK(pixel(left, top).at(3) == 0);
    CHECK(pixel(left + 1, top + 1).at(3) == 255);  // the cut's edge
    // The chosen line (the first) in white, the others grey.
    bool white = false;
    bool grey = false;
    for (int y = top; y < kScreenHeight - top; ++y) {
        const auto colour = pixel(left + 8, y);
        white = white || colour == std::vector<std::uint8_t>{251, 251, 251, 255};
        grey = grey || colour == std::vector<std::uint8_t>{170, 170, 170, 255};
    }
    CHECK(white);
    CHECK(grey);
    // A long page shows part of itself, and still fits; a note that flashes may be gone.
    go_to(menu, 2);
    (void)menu.press(Key::kChoose);
    CHECK(menu_picture(menu, font, false) != menu_picture(menu, font, true));
    (void)menu.press(Key::kBack);
    go_to(menu, 1);
    (void)menu.press(Key::kChoose);
    go_to(menu, 18);
    CHECK(menu_picture(menu, font).size() == picture.size());
}
