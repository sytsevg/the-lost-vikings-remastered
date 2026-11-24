#pragma once

// A scene's script: the cues its director runs, one after another (the scenes outside the levels, and the
// starfield).

#include <cstddef>
#include <cstdint>
#include <span>

#include "lvr/entity.hpp"
#include "lvr/names.hpp"

namespace lvr {

class World;

// A speed as a cue's value: pixels a tick, in 256ths.
constexpr std::uint16_t speed(double pixels) noexcept {
    return static_cast<std::uint16_t>(px(pixels));
}

// A scene's script: what its director does, one thing after another; a wait holds it for that many ticks.
struct Cue {
    enum class Kind : std::uint8_t {
        kEffect,        // value: the effect
        kStopEffect,    // value: the effect
        kRestartMusic,  //
        kColour,        // value: the boxes' colour
        kSay,           // value: the text; anchor; below: where the box points, from the director
        kClear,         // the text layer emptied
        kWait,          // value: ticks
        kSpeedAcross,   // slot, value: that entity's speed
        kNoHealth,      // slot: that entity's health to 0 (it goes)
        kSayAt,         // value: the text; anchor; below: where the box points, from the entity in slot
        kSayAtCell,     // value: the text; anchor; slot, below: the column and the row
        kSayAtEntity,   // value: the text; anchor; slot, below: where the box points, from the director
        kSayAtPoint,    // value: the text; anchor; slot, below: the point of the level its box points at
        kSpeedDown,     // slot, value: that entity's speed down
        kOwnSpeed,      // value: the director's own speed across
        kSwitches,      // value: the scene's switches
        kShake,         // value: the amount; slot: for how many ticks
        kTell,          // value: the message to the three Vikings
        kHealth,        // value: the three Vikings' health
        kLeave,         // the scene ends
    };

    Kind kind;
    std::uint16_t value = 0;
    int anchor = 0;
    int below = 0;
    int slot = 0;
};

// The cues, as a script writes them: `colour(text_colour::kErik), say(0x17b, 0, -40), wait(200), clear()`.
namespace cue {
using enum Cue::Kind;
constexpr Cue effect(std::uint16_t which) {
    return {.kind = kEffect, .value = which};
}
constexpr Cue stop_effect(std::uint16_t which) {
    return {.kind = kStopEffect, .value = which};
}
constexpr Cue restart_music() {
    return {.kind = kRestartMusic};
}
constexpr Cue colour(std::uint16_t of_the_boxes) {
    return {.kind = kColour, .value = of_the_boxes};
}
constexpr Cue clear() {
    return {.kind = kClear};
}
constexpr Cue wait(std::uint16_t ticks) {
    return {.kind = kWait, .value = ticks};
}
constexpr Cue leave() {
    return {.kind = kLeave};
}
// A text in a box: where the box points is from the director, from the entity in a slot, a cell of the screen, or
// a point of the level.
constexpr Cue say(std::uint16_t text, int anchor, int below) {
    return {.kind = kSay, .value = text, .anchor = anchor, .below = below};
}
constexpr Cue say_at(int slot, std::uint16_t text, int anchor, int below) {
    return {.kind = kSayAt, .value = text, .anchor = anchor, .below = below, .slot = slot};
}
constexpr Cue say_at_cell(std::uint16_t text, int anchor, int column, int row) {
    return {.kind = kSayAtCell, .value = text, .anchor = anchor, .below = row, .slot = column};
}
constexpr Cue say_at_entity(std::uint16_t text, int anchor, int across, int below) {
    return {.kind = kSayAtEntity, .value = text, .anchor = anchor, .below = below, .slot = across};
}
constexpr Cue say_at_point(std::uint16_t text, int anchor, int x, int y) {
    return {.kind = kSayAtPoint, .value = text, .anchor = anchor, .below = y, .slot = x};
}
// An entity's speed (by its slot), and the director's own.
constexpr Cue speed_across(int slot, std::uint16_t speed) {
    return {.kind = kSpeedAcross, .value = speed, .slot = slot};
}
constexpr Cue speed_down(int slot, std::uint16_t speed) {
    return {.kind = kSpeedDown, .value = speed, .slot = slot};
}
constexpr Cue own_speed(std::uint16_t speed) {
    return {.kind = kOwnSpeed, .value = speed};
}
constexpr Cue no_health(int slot) {
    return {.kind = kNoHealth, .slot = slot};
}  // it goes
constexpr Cue switches(std::uint16_t of_the_scene) {
    return {.kind = kSwitches, .value = of_the_scene};
}
constexpr Cue shake(std::uint16_t amount, int ticks) {
    return {.kind = kShake, .value = amount, .slot = ticks};
}
constexpr Cue tell(std::uint16_t the_vikings) {
    return {.kind = kTell, .value = the_vikings};
}
constexpr Cue health(std::uint16_t of_the_vikings) {
    return {.kind = kHealth, .value = of_the_vikings};
}
}  // namespace cue

// Runs the cues up to the next wait. True while there are more.
bool run_cues(World& world, Entity& director, std::span<const Cue> cues, std::size_t& at);

}  // namespace lvr
