// Tests for src/lvr/menu.*: Remaster rules' menu, its pages and what its keys change, and its picture.
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "lvr/menu.hpp"
#include "lvr/render.hpp"

using namespace lvr;
using Key = Menu::Key;
using input::Action;

namespace {

Menu::Choices all_choices() {
    return {.has_modern = true,
            .screens = {"none", "crt", "xbr"},
            .online = false,
            .hosting = false,
            .lobby = false,
            .passwords = {}};
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
    Menu without{
        modern,
        {.has_modern = false, .screens = {"none"}, .online = false, .hosting = false, .lobby = false, .passwords = {}}};
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

TEST_CASE("menu: HOST GAME and JOIN GAME, from the title: lines typed into, checked, and back closes the menu") {
    Settings settings;
    settings.host_password.clear();
    Menu menu{settings,
              {.has_modern = false,
               .screens = {"none"},
               .online = false,
               .hosting = false,
               .passwords = {"AAAA", "BBBB", "CCCC"}}};
    menu.open_host();
    CHECK(menu.title() == "HOST GAME");
    CHECK(labels(menu) == std::vector<std::string>{"NAME", "PORT", "PASSWORD", "START HOSTING", "BACK"});
    CHECK(menu.lines()[0].value == "PLAYER 1");  // (no name yet: the one the game gives)
    CHECK(menu.lines()[1].value == "47230");
    CHECK(menu.lines()[2].value == "NEW GAME");  // no password: a new game, from the intro

    // The name: typed (letters made capitals, what the font hasn't left out), at most 8; erased; done with Enter.
    CHECK(menu.press(Key::kChoose) == Menu::Asked::kNothing);
    CHECK(menu.waiting() == Menu::Waiting::kText);
    menu.take_text(" red-beard!9");
    CHECK(menu.lines()[0].value == "REDBEARD");
    CHECK(menu.lines()[0].typing);  // (a block after it, where the next letter goes)
    CHECK_FALSE(menu.lines()[1].typing);
    CHECK(menu.press(Key::kClear) == Menu::Asked::kNothing);
    menu.erase();
    CHECK(menu.lines()[0].value == "REDBEA");
    CHECK(menu.note().back() == "BACKSPACE ERASES, ENTER/ESC DONE");
    (void)menu.press(Key::kDown);  // (no other key while typing)
    CHECK(menu.chosen() == 0);
    (void)menu.press(Key::kChoose);
    CHECK(menu.waiting() == Menu::Waiting::kNothing);
    CHECK(menu.settings().name == "REDBEA");

    // The port: digits only, 1 to 65535.
    go_to(menu, 1);
    (void)menu.press(Key::kChoose);
    for (int i = 0; i < 5; ++i) {
        menu.erase();
    }
    menu.take_text("0x");
    menu.stop_waiting();
    CHECK(menu.settings().host_port == 0);
    go_to(menu, 3);
    CHECK(menu.press(Key::kChoose) == Menu::Asked::kNothing);
    CHECK(menu.note().front() == "THE PORT IS 1 TO 65535");
    go_to(menu, 1);
    (void)menu.press(Key::kChoose);
    menu.erase();
    menu.take_text("4000");
    menu.stop_waiting();
    CHECK(menu.settings().host_port == 4000);

    // The password: a level's, or none (a new game: level -1); another, and nothing starts.
    go_to(menu, 3);
    CHECK(menu.press(Key::kChoose) == Menu::Asked::kHost);
    CHECK(menu.level() == -1);
    go_to(menu, 2);
    (void)menu.press(Key::kChoose);
    menu.take_text("x");
    menu.stop_waiting();
    go_to(menu, 3);
    CHECK(menu.press(Key::kChoose) == Menu::Asked::kNothing);
    CHECK(menu.note().front() == "NO LEVEL HAS THAT PASSWORD");
    go_to(menu, 2);
    (void)menu.press(Key::kChoose);
    for (int i = 0; i < 4; ++i) {
        menu.erase();
    }
    menu.take_text("cccc");  // (four at most)
    menu.take_text("D");
    menu.stop_waiting();
    go_to(menu, 3);
    CHECK(menu.press(Key::kChoose) == Menu::Asked::kHost);
    CHECK(menu.level() == 2);
    CHECK(menu.settings().host_password == "CCCC");

    // Back from it: back to the title, the menu closed.
    CHECK(menu.press(Key::kBack) == Menu::Asked::kResume);

    Menu again{menu.settings(), {}};
    again.open_host();
    CHECK(again.chosen() == 3);  // a name there already: START HOSTING at once
    Menu join{menu.settings(), {}};
    join.open_join();
    CHECK(join.chosen() == 1);  // a name, no address yet: ADDRESS
    CHECK(join.title() == "JOIN GAME");
    CHECK(labels(join) == std::vector<std::string>{"NAME", "ADDRESS", "JOIN", "BACK"});
    CHECK(join.lines()[0].value == "REDBEA");  // the same name
    go_to(join, 2);
    CHECK(join.press(Key::kChoose) == Menu::Asked::kNothing);
    CHECK(join.note().front() == "TYPE THE HOST'S IP FIRST");
    go_to(join, 1);
    (void)join.press(Key::kChoose);
    CHECK(join.note().front() == "THE HOST'S IP, AND :PORT IF NOT 47230");
    join.take_text("192.168.100.200:4000 x");  // (21 at most: what the line has room for)
    CHECK(join.lines()[1].value == "192.168.100.200:4000X");
    join.take_text("Y");
    CHECK(join.lines()[1].value == "192.168.100.200:4000X");
    join.erase();
    (void)join.press(Key::kBack);  // (Escape ends the typing; it doesn't go back)
    CHECK(join.waiting() == Menu::Waiting::kNothing);
    CHECK(join.settings().join_address == "192.168.100.200:4000");
    go_to(join, 2);
    CHECK(join.press(Key::kChoose) == Menu::Asked::kJoin);
    Menu filled{join.settings(), {}};
    filled.open_join();
    CHECK(filled.chosen() == 2);  // both: JOIN at once
    go_to(join, 3);
    CHECK(join.press(Key::kChoose) == Menu::Asked::kResume);
    join.tell("THE HOST LEFT");
    CHECK(join.note().front() == "THE HOST LEFT");
}

TEST_CASE("menu: the lobby: the host's password and START GAME, a client's waiting; RETURN TO LOBBY in a game") {
    const Menu::Choices choices{.has_modern = false,
                                .screens = {"none"},
                                .online = false,
                                .hosting = false,
                                .lobby = false,
                                .passwords = {"AAAA", "BBBB"}};
    Menu host{Settings{}, choices};
    host.open_lobby(true);
    CHECK(host.in_lobby());
    CHECK(host.title() == "LOBBY");
    CHECK(labels(host) == std::vector<std::string>{"PASSWORD", "START GAME", "LEAVE"});
    CHECK(host.lines()[0].value == "NONE");
    CHECK(host.level_typed() == std::optional<int>{-1});
    // A password typed: the level it names, as it is typed (the front end tells the others).
    (void)host.press(Key::kChoose);
    host.take_text("bbb");
    CHECK_FALSE(host.level_typed().has_value());
    host.take_text("b");
    CHECK(host.level_typed() == std::optional<int>{1});
    host.stop_waiting();
    go_to(host, 1);
    CHECK(host.press(Key::kChoose) == Menu::Asked::kStart);
    CHECK(host.level() == 1);
    // What it starts with, and who is there, under its lines.
    CHECK_FALSE(host.shows_players());
    host.set_players(
        {{.name = "HOST", .network = "-", .engine = "-"}, {.name = "GUEST", .network = "20 MS", .engine = "36 MS"}});
    host.set_start("LEVEL 2, BBBB");
    CHECK(host.shows_players());
    const std::vector<std::uint8_t> picture = menu_picture(host, {});
    CHECK(picture.size() == std::size_t{kScreenWidth} * kScreenHeight * 4);
    // Back leaves it.
    CHECK(host.press(Key::kBack) == Menu::Asked::kLeave);

    Menu client{Settings{}, choices};
    client.open_lobby(false);
    CHECK(labels(client) == std::vector<std::string>{"LEAVE"});
    CHECK(client.note().front() == "WAITING FOR THE HOST TO START");
    CHECK(client.press(Key::kChoose) == Menu::Asked::kLeave);

    // In a game online, the host's first page goes back to the lobby; a client's has no such line.
    Menu playing{
        Settings{},
        {.has_modern = false, .screens = {"none"}, .online = true, .hosting = true, .lobby = true, .passwords = {}}};
    CHECK(labels(playing) ==
          std::vector<std::string>{"OPTIONS", "KEYBOARD", "GAMEPAD", "RETURN TO LOBBY", "GIVE UP LEVEL", "QUIT GAME"});
    go_to(playing, 3);
    CHECK(playing.press(Key::kChoose) == Menu::Asked::kLobby);
    Menu guest{
        Settings{},
        {.has_modern = false, .screens = {"none"}, .online = true, .hosting = false, .lobby = false, .passwords = {}}};
    CHECK(labels(guest) ==
          std::vector<std::string>{"OPTIONS", "KEYBOARD", "GAMEPAD", "QUIT GAME"});  // (the host drives)
}
