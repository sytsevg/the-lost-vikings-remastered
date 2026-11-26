#include "lvr/menu.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <format>
#include <string>
#include <utility>

#include "lvr/render.hpp"
#include "lvr/session.hpp"

namespace lvr {
namespace {

using input::Action;

// (No line to resume: Escape, B or Start does that, the user.)
constexpr std::array<std::string_view, 6> kFirstLines{"OPTIONS",         "KEYBOARD",      "GAMEPAD",
                                                      "RETURN TO LOBBY", "GIVE UP LEVEL", "QUIT GAME"};
// HOST GAME's lines and JOIN GAME's.
enum class Host : std::uint8_t { kName, kPort, kStart, kBack };  // (the password: the lobby's)
constexpr std::size_t kHostLines = 5;
enum class Join : std::uint8_t { kName, kAddress, kJoin, kBack };
constexpr std::size_t kJoinLines = 4;
// What can be typed into them: a name as the game shows it (net::shown_name: letters, digits and spaces, at most 8),
// a port's digits, a password's four letters and digits (the title's), an address and its port.
constexpr std::size_t kMostName = 8;
constexpr std::size_t kMostPort = 5;
constexpr std::size_t kMostPassword = 4;
constexpr int kMostPortNumber = 65535;
enum class Option : std::uint8_t { kSound, kMusic, kEffects, kScreen, kZoom, kMinimap, kLights, kBack };
constexpr std::size_t kOptions = 8;
constexpr std::array<std::string_view, Session::kZoomSteps> kZoomNames{"1X", "1.5X", "2X"};
constexpr std::size_t kControlsAfter = 2;  // after the actions: the defaults, and back

// The box's text, in the font's 8 x 8 characters: an action's name in this many, its keys beside it in the rest.
constexpr int kCharacter = 8;
constexpr int kLabelColumns = 15;
constexpr int kValueColumns = 21;
constexpr int kColumns = kLabelColumns + 1 + kValueColumns;
constexpr int kMostLines = 15;         // a longer page shows this many around the chosen line
constexpr int kRoundTripColumns = 10;  // the players' network round trip, before the game's

std::string upper(std::string_view text) {
    std::string out{text};
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return out;
}

std::string_view on_off(bool on) {
    return on ? "ON" : "OFF";
}

// A key's name as the menu shows it: in capitals, and a sign's spelled out (the font's - sits low, as a _ would).
std::string key_label(std::string_view key) {
    constexpr std::array<std::pair<std::string_view, std::string_view>, 9> kSigns{{
        {"-", "MINUS"},
        {"=", "EQUALS"},
        {",", "COMMA"},
        {".", "PERIOD"},
        {"/", "SLASH"},
        {"\\", "BACKSLASH"},
        {";", "SEMICOLON"},
        {"'", "QUOTE"},
        {"`", "GRAVE"},
    }};
    for (const auto& [sign, name] : kSigns) {
        if (key == sign) {
            return std::string{name};
        }
    }
    return upper(key);
}

// The names of what does an action, as many as fit, and how many more there are.
std::string listed(const std::vector<std::string>& bound, bool pad) {
    if (bound.empty()) {
        return "-";
    }
    std::string text;
    for (std::size_t i = 0; i < bound.size(); ++i) {
        const std::string name = pad ? std::string{input::pad_label(bound.at(i))} : key_label(bound.at(i));
        const std::string next = text.empty() ? name : std::format("{}, {}", text, name);
        const bool more = i + 1 < bound.size();
        if (std::cmp_greater(next.size(), more ? kValueColumns - 3 : kValueColumns)) {  // (room for " +N")
            return text.empty() ? next.substr(0, kValueColumns) : std::format("{} +{}", text, bound.size() - i);
        }
        text = next;
    }
    return text;
}

}  // namespace

Menu::Menu(Settings settings, Choices choices) : settings_{std::move(settings)}, choices_{std::move(choices)} {
    if (choices_.screens.empty()) {
        choices_.screens.emplace_back("none");
    }
    port_ = std::to_string(settings_.host_port);
}

std::vector<Menu::First> Menu::first_lines() const {
    std::vector<First> lines{First::kOptions, First::kKeyboard, First::kGamepad};
    if (from_title_) {  // (no level to give up, and the title has QUIT TO DOS of its own)
        return lines;
    }
    if (choices_.online && choices_.hosting && choices_.lobby) {
        lines.push_back(First::kLobby);
    }
    if (!choices_.online || choices_.hosting) {  // (only the host drives the game: its pause box is his)
        lines.push_back(First::kGiveUp);
    }
    lines.push_back(First::kQuit);
    return lines;
}

Menu::Field Menu::field_of(std::size_t line) const noexcept {
    if (page_ == Page::kHost) {
        switch (static_cast<Host>(line)) {
            case Host::kName:
                return Field::kName;
            case Host::kPort:
                return Field::kPort;
            case Host::kStart:
            case Host::kBack:
                break;
        }
    } else if (page_ == Page::kJoin) {
        switch (static_cast<Join>(line)) {
            case Join::kName:
                return Field::kName;
            case Join::kAddress:
                return Field::kAddress;
            case Join::kJoin:
            case Join::kBack:
                break;
        }
    } else if (page_ == Page::kLobby && lobby_host_ && line == 0) {
        return Field::kPassword;
    }
    return Field::kNone;
}

std::string* Menu::text_of(std::size_t line) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): the const one's answer, for this one
    return const_cast<std::string*>(std::as_const(*this).text_of(line));
}

