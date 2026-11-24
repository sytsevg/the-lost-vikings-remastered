// What is shot: the guns on the walls, every kind of shot that flies, and what is left when something bursts.

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

// A gun on a wall: a shot every thirty ticks, the way it faces. Enough damage destroys it for good.
class Gun : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            world.play(*this, anim::kGunWaits);
            count_down();
            world.animate(*this);
            return;
        }
        if (message != 0) {
            if (message == message::kBlast) {
                if (message_value >= health) {
                    gone_for_good(world, *this);
                    return;
                }
                health -= message_value;
                message_value = 0;
            }
            message = 0;
        }
        if (countdown == 0) {
            world.play(*this, anim::kGunFires);
            countdown = kTicksBetweenShots;
            world.sound(*this, effect::kShot30);
            world.spawn(*this, kind::kBullet, x + (facing_left() ? -4 : 4), y - 8);
        }
        world.animate(*this);
    }

  private:
    static constexpr std::uint16_t kTicksBetweenShots = 30;
};

// A gun on a wall that shoots every forty ticks. Only bombs harm it.
class Turret : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            world.play(*this, anim::kTurretWaits);
            count_down_from(kEvery);
        } else {
            if (message == message::kBlast) {
                if (message_value >= health) {
                    return gone_for_good(world, *this);
                }
                health -= message_value;
                message_value = 0;
            }
            message = 0;
            if (countdown == 0) {
                world.play(*this, anim::kTurretShoots);
                world.spawn(*this, factory_kind::kShot, x + (facing_left() ? -8 : 8), y - 8);
                world.sound(*this, effect::kShot33);
                countdown = kEvery;
            }
        }
        world.animate(*this);
    }

  private:
    static constexpr std::uint16_t kEvery = 40;
};

// Where fire comes out of a wall, every 40 ticks while its switches are on. One kind is gone once its cell of
// the map has been cleared away. Another sends fire in arcs, to the left, straight up and to the right by turns;
// once its switches have been off it sends plain fireballs like the others.
class FireShooter : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            count_down();
            s.arcs = kind == prehistoric_kind::kArcingFireShooter;
            return;
        }
        if (kind == prehistoric_kind::kFireShooterInAWall && world.map.tile_type_at(x, y) == tile::kFree) {
            return gone_for_good(world, *this);
        }
        if (!switches_on(world, argument)) {
            s.arcs = false;
            return;
        }
        if (countdown != 0) {
            return;
        }
        if (s.arcs) {
            world.sound(*this, effect::kWhoosh);
            world.spawn(*this, prehistoric_kind::kArcingFire, x, y, s.next_arc, 0);
            s.next_arc = static_cast<std::uint16_t>((s.next_arc + 1U) % 3U);
        } else {
            world.spawn(*this, prehistoric_kind::kFireball, x + (facing_left() ? -4 : 4), y);
            world.sound(*this, effect::kWhoosh);
        }
        countdown = 40;
    }

  private:
    struct Saved {
        bool arcs = false;
        std::uint16_t next_arc = 0;
    } s;
    LVR_SAVES(Entity)
};

// A burner: a flame every forty ticks while its switches are on.
class Burner : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            count_down();
            return;
        }
        if (!switches_on(world, argument) || countdown != 0) {
            return;
        }
        world.spawn(*this, wacky_kind::kFlame, x + (facing_left() ? -4 : 4), y);
        world.sound(*this, effect::kBurnerFires);
        countdown = 40;
    }
};

// What the spitter spits: straight ahead until it hits a wall, Olaf's shield from the front, or a Viking, whom
// it hurts.
class Spit : public StraightShot {
    void fired(World& world) override {
        world.play(*this, anim::kSpitFlies);
        speed_x = px(8);
    }
    void hit_viking(World& world) override {
        hit_viking_touched(world, *this, x + (facing_left() ? -8 : 8));
        burst(world);
    }
    void bursting(World& world) override {
        world.sound(*this, effect::kBurst);
        world.play(*this, anim::kSpitBursts);
        speed_x = px(-1);
    }
};

