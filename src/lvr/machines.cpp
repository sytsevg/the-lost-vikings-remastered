// The machines that run while their switches are on: presses, pistons, furnaces, fire jets, fans, conveyors,
// and the pair of plates.

#include <array>
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

// A press: while its switches are on it comes down, squashing a Viking under it, and goes back up to the
// ceiling, again and again. One kind bounces off the first thing it meets (Olaf's raised shield too); the
// other slams onto the floor.
class Press : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                return at_start(world);
            case State::kWaiting:
                return wait(world);
            case State::kComingDown:
                return when_coming_down(world);
            case State::kGoingUp:
                if (saw.ceiling) {
                    return come_down(world);
                }
                world.animate(*this);
                return;
        }
    }
    void at_start(World& world) {
        slams_ = kind == factory_kind::kSlammingPress || kind == factory_kind::kSlammingPressAE;
        world.play(*this, slams_ ? anim::kPressSlammerLooks : anim::kPressLooks);
        world.animate(*this);
        speed_ = static_cast<std::int16_t>(argument & kHighByte);
        argument &= kLowByte;
        acts_as = tile::kGround;
        return wait(world);
    }
    void when_coming_down(World& world) {
        if (saw.landed) {
            if (slams_) {
                world.shake_y = {.amount = 1, .offset = 0, .ticks = 4};
                world.sound(*this, effect::kPressSlams);
            }
            return go_up(world);
        }
        for (const int under : vikings_in_row(world, *this, Row::kBelow)) {
            if (!slams_ && under == slot::kOlaf && olaf_holds_shield_up(world)) {
                return go_up(world);
            }
            squash(world, *this, under);
        }
        world.animate(*this);
    }
    static const TileTypes& anything() {
        static const TileTypes types = tile_types({tile::kGround, tile::kGround02, tile::kPlatform,
                                                   tile::kShieldOverhead, tile::kPlatformStop, tile::kGround20});
        return types;
    }
    static const TileTypes& ceilings() {
        static const TileTypes types = tile_types({tile::kGround, tile::kPlatformStop});
        return types;
    }
    Looks looks_for() const override {
        switch (state_) {
            case State::kComingDown:
                return {lands_on(slams_ ? end_of_its_way() : anything())};
            case State::kGoingUp:
                return {stopped_above_or_below(ceilings())};
            default:
                return {};
        }
    }

  private:
    enum class State : std::uint8_t { kStart, kWaiting, kComingDown, kGoingUp };

    void wait(World& world) {
        speed_y = 0;
        state_ = State::kWaiting;
        if (switches_on(world, argument)) {
            come_down(world);
        }
    }

    void come_down(World& world) {
        if (!switches_on(world, argument)) {
            speed_y = 0;
            state_ = State::kWaiting;
            return;
        }
        stay_active();
        speed_y = speed_;
        state_ = State::kComingDown;
        world.animate(*this);
    }

    void go_up(World& world) {
        stay_active(false);
        speed_y = static_cast<std::int16_t>(-speed_);
        state_ = State::kGoingUp;
        world.animate(*this);
    }

    State state_ = State::kStart;
    bool slams_ = false;
    std::int16_t speed_ = 0;
};

