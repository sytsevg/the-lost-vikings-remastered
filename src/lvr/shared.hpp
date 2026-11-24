#pragma once

// What the kinds of every world say the same way: the tile types they look for, and the small steps they share
// (gone for good, leaving with their place kept, a level's switches).

#include <cstdint>

#include "lvr/entity.hpp"
#include "lvr/hits.hpp"
#include "lvr/kinds.hpp"
#include "lvr/level.hpp"
#include "lvr/looks.hpp"
#include "lvr/terrain.hpp"
#include "lvr/world.hpp"

namespace lvr {

// --- The tile types the kinds look for ---

inline const TileTypes& solid() {  // walls and floors, and what acts as them
    static const TileTypes types = tile_types({tile::kGround, tile::kGround02, tile::kGround20});
    return types;
}
inline const TileTypes& ground() {  // what can be stood on: floors, platforms, slopes
    static const TileTypes types = tile_types({tile::kGround, tile::kGround02, tile::kLadderTop, tile::kPlatform,
                                               tile::kShieldOverhead, tile::kGround20}) |
                                   slopes();
    return types;
}
inline const TileTypes& walls() {  // what turns an enemy round
    static const TileTypes types =
        tile_types({tile::kGround, tile::kGround02, tile::kBarrier, tile::kLadder0D, tile::kGround20});
    return types;
}
inline const TileTypes& floor_or_raised_shield() {  // what something dropped lands on
    static const TileTypes types = tile_types({tile::kGround, tile::kGround02, tile::kShieldOverhead, tile::kGround20});
    return types;
}
inline const TileTypes& end_of_its_way() {  // where something that comes down on rails stops
    static const TileTypes types = tile_types({tile::kGround, tile::kPlatformStop, tile::kSpikes});
    return types;
}
inline const TileTypes& shield() {  // Olaf's, held before him
    static const TileTypes types = tile_types({tile::kShield});
    return types;
}
inline const TileTypes& shield_overhead() {  // Olaf's, held up
    static const TileTypes types = tile_types({tile::kShieldOverhead});
    return types;
}
inline const TileTypes& vikings() {  // what the Vikings act as
    static const TileTypes types = tile_types({tile::kViking});
    return types;
}

// --- What an entity is to the others' hit tests (Entity::hit_class), and the looks for them ---

// A Viking touches it (looked for among the Vikings' three slots): saw.viking.
constexpr Look touching_a_viking() {
    return touching_viking(hit_class::kViking);
}
// The same, looked for among all the entities: something else of the Vikings' class is found first if its slot
// comes first.
constexpr Look touching_one_of_the_vikings_class() {
    return touching(hit_class::kViking, &Saw::viking);
}
// Another enemy touches it: saw.enemy.
constexpr Look touching_an_enemy() {
    return touching(hit_class::kEnemy, &Saw::enemy);
}

// --- Small steps ---

// Tells an entity something (message::), with a number that goes with it. It answers at its next turn.
inline void tell(Entity& whom, std::uint16_t what, int with) {
    whom.message = what;
    whom.message_value = static_cast<std::uint16_t>(with);
}

// Tells an entity it was harmed: by what (message::kStruck, kShot, kSwordBlow ...), how much health that costs, and
// from which side it came. It answers at its next turn.
inline void harm(Entity& whom, std::uint16_t what, std::uint16_t how_much, bool from_the_right) {
    whom.message = what;
    whom.message_value = static_cast<std::uint16_t>(how_much | (from_the_right ? kFromTheRight : 0U));
}

// Whether all of those switches of the level are on.
inline bool switches_on(const World& world, std::uint16_t which) {
    return (world.switches & which) == which;
}

// The cell of the map a point is in; nullptr outside the map.
inline Cell* cell_at(World& world, int x, int y) {
    return world.map.cell(cell_of(x), cell_of(y));
}

// Its placement never makes it again (it was collected, destroyed, used up).
inline void never_again(World& world, const Entity& entity) {
    if (entity.placement >= 0) {
        world.gone.set(static_cast<std::size_t>(entity.placement));
    }
}

// Removed, and never made again.
inline void gone_for_good(World& world, const Entity& entity) {
    never_again(world, entity);
    world.remove(entity.slot);
}

// Removed because it left the active area: where it was goes into its placement, which makes it again there.
inline void leave(World& world, Entity& entity) {
    if (entity.placement >= 0) {
        Placement& mine = world.placements.at(static_cast<std::size_t>(entity.placement));
        mine.x = entity.x;
        mine.y = entity.y;
        mine.flags = entity.flags & entity_flag::kKeptInPlacement;
    }
    world.remove(entity.slot);
}

// A Viking asked it something (the action key beside it): true, and he is its target. Anything else it was told
// is forgotten.
inline bool was_asked(Entity& entity) {
    const bool asked = entity.message == message::kAsked;
    entity.message = 0;
    if (asked) {
        entity.target = entity.message_value;
    }
    return asked;
}

// Whether the three Vikings are all near an entity: within 32 pixels down, and `across` pixels across. One who is gone
// counts as there, and `missing` says so.
inline bool all_vikings_here(const World& world, Entity& entity, bool& missing, int across = 32) {
    for (const int viking : {slot::kBaleog, slot::kErik, slot::kOlaf}) {  // in the original's order
        entity.target = viking;
        const Entity* he = world.at(viking);
        if (he == nullptr || he->dead) {
            missing = true;
            continue;
        }
        const World::Offset to = world.offset_to(entity, viking);
        if (to.below >= 32 || to.below < -32 || to.ahead >= across || to.ahead < -across) {
            return false;
        }
    }
    return true;
}

// Something a Viking can ride stays active while one stands on it (unless it always is: `stays_active`).
inline void stay_active_under_a_viking(const World& world, Entity& entity, bool stays_active) {
    if (stays_active) {
        return;
    }
    const bool ridden = find_viking_beside(world, entity, Side::kAbove, vikings()).has_value();
    entity.stay_active(ridden);
}

// A blow's strength against its health: true if it lives (the blow is taken off its health and forgotten), false
// if this one kills it. Which side it came from is no longer in message_value afterwards.
inline bool survives_blow(Entity& entity) {
    entity.forget_blow_side();
    if (entity.message_value >= entity.health) {
        return false;
    }
    entity.health -= entity.message_value;
    entity.message = 0;
    entity.message_value = 0;
    return true;
}

// --- What the blocks a Viking pushes share ---

// A Viking's push (message::kPushed; with it: how fast, and which way): the block goes by it unless a wall is in
// the way; a Viking on its far side is pushed along, or crushed against what is behind him. True if it went.
inline bool take_push(World& world, Entity& block) {
    const auto by = static_cast<std::int16_t>(block.message_value);
    const Side side = by < 0 ? Side::kBehind : Side::kAhead;
    if (find_beside(world, block, side, solid())) {
        return false;
    }
    block.push_x += by;
    const auto in_the_way = find_viking_beside(world, block, side, vikings());
    if (!in_the_way || in_the_way->entity < 0) {
        return true;
    }
    block.target = in_the_way->entity;
    if (Entity* other = world.at(block.target)) {
        other->message = message::kCrushed;
        other->message_value =
            static_cast<std::uint16_t>(block.speed_x + (block.kind_value == 0 ? block.push_x : block.kind_value));
        block.kind_value = 0;
    }
    return true;
}

// The scrape of a block being pushed: one sound for all the blocks of a level, on while any is pushed.
inline void stop_scraping(World& world, const Entity& block) {
    world.stop_sound(block, effect::kScrape);
    world.pushing_sound = false;
}
inline void scrape_while_pushed(World& world, const Entity& block, bool pushed) {
    if (!pushed) {
        return stop_scraping(world, block);
    }
    if (!world.pushing_sound) {
        world.sound(block, effect::kScrape);
        world.pushing_sound = true;
    }
}

// --- What the ones to talk to share ---

// Whether the nearest Viking has come up to it: less than `ahead_within` pixels ahead of it, no further than
// `behind_within` behind it (a negative number), and level with it within 5 pixels. He is then the one it deals
// with (Entity::target).
inline bool viking_comes_near(World& world, Entity& entity, int ahead_within, int behind_within) {
    const int nearest = world.nearest_viking(entity);
    const World::Offset to = world.offset_to(entity, nearest);
    if (to.ahead >= ahead_within || to.ahead < behind_within || to.below >= 5 || to.below < -5) {
        return false;
    }
    entity.target = nearest;
    return true;
}

// --- The Vikings on top of something, or under it ---

// The Vikings in the row just above an entity (its riders) or just below it, one after the other, for a range-for:
// each is looked for only when the one before has been dealt with, and is then who the entity deals with
// (Entity::target).
//     for (const int rider : vikings_in_row(world, *this, Row::kAbove)) { ... }
class VikingsInRow {
  public:
    VikingsInRow(const World& world, Entity& entity, Row row) : world_{world}, entity_{entity}, row_{row} {}

