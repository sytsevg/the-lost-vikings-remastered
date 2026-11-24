#pragma once

// What the enemies of every world share: what they act as, the tile sets they look for, and the base class
// `Enemy`: health kept while outside the active area, turning before they walk out of the view or into each
// other, and being hurt or killed by a blow. The enemies themselves are in a file per family (walkers.cpp,
// chargers.cpp, shooters.cpp, bombers.cpp).

#include <cstdint>

#include "lvr/hits.hpp"
#include "lvr/kinds.hpp"
#include "lvr/shared.hpp"
#include "lvr/terrain.hpp"
#include "lvr/world.hpp"

namespace lvr {

// What the spaceship's enemies share: their health kept while they are outside the active area, turning before
// they walk out of the view or into each other, and being hurt: thrown by the blow until its animation ends
// (Olaf's shield or a wall in the way stops them), or killed.
class Enemy : public Entity {
  public:
    void update(World& world) final {
        if (!enemy.hurt) {
            return turn(world);
        }
        if (!animation_over()) {
            return being_hurt(world);
        }
        enemy.hurt = false;
        if (enemy.turns_after) {
            countdown = kTurnedTicks;
            world.flip_across(*this);
            enemy.turns_after = false;
        }
        recovered(world);
    }
    // While a blow throws him he looks for nothing; otherwise for what his kind says (looks()).
    Looks looks_for() const final { return enemy.hurt ? Looks{} : looks(); }
    void outside_active_area(World& world) override {
        if (placement >= 0) {
            Placement& mine = world.placements.at(static_cast<std::size_t>(placement));
            mine.x = x;
            mine.y = y;
            mine.argument = health;
            mine.flags = flags & entity_flag::kKeptInPlacement;
        }
        world.remove(slot);
    }

  protected:
    static constexpr std::uint16_t kTurnedTicks = 10;  // before he turns again
    static constexpr int kViewMargin = 16;
    static constexpr std::int16_t kThrownForward = px(3);
    static constexpr std::int16_t kThrownBack = px(-4);

    // His turn while nothing has hurt him, and what he does when he is over a blow.
    virtual void turn(World& world) = 0;
    virtual void recovered(World& world) = 0;
    // What he looks for after a move, by what he is doing.
    virtual Looks looks() const { return {}; }
    bool is_hurt() const noexcept { return enemy.hurt; }
    // An enemy's first turn: the health he left with the last time, his first animation (named in the `home`
    // database) and a step of it, and what he is to the others from now on.
    void appear(World& world, Animation animation) {
        take_kept_health();
        world.play(*this, animation);
        world.animate(*this);
        acts_as = tile::kEnemy;
        count_down();
    }

    void take_kept_health() {
        if (argument != 0) {  // the health he left with, the last time
            health = argument;
            argument = 0;
        }
    }

    bool on_ground(const World& world) const { return find_beside(world, *this, Side::kBelow, ground()).has_value(); }

    // Olaf's shield, or a Viking, right in front of him.
    bool in_his_way(const World& world) const {
        return find_beside(world, *this, Side::kAhead, shield()).has_value() ||
               find_viking_beside(world, *this, Side::kAhead, vikings()).has_value();
    }

    // He turns before he walks out of the view (16 pixels of slack either side).
    void stay_in_view(World& world) {
        const bool to_the_right = (speed_x >= 0) != facing_left();
        const auto edge = static_cast<std::int16_t>(static_cast<std::uint16_t>(
            world.camera.x + (to_the_right ? world.camera.view_width + kViewMargin : -kViewMargin)));
        if (to_the_right ? x >= edge : x < edge) {
            world.flip_across(*this);
        }
    }

    // Killed: what is left of him falls where he stood, and he is never made again.
    void leave_remains(World& world) {
        world.spawn(*this, kind::kRemains, x, box.bottom, 0, entity_flag::kLargePieces);
        gone_for_good(world, *this);
    }

    // Spikes under him kill him. True if they did.
    bool dies_on_spikes(World& world) {
        static const TileTypes spikes = tile_types({tile::kSpikes});
        if (!find_beside(world, *this, Side::kBelow, spikes)) {
            return false;
        }
        leave_remains(world);
        return true;
    }