// A piston: while its switches are on it waits (its argument's low byte, in ticks), slams out to the floor (one
// kind: up to the ceiling) and goes back, again and again. A Viking in its way is impaled; one right in front of
// it is hurt. Blows can destroy it.
class Piston : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                upwards_ = kind == factory_kind::kUpwardPiston;
                wait_ = argument & kLowByte;
                argument &= kHighByte;
                acts_as = tile::kGround;
                count_down();
                world.play(*this, upwards_ ? anim::kPistonUpwardsLooks : anim::kPistonLooks);
                world.animate(*this);
                return rest(world);
            case State::kResting:
                hurt_viking_ahead(world);
                if (told(world)) {
                    return;
                }
                return rest(world);
            case State::kWaiting:
                if (countdown != 0) {
                    return;
                }
                speed_y = static_cast<std::int16_t>(upwards_ ? -kSpeed : kSpeed);
                world.sound(*this, effect::kPistonGoes);
                state_ = State::kGoingOut;
                break;
            case State::kGoingOut: {
                if ((saw.landed || saw.ceiling)) {
                    speed_y = static_cast<std::int16_t>(upwards_ ? kSpeed : -kSpeed);
                    state_ = State::kGoingBack;
                    break;
                }
                const int in_its_way =
                    find_in_row(world, *this, upwards_ ? Row::kAbove : Row::kBelow, vikings(), true, 0);
                if (in_its_way != kNobody) {
                    target = in_its_way;
                    world.tell(in_its_way, upwards_ ? message::kImpaled : message::kImpaledFromAbove);
                } else {
                    hurt_viking_ahead(world);
                }
                if (told(world)) {
                    return;
                }
                break;
            }
            case State::kGoingBack:
                if ((saw.landed || saw.ceiling)) {
                    return set_off(world);
                }
                hurt_viking_ahead(world);
                if (told(world)) {
                    return;
                }
                break;
        }
        world.animate(*this);
    }
    static const TileTypes& ceilings() {
        static const TileTypes types = tile_types({tile::kGround, tile::kPlatformStop});
        return types;
    }
    // Going out or back: what ends its way, a floor going down or a ceiling going up.
    Looks looks_for() const override {
        if (state_ != State::kGoingOut && state_ != State::kGoingBack) {
            return {};
        }
        const bool down = (state_ == State::kGoingOut) != upwards_;
        return down ? Looks{lands_on(end_of_its_way())} : Looks{stopped_above_or_below(ceilings())};
    }

  private:
    enum class State : std::uint8_t { kStart, kResting, kWaiting, kGoingOut, kGoingBack };
    static constexpr std::int16_t kSpeed = px(16);

    void rest(World& world) {
        state_ = State::kResting;
        if (switches_on(world, argument)) {
            set_off(world);
        }
    }

    void set_off(World& world) {
        if (!switches_on(world, argument)) {
            speed_y = 0;
            return rest(world);
        }
        countdown = wait_;
        state_ = State::kWaiting;
    }

    void hurt_viking_ahead(World& world) {
        const auto ahead = find_viking_beside(world, *this, Side::kAhead, vikings());
        if (!ahead || ahead->entity < 0) {
            return;
        }
        target = ahead->entity;
        Entity* viking = world.at(target);
        if (viking == nullptr || static_cast<std::int16_t>(viking->health) < 1) {
            return;
        }
        harm(*viking, message::kBlast, 2, facing_left());
        world.spawn(*this, kind::kSpark, viking->x, viking->y, 0, entity_flag::kLargePieces);
    }

    // True if a blow destroyed it.
    bool told(World& world) {
        if (message != message::kSwordBlow && message != message::kArrowBlow && message != message::kHeadButt &&
            message != message::kBlast) {
            message = 0;
            return false;
        }
        if (!survives_blow(*this)) {
            gone_for_good(world, *this);
            return true;
        }
        return false;
    }

    State state_ = State::kStart;
    bool upwards_ = false;
    std::uint16_t wait_ = 0;
};

// A place that burns the Vikings in it while its switches are on.
class Furnace : public Entity {
  public:
    void update(World& world) override {
        if (saw.viking != kNobody) {
            target = saw.viking;
            world.tell(saw.viking, message::kScorched, 10);
            saw.viking = kNobody;
        }
        on_ = switches_on(world, argument);
    }
    Looks looks_for() const override { return on_ ? Looks{touching_a_viking()} : Looks{}; }

  private:
    bool on_ = false;
};