    struct End {};
    class Iterator {
      public:
        Iterator(const VikingsInRow& row, int slot) : row_{row}, slot_{slot} {}
        int operator*() const {
            row_.entity_.target = slot_;
            return slot_;
        }
        Iterator& operator++() {
            slot_ = row_.find(slot_ + 1);
            return *this;
        }
        bool operator!=(End /*end*/) const { return slot_ != kNobody; }

      private:
        const VikingsInRow& row_;
        int slot_;
    };
    Iterator begin() const { return {*this, find(0)}; }
    static End end() { return {}; }

  private:
    int find(int from) const { return find_in_row(world_, entity_, row_, vikings(), true, from); }

    const World& world_;
    Entity& entity_;
    Row row_;
};
inline VikingsInRow vikings_in_row(const World& world, Entity& entity, Row row) {
    return {world, entity, row};
}

// Something going down takes the Vikings standing right on it along.
inline void take_riders_down(World& world, Entity& entity) {
    for (const int rider : vikings_in_row(world, entity, Row::kAbove)) {
        Entity* other = world.at(rider);
        if (other != nullptr && other->box.bottom + 1 == entity.box.top) {
            other->push_y += entity.speed_y;
        }
    }
}

// --- What comes down on a Viking ---

// The entity comes down on the Viking in `under`'s slot, if it is moving down and he isn't deeper in it than 16
// pixels: he is told he is squashed, and how fast it came.
inline void squash(World& world, const Entity& entity, int under) {
    Entity* other = world.at(under);
    if (other == nullptr || entity.speed_y <= 0 || other->hit_class != hit_class::kViking ||
        other->box.top < entity.box.bottom - 16) {
        return;
    }
    other->message_value = static_cast<std::uint16_t>(entity.speed_y);
    other->message = message::kSquashed;
}

// Every Viking right under it is squashed that way. (The last one looked at is who it deals with, Entity::target.)
inline void squash_below(World& world, Entity& entity) {
    for (const int under : vikings_in_row(world, entity, Row::kBelow)) {
        squash(world, entity, under);
    }
}

// --- What shots share ---

// Whether what a shot touches (saw.shield) stops it: Olaf's shield does, unless it faces the way the shot flies
// (then it is turned away from it and the shot goes past); anything else with that bit does.
inline bool stopped_by_what_it_touches(const World& world, Entity& shot) {
    if (shot.saw.shield == kNobody) {
        return false;
    }
    shot.target = shot.saw.shield;
    const Entity* other = world.at(shot.saw.shield);
    return other == nullptr || other->kind != kind::kShield || other->facing_left() != shot.facing_left();
}

// A shot hits the Viking it touches (saw.viking): he is told, and a spark shows at the shot's height, `spark_x`
// across.
inline void hit_viking_touched(World& world, Entity& shot, int spark_x) {
    shot.target = shot.saw.viking;
    if (Entity* viking = world.at(shot.saw.viking)) {
        harm(*viking, message::kShot, 2, shot.facing_left());
    }
    world.spawn(shot, kind::kSpark, spark_x, shot.y, 0, entity_flag::kLargePieces);
}

// Something shot straight ahead: it flies until a wall, Olaf's shield held towards it or a Viking stops it, and
// then bursts or is gone. A kind says how it is fired, what it does to a Viking, and how it bursts.
class StraightShot : public Entity {
  public:
    void update(World& world) final {
        switch (shot.state) {
            case State::kStart:
                fired(world);
                shot.state = State::kFlying;
                break;
            case State::kFlying:
                if (wall_stops_it_first() && saw.wall) {
                    return burst(world);
                }
                if (stopped_by_what_it_touches(world, *this)) {
                    return burst(world);
                }
                if (saw.viking != kNobody) {
                    return hit_viking(world);
                }
                if (saw.wall) {
                    return burst(world);
                }
                if (world.rules == Rules::kRemaster && under_slope(world, x, y)) {
                    return burst(world);  // (zoomed out, a shot is seen to fly on into a hill)
                }
                still_flying(world);
                break;
            case State::kEnding:
                if (animation_over()) {
                    world.remove(slot);
                    return;
                }
                break;
        }
        world.animate(*this);
    }
    Looks looks_for() const final {
        return shot.state == State::kFlying ? Looks{stopped_by(solid()), touching_any(hit::kShield, &Saw::shield),
                                                    touching_any(hit::kViking, &Saw::viking)}
                                            : Looks{};
    }

  protected:
    virtual void fired(World& world) = 0;       // its first turn: its animation and speed
    virtual void hit_viking(World& world) = 0;  // saw.viking: it hurts him, and bursts or is gone
    virtual void bursting(World& world) = 0;    // the sound and the animation of its end
    virtual void still_flying(World& /*world*/) {}
    virtual bool wall_stops_it_first() const { return true; }  // before a Viking in the same place is hit

    void burst(World& world) {
        bursting(world);
        shot.state = State::kEnding;
        world.animate(*this);
    }
    // It ends with an animation, without the rest of bursting. The turn goes on.
    void end_with(World& world, Animation animation) {
        world.play(*this, animation);
        shot.state = State::kEnding;
    }

  private:
    enum class State : std::uint8_t { kStart, kFlying, kEnding };
    struct Saved {
        State state = State::kStart;
    } shot;

  protected:
    LVR_SAVES_AS(Entity, shot)
};

}  // namespace lvr
