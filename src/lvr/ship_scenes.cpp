// The scenes of the ship: the starfield after the last level, and the curtain call with everyone in it.

#include <array>
#include <cstdint>
#include <memory>
#include <span>

#include "lvr/cues.hpp"
#include "lvr/dialogue.hpp"
#include "lvr/enemies.hpp"
#include "lvr/hits.hpp"
#include "lvr/kinds.hpp"
#include "lvr/world.hpp"

namespace lvr {

namespace {

// The starfield after Tomator: the Vikings talk in their ship (the boxes come and go by themselves), then it
// shoots off and the scene ends.
class Starfield : public Entity {
  public:
    void update(World& world) override {
        if (countdown != 0) {
            return;
        }
        static constexpr std::array<Cue, 57> kCues{{
            cue::effect(effect::kStarfieldSound),
            cue::wait(120),
            cue::effect(effect::kTalk),
            cue::colour(text_colour::kBaleog),
            cue::say(0x17a, tail::kBelowMiddle, -40),
            cue::wait(120),
            cue::clear(),
            cue::colour(text_colour::kErik),
            cue::say(0x17b, tail::kBelowMiddle, -40),
            cue::wait(200),
            cue::clear(),
            cue::colour(text_colour::kOlaf),
            cue::say(0x17c, tail::kAboveMiddle, 40),
            cue::wait(120),
            cue::clear(),
            cue::colour(text_colour::kErik),
            cue::say(0x17d, tail::kBelowMiddle, -40),
            cue::wait(120),
            cue::clear(),
            cue::stop_effect(effect::kStarfieldSound),
            cue::restart_music(),
            cue::colour(text_colour::kOlaf),
            cue::say(0x17e, tail::kAboveMiddle, 40),
            cue::wait(120),
            cue::clear(),
            cue::say(0x17f, tail::kAboveMiddle, 40),
            cue::wait(120),
            cue::clear(),
            cue::say(0x180, tail::kAboveMiddle, 40),
            cue::wait(120),
            cue::clear(),
            cue::say(0x181, tail::kAboveMiddle, 40),
            cue::wait(120),
            cue::clear(),
            cue::effect(effect::kPowerDown),
            cue::colour(text_colour::kErik),
            cue::say(0x182, tail::kBelowMiddle, -40),
            cue::wait(200),
            cue::clear(),
            cue::say(0x183, tail::kBelowMiddle, -40),
            cue::wait(120),
            cue::clear(),
            cue::colour(text_colour::kOlaf),
            cue::say(0x184, tail::kAboveMiddle, 40),
            cue::wait(200),
            cue::clear(),
            cue::colour(text_colour::kErik),
            cue::say(0x185, tail::kBelowMiddle, -40),
            cue::wait(120),
            cue::clear(),
            cue::stop_effect(effect::kPowerDown),
            cue::speed_across(1, speed(32)),
            cue::wait(10),
            cue::no_health(2),
            cue::wait(20),
            cue::wait(20),
            cue::leave(),
        }};
        (void)run_cues(world, *this, kCues, at_);
    }

  private:
    std::size_t at_ = 0;
};

// The Vikings' ship in the starfield: it bobs up and down, five ticks each way.
class BobbingShip : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            world.play(*this, anim::kBobbingShipFlies);
            count_down();
        } else if (countdown != 0) {
            world.animate(*this);
            return;
        } else {
            rising_ = !rising_;
        }
        speed_y = rising_ ? px(-1) : px(1);
        countdown = 5;
        world.animate(*this);
    }

  private:
    bool rising_ = true;
};

// Something fixed to the ship (the entity in slot 1) while it has health.
class ShipsTail : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            world.play(*this, anim::kShipsTailLooks);
        }
        if (health == 0) {
            world.remove(slot);
            return;
        }
        target = 1;
        moved_by = 1;
        push_x = -58;
        push_y = 6;
        world.animate(*this);
    }
};