    // A turn of falling: a wall he fell against takes his speed across. True when he has landed (his fall is over).
    bool landed_from_fall() {
        if (saw.wall) {
            speed_x = 0;
        }
        if (!saw.landed) {
            return false;
        }
        stop_falling();
        return true;
    }

    // He turns round; `pause`: and doesn't turn again, or look for a Viking, for that many ticks.
    void turn_round(World& world, std::uint16_t pause = 0) {
        world.flip_across(*this);
        if (pause != 0) {
            countdown = pause;
        }
    }

    // Another enemy touches him (saw.enemy): if it walks the same way ahead of him, he turns round.
    void turn_from_colleague(World& world) {
        if (saw.enemy != kNobody) {
            turn_from(world, saw.enemy);
        }
    }

    // Whether the nearest Viking is ahead of him and about level with him (within `level` pixels up or down), and
    // nearer than `within` if that is given: what a shooter looks for before he shoots.
    static constexpr int kAnyDistance = 32767;
    bool viking_ahead(World& world, int level, int within = kAnyDistance) {
        const World::Offset to = world.offset_to(*this, world.nearest_viking(*this));
        return to.ahead >= 0 && to.below < level && to.below >= -level && (within == kAnyDistance || to.ahead < within);
    }

    // Two that walk the same way and touch: the one behind turns round.
    void turn_from(World& world, int other_slot) {
        target = other_slot;
        const Entity* other = world.at(other_slot);
        if (other != nullptr && other->facing_left() == facing_left() &&
            world.offset_to(*this, other_slot).ahead >= 0) {
            world.flip_across(*this);
        }
    }

    // A Viking close behind him, about level: he turns to him (not again for a while).
    void face_viking_behind(World& world) {
        if (countdown != 0) {
            return;
        }
        const World::Offset to = world.offset_to(*this, world.nearest_viking(*this));
        if (to.below < -48 || to.below > 24 || to.ahead > 0) {
            return;
        }
        countdown = kTurnedTicks;
        world.flip_across(*this);
    }

    // Whether the entity he deals with is about level with him. If not: the next Viking after it that he touches
    // and that is (Olaf's turn is his shield's); that one is then the one he deals with.
    bool level_with(World& world, World::Offset& to) {
        to = world.offset_to(*this, target);
        if (to.below < 16 && to.below >= -24) {
            return true;
        }
        while (target >= 0 && target < kVikingSlots) {
            if (target == slot::kOlaf) {
                const Entity* olaf = world.at(slot::kOlaf);
                if (olaf == nullptr || olaf->health == 0 || olaf->last_created < 0) {
                    return false;
                }
                target = olaf->last_created;
            } else {
                ++target;
            }
            const Entity* other = world.at(target);
            if (other == nullptr || other->health == 0 || box.right < other->box.left || other->box.right < box.left ||
                box.bottom < other->box.top || other->box.bottom < box.top) {
                continue;
            }
            to = world.offset_to(*this, target);
            if (to.below < 16 && to.below >= -24) {
                return true;
            }
        }
        return false;
    }

    // In the scenes outside the levels a creature is told to bring its own colours. True if that was it.
    bool told_colours(World& world) {
        if (message != message::kLedColours || world.database != database::kScenes) {
            return false;
        }
        message = 0;
        // Where each creature's colours are in VIKINGS.EXE.
        struct Colours {
            std::uint16_t kind;
            std::size_t at;
        };
        static constexpr std::array<Colours, 13> kColours{{
            {prehistoric_kind::kBrawler, 0x3bf6},
            {prehistoric_kind::kSpitter, 0x3c26},
            {prehistoric_kind::kRoller, 0x3c56},
            {egypt_kind::kHopper, 0x3c86},
            {kind::kBomber, 0x3cb6},
            {egypt_kind::kMummy, 0x3ce6},
            {factory_kind::kShooter, 0x3d16},
            {factory_kind::kWalker, 0x3d46},
            {wacky_kind::kThrower, 0x3d76},
            {kind::kCharger, 0x3da6},
            {kind::kNearTrooper, 0x3dd6},
            {kind::kCloseGunner, 0x3e06},
            {wacky_kind::kClown, 0x3e36},
        }};
        for (const Colours& colours : kColours) {
            if (colours.kind == kind) {
                world.load_colours(colours.at);
            }
        }
        return true;
    }

