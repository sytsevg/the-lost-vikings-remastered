// What only some worlds ask of a Viking: blown up like a balloon (the wacky world), at a crane's controls (the
// factory), led by a scene's director.
#include "lvr/viking.hpp"

namespace lvr {

// --- The wacky world: blown up like a balloon ---

void Viking::use_pump(World& world, Entity& pump) {
    if (inflated) {
        return inflated_keys(world, false);
    }
    if (!on_ground(world) || doing_ == Doing::kDizzy || doing_ == Doing::kDizzyRising ||
        doing_ == Doing::kDizzyFalling) {
        return after_acting(world);
    }
    speed_x = speed_y = 0;
    set_falling(false);
    sound(world, effect::kPumped);
    pump.message = message::kAsked;
    if (who_ == kind::kOlaf) {
        shield_away_ = true;
    }
    world.play(*this, by_who(anim::kVikingPumps));
    world.animate(*this);
    wait(At::kInflating);
}

void Viking::inflated_float(World& world) {
    if ((warned_ || deflating_) && !animation_waits()) {  // the air is out
        count_down(false);
        inflated = false;
        warned_ = deflating_ = false;
        doing_ = Doing::kDizzy;
        return fall(world);
    }
    world.animate(*this);
    wait(At::kInflated, {Test::kWall, Test::kFloorOrCeiling, Test::kDeadly});
}

void Viking::let_air_out(World& world) {
    deflating_ = true;
    world.play(*this, by_who(anim::kVikingLetsAirOut));
    inflated_float(world);
}

void Viking::inflated_slow_down() {
    if (static_cast<std::uint16_t>(speed_x) <= px(0.5)) {
        speed_x = 0;
    } else if (speed_x > 0) {
        speed_x -= px(0.5);
    } else {
        speed_x += px(0.5);
    }
}

void Viking::inflated_steer(World& world) {
    const bool left = (world.held & key::kLeft) != 0;
    if (left || (world.held & key::kRight) != 0) {
        if (facing_left() != left) {
            speed_x = static_cast<std::int16_t>(-speed_x);
        }
        face(world, left);
        doing_ = left ? Doing::kWalkingLeft : Doing::kWalkingRight;
        if (speed_x < px(4)) {
            speed_x += (speed_x >= 0 ? px(0.25) : px(0.5));
        }
    } else {
        inflated_slow_down();
    }
    inflated_float(world);
}

void Viking::inflated_keys(World& world, bool also_act) {
    if (also_act && (world.pressed & key::kS) != 0 && act(world)) {
        return;
    }
    if ((world.pressed & key::kE) != 0 && use_item(world)) {
        return;
    }
    inflated_steer(world);
}

void Viking::pop(World& world) {
    sound(world, effect::kPopped);
    die(world);
    speed_x = 0;
    set_falling(true);
    world.play(*this, anim::kVikingPops);
    dying_in_the_air(world);
}

bool Viking::inflated_deadly(World& world) {
    if (find_beside(world, *this, Side::kBelow, types().spring)) {
        return false;
    }
    if (find_beside(world, *this, Side::kAbove, types().spring)) {
        speed_y = 0;
        return false;
    }
    speed_x = speed_y = 0;
    set_falling(false);
    static const TileTypes below =
        tile_types({tile::kWater, tile::kLava11, tile::kLava, tile::kSpikes, tile::kElectricity});
    static const TileTypes above = tile_types({tile::kSpikes, tile::kElectricity});
    if (find_beside(world, *this, Side::kBelow, below) || find_beside(world, *this, Side::kAbove, above)) {
        pop(world);
        return true;
    }
    return false;
}

bool Viking::told_inflated(World& world) {
    switch (message) {
        case message::kStruck:
        case message::kBurned:
        case message::kDrowning:
        case message::kShot:
        case message::kBlast:
        case message::kScorched:
            message = 0;
            lose_health(world);
            if (health == 0) {
                pop(world);
            } else {
                let_air_out(world);
            }
            return true;
        case message::kDissolved:
        case message::kElectrocuted:
        case message::kImpaledFromAbove:
        case message::kImpaled:
        case message::kCrushed:
            pop(world);
            return true;
        case message::kThrown:
            speed_y = static_cast<std::int16_t>(message_value);
            message = 0;
            return false;
        default:
            message = 0;
            return false;
    }
}

void Viking::inflated_turn(World& world) {
    if (found(Test::kWall)) {
        speed_x = 0;
    }
    if (found(Test::kDeadly) && inflated_deadly(world)) {
        return;
    }
    if (message != 0 && told_inflated(world)) {
        return;
    }
    if (!warned_ && !deflating_ && countdown == 0) {  // the air runs out: a wobble first
        warned_ = deflating_ = true;
        world.play(*this, by_who(anim::kVikingWobbles));
        return inflated_float(world);
    }
    // He rises, up to two pixels a tick.
    if (speed_y < px(-2)) {
        speed_y += px(0.25);
    } else if (speed_y > px(-2)) {
        speed_y -= px(0.25);
    }
    if (!played(world)) {
        if (!warned_ && !deflating_) {
            inflated_slow_down();
        }
        return inflated_float(world);
    }
    if ((warned_ || !deflating_) && ((world.pressed & key::kF) != 0 || (world.pressed & key::kD) != 0)) {
        warned_ = false;
        return let_air_out(world);
    }
    inflated_keys(world, true);
}

void Viking::take_controls(World& world) {
    if (!on_ground(world) || doing_ == Doing::kDizzy || doing_ == Doing::kDizzyRising ||
        doing_ == Doing::kDizzyFalling) {
        return after_acting(world);
    }
    speed_x = speed_y = 0;
    set_falling(false);
    if (who_ == kind::kOlaf) {
        shield_away_ = true;
    }
    world.play(*this, by_who(anim::kVikingAtControls));
    wait(At::kTakingControls, {Test::kTouchesThing});
}

void Viking::at_controls(World& world, bool first) {
    if (found(Test::kTouchesThing)) {
        target = touched_;
    }
    const Entity* panel = target >= 0 && target < kSlots ? world.at(target) : nullptr;
    if (first) {
        if (panel != nullptr) {
            face(world, panel->facing_left());
            push_x = static_cast<std::int16_t>(static_cast<std::uint16_t>((panel->x - x) * 256));
        }
    } else {
        if (message == message::kSquashed) {
            squashed(world);
            return;
        }
        message = 0;
        // Which crane: the argument of what is in the slot of the controls he took. They may be gone from
        // there (he was carried off still holding on): then it is what was there last, as the original reads it.
        if (panel != nullptr) {
            crane_ = panel->argument;
        }
        if (played(world)) {
            const std::uint16_t crane = crane_;
            if ((world.pressed & key::kD) != 0 || (world.pressed & key::kS) != 0) {
                push_x = facing_left() ? px(-8) : px(8);
                world.factory.cranes.at(crane == 1 || crane == 2 ? crane : 0) &= static_cast<std::uint16_t>(~1U);
                doing_ = Doing::kOther;
                return carry_on(world);  // he falls if the controls weren't on the ground
            }
            std::uint16_t& asked = world.factory.cranes.at(crane < 2 ? crane : 2);
            if ((world.pressed & key::kF) != 0) {
                asked ^= 1U;
            }
            if ((world.held & key::kLeft) != 0) {
                asked |= 2U;
            } else if ((world.held & key::kRight) != 0) {
                asked |= 4U;
            }
        }
    }
    world.animate(*this);
    wait(At::kAtControls, {Test::kTouchesThing});
}

void Viking::step_into(World& world, const Entity& teleporter) {
    if (!on_ground(world) || doing_ == Doing::kDizzy || doing_ == Doing::kDizzyRising ||
        doing_ == Doing::kDizzyFalling) {
        return after_acting(world);
    }
    speed_x = speed_y = 0;
    set_falling(false);
    doing_ = Doing::kTeleporting;
    hit_class = 0;
    hit_mask &= static_cast<std::uint16_t>(~kVikingMaskBits);
    sound(world, effect::kTeleportedAway);
    world.play(*this, by_who(anim::kVikingBeamedAway));
    teleports_to_ = teleporter_destination(teleporter.argument).value_or(TeleporterDestination{});
    fade_away(world);
}

void Viking::fade_away(World& world) {
    keep_on_ground();
    world.animate(*this);
    wait(At::kFadingAway);
}

void Viking::appear(World& world) {
    keep_on_ground();
    world.animate(*this);
    wait(At::kAppearing);
}

void Viking::after_acting(World& world) {
    if (inflated) {
        return inflated_keys(world, false);
    }
    switch (doing_) {
        case Doing::kHurt:
        case Doing::kHurtFalling:
            return hurt_after_acting(world);
        case Doing::kDizzy:
        case Doing::kDizzyRising:
        case Doing::kDizzyFalling:
            return dizzy_after_acting(world);
        case Doing::kFalling:
            return falling_after_acting(world);
        case Doing::kCharging:
            return charging_after_acting(world);
        case Doing::kHeadHit:
            return head_hit_after_acting(world);
        case Doing::kBouncing:
        case Doing::kBouncingAgain:
            return bouncing_after_acting(world);
        case Doing::kGettingUp:
        case Doing::kStoppingCharge:
            return on_his_feet_after_acting(world);
        default:
            return standing_after_acting(world);
    }
}

// --- In the scenes outside the levels: led by the scene's director ---

void Viking::led_walk(World& world) {
    message = 0;
    face(world, false);
    speed_x = px(6.5);
    world.play(*this, by_who(anim::kVikingLedWalks));
    led_walking(world);
}

void Viking::led_walking(World& world) {
    keep_on_ground();
    world.animate(*this);
    wait(At::kLedWalking, {Test::kWall});
}

void Viking::led_stand(World& world) {
    message = 0;
    speed_x = 0;
    world.play(*this, by_who(anim::kVikingLedStands));
    led_standing(world);
}

void Viking::led_standing(World& world) {
    world.animate(*this);
    wait(At::kLedStanding);
}

void Viking::cheer(World& world) {
    message = 0;
    hit_class = 0;
    hit_mask = 0;
    acts_as = tile::kBody;
    world.play(*this, who_ == kind::kOlaf && world.scene == scene::kShip ? anim::kOlafCheersOnTheShip
                                                                         : by_who(anim::kVikingCheers));
    cheering(world);
}

void Viking::cheering(World& world) {
    keep_on_ground();
    world.animate(*this);
    wait(At::kCheering);
}

void Viking::arriving(World& world) {
    keep_on_ground();
    world.animate(*this);
    wait(At::kArriving);
}

// A turn of what the worlds and the scenes have him do: teleporters and beams, the pump's air, the crane's
// controls, being led.
void Viking::resume_in_world(World& world) {
    switch (at_) {
        case At::kFadingAway:
            if (animation_waits()) {
                return fade_away(world);
            }
            if (beamed_) {  // to the beam's point, side by side: Erik on it, Baleog and Olaf behind him
                face(world, !world.beam.faces_right);
                const int behind = by_who(0, 32, 64);
                teleports_to_ = {.x = world.beam.x + (facing_left() ? behind : -behind), .y = world.beam.y};
            }
            world.play(*this, by_who(anim::kVikingBeamedIn));
            push_x = static_cast<std::int16_t>(teleports_to_.x - x);
            push_y = static_cast<std::int16_t>(teleports_to_.y - y);
            moved_by = kMovedFreely;
            return wait(beamed_ ? At::kBeamed : At::kTeleported);
        case At::kBeamed:  // a tick more than a teleporter's
            return wait(At::kTeleported);
        case At::kTeleported:
            // Beamed, he waits for the view whether he is played or not.
            if ((beamed_ || played(world)) && (static_cast<std::uint16_t>(world.camera.moved_x) >= kCameraAtRest ||
                                               static_cast<std::uint16_t>(world.camera.moved_y) >= kCameraAtRest)) {
                return wait(At::kTeleported);
            }
            beamed_ = false;
            sound(world, effect::kTeleportedIn);
            return appear(world);
        case At::kAppearing:
            if (animation_waits()) {
                return appear(world);
            }
            hit_class = hit_class::kViking;
            hit_mask |= kVikingMaskBits;
            message = 0;
            return carry_on(world);
        case At::kTakingControls:
            return at_controls(world, true);
        case At::kAtControls:
            return at_controls(world, false);
        case At::kLedWalking:
        case At::kLedStanding:
            if (message == message::kLedStand) {
                return led_stand(world);
            }
            if (message == message::kLedCheer) {
                return cheer(world);
            }
            message = 0;
            return at_ == At::kLedWalking ? led_walking(world) : led_standing(world);
        case At::kCheering:
            return cheering(world);
        case At::kInflating:
            if (animation_waits()) {
                world.animate(*this);
                return wait(At::kInflating);
            }
            inflated = true;
            count_down_from(100);
            speed_y = px(-2);
            return inflated_float(world);
        case At::kInflated:
            return inflated_turn(world);
        default:
            return;
    }
}

}  // namespace lvr
