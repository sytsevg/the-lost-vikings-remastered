// The enemies with a way of their own of getting at the Vikings: the one that hops, the one that rolls, the one
// that charges.

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

// An enemy that runs a few steps and then hops, forwards or (after a fight) backwards. A Viking he touches who
// is about level with him he strikes, also in the air. Olaf's shield turns him. Spikes under him kill him.
class Hopper : public Enemy {
  public:
    Looks looks() const override {
        const Look viking = touching_one_of_the_vikings_class();
        const Look shield_touched = touching_any(hit::kShield, &Saw::shield);
        switch (at_) {
            case At::kAppeared:
                return {viking};
            case At::kRunning:
                return {stopped_by(walls()), touching_an_enemy(), viking};
            case At::kStriking:  // on his way up a ceiling stops him, on his way down the ground
                return strikes_rising_ ? Looks{stopped_above_or_below(solid()), viking}
                                       : Looks{lands_on(ground()), viking};
            case At::kRising:
                return {stopped_by(walls()), stopped_above_or_below(solid()), shield_touched, viking};
            case At::kFalling:
                return {stopped_by(walls()), stopped_by(shield(), &Saw::shield_wall), lands_on(ground()),
                        shield_touched, viking};
            default:
                return {};
        }
    }

  protected:
    void turn(World& world) override {
        switch (at_) {
            case At::kStart:
                return at_start(world);
            case At::kAppeared:
                if (saw.viking != kNobody && touched(world)) {
                    return;
                }
                return run(world);
            case At::kRunning:
                return when_running(world);
            case At::kStriking:
                return when_striking(world);
            case At::kRising:
                return when_rising(world);
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
        appear(world, anim::kHopperAppears);
        if (is_falling()) {
            return fall(world);
        }
        at_ = At::kAppeared;
    }
    void when_running(World& world) {
        if (saw.wall) {
            turn_round(world, kTurnedTicks);
        }
        turn_from_colleague(world);
        if (saw.viking != kNobody && touched(world)) {
            return;
        }
        if (find_beside(world, *this, Side::kBelow, spikes())) {
            return leave_remains(world);
        }
        if (told_all(world)) {
            return;
        }
        stay_in_view(world);
        if (!can_walk_on(world, *this)) {
            if (!on_ground(world)) {
                return fall(world);
            }
            turn_round(world, 20);
        } else {
            face_viking_behind(world);
        }
        if (steps_ == 0) {
            return hop(world);
        }
        --steps_;
        return running(world);
    }
    void when_striking(World& world) {
        if (strikes_rising_ ? saw.ceiling : saw.landed) {
            speed_y = 0;
            if (!strikes_rising_) {
                clear_flag(entity_flag::kFalls);
                set_flag(entity_flag::kPutOnGround);
            }
        }
        if (saw.viking != kNobody) {
            target = saw.viking;
            still_touching_ = true;
        }
        World::Offset to;
        if (sprite.packed_frame == kStrikeOver || !level_with(world, to)) {
            return jump_away(world);
        }
        if (to.ahead < 0) {
            turn_round(world);
        }
        if (animation_over() && still_touching_) {
            Entity* other = world.at(target);
            if (other != nullptr && other->kind < kVikingSlots) {
                harm(*other, message::kStruck, 2, facing_left());
                world.spawn(*this, kind::kSpark, x + (facing_left() ? -14 : 14), y - 4, 0, entity_flag::kLargePieces);
            }
        }
        if (told_all(world)) {
            return;
        }
        return striking(world);
    }
    void when_rising(World& world) {
        if (saw.wall) {
            speed_x = 0;
        }
        if (saw.ceiling) {
            speed_y = 0;
        }
        if (saw.shield != kNobody) {
            off_the_shield(world);
        }
        if (saw.viking != kNobody && touched(world)) {
            return;
        }
        if (told_all(world)) {
            return;
        }
        stay_in_view(world);
        return in_the_air(world);
    }
    void when_falling(World& world) {
        if (saw.wall) {
            speed_x = 0;
        }
        if (saw.shield_wall) {
            turn_round(world);
        }
        if (saw.landed) {
            stop_falling();
            return run(world);
        }
        if (saw.shield != kNobody) {
            off_the_shield(world);
        }
        if (saw.viking != kNobody && touched(world)) {
            return;
        }
        if (find_beside(world, *this, Side::kBelow, spikes())) {
            return leave_remains(world);
        }
        if (told_all(world)) {
            return;
        }
        stay_in_view(world);
        world.animate(*this);
    }
    void recovered(World& world) override { jump_away(world); }

  private:
    enum class At : std::uint8_t { kStart, kAppeared, kRunning, kStriking, kRising, kFalling, kLed };
    static constexpr std::uint16_t kStrikeOver = 8;
    static constexpr std::int16_t kPace = px(3.5);
    static constexpr std::int16_t kHop = px(-10);

    static const TileTypes& spikes() {
        static const TileTypes types = tile_types({tile::kSpikes});
        return types;
    }

    void run(World& world) {
        world.play(*this, anim::kHopperRuns);
        speed_x = kPace;
        const std::uint16_t dice = world.random();
        steps_ = dice < 20000 ? 5 : (dice < 40000 ? 10 : 15);
        running(world);
    }

    void running(World& world) {
        world.animate_on_ground(*this);
        at_ = At::kRunning;
    }

    void hop(World& world) {
        if (on_ground(world)) {
            speed_y = kHop;
            world.play(*this, anim::kHopperHops);
        }
        fall(world);
    }

    // After a fight, or a blow: half the time he hops on, half the time back.
    void jump_away(World& world) {
        if (!on_ground(world)) {
            return fall(world);
        }
        speed_x = kPace;
        if (world.random() < 32000) {
            return hop(world);
        }
        speed_y = kHop;
        speed_x = -kPace;
        world.play(*this, anim::kHopperHops);
        fall(world);
    }

    void fall(World& world) {
        start_falling();
        in_the_air(world);
    }

    void in_the_air(World& world) {
        if (speed_y < 0) {
            world.animate(*this);
            at_ = At::kRising;
            return;
        }
        world.play(*this, anim::kHopperComesDown);
        world.animate(*this);
        at_ = At::kFalling;
    }

    void striking(World& world) {
        still_touching_ = false;
        strikes_rising_ = speed_y < 0;
        world.animate(*this);
        at_ = At::kStriking;
    }

    // Olaf's shield, held towards him from six pixels or more ahead: he turns, and the touch doesn't count.
    void off_the_shield(World& world) {
        target = slot::kOlaf;
        const Entity* olaf = world.at(slot::kOlaf);
        if (olaf == nullptr || olaf->facing_left() == facing_left() || world.offset_to(*this, slot::kOlaf).ahead < 6) {
            return;
        }
        turn_round(world);
        kind_value = 1;
    }

    // A Viking touches him. True if his turn ended: he strikes, or jumps away.
    bool touched(World& world) {
        target = saw.viking;
        if (kind_value == 1) {
            kind_value = 0;
            return false;
        }
        World::Offset to;
        if (!level_with(world, to)) {
            jump_away(world);
            return true;
        }
        if (to.ahead < 0) {
            turn_round(world);
        }
        world.play(*this, anim::kHopperStrikes);
        speed_x = 0;
        striking(world);
        return true;
    }

    void led_to_attack(World& world) override { lead(world, anim::kHopperStrikes, 0, true); }
    void led_to_stand(World& world) override { lead(world, anim::kHopperRuns, 0, false); }
    void led_to_walk(World& world) override { lead(world, anim::kHopperRuns, kPace, true); }

    void lead(World& world, Animation animation, std::int16_t speed, bool moving) {
        speed_x = speed;
        follow(world, animation, moving ? Led::kMoving : Led::kStill);
        at_ = At::kLed;
    }

    At at_ = At::kStart;
    bool still_touching_ = false;
    bool strikes_rising_ = false;
    std::uint16_t steps_ = 0;
};

// An enemy that by turns rolls along, armoured, and walks. Rolling, blows glance off him (he turns from sword and
// arrows; only a hard blow kills him), he knocks into the Vikings and bounces off Olaf's shield. Walking, he can
// be hurt, and he strikes a Viking he touches who is about level with him.
class Roller : public Enemy {
  public:
    Looks looks() const override {
        if (on_the_ship_) {  // the ship's scene: nothing stops him but the ground under him, and he touches nobody
            return at_ == At::kFalling || at_ == At::kShipFalling ? Looks{lands_on(ground())} : Looks{};
        }
        switch (at_) {
            case At::kRolling:
                return {stopped_by(walls()), stopped_by(shield(), &Saw::shield_wall), touching_a_viking()};
            case At::kWalking:
                return {stopped_by(walls()), touching_an_enemy(), touching_one_of_the_vikings_class()};
            case At::kStriking:
                return {touching_one_of_the_vikings_class()};
            case At::kFalling:
                return {stopped_by(solid()), lands_on(ground())};
            default:
                return {};
        }
    }

