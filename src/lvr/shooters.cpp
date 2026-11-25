// The enemies that shoot at a Viking ahead of them: the ones that walk, and the ones that fly.

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

// A gunner walks back and forth inside the view, turns at walls and ledges, and shoots at a Viking he faces who
// is level with him. One kind shoots from any distance and hurts a Viking he touches; the other only shoots from
// close by, and his touch does nothing. Olaf's shield, or a Viking in his way, stops him.
class Gunner : public Enemy {
  public:
  protected:
    Looks looks() const override {
        switch (s.at) {
            case At::kStart:
                return {};
            case At::kWalking:
                return {stopped_by(walls()), touching_a_viking(), touching_an_enemy()};
            case At::kFalling:
                return {lands_on(ground()), touching_a_viking()};
            default:
                return {touching_a_viking()};
        }
    }
    void turn(World& world) override {
        switch (s.at) {
            case At::kStart:
                appear(world, anim::kGunnerAppears);
                if (is_falling()) {
                    return fall(world);
                }
                return set_off(world);
            case At::kWalking:
                return when_walking(world);
            case At::kHeldUp:
                return when_held_up(world);
            case At::kAttacking:
                return when_attacking(world);
            case At::kFalling:
                return when_falling(world);
            case At::kLed:
                if (told_all(world)) {
                    return;
                }
                return led_on(world);
            case At::kLedShooting:
                return when_led_shooting(world);
        }
    }
    void when_walking(World& world) {
        if (saw.wall) {
            turn_round(world);
        }
        touched(world);
        turn_from_colleague(world);
        if (told_all(world)) {
            return;
        }
        stay_in_view(world);
        if (!can_walk_on(world, *this)) {
            if (!on_ground(world)) {
                return fall(world);
            }
            turn_round(world);
        }
        if (countdown == 0 && viking_ahead(world, kLevelWith, kind == kind::kCloseGunner ? kCloseBy : kAnyDistance)) {
            return attack(world);
        }
        return walk(world);
    }
    void when_held_up(World& world) {
        touched(world);
        if (told_all(world)) {
            return;
        }
        if (!on_ground(world)) {
            return fall(world);
        }
        if (countdown != 0) {
            return held_up(world);
        }
        return attack(world);
    }
    void when_attacking(World& world) {
        touched(world);
        if (!animation_over()) {
            return attacking(world);
        }
        world.spawn(*this, kind::kBullet, x + (facing_left() ? -10 : 10), y - 6);
        world.sound(*this, effect::kShot4D);
        return set_off(world);
    }
    void when_falling(World& world) {
        if (saw.landed) {
            stop_falling();
            return set_off(world);
        }
        touched(world);
        if (told_all(world)) {
            return;
        }
        return falling(world);
    }
    void when_led_shooting(World& world) {
        if (told_all(world)) {
            return;
        }
        if (animation_over()) {
            world.spawn(*this, kind::kBullet, x + (facing_left() ? -10 : 10), y - 6);
            world.sound(*this, effect::kShot4D);
            return lead(world, anim::kCloseGunnerWalks, Led::kStill);
        }
        world.animate_on_ground(*this);
    }
    void recovered(World& world) override { attack(world); }

  private:
    enum class At : std::uint8_t { kStart, kWalking, kHeldUp, kAttacking, kFalling, kLed, kLedShooting };
    static constexpr std::uint16_t kWalkTicks = 15;  // before he looks for a Viking again
    static constexpr std::uint16_t kHeldUpTicks = 10;
    static constexpr int kLevelWith = 16;
    static constexpr int kCloseBy = 24;

    void set_off(World& world) {
        world.play(*this, kind == kind::kCloseGunner ? anim::kCloseGunnerWalks : anim::kGunnerWalks);
        speed_x = 0;
        countdown = kWalkTicks;
        walk(world);
    }

    void walk(World& world) {
        if (in_his_way(world)) {
            world.play(*this, anim::kGunnerWaits);
            countdown = kHeldUpTicks;
            return held_up(world);
        }
        world.animate_on_ground(*this);
        s.at = At::kWalking;
    }

    void held_up(World& world) {
        if (!in_his_way(world)) {
            return set_off(world);
        }
        world.animate_on_ground(*this);
        s.at = At::kHeldUp;
    }

