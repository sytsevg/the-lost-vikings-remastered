// The enemies that throw things in an arc, and the bomb they throw.

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

// A bomber strolls and runs by turns, for a number of steps picked at random each time, and when a Viking is about
// level with him (or in his way) he stops and throws three bombs, each further than the last.
class Bomber : public Enemy {
  public:
    Looks looks() const override {
        switch (s.at) {
            case At::kWaiting:
                return {touching_one_of_the_vikings_class()};
            case At::kWalking:
                return {stopped_by(walls()), touching_an_enemy()};
            case At::kFalling:
                return {stopped_by(walls()), lands_on(ground())};
            default:
                return {};
        }
    }

  protected:
    void turn(World& world) override {
        switch (s.at) {
            case At::kStart:
                return at_start(world);
            case At::kWaiting:
                if (saw.viking != kNobody) {
                    target = saw.viking;
                    return attack(world);
                }
                return stroll(world);
            case At::kWalking:
                return when_walking(world);
            case At::kHeldUp:
                return when_held_up(world);
            case At::kAttacking:
                return when_attacking(world);
            case At::kFalling:
                return when_falling(world);
        }
    }
    void at_start(World& world) {
        appear(world, anim::kBomberAppears);
        if (is_falling()) {
            return fall(world);
        }
        s.at = At::kWaiting;
    }
    void when_walking(World& world) {
        if (saw.wall) {
            turn_round(world, kTurnedTicks);
        }
        turn_from_colleague(world);
        if (told(world)) {
            return;
        }
        stay_in_view(world);
        face_viking_behind(world);
        if (s.count != 0) {
            --s.count;
            return next_step(world);
        }
        const int below = world.offset_to(*this, world.nearest_viking(*this)).below;
        if (below >= kAboutLevel || below < -kAboutLevel) {
            return next_step(world);
        }
        return attack(world);
    }
    void when_held_up(World& world) {
        if (told(world)) {
            return;
        }
        if (!on_ground(world)) {
            return fall(world);
        }
        if (s.count == 0) {
            return attack(world);
        }
        --s.count;
        return held_up(world);
    }
    void when_attacking(World& world) {
        if (animation_over()) {
            throw_bomb(world);
        }
        if (told(world)) {
            return;
        }
        if (!on_ground(world)) {
            world.play(*this, anim::kBomberStrolls);
            return fall(world);
        }
        if (s.count != kBombs || sprite.packed_frame != kLastThrowFrame) {
            return attacking(world);
        }
        if (world.random() <= 32000) {  // about half the time he turns round after it
            turn_round(world, kTurnedTicks);
        }
        return stroll(world);
    }
    void when_falling(World& world) {
        if (landed_from_fall()) {
            return stroll(world);
        }
        if (told(world)) {
            return;
        }
        return falling(world);
    }
    void recovered(World& world) override { run(world); }

  private:
    enum class At : std::uint8_t { kStart, kWaiting, kWalking, kHeldUp, kAttacking, kFalling };
    static constexpr std::uint16_t kPatience = 15;  // ticks of walking before he looks for a Viking again
    static constexpr int kAboutLevel = 64;
    static constexpr std::uint16_t kBombs = 3;
    static constexpr std::uint16_t kLastThrowFrame = 11;

    // How many steps the next stretch is: one of three, at random.
    std::uint16_t pick(World& world, std::uint16_t often, std::uint16_t sometimes, std::uint16_t otherwise) const {
        const std::uint16_t dice = world.random();
        return dice < 20000 ? often : (dice < 40000 ? sometimes : otherwise);
    }

    void stroll(World& world) {
        speed_x = px(2.5);
        world.play(*this, anim::kBomberStrolls);
        countdown = kTurnedTicks;
        s.count = kPatience;
        s.steps = pick(world, 40, 30, 20);
        s.running = false;
        go(world);
    }

    void run(World& world) {
        speed_x = px(6);
        world.play(*this, anim::kBomberRuns);
        s.steps = pick(world, 35, 25, 15);
        s.running = true;
        go(world);
    }

    void go(World& world) {
        if (in_his_way(world)) {
            speed_x = 0;
            return held_up(world);
        }
        if (!can_walk_on(world, *this)) {  // a ledge: he turns
            if (!on_ground(world)) {
                return fall(world);
            }
            turn_round(world, kTurnedTicks);
            if (s.count != 0) {
                --s.count;
            }
            return next_step(world);
        }
        world.animate_on_ground(*this);
        s.at = At::kWalking;
    }

    void next_step(World& world) {
        if (s.steps == 0) {
            return s.running ? stroll(world) : run(world);
        }
        --s.steps;
        go(world);
    }

