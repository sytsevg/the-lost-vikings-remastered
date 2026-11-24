// The enemies that walk back and forth and strike a Viking they touch.

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

// Touching a Viking hurts him, from the side the enemy is on.
void knock(World& world, Entity& enemy, int who) {
    Entity* other = world.at(who);
    if (other == nullptr) {
        return;
    }
    const bool ahead = world.offset_to(enemy, who).ahead >= 0;
    const bool from_the_right = ahead ? enemy.facing_left() : !enemy.facing_left();
    harm(*other, message::kStruck, 2, from_the_right);
    const int reach = (ahead != enemy.facing_left()) ? 14 : -14;
    world.spawn(enemy, kind::kSpark, enemy.x + reach, enemy.y - 4, 0, entity_flag::kLargePieces);
}

// A robot that walks back and forth and hurts a Viking it touches (then it stands for a moment).
class WalkingRobot : public Enemy {
  public:
  protected:
    Looks looks() const override {
        switch (s.at) {
            case At::kWalking:
                return {stopped_by(walls()), touching_an_enemy(), touching_a_viking()};
            case At::kFalling:
                return {stopped_by(walls()), lands_on(ground())};
            case At::kHurt:
                return {stopped_by(walls())};
            default:
                return {};
        }
    }
    void turn(World& world) override {
        switch (s.at) {
            case At::kStart:
                appear(world, anim::kWalkerAppears);
                return set_off(world);
            case At::kWalking:
                return when_walking(world);
            case At::kFalling:
                return when_falling(world);
            case At::kHurt:
                return when_hurt(world);
            case At::kLed:
                if (told_all(world)) {
                    return;
                }
                return led_on(world);
        }
    }
    void when_walking(World& world) {
        if (saw.wall) {
            turn_round(world, kTurnedTicks);
        }
        turn_from_colleague(world);
        if (saw.viking != kNobody) {
            target = saw.viking;
            speed_x = 0;
            countdown = 35;
            knock(world, *this, saw.viking);
        }
        if (told_all(world)) {
            return;
        }
        stay_in_view(world);
        if (!can_walk_on(world, *this)) {
            if (!on_ground(world)) {
                start_falling();
                s.at = At::kFalling;
                world.animate(*this);
                return;
            }
            turn_round(world, 20);
        } else if (countdown == 0) {
            if (speed_x == 0) {
                speed_x = kPace;
            }
            face_viking_behind(world);
        }
        return walk(world);
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
    void when_hurt(World& world) {
        if (saw.wall) {
            speed_x = 0;
        }
        if (animation_over()) {
            return set_off(world);
        }
        world.animate_on_ground(*this);
    }
    void recovered(World& /*world*/) override {}

  private:
    enum class At : std::uint8_t { kStart, kWalking, kFalling, kHurt, kLed };
    static constexpr std::int16_t kPace = px(3);

    void set_off(World& world) {
        speed_x = kPace;
        world.play(*this, anim::kWalkerWalks);
        walk(world);
    }
    void walk(World& world) {
        world.animate_on_ground(*this);
        s.at = At::kWalking;
    }

    // A blow, in his own way; whatever else he is told he forgets.
    bool struck(World& world) override {
        switch (message) {
            case message::kSwordBlow:
            case message::kArrowBlow:
            case message::kHeadButt:
            case message::kBlast: {
                world.sound(*this, effect::kEnemyHurt);
                const bool from_the_right = blow_from_the_right();
                if (!survives_blow(*this)) {
                    leave_remains(world);
                    return true;
                }
                speed_x = facing_left() == from_the_right ? px(3) : px(-4);
                stop_falling();
                world.play(*this, anim::kWalkerIsHurt);
                world.animate_on_ground(*this);
                s.at = At::kHurt;
                return true;
            }
            default:
                message = 0;
                return false;
        }
    }
    void led_to_attack(World& world) override {
        speed_x = 0;
        follow(world, anim::kWalkerWalks, Led::kAnimated);
        s.at = At::kLed;
    }
    void led_to_stand(World& world) override {
        speed_x = 0;
        follow(world, anim::kWalkerWalks, Led::kAnimated);
        s.at = At::kLed;
    }
    void led_to_walk(World& world) override {
        speed_x = kPace;
        follow(world, anim::kWalkerWalks, Led::kMoving);
        s.at = At::kLed;
    }

    struct Saved {
        At at = At::kStart;
    } s;
    LVR_SAVES(Enemy)
};

// An enemy that walks back and forth inside the view, turns at walls and ledges, and strikes a Viking he touches
// who is about level with him, for as long as he touches one. Two kinds: the second walks faster.
class Brawler : public Enemy {
  public:
    Looks looks() const override {
        switch (s.at) {
            case At::kAppeared:
            case At::kStriking:
                return {touching_one_of_the_vikings_class()};
            case At::kWalking:
                return {stopped_by(solid()), touching_an_enemy(), touching_one_of_the_vikings_class()};
            case At::kFalling:
                return {stopped_by(solid()), lands_on(ground())};
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
                    return touched(world);
                }
                return set_off(world);
            case At::kWalking:
                return when_walking(world);
            case At::kStriking:
                return striking(world);
            case At::kFalling:
                return when_falling(world);
            case At::kLed:
                if (message != 0 && told_more(world)) {
                    return;
                }
                return led_on(world);
        }
    }
    void at_start(World& world) {
        world.play(*this, anim::kBrawlerAppears);
        world.animate(*this);
        take_kept_health();
        acts_as = tile::kEnemy;
        s.pace = kind == prehistoric_kind::kFastBrawler ? px(4) : px(3);
        count_down();
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
        if (saw.viking != kNobody) {
            target = saw.viking;
            return touched(world);
        }
        if (message != 0 && told_more(world)) {
            return;
        }
        stay_in_view(world);
        if (!can_walk_on(world, *this)) {
            if (!on_ground(world)) {
                return fall(world);
            }
            turn_round(world, kTurnedTicks);
            return walking(world);
        }
        face_viking_behind(world);
        return walking(world);
    }
    void when_falling(World& world) {
        if (landed_from_fall()) {
            return set_off(world);
        }
        if (message != 0 && told_more(world)) {
            return;
        }
        world.animate(*this);
    }
    void recovered(World& world) override { set_off(world); }