const std::string* Menu::text_of(std::size_t line) const {
    switch (field_of(line)) {
        case Field::kName:
            return &settings_.name;
        case Field::kPort:
            return &port_;
        case Field::kPassword:
            return &settings_.host_password;
        case Field::kAddress:
            return &settings_.join_address;
        case Field::kNone:
            break;
    }
    return nullptr;
}

std::optional<int> Menu::level_typed() const {
    if (settings_.host_password.empty()) {
        return -1;  // a new game
    }
    const auto level = std::ranges::find(choices_.passwords, settings_.host_password);
    return level != choices_.passwords.end() ? std::optional<int>{static_cast<int>(level - choices_.passwords.begin())}
                                             : std::nullopt;
}

std::size_t Menu::line_count() const {
    switch (page_) {
        case Page::kOptions:
            return kOptions;
        case Page::kKeyboard:
        case Page::kGamepad:
            return input::kActionCount + kControlsAfter;
        case Page::kHost:
            return kHostLines;
        case Page::kJoin:
            return kJoinLines;
        case Page::kLobby:
            return lobby_host_ ? 3 : 1;
        case Page::kFirst:
            break;
    }
    return first_lines().size();
}

void Menu::open_host() {
    page_ = Page::kHost;
    waiting_ = Waiting::kNothing;
    // (What is filled in already: on to START HOSTING at once.)
    chosen_.at(static_cast<std::size_t>(Page::kHost)) =
        static_cast<std::size_t>(settings_.name.empty() ? Host::kName : Host::kStart);
}

void Menu::open_options() {
    page_ = Page::kFirst;
    from_title_ = true;
    waiting_ = Waiting::kNothing;
    chosen_.at(static_cast<std::size_t>(Page::kFirst)) = 0;
}

void Menu::open_join() {
    page_ = Page::kJoin;
    waiting_ = Waiting::kNothing;
    chosen_.at(static_cast<std::size_t>(Page::kJoin)) = static_cast<std::size_t>(
        settings_.join_address.empty() ? (settings_.name.empty() ? Join::kName : Join::kAddress) : Join::kJoin);
}

void Menu::open_lobby(bool hosting) {
    page_ = Page::kLobby;
    lobby_host_ = hosting;
    waiting_ = Waiting::kNothing;
    chosen_.at(static_cast<std::size_t>(Page::kLobby)) = 0;
}

std::string_view Menu::title() const noexcept {
    switch (page_) {
        case Page::kOptions:
            return "OPTIONS";
        case Page::kKeyboard:
            return "KEYBOARD";
        case Page::kGamepad:
            return "GAMEPAD";
        case Page::kHost:
            return "HOST GAME";
        case Page::kJoin:
            return "JOIN GAME";
        case Page::kLobby:
            if (countdown_ > 0) {
                return title_;
            }
            return "LOBBY";
        case Page::kFirst:
            break;
    }
    return "MENU";
}