  protected:
    void turn(World& world) override {
        switch (at_) {
            case At::kStart:
                return at_start(world);
            case At::kShipRolling:
                return when_ship_rolling(world);
            case At::kShipWalking:
                return when_ship_walking(world);
            case At::kShipFalling:
                return when_ship_falling(world);
            case At::kRolling:
                return when_rolling(world);
            case At::kWalking:
                return when_walking(world);
            case At::kStriking:
                return when_striking(world);
            case At::kFalling:
                return when_falling(world);
            case At::kLed:
                return when_led(world);
        }
    }
    void at_start(World& world) {
        appear(world, anim::kRollerAppears);
        if (world.database == database::kScenes && world.scene == scene::kShip) {
            // The ship's scene: he only rolls and walks by turns, until he is led.
            on_the_ship_ = true;
            world.play(*this, anim::kRollerOnTheShip);
            world.animate(*this);
            return ship_roll(world);
        }
        if (is_falling()) {
            world.play(*this, anim::kRollerRolls);
            return fall(world);
        }
        return roll(world);
    }
    void when_ship_rolling(World& world) {
        stay_in_view(world);
        if (message != 0 && told_walking(world)) {
            return;
        }
        if (!can_walk_on(world, *this)) {
            if (!on_ground(world)) {
                start_falling();
                world.animate(*this);
                at_ = At::kShipFalling;
                return;
            }
            turn_round(world);
        } else if (countdown == 0) {
            countdown = 40;
            world.play(*this, anim::kRollerWalks);
            return go(world, At::kShipWalking);
        }
        return go(world, At::kShipRolling);
    }
    void when_ship_walking(World& world) {
        stay_in_view(world);
        if (!can_walk_on(world, *this)) {
            turn_round(world);
        } else if (countdown == 0) {
            return ship_roll(world);
        }
        return go(world, At::kShipWalking);
    }
    void when_ship_falling(World& world) {
        if (saw.landed) {
            stop_falling();
            return go(world, At::kShipRolling);
        }
        if (message != 0 && told_walking(world)) {
            return;
        }
        world.animate(*this);
    }
    void when_rolling(World& world) {
        if (saw.wall) {
            turn_round(world);
        }
        if (saw.shield_wall) {
            target = slot::kOlaf;
            world.tell(slot::kOlaf, message::kBlownBack);
            turn_round(world);
        }
        if (saw.viking != kNobody) {
            target = saw.viking;
            knock(world);
        }
        stay_in_view(world);
        if (message != 0 && told_rolling(world)) {
            return;
        }
        if (!can_walk_on(world, *this)) {
            if (!on_ground(world)) {
                return fall(world);
            }
            turn_round(world);
            return go(world, At::kRolling);
        }
        if (countdown == 0) {
            return walk(world);
        }
        return go(world, At::kRolling);
    }
    void when_walking(World& world) {
        if (saw.wall) {
            turn_round(world);
        }
        turn_from_colleague(world);
        if (saw.viking != kNobody) {
            target = saw.viking;
            World::Offset to;
            if (!level_with(world, to)) {
                return go(world, At::kWalking);
            }
            if (to.ahead < 0) {
                turn_round(world);
            }
            world.play(*this, anim::kRollerStrikes);
            speed_x = 0;
            still_touching_ = false;
            world.animate(*this);
            at_ = At::kStriking;
            return;
        }
        if (message != 0 && told_walking(world)) {
            return;
        }
        stay_in_view(world);
        if (!can_walk_on(world, *this)) {
            if (!on_ground(world)) {
                return fall(world);
            }
            turn_round(world);
            return go(world, At::kWalking);
        }
        if (countdown != 0) {
            return go(world, At::kWalking);
        }
        return roll(world);
    }
    void when_striking(World& world) {
        if (saw.viking != kNobody) {
            target = saw.viking;
            still_touching_ = true;
        }
        World::Offset to;
        if (level_with(world, to)) {
            if (to.ahead < 0) {
                turn_round(world);
            }
            if (animation_over() && still_touching_) {
                Entity* other = world.at(target);
                if (other != nullptr && other->kind < kVikingSlots) {
                    harm(*other, message::kStruck, 2, facing_left());
                    world.spawn(*this, kind::kSpark, x + (facing_left() ? -14 : 14), y - 4, 0,
                                entity_flag::kLargePieces);
                }
            }
        }
        if (message != 0 && told_walking(world)) {
            return;
        }
        if (!on_ground(world)) {
            world.play(*this, anim::kRollerRolls);
            return fall(world);
        }
        if (sprite.packed_frame == kStrikeOver) {
            world.shake_y = {.amount = 1, .offset = 0, .ticks = 4};
            turn_round(world);
            return roll(world);
        }
        still_touching_ = false;
        world.animate(*this);
    }
    void when_falling(World& world) {
        if (landed_from_fall()) {
            return roll(world);
        }
        if (message != 0 && told_walking(world)) {
            return;
        }
        world.animate(*this);
    }
    void when_led(World& world) {
        if (led_stamps_ && sprite.packed_frame == kStrikeOver) {
            world.shake_y = {.amount = 1, .offset = 0, .ticks = 4};
        }
        if (message != 0 && told_walking(world)) {
            return;
        }
        return led_on(world);
    }
    void recovered(World& world) override { roll(world); }