    void attack(World& world) {
        speed_x = 0;
        world.play(*this, anim::kGunnerShoots);
        attacking(world);
    }

    void attacking(World& world) {
        s.at = At::kAttacking;
        if (told_all(world)) {
            return;
        }
        world.animate(*this);
    }

    void fall(World& world) {
        speed_x = 0;
        world.play(*this, anim::kGunnerFalls);
        start_falling();
        falling(world);
    }

    void falling(World& world) {
        world.animate(*this);
        s.at = At::kFalling;
    }

    // What he is told besides blows, by a scene's director: to shoot where he stands, to stand, to walk on, to
    // turn and walk, to go. (His walk is his animation's: no speed is set.)
    void led_to_attack(World& world) override {
        speed_x = 0;
        lead(world, anim::kGunnerShoots, Led::kMoving);
        s.at = At::kLedShooting;
    }
    void led_to_stand(World& world) override {
        speed_x = 0;
        lead(world, anim::kCloseGunnerWalks, Led::kStill);
    }
    void led_to_walk(World& world) override { lead(world, anim::kCloseGunnerWalks, Led::kMoving); }

    void lead(World& world, Animation animation, Led how) {
        follow(world, animation, how);
        s.at = At::kLed;
    }

    // A Viking he touches is hurt (not by the kind that shoots from close by): a spark where they meet.
    void touched(World& world) {
        if (saw.viking == kNobody) {
            return;
        }
        target = saw.viking;
        if (kind == kind::kCloseGunner) {
            return;
        }
        Entity* other = world.at(saw.viking);
        if (other == nullptr) {
            return;
        }
        const bool ahead = world.offset_to(*this, saw.viking).ahead >= 0;
        harm(*other, message::kStruck, 2, ahead == facing_left());
        const int reach = (ahead != facing_left()) ? 14 : -14;
        world.spawn(*this, kind::kSpark, x + reach, y - 4, 0, entity_flag::kLargePieces);
    }

    struct Saved {
        At at = At::kStart;
    } s;
    LVR_SAVES(Enemy)
};

// The spaceship's trooper: he walks, and shoots at a Viking ahead of him about level (one kind only from near
// by). The other kind also cloaks: every so often, and after a blow, he vanishes, walks on unseen and untouchable
// for a while, and comes back shooting. A Viking or a block in his way holds him up; then he shoots too.
class Trooper : public Enemy {
  public:
    Looks looks() const override {
        switch (s.at) {
            case At::kWalking:
                return {stopped_by(walls()), touching_an_enemy()};
            case At::kUnseen:
                return {stopped_by(walls())};
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
            case At::kWalking:
                return when_walking(world);
            case At::kHeldUp:
                return when_held_up(world);
            case At::kShooting:
                return when_shooting(world);
            case At::kFalling:
                return when_falling(world);
            case At::kVanishing:
                return when_vanishing(world);
            case At::kUnseen:
                return when_unseen(world);
            case At::kReappearing:
                world.show_pieces(*this);
                world.animate(*this);
                s.at = At::kAppearing;
                return;
            case At::kAppearing:
                return when_appearing(world);
        }
    }
    void at_start(World& world) {
        take_kept_health();
        s.near_only = kind == kind::kNearTrooper;
        s.pace = s.near_only ? px(3) : px(4);
        world.play(*this, anim::kTrooperAppears);
        world.animate(*this);
        acts_as = tile::kEnemy;
        count_down();
        clear_flag(entity_flag::kFalls);
        if (world.scene == scene::kLastLevel) {  // in the last level he starts unseen
            world.hide_pieces(*this);
            hit_mask = 0;
            return reappear(world);
        }
        return set_off(world);
    }
    void when_walking(World& world) {
        if (saw.wall) {
            turn_round(world);
        }
        turn_from_colleague(world);
        if (told(world)) {
            return;
        }
        stay_in_view(world);
        if (!can_walk_on(world, *this)) {
            if (!on_ground(world)) {
                return fall(world);
            }
            turn_round(world);
        }
        if (cloak_due(world)) {
            return;
        }
        if (countdown == 0 && viking_ahead(world, 16, s.near_only ? 128 : kAnyDistance)) {
            return shoot(world);
        }
        return walk(world);
    }
    void when_held_up(World& world) {
        if (told(world)) {
            return;
        }
        if (!on_ground(world)) {
            return fall(world);
        }
        if (cloak_due(world)) {
            return;
        }
        if (countdown == 0) {
            return shoot(world);
        }
        return held_up(world);
    }
    void when_shooting(World& world) {
        if (animation_over()) {
            return set_off(world);
        }
        if (cloak_due(world)) {
            return;
        }
        if (sprite.packed_frame == kFiresAt && !s.fired) {
            s.fired = true;
            world.spawn(*this, kind::kBullet, x + (facing_left() ? -6 : 6), y + 4);
            world.sound(*this, effect::kShot30);
        }
        return shooting(world);
    }
    void when_falling(World& world) {
        if (landed_from_fall()) {
            return set_off(world);
        }
        if (told(world)) {
            return;
        }
        world.animate(*this);
    }
    void when_vanishing(World& world) {
        if (!animation_over()) {
            world.animate(*this);
            return;
        }
        world.hide_pieces(*this);
        speed_x = px(4);
        world.animate_on_ground(*this);
        s.at = At::kUnseen;
    }
    void when_unseen(World& world) {
        if (saw.wall) {
            turn_round(world);
        }
        stay_in_view(world);
        if (!can_walk_on(world, *this)) {
            turn_round(world);
        }
        if (countdown != 0) {
            world.animate_on_ground(*this);
            return;
        }
        return reappear(world);
    }
    void when_appearing(World& world) {
        if (!animation_over()) {
            world.animate(*this);
            return;
        }
        face_viking_behind(world);
        hit_mask = kHitMask;
        s.cloak_in = kCloakTicks;
        return shoot(world);
    }
    // Over a blow: he cloaks at once, if he is the kind that does.
    void recovered(World& world) override {
        if (!cloak(world)) {
            set_off(world);
        }
    }

