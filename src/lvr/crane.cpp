// The crane: a magnet the Vikings drive from a control panel, what it lifts, and the trolley that runs with it.

#include <cstdint>
#include <memory>
#include <span>

#include "lvr/enemies.hpp"
#include "lvr/hits.hpp"
#include "lvr/kinds.hpp"
#include "lvr/shared.hpp"
#include "lvr/terrain.hpp"
#include "lvr/world.hpp"

namespace lvr {

namespace {

constexpr std::uint16_t kMagnetOn = 0x0001;  // a crane's controls (World::cranes)

constexpr std::uint16_t kGoLeft = 0x0002;

constexpr std::uint16_t kGoRight = 0x0004;

std::uint16_t& controls(World& world, std::uint16_t crane) {
    return world.factory.cranes.at(crane < 2 ? crane : 2);
}

// A Viking beside something that is carried along: he is pushed, or crushed against a wall.
void crush_beside(World& world, Entity& entity, Side side) {
    const auto in_the_way = find_viking_beside(world, entity, side, vikings());
    if (!in_the_way || in_the_way->entity < 0) {
        return;
    }
    entity.target = in_the_way->entity;
    if (Entity* other = world.at(entity.target)) {
        other->message = message::kCrushed;
        other->message_value =
            static_cast<std::uint16_t>(entity.speed_x + (entity.kind_value == 0 ? entity.push_x : entity.kind_value));
        entity.kind_value = 0;
    }
}

void squash_all_below(World& world, Entity& entity) {
    for (const int under : vikings_in_row(world, entity, Row::kBelow)) {
        squash(world, entity, under);
    }
}

// The magnet: it runs along its rail while the Viking at its controls holds left or right, and while it is
// switched on it pulls up what is magnetic under it (crates, and Olaf by his raised shield), holds it against
// itself and carries it along; switched off, or moved away from, it lets go.
class Magnet : public Entity {
  public:
    void update(World& world) override {
        const std::uint16_t asked = controls(world, argument);
        const bool on = (asked & kMagnetOn) != 0;
        switch (state_) {
            case State::kStart:
                world.play(*this, anim::kMagnetAppears);
                world.animate(*this);
                world.play(*this, anim::kMagnetIsOff);
                return stand_off(world);
            case State::kOff:
                if (on) {
                    world.play(*this, anim::kMagnetIsOn);
                    hum(world, true);
                    return stand_on(world);
                }
                if ((asked & kGoLeft) != 0) {
                    return set_off(world, kGoLeft);
                }
                if ((asked & kGoRight) != 0) {
                    return set_off(world, kGoRight);
                }
                return;
            case State::kOn:
                if (!on) {
                    world.play(*this, anim::kMagnetIsOff);
                    hum(world, false);
                    return stand_off(world);
                }
                if ((asked & kGoLeft) != 0 || (asked & kGoRight) != 0) {
                    world.sound(*this, effect::kMotor);
                    return carry(world, (asked & kGoLeft) != 0 ? kGoLeft : kGoRight);
                }
                return stand_on(world);
            case State::kGoingOff:
                if ((saw.wall && going_ == kGoRight) || (asked & going_) == 0) {
                    speed_x = 0;
                    world.stop_sound(*this, effect::kMotor);
                    state_ = State::kOff;
                    return;
                }
                if (on) {
                    world.play(*this, anim::kMagnetIsOn);
                    hum(world, true);
                    world.sound(*this, effect::kMotor);
                    return carry(world, going_);
                }
                return go(world, going_);
            case State::kGoingOn:
                if ((asked & going_) == 0) {
                    speed_x = 0;
                    world.stop_sound(*this, effect::kMotor);
                    return stand_on(world);
                }
                if (!on) {
                    return set_off(world, going_);
                }
                return carry(world, going_);
        }
    }
    Looks looks_for() const override {
        return state_ == State::kGoingOff && going_ == kGoRight ? Looks{stopped_by(rail_ends())} : Looks{};
    }
    void outside_active_area(World& world) override {
        world.stop_sound(*this, effect::kMotor);
        hum(world, false);
        leave(world, *this);
    }

  private:
    static constexpr std::int16_t kSpeed = px(4);
    static constexpr std::int16_t kPull = px(-3);

    // One hum for all the magnets that are on.
    static void hum(World& world, bool on) {
        if (on) {
            if (++world.factory.magnets_on == 1) {
                world.sound(effect::kMagnetHums);
            }
        } else if (world.factory.magnets_on != 0) {
            --world.factory.magnets_on;
            if (world.factory.magnets_on == 0) {
                world.stop_sound(effect::kMagnetHums);
            }
        }
    }

