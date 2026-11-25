// What each Viking can do that the others can't: Erik's jump and charge, Olaf's shield, Baleog's sword and bow.
#include "lvr/viking.hpp"

namespace lvr {

// --- Erik: the jump, and the charge that ends head first ---

void Viking::jump(World& world) {
    world.play(*this, looks().rises);
    const auto speed = static_cast<std::uint16_t>(speed_x);
    speed_y -= (speed >= px(8) ? px(10.5) : (speed >= kRunning ? px(10) : px(9.5)));
    fall(world);
}

void Viking::charge(World& world) {
    s.doing = Doing::kCharging;
    world.play(*this, anim::kErikCharges);
    charging(world);
}

void Viking::charging(World& world) {
    keep_on_ground();
    world.animate(*this);
    wait(At::kCharging, {Test::kTouchesEnemy, Test::kBreakableWall, Test::kWall, Test::kDeadly});
}

void Viking::charging_after_acting(World& world) {
    if ((input(world).pressed & key::kE) != 0 && use_item(world)) {
        return;
    }
    charging_after_item(world);
}

void Viking::charging_after_item(World& world) {
    // He charges on until the charge's animation ends, or the key of the way he faces is let go.
    if (!animation_waits() || (input(world).held & (facing_left() ? key::kLeft : key::kRight)) == 0) {
        s.doing = Doing::kStoppingCharge;
        world.play(*this, anim::kErikStopsCharging);
        return on_his_feet(world);
    }
    charging(world);
}

void Viking::head_hits_enemy(World& world) {
    target = s.touched;
    if (Entity* enemy = world.at(s.touched)) {
        harm(*enemy, message::kHeadButt, 2, facing_left());
        // The roller, in four frames of his (13 to 16), shows no spark.
        const bool shielded = enemy->kind == prehistoric_kind::kRoller && enemy->sprite.packed_frame >= 13 &&
                              enemy->sprite.packed_frame < 17;
        if (!shielded) {
            world.spawn(*this, kind::kSpark, box.right + (facing_left() ? -10 : 10), y, 1, entity_flag::kLargePieces);
        }
    }
    head_hits(world);
}

void Viking::head_hits_wall(World& world, bool breakable) {
    if (breakable) {  // rubble, at the middle of the cell ahead
        const int ahead = x + (facing_left() ? -13 : 13);
        world.spawn(*this, kind::kRubble, static_cast<int>((static_cast<std::uint16_t>(ahead) & 0xfff0U) | 8U),
                    static_cast<int>((static_cast<std::uint16_t>(y) & 0xfff0U) | 8U));
    }
    head_hits(world);
}

void Viking::head_hits(World& world) {
    s.doing = Doing::kHeadHit;
    world.shake_x = {.amount = 1, .offset = 0, .ticks = 2};
    speed_x = 0;
    world.play(*this, anim::kErikHitsHisHead);
    sound(world, effect::kHeadButt);
    head_hit(world);
}

void Viking::head_hit(World& world) {
    if ((input(world).pressed & key::kS) != 0 && act(world)) {
        return;
    }
    head_hit_after_acting(world);
}

void Viking::head_hit_after_acting(World& world) {
    if ((input(world).pressed & key::kE) != 0 && use_item(world)) {
        return;
    }
    head_hit_after_item(world);
}

void Viking::head_hit_after_item(World& world) {
    if (animation_waits()) {
        world.animate(*this);
        return wait(At::kHeadHit, {Test::kDeadly});
    }
    // Thrown back, up and away from it.
    world.play(*this, anim::kErikBouncesBack);
    s.doing = Doing::kBouncing;
    speed_x = px(-3.5);
    speed_y = px(-4);
    set_falling(true);
    bounce(world);
}

void Viking::bounce(World& world) {
    world.animate(*this);
    if (speed_y > 0) {
        wait(At::kBouncingDown, {Test::kWall, Test::kLands, Test::kDeadly});
    } else {
        wait(At::kBouncing, {Test::kWall, Test::kFloorOrCeiling, Test::kDeadly});
    }
}

void Viking::bouncing_after_acting(World& world) {
    if ((input(world).pressed & key::kE) != 0 && use_item(world)) {
        return;
    }
    bouncing_after_item(world);
}

void Viking::bounce_again(World& world) {
    s.doing = Doing::kBouncingAgain;
    world.play(*this, anim::kErikBouncesAgain);
    speed_y = px(-3);
    set_falling(true);
    if (speed_x != 0) {
        speed_x += px(1.5);
    }
    bounce(world);
}

void Viking::get_up(World& world) {
    s.doing = Doing::kGettingUp;
    sound(world, effect::kBackOnHisFeet);
    world.play(*this, anim::kErikGetsUp);
    speed_x = speed_y = 0;
    set_falling(false);
    on_his_feet(world);
}

void Viking::on_his_feet(World& world) {
    keep_on_ground();
    world.animate(*this);
    wait(s.doing == Doing::kGettingUp ? At::kGettingUp : At::kStoppingCharge, {Test::kWall, Test::kDeadly});
}

void Viking::on_his_feet_after_acting(World& world) {
    if ((input(world).pressed & key::kE) != 0 && use_item(world)) {
        return;
    }
    on_his_feet_after_item(world);
}

// --- Olaf: the shield ---

void Viking::turn_shield(World& world) {
    if (s.shield_up) {
        lower_shield(world);
    } else {
        s.shield_up = true;
        if (sprite.packed_frame < kOlafRaises.size()) {
            world.play(*this, kOlafRaises.at(sprite.packed_frame));
        } else if (s.doing != Doing::kPushing) {
            world.play(*this, anim::kOlafStopsShieldUp);
        }
    }
    if ((input(world).pressed & key::kS) != 0 && act(world)) {
        return;
    }
    standing_after_acting(world);
}

void Viking::lower_shield(const World& world) {
    s.shield_up = false;
    const std::size_t frame = sprite.packed_frame;
    if (frame >= kOlafRaises.size() && frame < kOlafRaises.size() + kOlafLowers.size()) {
        world.play(*this, kOlafLowers.at(frame - kOlafRaises.size()));
    } else if (s.doing != Doing::kPushing) {
        world.play(*this, looks().stops);
    }
}

void Viking::hang(World& world) {
    message = 0;
    s.doing = Doing::kHanging;
    world.play(*this, anim::kOlafGlides);
    world.animate(*this);
    wait(At::kHanging, {Test::kWall});
}

void Viking::hanging(World& world) {
    if (played(world)) {
        if ((input(world).pressed & key::kFOrD) != 0) {
            s.doing = Doing::kOther;
            return turn_shield(world);
        }
        if ((input(world).pressed & key::kE) != 0 && use_item(world)) {
            return;
        }
        return hanging_after_item(world);
    }
    wait(At::kHanging, {Test::kWall});
}

void Viking::hanging_after_item(World& world) {
    if ((input(world).held & key::kLeft) != 0) {
        face(world, true);
    } else if ((input(world).held & key::kRight) != 0) {
        face(world, false);
    }
    wait(At::kHanging, {Test::kWall});
}

// --- Baleog: the sword and the bow ---

void Viking::swing(World& world) {
    s.swing = world.random() < 30000 ? 0 : 1;
    world.play(*this, kBaleogSwings.at(s.swing));
    s.doing = Doing::kStanding;
    if (s.swing == 1) {
        set_falling(false);
        speed_y = 0;
    }
    speed_x = 0;
    world.spawn(*this, kind::kSword, x + (facing_left() ? -13 : 13), y, 0, entity_flag::kLargePieces);
    baleog_waits(world, At::kSwinging);
}

void Viking::draw_bow(World& world) {
    world.play(*this, anim::kBaleogDrawsBow);
    s.doing = Doing::kOther;
    speed_x = speed_y = 0;
    set_falling(false);
    baleog_waits(world, At::kDrawingBow);
}

void Viking::baleog_waits(World& world, At at) {
    world.animate(*this);
    wait(at, {Test::kDeadly});
}

void Viking::baleog_goes_on(World& world) {
    switch (s.at) {
        case At::kDrawingBow:
            return baleog_waits(world, animation_waits() ? At::kDrawingBow : At::kAiming);
        case At::kAiming:
            if ((input(world).held & key::kD) != 0) {
                return baleog_waits(world, At::kAiming);
            }
            world.play(*this, anim::kBaleogLooses);
            sound(world, effect::kArrowLoosed);
            world.spawn(*this, s.has_fire_arrows ? kind::kFireArrow : kind::kArrow, x, y - 4);
            return baleog_waits(world, At::kLoosing);
        case At::kLoosing:
            if (animation_waits()) {
                return baleog_waits(world, At::kLoosing);
            }
            world.play(*this, looks().stops);
            return keys(world);
        case At::kSwinging:
            if (animation_waits()) {
                return baleog_waits(world, At::kSwinging);
            }
            world.play(*this, kBaleogSwingsOn.at(s.swing));
            return baleog_waits(world, At::kSwingingOn);
        default:
            if (animation_waits()) {
                return baleog_waits(world, At::kSwingingOn);
            }
            return stand(world);
    }
}

// A turn of what only one of them can do: Erik's charge and its ends, Baleog's bow and sword, Olaf blown back.
void Viking::resume_ability(World& world) {
    switch (s.at) {
        case At::kCharging:
            if (found(Test::kTouchesEnemy)) {
                return head_hits_enemy(world);
            }
            if (found(Test::kBreakableWall)) {
                return head_hits_wall(world, true);
            }
            if (found(Test::kWall)) {
                return head_hits_wall(world, false);
            }
            if (found(Test::kDeadly) && deadly(world)) {
                return;
            }
            if (!on_ground(world)) {
                return fall(world);
            }
            if (message != 0 && told(world)) {
                return;
            }
            if ((input(world).pressed & key::kS) != 0 && act(world)) {
                return;
            }
            return charging_after_acting(world);
        case At::kHeadHit:
            if (found(Test::kDeadly) && deadly(world)) {
                return;
            }
            if (!on_ground(world)) {
                return fall(world);
            }
            if (message != 0 && told(world)) {
                return;
            }
            return head_hit(world);
        case At::kBouncing:
        case At::kBouncingDown:
            if (found(Test::kWall)) {
                speed_x = 0;
            }
            if (landed() || found(Test::kFloorOrCeiling)) {
                if (s.at == At::kBouncingDown) {
                    return s.doing == Doing::kBouncing ? bounce_again(world) : get_up(world);
                }
                speed_y = 0;
            }
            if (found(Test::kDeadly) && deadly(world)) {
                return;
            }
            if (message != 0 && told(world)) {
                return;
            }
            if ((input(world).pressed & key::kS) != 0 && act(world)) {
                return;
            }
            return bouncing_after_acting(world);
        case At::kHanging:
            if (found(Test::kWall)) {
                speed_x = 0;
            }
            if (message != 0 && told(world)) {
                return;
            }
            return hanging(world);
        case At::kBlownBack:
            if (found(Test::kWall)) {
                speed_x = 0;
            }
            return grounded(world);
        case At::kDrawingBow:
        case At::kAiming:
        case At::kLoosing:
        case At::kSwinging:
        case At::kSwingingOn:
            if (found(Test::kDeadly) && deadly(world)) {
                return;
            }
            if (!on_ground(world)) {
                return fall(world);
            }
            if (message != 0 && told(world)) {
                return;
            }
            return baleog_goes_on(world);
        case At::kGettingUp:
        case At::kStoppingCharge:
            if (found(Test::kWall)) {
                speed_x = 0;
            }
            if (found(Test::kDeadly) && deadly(world)) {
                return;
            }
            if (!on_ground(world)) {
                return fall(world);
            }
            if (message != 0 && told(world)) {
                return;
            }
            if (!animation_waits()) {
                return keys(world);
            }
            if ((input(world).pressed & key::kS) != 0 && act(world)) {
                return;
            }
            return on_his_feet_after_acting(world);
        default:
            return;
    }
}

}  // namespace lvr
