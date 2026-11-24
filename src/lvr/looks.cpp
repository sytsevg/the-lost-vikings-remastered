#include "lvr/looks.hpp"

#include "lvr/entity.hpp"
#include "lvr/hits.hpp"
#include "lvr/terrain.hpp"
#include "lvr/world.hpp"

namespace lvr {

void Entity::after_move(World& world) {
    // Two looks may share an answer (landing on ground, or on something deadly): it is true if either is. Each
    // look is made whatever the one before it found.
    saw.wall = saw.ceiling = saw.landed = saw.shield_wall = saw.on_shield = saw.held = false;
    for (const Look& look : looks_for()) {
        if (look.unless_answered && saw.*look.stopped) {
            continue;
        }
        switch (look.what) {
            case Look::For::kWall:
                saw.*look.stopped = stop_at_wall(world, *this, *look.types) || saw.*look.stopped;
                break;
            case Look::For::kCeilingOrFloor:
                saw.*look.stopped = stop_at_floor_or_ceiling(world, *this, *look.types) || saw.*look.stopped;
                break;
            case Look::For::kLanding:
                saw.*look.stopped = land(world, *this, *look.types) || saw.*look.stopped;
                break;
            case Look::For::kTouching:
            case Look::For::kTouchingAny:
                break;  // later in the tick
        }
    }
}

void Entity::check_hits(World& world) {
    saw.viking = saw.enemy = saw.shield = saw.shield_overhead = saw.other = saw.another = kNobody;
    for (const Look& look : looks_for()) {
        if (look.what == Look::For::kTouching) {
            saw.*look.who = find_touching(world, *this, look.hits, look.vikings_only);
        } else if (look.what == Look::For::kTouchingAny) {
            saw.*look.who = find_touching_any(world, *this, look.hits, look.vikings_only);
        }
    }
}

}  // namespace lvr