std::vector<Menu::Line> Menu::lines() const {
    std::vector<Line> lines;
    // A line: a choice alone, or with a value that left and right change.
    const auto line = [&lines](std::string_view label, std::string value = {}, bool changes = false) {
        Line& made = lines.emplace_back();
        made.label = label;
        made.value = std::move(value);
        made.changes = changes;
    };
    // A line typed into: its text (while it is typed into, as it is: the picture marks where the next letter goes).
    const auto typed = [this](std::size_t at, std::string_view none = "-") {
        const std::string& text = *text_of(at);
        return text.empty() && !(waiting_ == Waiting::kText && chosen() == at) ? std::string{none} : text;
    };
    switch (page_) {
        case Page::kFirst:
            for (const First first : first_lines()) {
                line(kFirstLines.at(static_cast<std::size_t>(first)));
            }
            break;
        case Page::kLobby:
            if (lobby_host_) {
                line("PASSWORD", typed(0, "NONE"));  // (what it starts with: under the lines)
                line(countdown_ > 0 ? "CANCEL START" : "START GAME");
            }
            line("LEAVE");
            break;
        case Page::kHost:
            line("NAME", typed(static_cast<std::size_t>(Host::kName), "PLAYER 1"));
            line("PORT", typed(static_cast<std::size_t>(Host::kPort)));
            line("START HOSTING");
            line("BACK");
            break;
        case Page::kJoin:
            line("NAME", typed(static_cast<std::size_t>(Join::kName), "PLAYER 2"));
            line("ADDRESS", typed(static_cast<std::size_t>(Join::kAddress)));
            line("JOIN", join_said_.substr(0, kValueColumns));
            if (join_said_.empty()) {
                lines.back().turning = joining_;
                lines.back().dim = joining_.has_value();  // (greyed while it joins)
            }
            line("BACK");
            break;
        case Page::kOptions: {
            const auto screen = settings_.screen == "none" ? std::string{"OFF"} : upper(settings_.screen);
            const auto zoom = static_cast<std::size_t>(std::clamp(settings_.zoom, 0, Session::kZoomSteps - 1));
            line("SOUND", settings_.modern_sound && choices_.has_modern ? "MODERN" : "CLASSIC", choices_.has_modern);
            lines.back().dim = !choices_.has_modern;  // (no SoundFont)
            line("MUSIC", {}, true);
            lines.back().slider = settings_.music;
            line("SOUND FX", {}, true);
            lines.back().slider = settings_.sound_fx;
            line("SCREEN", screen, choices_.screens.size() > 1);
            line("ZOOM", std::string{kZoomNames.at(zoom)}, true);
            line("MINIMAP", std::string{on_off(settings_.minimap)}, true);
            line("VISUAL FX", std::string{on_off(settings_.visual_fx)}, true);
            line("BACK");
            break;
        }
        case Page::kKeyboard:
        case Page::kGamepad: {
            const bool pad = page_ == Page::kGamepad;
            for (std::size_t action = 0; action < input::kActionCount; ++action) {
                line(input::label_of(static_cast<Action>(action)),
                     listed(pad ? settings_.controls.pad.at(action) : settings_.controls.keys.at(action), pad));
                lines.back().dim = pad && !has_pad_;  // (nothing to bind with)
            }
            line("DEFAULTS");
            lines.back().dim = pad && !has_pad_;
            line("BACK");
            break;
        }
    }
    if (waiting_ == Waiting::kText && chosen() < lines.size()) {
        lines.at(chosen()).typing = true;
    }
    return lines;
}

std::vector<std::string> Menu::note() const {
    const auto action = [this] { return input::label_of(static_cast<Action>(chosen())); };
    if (waiting_ == Waiting::kKey) {
        return {std::format("PRESS A KEY FOR {}", action()), "ESC LEAVES IT AS IT IS"};
    }
    if (waiting_ == Waiting::kPad) {
        return {std::format("PRESS A BUTTON FOR {}", action()), "START LEAVES IT AS IT IS"};
    }
    if (waiting_ == Waiting::kText) {
        const Field field = field_of(chosen());
        return {field == Field::kAddress    ? "THE HOST'S IP, AND :PORT IF NOT 47230"
                : field == Field::kPassword ? "A LEVEL'S, OR NONE: A NEW GAME"
                                            : "TYPE IT IN",
                "BACKSPACE ERASES, ENTER/ESC DONE"};
    }
    std::vector<std::string> note;
    if (!said_.empty()) {
        note.push_back(said_);
    } else if (page_ == Page::kGamepad) {
        note.emplace_back(has_pad_ ? "GAMEPAD FOUND" : "NO GAMEPAD CONNECTED");
    }
    switch (page_) {
        case Page::kFirst:
            note.emplace_back(from_title_ ? "ENTER/A CHOOSES, ESC/B BACK" : "ENTER/A CHOOSES, ESC/B RESUMES");
            break;
        case Page::kOptions:
            note.emplace_back("LEFT/RIGHT CHANGE, ESC/B BACK");
            break;
        case Page::kKeyboard:
            note.emplace_back("ENTER/A SET, DEL/Y CLEAR, ESC/B BACK");
            break;
        case Page::kGamepad:
            note.emplace_back("ENTER/A ADD, DEL/Y CLEAR, ESC/B BACK");
            break;
        case Page::kHost:
        case Page::kJoin:
            note.emplace_back("ENTER/A TYPES OR CHOOSES, ESC/B BACK");
            break;
        case Page::kLobby:
            if (!lobby_host_ && said_.empty()) {
                note.emplace_back("WAITING FOR THE HOST TO START");
            }
            note.emplace_back(lobby_host_ ? "ENTER/A CHOOSES, ESC/B LEAVES" : "ESC/B LEAVES");
            break;
    }
    return note;
}