    void held_up(World& world) {
        if (!in_his_way(world)) {
            return stroll(world);
        }
        world.animate(*this);
        s.at = At::kHeldUp;
    }

    void attack(World& world) {
        world.play(*this, anim::kBomberThrows);
        speed_x = 0;
        s.count = 0;
        if (world.offset_to(*this, world.nearest_viking(*this)).ahead <= 0) {
            turn_round(world);
        }
        attacking(world);
    }

    void attacking(World& world) {
        world.animate(*this);
        s.at = At::kAttacking;
    }

    void throw_bomb(World& world) {
        Entity* bomb = world.create_by(*this, kind::kBomb, x + (facing_left() ? -8 : 8), y - 6, 1);
        world.sound(*this, effect::kThrow);
        if (bomb != nullptr) {
            target = bomb->slot;
            bomb->speed_y = px(-8);
            bomb->speed_x = s.count == 1 ? px(4) : (s.count == 2 ? px(6) : px(2));
        }
        ++s.count;
    }

    void fall(World& world) {
        start_falling();
        falling(world);
    }

    void falling(World& world) {
        world.animate(*this);
        s.at = At::kFalling;
    }

    struct Saved {
        At at = At::kStart;
        // The ticks of walking left before he looks for a Viking again, and while he throws the bombs thrown so far:
        // one number, as in the original. So a blow between two bombs leaves their number as the ticks he then runs
        // before he looks again.
        std::uint16_t count = 0;
        std::uint16_t steps = 0;
        bool running = false;
    } s;
    LVR_SAVES(Enemy)
};

// An enemy that throws: he walks, then runs, by turns, a random number of steps each; a Viking about level with
// him (once he is ready again) makes him stop and throw three bombs (the spaceship bomber's kind), each farther
// than the one before. He stops before a Viking or a block in his way, and throws from there too.
class StoneThrower : public Enemy {
  public:
    Looks looks() const override {
        switch (s.at) {
            case At::kAppeared:
                return {touching_one_of_the_vikings_class()};
            case At::kWalking:
            case At::kRunning:
                return {stopped_by(walls()), touching_an_enemy()};
            case At::kFalling:
                return {stopped_by(walls()), lands_on(ground())};
            default:
                return {};
        }
    }

  protected:
    void turn(World& world) override {
        switch (s.at) {
            case At::kStart:
                return at_start(world);
            case At::kAppeared:
                if (saw.viking != kNobody) {
                    target = saw.viking;
                    return aim(world);
                }
                return set_off(world);
            case At::kWalking:
            case At::kRunning:
                return when_walking(world);
            case At::kBlocked:
                return when_blocked(world);
            case At::kThrowing:
                return when_throwing(world);
            case At::kFalling:
                return when_falling(world);
            case At::kLedThrowing:
                return when_led_throwing(world);
            case At::kLed:
                if (told_all(world)) {
                    return;
                }
                return led_on(world);
        }
    }
    void at_start(World& world) {
        appear(world, anim::kStoneThrowerAppears);
        if (is_falling()) {
            return fall(world);
        }
        s.at = At::kAppeared;
    }
    void when_walking(World& world) {
        if (saw.wall) {
            turn_round(world, kTurnedTicks);
        }
        turn_from_colleague(world);
        if (told_all(world)) {
            return;
        }
        stay_in_view(world);
        face_viking_behind(world);
        if (s.count != 0) {
            --s.count;
        } else {
            const World::Offset to = world.offset_to(*this, world.nearest_viking(*this));
            if (to.below < 64 && to.below >= -64) {
                return aim(world);
            }
        }
        return next_step(world);
    }
    void when_blocked(World& world) {
        if (told_all(world)) {
            return;
        }
        if (!on_ground(world)) {
            return fall(world);
        }
        if (s.count == 0) {
            return aim(world);
        }
        --s.count;
        if (!blocked(world)) {
            return set_off(world);
        }
        world.animate(*this);
    }
    void when_throwing(World& world) {
        if (animation_over()) {
            throw_one(world);
        }
        if (told_all(world)) {
            return;
        }
        if (!on_ground(world)) {
            world.play(*this, anim::kStoneThrowerWalks);
            return fall(world);
        }
        if (s.count == 3 && sprite.packed_frame == kThrowOver) {
            if (world.random() <= 32000) {  // half the time he turns round after it
                turn_round(world, kTurnedTicks);
            }
            return set_off(world);
        }
        world.animate(*this);
    }
    void when_falling(World& world) {
        if (landed_from_fall()) {
            return set_off(world);
        }
        if (told_all(world)) {
            return;
        }
        world.animate(*this);
    }
    void when_led_throwing(World& world) {
        if (animation_over()) {
            throw_one(world);
        }
        if (told_all(world)) {
            return;
        }
        if (s.count == 3 && sprite.packed_frame == kThrowOver) {
            return lead(world, 0, Led::kAnimated);
        }
        world.animate_on_ground(*this);
    }
    void recovered(World& world) override { run(world); }  // over a blow he runs