  private:
    enum class At : std::uint8_t {
        kStart,
        kRolling,
        kWalking,
        kStriking,
        kFalling,
        kLed,
        kShipRolling,
        kShipWalking,
        kShipFalling
    };
    static constexpr std::uint16_t kStrikeOver = 12;
    static constexpr std::uint16_t kArmouredMask =
        hit::kToldOfSword | hit::kStopsArrow;  // what he is to hit tests while he rolls
    static constexpr std::uint16_t kWalkingMask = hit::kHurtBySword | hit::kHurtByArrow;

    // In the ship's scene: he rolls for 100 ticks and walks for 40, by turns, at the same speed.
    void ship_roll(World& world) {
        countdown = 100;
        speed_x = px(4.5);
        world.play(*this, anim::kRollerRolls);
        go(world, At::kShipRolling);
    }

    void roll(World& world) {
        countdown = 100;
        speed_x = px(4.5);
        world.play(*this, anim::kRollerRolls);
        hit_mask &= static_cast<std::uint16_t>(~kWalkingMask);
        hit_mask |= kArmouredMask;
        go(world, At::kRolling);
    }

    void walk(World& world) {
        face_viking_behind(world);
        countdown = 40;
        speed_x = px(3.5);
        hit_mask &= static_cast<std::uint16_t>(~kArmouredMask);
        hit_mask |= kWalkingMask;
        world.play(*this, anim::kRollerWalks);
        go(world, At::kWalking);
    }