// Something that rides on the ship for 20 ticks and then flies off, up and to the left.
class ShipsRider : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            world.play(*this, anim::kShipsRiderLooks);
            count_down_from(20);
        } else if (riding_ && countdown == 0) {
            riding_ = false;
            speed_x = px(-5);
            speed_y = px(-1.5);
        }
        if (riding_) {
            target = 1;
            moved_by = 1;
            push_x = 24;
            push_y = 0;
        }
        world.animate(*this);
    }

  private:
    bool riding_ = true;
};

// The spaceship's trooper as the ship's curtain call has it: it waits to be led, walks where it is told, and
// fires once when told to stand. (What it does in the spaceship's levels isn't written.)
class SceneTrooper : public Enemy {
  public:
  protected:
    void turn(World& world) override {
        switch (at_) {
            case At::kStart:
                return at_start(world);
            case At::kWaiting:
            case At::kStanding:
                if (message != 0) {
                    (void)told_all(world);
                }
                return;
            case At::kWalking:
                if (told_all(world)) {
                    return;
                }
                return go(world, At::kWalking);
            case At::kFiring:
                return when_firing(world);
        }
    }
    void at_start(World& world) {
        appear(world, anim::kSceneTrooperAppears);
        clear_flag(entity_flag::kFalls);
        at_ = At::kWaiting;
        if (message != 0) {
            (void)told_all(world);
        }
    }
    void when_firing(World& world) {
        if (told_all(world)) {
            return;
        }
        if (animation_over()) {
            return stand(world);
        }
        if (sprite.packed_frame == kFiresAt && !fired_) {
            fired_ = true;
            world.spawn(*this, kind::kBullet, x + (facing_left() ? -6 : 6), y + 4);
            world.sound(*this, effect::kShot30);
        }
        return go(world, At::kFiring);
    }
    void recovered(World& world) override { stand(world); }

  private:
    enum class At : std::uint8_t { kStart, kWaiting, kWalking, kFiring, kStanding };
    static constexpr std::uint16_t kFiresAt = 8;

    void go(World& world, At at) {
        world.animate_on_ground(*this);
        at_ = at;
    }
    void stand(World& world) {
        message = 0;
        world.play(*this, anim::kSceneTrooperWalks);
        speed_x = 0;
        go(world, At::kStanding);
    }

    bool told_all(World& world) {
        switch (message) {
            case message::kLedAttack:
                message = 0;
                world.play(*this, anim::kSceneTrooperFires);
                speed_x = 0;
                fired_ = false;
                go(world, At::kFiring);
                return true;
            case message::kLedStand:
                stand(world);
                return true;
            case message::kLedTurn:
                turn_round(world);
                [[fallthrough]];
            case message::kLedWalk:
                message = 0;
                world.play(*this, anim::kSceneTrooperWalks);
                speed_x = px(3);
                go(world, At::kWalking);
                return true;
            case message::kLedLeave:
                gone_for_good(world, *this);
                return true;
            case message::kLedColours:
                (void)told_colours(world);
                return false;
            default:
                message = 0;
                return false;
        }
    }

    At at_ = At::kStart;
    bool fired_ = false;
};

// One of the curtain call's creatures.
struct Creature {
    std::uint16_t kind;
    int across;  // where it comes on, from the director
    int below;
    std::uint16_t walk_in;  // ticks
    std::uint16_t walk_out;
    std::uint16_t pause = 30;
};

