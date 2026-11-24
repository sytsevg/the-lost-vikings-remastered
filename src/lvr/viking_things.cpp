// The Vikings and the things around them: acting on what is there, and the items they carry.
#include "lvr/viking.hpp"

namespace lvr {

// --- Acting on things, items ---

bool Viking::act(World& world) {
    const TileTypes& thing = types().thing;
    if (!find_beside(world, *this, Side::kAhead, thing) && !find_beside(world, *this, Side::kBehind, thing) &&
        !find_at(world, *this, x, box.top, thing) && !find_at(world, *this, x, y, thing)) {
        return false;
    }
    if (is_falling()) {
        act_on(world);
        return true;
    }
    keep_on_ground();
    world.animate(*this);
    wait(At::kActing, {Test::kWall, Test::kDeadly, Test::kTouchesThing});
    return true;
}

void Viking::act_on(World& world) {
    world.animate(*this);
    if (speed_y > 0) {
        wait(At::kActed, {Test::kWall, Test::kLands, Test::kDeadly, Test::kTouchesThing});
    } else {
        wait(At::kActed, {Test::kWall, Test::kFloorOrCeiling, Test::kDeadly, Test::kTouchesThing});
    }
}

void Viking::ask(World& world) {
    target = s.touched;
    if (Entity* thing = world.at(s.touched)) {
        if (thing->kind == kind::kTeleporter) {
            return step_into(world, *thing);
        }
        if (thing->kind == factory_kind::kCraneControls && world.database == database::kFactory) {
            return take_controls(world);
        }
        if (thing->kind == wacky_kind::kPump && world.database == database::kWacky) {
            return use_pump(world, *thing);
        }
        if (thing->kind == wacky_kind::kPump && world.database == database::kFactory) {
            // A quirk of the original: the factory's Vikings still test for the pump, and the test leads
            // nowhere: the Viking is gone. (No factory level has one.)
            world.remove(slot);
            return;
        }
        tell(*thing, message::kAsked, slot);
    }
    after_acting(world);
}

// --- Items ---

void Viking::after_item(World& world) {
    if (inflated) {
        return inflated_steer(world);
    }
    switch (s.doing) {
        case Doing::kHurt:
        case Doing::kHurtFalling:
            return hurt_after_item(world);
        case Doing::kDizzy:
        case Doing::kDizzyRising:
        case Doing::kDizzyFalling:
            return dizzy_after_item(world);
        case Doing::kFalling:
            return falling_after_item(world);
        case Doing::kClimbing:
            return climbing(world);
        case Doing::kHanging:
            return hanging_after_item(world);
        case Doing::kCharging:
            return charging_after_item(world);
        case Doing::kHeadHit:
            return head_hit_after_item(world);
        case Doing::kBouncing:
        case Doing::kBouncingAgain:
            return bouncing_after_item(world);
        case Doing::kGettingUp:
        case Doing::kStoppingCharge:
            return on_his_feet_after_item(world);
        default:
            return standing_after_item(world);
    }
}

std::uint16_t& Viking::chosen_item(World& world) const {
    const auto who = static_cast<std::size_t>(slot);
    return world.items.at(who).at(static_cast<std::size_t>(world.chosen_square.at(who)));
}

void Viking::used_up(World& world) const {
    chosen_item(world) = 0;
    const auto who = static_cast<std::size_t>(slot);
    int& chosen = world.chosen_square.at(who);
    for (int next = chosen;;) {
        if (world.items.at(who).at(static_cast<std::size_t>(next)) != 0) {
            chosen = next;
            return;
        }
        next = (next + 1) % World::kSquares;
        if (next == chosen) {
            return;
        }
    }
}

bool Viking::use_item(World& world) {
    if (slot < 0 || slot >= kVikingSlots) {
        return false;
    }
    const std::uint16_t item = chosen_item(world);
    std::uint16_t& armour = world.armour.at(static_cast<std::size_t>(slot));
    if (s.who == kind::kBaleog && item == item::kFireArrows) {
        if (s.has_fire_arrows) {
            sound(world, effect::kNothingToUse);
            return false;
        }
        s.has_fire_arrows = true;
        sound(world, effect::kItemUsed);
        used_up(world);
        return false;
    }
    switch (item) {
        case item::kFood1:
        case item::kFood2:
        case item::kFood4:
        case item::kBigFood: {  // food: two points of health, the fourth kind four; none when he has all he can
                                // have
            const auto most = static_cast<std::uint16_t>(kFullHealth + armour);
            if (health >= most) {
                sound(world, effect::kNothingToUse);
                return false;
            }
            health += (item == item::kBigFood ? 4 : 2);
            sound(world, effect::kEaten);
            if (static_cast<std::int16_t>(health) >= static_cast<std::int16_t>(most)) {
                health = most;
            }
            used_up(world);
            return false;
        }
        case item::kBomb:  // a bomb, put down where he stands
            world.spawn(*this, kind::kLitBomb, x, y);
            used_up(world);
            return false;
        case item::kKey:
        case item::kSecondKey:
        case item::kThirdKey:  // the three keys: each works the lock of its kind beside him
            return use_key(world, static_cast<std::uint16_t>(item + 1));
        case item::kShield:  // the shield: a point of health on top of his own
            if (armour == kArmour) {
                sound(world, effect::kNothingToUse);
                return false;
            }
            health = static_cast<std::uint16_t>(health - armour + kArmour);
            armour = kArmour;
            sound(world, effect::kItemUsed);
            used_up(world);
            return false;
        case item::kSmartBomb:
            world.spawn(*this, kind::kSmartBomb, x, y, 0, entity_flag::kLargePieces);
            sound(world, effect::kItemUsed);
            used_up(world);
            return false;
        case item::kBoots:  // the boots that keep his feet down where there is no gravity
            ignores_weightlessness = true;
            sound(world, effect::kItemUsed);
            used_up(world);
            return false;
        default:
            if (item < item::kFirstWanted || item > item::kLastWanted) {
                sound(world, effect::kNothingToUse);
                return false;
            }
            // A thing a level asks for: handed to what wants this one, under it.
            if (!find_beside(world, *this, Side::kAbove, types().taker)) {
                sound(world, effect::kNothingToUse);
                return false;
            }
            s.touch_class = hit_class::kTaker;
            wait(At::kHandingOver, {Test::kTouchesClass});
            return true;
    }
}

bool Viking::use_key(World& world, std::uint16_t lock_class) {
    const TileTypes& lock = types().lock;
    if (!find_beside(world, *this, Side::kAhead, lock) && !find_beside(world, *this, Side::kBehind, lock) &&
        !find_at(world, *this, x, box.top, lock) && !find_at(world, *this, x, y, lock)) {
        sound(world, effect::kNothingToUse);
        return false;
    }
    world.animate(*this);
    s.touch_class = lock_class;
    if (speed_y > 0) {
        wait(At::kUsingKey, {Test::kWall, Test::kFloorOrCeiling, Test::kDeadly, Test::kTouchesClass});
    } else {
        wait(At::kUsingKey, {Test::kWall, Test::kLands, Test::kDeadly, Test::kTouchesClass});
    }
    return true;
}

void Viking::hand_over(World& world) {
    target = s.touched;
    world.tell(s.touched, message::kAsked);
    sound(world, effect::kItemUsed);
    used_up(world);
}

// A turn of using a thing: a key, an item handed over, the action key's other uses.
void Viking::resume_acting(World& world) {
    switch (s.at) {
        case At::kUsingKey:
            if (found(Test::kWall)) {
                speed_x = 0;
            }
            if (landed() || found(Test::kFloorOrCeiling)) {
                speed_y = 0;
            }
            if (found(Test::kDeadly) && deadly(world)) {
                return;
            }
            if (found(Test::kTouchesClass)) {
                hand_over(world);
            }
            return after_item(world);
        case At::kHandingOver:
            if (found(Test::kTouchesClass)) {
                const Entity* taker = world.at(s.touched);
                if (taker != nullptr && taker->wanted_item == chosen_item(world)) {
                    hand_over(world);
                } else {
                    target = s.touched;
                }
            }
            return after_item(world);
        case At::kActing:
            if (found(Test::kWall)) {
                speed_x = 0;
            }
            if (found(Test::kDeadly) && deadly(world)) {
                return;
            }
            if (found(Test::kTouchesThing)) {
                return ask(world);
            }
            return act_on(world);
        case At::kActed:
            if (found(Test::kWall)) {
                speed_x = 0;
            }
            if (landed() || found(Test::kFloorOrCeiling)) {
                speed_y = 0;
            }
            if (found(Test::kDeadly) && deadly(world)) {
                return;
            }
            if (found(Test::kTouchesThing)) {
                return ask(world);
            }
            return after_acting(world);
        default:
            return;
    }
}

}  // namespace lvr