    static const TileTypes& rail_ends() {
        static const TileTypes types = tile_types({tile::kGround, tile::kPlatformStop});
        return types;
    }
    bool at_the_end(const World& world, std::uint16_t way) const {
        return find_at(world, *this, way == kGoLeft ? box.left - 3 : box.right + 1, y, rail_ends()).has_value();
    }

    // Standing with the magnet off: what it held is let go.
    void stand_off(World& world) {
        world.animate(*this);
        let_go(world);
        stay_active(false);
        state_ = State::kOff;
    }

    void stand_on(World& world) {
        stay_active(false);
        pull_olaf(world);
        pull_crates(world);
        controls(world, argument) &= kMagnetOn;
        world.animate(*this);
        state_ = State::kOn;
    }

    // On its way with the magnet off.
    void set_off(World& world, std::uint16_t way) {
        let_go(world);
        stay_active(false);
        hum(world, false);
        world.sound(*this, effect::kMotor);
        world.play(*this, anim::kMagnetIsOff);
        go(world, way);
    }
    void go(World& world, std::uint16_t way) {
        going_ = way;
        speed_x = static_cast<std::int16_t>(way == kGoLeft ? -kSpeed : kSpeed);
        if (at_the_end(world, way)) {
            speed_x = 0;
        }
        controls(world, argument) &= kMagnetOn;
        world.animate(*this);
        state_ = State::kGoingOff;
    }

    // On its way with the magnet on: what it holds goes with it, unless it is at the end of its rail.
    void carry(World& world, std::uint16_t way) {
        going_ = way;
        blocked_ = false;
        speed_x = static_cast<std::int16_t>(way == kGoLeft ? -kSpeed : kSpeed);
        if (at_the_end(world, way)) {
            blocked_ = true;
            speed_x = 0;
        }
        stay_active(false);
        pull_olaf(world);
        pull_crates(world);
        controls(world, argument) &= kMagnetOn;
        world.animate(*this);
        state_ = State::kGoingOn;
    }

    void let_go(World& world) {
        let_go_of_olaf(world);
        for (int slot_of = kVikingSlots; slot_of < world.slots() - 1; ++slot_of) {
            Entity* crate = world.at(slot_of);
            if (crate == nullptr || (crate->hit_mask & hit::kWeight) == 0 || !crate->held || y >= crate->box.bottom ||
                crate->box.top >= box.bottom + 64) {
                continue;
            }
            crate->held = false;
            crate->start_falling();
        }
    }

    void let_go_of_olaf(World& world) {
        if (!holds_olaf_) {
            return;
        }
        holds_olaf_ = false;
        target = slot::kOlaf;
        if (Entity* olaf = world.at(slot::kOlaf)) {
            olaf->message = message::kLetGo;
            olaf->start_falling();
        }
    }

    // Olaf under it with his shield up: pulled up by it, then held, hanging from it.
    void pull_olaf(World& world) {
        target = slot::kOlaf;
        Entity* olaf = world.at(slot::kOlaf);
        if (olaf == nullptr) {
            return;
        }
        const bool under = olaf->x < box.right && box.left < olaf->x;
        if (!holds_olaf_) {
            if (!olaf_holds_shield_up(world) || olaf_shield_away(world)) {
                return;
            }
            if (!under) {
                return;
            }
            if (box.bottom + 19 < olaf->y) {
                stay_active();
                if (!olaf->is_falling()) {
                    olaf->message = message::kDropped;
                }
                olaf->speed_y = kPull;
                return;
            }
            if (box.bottom + 11 >= olaf->y) {
                return;
            }
            holds_olaf_ = true;
            olaf->clear_flag(entity_flag::kFalls);
            olaf->message = message::kHeldInTheAir;
            olaf->speed_y = 0;
        }
        if (!olaf_holds_shield_up(world) || !under) {
            return let_go_of_olaf(world);
        }
        stay_active();
        const int off = box.bottom + 18 - olaf->y;
        if (off != 0) {
            olaf->push_y += static_cast<std::int16_t>(off * 256);
        }
        if (!blocked_) {
            olaf->push_x += (facing_left() ? -speed_x : speed_x);
            olaf->speed_x = 0;
        }
    }