bool Menu::note_flashes() const noexcept {
    return page_ == Page::kGamepad && !has_pad_ && said_.empty() && waiting_ == Waiting::kNothing;
}

void Menu::set_has_pad(bool has_pad) noexcept {
    has_pad_ = has_pad;
    if (!has_pad && waiting_ == Waiting::kPad) {  // (the one to bind with is gone)
        waiting_ = Waiting::kNothing;
    }
}

Menu::Asked Menu::press(Key key) {
    if (waiting_ == Waiting::kText) {  // (a gamepad's A or B ends it too)
        if (key == Key::kChoose || key == Key::kBack) {
            stop_waiting();
        } else if (key == Key::kClear) {
            erase();
        }
        return Asked::kNothing;
    }
    if (waiting_ != Waiting::kNothing) {
        return Asked::kNothing;
    }
    said_.clear();
    std::size_t& chosen = chosen_.at(static_cast<std::size_t>(page_));
    const std::size_t count = line_count();
    switch (key) {
        case Key::kUp:
            chosen = (chosen + count - 1) % count;
            break;
        case Key::kDown:
            chosen = (chosen + 1) % count;
            break;
        case Key::kLeft:
        case Key::kRight:
            if (page_ == Page::kOptions) {
                change(key == Key::kLeft ? -1 : 1);
            }
            break;
        case Key::kChoose:
            return choose();
        case Key::kBack:
            if (page_ == Page::kLobby) {
                return Asked::kLeave;
            }
            if (page_ == Page::kFirst || page_ == Page::kHost || page_ == Page::kJoin) {
                return Asked::kResume;  // (HOST GAME and JOIN GAME: back to the title they were opened from)
            }
            page_ = Page::kFirst;
            break;
        case Key::kClear:
            if ((page_ == Page::kKeyboard || (page_ == Page::kGamepad && has_pad_)) && chosen < input::kActionCount) {
                (page_ == Page::kGamepad ? settings_.controls.pad : settings_.controls.keys).at(chosen).clear();
            }
            break;
    }
    return Asked::kNothing;
}

Menu::Asked Menu::choose() {
    const std::size_t chosen = this->chosen();
    switch (page_) {
        case Page::kFirst:
            switch (first_lines().at(chosen)) {
                case First::kOptions:
                    page_ = Page::kOptions;
                    break;
                case First::kKeyboard:
                    page_ = Page::kKeyboard;
                    break;
                case First::kGamepad:
                    page_ = Page::kGamepad;
                    break;
                case First::kLobby:
                    return Asked::kLobby;
                case First::kGiveUp:
                    return Asked::kGiveUp;
                case First::kQuit:
                    return Asked::kQuit;
            }
            break;
        case Page::kLobby:
            if (text_of(chosen) != nullptr) {
                waiting_ = Waiting::kText;
            } else if (lobby_host_ && chosen == 1 && countdown_ > 0) {
                return Asked::kCancel;
            } else if (lobby_host_ && chosen == 1) {  // START GAME
                if (const std::optional<int> level = level_typed()) {
                    level_ = *level;
                    return Asked::kStart;
                }
                said_ = "NO LEVEL HAS THAT PASSWORD";
            } else {
                return Asked::kLeave;
            }
            break;
        case Page::kOptions:
            if (chosen == static_cast<std::size_t>(Option::kBack)) {
                page_ = Page::kFirst;
            } else {
                change(1);
            }
            break;
        case Page::kHost:
        case Page::kJoin: {
            if (text_of(chosen) != nullptr) {
                waiting_ = Waiting::kText;
                break;
            }
            const bool host = page_ == Page::kHost;
            if (chosen == (host ? static_cast<std::size_t>(Host::kBack) : static_cast<std::size_t>(Join::kBack))) {
                return Asked::kResume;
            }
            if (host) {  // (the lobby starts with the password kept from before: the lobby's page changes it)
                if (settings_.host_port < 1 || settings_.host_port > kMostPortNumber) {
                    said_ = "THE PORT IS 1 TO 65535";
                    break;
                }
                level_ = level_typed().value_or(-1);  // (one naming no level: a new game)
                return Asked::kHost;
            }
            if (settings_.join_address.empty()) {
                said_ = "TYPE THE HOST'S IP FIRST";
                break;
            }
            return Asked::kJoin;
        }
        case Page::kKeyboard:
        case Page::kGamepad: {
            const bool pad = page_ == Page::kGamepad;
            if (pad && !has_pad_ && chosen <= input::kActionCount) {
                break;  // (without a gamepad its buttons are left as they are)
            }
            if (chosen < input::kActionCount) {
                waiting_ = pad ? Waiting::kPad : Waiting::kKey;
            } else if (chosen == input::kActionCount) {  // the defaults
                const input::Controls defaults = input::remaster_controls();
                (pad ? settings_.controls.pad : settings_.controls.keys) = pad ? defaults.pad : defaults.keys;
            } else {
                page_ = Page::kFirst;
            }
            break;
        }
    }
    return Asked::kNothing;
}

