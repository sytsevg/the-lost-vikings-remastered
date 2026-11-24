#include "input/input.hpp"

#include <array>
#include <format>
#include <utility>

namespace lvr::input {
namespace {

constexpr std::array<std::pair<std::string_view, std::uint16_t>, 13> kActions{{
    {"none", 0x0000},
    {"next-viking", 0x0010},
    {"previous-viking", 0x0020},
    {"use-item", 0x0040},
    {"act", 0x0080},
    {"right", 0x0100},
    {"left", 0x0200},
    {"down", 0x0400},
    {"up", 0x0800},
    {"pause", 0x1000},
    {"inventory", 0x2000},
    {"ability-d", 0x4000},
    {"ability-f", 0x8000},
}};

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

}  // namespace

Bindings default_bindings() {
    return Bindings{
        {"Insert", 0x0010},   {"PageUp", 0x0010}, {"Left Ctrl", 0x0020}, {"Right Ctrl", 0x0020}, {"Home", 0x0020},
        {"E", 0x0040},        {"S", 0x0080},      {"Right", 0x0100},     {"Keypad 6", 0x0100},   {"Left", 0x0200},
        {"Keypad 4", 0x0200}, {"Down", 0x0400},   {"Keypad 2", 0x0400},  {"Keypad 5", 0x0400},   {"Up", 0x0800},
        {"Keypad 8", 0x0800}, {"Escape", 0x1000}, {"P", 0x1000},         {"Tab", 0x2000},        {"CapsLock", 0x2000},
        {"D", 0x4000},        {"F", 0x8000},      {"Space", 0x8000},     {"Return", 0x8000},     {"Keypad +", 0x8000},
    };
}

std::expected<std::uint16_t, std::string> action_bits(std::string_view action) {
    for (const auto& [name, bits] : kActions) {
        if (name == action) {
            return bits;
        }
    }
    return std::unexpected{std::format("unknown action '{}'", action)};
}

std::expected<Bindings, std::string> parse_bindings(std::string_view text, Bindings base) {
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
        std::uint16_t bits = 0;
        std::string_view actions = line.substr(equals + 1);
        while (true) {
            const auto comma = actions.find(',');
            auto found = action_bits(trim(actions.substr(0, comma)));
            if (!found) {
                return std::unexpected{std::format("line {}: {}", number, found.error())};
            }
            bits = static_cast<std::uint16_t>(bits | *found);
            if (comma == std::string_view::npos) {
                break;
            }
            actions = actions.substr(comma + 1);
        }
        if (bits == 0) {
            base.erase(std::string{key});
        } else {
            base.insert_or_assign(std::string{key}, bits);
        }
    }
    return base;
}

std::uint16_t key_bits(const Bindings& bindings, std::string_view key) {
    const auto found = bindings.find(key);
    return found == bindings.end() ? std::uint16_t{0} : found->second;
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

std::uint16_t pad_bits(const Pad& pad) noexcept {
    unsigned bits = 0;
    const auto add = [&bits](bool on, unsigned bit) { bits |= on ? bit : 0U; };
    add(pad.right, 0x0100);
    add(pad.left, 0x0200);
    add(pad.down, 0x0400);
    add(pad.up, 0x0800);
    add(pad.south, 0x8000);
    add(pad.east, 0x4000);
    add(pad.west, 0x0040);
    add(pad.north, 0x0080);
    add(pad.left_shoulder, 0x0020);
    add(pad.right_shoulder, 0x0010);
    add(pad.start, 0x1000);
    add(pad.back, 0x2000);
    return static_cast<std::uint16_t>(bits);
}

}  // namespace lvr::input
