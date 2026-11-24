// What a Viking is told (blows, deaths, a scene's orders), and how he dies.
#include "lvr/viking.hpp"

namespace lvr {

// --- What he is told ---

bool Viking::told(World& world) {
    switch (message) {
        case message::kStruck:
        case message::kBurned:
        case message::kDrowning:
        case message::kShot:
        case message::kBlast:
        case message::kScorched:
            world.play(*this, looks().is_hurt);
            doing_ = Doing::kOther;
            hurt(world);
            return true;
        case message::kSquashed:
            squashed(world);
            return true;
        case message::kImpaledFromAbove:
        case message::kImpaled:
            if (world.database != database::kSpaceship) {  // impaled, in the worlds that have spikes
                sound(world, effect::kImpaled);
                speed_x = speed_y = 0;
                set_falling(false);
                die(world);
                acts_as = tile::kBody;
                world.play(*this, by_who(anim::kVikingImpaledFromBelow));
                dying(world);
                return true;
            }
            world.remove(slot);
            return true;
        case message::kDissolved:
            if (world.database == database::kEgypt) {  // a drop from above: he dissolves
                sound(world, effect::kDrowned);
                die(world);
                speed_x = speed_y = 0;
                world.play(*this, by_who(anim::kVikingDissolves));
                if (!on_ground(world)) {
                    set_falling(true);
                    dying_in_the_air(world);
                } else {
                    set_falling(false);
                    dying_on_the_ground(world);
                }
                return true;
            }
            world.remove(slot);
            return true;
        case message::kMummified:
            if (world.database == database::kEgypt) {  // a mummy has him: he becomes one
                sound(world, effect::kMummified);
                die(world);
                speed_x = speed_y = 0;
                world.play(*this, by_who(anim::kVikingMummified));
                becoming_a_mummy(world);
                return true;
            }
            world.remove(slot);
            return true;
        case message::kBlown:
            if (world.database == database::kWacky) {  // a puff of air: pushed along by it
                message = 0;
                const bool same_way = blow_from_the_right() == facing_left();
                const auto push = static_cast<std::int16_t>(blow_strength());
                forget_blow_side();
                speed_x = static_cast<std::int16_t>(same_way ? speed_x + push : speed_x - push);
                return false;
            }
            world.remove(slot);
            return true;
        case message::kLedWalk:
        case message::kLedCheer:
            if (world.database == database::kScenes) {  // a scene's director leads him
                if (message == message::kLedWalk) {
                    led_walk(world);
                } else {
                    cheer(world);
                }
                return true;
            }
            world.remove(slot);
            return true;
        case message::kVanish:
            world.remove(slot);
            return true;
        case message::kDropped:
            message = 0;
            fall(world);
            return true;
        case message::kElectrocuted:
            if (world.database == database::kPrehistoric || world.database == database::kEgypt) {
                world.remove(slot);  // nothing there says so; their Vikings have no such death
                return true;
            }
            die(world);
            acts_as = tile::kBody;
            speed_x = speed_y = 0;
            set_falling(false);
            world.play(*this, looks().electrocuted);
            dying(world);
            return true;
        case message::kConversation:
            message = 0;
            show_pieces(world);
            return false;
        case message::kCrushed:
            return crushed(world);
        case message::kThrown:
            speed_y = static_cast<std::int16_t>(message_value);
            message = 0;
            return false;
        case message::kHeldInTheAir:
            if (who_ != kind::kOlaf) {
                message = 0;
                return false;
            }
            hang(world);
            return true;
        case message::kLetGo:
            message = 0;
            if (who_ != kind::kOlaf) {
                return false;
            }
            fall(world);
            return true;
        case message::kBlownBack:
            message = 0;
            if (who_ != kind::kOlaf || is_falling()) {
                return false;
            }
            if (!on_own_ground(world)) {
                fall(world);
                return true;
            }
            doing_ = Doing::kOther;
            speed_x = kBlownBack;
            keep_on_ground();
            world.animate(*this);
            wait(At::kBlownBack, {Test::kWall});
            return true;
        case message::kBeamed:
            message = 0;
            if (world.database != database::kSpaceship) {
                return false;
            }
            beamed_ = true;
            speed_x = speed_y = 0;
            set_falling(false);
            doing_ = Doing::kTeleporting;
            hit_class = 0;
            hit_mask &= static_cast<std::uint16_t>(~kVikingMaskBits);
            sound(world, effect::kTeleportedAway);
            world.play(*this, by_who(anim::kVikingBeamedAway));
            fade_away(world);
            return true;
        default:
            message = 0;
            return false;
    }
}

bool Viking::told_while_hurt(World& world) {
    const bool egypt = world.database == database::kEgypt;
    switch (message) {
        case message::kSquashed:
            if (on_ground(world)) {
                squashed(world);
                return true;
            }
            message = 0;
            return false;
        case message::kElectrocuted:
            if (egypt) {
                message = 0;
                return false;
            }
            return told(world);
        case message::kDropped:
            message = 0;
            if (!egypt && world.database != database::kWacky) {
                set_falling(true);
            }
            return false;
        case message::kDissolved:
        case message::kMummified:
            if (world.database == database::kSpaceship || world.database == database::kWacky) {
                message = 0;
                return false;
            }
            return told(world);
        case message::kConversation:
            return told(world);
        case message::kBlast:
            message = 0;
            forget_blow_side();
            if (message_value >= 20) {
                die_where_he_is(world, false);
                return true;
            }
            return false;
        default:
            message = 0;
            return false;
    }
}

bool Viking::told_while_dizzy(World& world) {
    if (message == message::kDropped) {
        message = 0;
        set_falling(true);
        dizzy_in_the_air(world);
        return true;
    }
    return told_while_hurt(world);
}

void Viking::lose_health(World& world) {
    const int amount = static_cast<int>(blow_strength());
    const int left = static_cast<std::int16_t>(health) - amount;
    health = static_cast<std::uint16_t>(left < 1 ? 0 : left);
    if (slot >= 0 && slot < kVikingSlots) {  // the shield item's point goes with it
        std::uint16_t& armour = world.armour.at(static_cast<std::size_t>(slot));
        armour = static_cast<std::uint16_t>(int{armour} < amount ? 0 : int{armour} - amount);
    }
}

void Viking::hurt(World& world) {
    lose_health(world);
    if (health == 0) {
        // The spaceship's Vikings have a death by drowning; the other worlds' one by fire instead.
        if (world.database == database::kSpaceship) {
            return die_where_he_is(world, message == message::kDrowning);
        }
        if (message == message::kBurned || message == message::kScorched) {
            sound(world, effect::kDrowned);
            die(world);
            speed_x = speed_y = 0;
            set_falling(false);
            world.play(*this, by_who(anim::kVikingBurnsUp));
            return dying_on_the_ground(world);
        }
        return die_where_he_is(world, false);
    }
    sound(world, effect::kVikingHurt);
    message = 0;
    hit_from_the_front_ = blow_from_the_right() != facing_left();
    speed_x = static_cast<std::int16_t>(hit_from_the_front_ ? -kKnockedBack : kKnockedBack);
    if (!on_ground(world)) {
        set_falling(true);
    }
    being_hurt(world);
}

void Viking::being_hurt(World& world) {
    if (!is_falling()) {
        keep_on_ground();
    }
    world.animate(*this);
    if (speed_y > 0) {
        doing_ = Doing::kHurtFalling;
        wait(At::kHurt, {Test::kWall, Test::kLands, Test::kDeadly});
    } else {
        doing_ = Doing::kHurt;
        wait(At::kHurt, {Test::kWall, Test::kFloorOrCeiling, Test::kDeadly});
    }
}

void Viking::hurt_after_acting(World& world) {
    if ((world.pressed & key::kE) != 0 && use_item(world)) {
        return;
    }
    hurt_after_item(world);
}

void Viking::hurt_after_item(World& world) {
    if (animation_waits()) {
        return being_hurt(world);
    }
    speed_x = 0;
    message = 0;
    doing_ = Doing::kOther;
    if (!on_ground(world)) {
        return fall(world);
    }
    speed_y = 0;
    set_falling(false);
    if (who_ == kind::kBaleog) {
        if (played(world)) {
            return stand(world);
        }
        // Left to himself he turns to where it came from and strikes back.
        if (!hit_from_the_front_) {
            world.flip_across(*this);
        }
        return swing(world);
    }
    if (who_ == kind::kOlaf) {
        if (played(world)) {
            return stand(world);
        }
        // Left to himself he turns to where it came from, his shield before him.
        if (!hit_from_the_front_) {
            world.flip_across(*this);
        }
        lower_shield(world);
        if ((world.pressed & key::kS) != 0 && act(world)) {
            return;
        }
        return standing_after_acting(world);
    }
    standing(world);
}

void Viking::carry_on(World& world) {
    switch (doing_) {
        case Doing::kClimbing:
            return climb(world);
        case Doing::kFalling:
            return falling(world);
        case Doing::kTeleporting:
            world.play(*this, shield_up_ ? anim::kOlafStandsShieldUp : looks().back_on_his_feet);
            return stand(world);
        case Doing::kHanging:
            return hang(world);
        case Doing::kDazedInTheAir:
            return fall(world);
        default:
            break;
    }
    if (!on_own_ground(world)) {
        return fall(world);
    }
    set_falling(false);
    if (world.vikings_arriving != 0) {  // the level's start waits until all three are on their feet
        world.vikings_arriving &= static_cast<std::uint16_t>(~by_who(1, 2, 4));
        world.play(*this, looks().stops);
        return arriving(world);
    }
    stand(world);
}

// --- Dying ---

void Viking::die(World& world) {
    shield_away_ = who_ == kind::kOlaf;
    if (slot >= 0 && slot < kVikingSlots) {
        world.armour.at(static_cast<std::size_t>(slot)) = 0;
    }
    health = 0;
    hit_class = 0;
    hit_mask = 0;
}

bool Viking::deadly(World& world) {
    if (find_beside(world, *this, Side::kBelow, types().spring)) {
        return false;
    }
    if (find_beside(world, *this, Side::kAbove, types().spring)) {
        speed_y = 0;
        return false;
    }
    speed_x = speed_y = 0;
    set_falling(false);
    // Each world has its own of these; a world without one has no such cells.
    if (find_beside(world, *this, Side::kBelow, types().water)) {
        sound(world, effect::kSplash);
        die(world);
        world.play(*this, by_who(anim::kVikingGoesUnder));
        dying(world);
        return true;
    }
    if (find_beside(world, *this, Side::kBelow, types().lava)) {
        sound(world, effect::kDrowned);
        die(world);
        world.play(*this, looks().drowns);
        dying(world);
        return true;
    }
    const bool spikes_below = find_beside(world, *this, Side::kBelow, types().spikes).has_value();
    if (spikes_below || find_beside(world, *this, Side::kAbove, types().spikes)) {
        sound(world, effect::kImpaled);
        die(world);
        acts_as = tile::kBody;
        world.play(*this, spikes_below ? by_who(anim::kVikingImpaledFromBelow) : by_who(anim::kVikingImpaledFromAbove));
        dying(world);
        return true;
    }
    if (find_beside(world, *this, Side::kBelow, types().quicksand)) {
        die(world);
        sound(world, effect::kSinking);
        speed_y = kSinkingSpeed;
        world.play(*this, by_who(anim::kVikingSinks));
        sink(world);
        return true;
    }
    const bool below = find_beside(world, *this, Side::kBelow, types().electric).has_value();
    if (below || find_beside(world, *this, Side::kAbove, types().electric)) {
        die(world);
        acts_as = tile::kBody;
        if (world.database == database::kSpaceship) {
            world.play(*this, below ? looks().electrocuted_from_below : looks().electrocuted_from_above);
        } else {  // the factory's and the wacky world's, named in the factory's database
            world.play(*this, below ? by_who(anim::kVikingShockedFromBelow) : by_who(anim::kVikingShockedFromAbove));
        }
        dying(world);
        return true;
    }
    if (find_beside(world, *this, Side::kBelow, types().rotating_spikes)) {
        die(world);
        sound(world, effect::kSpiked);
        world.play(*this, looks().spiked);
        dying(world);
        return true;
    }
    die_where_he_is(world, false);
    return true;
}

void Viking::die_where_he_is(World& world, bool drowned) {
    sound(world, drowned ? effect::kDrowned : effect::kVikingHurt);
    die(world);
    speed_x = 0;
    world.play(*this, drowned ? looks().drowns : looks().dies);
    if (!on_ground(world)) {
        set_falling(true);
        return dying_in_the_air(world);
    }
    speed_y = 0;
    set_falling(false);
    dying_on_the_ground(world);
}

void Viking::squashed(World& world) {
    message = 0;
    if (!on_ground(world)) {  // pushed down instead
        if (speed_y < static_cast<std::int16_t>(message_value)) {
            set_falling(true);
            speed_y = static_cast<std::int16_t>(message_value);
            if (doing_ != Doing::kFalling) {
                doing_ = Doing::kOther;
            }
        }
        return carry_on(world);
    }
    die(world);
    sound(world, effect::kSquashed);
    speed_x = speed_y = 0;
    world.play(*this, message_value >= 0x300 ? looks().squashed_flat : looks().squashed);
    if (is_falling()) {
        return dying_in_the_air(world);
    }
    dying_on_the_ground(world);
}

bool Viking::crushed(World& world) {
    message = 0;
    const auto push = static_cast<std::int16_t>(message_value);
    if (push == 0) {
        return false;
    }
    // Squashed against a wall on the side he is pushed towards.
    const Side side = (push > 0) != facing_left() ? Side::kAhead : Side::kBehind;
    if (!find_beside(world, *this, side, types().solid)) {
        return false;
    }
    die(world);
    sound(world, effect::kSquashed);
    acts_as = tile::kFree;
    speed_x = speed_y = 0;
    set_falling(false);
    world.play(*this, looks().crushed);
    face(world, push >= 0);
    crush_parts_left_ = 4;
    world.animate(*this);
    wait(At::kCrushed);
    return true;
}

void Viking::being_crushed(World& world) {
    if (!animation_waits() && --crush_parts_left_ == 0) {
        return dead(world);
    }
    world.animate(*this);
    wait(At::kCrushed);
}

void Viking::sink(World& world) {
    world.animate(*this);
    wait(At::kSinking);
}

void Viking::dying(World& world) {
    if (!animation_waits()) {
        return dead(world);
    }
    world.animate(*this);
    wait(At::kDying);
}

void Viking::becoming_a_mummy(World& world) {
    if (!animation_waits()) {
        hide_pieces(world);
        world.spawn(*this, egypt_kind::kMummy, x, y, 0, entity_flag::kAlwaysActive | entity_flag::kLargePieces);
        world.animate(*this);
        return wait(At::kBecameAMummy);
    }
    world.animate(*this);
    if (is_falling()) {
        wait(At::kBecomingAMummy, {Test::kLandsAnywhere});
    } else {
        wait(At::kBecomingAMummy);
    }
}

void Viking::dying_on_the_ground(World& world) {
    if (!animation_waits()) {
        return dead(world);
    }
    world.animate(*this);
    wait(At::kDyingOnTheGround);
}

void Viking::dying_in_the_air(World& world) {
    if (!animation_waits()) {
        return dead(world);
    }
    world.animate(*this);
    wait(At::kDyingInTheAir, {Test::kLands});
}

void Viking::dead(World& world) {
    acts_as = tile::kBody;
    Entity::dead = true;
    if (slot >= 0 && slot < kVikingSlots) {
        world.items.at(static_cast<std::size_t>(slot)) = {};
        world.chosen_square.at(static_cast<std::size_t>(slot)) = 0;
    }
    hide_pieces(world);
    wait(At::kDead);
}

// A turn of being hurt, dizzy or dying.
void Viking::resume_harmed(World& world) {
    switch (at_) {
        case At::kHurt:
            if (found(Test::kWall)) {
                speed_x = 0;
            }
            if (landed() || found(Test::kFloorOrCeiling)) {
                if (doing_ == Doing::kHurtFalling) {
                    set_falling(false);
                }
                speed_y = 0;
            }
            if (found(Test::kDeadly) && deadly(world)) {
                return;
            }
            if (message != 0 && told_while_hurt(world)) {
                return;
            }
            if ((world.pressed & key::kS) != 0 && act(world)) {
                return;
            }
            return hurt_after_acting(world);
        case At::kDizzy:
            if (found(Test::kWall)) {
                speed_x = 0;
            }
            if (found(Test::kDeadly) && deadly(world)) {
                return;
            }
            if (found(Test::kWall) || found(Test::kDeadly) || message != 0 || (world.pressed & key::kSOrE) != 0) {
                returns_to_landing_ = false;
            }
            if ((world.pressed & key::kS) != 0 && act(world)) {
                return;
            }
            return dizzy_after_acting(world);
        case At::kDizzyInTheAir:
            if (found(Test::kWall) || (landed() || found(Test::kFloorOrCeiling)) || found(Test::kDeadly) ||
                (world.pressed & key::kSOrE) != 0) {
                returns_to_landing_ = false;
            }
            if (found(Test::kWall)) {
                speed_x = 0;
            }
            if (landed() || found(Test::kFloorOrCeiling)) {
                speed_y = 0;
                if (doing_ == Doing::kDizzyFalling) {  // landed
                    set_falling(false);
                    return dizzy(world);
                }
            }
            if (found(Test::kDeadly) && deadly(world)) {
                return;
            }
            if ((world.pressed & key::kS) != 0 && act(world)) {
                return;
            }
            return dizzy_after_acting(world);
        case At::kDying:
            return dying(world);
        case At::kSinking:
            if (find_beside(world, *this, Side::kAbove, types().quicksand)) {
                return dead(world);
            }
            return sink(world);
        case At::kBecomingAMummy:
            if (landed()) {
                set_falling(false);
                speed_y = 0;
            }
            return becoming_a_mummy(world);
        case At::kBecameAMummy:
            if (!animation_waits()) {
                return dead(world);
            }
            world.animate(*this);
            return wait(At::kBecameAMummy);
        case At::kDyingOnTheGround:
            if (!on_ground(world)) {
                set_falling(true);
                return dying_in_the_air(world);
            }
            return dying_on_the_ground(world);
        case At::kDyingInTheAir:
            if (landed()) {
                speed_y = 0;
                set_falling(false);
                return dying_on_the_ground(world);
            }
            return dying_in_the_air(world);
        case At::kCrushed:
            return being_crushed(world);
        default:  // dead: nothing more; and the states of the other subjects
            return;
    }
}

}  // namespace lvr
