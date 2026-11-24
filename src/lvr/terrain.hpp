#pragma once

// Terrain: what an entity finds around it (cells of some tile types, or other entities that act as such cells), and
// how the map stops it: pushed out of walls, floors and ceilings, landing on slopes, put on the ground. The
// original's rules: docs/original/terrain.md.

#include <bitset>
#include <initializer_list>
#include <optional>

#include "lvr/entity.hpp"
#include "lvr/world.hpp"

namespace lvr {

// A set of tile types to look for. Entities act as a tile type too (Entity::acts_as); the numbers from 0x28 on are
// theirs alone.
using TileTypes = std::bitset<256>;
TileTypes tile_types(std::initializer_list<int> types);
// The slopes the kinds' lists name (0x30-0x35): added to a set with `|`.
const TileTypes& slopes();

// What a test found: a cell's tile type; or an entity, and what it acts as.
struct Found {
    std::uint8_t tile_type = 0;
    int entity = -1;  // its slot; -1: a cell

    bool operator==(const Found&) const = default;
};

enum class Side : std::uint8_t { kAbove, kBelow, kBehind, kAhead };

// Just outside one side of the entity's box: the row above or below it across its width, or the column behind or
// ahead of it down its height (as it was before this tick's vertical move). Cells first, then entities.
std::optional<Found> find_beside(const World& world, const Entity& entity, Side side, const TileTypes& types);
// The same, looking only for the Vikings.
std::optional<Found> find_viking_beside(const World& world, const Entity& entity, Side side, const TileTypes& types);
// The point diagonally ahead of its feet.
std::optional<Found> find_ahead_below(const World& world, const Entity& entity, const TileTypes& types);
// A point.
std::optional<Found> find_at(const World& world, const Entity& entity, int x, int y, const TileTypes& types);
// Whether it can walk on: 16 pixels ahead there is a slope at its feet, a step up with room above it, or nothing
// at its feet and ground or a slope under that.
bool can_walk_on(const World& world, const Entity& entity);

// After the tick's move: stopped by a wall of these types it moved into, or by such an entity: pushed back to its
// edge. True if it was.
bool stop_at_wall(const World& world, Entity& entity, const TileTypes& types);
// The same up and down: a ceiling or a floor.
bool stop_at_floor_or_ceiling(const World& world, Entity& entity, const TileTypes& types);
// Moving down: lands on a slope's surface (if the types have slopes), on the top of a cell row it crossed into, or
// on such an entity its feet passed.
bool land(const World& world, Entity& entity, const TileTypes& types);

// An entity that asked for it (entity_flag::kPutOnGround) and isn't moving up: up a step, onto a slope's surface,
// or down onto the ground in the cell under its feet. A Viking standing on an entity stays.
void put_on_ground(const World& world, Entity& entity);

}  // namespace lvr