  private:
    enum class At : std::uint8_t {
        kStart,
        kWalking,
        kHeldUp,
        kShooting,
        kFalling,
        kVanishing,
        kUnseen,
        kReappearing,
        kAppearing
    };
    static constexpr std::uint16_t kFiresAt = 8;
    static constexpr std::uint16_t kHitMask =
        bits(hit::kHurtByFire, hit::kBombable, hit::kHurtBySword, hit::kEnemy, hit::kHurtByArrow);
    static constexpr std::uint16_t kCloakTicks = 20;

    static const TileTypes& blocks() {
        static const TileTypes types = tile_types({tile::kShield});
        return types;
    }
    bool blocked(const World& world) const {
        return find_beside(world, *this, Side::kAhead, blocks()).has_value() ||
               find_viking_beside(world, *this, Side::kAhead, vikings()).has_value();
    }

    void set_off(World& world) {
        world.play(*this, anim::kTrooperWalks);
        speed_x = s.pace;
        countdown = 15;
        if (s.cloak_in == 0) {
            s.cloak_in = kCloakTicks;
        }
        walk(world);
    }
    void walk(World& world) {
        if (blocked(world)) {
            world.play(*this, anim::kTrooperWaits);
            speed_x = 0;
            countdown = 10;
            return held_up(world);
        }
        world.animate_on_ground(*this);
        s.at = At::kWalking;
    }
    void held_up(World& world) {
        if (!blocked(world)) {
            return set_off(world);
        }
        world.animate(*this);
        s.at = At::kHeldUp;
    }
    void shoot(World& world) {
        world.play(*this, anim::kTrooperShoots);
        speed_x = 0;
        s.fired = false;
        shooting(world);
    }
    void shooting(World& world) {
        s.at = At::kShooting;
        if (told(world)) {
            return;
        }
        world.animate(*this);
    }
    void fall(World& world) {
        start_falling();
        world.animate(*this);
        s.at = At::kFalling;
    }
    // The count to his next cloaking, a tick on; true if he cloaked.
    bool cloak_due(World& world) {
        if (s.cloak_in != 0) {
            --s.cloak_in;
        }
        return s.cloak_in == 0 && cloak(world);
    }
    // He vanishes: only the kind that shoots from afar, and not in the last level.
    bool cloak(World& world) {
        if (s.near_only || world.scene == scene::kLastLevel) {
            return false;
        }
        world.play(*this, anim::kTrooperVanishes);
        speed_x = 0;
        hit_mask = 0;
        countdown = 25;
        world.animate(*this);
        s.at = At::kVanishing;
        return true;
    }
    void reappear(World& world) {
        world.play(*this, anim::kTrooperComesBack);
        speed_x = 0;
        world.animate(*this);
        s.at = At::kReappearing;
    }