  private:
    enum class At : std::uint8_t { kStart, kAppeared, kWalking, kStriking, kFalling, kLed };
    static constexpr std::uint16_t kStrikeOver = 10;  // the frame his strike ends with

    void set_off(World& world) {
        world.play(*this, anim::kBrawlerWalks);
        speed_x = s.pace;
        walking(world);
    }

    void walking(World& world) {
        world.animate_on_ground(*this);
        s.at = At::kWalking;
    }

    void fall(World& world) {
        start_falling();
        world.animate(*this);
        s.at = At::kFalling;
    }

    // A Viking touches him: about level with him (or another one that is), he turns to him and strikes.
    void touched(World& world) {
        World::Offset to;
        if (!level_with(world, to)) {
            return walking(world);
        }
        if (to.ahead < 0) {
            turn_round(world);
        }
        world.play(*this, anim::kBrawlerStrikes);
        speed_x = 0;
        s.still_touching = true;
        world.animate(*this);
        s.at = At::kStriking;
    }

    void striking(World& world) {
        if (saw.viking != kNobody) {
            target = saw.viking;
            s.still_touching = true;
        }
        World::Offset to;
        if (s.still_touching && level_with(world, to)) {
            if (to.ahead < 0) {
                turn_round(world);
            }
            if (animation_over()) {
                strike(world);
            }
        } else if (sprite.packed_frame == kStrikeOver) {
            return set_off(world);
        }
        if (message != 0 && told_more(world)) {
            return;
        }
        if (!on_ground(world)) {
            world.play(*this, anim::kBrawlerWalks);
            return fall(world);
        }
        s.still_touching = false;
        world.animate(*this);
    }

    void strike(World& world) {
        Entity* other = world.at(target);
        if (other == nullptr || other->kind >= kVikingSlots) {
            return;
        }
        harm(*other, message::kStruck, 2, facing_left());
        world.spawn(*this, kind::kSpark, x + (facing_left() ? -14 : 14), y - 4, 0, entity_flag::kLargePieces);
    }