  private:
    enum class At : std::uint8_t {
        kStart,
        kAppeared,
        kWalking,
        kRunning,
        kBlocked,
        kThrowing,
        kFalling,
        kLedThrowing,
        kLed
    };
    static constexpr std::uint16_t kThrowOver = 11;
    static constexpr std::int16_t kPace = px(2.5);

    static const TileTypes& blocks() {
        static const TileTypes types = tile_types({tile::kShield});
        return types;
    }
    bool blocked(const World& world) const {
        return find_beside(world, *this, Side::kAhead, blocks()).has_value() ||
               find_viking_beside(world, *this, Side::kAhead, vikings()).has_value();
    }

    // He walks: slowly, for 20, 30 or 40 steps.
    void set_off(World& world) {
        speed_x = kPace;
        world.play(*this, anim::kStoneThrowerWalks);
        countdown = kTurnedTicks;
        s.count = 15;
        const std::uint16_t dice = world.random();
        s.steps = dice < 20000 ? 40 : (dice < 40000 ? 30 : 20);
        step(world, At::kWalking);
    }
    // He runs: for 15, 25 or 35 steps.
    void run(World& world) {
        speed_x = px(6);
        world.play(*this, anim::kStoneThrowerRuns);
        const std::uint16_t dice = world.random();
        s.steps = dice < 20000 ? 35 : (dice < 40000 ? 25 : 15);
        step(world, At::kRunning);
    }
    // After a step: the next, or the other pace when these are done.
    void next_step(World& world) {
        if (s.steps == 0) {
            return s.at == At::kWalking ? run(world) : set_off(world);
        }
        --s.steps;
        step(world, s.at);
    }
    // One step on, unless something is in his way or the ground ends.
    void step(World& world, At pace) {
        s.at = pace;
        // Where the ground ends he turns and goes on counting his steps, without a tick passing; a place with
        // no way on either side ends when the steps of both paces have (as in the original: bounded here).
        for (int turns = 0; turns < 4; ++turns) {
            if (blocked(world)) {
                speed_x = 0;
                s.at = At::kBlocked;
                world.animate(*this);
                return;
            }
            if (can_walk_on(world, *this)) {
                world.animate_on_ground(*this);
                return;
            }
            if (!on_ground(world)) {
                return fall(world);
            }
            turn_round(world, kTurnedTicks);
            if (s.count != 0) {
                --s.count;
            }
            if (s.steps == 0) {
                return s.at == At::kWalking ? run(world) : set_off(world);
            }
            --s.steps;
        }
        world.animate_on_ground(*this);
    }
    void fall(World& world) {
        start_falling();
        world.animate(*this);
        s.at = At::kFalling;
    }
    // He stops, turns to the nearest Viking, and throws.
    void aim(World& world) {
        world.play(*this, anim::kStoneThrowerThrows);
        speed_x = 0;
        s.count = 0;
        if (world.offset_to(*this, world.nearest_viking(*this)).ahead <= 0) {
            turn_round(world);
        }
        world.animate(*this);
        s.at = At::kThrowing;
    }
    void throw_one(World& world) {
        Entity* thrown = world.create_by(*this, kind::kBomb, x + (facing_left() ? -8 : 8), y - 6, 1);
        world.sound(*this, effect::kThrow);
        if (thrown != nullptr) {
            target = thrown->slot;
            thrown->speed_y = px(-8);
            thrown->speed_x = s.count == 1 ? px(4) : (s.count == 2 ? px(6) : px(2));
        }
        ++s.count;
    }

    void led_to_attack(World& world) override {
        s.count = 0;
        speed_x = 0;
        follow(world, anim::kStoneThrowerThrows, Led::kMoving);
        s.at = At::kLedThrowing;
    }
    void led_to_stand(World& world) override { lead(world, 0, Led::kAnimated); }
    void led_to_walk(World& world) override { lead(world, kPace, Led::kMoving); }
    // Led by a scene's director to stand, or to walk on.
    void lead(World& world, std::int16_t speed, Led how) {
        speed_x = speed;
        follow(world, anim::kStoneThrowerWalks, how);
        s.at = At::kLed;
    }

