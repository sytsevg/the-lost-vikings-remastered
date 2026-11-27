#pragma once

// The player's keys and gamepad as the original's input word (docs/original/input.md), and Remaster rules' own
// actions: what each action is done by, on the keyboard (SDL's key names) and on a gamepad (SDL's names for its
// buttons and axes). Vanilla rules have the original's (its keys, 0000:2C2A, and its joystick, 0000:2EF8), with
// other keys from a small text file if the player wants them; Remaster rules the player's, set in the game's menu
// (lvr/menu.hpp) and kept in the profile (lvr/profile.hpp). No SDL types here: the game resolves the names.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lvr::input {

// What a key or a button can do: the game's actions (bits of its input word), then Remaster rules' own, in the
// order the menu lists them.
enum class Action : std::uint8_t {
    kLeft,
    kRight,
    kUp,
    kDown,
    kAbilityF,
    kAbilityD,
    kUseItem,  // E
    kAct,      // S
    kPreviousViking,
    kNextViking,
    kInventory,
    kPause,  // the original's pause box (giving up the level is in it)
    kRewind,
    kZoomOut,
    kZoomIn,
    kMinimap,
    kQuicksave,  // (the sound and the screen's look are the menu's alone)
    kQuickload,
};
inline constexpr std::size_t kActionCount = 18;
// As a keys file and the profile write it: "left", "right", "up", "down", "ability-f", "ability-d", "use-item",
// "act", "previous-viking", "next-viking", "inventory", "pause", "rewind", "zoom-out", "zoom-in", "minimap",
// "quicksave", "quickload".
std::string_view name_of(Action action);
std::optional<Action> action_named(std::string_view name);
// As the menu shows it, in the font's capitals.
std::string_view label_of(Action action);

// A set of actions, a bit each.
using Actions = std::uint32_t;
constexpr Actions bit(Action action) noexcept {
    return Actions{1} << static_cast<unsigned>(action);
}
// The input word's bits of the game's actions among `actions`.
std::uint16_t game_bits(Actions actions);
// The game's actions, left to kPause; the rest are Remaster rules' own.
inline constexpr Actions kGameActions = (Actions{1} << (static_cast<unsigned>(Action::kPause) + 1)) - 1;

// A gamepad's inputs by SDL's names: its buttons in SDL's order ("a", "b", "x", "y", "back", "guide", "start",
// "leftstick", ...: SDL_GetGamepadStringForButton's), then each stick's four directions ("leftx-" left, "leftx+"
// right, "lefty-" up, "lefty+" down; "rightx-", ...) and the triggers ("lefttrigger", "righttrigger").
inline constexpr std::size_t kPadInputCount = 36;
const std::array<std::string_view, kPadInputCount>& pad_inputs() noexcept;
std::optional<std::size_t> pad_input_index(std::string_view input);
// As the menu shows it: "A", "LB", "LT", "L3", "DPAD UP", "LS LEFT", ... (an Xbox pad's names, which SDL's layout
// is; on another pad the button in the same place).
std::string_view pad_label(std::string_view input);

// What does each action (by Action): keys by SDL's key names, gamepad inputs by the names above. The menu binds a
// key or an input to one action; a keys file may give it more.
struct Controls {
    std::array<std::vector<std::string>, kActionCount> keys;
    std::array<std::vector<std::string>, kActionCount> pad;

    bool operator==(const Controls&) const = default;
};
// Vanilla rules': the original's keys (the keypad's arrows are the arrows' scancodes there), and a gamepad as its
// joystick: its buttons 1-4 as A, B, X, Y (F, D, E, S), the d-pad or the left stick as the arrows, the shoulders
// switch Vikings (previous, next), Start pauses and Back opens the inventory.
Controls original_controls();
// Remaster rules' to begin with: a key an action: the arrows, Space and D the abilities (1 and 2), E and S, Left Ctrl
// and Right Ctrl the Viking before and after, Tab the inventory, P pauses; the gamepad as the original's joystick, but
// Start opens the menu (as Escape does) and nothing pauses. And Remaster's own: Backspace and the left
// trigger rewind, - and = and the right stick up and down zoom out and in, M and the right stick's press the
// minimap, F7 and F8 the quicksave.
Controls remaster_controls();

// `base` with the lines of a keys file applied: "KEY NAME = action[, action...]" binds a key (replacing what it
// had; "none" unbinds it); blank lines and lines starting with # are skipped. Errors name the line.
std::expected<Controls, std::string> parse_bindings(std::string_view text, Controls base);

// The actions `key` (or a pad's `input`) does.
Actions key_actions(const Controls& controls, std::string_view key);
Actions pad_actions(const Controls& controls, std::string_view input);
// Keys and inputs that can't be bound: they open the menu (Escape, the pad's Start and its guide button) or are the
// original's own menu keys (F10, the Alts).
bool fixed_key(std::string_view key) noexcept;
bool fixed_pad_input(std::string_view input) noexcept;
// `key` (or `input`) does `action` from now on, and nothing else: taken from any action that had it. A key is then the
// action's only one (the menu gives an action one key); an input is added to the action's others (a stick and the
// d-pad both walk). False (and nothing changed) for a fixed one, or a pad input that isn't one.
bool bind_key(Controls& controls, Action action, std::string_view key);
bool bind_pad(Controls& controls, Action action, std::string_view input);

// A gamepad as it is read: how far each of pad_inputs() is pushed, 0 to 1 (a button 0 or 1).
using PadReading = std::array<float, kPadInputCount>;
struct PadActions {
    // Past half way: a stick's direction or a trigger (with `analog`, one that walks or climbs: past its dead zone,
    // where it is the way the stick is pushed most).
    Actions held = 0;
    // How fast the Vikings walk (and steer in the air) and climb, with `analog`: how far a stick bound to left or
    // right (up or down) is pushed past the dead zone, 0 to 1. 1 for anything else that does it (a button, the
    // d-pad), when nothing does, and without `analog`.
    float walk = 1;
    float climb = 1;
};
// `analog`: Remaster rules' walking and climbing by how far a stick is pushed.
PadActions read_pad(const Controls& controls, const PadReading& reading, bool analog);

// The character the original's keyboard handler notes for a pressed key (its table at ds:8E68, a word per
// scancode; docs/original/input.md): the digits and the consonants as ASCII (no vowels: the passwords have none), 0x80
// for Backspace, 0x81 for Return, Space, [ and ]; 0 for every other key. `key` is SDL's key name.
std::uint16_t typed_character(std::string_view key) noexcept;
// Remaster rules' own (the handler gives it; the original's table has none): Escape, which takes the title's
// password back to its menu.
inline constexpr std::uint16_t kTypedEscape = 0x82;

}  // namespace lvr::input