void Menu::change(int by) {
    switch (static_cast<Option>(chosen())) {
        case Option::kSound:
            settings_.modern_sound = choices_.has_modern && !settings_.modern_sound;
            break;
        case Option::kMusic:
            settings_.music = std::clamp(settings_.music + by, 0, kLoudest);
            break;
        case Option::kEffects:
            settings_.sound_fx = std::clamp(settings_.sound_fx + by, 0, kLoudest);
            break;
        case Option::kScreen: {
            const auto found = std::ranges::find(choices_.screens, settings_.screen);
            const auto at = found == choices_.screens.end() ? 0 : found - choices_.screens.begin();
            const auto count = static_cast<std::ptrdiff_t>(choices_.screens.size());
            settings_.screen = choices_.screens.at(static_cast<std::size_t>((at + by + count) % count));
            break;
        }
        case Option::kZoom:
            settings_.zoom = std::clamp(settings_.zoom + by, 0, Session::kZoomSteps - 1);
            break;
        case Option::kMinimap:
            settings_.minimap = !settings_.minimap;
            break;
        case Option::kLights:
            settings_.visual_fx = !settings_.visual_fx;
            break;
        case Option::kBack:
            break;
    }
}

void Menu::take_key(std::string_view key) {
    if (waiting_ != Waiting::kKey) {
        return;
    }
    waiting_ = Waiting::kNothing;
    const auto action = static_cast<Action>(chosen());
    const input::Actions had = input::key_actions(settings_.controls, key) & ~input::bit(action);
    if (!input::bind_key(settings_.controls, action, key)) {
        said_ = std::format("{} CAN NOT BE CHANGED", key_label(key));
        return;
    }
    for (std::size_t other = 0; other < input::kActionCount; ++other) {
        if ((had & input::bit(static_cast<Action>(other))) != 0) {
            said_ = std::format("{} NO LONGER DOES {}", key_label(key), input::label_of(static_cast<Action>(other)));
        }
    }
}

void Menu::take_pad(std::string_view input) {
    if (waiting_ != Waiting::kPad) {
        return;
    }
    waiting_ = Waiting::kNothing;
    const auto action = static_cast<Action>(chosen());
    const input::Actions had = input::pad_actions(settings_.controls, input) & ~input::bit(action);
    if (!input::bind_pad(settings_.controls, action, input)) {
        said_ = std::format("{} CAN NOT BE CHANGED", input::pad_label(input));
        return;
    }
    for (std::size_t other = 0; other < input::kActionCount; ++other) {
        if ((had & input::bit(static_cast<Action>(other))) != 0) {
            said_ = std::format("{} NO LONGER DOES {}", input::pad_label(input),
                                input::label_of(static_cast<Action>(other)));
        }
    }
}

void Menu::stop_waiting() noexcept {
    waiting_ = Waiting::kNothing;
}