    void go(World& world, At at) {
        world.animate_on_ground(*this);
        at_ = at;
    }

    void fall(World& world) {
        start_falling();
        world.animate(*this);
        at_ = At::kFalling;
    }

    // Rolling into a Viking: he is hurt, from the side the roller is on, and the roller turns. Not Erik while
    // he charges.
    void knock(World& world) {
        Entity* other = world.at(target);
        if (other == nullptr) {
            return;
        }
        if (target == 0 && other->sprite.packed_frame >= 12 && other->sprite.packed_frame <= 21) {
            return;
        }
        const bool ahead = world.offset_to(*this, target).ahead >= 0;
        const bool from_the_right = ahead ? facing_left() : !facing_left();
        harm(*other, message::kStruck, 2, from_the_right);
        const int reach = (ahead != facing_left()) ? 14 : -14;
        world.spawn(*this, kind::kSpark, x + reach, y - 4, 0, entity_flag::kLargePieces);
        turn_round(world);
    }

    bool told_rolling(World& world) {
        switch (message) {
            case message::kSwordBlow:
            case message::kHeadButt:
                if (blow_from_the_right() != facing_left()) {
                    turn_round(world);
                }
                message = 0;
                return false;
            case message::kArrowBlow:
            case message::kBlast:
                message = 0;
                forget_blow_side();
                if (message_value < 4) {
                    return false;
                }
                world.spawn(*this, kind::kRemains, x, box.bottom, 0, entity_flag::kLargePieces);
                gone(world);
                return true;
            default:
                return led(world);
        }
    }