// What the thrower throws: straight on until it hits a wall, Olaf's shield held towards it, or a Viking, whom
// it hurts.
class Thrown : public StraightShot {
    void fired(World& world) override {
        world.play(*this, anim::kThrownFlies);
        speed_x = px(10);
    }
    void hit_viking(World& world) override {
        hit_viking_touched(world, *this, x);
        world.sound(*this, effect::kSplat);
        world.remove(slot);
    }
    void bursting(World& world) override {
        world.sound(*this, effect::kSplat);
        world.play(*this, anim::kThrownBursts);
        speed_x = 0;
        speed_y = px(2);
    }
};

// Something shot straight ahead: the flame a burner sends out (it gives out after a while), and another shot.
// It bursts on a wall and on Olaf's shield held towards it, and hurts a Viking it hits.
class Flame : public StraightShot {
    void fired(World& world) override {
        s.shot = kind == wacky_kind::kFlameShot;
        world.play(*this, s.shot ? anim::kFlameShotFlies : anim::kFlameFlies);
        if (s.shot) {
            speed_x = px(12);
        } else {
            speed_x = px(8);
            stay_active();
            count_down_from(35);
        }
    }
    void hit_viking(World& world) override {
        hit_viking_touched(world, *this, s.shot ? x + (facing_left() ? -6 : 6) : x);
        world.remove(slot);
    }
    void still_flying(World& world) override {
        if (!s.shot && countdown == 0) {
            end_with(world, anim::kFlameGivesOut);
        }
    }
    void bursting(World& world) override {
        world.sound(*this, s.shot ? effect::kArrowBreaks : effect::kBurst);
        world.play(*this, s.shot ? anim::kFlameShotBursts : anim::kFlameBursts);
        speed_x = 0;
    }

    struct Saved {
        bool shot = false;
    } s;
    LVR_SAVES(StraightShot)
};

// What the robots shoot, and what the flying one drops: straight on until it hits a wall or the floor, Olaf's
// shield, or a Viking, whom it hurts.
class RobotShot : public Entity {
  public:
    void update(World& world) override {
        switch (s.state) {
            case State::kStart:
                s.dropped = kind == factory_kind::kDroppedShot;
                world.play(*this, s.dropped ? anim::kShotDrops : anim::kShotFlies);
                if (s.dropped) {
                    speed_y = px(3);
                    start_falling();
                } else {
                    speed_x = px(10);
                }
                s.state = State::kFlying;
                break;
            case State::kFlying:
                if ((saw.landed || saw.wall)) {
                    return burst(world);
                }
                if (saw.shield != kNobody) {
                    target = saw.shield;
                    const Entity* other = world.at(saw.shield);
                    if (s.dropped || other == nullptr || other->kind != kind::kShield ||
                        other->facing_left() != facing_left()) {
                        return burst(world);
                    }
                }
                if (saw.viking != kNobody) {
                    hit_viking_touched(world, *this, x);
                    return burst(world);
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
    // Flying: a wall ends a shot that was fired, a floor one that was dropped; Olaf's shield, held the way it
    // comes from, stops either.
    Looks looks_for() const override {
        if (s.state != State::kFlying) {
            return {};
        }
        const Look viking = touching_any(hit::kViking, &Saw::viking);
        return s.dropped
                   ? Looks{lands_on(floor_or_raised_shield()), touching_any(hit::kShieldOverhead, &Saw::shield), viking}
                   : Looks{stopped_by(solid()), touching_any(hit::kShield, &Saw::shield), viking};
    }

  private:
    enum class State : std::uint8_t { kStart, kFlying, kBurst };

    void burst(World& world) {
        if (s.dropped) {
            world.play(*this, anim::kShotLands);
            stop_falling();
        } else {
            world.sound(*this, effect::kBurst);
            world.play(*this, anim::kShotBursts);
            speed_x = 0;
        }
        s.state = State::kBurst;
        world.animate(*this);
    }

    struct Saved {
        State state = State::kStart;
        bool dropped = false;
    } s;
    LVR_SAVES(Entity)
};

// What the drone shoots: straight ahead until it hits a wall, Olaf's shield from the front, or a Viking, whom it
// hurts.
class DroneShot : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            world.play(*this, anim::kDroneShotFlies);
            speed_x = px(10);
        } else {
            if (saw.wall) {
                world.remove(slot);
                return;
            }
            if (stopped_by_what_it_touches(world, *this)) {
                world.remove(slot);
                return;
            }
            if (saw.viking != kNobody) {
                hit_viking_touched(world, *this, x);
                world.remove(slot);
                return;
            }
        }
        world.animate(*this);
    }
    static const TileTypes& solid() {
        static const TileTypes types = tile_types({tile::kGround, tile::kGround02, tile::kGround20});
        return types;
    }
    Looks looks_for() const override {
        return started() ? Looks{stopped_by(solid()), touching_any(hit::kShield, &Saw::shield),
                                 touching_any(hit::kViking, &Saw::viking)}
                         : Looks{};
    }
};