void Menu::take_text(std::string_view typed) {
    std::string* text = waiting_ == Waiting::kText ? text_of(chosen()) : nullptr;
    if (text == nullptr) {
        return;
    }
    const Field field = field_of(chosen());
    const bool name = field == Field::kName;
    const bool port = field == Field::kPort;
    const bool password = field == Field::kPassword;
    const std::size_t most = name       ? kMostName
                             : port     ? kMostPort
                             : password ? kMostPassword
                                        : static_cast<std::size_t>(kValueColumns);  // (the block after it: the margin)
    for (char each : typed) {
        each = each >= 'a' && each <= 'z' ? static_cast<char>(each - 'a' + 'A') : each;
        const bool letter = each >= 'A' && each <= 'Z';
        const bool digit = each >= '0' && each <= '9';
        const bool fits = name       ? letter || digit || (each == ' ' && !text->empty())
                          : port     ? digit
                          : password ? letter || digit || each == '!'
                                     : letter || digit || each == '.' || each == ':' || each == '-';
        if (fits && text->size() < most) {
            text->push_back(each);
        }
    }
    if (port) {
        settings_.host_port = port_.empty() ? 0 : std::stoi(port_);
    }
}

void Menu::erase() {
    std::string* text = waiting_ == Waiting::kText ? text_of(chosen()) : nullptr;
    if (text == nullptr || text->empty()) {
        return;
    }
    text->pop_back();
    if (field_of(chosen()) == Field::kPort) {
        settings_.host_port = port_.empty() ? 0 : std::stoi(port_);
    }
}

namespace {

// The colours, as the rewind's bar's (gpu_gauge.frag): the game's white, a light grey, and the dark it is on.
struct Colour {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;
};
constexpr std::uint8_t kSeeThrough = 235;  // 92%: the box (the text and the frame are solid)
constexpr Colour kWhite{251, 251, 251, 255};
constexpr Colour kGrey{170, 170, 170, 255};
constexpr Colour kDimmed{96, 96, 96, 255};
constexpr Colour kDark{4, 4, 4, kSeeThrough};           // black (the user: not the bar's blue-black)
constexpr Colour kChosenBand{40, 40, 40, kSeeThrough};  // behind the chosen line

class Canvas {
  public:
    Canvas() : pixels_(std::size_t{kScreenWidth} * kScreenHeight * 4) {}
    void put(int x, int y, Colour c) {
        if (x < 0 || y < 0 || x >= kScreenWidth || y >= kScreenHeight) {
            return;
        }
        const std::size_t at = ((static_cast<std::size_t>(y) * kScreenWidth) + static_cast<std::size_t>(x)) * 4;
        pixels_.at(at) = c.r;
        pixels_.at(at + 1) = c.g;
        pixels_.at(at + 2) = c.b;
        pixels_.at(at + 3) = c.a;
    }
    void fill(int x, int y, int width, int height, Colour c) {
        for (int row = y; row < y + height; ++row) {
            for (int column = x; column < x + width; ++column) {
                put(column, row, c);
            }
        }
    }
    // A box with a frame a pixel wide and two pixels cut off each corner, as the rewind's bar's.
    void framed(int x, int y, int width, int height, Colour inside, Colour frame) {
        for (int row = 0; row < height; ++row) {
            for (int column = 0; column < width; ++column) {
                const int across = std::min(column, width - 1 - column);
                const int down = std::min(row, height - 1 - row);
                if (across + down < 2) {
                    continue;  // cut off
                }
                const bool edge = across == 0 || down == 0 || across + down == 2;
                put(x + column, y + row, edge ? frame : inside);
            }
        }
    }
    // A text in the font: a character's letter in `c`, its shadow dark, and nothing of the box it is drawn on (the
    // font's three colours, as the text boxes give them their own).
    void text(int x, int y, std::string_view text, Colour c, const std::vector<assets::IndexedImage>& font) {
        constexpr std::uint8_t kShadow = 1;
        constexpr std::uint8_t kLetter = 2;
        constexpr Colour kShadowColour{kDark.r, kDark.g, kDark.b, 255};
        for (const char each : text) {
            const auto character = static_cast<std::uint8_t>(each);
            if (character >= kFirstGlyph && std::size_t{character} - kFirstGlyph < font.size()) {
                const assets::IndexedImage& glyph = font.at(std::size_t{character} - kFirstGlyph);
                for (std::size_t gy = 0; gy < glyph.height; ++gy) {
                    for (std::size_t gx = 0; gx < glyph.width; ++gx) {
                        if (const std::uint8_t colour = glyph.at(gx, gy); colour == kLetter || colour == kShadow) {
                            put(x + static_cast<int>(gx), y + static_cast<int>(gy),
                                colour == kLetter ? c : kShadowColour);
                        }
                    }
                }
            }
            x += kCharacter;
        }
    }
    // A small triangle, its point at (x, y): pointing left, right, up or down, 3 pixels long.
    enum class Pointing : std::uint8_t { kLeft, kRight, kUp, kDown };
    void arrow(int x, int y, Pointing pointing, Colour c) {
        for (int step = 0; step < 3; ++step) {
            for (int side = -step; side <= step; ++side) {
                switch (pointing) {
                    case Pointing::kLeft:
                        put(x + step, y + side, c);
                        break;
                    case Pointing::kRight:
                        put(x - step, y + side, c);
                        break;
                    case Pointing::kUp:
                        put(x + side, y + step, c);
                        break;
                    case Pointing::kDown:
                        put(x + side, y - step, c);
                        break;
                }
            }
        }
    }
    std::vector<std::uint8_t> done() { return std::move(pixels_); }