    void pull_crates(World& world) {
        for (int slot_of = kVikingSlots; slot_of < world.slots() - 1; ++slot_of) {
            Entity* crate = world.at(slot_of);
            if (crate == nullptr || (crate->hit_mask & hit::kWeight) == 0) {
                continue;
            }
            target = slot_of;
            if (!crate->held) {
                if (crate->box.left >= x || x >= crate->box.right || y >= crate->box.bottom) {
                    continue;
                }
                if (box.bottom < crate->box.top) {  // under it, not there yet: pulled up
                    if (!crate->is_falling()) {
                        crate->message = message::kDropped;
                    }
                    crate->speed_y = kPull;
                    stay_active();
                    continue;
                }
                if (box.bottom - 8 >= crate->box.top) {
                    continue;
                }
                world.sound(*this, effect::kThud);
                crate->held = true;
                crate->clear_flag(entity_flag::kFalls);
                crate->speed_y = 0;
            }
            if (y >= crate->box.bottom || crate->box.top >= box.bottom + 64) {
                continue;
            }
            if (crate->box.left >= x || x >= crate->box.right) {  // moved from under it
                crate->held = false;
                crate->start_falling();
                continue;
            }
            stay_active();
            const int off = box.bottom + 1 - crate->box.top;
            if (off != 0) {
                crate->push_y += static_cast<std::int16_t>(off * 256);
            }
            if (!blocked_) {
                crate->push_x += (facing_left() ? -speed_x : speed_x);
                crate->kind_value = static_cast<std::uint16_t>(speed_x);
                crate->speed_y = 0;
            }
        }
    }

    enum class State : std::uint8_t { kStart, kOff, kOn, kGoingOff, kGoingOn };

    State state_ = State::kStart;
    std::uint16_t going_ = 0;  // kGoLeft or kGoRight
    bool blocked_ = false;     // at the end of its rail
    bool holds_olaf_ = false;
};

// A crate: the Vikings push it (off an edge it falls, squashing a Viking under it), and a magnet pulls it up
// and carries it. One with switches is gone, or fixed in its place, once they are on.
class Crate : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                return at_start(world);
            case State::kResting:
                return when_resting(world);
            case State::kFalling:
                return when_falling(world);
            case State::kRising:
                return when_rising(world);
            case State::kHeld:
                return when_held(world);
            case State::kDone:
                return;
        }
    }
    void at_start(World& world) {
        stays_active_ = stays_active();
        if (facing_left()) {
            world.flip_across(*this);
        }
        world.play(*this, anim::kCrateLooks);
        world.animate(*this);
        acts_as = tile::kGround;
        if (argument != 0) {
            stays_when_done_ = (argument & kTopBit) != 0;
            argument &= ~kTopBit;
            if (switches_on(world, argument)) {
                return done(world);
            }
        }
        if (is_falling()) {
            return fall(world);
        }
        return rest(world);
    }
    void when_resting(World& world) {
        if (saw.wall) {
            speed_x = 0;
        }
        pushed_ = false;
        if (message == message::kDropped) {
            message = 0;
            start_falling();
            stay_active();
            stop_scraping(world, *this);
            return in_the_air(world, State::kRising);
        }
        if (message == message::kPushed) {
            push(world);
        }
        message = 0;
        scrape_while_pushed(world, *this, pushed_);
        if (!find_beside(world, *this, Side::kBelow, ground())) {
            return fall(world);
        }
        if (argument != 0 && switches_on(world, argument)) {
            return done(world);
        }
        return rest(world);
    }
    void when_falling(World& world) {
        if (saw.wall) {
            speed_x = 0;
        }
        if (saw.landed) {
            stay_active(stays_active_);
            stop_falling();
            world.sound(*this, effect::kBlockLands);
            return rest(world);
        }
        squash_all_below(world, *this);
        if (held) {
            return in_the_air(world, State::kHeld);
        }
        return in_the_air(world, speed_y <= 0 ? State::kRising : State::kFalling);
    }
    void when_rising(World& world) {
        if (saw.wall) {
            speed_x = 0;
        }
        if (saw.ceiling) {
            speed_y = 0;
        }
        if (held) {
            return in_the_air(world, State::kHeld);
        }
        if (speed_y > 0) {
            return let_go(world);
        }
        return in_the_air(world, State::kRising);
    }
    void when_held(World& world) {
        if (saw.wall) {
            speed_x = 0;
        }
        crush_beside(world, *this, Side::kBehind);
        crush_beside(world, *this, Side::kAhead);
        if (held) {
            return in_the_air(world, State::kHeld);
        }
        return let_go(world);
    }
    static const TileTypes& viking_above() {
        static const TileTypes types = tile_types({tile::kViking});
        return types;
    }
    Looks looks_for() const override {
        switch (state_) {
            case State::kResting:
            case State::kHeld:
                return {stopped_by(solid())};
            case State::kFalling:
                return {stopped_by(solid()), lands_on(ground())};
            case State::kRising:
                return {stopped_by(solid()), stopped_above_or_below(viking_above()), stopped_above_or_below(solid())};
            default:
                return {};
        }
    }
    void outside_active_area(World& world) override { leave(world, *this); }

  private:
    enum class State : std::uint8_t { kStart, kResting, kFalling, kRising, kHeld, kDone };

    void rest(World& world) {
        stay_active_under_a_viking(world, *this, stays_active_);
        world.animate(*this);
        state_ = State::kResting;
    }
    void fall(World& world) {
        start_falling();
        stay_active();
        stop_scraping(world, *this);
        in_the_air(world, State::kFalling);
    }
    void in_the_air(World& world, State state) {
        world.animate(*this);
        state_ = state;
    }
    // Out of a magnet's hold, or past the top of its way up.
    void let_go(World& world) {
        world.animate(*this);
        state_ = speed_y <= 0 ? State::kRising : State::kFalling;
    }
    void done(World& world) {
        if (!stays_when_done_) {
            return gone_for_good(world, *this);
        }
        hit_mask = 0;
        stay_active();
        state_ = State::kDone;
    }

    void push(World& world) {
        const auto by = static_cast<std::int16_t>(message_value);
        const Side side = by < 0 ? Side::kBehind : Side::kAhead;
        if (find_beside(world, *this, side, solid())) {
            return;
        }
        push_x += by;
        pushed_ = true;
        crush_beside(world, *this, side);
    }

    State state_ = State::kStart;
    bool stays_active_ = false;
    bool stays_when_done_ = false;
    bool pushed_ = false;
};