// A furnace that glows (kind BE). While its switches are on, the screen turns red as the Viking being played
// comes near it inside its box, a step every two ticks, and a Viking within reach of it is burnt, each at most
// once in 40 ticks. Switched off it shows its dark cells and the red fades.
class GlowingFurnace : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                count_down();
                if (switches_on(world, argument)) {
                    return burn_on(world);
                }
                return go_out(world);
            case State::kOut:
                return when_out(world);
            case State::kBurning:
                return when_burning(world);
        }
    }
    void when_out(World& world) {
        if (switches_on(world, argument)) {
            world.sound(*this, effect::kFurnaceGlows);
            world.factory.burn_wait.fill(kBurnEvery);
            show(world, {3, 4, 6, 5});
            return burn_on(world);
        }
        stay_active(false);
        if (glow_ == 0) {
            return;
        }
        stay_active();
        if (countdown == 0) {
            set_glow(world, glow_ - 1);
        }
    }
    void when_burning(World& world) {
        bool active = saw.viking != kNobody;
        if (active) {
            burn_those_in_reach(world);
        }
        if (!switches_on(world, argument)) {
            return go_out(world);
        }
        const int wanted = wanted_glow(world);
        active = active || glow_ != 0;
        if (wanted != glow_) {
            active = true;
            if (countdown == 0) {
                set_glow(world, wanted < glow_ ? glow_ - 1 : glow_ + 1);
            }
        }
        stay_active(active);
        return burn_on(world);
    }
    Looks looks_for() const override { return state_ == State::kBurning ? Looks{touching_a_viking()} : Looks{}; }

  private:
    enum class State : std::uint8_t { kStart, kOut, kBurning };
    static constexpr std::uint16_t kBurnEvery = 40;
    static constexpr int kBrightest = 16;

    // Its four cells' pictures: top left, top right, bottom right, bottom left.
    void show(World& world, const std::array<std::uint16_t, 4>& metatiles) const {
        const int column = cell_of(x - 8);
        const int row = cell_of(y - 8);
        const std::array<std::array<int, 2>, 4> cells{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
        for (std::size_t i = 0; i < cells.size(); ++i) {
            if (Cell* cell = world.map.cell(column + cells.at(i).at(0), row + cells.at(i).at(1))) {
                cell->metatile = metatiles.at(i);
            }
        }
    }
    void go_out(World& world) {
        stay_active();
        show(world, {7, 8, 10, 9});
        state_ = State::kOut;
    }
    void burn_on(World& world) {
        health = 0;
        for (std::uint16_t& wait : world.factory.burn_wait) {
            if (wait != 0) {
                --wait;
            }
        }
        state_ = State::kBurning;
    }
    void set_glow(World& world, int glow) {
        glow_ = glow;
        const auto by = static_cast<std::uint8_t>(glow == 0 ? 0 : (2 * glow) - 1);
        world.tint = {0, by, by};
        countdown = 2;
    }
    // Brightest with the Viking being played at it, a step less for every 8 pixels he is away; nothing outside
    // its box.
    int wanted_glow(const World& world) const {
        const Entity* viking = world.current_viking == kNobody ? nullptr : world.at(world.current_viking);
        if (viking == nullptr || viking->x >= box.right || box.left >= viking->x || viking->y >= box.bottom ||
            box.top >= viking->y) {
            return 0;
        }
        const int ahead = world.offset_to(*this, world.current_viking).ahead;
        if (ahead >= 0) {
            return std::max(1, kBrightest - (ahead / 8));
        }
        return ahead < -128 ? 0 : kBrightest - ((-ahead - 1) / 8);
    }
    void burn_those_in_reach(World& world) {
        for (int viking = 0; viking < kVikingSlots; ++viking) {
            Entity* other = world.at(viking);
            std::uint16_t& wait = world.factory.burn_wait.at(static_cast<std::size_t>(viking));
            if (other == nullptr || wait != 0) {
                continue;
            }
            const World::Offset to = world.offset_to(*this, viking);
            if (to.ahead >= 64 || to.ahead <= -64 || to.below >= 48 || to.below <= -48) {
                continue;
            }
            target = viking;
            harm(*other, message::kScorched, 2, to.ahead <= 0);
            wait = kBurnEvery;
        }
    }

    State state_ = State::kStart;
    int glow_ = 0;
};

// A fire jet: on while its switches are. A Viking who touches it then burns. One sound for all the jets that are
// on.
class FireJet : public Entity {
  public:
    void update(World& world) override {
        const bool wanted = switches_on(world, argument);
        if (first_turn()) {
            world.play(*this, anim::kFireJetLooks);
            world.animate(*this);
            if (wanted) {
                turn_on(world, anim::kFireJetBurns);
            } else {
                world.play(*this, anim::kFireJetOut);
            }
        } else if (on_) {
            if (saw.viking != kNobody) {
                target = saw.viking;
                world.tell(saw.viking, message::kBurned, 50);
            }
            if (!wanted) {
                world.play(*this, anim::kFireJetOut);
                on_ = false;
                acts_as = tile::kFree;
                one_less(world);
            }
        } else if (wanted) {
            turn_on(world, anim::kFireJetLights);
        }
        world.animate(*this);
    }
    Looks looks_for() const override { return on_ ? Looks{touching_a_viking()} : Looks{}; }
    void outside_active_area(World& world) override {
        if (on_) {
            one_less(world);
        }
        world.remove(slot);
    }