    struct Saved {
        At at = At::kStart;
        bool near_only = false;
        std::int16_t pace = 0;
        std::uint16_t cloak_in = 0;
        bool fired = false;
    } s;
    LVR_SAVES(Enemy)
};

// A robot that walks back and forth and shoots at a Viking ahead who is level with it. Olaf's shield, or a
// Viking in its way, holds it up, and then it shoots too. A sword does nothing to it.
class ShootingRobot : public Enemy {
  public:
    Looks looks() const override {
        switch (s.at) {
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
                appear(world, anim::kShooterAppears);
                if (is_falling()) {
                    return fall(world);
                }
                return set_off(world);
            case At::kWalking:
                return when_walking(world);
            case At::kHeldUp:
                return when_held_up(world);
            case At::kShooting:
            case At::kLedShooting:
                return when_shooting(world);
            case At::kFalling:
                return when_falling(world);
            case At::kLed:
                if (told_all(world)) {
                    return;
                }
                return led_on(world);
        }
    }
    void when_walking(World& world) {
        if (saw.wall) {
            turn_round(world);
        }
        turn_from_colleague(world);
        if (told_all(world)) {
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
            return shoot(world);
        }
        return walk(world);
    }
    void when_held_up(World& world) {
        if (told_all(world)) {
            return;
        }
        if (!on_ground(world)) {
            return fall(world);
        }
        if (countdown != 0) {
            return held_up(world);
        }
        return shoot(world);
    }
    void when_shooting(World& world) {
        if (animation_over()) {
            if (s.at == At::kLedShooting) {
                speed_x = 0;
                follow(world, anim::kShooterWalks, Led::kAnimated);
                s.at = At::kLed;
                return;
            }
            countdown = 30;
            return set_off(world);
        }
        if (sprite.packed_frame == kShotFrame && !s.shot) {
            s.shot = true;
            world.spawn(*this, factory_kind::kShot, x + (facing_left() ? -12 : 12), y - 8);
            world.sound(*this, effect::kShot33);
        }
        if (told_all(world)) {
            return;
        }
        if (s.at == At::kLedShooting) {
            set_flag(entity_flag::kPutOnGround);
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
    void recovered(World& world) override { shoot(world); }

  private:
    enum class At : std::uint8_t { kStart, kWalking, kHeldUp, kShooting, kFalling, kLed, kLedShooting };
    static constexpr std::uint16_t kShotFrame = 6;

    void set_off(World& world) {
        world.play(*this, anim::kShooterWalks);
        speed_x = px(3);
        walk(world);
    }
    void walk(World& world) {
        if (in_his_way(world)) {
            speed_x = 0;
            countdown = 4;
            return held_up(world);
        }
        world.animate_on_ground(*this);
        s.at = At::kWalking;
    }
    void held_up(World& world) {
        if (!in_his_way(world)) {
            return set_off(world);
        }
        world.animate_on_ground(*this);
        s.at = At::kHeldUp;
    }
    void shoot(World& world) {
        world.play(*this, anim::kShooterShoots);
        speed_x = 0;
        s.shot = false;
        s.at = At::kShooting;
        if (told_all(world)) {
            return;
        }
        world.animate(*this);
    }
    void fall(World& world) {
        start_falling();
        world.animate(*this);
        s.at = At::kFalling;
    }

    // A blow, in his own way; whatever else he is told he forgets.
    bool struck(World& world) override {
        switch (message) {
            case message::kArrowBlow:
            case message::kHeadButt:
            case message::kBlast:
                return told(world);
            default:
                message = 0;
                return false;
        }
    }
    void led_to_attack(World& world) override {
        message = 0;
        world.play(*this, anim::kShooterShoots);
        speed_x = 0;
        s.shot = false;
        world.animate_on_ground(*this);
        s.at = At::kLedShooting;
    }
    void led_to_stand(World& world) override {
        speed_x = 0;
        follow(world, anim::kShooterWalks, Led::kAnimated);
        s.at = At::kLed;
    }
    void led_to_walk(World& world) override {
        speed_x = px(3);
        follow(world, anim::kShooterWalks, Led::kMoving);
        s.at = At::kLed;
    }

    struct Saved {
        At at = At::kStart;
        bool shot = false;
    } s;
    LVR_SAVES(Enemy)
};

// An enemy that walks slowly back and forth and spits at a Viking ahead who is level with him. Olaf's shield, or
// a Viking in his way, holds him up, and then he spits too.
class Spitter : public Enemy {
  public:
    static const TileTypes& stops() {
        static const TileTypes types = tile_types({tile::kGround, tile::kGround02, tile::kGround20}) | slopes();
        return types;
    }
    Looks looks() const override {
        switch (s.at) {
            case At::kWalking:
                return {stopped_by(stops()), touching_an_enemy()};
            case At::kFalling:
            case At::kHurt:
                return {stopped_by(stops()), lands_on(ground())};
            default:
                return {};
        }
    }

  protected:
    void turn(World& world) override {
        switch (s.at) {
            case At::kStart:
                appear(world, anim::kSpitterAppears);
                if (is_falling()) {
                    return fall(world);
                }
                return set_off(world);
            case At::kWalking:
                return when_walking(world);
            case At::kHeldUp:
                return when_held_up(world);
            case At::kSpitting:
            case At::kLedSpitting:
                return when_spitting(world);
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
            turn_round(world);
        }
        turn_from_colleague(world);
        if (told_all(world)) {
            return;
        }
        stay_in_view(world);
        if (!find_ahead_below(world, *this, floor())) {
            if (!find_beside(world, *this, Side::kBelow, floor())) {
                return fall(world);
            }
            turn_round(world);
        }
        if (countdown == 0 && viking_ahead(world, 16)) {
            return spit(world);
        }
        return walk(world);
    }
    void when_held_up(World& world) {
        if (told_all(world)) {
            return;
        }
        if (!find_beside(world, *this, Side::kBelow, floor())) {
            return fall(world);
        }
        if (countdown != 0) {
            return held_up(world);
        }
        return spit(world);
    }
    void when_spitting(World& world) {
        if (animation_over()) {
            if (s.at == At::kLedSpitting) {
                return lead(world, 0, false);
            }
            countdown = kRestTicks;
            return set_off(world);
        }
        if (sprite.packed_frame == kSpitFrame && !s.spat) {
            s.spat = true;
            world.spawn(*this, prehistoric_kind::kSpit, x + (facing_left() ? -12 : 12), y + 4);
            sound(world, effect::kSpit);
        }
        return spitting(world);
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
        if (saw.landed) {
            stop_falling();
        }
        if (animation_over()) {
            if (s.turns_after) {
                turn_round(world);
                s.turns_after = false;
            }
            return set_off(world);
        }
        return being_hurt(world);
    }
    void recovered(World& /*world*/) override {}

  private:
    enum class At : std::uint8_t { kStart, kWalking, kHeldUp, kSpitting, kFalling, kHurt, kLed, kLedSpitting };
    static constexpr std::uint16_t kSpitFrame = 7;
    static constexpr std::uint16_t kRestTicks = 15;
    static constexpr std::uint16_t kHeldUpTicks = 4;

    static const TileTypes& floor() {
        static const TileTypes types = tile_types({tile::kGround, tile::kGround02, tile::kLadderTop, tile::kGround20});
        return types;
    }
    void sound(World& world, std::uint8_t effect) const { world.sound(*this, effect); }

    void set_off(World& world) {
        speed_x = px(1);
        world.play(*this, anim::kSpitterWalks);
        walk(world);
    }

    void walk(World& world) {
        if (in_his_way(world)) {
            speed_x = 0;
            countdown = kHeldUpTicks;
            return held_up(world);
        }
        world.animate(*this);
        s.at = At::kWalking;
    }

    void held_up(World& world) {
        if (!in_his_way(world)) {
            return set_off(world);
        }
        world.animate(*this);
        s.at = At::kHeldUp;
    }

    void spit(World& world) {
        world.play(*this, anim::kSpitterSpits);
        speed_x = 0;
        s.spat = false;
        s.at = At::kSpitting;
        spitting(world);
    }

    void spitting(World& world) {
        if (told_all(world)) {
            return;
        }
        world.animate(*this);
    }

    void fall(World& world) {
        start_falling();
        world.animate(*this);
        s.at = At::kFalling;
    }

    void being_hurt(World& world) {
        const Entity* olaf = world.at(slot::kOlaf);
        const bool olaf_faces_left = olaf != nullptr && olaf->facing_left();
        if (find_beside(world, *this, Side::kAhead, shield()) && facing_left() != olaf_faces_left && speed_x == px(3)) {
            speed_x = 0;
        }
        if (find_beside(world, *this, Side::kBehind, shield()) && facing_left() == olaf_faces_left &&
            speed_x == px(-4)) {
            speed_x = 0;
        }
        world.animate(*this);
        s.at = At::kHurt;
    }

    // What he is told. True if his turn ended (or he is gone).
    // A blow, in his own way; whatever else he is told he forgets.
    bool struck(World& world) override {
        switch (message) {
            case message::kSwordBlow:
            case message::kArrowBlow:
            case message::kHeadButt:
            case message::kBlast: {
                sound(world, effect::kEnemyHurt);
                const bool from_the_right = blow_from_the_right();
                if (!survives_blow(*this)) {
                    leave_remains(world);
                    return true;
                }
                const bool from_behind = from_the_right == facing_left();
                if (from_behind) {
                    s.turns_after = true;
                }
                speed_x = from_behind ? px(3) : px(-4);
                world.play(*this, anim::kSpitterIsHurt);
                being_hurt(world);
                return true;
            }
            default:
                message = 0;
                return false;
        }
    }
    void led_to_attack(World& world) override {
        message = 0;
        world.play(*this, anim::kSpitterSpits);
        speed_x = 0;
        s.spat = false;
        s.at = At::kLedSpitting;
        spitting(world);
    }
    void led_to_stand(World& world) override { lead(world, 0, false); }
    void led_to_walk(World& world) override { lead(world, px(2), true); }

    void lead(World& world, std::int16_t speed, bool moving) {
        speed_x = speed;
        follow(world, anim::kSpitterWalks, moving ? Led::kMoving : Led::kStill);
        s.at = At::kLed;
    }

    struct Saved {
        At at = At::kStart;
        bool spat = false;
        bool turns_after = false;
    } s;
    LVR_SAVES(Enemy)
};

// An enemy that walks back and forth and throws something at a Viking ahead who is level with him. Olaf's
// shield, or a Viking in his way, holds him up, and then he throws too. Spikes under him kill him.
// (The same pattern as the prehistoric world's spitter and the factory's shooting robot.)
class Thrower : public Enemy {
  public:
    Looks looks() const override {
        switch (s.at) {
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
                appear(world, anim::kThrowerAppears);
                if (is_falling()) {
                    return fall(world);
                }
                return set_off(world);
            case At::kWalking:
                return when_walking(world);
            case At::kHeldUp:
                return when_held_up(world);
            case At::kThrowing:
            case At::kLedThrowing:
                return when_throwing(world);
            case At::kFalling:
                return when_falling(world);
            case At::kLed:
                if (told_all(world)) {
                    return;
                }
                return led_on(world);
        }
    }
    void when_walking(World& world) {
        if (saw.wall) {
            turn_round(world);
        }
        turn_from_colleague(world);
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
            return throw_it(world);
        }
        return walk(world);
    }
    void when_held_up(World& world) {
        if (dies_on_spikes(world) || (told_all(world))) {
            return;
        }
        if (!on_ground(world)) {
            return fall(world);
        }
        if (countdown != 0) {
            return held_up(world);
        }
        return throw_it(world);
    }
    void when_throwing(World& world) {
        if (s.at == At::kLedThrowing) {
            if (told_all(world)) {
                return;
            }
        } else if (dies_on_spikes(world)) {
            return;
        }
        if (animation_over()) {
            if (s.at == At::kLedThrowing) {
                return lead(world, 0, false);
            }
            return set_off(world);
        }
        if (sprite.packed_frame == kThrowFrame && !s.thrown) {
            s.thrown = true;
            world.spawn(*this, wacky_kind::kThrown, x + (facing_left() ? -10 : 10), y - 6);
        }
        if (s.at == At::kLedThrowing) {
            set_flag(entity_flag::kPutOnGround);
        } else if (told_all(world)) {
            return;
        }
        world.animate(*this);
    }
    void when_falling(World& world) {
        if (saw.wall) {
            turn_round(world);
        }
        if (saw.landed) {
            stop_falling();
            return set_off(world);
        }
        if (dies_on_spikes(world) || (told_all(world))) {
            return;
        }
        world.animate(*this);
    }
    void recovered(World& world) override { throw_it(world); }

  private:
    enum class At : std::uint8_t { kStart, kWalking, kHeldUp, kThrowing, kFalling, kLed, kLedThrowing };
    static constexpr std::uint16_t kThrowFrame = 10;

    void set_off(World& world) {
        world.play(*this, anim::kThrowerWalks);
        speed_x = px(4);
        countdown = 15;
        walk(world);
    }
    void walk(World& world) {
        if (in_his_way(world)) {
            speed_x = 0;
            countdown = 4;
            return held_up(world);
        }
        world.animate_on_ground(*this);
        s.at = At::kWalking;
    }
    void held_up(World& world) {
        if (!in_his_way(world)) {
            return set_off(world);
        }
        world.animate_on_ground(*this);
        s.at = At::kHeldUp;
    }
    void throw_it(World& world) {
        s.thrown = false;
        speed_x = 0;
        world.play(*this, anim::kThrowerThrows);
        s.at = At::kThrowing;
        if (told_all(world)) {
            return;
        }
        world.animate(*this);
    }
    void fall(World& world) {
        world.play(*this, anim::kThrowerFalls);
        start_falling();
        world.animate(*this);
        s.at = At::kFalling;
    }

    void led_to_attack(World& world) override {
        message = 0;
        world.play(*this, anim::kThrowerThrows);
        speed_x = 0;
        s.thrown = false;
        world.animate_on_ground(*this);
        s.at = At::kLedThrowing;
    }
    void led_to_stand(World& world) override { lead(world, 0, false); }
    void led_to_walk(World& world) override { lead(world, px(3), true); }

    void lead(World& world, std::int16_t speed, bool moving) {
        speed_x = speed;
        follow(world, anim::kThrowerWalks, moving ? Led::kMoving : Led::kStill);
        s.at = At::kLed;
    }

    struct Saved {
        At at = At::kStart;
        bool thrown = false;
    } s;
    LVR_SAVES(Enemy)
};

// A robot that flies back and forth, bobbing up and down, and drops a bomb every eight ticks.
class FlyingRobot : public Enemy {
  public:
    // Flying, it bounces off walls, ceilings and floors.
    Looks looks() const override {
        return s.at == At::kFlying ? Looks{stopped_by(walls()), stopped_above_or_below(walls()), lands_on(ground())}
                                   : Looks{};
    }

