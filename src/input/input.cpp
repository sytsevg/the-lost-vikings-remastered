#include "input/input.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <initializer_list>
#include <string>

namespace lvr::input {
namespace {

struct ActionInfo {
    std::string_view name;
    std::string_view label;
    std::uint16_t bits;
};
constexpr std::array<ActionInfo, kActionCount> kActionInfo{{
    {"left", "LEFT", 0x0200},
    {"right", "RIGHT", 0x0100},
    {"up", "UP", 0x0800},
    {"down", "DOWN", 0x0400},
    {"ability-f", "ABILITY 1", 0x8000},
    {"ability-d", "ABILITY 2", 0x4000},
    {"use-item", "USE ITEM", 0x0040},
    {"act", "ACT", 0x0080},
    {"previous-viking", "PREVIOUS VIKING", 0x0020},
    {"next-viking", "NEXT VIKING", 0x0010},
    {"inventory", "INVENTORY", 0x2000},
    {"pause", "GIVE UP LEVEL", 0x1000},  // the original's pause box: its question is whether to give up
    {"rewind", "REWIND", 0},
    {"zoom-out", "ZOOM OUT", 0},
    {"zoom-in", "ZOOM IN", 0},
    {"minimap", "MINIMAP", 0},
    {"quicksave", "QUICKSAVE", 0},
    {"quickload", "QUICKLOAD", 0},
}};

const ActionInfo& info(Action action) {
    return kActionInfo.at(static_cast<std::size_t>(action));
}

// SDL's buttons in its order (SDL_GamepadButton), then the sticks' directions and the triggers.
constexpr std::array<std::string_view, kPadInputCount> kPadInputs{
    "a",
    "b",
    "x",
    "y",
    "back",
    "guide",
    "start",
    "leftstick",
    "rightstick",
    "leftshoulder",
    "rightshoulder",
    "dpup",
    "dpdown",
    "dpleft",
    "dpright",
    "misc1",
    "paddle1",
    "paddle2",
    "paddle3",
    "paddle4",
    "touchpad",
    "misc2",
    "misc3",
    "misc4",
    "misc5",
    "misc6",
    "leftx-",
    "leftx+",
    "lefty-",
    "lefty+",
    "rightx-",
    "rightx+",
    "righty-",
    "righty+",
    "lefttrigger",
    "righttrigger",
};
constexpr std::array<std::string_view, kPadInputCount> kPadLabels{
    "A",        "B",        "X",        "Y",         "BACK",      "GUIDE",      "START",   "L3",       "R3",
    "LB",       "RB",       "DPAD UP",  "DPAD DOWN", "DPAD LEFT", "DPAD RIGHT", "MISC 1",  "PADDLE 1", "PADDLE 2",
    "PADDLE 3", "PADDLE 4", "TOUCHPAD", "MISC 2",    "MISC 3",    "MISC 4",     "MISC 5",  "MISC 6",   "LS LEFT",
    "LS RIGHT", "LS UP",    "LS DOWN",  "RS LEFT",   "RS RIGHT",  "RS UP",      "RS DOWN", "LT",       "RT",
};

// Under Remaster rules a stick that walks does so from its dead zone on, and at full speed from near its end (a
// stick pushed along a diagonal doesn't reach the end of either axis). Anything else is held from half way.
constexpr float kHalfWay = 0.5F;
constexpr float kDeadZone = 0.2F;
constexpr float kFullWalk = 0.9F;

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

using Table = std::array<std::vector<std::string>, kActionCount>;

void add(Table& table, Action action, std::initializer_list<std::string_view> names) {
    for (const std::string_view name : names) {
        table.at(static_cast<std::size_t>(action)).emplace_back(name);
    }
}

Actions actions_of(const Table& table, std::string_view input) {
    Actions actions = 0;
    for (std::size_t action = 0; action < kActionCount; ++action) {
        if (std::ranges::find(table.at(action), input) != table.at(action).end()) {
            actions |= Actions{1} << action;
        }
    }
    return actions;
}

void unbind(Table& table, std::string_view input) {
    for (std::vector<std::string>& bound : table) {
        std::erase(bound, input);
    }
}

}  // namespace

std::string_view name_of(Action action) {
    return info(action).name;
}

std::optional<Action> action_named(std::string_view name) {
    for (std::size_t i = 0; i < kActionCount; ++i) {
        if (kActionInfo.at(i).name == name) {
            return static_cast<Action>(i);
        }
    }
    return std::nullopt;
}

std::string_view label_of(Action action) {
    return info(action).label;
}

std::uint16_t game_bits(Actions actions) {
    unsigned bits = 0;
    for (std::size_t i = 0; i < kActionCount; ++i) {
        if ((actions & (Actions{1} << i)) != 0) {
            bits |= kActionInfo.at(i).bits;
        }
    }
    return static_cast<std::uint16_t>(bits);
}

const std::array<std::string_view, kPadInputCount>& pad_inputs() noexcept {
    return kPadInputs;
}

std::optional<std::size_t> pad_input_index(std::string_view input) {
    for (std::size_t i = 0; i < kPadInputCount; ++i) {
        if (kPadInputs.at(i) == input) {
            return i;
        }
    }
    return std::nullopt;
}

std::string_view pad_label(std::string_view input) {
    const auto index = pad_input_index(input);
    return index ? kPadLabels.at(*index) : std::string_view{"?"};
}

Controls original_controls() {
    Controls c;
    add(c.keys, Action::kLeft, {"Left", "Keypad 4"});
    add(c.keys, Action::kRight, {"Right", "Keypad 6"});
    add(c.keys, Action::kUp, {"Up", "Keypad 8"});
    add(c.keys, Action::kDown, {"Down", "Keypad 2", "Keypad 5"});
    add(c.keys, Action::kAbilityF, {"F", "Space", "Return", "Keypad +"});
    add(c.keys, Action::kAbilityD, {"D"});
    add(c.keys, Action::kUseItem, {"E"});
    add(c.keys, Action::kAct, {"S"});
    add(c.keys, Action::kPreviousViking, {"Home", "Left Ctrl", "Right Ctrl"});
    add(c.keys, Action::kNextViking, {"Insert", "PageUp"});
    add(c.keys, Action::kInventory, {"Tab", "CapsLock"});
    add(c.keys, Action::kPause, {"Escape", "P"});
    add(c.pad, Action::kLeft, {"dpleft", "leftx-"});
    add(c.pad, Action::kRight, {"dpright", "leftx+"});
    add(c.pad, Action::kUp, {"dpup", "lefty-"});
    add(c.pad, Action::kDown, {"dpdown", "lefty+"});
    add(c.pad, Action::kAbilityF, {"a"});
    add(c.pad, Action::kAbilityD, {"b"});
    add(c.pad, Action::kUseItem, {"x"});
    add(c.pad, Action::kAct, {"y"});
    add(c.pad, Action::kPreviousViking, {"leftshoulder"});
    add(c.pad, Action::kNextViking, {"rightshoulder"});
    add(c.pad, Action::kInventory, {"back"});
    add(c.pad, Action::kPause, {"start"});
    return c;
}

Controls remaster_controls() {
    Controls c = original_controls();
    // A key an action (the user, 2026-11-25): the plain ones of the original's, the two Ctrls a Viking each way.
    c.keys = {};
    add(c.keys, Action::kLeft, {"Left"});
    add(c.keys, Action::kRight, {"Right"});
    add(c.keys, Action::kUp, {"Up"});
    add(c.keys, Action::kDown, {"Down"});
    add(c.keys, Action::kAbilityF, {"Space"});
    add(c.keys, Action::kAbilityD, {"D"});
    add(c.keys, Action::kUseItem, {"E"});
    add(c.keys, Action::kAct, {"S"});
    add(c.keys, Action::kPreviousViking, {"Left Ctrl"});
    add(c.keys, Action::kNextViking, {"Right Ctrl"});
    add(c.keys, Action::kInventory, {"Tab"});
    add(c.keys, Action::kPause, {"P"});  // (Escape and Start open the menu)
    unbind(c.pad, "start");
    // (Nothing pauses on the pad: the left stick's press, which pausing was on, is too easily pressed while
    // walking, the user. The menu's "give up level" opens the original's pause box.)
    add(c.keys, Action::kRewind, {"Backspace"});
    add(c.keys, Action::kZoomOut, {"-"});
    add(c.keys, Action::kZoomIn, {"="});
    add(c.keys, Action::kMinimap, {"M"});
    add(c.keys, Action::kQuicksave, {"F7"});
    add(c.keys, Action::kQuickload, {"F8"});
    add(c.pad, Action::kRewind, {"lefttrigger"});
    add(c.pad, Action::kZoomOut, {"righty-"});
    add(c.pad, Action::kZoomIn, {"righty+"});
    add(c.pad, Action::kMinimap, {"rightstick"});
    return c;
}

std::expected<Controls, std::string> parse_bindings(std::string_view text, Controls base) {
    int number = 0;
    while (!text.empty()) {
        ++number;
        const auto end = text.find('\n');
        const std::string_view line = trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto equals = line.find('=');
        const std::string_view key = trim(line.substr(0, equals));
        if (equals == std::string_view::npos || key.empty()) {
            return std::unexpected{std::format("line {}: expected 'KEY = action'", number)};
        }
        Actions actions = 0;
        std::string_view names = line.substr(equals + 1);
        while (true) {
            const auto comma = names.find(',');
            const std::string_view name = trim(names.substr(0, comma));
            if (const auto action = action_named(name)) {
                actions |= bit(*action);
            } else if (name != "none") {
                return std::unexpected{std::format("line {}: unknown action '{}'", number, name)};
            }
            if (comma == std::string_view::npos) {
                break;
            }
            names = names.substr(comma + 1);
        }
        unbind(base.keys, key);
        for (std::size_t action = 0; action < kActionCount; ++action) {
            if ((actions & (Actions{1} << action)) != 0) {
                base.keys.at(action).emplace_back(key);
            }
        }
    }
    return base;
}

Actions key_actions(const Controls& controls, std::string_view key) {
    return actions_of(controls.keys, key);
}

Actions pad_actions(const Controls& controls, std::string_view input) {
    return actions_of(controls.pad, input);
}

bool fixed_key(std::string_view key) noexcept {
    return key == "Escape" || key == "F10" || key == "Left Alt" || key == "Right Alt";
}

bool fixed_pad_input(std::string_view input) noexcept {
    return input == "start" || input == "guide";
}

bool bind_key(Controls& controls, Action action, std::string_view key) {
    if (key.empty() || fixed_key(key)) {
        return false;
    }
    unbind(controls.keys, key);
    controls.keys.at(static_cast<std::size_t>(action)) = {std::string{key}};
    return true;
}

bool bind_pad(Controls& controls, Action action, std::string_view input) {
    if (!pad_input_index(input) || fixed_pad_input(input)) {
        return false;
    }
    unbind(controls.pad, input);
    add(controls.pad, action, {input});
    return true;
}

PadActions read_pad(const Controls& controls, const PadReading& reading, bool analog) {
    constexpr Actions kWalking = bit(Action::kLeft) | bit(Action::kRight);
    constexpr Actions kClimbing = bit(Action::kUp) | bit(Action::kDown);
    // How far the stick of axis input `name` ("leftx-", ...) is pushed along its other axis, either way.
    const auto across = [&reading](std::string_view name) {
        std::string other{name};
        other.at(other.size() - 2) = other.at(other.size() - 2) == 'x' ? 'y' : 'x';
        float most = 0;
        for (const char sign : {'-', '+'}) {
            other.back() = sign;
            if (const auto index = pad_input_index(other)) {
                most = std::max(most, reading.at(*index));
            }
        }
        return most;
    };
    PadActions read;
    float walk = 0;  // the furthest a stick that walks (or climbs) is pushed, past the dead zone
    float climb = 0;
    bool walks_full = false;
    bool climbs_full = false;
    for (std::size_t i = 0; i < kPadInputCount; ++i) {
        const float pushed = reading.at(i);
        const Actions actions = pushed > 0 ? pad_actions(controls, kPadInputs.at(i)) : 0;
        if (actions == 0) {
            continue;
        }
        const std::string_view name = kPadInputs.at(i);
        const bool axis = name.ends_with('-') || name.ends_with('+');
        // A stick's direction that walks or climbs is held from its dead zone, as far as it is pushed, when it is
        // the way the stick is pushed most: up a ladder held a little to the side doesn't step off it, nor does
        // walking a little up go up one.
        const bool moving = (actions & (kWalking | kClimbing)) != 0;
        const bool analog_axis = analog && axis && moving && pushed >= across(name);
        if (pushed <= (analog_axis ? kDeadZone : kHalfWay)) {
            continue;
        }
        read.held |= actions;
        const float how_far = std::min((pushed - kDeadZone) / (kFullWalk - kDeadZone), 1.0F);
        if ((actions & kWalking) != 0) {
            walk = analog_axis ? std::max(walk, how_far) : walk;
            walks_full = walks_full || !analog_axis;
        }
        if ((actions & kClimbing) != 0) {
            climb = analog_axis ? std::max(climb, how_far) : climb;
            climbs_full = climbs_full || !analog_axis;
        }
    }
    read.walk = (walks_full || walk <= 0) ? 1.0F : walk;
    read.climb = (climbs_full || climb <= 0) ? 1.0F : climb;
    return read;
}

std::uint16_t typed_character(std::string_view key) noexcept {
    if (key.size() == 1) {
        const char c = key.front();
        if ((c >= '0' && c <= '9') || (c >= 'B' && c <= 'Z' && !std::string_view{"EIOU"}.contains(c))) {
            return static_cast<std::uint16_t>(c);
        }
        return c == '[' || c == ']' ? std::uint16_t{0x81} : std::uint16_t{0};
    }
    if (key == "Backspace") {
        return 0x80;
    }
    return key == "Return" || key == "Space" ? std::uint16_t{0x81} : std::uint16_t{0};
}

}  // namespace lvr::input