// The ship's curtain call: one creature after another is brought on, walks in, stands while its name is shown,
// turns, walks off and goes. After the last something drops in, and one more creature.
class CurtainCall : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                count_down();
                world.spawn(*this, scene_kind::kAlarm, 0, 92, 0, entity_flag::kAlwaysActive);
                return wait(50, State::kOpened);
            case State::kOpened:
                if (countdown != 0) {
                    return;
                }
                world.colour_boxes(text_colour::kErik);
                return bring_on(world);
            case State::kBroughtOn:
                return wait(2, State::kWalkOn);
            case State::kWalkOn:
                if (countdown != 0) {
                    return;
                }
                tell(world, message::kLedWalk);
                return wait(now().walk_in, State::kStand);
            case State::kStand:
                if (countdown != 0) {
                    return;
                }
                tell(world, message::kLedAttack);
                return wait(now().pause, State::kName);
            case State::kName:
                return when_name(world);
            case State::kNamed:
                return when_named(world);
            case State::kGo:
                if (countdown != 0) {
                    return;
                }
                tell(world, message::kLedLeave);
                return wait(20, State::kNext);
            case State::kNext:
                if (countdown != 0) {
                    return;
                }
                ++number_;
                return bring_on(world);
            case State::kDropping:
                return when_dropping(world);
            case State::kOver:
                return;
        }
    }
    void when_name(World& world) {
        if (countdown != 0) {
            return;
        }
        if (const Entity* creature = its(world)) {
            world.say_at_point(0x16b + static_cast<int>(number_), tail::kBelowMiddle, creature->x, creature->y - 80);
        }
        countdown = 80;
        state_ = State::kNamed;
    }
    void when_named(World& world) {
        if (countdown != 0) {
            return;
        }
        world.clear_text();
        tell(world, message::kLedTurn);
        return wait(now().walk_out, State::kGo);
    }
    void when_dropping(World& world) {
        if (countdown != 0) {
            return;
        }
        if (Entity* last = world.create_by(*this, prehistoric_kind::kRoller, 192, 0, 0, 1)) {
            creature_ = last->slot;
            last->message = message::kLedColours;
        }
        never_again(world, *this);
        state_ = State::kOver;
    }

  private:
    enum class State : std::uint8_t {
        kStart,
        kOpened,
        kBroughtOn,
        kWalkOn,
        kStand,
        kName,
        kNamed,
        kGo,
        kNext,
        kDropping,
        kOver
    };
    static constexpr std::array<Creature, 13> kCreatures{{
        {.kind = prehistoric_kind::kBrawler, .across = 0, .below = 0, .walk_in = 27, .walk_out = 27},
        {.kind = egypt_kind::kHopper, .across = 352, .below = 0, .walk_in = 30, .walk_out = 30},
        {.kind = prehistoric_kind::kRoller, .across = 0, .below = 0, .walk_in = 29, .walk_out = 29},
        {.kind = egypt_kind::kMummy, .across = 312, .below = 0, .walk_in = 120, .walk_out = 130},
        {.kind = kind::kBomber, .across = 0, .below = 0, .walk_in = 42, .walk_out = 42},
        {.kind = factory_kind::kWalker, .across = 352, .below = 0, .walk_in = 40, .walk_out = 40},
        {.kind = factory_kind::kShooter, .across = 0, .below = 0, .walk_in = 33, .walk_out = 33},
        {.kind = kind::kCharger, .across = 352, .below = 0, .walk_in = 40, .walk_out = 50, .pause = 5},
        {.kind = kind::kCloseGunner, .across = 0, .below = 0, .walk_in = 60, .walk_out = 60},
        {.kind = wacky_kind::kThrower, .across = 352, .below = 0, .walk_in = 32, .walk_out = 32},
        {.kind = kind::kNearTrooper, .across = 0, .below = 0, .walk_in = 37, .walk_out = 37},
        {.kind = prehistoric_kind::kSpitter, .across = 320, .below = 6, .walk_in = 57, .walk_out = 57},
        {.kind = prehistoric_kind::kBrawler, .across = 0, .below = 0, .walk_in = 27, .walk_out = 27},
    }};

    const Creature& now() const { return kCreatures.at(number_); }
    Entity* its(World& world) const { return world.at(creature_); }
    void tell(World& world, std::uint16_t what) const {
        if (Entity* creature = its(world)) {
            creature->message = what;
        }
    }
    // A wait takes a tick at least.
    void wait(std::uint16_t ticks, State then) {
        countdown = ticks;
        state_ = then;
    }

    void bring_on(World& world) {
        if (number_ >= kCreatures.size()) {  // the end: something drops in
            world.spawn(*this, scene_kind::kDroppedIn, 160, 0, 0, entity_flag::kLargePieces);
            return wait(100, State::kDropping);
        }
        const Creature& creature = now();
        Entity* created = world.create_by(*this, creature.kind, x + creature.across, y + creature.below, 0,
                                          entity_flag::kAlwaysActive | entity_flag::kLargePieces);
        creature_ = created != nullptr ? created->slot : kNobody;
        tell(world, message::kLedColours);
        state_ = State::kBroughtOn;
    }

    State state_ = State::kStart;
    std::size_t number_ = 0;
    int creature_ = kNobody;
};