    // What he is told besides blows: to stand, to walk on, to turn and walk, to go. (Nothing in the levels tells
    // him; the cutscenes' database has the same kind.)
    bool told_more(World& world) {
        switch (message) {
            case message::kLedAttack:
                lead(world, anim::kBrawlerStrikes, 0, true);
                return true;
            case message::kLedStand:
                lead(world, anim::kBrawlerWalks, 0, false);
                return true;
            case message::kLedTurn:
                turn_round(world);
                [[fallthrough]];
            case message::kLedWalk:
                lead(world, anim::kBrawlerWalks, px(4), true);
                return true;
            case message::kLedLeave:
                gone_for_good(world, *this);
                return true;
            case message::kLedColours:
                if (told_colours(world)) {
                    return false;
                }
                world.remove(slot);
                return true;
            default:
                return told(world);
        }
    }

    void lead(World& world, Animation animation, std::int16_t speed, bool moving) {
        speed_x = speed;
        follow(world, animation, moving ? Led::kMoving : Led::kStill);
        s.at = At::kLed;
    }

    struct Saved {
        At at = At::kStart;
        std::int16_t pace = 0;
        bool still_touching = false;
    } s;
    LVR_SAVES(Enemy)
};

// An enemy that shuffles back and forth (his walk moves him, not a speed), turns at walls and ledges, and takes
// hold of a Viking he touches who is about level with him: the Viking is gone.
class Mummy : public Enemy {
  public:
  protected:
    Looks looks() const override {
        switch (s.at) {
            case At::kAppeared:
            case At::kGrabbing:
                return {touching_one_of_the_vikings_class()};
            case At::kWalking:
                return {stopped_by(walls()), touching_an_enemy(), touching_one_of_the_vikings_class()};
            case At::kFalling:
                return {stopped_by(walls()), lands_on(ground())};
            default:
                return {};
        }
    }
    void turn(World& world) override {
        switch (s.at) {
            case At::kStart:
                return at_start(world);
            case At::kAppeared:
                if (saw.viking != kNobody) {
                    target = saw.viking;
                    return touched(world);
                }
                return set_off(world);
            case At::kWalking:
                return when_walking(world);
            case At::kGrabbing:
                return when_grabbing(world);
            case At::kFalling:
                return when_falling(world);
            case At::kLed:
                if (told_all(world)) {
                    return;
                }
                return led_on(world);
        }
    }
    void at_start(World& world) {
        appear(world, anim::kMummyAppears);
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
        if (saw.viking != kNobody) {
            target = saw.viking;
            return touched(world);
        }
        if (told_all(world)) {
            return;
        }
        stay_in_view(world);
        if (!can_walk_on(world, *this)) {
            if (!on_ground(world)) {
                return fall(world);
            }
            turn_round(world, 30);
        } else {
            face_viking_behind(world);
        }
        return walking(world);
    }
    void when_grabbing(World& world) {
        if (saw.viking != kNobody) {
            target = saw.viking;
            s.still_touching = true;
        }
        World::Offset to;
        if (s.still_touching && level_with(world, to)) {
            if (to.ahead < 0) {
                turn_round(world);
            }
            if (animation_over()) {
                Entity* other = world.at(target);
                if (other != nullptr && other->kind < kVikingSlots) {
                    other->message = message::kMummified;
                    other->forget_blow_side();
                    if (facing_left()) {
                        other->message_value |= kFromTheRight;
                    }
                    world.spawn(*this, kind::kSpark, x + (facing_left() ? -14 : 14), y - 4, 0,
                                entity_flag::kLargePieces);
                }
            }
        } else if (sprite.packed_frame == kGrabOver) {
            return set_off(world);
        }
        if (told_all(world)) {
            return;
        }
        s.still_touching = false;
        if (!on_ground(world)) {
            return fall(world);
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
    void recovered(World& world) override {
        if (s.still_touching) {
            return grab(world);
        }
        countdown = 30;
        set_off(world);
    }

  private:
    enum class At : std::uint8_t { kStart, kAppeared, kWalking, kGrabbing, kFalling, kLed };
    static constexpr std::uint16_t kGrabOver = 11;

    void set_off(World& world) {
        speed_x = 0;
        world.play(*this, anim::kMummyWalks);
        walking(world);
    }
    void walking(World& world) {
        world.animate_on_ground(*this);
        s.at = At::kWalking;
    }
    void fall(World& world) {
        world.play(*this, anim::kMummyFalls);
        start_falling();
        world.animate(*this);
        s.at = At::kFalling;
    }
    void grab(World& world) {
        world.play(*this, anim::kMummyGrabs);
        speed_x = 0;
        s.still_touching = true;
        world.animate(*this);
        s.at = At::kGrabbing;
    }
    void touched(World& world) {
        World::Offset to;
        if (!level_with(world, to)) {
            return walking(world);
        }
        if (to.ahead < 0) {
            turn_round(world);
        }
        grab(world);
    }

    void led_to_attack(World& world) override { lead(world, anim::kMummyGrabs, true, true); }
    void led_to_stand(World& world) override { lead(world, anim::kMummyWalks, true, false); }
    void led_to_walk(World& world) override { lead(world, anim::kMummyWalks, false, true); }
    void lead(World& world, Animation animation, bool stops, bool moving) {
        if (stops) {
            speed_x = 0;
        }
        follow(world, animation, moving ? Led::kMoving : Led::kStill);
        s.at = At::kLed;
    }

    struct Saved {
        At at = At::kStart;
        bool still_touching = false;
    } s;
    LVR_SAVES(Enemy)
};

// An enemy that runs back and forth, strikes a Viking he touches who is about level with him, and shoots at
// one further ahead. Spikes under him kill him.
class Clown : public Enemy {
  public:
    Clown() { enemy.own_hurt_animation = anim::kClownIsHurt; }
    Looks looks() const override {
        switch (s.at) {
            case At::kAppeared:
            case At::kStriking:
                return {touching_one_of_the_vikings_class()};
            case At::kWalking:
                return {stopped_by(walls()), touching_one_of_the_vikings_class()};
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
                    return touched(world);
                }
                return set_off(world);
            case At::kWalking:
                return when_walking(world);
            case At::kStriking:
                return when_striking(world);
            case At::kAiming:
                return when_aiming(world);
            case At::kAfterShot:
                return when_after_shot(world);
            case At::kFalling:
                return when_falling(world);
            case At::kLed:
                return when_led(world);
        }
    }
    void at_start(World& world) {
        appear(world, anim::kClownAppears);
        if (is_falling()) {
            return fall(world);
        }
        s.at = At::kAppeared;
    }
    void when_walking(World& world) {
        if (saw.wall) {
            turn_round(world);
        }
        if (saw.viking != kNobody) {
            target = saw.viking;
            return touched(world);
        }
        if (dies_on_spikes(world) || (told_all(world))) {
            return;
        }
        stay_in_view(world);
        if (!can_walk_on(world, *this)) {
            if (!on_ground(world)) {
                return fall(world);
            }
            turn_round(world);
        }
        if (countdown == 0 && viking_ahead(world, 16)) {
            return aim(world);
        }
        return walk(world);
    }
    void when_striking(World& world) {
        if (saw.viking != kNobody) {
            target = saw.viking;
            s.still_touching = true;
        }
        if (dies_on_spikes(world)) {
            return;
        }
        World::Offset to;
        if (s.still_touching && level_with(world, to)) {
            if (!on_ground(world)) {
                return fall(world);
            }
            if (to.ahead < 0) {
                turn_round(world);
            }
            if (animation_over()) {
                Entity* other = world.at(target);
                if (other != nullptr && other->kind < kVikingSlots) {
                    harm(*other, message::kStruck, 2, facing_left());
                    world.spawn(*this, kind::kSpark, x + (facing_left() ? -14 : 14), y - 4, 0,
                                entity_flag::kLargePieces);
                }
            }
        } else if (sprite.packed_frame == kStrikeOver) {
            return set_off(world);
        }
        if (told_all(world)) {
            return;
        }
        s.still_touching = false;
        world.animate(*this);
    }
    void when_aiming(World& world) {
        if (dies_on_spikes(world) || (told_all(world))) {
            return;
        }
        if (animation_over()) {
            world.spawn(*this, wacky_kind::kFlameShot, x, y - 4);
            world.sound(*this, effect::kArrowLoosed);
            world.play(*this, anim::kClownHasShot);
            s.at = At::kAfterShot;
        }
        world.animate(*this);
    }
    void when_after_shot(World& world) {
        if (dies_on_spikes(world)) {
            return;
        }
        if (animation_over()) {
            countdown = 15;
            return set_off(world);
        }
        if (told_all(world)) {
            return;
        }
        world.animate(*this);
    }
    void when_falling(World& world) {
        if (landed_from_fall()) {
            return set_off(world);
        }
        if (dies_on_spikes(world) || (told_all(world))) {
            return;
        }
        world.animate(*this);
    }
    void when_led(World& world) {
        if (told_all(world)) {
            return;
        }
        if (s.led_bows && animation_over()) {
            return lead(world, anim::kClownStands, 0, false, true);
        }
        return led_on(world);
    }
    void recovered(World& world) override { aim(world); }

  private:
    enum class At : std::uint8_t { kStart, kAppeared, kWalking, kStriking, kAiming, kAfterShot, kFalling, kLed };
    static constexpr std::uint16_t kStrikeOver = 21;

    void set_off(World& world) {
        world.play(*this, anim::kClownRuns);
        speed_x = px(5);
        walk(world);
    }
    void walk(World& world) {
        world.animate_on_ground(*this);
        s.at = At::kWalking;
    }
    void aim(World& world) {
        world.play(*this, anim::kClownAims);
        speed_x = 0;
        world.animate(*this);
        s.at = At::kAiming;
    }
    void fall(World& world) {
        world.play(*this, anim::kClownFalls);
        start_falling();
        world.animate(*this);
        s.at = At::kFalling;
    }

    // A Viking touches him: about level with him (or another one that is), he turns to him, shouts and strikes.
    void touched(World& world) {
        World::Offset to;
        if (!level_with(world, to)) {
            return walk(world);
        }
        if (to.ahead < 0) {
            turn_round(world);
        }
        const std::uint16_t dice = world.random();
        world.sound(*this, dice < 21850 ? effect::kClownStrikes1
                                        : (dice < 43700 ? effect::kClownStrikes2 : effect::kClownStrikes3));
        world.play(*this, anim::kClownStrikes);
        speed_x = 0;
        s.still_touching = true;
        world.animate(*this);
        s.at = At::kStriking;
    }

    void led_to_attack(World& world) override {
        lead(world, anim::kClownBows, 0, false, true);
        s.led_bows = true;
    }
    void led_to_stand(World& world) override { lead(world, anim::kClownStands, 0, false, true); }
    void led_to_walk(World& world) override { lead(world, anim::kClownRuns, px(3), true, true); }

    void lead(World& world, Animation animation, std::int16_t speed, bool moving, bool /*animated*/) {
        speed_x = speed;
        follow(world, animation, moving ? Led::kMoving : Led::kAnimated, animation != anim::kClownBows);
        s.led_bows = false;
        s.at = At::kLed;
    }

    struct Saved {
        At at = At::kStart;
        bool still_touching = false;
        bool led_bows = false;
    } s;
    LVR_SAVES(Enemy)
};

}  // namespace

std::span<const KindOf> walkers() {
    static constexpr KindOf kList[] = {
        {Home::kFactory, factory_kind::kWalker, make<WalkingRobot>},
        {Home::kPrehistoric, prehistoric_kind::kBrawler, make<Brawler>},
        {Home::kPrehistoric, prehistoric_kind::kFastBrawler, make<Brawler>},
        {Home::kEgypt, egypt_kind::kMummy, make<Mummy>},
        {Home::kWacky, wacky_kind::kClown, make<Clown>},
    };
    return kList;
}

}  // namespace lvr