    bool told_walking(World& world) {
        if (message == message::kSwordBlow || message == message::kArrowBlow || message == message::kHeadButt ||
            message == message::kBlast) {
            return told(world, true);
        }
        return led(world);
    }

    // What the cutscenes tell him: to stamp, to stand, to walk on, to turn and walk, to go.
    bool led(World& world) {
        switch (message) {
            case message::kLedAttack:
                lead(world, anim::kRollerStrikes, 0, true);
                led_stamps_ = true;
                return true;
            case message::kLedStand:
                lead(world, anim::kRollerWalks, 0, false);
                return true;
            case message::kLedTurn:
                turn_round(world);
                [[fallthrough]];
            case message::kLedWalk:
                lead(world, anim::kRollerWalks, px(3.5), true);
                return true;
            case message::kLedLeave:
                gone(world);
                return true;
            case message::kLedColours:
                if (told_colours(world)) {
                    return false;
                }
                world.remove(slot);
                return true;
            default:
                message = 0;
                return false;
        }
    }

    void lead(World& world, Animation animation, std::int16_t speed, bool moving) {
        speed_x = speed;
        follow(world, animation, moving ? Led::kMoving : Led::kStill);
        led_stamps_ = false;
        at_ = At::kLed;
    }

    void gone(World& world) { gone_for_good(world, *this); }

    bool on_the_ship_ = false;
    At at_ = At::kStart;
    bool still_touching_ = false;
    bool led_stamps_ = false;
};

// The charger: it stands, hops (again and again: up and forward, down, a rest), and when a Viking is ahead of it
// about level as it lands, it winds up and dashes at him until a wall or the ground's end stops it; then it
// turns round. It bites a Viking it touches (not again for 20 ticks), bounces off Olaf's shield in the air, and
// Olaf's shield held against its dash throws Olaf back. Spikes under it kill it. In the scenes outside the levels
// a director leads it. The spaceship has it too.
class Charger : public Enemy {
  public:
    Looks looks() const override {
        const Look shield_touched = touching_any(hit::kShield, &Saw::shield);
        switch (at_) {
            case At::kStanding:
            case At::kLanded:
                return {touching_a_viking()};
            case At::kRising:
                return {stopped_by(walls()), stopped_above_or_below(walls()), shield_touched, touching_a_viking()};
            case At::kFalling:
                return {stopped_by(walls()), lands_on(ground()), shield_touched, touching_a_viking()};
            case At::kDashing:  // what he runs into he strikes
                return {stopped_by(solid()), shield_touched, touching_any(hit::kViking, &Saw::other)};
            default:
                return {};
        }
    }

