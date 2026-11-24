// What the Vikings hit with: Baleog's sword and arrows, and the spark where a blow lands. Each is an entity of its
// own that lives for a moment.

#include <array>

#include "lvr/hits.hpp"
#include "lvr/kinds.hpp"
#include "lvr/shared.hpp"
#include "lvr/terrain.hpp"
#include "lvr/world.hpp"

namespace lvr {

namespace {

// Tells the entity in `slot` it was hit: what by, how hard, and from which side.
void strike(World& world, const Entity& by, int slot, std::uint16_t what, std::uint16_t strength) {
    if (Entity* other = world.at(slot)) {
        harm(*other, what, strength, by.facing_left());
    }
}

// A spark where a blow landed: its animation, then it is gone.
class Spark : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            world.play(*this, argument == 0 ? anim::kSparkSmall : anim::kSparkBig);
        } else if (animation_over()) {
            world.remove(slot);
            return;
        }
        world.animate(*this);
    }
};

// Baleog's sword: unseen, in front of him for three ticks. The first enemy it touches is hit.
class Sword : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            count_down_from(kTicks);
            return;
        }
        if (saw.enemy != kNobody) {
            target = saw.enemy;
            strike(world, *this, saw.enemy, message::kSwordBlow, 2);
            world.spawn(*this, kind::kSpark, x + (facing_left() ? -kReach : kReach), y, 1, entity_flag::kLargePieces);
            world.remove(slot);
            return;
        }
        if (saw.other != kNobody) {  // one a sword doesn't hurt: it is told all the same
            target = saw.other;
            strike(world, *this, saw.other, message::kSwordBlow, 0);
            world.remove(slot);
            return;
        }
        if (countdown == 0) {
            world.remove(slot);
        }
    }
    Looks looks_for() const override {
        return started()
                   ? Looks{touching_any(hit::kHurtBySword, &Saw::enemy), touching_any(hit::kToldOfSword, &Saw::other)}
                   : Looks{};
    }

  private:
    static constexpr std::uint16_t kTicks = 3;
    static constexpr int kReach = 14;
};

// Baleog's arrow, plain or burning: it flies until it hits an enemy, or breaks on a wall or on what stops arrows.
// One kind of enemy sends a plain arrow back, and then it hits a Viking.
class Arrow : public Entity {
  public:
    explicit Arrow(bool burning) : burning_{burning} {}

    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                speed_x = kSpeed;
                world.play(*this, burning_ ? anim::kFireArrowFlies : anim::kArrowFlies);
                state_ = State::kFlying;
                break;
            case State::kFlying:
                if (saw.other != kNobody) {
                    target = saw.other;
                    return hits(world);
                }
                if (saw.another != kNobody) {
                    target = saw.another;
                    return breaks(world);
                }
                if (saw.wall) {
                    return breaks(world);
                }
                break;
            case State::kSentBack:
                if (saw.wall) {
                    return breaks(world);
                }
                if (saw.other != kNobody) {
                    target = saw.other;
                    return breaks(world);
                }
                if (saw.another != kNobody) {
                    target = saw.another;
                    strike(world, *this, target, kArrowBack, 2);
                    world.spawn(*this, kind::kSpark, x + (facing_left() ? -kAhead : kAhead), y, 0,
                                entity_flag::kLargePieces);
                    world.remove(slot);
                    return;
                }
                break;
            case State::kBroken:
                if (animation_over()) {
                    world.remove(slot);
                    return;
                }
                break;
        }
        world.animate(*this);
    }
    // Flying: what it hurts, and what stops it. Sent back by a shield: what stops it then, and a Viking.
    Looks looks_for() const override {
        switch (state_) {
            case State::kFlying:
                return {stopped_by(solid()), touching_any(burning_ ? hit::kHurtByFire : hit::kHurtByArrow, &Saw::other),
                        touching_any(burning_ ? hit::kStopsFire : hit::kStopsArrow, &Saw::another)};
            case State::kSentBack:
                return {stopped_by(solid()), touching_any(hit::kShield, &Saw::other),
                        touching_any(hit::kViking, &Saw::another)};
            default:
                return {};
        }
    }

  private:
    enum class State : std::uint8_t { kStart, kFlying, kSentBack, kBroken };
    static constexpr std::int16_t kSpeed = px(12);
    static constexpr int kAhead = 6;
    static constexpr std::uint16_t kArrowBack = 7;
    // The kind with this number sends a plain arrow back: the spaceship's second gunner, which no level and no
    // scene places and no script makes. The game has the enemy and doesn't use it.
    static constexpr std::uint16_t kSendsBack = 0x78;

    void hits(World& world) {
        const Entity* enemy = world.at(target);
        if (!burning_ && enemy != nullptr) {
            // On the spitter's back a plain arrow breaks.
            if (enemy->kind == prehistoric_kind::kSpitter && enemy->facing_left() == facing_left()) {
                return breaks(world);
            }
            if (enemy->kind == kSendsBack) {
                world.flip_across(*this);
                state_ = State::kSentBack;
                world.animate(*this);
                return;
            }
        }
        strike(world, *this, target, message::kArrowBlow, burning_ ? 8 : 2);
        world.spawn(*this, kind::kSpark, x + (facing_left() ? -kAhead : kAhead), y, 1, entity_flag::kLargePieces);
        world.remove(slot);
    }

    void breaks(World& world) {
        world.sound(*this, effect::kArrowBreaks);
        world.play(*this, burning_ ? anim::kFireArrowBroken : anim::kArrowBroken);
        speed_x = 0;
        message = 0;
        state_ = State::kBroken;
        world.animate(*this);
    }

    bool burning_;
    State state_ = State::kStart;
};

// A gun's shot: it flies until it hits a Viking, a wall, or Olaf's shield held against it.
class Bullet : public StraightShot {
    static constexpr std::uint16_t kShot = 1;

    void fired(World& world) override {
        world.play(*this, anim::kBulletFlies);
        speed_x = px(8);
    }
    bool wall_stops_it_first() const override { return false; }  // a Viking in a wall's cell is still hit
    void hit_viking(World& world) override {
        target = saw.viking;
        strike(world, *this, saw.viking, kShot, 2);
        world.spawn(*this, kind::kSpark, x, y, 0, entity_flag::kLargePieces);
        world.remove(slot);
    }
    void bursting(World& world) override {
        world.sound(*this, effect::kBurst);
        speed_x = 0;
        world.play(*this, anim::kBulletBursts);
    }
};

}  // namespace

std::unique_ptr<Entity> make_weapon(std::uint16_t kind) {
    switch (kind) {
        case kind::kSword:
            return make_entity<Sword>();
        case kind::kArrow:
        case kind::kFireArrow:
            return make_entity<Arrow>(kind == kind::kFireArrow);
        case kind::kBullet:
            return make_entity<Bullet>();
        default:
            return make_entity<Spark>();
    }
}

}  // namespace lvr