  private:
    std::vector<std::uint8_t> pixels_;
};

}  // namespace

std::vector<std::uint8_t> coop_picture(const CoopOverlay& overlay, const std::vector<assets::IndexedImage>& font) {
    Canvas canvas;
    if (!overlay.waiting.empty()) {
        constexpr int kPadding = 6;
        const int width = (static_cast<int>(overlay.waiting.size()) * kCharacter) + (2 * kPadding);
        const int height = kCharacter + (2 * kPadding) - 2;
        const int x = (kScreenWidth - width) / 2;
        constexpr int kTop = 8;
        canvas.framed(x, kTop, width, height, kDark, kWhite);
        canvas.text(x + kPadding, kTop + kPadding - 1, overlay.waiting, kWhite, font);
    }
    return canvas.done();
}

std::vector<std::uint8_t> menu_picture(const Menu& menu, const std::vector<assets::IndexedImage>& font,
                                       bool flash_shown) {
    constexpr int kPadding = 8;  // inside the frame, around the text
    constexpr int kLine = 10;    // a line of text with a pixel above and below it
    constexpr int kNoteLine = 9;
    const std::vector<Menu::Line> lines = menu.lines();
    const std::vector<std::string> note = menu.note();
    const int shown = std::min(static_cast<int>(lines.size()), kMostLines);
    const int width = (kColumns * kCharacter) + (2 * kPadding);
    // Online, under the first page's lines: a divider, and each player's name and round trip.
    const int players = menu.shows_players() ? static_cast<int>(menu.players().size()) : 0;
    // (The lobby's: what the game starts with, a line above them.)
    const bool start_row = players > 0 && menu.in_lobby() && !menu.start().empty();
    const int players_height = players > 0 ? 6 + ((players + 1 + (start_row ? 1 : 0)) * kLine) : 0;
    const int content =
        6 + kCharacter + 6 + (shown * kLine) + players_height + 6 + (static_cast<int>(note.size()) * kNoteLine) + 4;
    const int x = (kScreenWidth - width) / 2;
    // The title's pages: under its logo, nothing of the title's own menu showing through (the user: a menu of its
    // own, not one laid over the game): as far down as that menu goes at least (its six rows to row 21, the pointer
    // beside them), the notes at the bottom.
    constexpr int kUnderLogo = 87 - kTitleLower;  // (the logo's last row: 86, raised for the six choices)
    constexpr int kTitleMenuEnd = 179;
    const bool of_title = menu.of_title();
    const int height = of_title ? std::max(content, kTitleMenuEnd - kUnderLogo) : content;
    const int y = of_title ? std::min(kUnderLogo, kScreenHeight - height - 2) : (kScreenHeight - height) / 2;
    const Colour inside = of_title ? Colour{kDark.r, kDark.g, kDark.b, 255} : kDark;
    const Colour band = of_title ? Colour{kChosenBand.r, kChosenBand.g, kChosenBand.b, 255} : kChosenBand;
    Canvas canvas;
    canvas.framed(x, y, width, height, inside, kWhite);
    const int text_x = x + kPadding;
    const std::string_view title = menu.title();
    canvas.text(x + ((width - (static_cast<int>(title.size()) * kCharacter)) / 2), y + 6, title, kWhite, font);
    canvas.fill(text_x, y + 6 + kCharacter + 2, kColumns * kCharacter, 1, kDimmed);
    // The lines around the chosen one, as many as the box holds.
    const int chosen = static_cast<int>(menu.chosen());
    const int first = std::clamp(chosen - (shown / 2), 0, static_cast<int>(lines.size()) - shown);
    const int lines_y = y + 6 + kCharacter + 6;
    for (int i = 0; i < shown; ++i) {
        const Menu::Line& line = lines.at(static_cast<std::size_t>(first) + static_cast<std::size_t>(i));
        const bool is_chosen = first + i == chosen;
        const int top = lines_y + (i * kLine);
        if (is_chosen) {
            canvas.fill(text_x - 4, top, (kColumns * kCharacter) + 8, kLine, band);
        }
        const Colour ink = line.dim ? kDimmed : is_chosen ? kWhite : kGrey;
        canvas.text(text_x, top + 1, line.label, ink, font);
        int value_x = text_x + ((kLabelColumns + 1) * kCharacter);
        if (line.changes && is_chosen) {
            canvas.arrow(value_x - 7, top + 4, Canvas::Pointing::kLeft, ink);
        }
        if (line.slider) {
            // A volume: a bar of kLoudest steps in the rewind bar's look, and its number after it.
            constexpr int kStep = 5;
            const int bar_width = (kLoudest * kStep) + 3;
            canvas.framed(value_x, top + 1, bar_width, 8, kDark, ink);
            for (int step = 0; step < *line.slider; ++step) {
                canvas.fill(value_x + 2 + (step * kStep), top + 3, kStep - 1, 4, ink);
            }
            value_x += bar_width + 4;
            canvas.text(value_x, top + 1, std::format("{}", *line.slider), ink, font);
            value_x += 2 * kCharacter;
        } else if (!line.value.empty()) {
            canvas.text(value_x, top + 1, line.value, ink, font);
            value_x += static_cast<int>(line.value.size()) * kCharacter;
        } else if (line.turning) {  // a short line turning about its middle: | / - \ (the font has no \ nor |)
            constexpr int kHalf = 3;
            const int middle_x = value_x + kHalf;
            const int middle_y = top + 1 + kHalf;
            for (int along = -kHalf; along <= kHalf; ++along) {
                const std::array<std::pair<int, int>, 4> at{
                    {{0, along}, {-along, along}, {along, 0}, {along, along}}};  // (| / - \, y downwards)
                const auto [dx, dy] = at.at(static_cast<std::size_t>(*line.turning % 4));
                canvas.fill(middle_x + dx, middle_y + dy, 1, 1, ink);
            }
        }
        if (line.typing && flash_shown) {  // where the next letter goes: a block, coming and going
            canvas.fill(value_x, top + 1, kCharacter - 2, kCharacter - 1, ink);
        }
        if (line.changes && is_chosen) {
            canvas.arrow(value_x + 6, top + 4, Canvas::Pointing::kRight, ink);
        }
    }
    if (first > 0) {  // more above, more below
        canvas.arrow(x + width - 6, lines_y + 1, Canvas::Pointing::kUp, kGrey);
    }
    if (first + shown < static_cast<int>(lines.size())) {
        canvas.arrow(x + width - 6, lines_y + (shown * kLine) - 2, Canvas::Pointing::kDown, kGrey);
    }
    if (players > 0) {
        int divider_y = lines_y + (shown * kLine) + 3;
        canvas.fill(text_x, divider_y, kColumns * kCharacter, 1, kDimmed);
        if (start_row) {
            canvas.text(text_x, divider_y + 4, "STARTS WITH", kDimmed, font);
            canvas.text(text_x + ((kLabelColumns + 1) * kCharacter), divider_y + 4, menu.start(), kGrey, font);
            divider_y += kLine;
        }
        // Two columns of round trips: the network's, and the game's (the network's and both games' rhythm).
        const int network_x = text_x + ((kLabelColumns + 1) * kCharacter);
        const int engine_x = network_x + (kRoundTripColumns * kCharacter);
        canvas.text(network_x, divider_y + 4, "NETWORK", kDimmed, font);
        canvas.text(engine_x, divider_y + 4, "GAME", kDimmed, font);
        for (int i = 0; i < players; ++i) {
            const Menu::PlayerLine& player = menu.players().at(static_cast<std::size_t>(i));
            const int top = divider_y + 3 + ((i + 1) * kLine);
            canvas.text(text_x, top + 1, player.name, kGrey, font);
            canvas.text(network_x, top + 1, player.network, kGrey, font);
            canvas.text(engine_x, top + 1, player.engine, kGrey, font);
        }
    }
    const int note_y = y + height - 4 - (static_cast<int>(note.size()) * kNoteLine);
    for (std::size_t i = 0; i < note.size(); ++i) {
        if (i == 0 && menu.note_flashes() && !flash_shown) {
            continue;
        }
        canvas.text(text_x, note_y + (static_cast<int>(i) * kNoteLine), note.at(i).substr(0, kColumns), kGrey, font);
    }
    return canvas.done();
}

}  // namespace lvr