  private:
    void turn_on(World& world, Animation animation) {
        world.play(*this, animation);
        on_ = true;
        if (++world.factory.jets_on == 1) {
            world.sound(*this, effect::kRoar);
        }
    }
    void one_less(World& world) const {
        if (--world.factory.jets_on == 0) {
            world.stop_sound(*this, effect::kRoar);
        }
    }

    bool on_ = false;
};

// A fan: while its switches are on it blows the Vikings before it along (one kind: across, up to 128 pixels
// ahead) or carries them up (the other kind: a Viking in the air or blown up like a balloon, up to 128 pixels
// over it). While it blows on one it stays active.
class Fan : public Entity {
  public:
    void update(World& world) override {
        const bool on = switches_on(world, argument);
        if (first_turn()) {
            upwards_ = kind == wacky_kind::kUpwardFan;
            world.play(*this, upwards_ ? anim::kFanUpwardsAppears : anim::kFanAppears);
            world.animate(*this);
            on_ = !on;
        }
        if (on != on_) {
            on_ = on;
            stay_active(false);
            if (on) {
                world.play(*this, upwards_ ? anim::kFanUpwardsBlows : anim::kFanBlows);
            } else {
                world.play(*this, upwards_ ? (upside_down() ? anim::kFanUpwardsStillFlipped : anim::kFanUpwardsStill)
                                           : anim::kFanStill);
                world.animate(*this);
                return;
            }
        }
        if (!on) {
            return;
        }
        stay_active(false);
        for (const int who : {1, 0, 2}) {
            target = who;
            if (Entity* viking = world.at(who)) {
                upwards_ ? lift(world, *viking) : blow(world, *viking);
            }
        }
        world.animate(*this);
    }

  private:
    static constexpr int kReach = 128;

    void blow(World& world, Entity& viking) {
        if (viking.y < box.top || box.bottom < viking.y) {
            return;
        }
        const int ahead = world.offset_to(*this, viking.slot).ahead;
        if (ahead < 0 || ahead >= kReach) {
            return;
        }
        stay_active();
        if (viking.facing_left() != facing_left()) {
            if (viking.speed_x >= px(-8)) {
                viking.speed_x -= px(1);
            }
        } else if (viking.speed_x < px(8)) {
            viking.speed_x += px(1);
        }
    }

    void lift(World& world, Entity& viking) {
        if (!viking.is_falling() && !viking.inflated) {
            return;
        }
        if (viking.x < box.left || box.right < viking.x) {
            return;
        }
        const int below = world.offset_to(*this, viking.slot).below;
        if (below >= kReach || below < 0) {
            return;
        }
        stay_active();
        if (viking.upside_down() != upside_down()) {
            if (viking.speed_y >= px(-12)) {
                viking.speed_y -= px(1);
            }
        } else if (viking.speed_y < px(4)) {
            viking.speed_y += px(0.5);
        }
    }

    bool upwards_ = false;
    bool on_ = false;
};

// A conveyor belt: the Vikings on it are carried the way it faces. While one is on it, it stays active.
class Conveyor : public Entity {
  public:
    void update(World& world) override {
        if (!first_turn()) {
            for (const int rider : vikings_in_row(world, *this, Row::kAbove)) {
                if (Entity* other = world.at(rider)) {
                    other->push_x += (facing_left() ? px(-2) : px(2));
                }
            }
        }
        const bool ridden = find_viking_beside(world, *this, Side::kAbove, vikings()).has_value();
        stay_active(ridden);
    }
};