  protected:
    void turn(World& world) override {
        if (s.at == At::kStart) {
            appear(world, anim::kFlyerAppears);
            countdown = kBobTicks;
            return set_off(world);
        }
        if (saw.wall) {
            turn_round(world);
        }
        for (int i = 0; i < (saw.ceiling ? 1 : 0) + (saw.landed ? 1 : 0); ++i) {
            bob(world);
        }
        if (message != 0) {
            if (message == message::kSwordBlow || message == message::kArrowBlow || message == message::kHeadButt ||
                message == message::kBlast) {
                if (told(world)) {
                    return;
                }
            }
            message = 0;
        }
        if ((countdown & 7U) == 7U) {
            world.spawn(*this, factory_kind::kDroppedShot, x + (facing_left() ? 3 : -3), y + 18);
        }
        stay_in_view(world);
        if (world.at_edge_of_view(*this, false, speed_y >= 0, 0, 16)) {
            bob(world);
        }
        if (countdown == 0) {
            bob(world);
        }
        fly(world);
    }
    void recovered(World& world) override { set_off(world); }

  private:
    enum class At : std::uint8_t { kStart, kFlying };
    static constexpr std::uint16_t kBobTicks = 15;

    void set_off(World& world) {
        world.play(*this, anim::kFlyerFlies);
        speed_x = px(5);
        speed_y = px(-2);
        fly(world);
    }
    void fly(World& world) {
        if (find_beside(world, *this, Side::kAhead, shield())) {
            turn_round(world);
        }
        world.animate(*this);
        s.at = At::kFlying;
    }
    void bob(World& /*world*/) {
        countdown = kBobTicks;
        speed_y = speed_y == px(2) ? px(-2) : px(2);
    }