  protected:
    void turn(World& world) override {
        switch (at_) {
            case At::kStart:
                return at_start(world);
            case At::kStanding:
                return when_standing(world);
            case At::kRising:
                return when_rising(world);
            case At::kFalling:
                return when_falling(world);
            case At::kLanded:
                return when_landed(world);
            case At::kWindingUp:
                return when_winding_up(world);
            case At::kDashing:
                return when_dashing(world);
            case At::kStopping:
                return when_stopping(world);
            case At::kLedGoing:
                if (told_all(world)) {
                    return;
                }
                return go(world, At::kLedGoing);
            case At::kLedWindingUp:
                return when_led_winding_up(world);
            case At::kLedStanding:
                if (message != 0) {
                    (void)told_all(world);
                }
                return;
        }
    }
    void at_start(World& world) {
        looks_ = world.database == database::kSpaceship ? &anim::kSpaceshipChargerLooks : &anim::kWackyChargerLooks;
        take_kept_health();
        acts_as = tile::kEnemy;
        count_down_from(kBiteTicks);
        world.play(*this, looks_->appears);
        world.animate(*this);
        if (world.database == database::kScenes && world.scene == scene::kShip) {  // the curtain call: it walks on
            world.play(*this, anim::kChargerWalks);
            turn_round(world);
            speed_x = kPace;
            return go(world, At::kLedGoing);
        }
        if (is_falling()) {
            return rise(world);
        }
        return stand(world);
    }
    void when_standing(World& world) {
        if (saw.viking != kNobody) {
            bite(world, saw.viking);
        }
        if (dies_on_spikes(world) || (told_all(world))) {
            return;
        }
        if (animation_over()) {
            return hop(world);
        }
        return go(world, At::kStanding);
    }
    void when_rising(World& world) {
        if (saw.wall) {
            turn_round(world);
        }
        if (saw.ceiling) {
            speed_y = 0;
        }
        if (saw.shield != kNobody) {
            bounce(world);
        }
        if (saw.viking != kNobody) {
            bite(world, saw.viking);
        }
        if (dies_on_spikes(world) || (told_all(world))) {
            return;
        }
        stay_in_view(world);
        return rise(world);
    }
    void when_falling(World& world) {
        if (saw.wall) {
            turn_round(world);
        }
        if (saw.landed) {
            stop_falling();
            speed_x = 0;
            world.play(*this, looks_->lands);
            world.animate(*this);
            at_ = At::kLanded;
            return;
        }
        if (saw.shield != kNobody) {
            bounce(world);
        }
        if (saw.viking != kNobody) {
            bite(world, saw.viking);
        }
        if (dies_on_spikes(world) || (told_all(world))) {
            return;
        }
        stay_in_view(world);
        world.animate(*this);
    }
    void when_landed(World& world) {
        if (saw.viking != kNobody) {
            bite(world, saw.viking);
        }
        if (dies_on_spikes(world) || (told_all(world))) {
            return;
        }
        if (animation_over()) {
            return hop(world);
        }
        if (countdown == 0 && sprite.packed_frame == kLooksAt) {
            const World::Offset to = world.offset_to(*this, world.nearest_viking(*this));
            if (to.ahead >= 0 && to.below < 16 && to.below >= -16) {
                countdown = kBiteTicks;
                world.play(*this, looks_->winds_up);
                speed_x = 0;
                return go(world, At::kWindingUp);
            }
        }
        world.animate(*this);
    }
    void when_winding_up(World& world) {
        if (told_all(world)) {
            return;
        }
        if (animation_over()) {
            world.play(*this, looks_->dashes);
            speed_x = px(12);
            return go(world, At::kDashing);
        }
        return go(world, At::kWindingUp);
    }
    void when_dashing(World& world) {
        if (saw.wall) {
            return stop(world);
        }
        if (saw.shield != kNobody) {
            target = saw.shield;
            const Entity* other = world.at(saw.shield);
            if (other == nullptr || other->kind != kind::kShield) {
                return stop(world);
            }
            if (other->facing_left() != facing_left()) {  // held against it: Olaf is thrown back
                target = slot::kOlaf;
                world.tell(slot::kOlaf, message::kBlownBack);
                return stop(world);
            }
        }
        if (saw.other != kNobody) {
            target = saw.other;
            if (Entity* other = world.at(saw.other)) {
                harm(*other, message::kShot, 2, facing_left());
            }
            world.spawn(*this, kind::kSpark, x + (facing_left() ? -8 : 8), y, 0, entity_flag::kLargePieces);
            return stop(world);
        }
        if (told_all(world)) {
            return;
        }
        stay_in_view(world);
        if (!can_walk_on(world, *this)) {
            return stop(world);
        }
        return go(world, At::kDashing);
    }
    void when_stopping(World& world) {
        if (dies_on_spikes(world)) {
            return;
        }
        if (animation_over()) {
            turn_round(world);
            return stand(world);
        }
        world.animate(*this);
    }
    void when_led_winding_up(World& world) {
        if (told_all(world)) {
            return;
        }
        if (animation_over()) {
            world.play(*this, looks_->dashes);
            speed_x = px(12);
            return go(world, At::kLedGoing);
        }
        return go(world, At::kLedWindingUp);
    }
    void recovered(World& world) override { stand(world); }