// Something that flies or falls straight until it hits a wall, Olaf's shield or a Viking, whom it burns: the
// fireball a fire shooter sends out (it also gives out after a while), and the drop that falls from above.
class Fireball : public Entity {
  public:
    void update(World& world) override {
        switch (s.state) {
            case State::kStart:
                s.drop = kind == prehistoric_kind::kFireDrop;
                world.play(*this, s.drop ? anim::kFireballDropFalls : anim::kFireballFlies);
                stay_active();
                if (s.drop) {
                    speed_y = px(6);
                } else {
                    speed_x = px(8);
                    count_down_from(35);
                }
                s.state = State::kFlying;
                break;
            case State::kFlying:
                if (!s.drop && (saw.wall || saw.shield != kNobody)) {
                    world.sound(*this, effect::kBurst);
                    return end(world, anim::kFireballBursts, true);
                }
                if (s.drop && saw.shield != kNobody) {
                    return end(world, anim::kFireballDropSplashes, true);
                }
                if (saw.viking != kNobody) {
                    target = saw.viking;
                    if (Entity* other = world.at(saw.viking)) {
                        const bool from_the_right = s.drop ? x >= other->x : facing_left();
                        harm(*other, message::kBurned, 4, from_the_right);
                    }
                    world.spawn(*this, kind::kSpark, x, y, 0, entity_flag::kLargePieces);
                    world.remove(slot);
                    return;
                }
                if (s.drop && saw.landed) {
                    return end(world, anim::kFireballDropSplashes, true);
                }
                message = 0;
                message_value = 0;
                if (!s.drop && countdown == 0) {
                    return end(world, anim::kFireballGivesOut, false);
                }
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
    // Flying: a wall ends one that was shot, a floor one that drops; Olaf's shield, held the way it comes from,
    // stops either.
    Looks looks_for() const override {
        if (s.state != State::kFlying) {
            return {};
        }
        const Look viking = touching_any(hit::kViking, &Saw::viking);
        return s.drop ? Looks{lands_on(solid()), touching_any(hit::kShieldOverhead, &Saw::shield), viking}
                      : Looks{stopped_by(solid()), touching_any(hit::kShield, &Saw::shield), viking};
    }

  private:
    enum class State : std::uint8_t { kStart, kFlying, kEnding };

    void end(World& world, Animation animation, bool stops) {
        world.play(*this, animation);
        if (stops) {
            speed_x = speed_y = 0;
        }
        s.state = State::kEnding;
        world.animate(*this);
    }

    struct Saved {
        State state = State::kStart;
        bool drop = false;
    } s;
    LVR_SAVES(Entity)
};

// Fire thrown in an arc (its argument: to the left, straight up, or to the right). It goes out on a wall, the
// ground or Olaf's shield, and after a while by itself; a Viking it hits is burnt.
class ArcingFire : public Entity {
  public:
    void update(World& world) override {
        switch (s.state) {
            case State::kStart:
                world.play(*this, anim::kArcingFireFlies);
                start_falling();
                stay_active();
                count_down();
                s.from_the_right = argument == 0;
                speed_x = static_cast<std::int16_t>(argument == 0 ? px(-8) : (argument == 1 ? 0 : px(12)));
                speed_y = argument == 1 ? px(-12) : px(-10);
                countdown = argument == 0 ? 35 : (argument == 1 ? 25 : 40);
                s.state = State::kFlying;
                break;
            case State::kFlying:
                if (saw.wall || saw.landed || saw.shield != kNobody || saw.other != kNobody) {
                    world.play(*this, anim::kArcingFireGoesOut);
                    speed_x = 0;
                    stop_falling();
                    s.state = State::kEnding;
                    break;
                }
                if (saw.viking != kNobody) {
                    target = saw.viking;
                    if (Entity* other = world.at(saw.viking)) {
                        harm(*other, message::kBurned, 4, s.from_the_right);
                    }
                    world.spawn(*this, kind::kSpark, x, y, 0, entity_flag::kLargePieces);
                    world.remove(slot);
                    return;
                }
                message = 0;
                message_value = 0;
                if (countdown == 0) {
                    world.play(*this, anim::kArcingFireFades);
                    s.state = State::kEnding;
                }
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
    Looks looks_for() const override {
        return s.state == State::kFlying
                   ? Looks{stopped_by(solid()), lands_on(solid()), touching_any(hit::kShield, &Saw::shield),
                           touching_any(hit::kShieldOverhead, &Saw::other), touching_any(hit::kViking, &Saw::viking)}
                   : Looks{};
    }

  private:
    enum class State : std::uint8_t { kStart, kFlying, kEnding };

    struct Saved {
        State state = State::kStart;
        bool from_the_right = false;
    } s;
    LVR_SAVES(Entity)
};

// What is left of an enemy that was killed: its animation, then it is gone.
class Remains : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            acts_as = tile::kBody;
            static const TileTypes floor =
                tile_types({tile::kGround, tile::kGround02, tile::kLadderTop, tile::kGround20});
            const bool on_floor = find_at(world, *this, box.left, y + 1, floor).has_value() &&
                                  find_at(world, *this, box.right, y + 1, floor).has_value();
            world.play(*this, on_floor ? anim::kRemainsFallsApart : anim::kRemainsFallsApartInTheAir);
            if (facing_left()) {
                world.flip_across(*this);
            }
        } else if (animation_over()) {
            world.remove(slot);
            return;
        }
        world.animate(*this);
    }
};

// A blast: its animation, then it is gone. The large one shakes the screen.
class Blast : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            stay_active();
            if (argument == 0) {
                world.shake_y = {.amount = 3, .offset = 0, .ticks = 40};
                world.play(*this, anim::kBlastLarge);
            } else {
                world.play(*this, anim::kBlastSmall);
            }
        } else if (animation_over()) {
            world.remove(slot);
            return;
        }
        world.animate(*this);
    }
};

}  // namespace

