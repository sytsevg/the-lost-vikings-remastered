#pragma once

// How entities move: speed and gravity gathered into a tick's push, and the push taken as whole pixels with the
// rest kept for the next tick. The original's rules: docs/original/terrain.md#movement,
// docs/original/animation.md#the-speed-step-35cf.

#include "lvr/entity.hpp"

namespace lvr {

// An entity's speed into this tick's push. Unless another entity moves it: gravity and the level's push are added
// to its speed if it takes them, the speed is kept within the most it moves in a tick, and added to its push (the
// other way for an entity facing left, or upside down).
void add_speed(Entity& entity, int gravity, int level_push) noexcept;

// The tick's move: where it was is remembered, and an entity that moves itself goes by its push, at most its limit
// each way, in whole pixels. The push then holds the pixels moved.
void move(Entity& entity) noexcept;

// Moves the entity and its box by whole pixels.
void shift(Entity& entity, int dx, int dy) noexcept;

}  // namespace lvr