  private:
    enum class At : std::uint8_t {
        kStart,
        kStanding,
        kRising,
        kFalling,
        kLanded,
        kWindingUp,
        kDashing,
        kStopping,
        kLedGoing,
        kLedWindingUp,
        kLedStanding
    };
    static constexpr std::uint16_t kLooksAt = 5;  // the frame of its rest it looks for a Viking at
    static constexpr std::uint16_t kBiteTicks = 20;
    static constexpr std::int16_t kPace = px(3);

    void go(World& world, At at) {
        world.animate_on_ground(*this);
        at_ = at;
    }
    void stand(World& world) {
        world.play(*this, looks_->stands);
        speed_x = 0;
        go(world, At::kStanding);
    }
    // Up and forward; with no ground under it, straight into the fall.
    void hop(World& world) {
        start_falling();
        speed_x = px(4);
        if (!on_ground(world)) {
            return come_down(world);
        }
        speed_y = px(-8);
        world.play(*this, looks_->jumps);
        rise(world);
    }
    void rise(World& world) {
        if (speed_y >= 0) {
            return come_down(world);
        }
        world.animate(*this);
        at_ = At::kRising;
    }
    void come_down(World& world) {
        world.play(*this, looks_->comes_down);
        world.animate(*this);
        at_ = At::kFalling;
    }
    void stop(World& world) {
        world.play(*this, looks_->stops);
        speed_x = 0;
        world.animate(*this);
        at_ = At::kStopping;
    }
    // In the air against Olaf's shield, from the side it faces: it turns back, and its next touch doesn't bite.
    void bounce(World& world) {
        target = slot::kOlaf;
        const Entity* olaf = world.at(slot::kOlaf);
        if (olaf == nullptr || olaf->facing_left() == facing_left() || world.offset_to(*this, slot::kOlaf).ahead < 6) {
            return;
        }
        turn_round(world);
        bounced_ = true;
    }
    void bite(World& world, int who) {
        target = who;
        if (!bounced_ && countdown == 0) {
            if (Entity* other = world.at(who)) {
                harm(*other, message::kShot, 2, facing_left());
            }
            world.spawn(*this, kind::kSpark, x, y, 0, entity_flag::kLargePieces);
            countdown = kBiteTicks;
        }
        bounced_ = false;
    }
    bool told_all(World& world) {
        if (world.database == database::kSpaceship) {  // no director leads the spaceship's
            if (told(world)) {
                stop_falling();
                return true;
            }
            return false;
        }
        switch (message) {
            case message::kLedAttack:
                message = 0;
                world.play(*this, looks_->winds_up);
                speed_x = 0;
                go(world, At::kLedWindingUp);
                return true;
            case message::kLedStand:
                message = 0;
                world.play(*this, anim::kChargerWalks);
                speed_x = 0;
                go(world, At::kLedStanding);
                return true;
            case message::kLedTurn:
                turn_round(world);
                [[fallthrough]];
            case message::kLedWalk:
                message = 0;
                speed_x = kPace;
                world.play(*this, anim::kChargerWalks);
                go(world, At::kLedGoing);
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
                if (told(world)) {
                    stop_falling();
                    return true;
                }
                return false;
        }
    }

    At at_ = At::kStart;
    const anim::ChargerLooks* looks_ = &anim::kWackyChargerLooks;
    bool bounced_ = false;
};

}  // namespace

std::span<const KindOf> chargers() {
    static constexpr KindOf kList[] = {
        {Home::kEgypt, egypt_kind::kHopper, make<Hopper>},
        {Home::kPrehistoric, prehistoric_kind::kRoller, make<Roller>},
        {Home::kWacky, wacky_kind::kCharger, make<Charger>},
    };
    return kList;
}

}  // namespace lvr