// One half of a pair of plates: one is up while their switches are off, the other (the one facing left) while
// they are on. A Viking or a block that comes to rest on the one that is up presses it down, which flips the
// switches and sends the other up: whoever stands on that one is thrown into the air.
class Plate : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                return at_start(world);
            case State::kUp:
                return when_up(world);
            case State::kGoingDown:
                if (countdown == 0) {
                    state_ = State::kDown;
                    return down(world);
                }
                return go_down(world);
            case State::kDown:
                return down(world);
            case State::kGoingUp:
                return when_going_up(world);
        }
    }
    void at_start(World& world) {
        world.play(*this, anim::kPlateAppears);
        world.animate(*this);
        count_down();
        acts_as = tile::kPlatform;
        up_when_on_ = facing_left();
        if (is_up(world)) {
            world.play(*this, anim::kPlateIsUp);
            world.animate(*this);
            shift_box(-12);
            state_ = State::kUp;
        } else {
            world.play(*this, anim::kPlateIsDown);
            world.animate(*this);
            shift_box(12);
            state_ = State::kDown;
        }
    }
    void when_up(World& world) {
        if (!pressed(world)) {
            return;
        }
        world.sound(*this, effect::kSprung);
        if (up_when_on_) {
            world.switches &= static_cast<std::uint16_t>(~argument);
        } else {
            world.switches |= argument;
        }
        world.play(*this, anim::kPlateIsDown);
        countdown = 3;
        state_ = State::kGoingDown;
        return go_down(world);
    }
    void when_going_up(World& world) {
        if (countdown == 0) {
            state_ = State::kUp;
            return;
        }
        shift_box(-8);
        world.animate(*this);
    }
    void outside_active_area(World& world) override { leave(world, *this); }

  private:
    enum class State : std::uint8_t { kStart, kUp, kGoingDown, kDown, kGoingUp };

    bool is_up(const World& world) const { return (world.switches & argument) == (up_when_on_ ? argument : 0); }
    void shift_box(int by) {
        box.top += by;
        box.bottom += by;
    }

    // Something resting on it, not on its way up: a Viking, or anything solid.
    bool pressed(World& world) {
        const auto rests = [&](int who) {
            target = who;
            const Entity* other = world.at(who);
            return other != nullptr && other->box.bottom == box.top - 1 && other->speed_y >= 0;
        };
        for (int from = 0; from < kVikingSlots;) {
            const int who = find_in_row(world, *this, Row::kAbove, vikings(), true, from);
            if (who == kNobody) {
                break;
            }
            if (rests(who)) {
                return true;
            }
            from = who + 1;
        }
        for (int from = 0; from < world.slots();) {
            const int who = find_in_row(world, *this, Row::kAbove, solid(), false, from);
            if (who == kNobody) {
                break;
            }
            if (rests(who)) {
                return true;
            }
            from = who + 1;
        }
        return false;
    }

    void go_down(World& world) {
        for (const int who : vikings_in_row(world, *this, Row::kAbove)) {
            Entity* other = world.at(who);
            if (other != nullptr && other->box.bottom == box.top - 1) {
                other->push_y += px(8);
            }
        }
        shift_box(8);
        world.animate(*this);
    }

    void down(World& world) {
        if (!is_up(world)) {
            return;
        }
        world.play(*this, anim::kPlateIsUp);
        countdown = 3;
        for (const int who : vikings_in_row(world, *this, Row::kAbove)) {
            if (Entity* other = world.at(who)) {
                other->speed_y -= px(16);
            }
        }
        state_ = State::kGoingUp;
        shift_box(-8);
        world.animate(*this);
    }

    State state_ = State::kStart;
    bool up_when_on_ = false;
};

}  // namespace

std::span<const KindOf> machines() {
    static constexpr KindOf kList[] = {
        {Home::kFactory, factory_kind::kSlammingPress, make<Press>},
        {Home::kFactory, factory_kind::kPress, make<Press>},
        {Home::kFactory, factory_kind::kSlammingPressAE, make<Press>},
        {Home::kFactory, factory_kind::kPressAF, make<Press>},
        {Home::kFactory, factory_kind::kPiston, make<Piston>},
        {Home::kFactory, factory_kind::kUpwardPiston, make<Piston>},
        {Home::kFactory, factory_kind::kFurnace, make<Furnace>},
        {Home::kFactory, factory_kind::kGlowingFurnace, make<GlowingFurnace>},
        {Home::kFactory, factory_kind::kFireJet, make<FireJet>},
        {Home::kWacky, wacky_kind::kFan, make<Fan>},
        {Home::kWacky, wacky_kind::kUpwardFan, make<Fan>},
        {Home::kFactory, factory_kind::kConveyor, make<Conveyor>},
        {Home::kFactory, factory_kind::kConveyor8E, make<Conveyor>},
        {Home::kFactory, factory_kind::kPlate, make<Plate>},
    };
    return kList;
}

}  // namespace lvr