// A block only a magnet moves (one kind becomes magnetic, and falls, when its switches come on). It falls where
// nothing holds it, rests on the ground or on Olaf's raised shield, and is dead once it sits in an electric cell.
class MagneticBlock : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                return at_start(world);
            case State::kWaiting:
                if (switches_on(world, argument)) {
                    hit_mask |= hit::kWeight;
                    fall(world);
                }
                return;
            case State::kResting:
                return when_resting(world);
            case State::kFalling:
                return when_falling(world);
            case State::kRising:
                return when_rising(world);
            case State::kHeld:
                return when_held(world);
            case State::kOverShield:
                return when_over_shield(world);
            case State::kDead:
                return;
        }
    }
    void at_start(World& world) {
        stays_active_ = stays_active();
        acts_as = tile::kGround;
        if (kind == factory_kind::kMagneticBlockWaitingForSwitch) {
            world.play(*this, anim::kMagneticBlockWaitingLooks);
            world.animate(*this);
            if (is_falling()) {
                return fall(world);
            }
            state_ = State::kWaiting;
            return;
        }
        world.play(*this, anim::kMagneticBlockLooks);
        return rest(world);
    }
    void when_resting(World& world) {
        if (saw.wall) {
            speed_x = 0;
        }
        if (message == message::kDropped) {
            message = 0;
            start_falling();
            stay_active();
            return in_the_air(world, State::kRising);
        }
        message = 0;
        return rest(world);
    }
    void when_falling(World& world) {
        if (saw.wall) {
            speed_x = 0;
        }
        if (saw.landed) {
            clear_flag(entity_flag::kFalls);
            stay_active(stays_active_);
            speed_y = 0;
            world.sound(*this, effect::kThud);
            return rest(world);
        }
        squash_all_below(world, *this);
        if (held) {
            return in_the_air(world, State::kHeld);
        }
        return in_the_air(world, speed_y <= 0 ? State::kRising : State::kFalling);
    }
    void when_rising(World& world) {
        if (saw.wall) {
            speed_x = 0;
        }
        if (saw.ceiling) {
            speed_y = 0;
        }
        if (held) {
            return in_the_air(world, State::kHeld);
        }
        if (speed_y > 0) {
            state_ = State::kOverShield;
            return;
        }
        return in_the_air(world, State::kRising);
    }
    void when_held(World& world) {
        if (saw.wall) {
            speed_x = 0;
        }
        crush_beside(world, *this, Side::kBehind);
        crush_beside(world, *this, Side::kAhead);
        if (held) {
            return in_the_air(world, State::kHeld);
        }
        state_ = State::kOverShield;
    }
    void when_over_shield(World& world) {
        if (saw.shield != kNobody) {
            target = saw.shield;
            if (speed_y < 0) {
                return in_the_air(world, State::kRising);
            }
            if (held) {
                return in_the_air(world, State::kHeld);
            }
            return;
        }
        world.animate(*this);
        state_ = speed_y < 0 ? State::kRising : State::kFalling;
    }
    static const TileTypes& viking_above() {
        static const TileTypes types = tile_types({tile::kViking});
        return types;
    }
    static const TileTypes& raised_shield() {
        static const TileTypes types = tile_types({tile::kShieldOverhead});
        return types;
    }
    Looks looks_for() const override {
        switch (state_) {
            case State::kResting:
            case State::kHeld:
                return {stopped_by(solid())};
            case State::kFalling:
                return {stopped_by(solid()), lands_on(raised_shield()), lands_on(solid())};
            case State::kRising:
                return {stopped_by(solid()), stopped_above_or_below(viking_above()), stopped_above_or_below(solid())};
            case State::kOverShield:
                return {touching_any(hit::kShieldOverhead, &Saw::shield)};
            default:
                return {};
        }
    }
    void outside_active_area(World& world) override { leave(world, *this); }

  private:
    enum class State : std::uint8_t { kStart, kWaiting, kResting, kFalling, kRising, kHeld, kOverShield, kDead };

    void rest(World& world) {
        static const TileTypes electric = tile_types({tile::kElectricity});
        if (!find_beside(world, *this, Side::kBelow, ground())) {
            return fall(world);
        }
        if (find_at(world, *this, x, box.top, electric)) {
            state_ = State::kDead;
            return;
        }
        world.animate(*this);
        state_ = State::kResting;
    }
    void fall(World& world) {
        start_falling();
        stay_active();
        in_the_air(world, State::kFalling);
    }
    void in_the_air(World& world, State state) {
        world.animate(*this);
        state_ = state;
    }

    State state_ = State::kStart;
    bool stays_active_ = false;
};

