#pragma once

// What is said, and by whom: every conversation of the game as rows. The texts themselves are in VIKINGS.EXE; a
// row has a text's number there. The kinds that hold a conversation (conversations.cpp, and Egypt's Talker) say
// its rows with speak().

#include <array>
#include <cstdint>

#include "lvr/names.hpp"

namespace lvr::talk {

enum class Who : std::uint8_t {
    kErik,  // a Viking by name: the same numbers as his slot
    kBaleog,
    kOlaf,
    kVoice,              // nobody to be seen: its box is at the top of the screen
    kHost,               // the one a Viking has come to talk to
    kViking,             // that Viking
    kVikingBeside,       // that Viking, with his box at the host's height
    kVikingBesideHisOwn  // ... and the text is the first of three, one for each: Baleog's, Erik's, Olaf's
};

// The slot of a Viking named by a row; kNobody for the others.
constexpr int slot_of(Who who) noexcept {
    return who <= Who::kOlaf ? static_cast<int>(who) : kNobody;
}

struct Said {
    Who who;
    std::uint16_t text;  // its number among the texts
    int anchor;          // where on the box its tail is (tail::)
    int below;           // the box is this far below the speaker's position (above, if negative)
    bool colour = true;  // false: the same speaker goes on, and the colour isn't set again
};

// A row of a level's own conversation.
struct LevelSaid {
    int level;  // the level (openings), or its scene (endings)
    Said said;
};

// --- The Vikings among themselves ---

// When a world's first level begins (by level).
inline constexpr std::array<LevelSaid, 18> kOpenings{{
    {1, {Who::kErik, 0x51, tail::kBelowLeft, -28}},
    {1, {Who::kErik, 0x52, tail::kBelowLeft, -28, false}},
    {5, {Who::kErik, 0x61, tail::kBelowLeft, -28}},
    {5, {Who::kBaleog, 0x62, tail::kAboveLeft, 20}},
    {5, {Who::kOlaf, 0x63, tail::kBelowLeft, -28}},
    {12, {Who::kErik, 0x7c, tail::kBelowLeft, -28}},
    {12, {Who::kBaleog, 0x7d, tail::kAboveLeft, 20}},
    {12, {Who::kErik, 0x7e, tail::kBelowLeft, -28}},
    {12, {Who::kOlaf, 0x7f, tail::kBelowLeft, -28}},
    {18, {Who::kOlaf, 0x95, tail::kBelowLeft, -28}},
    {18, {Who::kBaleog, 0x96, tail::kAboveLeft, 20}},
    {18, {Who::kErik, 0x97, tail::kBelowLeft, -28}},
    {26, {Who::kErik, 0xbf, tail::kBelowLeft, -28}},
    {26, {Who::kBaleog, 0xc0, tail::kAboveLeft, 20}},
    {26, {Who::kOlaf, 0xc1, tail::kBelowLeft, -28}},
    {34, {Who::kBaleog, 0xdc, tail::kAboveLeft, 20}},
    {34, {Who::kErik, 0xdd, tail::kBelowMiddle, -28}},
    {34, {Who::kOlaf, 0xde, tail::kBelowLeft, -28}},
}};

// The last level: the Vikings find the way home, in three parts.
inline constexpr std::array<Said, 3> kWayHomeFirst{{
    {Who::kVoice, 0xe9, tail::kAboveMiddle, 0},
    {Who::kVoice, 0xea, tail::kAboveMiddle, 0, false},
    {Who::kVoice, 0xeb, tail::kAboveMiddle, 0, false},
}};

// ...
inline constexpr std::array<Said, 3> kWayHomeSecond{{
    {Who::kBaleog, 0xec, tail::kAboveLeft, 20},
    {Who::kOlaf, 0xed, tail::kBelowLeft, -28},
    {Who::kOlaf, 0xee, tail::kBelowLeft, -28, false},
}};

// ...
inline constexpr std::array<Said, 2> kWayHomeThird{{
    {Who::kBaleog, 0xef, tail::kAboveLeft, 20},
    {Who::kErik, 0xf0, tail::kBelowMiddle, -28},
}};

// At the exit of a world's last level (by level), before the galaxy.
inline constexpr std::array<LevelSaid, 21> kWorldEndings{{
    {4, {Who::kOlaf, 0x5d, tail::kBelowRight, -28}},         {4, {Who::kBaleog, 0x5e, tail::kAboveRight, 20}},
    {4, {Who::kErik, 0x5f, tail::kBelowRight, -28}},         {4, {Who::kErik, 0x60, tail::kBelowRight, -28, false}},
    {11, {Who::kOlaf, 0x78, tail::kBelowMiddle, -28}},       {11, {Who::kBaleog, 0x79, tail::kAboveLeft, 20}},
    {11, {Who::kOlaf, 0x7a, tail::kBelowMiddle, -28}},       {11, {Who::kErik, 0x7b, tail::kBelowMiddle, -28}},
    {17, {Who::kErik, 0x91, tail::kBelowLeft, -28}},         {17, {Who::kOlaf, 0x92, tail::kBelowLeft, -28}},
    {17, {Who::kOlaf, 0x93, tail::kBelowLeft, -28, false}},  {17, {Who::kBaleog, 0x94, tail::kAboveLeft, 20}},
    {25, {Who::kBaleog, 0xb9, tail::kBelowRight, -28}},      {25, {Who::kErik, 0xba, tail::kBelowRight, -28}},
    {25, {Who::kOlaf, 0xbb, tail::kBelowRight, -28}},        {25, {Who::kOlaf, 0xbc, tail::kBelowRight, -28, false}},
    {25, {Who::kOlaf, 0xbd, tail::kBelowRight, -28, false}}, {25, {Who::kErik, 0xbe, tail::kBelowRight, -28}},
    {33, {Who::kBaleog, 0xd9, tail::kAboveRight, 20}},       {33, {Who::kErik, 0xda, tail::kBelowRight, -28}},
    {33, {Who::kOlaf, 0xdb, tail::kBelowRight, -28}},
}};

// When a level is done (by scene), in the order they are said.
inline constexpr std::array<LevelSaid, 114> kEndings{{
    {0, {Who::kBaleog, 0x53, tail::kAboveMiddle, 20}},   {0, {Who::kOlaf, 0x54, tail::kBelowMiddle, -28}},
    {0, {Who::kErik, 0x55, tail::kBelowRight, -28}},     {1, {Who::kErik, 0x56, tail::kBelowMiddle, -28}},
    {1, {Who::kOlaf, 0x57, tail::kBelowMiddle, -28}},    {1, {Who::kBaleog, 0x58, tail::kBelowMiddle, -28}},
    {2, {Who::kErik, 0x59, tail::kBelowMiddle, -28}},    {2, {Who::kOlaf, 0x5a, tail::kBelowMiddle, -28}},
    {2, {Who::kBaleog, 0x5b, tail::kAboveMiddle, 20}},   {2, {Who::kErik, 0x5c, tail::kBelowMiddle, -28}},
    {33, {Who::kVoice, 0xdf, tail::kAboveMiddle, 0}},    {33, {Who::kOlaf, 0xe0, tail::kBelowMiddle, -28}},
    {33, {Who::kVoice, 0xe1, tail::kAboveMiddle, 0}},    {33, {Who::kVoice, 0xe2, tail::kAboveMiddle, 0}},
    {34, {Who::kVoice, 0xe3, tail::kAboveMiddle, 0}},    {34, {Who::kBaleog, 0xe4, tail::kBelowRight, -28}},
    {34, {Who::kErik, 0xe5, tail::kBelowRight, -28}},    {34, {Who::kOlaf, 0xe6, tail::kBelowRight, -28}},
    {34, {Who::kOlaf, 0xe7, tail::kBelowRight, -28}},    {34, {Who::kOlaf, 0xe8, tail::kBelowRight, -28}},
    {35, {Who::kErik, 0xfe, tail::kBelowLeft, -28}},     {4, {Who::kErik, 0x64, tail::kBelowLeft, -28}},
    {4, {Who::kOlaf, 0x65, tail::kBelowLeft, -28}},      {4, {Who::kBaleog, 0x66, tail::kAboveLeft, 20}},
    {5, {Who::kErik, 0x67, tail::kBelowRight, -28}},     {5, {Who::kOlaf, 0x68, tail::kBelowRight, -28}},
    {5, {Who::kBaleog, 0x69, tail::kAboveRight, 20}},    {6, {Who::kOlaf, 0x6a, tail::kBelowRight, -28}},
    {6, {Who::kBaleog, 0x6b, tail::kAboveRight, 20}},    {6, {Who::kErik, 0x6c, tail::kBelowRight, -28}},
    {6, {Who::kOlaf, 0x6d, tail::kBelowRight, -28}},     {7, {Who::kErik, 0x6e, tail::kBelowMiddle, -28}},
    {7, {Who::kOlaf, 0x6f, tail::kBelowLeft, -28}},      {7, {Who::kBaleog, 0x70, tail::kBelowMiddle, -20}},
    {8, {Who::kBaleog, 0x71, tail::kAboveMiddle, 20}},   {8, {Who::kOlaf, 0x72, tail::kBelowMiddle, -28}},
    {8, {Who::kErik, 0x73, tail::kBelowMiddle, -28}},    {9, {Who::kOlaf, 0x74, tail::kBelowRight, -28}},
    {9, {Who::kErik, 0x75, tail::kBelowRight, -28}},     {9, {Who::kBaleog, 0x76, tail::kAboveRight, 20}},
    {9, {Who::kErik, 0x77, tail::kBelowRight, -28}},     {11, {Who::kErik, 0x80, tail::kBelowRight, -28}},
    {11, {Who::kBaleog, 0x81, tail::kBelowRight, -28}},  {11, {Who::kErik, 0x82, tail::kBelowRight, -28}},
    {11, {Who::kOlaf, 0x83, tail::kBelowRight, -28}},    {12, {Who::kErik, 0x84, tail::kBelowRight, -28}},
    {12, {Who::kBaleog, 0x85, tail::kAboveRight, 20}},   {12, {Who::kOlaf, 0x86, tail::kBelowRight, -28}},
    {13, {Who::kBaleog, 0x87, tail::kAboveMiddle, 20}},  {13, {Who::kOlaf, 0x88, tail::kBelowMiddle, -28}},
    {13, {Who::kErik, 0x89, tail::kBelowLeft, -28}},     {13, {Who::kOlaf, 0x8a, tail::kBelowMiddle, -28}},
    {14, {Who::kErik, 0x8b, tail::kBelowLeft, -28}},     {14, {Who::kBaleog, 0x8c, tail::kBelowLeft, -28}},
    {14, {Who::kOlaf, 0x8d, tail::kBelowLeft, -28}},     {15, {Who::kErik, 0x8e, tail::kBelowRight, -28}},
    {15, {Who::kOlaf, 0x8f, tail::kBelowRight, -28}},    {15, {Who::kBaleog, 0x90, tail::kBelowRight, -28}},
    {17, {Who::kBaleog, 0x98, tail::kBelowMiddle, -28}}, {17, {Who::kOlaf, 0x99, tail::kBelowLeft, -28}},
    {17, {Who::kErik, 0x9a, tail::kBelowLeft, -28}},     {17, {Who::kErik, 0x9b, tail::kBelowLeft, -28}},
    {17, {Who::kBaleog, 0x9c, tail::kBelowMiddle, -28}}, {18, {Who::kOlaf, 0x9d, tail::kBelowLeft, -28}},
    {18, {Who::kOlaf, 0x9e, tail::kBelowLeft, -28}},     {18, {Who::kBaleog, 0x9f, tail::kAboveLeft, 20}},
    {18, {Who::kErik, 0xa0, tail::kBelowLeft, -28}},     {19, {Who::kBaleog, 0xa1, tail::kAboveRight, 20}},
    {19, {Who::kErik, 0xa2, tail::kBelowRight, -28}},    {19, {Who::kBaleog, 0xa3, tail::kAboveRight, 20}},
    {19, {Who::kOlaf, 0xa4, tail::kBelowRight, -28}},    {19, {Who::kBaleog, 0xa5, tail::kAboveRight, 20}},
    {20, {Who::kErik, 0xa6, tail::kBelowMiddle, -28}},   {20, {Who::kOlaf, 0xa7, tail::kBelowMiddle, -28}},
    {20, {Who::kBaleog, 0xa8, tail::kBelowMiddle, -28}}, {20, {Who::kOlaf, 0xa9, tail::kBelowMiddle, -28}},
    {20, {Who::kBaleog, 0xaa, tail::kBelowMiddle, -28}}, {21, {Who::kErik, 0xab, tail::kBelowLeft, -28}},
    {21, {Who::kOlaf, 0xac, tail::kBelowMiddle, -28}},   {21, {Who::kBaleog, 0xad, tail::kAboveMiddle, 20}},
    {21, {Who::kOlaf, 0xae, tail::kBelowMiddle, -28}},   {21, {Who::kErik, 0xaf, tail::kBelowLeft, -28}},
    {21, {Who::kErik, 0xb0, tail::kBelowLeft, -28}},     {21, {Who::kOlaf, 0xb1, tail::kBelowMiddle, -28}},
    {22, {Who::kErik, 0xb2, tail::kBelowRight, -28}},    {22, {Who::kOlaf, 0xb3, tail::kBelowRight, -28}},
    {22, {Who::kBaleog, 0xb4, tail::kBelowRight, -28}},  {22, {Who::kOlaf, 0xb5, tail::kBelowRight, -28}},
    {23, {Who::kErik, 0xb6, tail::kBelowRight, -28}},    {23, {Who::kBaleog, 0xb7, tail::kBelowRight, -28}},
    {23, {Who::kOlaf, 0xb8, tail::kBelowRight, -28}},    {25, {Who::kBaleog, 0xc2, tail::kAboveRight, 20}},
    {25, {Who::kErik, 0xc3, tail::kBelowRight, -28}},    {25, {Who::kOlaf, 0xc4, tail::kBelowRight, -28}},
    {26, {Who::kOlaf, 0xc5, tail::kBelowMiddle, -28}},   {26, {Who::kBaleog, 0xc6, tail::kAboveMiddle, 20}},
    {26, {Who::kOlaf, 0xc7, tail::kBelowMiddle, -28}},   {26, {Who::kErik, 0xc8, tail::kBelowRight, -28}},
    {27, {Who::kErik, 0xc9, tail::kBelowMiddle, -28}},   {27, {Who::kOlaf, 0xca, tail::kAboveMiddle, 20}},
    {27, {Who::kErik, 0xcb, tail::kBelowMiddle, -28}},   {28, {Who::kErik, 0xcc, tail::kBelowMiddle, -28}},
    {28, {Who::kBaleog, 0xcd, tail::kAboveMiddle, 20}},  {28, {Who::kOlaf, 0xce, tail::kBelowMiddle, -28}},
    {29, {Who::kOlaf, 0xcf, tail::kBelowMiddle, -28}},   {29, {Who::kErik, 0xd0, tail::kBelowMiddle, -28}},
    {29, {Who::kOlaf, 0xd1, tail::kBelowMiddle, -28}},   {30, {Who::kOlaf, 0xd2, tail::kBelowLeft, -28}},
    {30, {Who::kBaleog, 0xd3, tail::kBelowLeft, -28}},   {30, {Who::kErik, 0xd4, tail::kBelowLeft, -28}},
    {31, {Who::kOlaf, 0xd5, tail::kBelowRight, -28}},    {31, {Who::kBaleog, 0xd6, tail::kAboveRight, 20}},
    {31, {Who::kErik, 0xd7, tail::kBelowRight, -28}},    {31, {Who::kErik, 0xd8, tail::kBelowRight, -28}},
}};

// After losing again: one of these, picked at random by how often they have lost. A row with text 0 ends one.
// Baleog's and Olaf's tails are where Olaf faces (LevelStart), not as given here.
inline constexpr std::array<std::array<Said, 9>, 16> kRetries{{
    {{{Who::kErik, 0x119, tail::kBelowMiddle, -28},
      {Who::kBaleog, 0x11a, tail::kBelowLeft, -28},
      {Who::kBaleog, 0x11b, tail::kBelowLeft, -28},
      {Who::kOlaf, 0x11c, tail::kBelowLeft, -28}}},
    {{{Who::kErik, 0x11d, tail::kBelowMiddle, -28},
      {Who::kBaleog, 0x11e, tail::kBelowLeft, -28},
      {Who::kOlaf, 0x11f, tail::kBelowLeft, -28}}},
    {{{Who::kOlaf, 0x120, tail::kBelowLeft, -28},
      {Who::kBaleog, 0x121, tail::kBelowLeft, -28},
      {Who::kErik, 0x122, tail::kBelowMiddle, -28},
      {Who::kOlaf, 0x123, tail::kBelowLeft, -28}}},
    {{{Who::kErik, 0x124, tail::kBelowMiddle, -28},
      {Who::kOlaf, 0x125, tail::kBelowLeft, -28},
      {Who::kBaleog, 0x126, tail::kBelowLeft, -28},
      {Who::kErik, 0x127, tail::kBelowMiddle, -28}}},
    {{{Who::kOlaf, 0x128, tail::kBelowLeft, -28},
      {Who::kErik, 0x129, tail::kBelowMiddle, -28},
      {Who::kBaleog, 0x12a, tail::kBelowLeft, -28}}},
    {{{Who::kOlaf, 0x12b, tail::kBelowLeft, -28},
      {Who::kErik, 0x12c, tail::kBelowMiddle, -28},
      {Who::kBaleog, 0x12d, tail::kBelowLeft, -28}}},
    {{{Who::kErik, 0x12e, tail::kBelowMiddle, -28}, {Who::kOlaf, 0x12f, tail::kBelowLeft, -28}}},
    {{{Who::kErik, 0x130, tail::kBelowMiddle, -28},
      {Who::kBaleog, 0x131, tail::kBelowLeft, -28},
      {Who::kOlaf, 0x132, tail::kBelowLeft, -28}}},
    {{{Who::kErik, 0x133, tail::kBelowMiddle, -28},
      {Who::kBaleog, 0x134, tail::kBelowLeft, -28},
      {Who::kOlaf, 0x135, tail::kBelowLeft, -28}}},
    {{{Who::kErik, 0x136, tail::kBelowMiddle, -28},
      {Who::kOlaf, 0x137, tail::kBelowLeft, -28},
      {Who::kErik, 0x138, tail::kBelowMiddle, -28},
      {Who::kBaleog, 0x139, tail::kBelowLeft, -28},
      {Who::kErik, 0x13a, tail::kBelowMiddle, -28}}},
    {{{Who::kBaleog, 0x13b, tail::kBelowLeft, -28},
      {Who::kOlaf, 0x13c, tail::kBelowLeft, -28},
      {Who::kErik, 0x13d, tail::kBelowMiddle, -28},
      {Who::kOlaf, 0x13e, tail::kBelowLeft, -28}}},
    {{{Who::kOlaf, 0x13f, tail::kBelowLeft, -28},
      {Who::kBaleog, 0x140, tail::kBelowLeft, -28},
      {Who::kErik, 0x141, tail::kBelowMiddle, -28},
      {Who::kErik, 0x142, tail::kBelowMiddle, -28},
      {Who::kOlaf, 0x143, tail::kBelowLeft, -28}}},
    {{{Who::kOlaf, 0x144, tail::kBelowLeft, -28},
      {Who::kErik, 0x145, tail::kBelowMiddle, -28},
      {Who::kBaleog, 0x146, tail::kBelowLeft, -28},
      {Who::kErik, 0x147, tail::kBelowMiddle, -28}}},
    {{{Who::kVoice, 0x148, tail::kAboveMiddle, 0},
      {Who::kVoice, 0x149, tail::kAboveMiddle, 0},
      {Who::kErik, 0x14a, tail::kBelowMiddle, -28},
      {Who::kOlaf, 0x14b, tail::kBelowLeft, -28},
      {Who::kVoice, 0x14c, tail::kAboveMiddle, 0},
      {Who::kErik, 0x14d, tail::kBelowMiddle, -28},
      {Who::kVoice, 0x14e, tail::kAboveMiddle, 0},
      {Who::kOlaf, 0x14f, tail::kBelowLeft, -28},
      {Who::kVoice, 0x150, tail::kAboveMiddle, 0}}},
    {{{Who::kOlaf, 0x151, tail::kBelowLeft, -28},
      {Who::kBaleog, 0x152, tail::kBelowLeft, -28},
      {Who::kBaleog, 0x153, tail::kBelowLeft, -28},
      {Who::kErik, 0x154, tail::kBelowMiddle, -28},
      {Who::kErik, 0x155, tail::kBelowMiddle, -28},
      {Who::kBaleog, 0x156, tail::kBelowLeft, -28},
      {Who::kErik, 0x157, tail::kBelowMiddle, -28}}},
    {{{Who::kErik, 0x158, tail::kBelowMiddle, -28},
      {Who::kOlaf, 0x159, tail::kBelowLeft, -28},
      {Who::kBaleog, 0x15a, tail::kBelowLeft, -28}}},
}};

// --- On the ship home (ShipStop) ---

inline constexpr std::array<Said, 7> kShipFirstStop{{
    {Who::kVoice, 0xf1, tail::kAboveMiddle, 0},
    {Who::kBaleog, 0xf2, tail::kAboveRight, 20},
    {Who::kBaleog, 0xf3, tail::kAboveRight, 20},
    {Who::kOlaf, 0xf4, tail::kBelowRight, -28},
    {Who::kBaleog, 0xf5, tail::kAboveRight, 20},
    {Who::kVoice, 0xf6, tail::kAboveMiddle, 0},
    {Who::kVoice, 0xf7, tail::kAboveMiddle, 0},
}};

inline constexpr std::array<Said, 4> kShipSecondStop{{
    {Who::kVoice, 0xf8, tail::kAboveMiddle, 0},
    {Who::kBaleog, 0xf9, tail::kAboveRight, 20},
    {Who::kOlaf, 0xfa, tail::kBelowRight, -28},
    {Who::kVoice, 0xfb, tail::kAboveMiddle, 0},
}};

// ... and what Erik and Olaf say when they are back.
inline constexpr std::array<Said, 2> kShipSecondStopAfter{{
    {Who::kErik, 0xfc, tail::kBelowLeft, -28},
    {Who::kOlaf, 0xfd, tail::kBelowLeft, -28},
}};

// --- With someone met in a level ---

// The spaceship's janitor: the long word, and the short one for who asks again.
inline constexpr std::array<Said, 7> kJanitor{{
    {Who::kViking, 0x26, tail::kAboveMiddle, 24},
    {Who::kHost, 0x27, tail::kBelowMiddle, -24},
    {Who::kViking, 0x28, tail::kAboveMiddle, 24},
    {Who::kHost, 0x29, tail::kBelowMiddle, -24},
    {Who::kViking, 0x2a, tail::kAboveMiddle, 24},
    {Who::kViking, 0x2b, tail::kAboveMiddle, 24},
    {Who::kHost, 0x2c, tail::kBelowMiddle, -24},
}};

// ...
inline constexpr std::array<Said, 2> kJanitorAgain{{
    {Who::kViking, 0x2d, tail::kAboveMiddle, 24},
    {Who::kHost, 0x2e, tail::kBelowMiddle, -24},
}};

// The elder: his whole story, and what he says again.
inline constexpr std::array<Said, 9> kElder{{
    {Who::kViking, 0x07, tail::kAboveMiddle, 24},
    {Who::kHost, 0x08, tail::kBelowMiddle, -24},
    {Who::kHost, 0x09, tail::kBelowMiddle, -24},
    {Who::kViking, 0x0a, tail::kAboveMiddle, 24},
    {Who::kHost, 0x0b, tail::kBelowMiddle, -24},
    {Who::kViking, 0x0c, tail::kAboveMiddle, 24},
    {Who::kHost, 0x0d, tail::kBelowMiddle, -24},
    {Who::kViking, 0x0e, tail::kAboveMiddle, 24},
    {Who::kHost, 0x0f, tail::kBelowMiddle, -24},
}};

// ...
inline constexpr std::array<Said, 3> kElderAgain{{
    {Who::kHost, 0x08, tail::kBelowMiddle, -24},
    {Who::kHost, 0x0b, tail::kBelowMiddle, -24},
    {Who::kHost, 0x0d, tail::kBelowMiddle, -24},
}};

inline constexpr std::array<Said, 6> kPrisoner{{
    {Who::kHost, 0x31, tail::kBelowMiddle, -28},
    {Who::kVikingBeside, 0x32, tail::kAboveMiddle, 20},
    {Who::kHost, 0x33, tail::kBelowMiddle, -28},
    {Who::kHost, 0x34, tail::kBelowMiddle, -28},
    {Who::kVikingBeside, 0x35, tail::kAboveMiddle, 20},
    {Who::kHost, 0x36, tail::kBelowMiddle, -28},
}};

// The caveman: his long story, and the short one.
inline constexpr std::array<Said, 10> kCaveman{{
    {Who::kHost, 0x11, tail::kBelowLeft, -24},
    {Who::kHost, 0x12, tail::kBelowLeft, -24},
    {Who::kVikingBesideHisOwn, 0x13, tail::kAboveLeft, 24},
    {Who::kHost, 0x16, tail::kBelowLeft, -24},
    {Who::kVikingBeside, 0x17, tail::kAboveLeft, 24},
    {Who::kHost, 0x18, tail::kBelowLeft, -24},
    {Who::kHost, 0x19, tail::kBelowLeft, -24},
    {Who::kVikingBeside, 0x1a, tail::kAboveLeft, 24},
    {Who::kHost, 0x1b, tail::kBelowLeft, -24},
    {Who::kHost, 0x1c, tail::kBelowLeft, -24},
}};

// ...
inline constexpr std::array<Said, 5> kCavemanAgain{{
    {Who::kHost, 0x1d, tail::kBelowLeft, -24},
    {Who::kHost, 0x1e, tail::kBelowLeft, -24},
    {Who::kViking, 0x1f, tail::kAboveLeft, 24},
    {Who::kHost, 0x20, tail::kBelowLeft, -24},
    {Who::kHost, 0x21, tail::kBelowLeft, -24},
}};

// Egypt's man to talk to.
inline constexpr std::array<Said, 9> kEgyptian{{
    {Who::kHost, 0x37, tail::kBelowMiddle, -28},
    {Who::kVikingBeside, 0x38, tail::kAboveMiddle, 20},
    {Who::kHost, 0x39, tail::kBelowMiddle, -28},
    {Who::kVikingBeside, 0x3a, tail::kAboveMiddle, 20},
    {Who::kHost, 0x3b, tail::kBelowMiddle, -28},
    {Who::kHost, 0x3c, tail::kBelowMiddle, -28},
    {Who::kHost, 0x3d, tail::kBelowMiddle, -28},
    {Who::kVikingBeside, 0x3e, tail::kAboveMiddle, 20},
    {Who::kHost, 0x3f, tail::kBelowMiddle, -28},
}};

}  // namespace lvr::talk
