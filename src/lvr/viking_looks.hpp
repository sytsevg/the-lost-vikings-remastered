#pragma once

// The Vikings' animations and each one's own numbers: where the animations are in the spaceship world's programs
// (the addresses go when the animations have names: docs/ROADMAP.md, M8), and the speeds of what each can do.
// Read by the Vikings' files (through viking.hpp), and by the effects' table for his deaths (effects.cpp): the
// numbers are apart from the behaviour.

#include <array>
#include <cstdint>

#include "lvr/names.hpp"

namespace lvr {

// Where a Viking's animations are in the spaceship world's programs.
struct VikingLooks {
    Animation stands;
    Animation back_on_his_feet;
    Animation walks;
    Animation stops;
    Animation skids;
    Animation rises;
    Animation floats;
    Animation falls;
    Animation falls_far;
    Animation pushes;
    Animation is_hurt;
    Animation dizzy;
    // On a ladder. The frames are where a climbing step ends: the three rungs of the cycle, and the two of
    // stepping off its top.
    Animation onto_ladder_up;
    Animation onto_ladder_down;
    std::array<std::uint16_t, 3> rung_frames;
    std::uint16_t nearly_off_frame;
    std::uint16_t off_frame;
    std::array<Animation, 3> climbs_down_from;
    Animation climbs_down;
    Animation climbs_back_down;  // from stepping off the top
    Animation climbs_back_down_nearly_off;
    std::array<Animation, 3> climbs_up_from;
    Animation climbs_up;
    Animation steps_off;
    Animation steps_off_more;
    Animation stepped_off;
    // Dying.
    Animation dies;
    Animation drowns;
    Animation dies_from_a_fall;  // landing too hard
    Animation electrocuted;
    Animation electrocuted_from_below;
    Animation electrocuted_from_above;
    Animation spiked;
    Animation squashed;
    Animation squashed_flat;
    Animation crushed;
};

inline constexpr VikingLooks kErikLooks{
    .stands = {0x4c7d},
    .back_on_his_feet = {0x4c7d},
    .walks = {0x4c89},
    .stops = {0x4cdc},
    .skids = {0x4cb2},
    .rises = {0x4d1f},
    .floats = {0x4d2a},
    .falls = {0x4d32},
    .falls_far = {0x4eae},
    .pushes = {0x4e95},
    .is_hurt = {0x4e85},
    .dizzy = {0x6bbe},
    .onto_ladder_up = {0x4dd5},
    .onto_ladder_down = {0x4e5b},
    .rung_frames = {34, 35, 36},
    .nearly_off_frame = 38,
    .off_frame = 39,
    .climbs_down_from = {{{0x4e18}, {0x4e23}, {0x4e2e}}},
    .climbs_down = {0x4e0a},
    .climbs_back_down = {0x4e66},
    .climbs_back_down_nearly_off = {0x4e74},
    .climbs_up_from = {{{0x4dff}, {0x4dd8}, {0x4de9}}},
    .climbs_up = {0x4df4},
    .steps_off = {0x4e39},
    .steps_off_more = {0x4e4a},
    .stepped_off = {0x4e58},
    .dies = {0x6914},
    .drowns = {0x67d9},
    .dies_from_a_fall = {0x69a7},
    .electrocuted = {0x6ae9},
    .electrocuted_from_below = {0x6afb},
    .electrocuted_from_above = {0x6b0d},
    .spiked = {0x6c57},
    .squashed = {0x686a},
    .squashed_flat = {0x6822},
    .crushed = {0x68bc},
};

inline constexpr VikingLooks kBaleogLooks{
    .stands = {0x403a},
    .back_on_his_feet = {0x403a},
    .walks = {0x4046},
    .stops = {0x406f},
    .skids = {0x406f},
    .rises = {0x421e},
    .floats = {0x421e},
    .falls = {0x421e},
    .falls_far = {0x4258},
    .pushes = {0x422f},
    .is_hurt = {0x4248},
    .dizzy = {0x6c02},
    .onto_ladder_up = {0x416b},
    .onto_ladder_down = {0x41f4},
    .rung_frames = {26, 27, 28},
    .nearly_off_frame = 37,
    .off_frame = 38,
    .climbs_down_from = {{{0x41ae}, {0x41b9}, {0x41c4}}},
    .climbs_down = {0x41a0},
    .climbs_back_down = {0x41ff},
    .climbs_back_down_nearly_off = {0x4210},
    .climbs_up_from = {{{0x4195}, {0x416e}, {0x417f}}},
    .climbs_up = {0x418a},
    .steps_off = {0x41cf},
    .steps_off_more = {0x41e0},
    .stepped_off = {0x41f1},
    .dies = {0x6945},
    .drowns = {0x67e6},
    .dies_from_a_fall = {0x69b7},
    .electrocuted = {0x6aef},
    .electrocuted_from_below = {0x6b01},
    .electrocuted_from_above = {0x6b13},
    .spiked = {0x6c73},
    .squashed = {0x6882},
    .squashed_flat = {0x683a},
    .crushed = {0x68d7},
};

// Olaf's with his shield before him; the ones that differ with the shield up are below.
inline constexpr VikingLooks kOlafLooks{
    .stands = {0x587d},
    .back_on_his_feet = {0x587d},
    .walks = {0x5891},
    .stops = {0x58e4},
    .skids = {0x58e4},
    .rises = {0x5a6f},
    .floats = {0x5a6f},
    .falls = {0x5a6f},
    .falls_far = {0x5aa6},
    .pushes = {0x5a80},
    .is_hurt = {0x5a99},
    .dizzy = {0x6c38},
    .onto_ladder_up = {0x599b},
    .onto_ladder_down = {0x5a21},
    .rung_frames = {22, 23, 24},
    .nearly_off_frame = 26,
    .off_frame = 27,
    .climbs_down_from = {{{0x59db}, {0x59e6}, {0x59f1}}},
    .climbs_down = {0x59cd},
    .climbs_back_down = {0x5a2e},
    .climbs_back_down_nearly_off = {0x5a3c},
    .climbs_up_from = {{{0x59c2}, {0x599b}, {0x59ac}}},
    .climbs_up = {0x59b7},
    .steps_off = {0x59fc},
    .steps_off_more = {0x5a0d},
    .stepped_off = {0x5a1b},
    .dies = {0x6976},
    .drowns = {0x67f5},
    .dies_from_a_fall = {0x69c7},
    .electrocuted = {0x6af5},
    .electrocuted_from_below = {0x6b07},
    .electrocuted_from_above = {0x6b19},
    .spiked = {0x6c8f},
    .squashed = {0x689a},
    .squashed_flat = {0x6852},
    .crushed = {0x68f2},
};

// Olaf's own: with the shield held up.
// Raising or lowering the shield goes on from the walking frame he is at: the animation for each frame.
inline constexpr std::array<Animation, 9> kOlafRaises{
    {{0x597f}, {0x5936}, {0x5940}, {0x594a}, {0x5954}, {0x595e}, {0x5968}, {0x5972}, {0x592f}}};  // from frames 0-8
inline constexpr std::array<Animation, 9> kOlafLowers{
    {{0x58e4}, {0x589b}, {0x58a5}, {0x58af}, {0x58b9}, {0x58c3}, {0x58cd}, {0x58d7}, {0x5891}}};  // from frames 9-17
inline constexpr int kGliding = px(2);      // the most he falls at under his shield
inline constexpr int kGlidingFast = px(3);  // across: from here the glide looks different
inline constexpr int kBlownBack = px(-4);

// Baleog's own.
inline constexpr std::array<Animation, 2> kBaleogSwings{{{0x40f6}, {0x4111}}};  // his two swings, and how each ends
inline constexpr std::array<Animation, 2> kBaleogSwingsOn{{{0x4105}, {0x4123}}};

// Erik's own.

inline constexpr int kErikTopSpeed = px(8);
inline constexpr int kTopSpeed = px(6);  // Baleog's and Olaf's

}  // namespace lvr
