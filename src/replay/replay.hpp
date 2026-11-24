#pragma once

// A replay: the input of a run, step by step, as a small text file (replays/*.replay). Both engines play the same
// replay in the pixel-match harness (golden.hpp). A step is one pass of input as the original's loop reads it: a
// tick of a level, a pass of a menu's loop or of the wait for a key.
//
//   # a comment
//   slot 0               the original's slot the run starts in (0-36 the levels, 37-47 the scenes)
//   steps 900            how many steps
//   key 10-25 0x0010     the bits of the keyboard word held in steps 10 to 25 (docs/original/input.md); or one step
//   held 300 0x15        a key of the key-state array held in that step, by scancode (Y 0x15, N 0x31, Alt 0x38 ...)
//   typed 40 0x54        a character typed before that step, as the keyboard handler notes it (password letters,
//                        the menus' keys)
//   place 5 1200 400     before that step the three Vikings are put at that point of the level (x, y; their
//                        boxes with them), in both engines alike: a replay that goes where a walk from the start would
//                        take long (the kinds of a level no other replay reaches)
//
// Steps count from 0. Lines of the same kind add up: the keyboard word of a step is all its `key` lines or-ed.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lvr::replay {

struct Step {
    std::uint16_t keyboard = 0;
    std::vector<std::uint8_t> held;                    // scancodes, in order
    std::optional<std::uint16_t> typed;                // the last one, if several were given
    std::optional<std::array<std::int16_t, 2>> place;  // the Vikings put at x, y

    bool operator==(const Step&) const = default;
};

struct Replay {
    std::uint16_t slot = 0;
    std::vector<Step> steps;

    bool operator==(const Replay&) const = default;
};

// An error names the line.
std::expected<Replay, std::string> parse_replay(std::string_view text);
std::expected<Replay, std::string> load_replay(const std::filesystem::path& path);

// The text of a replay: runs of steps with the same keyboard word as one `key` line per bit pattern.
std::string format_replay(const Replay& replay);

}  // namespace lvr::replay