    struct Saved {
        At at = At::kStart;
        std::uint16_t steps = 0;
        // The stones thrown while he throws, the ticks until he can throw again while he goes: one word, as in the
        // original, so a blow between two stones leaves their number as ticks to wait.
        std::uint16_t count = 0;
    } s;
    LVR_SAVES(Enemy)
};

// An enemy in a pot: he keeps down for a while (nothing but a hard blow reaches him), then comes up, turns to the
// nearest Viking and throws something at him. A Viking who touches him while he is up is hurt.
class PotDweller : public Enemy {
  public:
    Looks looks() const override { return s.at == At::kUp ? Looks{touching_a_viking()} : Looks{}; }

  protected:
    void turn(World& world) override {
        switch (s.at) {
            case At::kStart:
                appear(world, anim::kPotDwellerAppears);
                return duck(world);
            case At::kDown:
                if (told_all(world)) {
                    return;
                }
                if (countdown == 0) {
                    world.play(*this, anim::kPotDwellerComesUp);
                    box.top -= 28;
                    hit_mask =
                        bits(hit::kHurtByFire, hit::kBombable, hit::kHurtBySword, hit::kEnemy, hit::kHurtByArrow);
                    s.at = At::kUp;
                }
                break;
            case At::kUp:
                if (saw.viking != kNobody) {
                    target = saw.viking;
                    knock(world);
                }
                if (told_all(world)) {
                    return;
                }
                if (world.offset_to(*this, world.nearest_viking(*this)).ahead <= 0) {
                    turn_round(world);
                }
                if (animation_over()) {
                    world.spawn(*this, kind::kBomb, x + (facing_left() ? -14 : 14), y - 3);
                    world.sound(*this, effect::kPotDwellerThrows);
                    s.at = At::kThrown;
                }
                break;
            case At::kThrown:
                if (told_all(world)) {
                    return;
                }
                if (animation_over()) {
                    return duck(world);
                }
                break;
        }
        world.animate(*this);
    }
    void recovered(World& /*world*/) override {}

  private:
    enum class At : std::uint8_t { kStart, kDown, kUp, kThrown };

    void duck(World& world) {
        world.play(*this, anim::kPotDwellerKeepsDown);
        countdown = 50;
        box.top = box.bottom;
        hit_mask = hit::kBombable;
        s.at = At::kDown;
        world.animate(*this);
    }

    void knock(World& world) {
        Entity* other = world.at(target);
        if (other == nullptr) {
            return;
        }
        const bool ahead = world.offset_to(*this, target).ahead >= 0;
        const bool from_the_right = ahead ? facing_left() : !facing_left();
        harm(*other, message::kStruck, 2, from_the_right);
        const int reach = (ahead != facing_left()) ? 14 : -14;
        world.spawn(*this, kind::kSpark, x + reach, y - 4, 0, entity_flag::kLargePieces);
    }

    // A blow takes his health, without throwing him. True if it killed him.
    bool told_all(World& world) {
        if (message != message::kSwordBlow && message != message::kArrowBlow && message != message::kHeadButt &&
            message != message::kBlast) {
            message = 0;
            return false;
        }
        world.sound(*this, effect::kEnemyHurt);
        if (!survives_blow(*this)) {
            leave_remains(world);
            return true;
        }
        return false;
    }

    struct Saved {
        At at = At::kStart;
    } s;
    LVR_SAVES(Enemy)
};

