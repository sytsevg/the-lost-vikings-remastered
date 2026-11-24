#pragma once

// Where a world keeps its animation programs. The kinds name an animation by its place in the spaceship world's
// object database, where their behaviour was read; the other five databases hold the same programs at other
// places. The places are paired up when a level is loaded, by reading the two databases' scripts side by side:
// where both have the same instructions, an animation started in the one is the animation started in the other.

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>

namespace lvr {

// A place in the spaceship's database -> the same program's place in another world's.
using AnimationPlaces = std::unordered_map<std::uint16_t, std::uint16_t>;

// Both are whole object databases (DATA.DAT entries 449-454): the first is the one the kinds name their animations
// in (the spaceship's, or another kind's home). Where the two worlds' scripts differ by a few instructions the
// pairing goes on after them; kinds that differ more are paired as far as they are the same.
AnimationPlaces pair_animation_places(std::span<const std::byte> spaceship, std::span<const std::byte> other);

}  // namespace lvr