// A trolley on a rail: it runs left and right with a crane's controls, carrying the Vikings on it.
class Trolley : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            stays_active_ = stays_active();
            acts_as = tile::kGround;
            world.play(*this, anim::kTrolleyLooks);
            world.animate(*this);
            stay_active_under_a_viking(world, *this, stays_active_);
            return;
        }
        const std::uint16_t asked = controls(world, argument);
        if (going_ == 0) {
            going_ = (asked & kGoLeft) != 0 ? kGoLeft : ((asked & kGoRight) != 0 ? kGoRight : 0);
            if (going_ == 0) {
                stay_active_under_a_viking(world, *this, stays_active_);
                return;
            }
            speed_x = static_cast<std::int16_t>(going_ == kGoLeft ? -kSpeed : kSpeed);
            world.sound(*this, effect::kMotor);
        } else if (saw.wall || (asked & going_) == 0) {
            speed_x = 0;
            world.stop_sound(*this, effect::kMotor);
            going_ = 0;
            stay_active_under_a_viking(world, *this, stays_active_);
            return;
        }
        static const TileTypes rail_ends = tile_types({tile::kGround, tile::kPlatformStop});
        const bool at_the_end =
            find_at(world, *this, going_ == kGoLeft ? box.left - 4 : box.right + 4, y, rail_ends).has_value();
        for (int from = 0; from < kVikingSlots && !at_the_end;) {
            const int rider = find_in_row(world, *this, Row::kAbove, vikings(), true, from);
            if (rider == kNobody) {
                break;
            }
            target = rider;
            from = rider + 1;
            if (Entity* viking = world.at(rider)) {
                viking->push_x += push_x + speed_x;
                viking->push_y += push_y + speed_y;
            }
        }
        controls(world, argument) &= kMagnetOn;
        stay_active_under_a_viking(world, *this, stays_active_);
        world.animate(*this);
    }
    static const TileTypes& rail_ends() {
        static const TileTypes types = tile_types({tile::kGround, tile::kPlatformStop});
        return types;
    }
    Looks looks_for() const override { return going_ != 0 ? Looks{stopped_by(rail_ends())} : Looks{}; }
    void outside_active_area(World& world) override { leave(world, *this); }

  private:
    static constexpr std::int16_t kSpeed = px(4);

    bool stays_active_ = false;
    std::uint16_t going_ = 0;
};

}  // namespace

std::span<const KindOf> crane() {
    static constexpr KindOf kList[] = {
        {Home::kFactory, factory_kind::kMagnet, make<Magnet>},
        {Home::kFactory, factory_kind::kCrate, make<Crate>},
        {Home::kFactory, factory_kind::kMagneticBlock, make<MagneticBlock>},
        {Home::kFactory, factory_kind::kMagneticBlockWaitingForSwitch, make<MagneticBlock>},
        {Home::kFactory, factory_kind::kTrolley, make<Trolley>},
    };
    return kList;
}

}  // namespace lvr