    // Led by a scene's director, he shows an animation and then goes on in one of three ways.
    enum class Led : std::uint8_t {
        kStill,     // as he stands: nothing of him moves
        kAnimated,  // his animation runs
        kMoving,    // it runs, and he is kept on the ground as he goes
    };
    // Starts being led: the director's word is taken, the animation (named in the `home` database) shown and
    // stepped once. `on_ground`: he is put on the ground for that step.
    void follow(World& world, Animation animation, Led how, bool on_ground = true) {
        message = 0;
        world.play(*this, animation);
        if (on_ground) {
            set_flag(entity_flag::kPutOnGround);
        }
        world.animate(*this);
        enemy.led = how;
    }
    // A turn of being led.
    void led_on(World& world) {
        if (enemy.led == Led::kMoving) {
            set_flag(entity_flag::kPutOnGround);
        }
        if (enemy.led != Led::kStill) {
            world.animate(*this);
        }
    }

    // What a scene's director tells him to do, each kind in its own way: to do what he does to a Viking where he
    // stands, to stand, to walk on. (Nothing in the levels tells an enemy these.)
    virtual void led_to_attack(World& /*world*/) {}
    virtual void led_to_stand(World& /*world*/) {}
    virtual void led_to_walk(World& /*world*/) {}
    // A blow, for a kind that takes it in its own way; as it is: told().
    virtual bool struck(World& world) { return told(world); }

    // Everything he can be told: the director's words, or a blow. True if his turn ended (or he is gone).
    bool told_all(World& world) {
        switch (message) {
            case message::kLedAttack:
                led_to_attack(world);
                return true;
            case message::kLedStand:
                led_to_stand(world);
                return true;
            case message::kLedTurn:
                world.flip_across(*this);
                [[fallthrough]];
            case message::kLedWalk:
                led_to_walk(world);
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
                return struck(world);
        }
    }

    // What he is told: a blow hurts or kills; anything else is forgotten. True if his turn ended (or he is gone).
    // `turns_whatever`: he turns round after it wherever the blow came from.
    bool told(World& world, bool turns_whatever = false) {
        if (message != message::kSwordBlow && message != message::kArrowBlow && message != message::kHeadButt &&
            message != message::kBlast) {
            message = 0;
            return false;
        }
        stop_falling();
        if (enemy.own_hurt_animation.place != 0) {
            world.play(*this, enemy.own_hurt_animation);
        } else {
            world.play(*this, anim::kEnemyIsHurt);
        }
        world.sound(*this, effect::kEnemyHurt);
        const bool from_the_right = blow_from_the_right();
        forget_blow_side();
        if (message_value >= health) {  // killed: what is left of him falls where he stood
            leave_remains(world);
            return true;
        }
        health -= message_value;
        message = 0;
        message_value = 0;
        // Hit from behind he is thrown forward; from the front, back, and then he turns to it.
        const bool from_behind = from_the_right == facing_left();
        enemy.turns_after = from_behind || turns_whatever;
        speed_x = from_behind ? kThrownForward : kThrownBack;
        enemy.hurt = true;
        being_hurt(world);
        return true;
    }

  private:
    void being_hurt(World& world) {
        const Entity* olaf = world.at(slot::kOlaf);
        const bool olaf_faces_left = olaf != nullptr && olaf->facing_left();
        if (find_beside(world, *this, Side::kAhead, shield()) && facing_left() != olaf_faces_left &&
            speed_x == kThrownForward) {
            speed_x = 0;
        }
        if (find_beside(world, *this, Side::kBehind, shield()) && facing_left() == olaf_faces_left &&
            speed_x == kThrownBack) {
            speed_x = 0;
        }
        if (find_beside(world, *this, Side::kAhead, walls()) && speed_x == kThrownForward) {
            speed_x = 0;
        }
        if (find_beside(world, *this, Side::kBehind, walls()) && speed_x == kThrownBack) {
            speed_x = 0;
        }
        set_flag(entity_flag::kPutOnGround);
        world.animate(*this);
    }

  protected:
    struct Saved {
        // An enemy with a hurt animation of its own: its place, and the database that is named in.
        Animation own_hurt_animation{};  // place 0: none of its own
        bool hurt = false;
        bool turns_after = false;
        Led led = Led::kStill;
    } enemy;
    LVR_SAVES_AS(Entity, enemy)
};

}  // namespace lvr