// A bomber's bomb: thrown in an arc, or (another kind's) straight at the nearest Viking. It bursts on a wall, the
// ground or Olaf's shield, and hurts a Viking it hits.
class Bomb : public Entity {
  public:
    void update(World& world) override {
        switch (s.state) {
            case State::kStart:
                world.play(*this, anim::kBombFlies);
                if (argument == 0) {
                    aim(world);
                    s.state = State::kStraight;
                } else {
                    start_falling();
                    s.state = State::kRising;
                }
                break;
            case State::kRising:
                if (saw.wall || saw.shield != kNobody || saw.shield_overhead != kNobody) {
                    return burst(world);
                }
                if (saw.viking != kNobody) {
                    return hit(world);
                }
                if (speed_y >= 0) {
                    s.state = State::kFalling;
                }
                break;
            case State::kFalling:
                if (saw.wall || saw.landed) {
                    return burst(world);
                }
                if (saw.shield != kNobody) {
                    // Olaf's shield before him: past it only from behind, or right at him.
                    target = slot::kOlaf;
                    const Entity* olaf = world.at(slot::kOlaf);
                    if (olaf != nullptr && olaf->facing_left() != facing_left() &&
                        world.offset_to(*this, slot::kOlaf).ahead >= 4) {
                        return burst(world);
                    }
                }
                if (saw.shield_overhead != kNobody) {
                    return burst(world);
                }
                if (saw.viking != kNobody) {
                    return hit(world);
                }
                break;
            case State::kStraight:
                if (saw.wall) {
                    return burst(world);
                }
                if (saw.shield != kNobody) {
                    target = saw.shield;
                    const Entity* shield_entity = world.at(saw.shield);
                    if (shield_entity == nullptr || shield_entity->kind != kind::kShield ||
                        shield_entity->facing_left() != facing_left()) {
                        return burst(world);
                    }
                }
                if (saw.viking != kNobody) {
                    return hit(world);
                }
                break;
            case State::kBurst:
                if (animation_over()) {
                    world.remove(slot);
                    return;
                }
                break;
        }
        world.animate(*this);
    }
    Looks looks_for() const override {
        const Look shield_touched = touching_any(hit::kShield, &Saw::shield);
        const Look viking = touching_any(hit::kViking, &Saw::viking);
        switch (s.state) {
            case State::kRising:
                return {stopped_by(solid()), shield_touched, touching_any(hit::kShieldOverhead, &Saw::shield_overhead),
                        viking};
            case State::kFalling:
                return {stopped_by(solid()),
                        lands_on(solid()),
                        or_else_lands_on(shield_up()),
                        shield_touched,
                        touching_any(hit::kShieldOverhead, &Saw::shield_overhead),
                        viking};
            case State::kStraight:
                return {stopped_by(solid()), shield_touched, viking};
            default:
                return {};
        }
    }

  private:
    enum class State : std::uint8_t { kStart, kRising, kFalling, kStraight, kBurst };
    static constexpr int kStraightSpeed = 12;

    static const TileTypes& shield_up() {
        static const TileTypes types = tile_types({tile::kShieldOverhead});
        return types;
    }

    // Its speed towards the nearest Viking: the distances across and down halved until the longer is at most
    // 12 pixels a tick, with what the halving drops kept as the fractions.
    void aim(World& world) {
        const int who = world.nearest_viking(*this);
        target = who;
        const Entity* viking = world.at(who);
        if (viking == nullptr) {
            return;
        }
        auto across = static_cast<std::int16_t>(viking->x - x);
        auto down = static_cast<std::int16_t>(viking->y - y);
        const bool left = across < 0;
        const bool up = down < 0;
        auto dx = static_cast<std::uint16_t>(left ? -across : across);
        auto dy = static_cast<std::uint16_t>(up ? -down : down);
        std::uint16_t longest = dy >= dx ? dy : dx;
        std::uint16_t fraction_x = 0;
        std::uint16_t fraction_y = 0;
        while (longest > kStraightSpeed) {
            longest = static_cast<std::uint16_t>(longest / 2U);
            fraction_x = static_cast<std::uint16_t>((fraction_x / 2U) + ((dx % 2U) * 0x8000U));
            dx = static_cast<std::uint16_t>(dx / 2U);
            fraction_y = static_cast<std::uint16_t>((fraction_y / 2U) + ((dy % 2U) * 0x8000U));
            dy = static_cast<std::uint16_t>(dy / 2U);
        }
        const auto vx =
            static_cast<std::int16_t>(static_cast<std::uint16_t>(((dx % 256U) * 256U) + (fraction_x / 256U)));
        const auto vy =
            static_cast<std::int16_t>(static_cast<std::uint16_t>(((dy % 256U) * 256U) + (fraction_y / 256U)));
        speed_x = static_cast<std::int16_t>(left != facing_left() ? -vx : vx);
        speed_y = static_cast<std::int16_t>(up != upside_down() ? -vy : vy);
    }

    void hit(World& world) {
        hit_viking_touched(world, *this, x);
        world.remove(slot);
    }

    void burst(World& world) {
        world.sound(*this, effect::kBurst);
        world.play(*this, anim::kBombBursts);
        speed_x = 0;
        stop_falling();
        s.state = State::kBurst;
        world.animate(*this);
    }

    struct Saved {
        State state = State::kStart;
    } s;
    LVR_SAVES(Entity)
};

}  // namespace

std::span<const KindOf> bombers() {
    static constexpr KindOf kList[] = {
        {Home::kAnywhere, kind::kBomber, make<Bomber>},
        {Home::kEgypt, egypt_kind::kStoneThrower, make<StoneThrower>},
        {Home::kEgypt, egypt_kind::kPotDweller, make<PotDweller>},
        {Home::kAnywhere, kind::kBomb, make<Bomb>},
    };
    return kList;
}

}  // namespace lvr