    struct Saved {
        At at = At::kStart;
    } s;
    LVR_SAVES(Enemy)
};

// A drone that flies back and forth, down to the floor (or the view's bottom) and up again for 20 ticks at a
// time, and shoots at a Viking ahead of it within two cells of its height, every 16 ticks. A block in its way or
// a wall turns it round.
class Drone : public Enemy {
  public:
    // Flying, it bounces off walls, ceilings and floors.
    Looks looks() const override {
        return s.at == At::kFlying ? Looks{stopped_by(walls()), stopped_above_or_below(walls()), lands_on(ground())}
                                   : Looks{};
    }

  protected:
    void turn(World& world) override {
        if (s.at == At::kStart) {
            appear(world, anim::kDroneAppears);
            countdown = kUpTicks;
            set_off(world);
            return fly(world);
        }
        if (saw.wall) {
            turn_round(world);
        }
        for (int i = 0; i < (saw.ceiling ? 1 : 0) + (saw.landed ? 1 : 0); ++i) {
            bob();
        }
        if (told(world)) {
            return;
        }
        if ((countdown & 0x0fU) == 0x0aU) {
            const World::Offset to = world.offset_to(*this, world.nearest_viking(*this));
            if (to.ahead >= 0 && to.below < 32 && to.below >= -32) {
                shoot(world);
            }
        }
        carry_on(world);
    }
    // Over a blow it flies on, and shoots at once.
    void recovered(World& world) override {
        set_off(world);
        shoot(world);
        carry_on(world);
    }

