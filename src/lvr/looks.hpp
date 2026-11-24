#pragma once

// What an entity looks for after a tick's move, and what it saw. The engine moves every entity at the end of a
// tick, after all their turns; only then can an entity find that a wall stopped it, that it landed, or who touches
// it, and it acts on that at its next turn. So a kind says, for what it is doing, what it looks for (looks_for()),
// the engine does the looking, and the kind reads the answers by name (saw.wall, saw.landed, saw.viking).
//
// The looks that stop an entity move it (back out of the wall, onto the ground), so which ones a kind asks for,
// and in which order, is part of how it behaves.

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "lvr/names.hpp"

namespace lvr {

using TileTypes = std::bitset<256>;

// What it saw: the answers to its looks. One that wasn't asked for is false, or nobody.
struct Saw {
    // After its move (asked for only by an entity that moves itself):
    bool wall = false;         // a wall it moved into stopped it
    bool ceiling = false;      // a ceiling or a floor did
    bool landed = false;       // it came down onto something
    bool shield_wall = false;  // Olaf's shield, held before him, stopped it
    bool on_shield = false;    // it came down onto his shield, held overhead
    bool held = false;         // it came down onto something that holds it up without being solid ground
    // Later in the tick: the slot of the first one that touches it.
    int viking = kNobody;
    int enemy = kNobody;
    int shield = kNobody;           // Olaf's, held before him
    int shield_overhead = kNobody;  // and held up
    int other = kNobody;            // whatever else its kind looks for
    int another = kNobody;          // and a second such
};

// One thing to look for. Made by the functions below.
struct Look {
    enum class For : std::uint8_t { kWall, kCeilingOrFloor, kLanding, kTouching, kTouchingAny };
    For what = For::kWall;
    const TileTypes* types = nullptr;  // for the first three: the tile types that count
    std::uint16_t hits = 0;            // for the touching ones: the hit class, or the bits of the hit mask
    bool vikings_only = false;
    bool unless_answered = false;   // not made if its answer is true already (the second of "this, or else that")
    bool Saw::* stopped = nullptr;  // where the answer goes
    int Saw::* who = nullptr;
};

// A wall of these types (or an entity that acts as one) that it moved into: it is put back at its edge.
constexpr Look stopped_by(const TileTypes& types, bool Saw::* answer = &Saw::wall) {
    return {.what = Look::For::kWall, .types = &types, .stopped = answer};
}
// The same up and down: a ceiling, or a floor.
constexpr Look stopped_above_or_below(const TileTypes& types, bool Saw::* answer = &Saw::ceiling) {
    return {.what = Look::For::kCeilingOrFloor, .types = &types, .stopped = answer};
}
// Moving down, it comes onto these: it is put on top.
constexpr Look lands_on(const TileTypes& types, bool Saw::* answer = &Saw::landed) {
    return {.what = Look::For::kLanding, .types = &types, .stopped = answer};
}
// The same, tried only if the look before it with the same answer found nothing.
constexpr Look or_else_lands_on(const TileTypes& types, bool Saw::* answer = &Saw::landed) {
    return {.what = Look::For::kLanding, .types = &types, .unless_answered = true, .stopped = answer};
}
// The first entity of a hit class whose box overlaps its own.
constexpr Look touching(std::uint16_t hit_class, int Saw::* answer) {
    return {.what = Look::For::kTouching, .hits = hit_class, .who = answer};
}
// The same among the Vikings' slots only.
constexpr Look touching_viking(std::uint16_t hit_class, int Saw::* answer = &Saw::viking) {
    return {.what = Look::For::kTouching, .hits = hit_class, .vikings_only = true, .who = answer};
}
// The first entity with any of these bits in its hit mask.
constexpr Look touching_any(std::uint16_t mask, int Saw::* answer, bool vikings_only = false) {
    return {.what = Look::For::kTouchingAny, .hits = mask, .vikings_only = vikings_only, .who = answer};
}

// What a kind looks for while it is doing one thing: a few looks, in the order they are made.
class Looks {
  public:
    static constexpr std::size_t kMost = 8;

    constexpr Looks() = default;
    constexpr Looks(std::initializer_list<Look> looks) {
        for (const Look& look : looks) {
            if (count_ < kMost) {
                looks_.at(count_++) = look;
            }
        }
    }
    constexpr const Look* begin() const noexcept { return looks_.data(); }
    constexpr const Look* end() const noexcept { return looks_.data() + count_; }

  private:
    std::array<Look, kMost> looks_{};
    std::size_t count_ = 0;
};

}  // namespace lvr