// The ship's alarm: the picture turns blue-green and red in turn, six ticks each, with six between.
class Alarm : public Entity {
  public:
    void update(World& world) override {
        count_down();
        if (!first_turn() && countdown != 0) {
            return;
        }
        if (phase_ == 1) {
            world.tint = {0, 10, 10};
        } else if (phase_ == 2) {
            world.tint = {10, 0, 0};
        }
        phase_ = (phase_ + 1) % 3;
        countdown = 6;
    }

  private:
    int phase_ = 0;
};

// Something of the ship that only plays its animation.
class ShipPart : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            world.play(*this, kind == scene_kind::kShipPart ? anim::kShipPartLooks : anim::kOtherShipPartLooks);
        }
        world.animate(*this);
    }
};

// What drops in at the end: it falls to a certain height, bounces once, and lies there.
class DroppedIn : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                world.play(*this, anim::kDroppedInLooks);
                start_falling();
                state_ = State::kFalling;
                break;
            case State::kFalling:
                if (y < kFloor) {
                    break;
                }
                world.sound(*this, effect::kThud3C);
                speed_y = px(-6);
                floor_ = y;
                world.shake_y = {.amount = 2, .offset = 0, .ticks = 8};
                state_ = State::kBouncing;
                break;
            case State::kBouncing:
                if (speed_y < 0 || static_cast<std::uint16_t>(y) < static_cast<std::uint16_t>(floor_)) {
                    break;
                }
                stop_falling();
                world.shake_y = {.amount = 1, .offset = 0, .ticks = 4};
                world.sound(*this, effect::kThud3C);
                state_ = State::kLying;
                return;
            case State::kLying:
                return;
        }
        world.animate(*this);
    }

  private:
    enum class State : std::uint8_t { kStart, kFalling, kBouncing, kLying };
    static constexpr std::int16_t kFloor = 134;

    State state_ = State::kStart;
    std::int16_t floor_ = 0;
};

}  // namespace

std::span<const KindOf> ship_scenes() {
    static constexpr KindOf kList[] = {
        {Home::kScenes, scene_kind::kStarfield, make<Starfield>},
        {Home::kScenes, scene_kind::kBobbingShip, make<BobbingShip>},
        {Home::kScenes, scene_kind::kShipsTail, make<ShipsTail>},
        {Home::kScenes, scene_kind::kShipsRider, make<ShipsRider>},
        {Home::kScenes, scene_kind::kTrooper, make<SceneTrooper>},
        {Home::kScenes, scene_kind::kTrooper7A, make<SceneTrooper>},
        {Home::kScenes, scene_kind::kCurtainCall, make<CurtainCall>},
        {Home::kScenes, scene_kind::kAlarm, make<Alarm>},
        {Home::kScenes, scene_kind::kShipPart, make<ShipPart>},
        {Home::kScenes, scene_kind::kShipPart4E, make<ShipPart>},
        {Home::kScenes, scene_kind::kDroppedIn, make<DroppedIn>},
    };
    return kList;
}

}  // namespace lvr
