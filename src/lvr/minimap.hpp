#pragma once

// The minimap (ADR-0011, Remaster rules): the level drawn small from what its cells are to the entities (ground, a
// ladder, something deadly), and how much of it a view has shown. Just a map, to look something up on. It is made
// from a world and decides nothing in it: what was seen is the player's, not the simulation's. What of it is
// shown, where, and the Vikings on it are the renderer's (gpu.*): a square of it around the view.

#include <cstdint>
#include <vector>

#include "lvr/world.hpp"

namespace lvr {

inline constexpr int kMinimapCell = 4;  // pixels to a cell of the map (16 x 16 in the level)

struct Minimap {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;  // red, green, blue a pixel, and whether it was seen (255) or not (0)

    bool operator==(const Minimap&) const = default;
};

// What the world's view shows now is seen from here on. `seen` has a flag for every pixel of the minimap, row by
// row (so the edge of what was seen moves as finely as the minimap is drawn); it is made the map's size if it
// isn't. Whether that changed anything (a pixel seen for the first time, or `seen` made anew).
bool reveal(std::vector<bool>& seen, const World& world, int player = 0);

// The whole map, each pixel with whether it was seen.
Minimap draw_minimap(const World& world, const std::vector<bool>& seen);

}  // namespace lvr
