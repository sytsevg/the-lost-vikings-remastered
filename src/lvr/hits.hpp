#pragma once

// Which entities touch which: the first other entity whose box overlaps an entity's, by what it is to hit tests;
// and the entities in the row just above or below an entity, by what they act as. Checked against the reference
// engine's (docs/original/hits.md).

#include "lvr/entity.hpp"
#include "lvr/terrain.hpp"
#include "lvr/world.hpp"

namespace lvr {

// The first other entity, in slot order, whose box overlaps this one's (edges included) and whose hit class is
// `hit_class`; its slot, or kNobody. `vikings_only`: among the Vikings' slots.
int find_touching(const World& world, const Entity& entity, std::uint16_t hit_class, bool vikings_only = false);
// The same, for an entity that has any of the bits of `mask` in its hit mask.
int find_touching_any(const World& world, const Entity& entity, std::uint16_t mask, bool vikings_only = false);

enum class Row : std::uint8_t { kAbove, kBelow };

// The next entity from slot `from` on that acts as one of `types`, overlaps this one across, and is at the row
// just above this one's box or just below it. To go through all of them: again with the found slot + 1.
// Looking at all entities, the original wants the other's bottom edge to be above that row (an entity resting on
// this one, or anything higher up); among the Vikings alone, the row to be inside the Viking's box.
int find_in_row(const World& world, const Entity& entity, Row row, const TileTypes& types, bool vikings_only,
                int from = 0);

}  // namespace lvr