  private:
    enum class At : std::uint8_t { kStart, kFlying };
    static constexpr std::uint16_t kUpTicks = 20;
    static constexpr std::int16_t kBob = px(2);

    static const TileTypes& blocks() {
        static const TileTypes types = tile_types({tile::kShield});
        return types;
    }
    void set_off(World& world) {
        world.play(*this, anim::kDroneFlies);
        speed_x = px(4);
        speed_y = kBob;
    }
    void shoot(World& world) {
        world.spawn(*this, kind::kDroneShot, x, y);
        world.sound(*this, effect::kShot);
    }
    void bob() {
        countdown = kUpTicks;
        speed_y = static_cast<std::int16_t>(speed_y == kBob ? -kBob : kBob);
    }
    void carry_on(World& world) {
        stay_in_view(world);
        // At the view's bottom (going down) or 16 pixels over its top (going up) it turns too.
        if (world.at_edge_of_view(*this, false, speed_y >= 0, 0, 16)) {
            bob();
        }
        if (speed_y != kBob && countdown == 0) {  // it has gone up long enough
            bob();
        }
        fly(world);
    }
    void fly(World& world) {
        if (find_beside(world, *this, Side::kAhead, blocks())) {
            turn_round(world);
        }
        world.animate(*this);
        s.at = At::kFlying;
    }

    struct Saved {
        At at = At::kStart;
    } s;
    LVR_SAVES(Enemy)
};

}  // namespace

std::span<const KindOf> shooters() {
    static constexpr KindOf kList[] = {
        {Home::kAnywhere, kind::kCloseGunner, make<Gunner>},
        {Home::kAnywhere, kind::kGunner, make<Gunner>},
        {Home::kAnywhere, kind::kNearTrooper, make<Trooper>},
        {Home::kAnywhere, kind::kTrooper, make<Trooper>},
        {Home::kFactory, factory_kind::kShooter, make<ShootingRobot>},
        {Home::kPrehistoric, prehistoric_kind::kSpitter, make<Spitter>},
        {Home::kWacky, wacky_kind::kThrower, make<Thrower>},
        {Home::kFactory, factory_kind::kFlyer, make<FlyingRobot>},
        {Home::kAnywhere, kind::kDrone, make<Drone>},
    };
    return kList;
}

}  // namespace lvr