std::span<const KindOf> shots() {
    static constexpr KindOf kList[] = {
        {Home::kAnywhere, kind::kGun, make<Gun>},
        {Home::kFactory, factory_kind::kTurret, make<Turret>},
        {Home::kPrehistoric, prehistoric_kind::kFireShooter, make<FireShooter>},
        {Home::kPrehistoric, prehistoric_kind::kArcingFireShooter, make<FireShooter>},
        {Home::kPrehistoric, prehistoric_kind::kFireShooterInAWall, make<FireShooter>},
        {Home::kWacky, wacky_kind::kBurner, make<Burner>},
        {Home::kPrehistoric, prehistoric_kind::kSpit, make<Spit>},
        {Home::kWacky, wacky_kind::kThrown, make<Thrown>},
        {Home::kWacky, wacky_kind::kFlame, make<Flame>},
        {Home::kWacky, wacky_kind::kFlameShot, make<Flame>},
        {Home::kFactory, factory_kind::kShot, make<RobotShot>},
        {Home::kFactory, factory_kind::kDroppedShot, make<RobotShot>},
        {Home::kAnywhere, kind::kDroneShot, make<DroneShot>},
        {Home::kPrehistoric, prehistoric_kind::kFireball, make<Fireball>},
        {Home::kPrehistoric, prehistoric_kind::kFireDrop, make<Fireball>},
        {Home::kPrehistoric, prehistoric_kind::kArcingFire, make<ArcingFire>},
        {Home::kAnywhere, kind::kRemains, make<Remains>},
        {Home::kFactory, factory_kind::kBlast, make<Blast>},
    };
    return kList;
}

}  // namespace lvr
