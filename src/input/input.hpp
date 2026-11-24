#pragma once

// The player's keys and gamepad as the original's input word (docs/original/input.md): which key sets which bits of the
// keyboard word, read from a small text file when the player wants other keys, and the gamepad's buttons and stick
// mapped the way the original maps its joystick (0000:2EF8). No SDL types here: keys are SDL's key names, which
// the game resolves to scancodes.

#include <cstdint>
#include <expected>
#include <map>
#include <string>
#include <string_view>

namespace lvr::input {

// Key name (SDL_GetScancodeName's) -> the input bits it sets.
using Bindings = std::map<std::string, std::uint16_t, std::less<>>;

// The original's defaults (0000:2C2A; the keypad's arrows are the arrows' scancodes there).
Bindings default_bindings();

// The input bits of an action name: right, left, up, down, previous-viking, next-viking, use-item (E),
// act (S), ability-d (D), ability-f (F), pause, inventory; "none" is 0.
std::expected<std::uint16_t, std::string> action_bits(std::string_view action);

// `base` with the lines of a bindings file applied: "KEY NAME = action[, action...]" binds a key (replacing what it
// had; "none" unbinds it); blank lines and lines starting with # are skipped. Errors name the line.
std::expected<Bindings, std::string> parse_bindings(std::string_view text, Bindings base);

// The bits `key` sets (0 if unbound).
std::uint16_t key_bits(const Bindings& bindings, std::string_view key);

// The character the original's keyboard handler notes for a pressed key (its table at ds:8E68, a word per
// scancode; docs/original/input.md): the digits and the consonants as ASCII (no vowels: the passwords have none), 0x80
// for Backspace, 0x81 for Return, Space, [ and ]; 0 for every other key. `key` is SDL's key name.
std::uint16_t typed_character(std::string_view key) noexcept;

struct Pad {
    bool left = false;  // the d-pad, or the left stick past half way
    bool right = false;
    bool up = false;
    bool down = false;
    bool south = false;  // A / cross: button 1 of the original's joystick
    bool east = false;   // B / circle: button 2
    bool west = false;   // X / square: button 3
    bool north = false;  // Y / triangle: button 4
    bool left_shoulder = false;
    bool right_shoulder = false;
    bool start = false;
    bool back = false;
};

// The input bits of the gamepad: the buttons 1-4 as the original's joystick buttons (F, D, E, S: 0x8000, 0x4000,
// 0x40, 0x80), the directions as the arrows, the shoulders switch Vikings (previous, next), Start pauses and Back
// opens the inventory.
std::uint16_t pad_bits(const Pad& pad) noexcept;

}  // namespace lvr::input
