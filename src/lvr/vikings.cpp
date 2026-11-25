// The Vikings: Erik, Baleog and Olaf (docs/original/vikings.md). What the three share is here once: standing,
// walking, falling and landing, ladders, acting on things, getting hurt and dying. Each one's abilities are his own.
//
// A Viking's turn ends where he waits for the next tick (`At`). What stops him after the tick's move, and who
// touches him, is tested later in the tick (`Test`) and acted on when his next turn starts.

#include <algorithm>
#include <memory>

#include "lvr/viking.hpp"

namespace lvr {

// --- Small things ---

void Viking::after_move(World& world) {
    for (std::size_t i = 0; i < s.tests.size(); ++i) {
        switch (s.tests.at(i)) {
            case Test::kWall:
                s.hit.at(i) = stop_at_wall(world, *this, types().solid);
                break;
            case Test::kBreakableWall:
                s.hit.at(i) = stop_at_wall(world, *this, types().breakable);
                break;
            case Test::kFloorOrCeiling:
                s.hit.at(i) = stop_at_floor_or_ceiling(world, *this, types().solid);
                break;
            case Test::kDeadly:
                s.hit.at(i) = stop_at_floor_or_ceiling(world, *this, types().deadly);
                break;
            case Test::kLands:
                s.hit.at(i) = land(world, *this, types().ground);
                break;
            case Test::kLandsAnywhere:
                s.hit.at(i) = land(world, *this, types().landing);
                break;
            default:
                break;
        }
    }
}

void Viking::check_hits(World& world) {
    for (std::size_t i = 0; i < s.tests.size(); ++i) {
        if (s.tests.at(i) == Test::kTouchesClass) {
            s.touched = find_touching(world, *this, s.touch_class);
            s.hit.at(i) = s.touched != kNobody;
        } else if (s.tests.at(i) == Test::kTouchesEnemy || s.tests.at(i) == Test::kTouchesThing) {
            s.touched =
                find_touching_any(world, *this, s.tests.at(i) == Test::kTouchesEnemy ? hit::kEnemy : hit::kThing);
            s.hit.at(i) = s.touched != kNobody;
        }
    }
}

void Viking::outside_active_area(World& world) {
    if (!Entity::dead) {  // a dead one stays as he is
        world.remove(slot);
    }
}

const VikingLooks& Viking::looks() const {
    return s.who == kind::kErik ? kErikLooks : (s.who == kind::kBaleog ? kBaleogLooks : kOlafLooks);
}

void Viking::wait(At at, std::array<Test, 4> tests) {
    if (at != At::kDizzy && at != At::kDizzyInTheAir) {
        s.returns_to_landing = false;
    }
    s.at = at;
    s.tests = tests;
    s.hit = {};
}

bool Viking::found(Test test) const {
    for (std::size_t i = 0; i < s.tests.size(); ++i) {
        if (s.tests.at(i) == test) {
            return s.hit.at(i);
        }
    }
    return false;
}

void Viking::face(World& world, bool left) {
    if (facing_left() != left) {
        world.flip_across(*this);
    }
}

bool Viking::on_ground(const World& world) const {
    return find_beside(world, *this, Side::kBelow, types().ground).has_value();
}

bool Viking::on_own_ground(const World& world) const {
    return find_beside(world, *this, Side::kBelow, s.who == kind::kOlaf ? types().landing : types().ground).has_value();
}

std::uint8_t Viking::type_at(const World& world, int at_x, int at_y) const {
    return world.map.tile_type_at(static_cast<std::int16_t>(at_x), static_cast<std::int16_t>(at_y));
}

std::uint16_t Viking::by_who(std::uint16_t erik, std::uint16_t baleog, std::uint16_t olaf) const {
    return s.who == kind::kErik ? erik : (s.who == kind::kBaleog ? baleog : olaf);
}

const Animation& Viking::by_who(const anim::ByViking& each) const {
    return each.at(s.who == kind::kErik ? 0 : (s.who == kind::kBaleog ? 1 : 2));
}

void Viking::hide_pieces(World& world) const {
    for (int index = sprite.first; index < sprite.first + sprite.count; ++index) {
        Piece& piece = world.piece_at(index);
        piece.hidden = true;
        piece.erase |= 2U;
    }
}

void Viking::show_pieces(World& world) const {
    for (int index = sprite.first; index < sprite.first + sprite.count; ++index) {
        Piece& piece = world.piece_at(index);
        piece.hidden = false;
        piece.blinked_off = false;
        piece.redraw = 2;
        piece.erase = 0;
    }
}

// --- The turn ---

// Each state belongs to one of five subjects, and its turn is with them.
void Viking::resume(World& world) {
    switch (s.at) {
        case At::kStart:
        case At::kStarted:
        case At::kAboutToFallIn:
        case At::kFallingIn:
        case At::kStanding:
        case At::kRising:
        case At::kFalling:
        case At::kOntoLadder:
        case At::kClimbing:
        case At::kArriving:
            return resume_moving(world);
        case At::kUsingKey:
        case At::kHandingOver:
        case At::kActing:
        case At::kActed:
            return resume_acting(world);
        case At::kFadingAway:
        case At::kBeamed:  // a tick more than a teleporter's
        case At::kTeleported:
        case At::kAppearing:
        case At::kTakingControls:
        case At::kAtControls:
        case At::kLedWalking:
        case At::kLedStanding:
        case At::kCheering:
        case At::kInflating:
        case At::kInflated:
            return resume_in_world(world);
        case At::kHurt:
        case At::kDizzy:
        case At::kDizzyInTheAir:
        case At::kDying:
        case At::kSinking:
        case At::kBecomingAMummy:
        case At::kBecameAMummy:
        case At::kDyingOnTheGround:
        case At::kDyingInTheAir:
        case At::kCrushed:
        case At::kDead:
            return resume_harmed(world);
        case At::kCharging:
        case At::kHeadHit:
        case At::kBouncing:
        case At::kBouncingDown:
        case At::kHanging:
        case At::kBlownBack:
        case At::kDrawingBow:
        case At::kAiming:
        case At::kLoosing:
        case At::kSwinging:
        case At::kSwingingOn:
        case At::kGettingUp:
        case At::kStoppingCharge:
            return resume_ability(world);
    }
}

// Standing, walking, jumping, falling, climbing: a turn of one of those.
void Viking::resume_moving(World& world) {
    switch (s.at) {
        case At::kStart:
            return start(world);
        case At::kStarted:
            if (world.vikings_falling_in == 0) {
                return stand(world);
            }
            if (s.who == kind::kErik) {
                world.play(*this, looks().falls_far);
            }
            return wait(At::kAboutToFallIn);
        case At::kAboutToFallIn:
            if (s.who == kind::kErik) {
                sound(world, effect::kFallingIn);
            } else {  // Baleog and Olaf
                world.play(*this, looks().falls_far);
            }
            set_falling(true);
            return fall_in(world);
        case At::kFallingIn:
            if (landed()) {
                if (s.who == kind::kErik || world.database == database::kScenes) {  // in a scene: each of them
                    world.stop_sound(*this, effect::kFallingIn);
                    sound(world, world.database == database::kScenes ? effect::kLandedInScene : effect::kLandedHard);
                }
                return land_hard(world);
            }
            return fall_in(world);
        case At::kStanding:
            if (found(Test::kWall)) {
                speed_x = 0;
            }
            if (found(Test::kDeadly) && deadly(world)) {
                return;
            }
            return standing(world);
        case At::kRising:
            if (found(Test::kWall)) {
                speed_x = 0;
            }
            if (found(Test::kFloorOrCeiling)) {
                speed_y = 0;
            }
            if (found(Test::kDeadly) && deadly(world)) {
                return;
            }
            if (message != 0 && told(world)) {
                return;
            }
            if (s.who == kind::kErik && (input(world).held & key::kF) != 0) {  // the key held: a higher jump
                speed_y -= px(0.25);
            }
            return falling(world);
        case At::kFalling:
            if (found(Test::kWall)) {
                speed_x = 0;
            }
            if (landed()) {
                return landed(world);
            }
            if (found(Test::kDeadly) && deadly(world)) {
                return;
            }
            if (message != 0 && told(world)) {
                return;
            }
            return falling(world);
        case At::kOntoLadder:
            world.play(*this, s.after_step);
            return climb(world);
        case At::kClimbing:
            if (found(Test::kFloorOrCeiling)) {
                set_falling(false);
                speed_y = 0;
            }
            if (found(Test::kDeadly) && deadly(world)) {
                return;
            }
            if (message != 0 && told(world)) {
                return;
            }
            if (!played(world)) {
                return climb(world);
            }
            if ((input(world).pressed & key::kE) != 0 && use_item(world)) {
                return;
            }
            return climbing(world);
        case At::kArriving:
            if (world.vikings_arriving == 0) {
                return stand(world);
            }
            return arriving(world);
        default:
            return;
    }
}

void Viking::start(World& world) {
    const bool on_the_ship = world.database == database::kScenes && world.scene == scene::kShip;
    if (on_the_ship && s.who == kind::kOlaf) {  // the ship's scene: they cheer from the start
        return cheer(world);
    }
    if (on_the_ship) {
        s.doing = s.who == kind::kErik ? Doing::kStanding : Doing::kOther;
        world.play(*this, looks().stands);
        keep_on_ground();
        world.animate(*this);
        return cheer(world);
    }
    if (s.who == kind::kOlaf) {  // his shield is an entity of its own
        world.spawn(*this, kind::kShield, x, y, kShieldArgument, entity_flag::kLargePieces);
    }
    s.doing = s.who == kind::kErik ? Doing::kStanding : Doing::kOther;
    acts_as = tile::kViking;
    world.play(*this, looks().stands);
    keep_on_ground();
    world.animate(*this);
    wait(At::kStarted);
}

void Viking::fall_in(World& world) {
    world.animate(*this);
    wait(At::kFallingIn, {Test::kLandsAnywhere});
}

// --- Standing and walking ---

void Viking::stand(World& world) {
    keep_on_ground();
    world.animate(*this);
    wait(At::kStanding, {Test::kWall, Test::kDeadly});
}

void Viking::standing(World& world) {
    if (message != 0 && told(world)) {
        return;
    }
    grounded(world);
}

void Viking::grounded(World& world) {
    if (!on_own_ground(world)) {
        return fall(world);
    }
    keys(world);
}

void Viking::keys(World& world) {
    if (!played(world)) {
        return slow_down(world);
    }
    if ((input(world).held & key::kAction) != 0) {
        if (s.who == kind::kOlaf) {
            if ((input(world).pressed & key::kFOrD) != 0) {
                return turn_shield(world);
            }
        } else if ((input(world).pressed & key::kF) != 0) {
            return s.who == kind::kErik ? jump(world) : swing(world);
        }
        if (s.who == kind::kErik && (input(world).pressed & key::kD) != 0 && speed_x >= kRunning) {
            return charge(world);
        }
        if ((input(world).pressed & key::kS) != 0 && act(world)) {
            return;
        }
    }
    standing_after_acting(world);
}

void Viking::standing_after_acting(World& world) {
    if ((input(world).pressed & key::kE) != 0 && use_item(world)) {
        return;
    }
    standing_after_item(world);
}

void Viking::standing_after_item(World& world) {
    if (s.who == kind::kBaleog && (input(world).held & key::kD) != 0) {
        return draw_bow(world);
    }
    arrow_keys(world);
}

void Viking::arrow_keys(World& world) {
    if ((input(world).held & key::kUp) != 0) {
        if (find_beside(world, *this, Side::kAbove, types().ladder) &&
            find_at(world, *this, x, box.top, types().ladder)) {
            return onto_ladder(world, true);
        }
    } else if ((input(world).held & key::kDown) != 0) {
        if (find_beside(world, *this, Side::kBelow, types().ladder_top) &&
            !find_at(world, *this, x, box.bottom + 1, types().no_way_down)) {
            return onto_ladder(world, false);
        }
    }
    if ((input(world).held & key::kLeft) != 0) {
        return walk(world, true);
    }
    if ((input(world).held & key::kRight) != 0) {
        return walk(world, false);
    }
    slow_down(world);
}

void Viking::walk(World& world, bool left) {
    if (facing_left() != left) {  // his speed is the way he faces
        speed_x = static_cast<std::int16_t>(-speed_x);
    }
    face(world, left);
    if (const auto wall = find_at(world, *this, left ? box.left - 1 : box.right + 1, y, types().solid)) {
        return push(world, *wall);
    }
    const Doing walking = left ? Doing::kWalkingLeft : Doing::kWalkingRight;
    if (s.doing != walking) {
        s.doing = walking;
        s.shield_away = false;
        world.play(*this, s.shield_up ? anim::kOlafWalksShieldUp : looks().walks);
    }
    int top_speed = s.who == kind::kErik ? kErikTopSpeed : kTopSpeed;
    // Remaster rules: a gamepad's stick pushed part way walks him slower (and Erik pushed too little to run can't
    // charge), and his walk's animation slower too, but less: at the square root of his pace (a quarter of his speed,
    // half of his steps), so his legs keep up with the floor (the user: at the same pace he slid ahead of them).
    constexpr int kFull = kFullPace;
    const int pace = std::clamp(int{input(world).pace.across}, 1, kFull);
    if (world.rules == Rules::kRemaster) {
        int steps = 1;  // in 16ths too: the smallest whose square is at least the pace's 16ths of 16
        while (steps * steps < pace * kFull) {
            ++steps;
        }
        sprite.pace = static_cast<std::uint8_t>(steps);
    }
    const bool paced = world.rules == Rules::kRemaster && pace < kFull;
    const int full_top = top_speed;
    top_speed = paced_speed(world, top_speed);
    if (world.rules == Rules::kRemaster) {
        // Pushed further while walking as fast as the pace he had: at the new one's speed at once (the user: it
        // didn't snap in). From standing, or slower, he still speeds up as the original's run does.
        const int walked_at = std::max(int{s.walked_at}, 1);
        const int had = (full_top * walked_at) / kFull;
        if (pace > walked_at && speed_x >= had && had > 0) {
            speed_x = static_cast<std::int16_t>(top_speed);
        }
        s.walked_at = static_cast<std::uint8_t>(pace);
    }
    if (speed_x >= top_speed) {
        speed_x = static_cast<std::int16_t>(top_speed);
    } else if (speed_x >= 0) {
        speed_x += px(0.75);
        if (paced) {
            speed_x = static_cast<std::int16_t>(std::min(int{speed_x}, top_speed));
        }
    } else {  // still going the other way: Erik slows, the others stop at once
        speed_x = static_cast<std::int16_t>(s.who == kind::kErik ? speed_x + px(1.5) : 0);
    }
    stand(world);
}

void Viking::push(World& world, const Found& wall) {
    if (s.doing != Doing::kPushing) {
        world.play(*this, looks().pushes);
        s.doing = Doing::kPushing;
        s.shield_away = s.who == kind::kOlaf;
    }
    speed_x = kPushing;
    if (wall.entity >= 0) {
        Entity* other = world.at(wall.entity);
        if (other != nullptr && (other->hit_mask & hit::kPushable) != 0) {
            target = wall.entity;
            tell(*other, message::kPushed, facing_left() ? -kPushing : kPushing);
        }
    }
    stand(world);
}

void Viking::slow_down(World& world) {
    if (s.who == kind::kBaleog) {
        if (s.doing != Doing::kStanding) {
            speed_x = 0;
            world.play(*this, looks().stops);
            s.doing = Doing::kStanding;
        }
        if (!animation_waits()) {  // left standing, he finds something to do
            world.play(*this, world.random() >= 32767 ? anim::kBaleogFidgets : anim::kBaleogLooksAround);
        }
        return stand(world);
    }
    if (s.who == kind::kOlaf) {
        if (s.doing != Doing::kStanding) {
            s.doing = Doing::kStanding;
            s.shield_away = false;
            speed_x = 0;
            world.play(*this, s.shield_up ? anim::kOlafStopsShieldUp : looks().stops);
        }
        return stand(world);
    }
    if (s.doing != Doing::kStanding) {
        s.doing = Doing::kStanding;
        world.play(*this, speed_x >= kRunning ? looks().skids : looks().stops);
    }
    speed_x = static_cast<std::int16_t>(speed_x > px(4) ? speed_x - px(4) : 0);
    stand(world);
}

// --- In the air ---

void Viking::fall(World& world) {
    s.doing = Doing::kFalling;
    set_falling(true);
    if (s.who == kind::kBaleog) {
        world.play(*this, looks().floats);
    } else if (s.who == kind::kOlaf) {
        s.shield_away = false;
        s.glides_fast = false;
        world.play(*this, s.shield_up ? anim::kOlafGlides : looks().floats);
    }
    s.fall_shown = false;
    s.long_fall_shown = false;
    falling(world);
}

void Viking::falling(World& world) {
    if (s.who == kind::kOlaf) {
        if (!played(world)) {
            return drift(world);
        }
        if ((input(world).pressed & key::kFOrD) != 0) {  // the shield up: he glides down
            s.shield_up = !s.shield_up;
            if (s.shield_up) {
                s.glides_fast = speed_x >= kGlidingFast;
                world.play(*this, s.glides_fast ? anim::kOlafGlidesFast : anim::kOlafGlides);
            } else {
                world.play(*this, looks().floats);
            }
        }
        if ((input(world).pressed & key::kS) != 0 && act(world)) {
            return;
        }
        return falling_after_acting(world);
    }
    if (s.who == kind::kBaleog) {
        if ((speed_y >= kLongFall) != s.long_fall_shown) {
            s.long_fall_shown = !s.long_fall_shown;
            world.play(*this, s.long_fall_shown ? looks().falls_far : looks().floats);
        }
    } else if (speed_y >= kLongFall) {
        if (!s.long_fall_shown) {
            s.long_fall_shown = true;
            s.fall_shown = false;
            world.play(*this, looks().falls_far);
        }
    } else if (speed_y >= kFall) {
        if (!s.fall_shown) {
            s.long_fall_shown = false;
            s.fall_shown = true;
            world.play(*this, looks().falls);
        }
    } else if (speed_y < -kFall) {
        s.fall_shown = false;
        world.play(*this, looks().rises);
    } else {
        world.play(*this, looks().floats);
    }
    if (!played(world)) {
        return drift(world);
    }
    if ((input(world).pressed & key::kS) != 0 && act(world)) {
        return;
    }
    falling_after_acting(world);
}

void Viking::falling_after_acting(World& world) {
    if ((input(world).pressed & key::kE) != 0 && use_item(world)) {
        return;
    }
    falling_after_item(world);
}

void Viking::falling_after_item(World& world) {
    if ((input(world).held & key::kUp) != 0 && find_beside(world, *this, Side::kAbove, types().ladder) &&
        find_at(world, *this, x, box.top, types().ladder)) {
        return onto_ladder(world, true);
    }
    if ((input(world).held & kLeftOrRight) == 0) {
        return drift(world);
    }
    const bool left = (input(world).held & key::kLeft) != 0;
    if (facing_left() != left) {
        speed_x = static_cast<std::int16_t>(-speed_x);
    }
    face(world, left);
    // Remaster rules: steered with a gamepad's stick pushed part way, no faster than he would walk so (what he
    // already had he keeps).
    const int top_speed = paced_speed(world, s.who == kind::kErik ? kErikTopSpeed : kTopSpeed);
    if (speed_x < top_speed) {
        speed_x += (speed_x <= 0 ? px(1.5) : (speed_x < px(1) ? px(1) : px(0.75)));
        if (world.rules == Rules::kRemaster && input(world).pace.across < kFullPace) {
            speed_x = static_cast<std::int16_t>(std::min(int{speed_x}, top_speed));
        }
    }
    fall_on(world);
}

void Viking::drift(World& world) {
    if (speed_x != 0) {
        if (static_cast<std::uint16_t>(speed_x) <= px(0.5)) {
            speed_x = 0;
        } else {
            speed_x += (speed_x > 0 ? -128 : 128);
        }
    }
    fall_on(world);
}

void Viking::fall_on(World& world) {
    if (s.who == kind::kOlaf) {
        if (!s.shield_up) {
            if ((speed_y >= kLongFall) != s.long_fall_shown) {
                s.long_fall_shown = !s.long_fall_shown;
                world.play(*this, s.long_fall_shown ? looks().falls_far : looks().floats);
            }
        } else if ((speed_x >= kGlidingFast) != s.glides_fast) {
            s.glides_fast = !s.glides_fast;
            world.play(*this, s.glides_fast ? anim::kOlafGlidesFast : anim::kOlafGlides);
        }
    }
    // Which way he is going decides the tests: for Erik and Olaf after this tick's gravity, for Baleog (in every
    // world's listing) before it.
    const bool going_down = speed_y > 0;
    world.animate(*this);
    if (s.who == kind::kBaleog ? going_down : speed_y > 0) {
        if (s.who == kind::kOlaf) {
            if (s.shield_up && speed_y >= kGliding) {
                speed_y = kGliding;
            }
            return wait(At::kFalling, {Test::kWall, Test::kLandsAnywhere, Test::kDeadly});
        }
        wait(At::kFalling, {Test::kWall, Test::kLands, Test::kDeadly});
    } else {
        wait(At::kRising, {Test::kWall, Test::kFloorOrCeiling, Test::kDeadly});
    }
}

void Viking::landed(World& world) {
    if (find_beside(world, *this, Side::kBelow, types().spring)) {
        return falling(world);
    }
    if (speed_y >= kLongFall) {
        return land_hard(world);
    }
    if (message != 0 && told(world)) {
        return;
    }
    s.doing = Doing::kOther;
    speed_y = 0;
    set_falling(false);
    if (s.who != kind::kOlaf) {
        world.play(*this, looks().back_on_his_feet);
    }
    stand(world);
}

void Viking::land_hard(World& world) {
    speed_x = speed_y = 0;
    set_falling(false);
    world.shake_y = {.amount = 1, .offset = 0, .ticks = 8};
    const auto my_bit = static_cast<std::uint16_t>(by_who(1, 2, 4));
    if (world.vikings_falling_in != 0) {
        world.vikings_falling_in &= static_cast<std::uint16_t>(~my_bit);
        if (world.database == database::kScenes) {  // the scenes' Vikings make their landing's sound here too
            sound(world, effect::kLandedInScene);
        }
    } else {
        message_value = 2;
        lose_health(world);
        s.returns_to_landing = true;
        return after_landing_hard(world);
    }
    world.play(*this, looks().dizzy);
    dizzy(world);
}

void Viking::after_landing_hard(World& world) {
    if (health == 0) {
        sound(world, effect::kHeavyLanding);
        speed_x = speed_y = 0;
        set_falling(false);
        die(world);
        world.play(*this, looks().dies_from_a_fall);
        return dying_on_the_ground(world);
    }
    sound(world, effect::kLandedHard);
    world.play(*this, looks().dizzy);
    dizzy(world);
}

void Viking::dizzy(World& world) {
    if (!on_ground(world)) {
        set_falling(true);
        return dizzy_in_the_air(world);
    }
    s.doing = Doing::kDizzy;
    keep_on_ground();
    world.animate(*this);
    wait(At::kDizzy, {Test::kWall, Test::kDeadly});
}

void Viking::dizzy_in_the_air(World& world) {
    world.animate(*this);
    if (speed_y > 0) {
        s.doing = Doing::kDizzyFalling;
        wait(At::kDizzyInTheAir, {Test::kWall, Test::kLands, Test::kDeadly});
    } else {
        s.doing = Doing::kDizzyRising;
        wait(At::kDizzyInTheAir, {Test::kWall, Test::kFloorOrCeiling, Test::kDeadly});
    }
}

void Viking::dizzy_after_acting(World& world) {
    if ((input(world).pressed & key::kE) != 0 && use_item(world)) {
        return;
    }
    dizzy_after_item(world);
}

void Viking::dizzy_after_item(World& world) {
    // A quirk of the original: in the air it jumps to what he is told instead of calling it, and what ends
    // with a return there (something he takes no notice of) comes back through whatever he last called. After
    // a hard landing with nothing since, that is the landing's loss of health: its sound and his stars start
    // again, without the loss. Any other call is taken here as coming straight back.
    if (message != 0) {
        if (told_while_dizzy(world)) {
            return;
        }
        if (s.doing != Doing::kDizzy && s.returns_to_landing) {
            return after_landing_hard(world);
        }
    }
    if (!animation_waits()) {  // the stars are gone
        message = 0;
        if (is_falling()) {
            s.doing = Doing::kDazedInTheAir;
        }
        return carry_on(world);
    }
    if (s.doing == Doing::kDizzy) {
        return dizzy(world);
    }
    dizzy_in_the_air(world);
}

// --- Ladders ---

void Viking::onto_ladder(World& world, bool up) {
    s.doing = Doing::kClimbing;
    s.climbs = 0;  // (no step of the climb yet)
    s.shield_away = s.who == kind::kOlaf;
    speed_x = speed_y = 0;
    set_falling(false);
    const std::uint8_t beside = type_at(world, x + kCellSize, up ? box.top - 1 : box.bottom + 1);
    const bool ladder_to_the_right =
        up ? (beside == tile::kLadder || beside == tile::kLadderTop || beside == tile::kLadder0D)
           : beside == tile::kLadderTop;
    const int middle = (static_cast<int>(static_cast<std::uint16_t>(x) & 0xfff0U)) + (ladder_to_the_right ? 16 : 0);
    const int feet = static_cast<int>(static_cast<std::uint16_t>(box.bottom) | 7U);
    push_x = static_cast<std::int16_t>(static_cast<std::uint16_t>((middle - x) * 256));
    push_y = static_cast<std::int16_t>(static_cast<std::uint16_t>((feet - box.bottom) * 256));
    world.play(*this, anim::kOntoLadder);
    world.animate(*this);
    s.after_step = up ? looks().onto_ladder_up : looks().onto_ladder_down;
    wait(At::kOntoLadder);
}

// Remaster rules: a climb's step moves him as its animation goes on, so it goes on as fast as a gamepad's stick
// is pushed up or down (and his hands and feet keep up), from tick to tick as the stick moves. A step the way the
// stick isn't pushed (now the other way, or let go) ends at full pace (the user: a slow step was sat out first).
void Viking::pace_climb(World& world) {
    if (world.rules != Rules::kRemaster) {
        return;
    }
    const int way = (input(world).held & key::kUp) != 0 ? 1 : (input(world).held & key::kDown) != 0 ? -1 : 0;
    sprite.pace =
        way != 0 && way == s.climbs ? std::clamp(input(world).pace.up_down, std::uint8_t{1}, kFullPace) : kFullPace;
}

int Viking::paced_speed(const World& world, int top_speed) const {
    if (world.rules != Rules::kRemaster || input(world).pace.across >= kFullPace) {
        return top_speed;
    }
    return (top_speed * std::max(int{input(world).pace.across}, 1)) / kFullPace;
}

void Viking::climb(World& world) {
    world.animate(*this);
    wait(At::kClimbing, {Test::kFloorOrCeiling, Test::kDeadly});
}

void Viking::climbing(World& world) {
    pace_climb(world);
    if (animation_waits()) {
        return climb(world);
    }
    const VikingLooks& look = looks();
    const std::uint16_t frame = sprite.packed_frame;
    if (s.who == kind::kErik && (input(world).pressed & key::kF) != 0) {
        return jump(world);
    }
    if ((input(world).held & key::kUp) != 0) {
        if (frame == look.off_frame) {
            return stepped_off(world);
        }
        if (frame == look.nearly_off_frame) {
            world.play(*this, look.steps_off_more);
        } else if (type_at(world, x, box.bottom) == tile::kLadderTop) {  // his feet at the ladder's top
            world.play(*this, look.steps_off);
        } else {
            world.play(*this, next_step(look.climbs_up_from, look.climbs_up));
        }
        s.climbs = 1;
        pace_climb(world);
        return climb(world);
    }
    if ((input(world).held & key::kDown) != 0) {
        if (type_at(world, x, box.bottom + 1) != tile::kLadderTop) {
            if (find_at(world, *this, x, box.bottom + 1, types().landing)) {
                return landed(world);
            }
            if (!find_at(world, *this, x, box.bottom, types().ladder) &&
                !find_at(world, *this, x, box.top + 4, types().ladder)) {
                return fall(world);
            }
        }
        if (frame == look.off_frame) {
            world.play(*this, look.climbs_back_down);
        } else if (frame == look.nearly_off_frame) {
            world.play(*this, look.climbs_back_down_nearly_off);
        } else {
            world.play(*this, next_step(look.climbs_down_from, look.climbs_down));
        }
        s.climbs = -1;
        pace_climb(world);
        return climb(world);
    }
    if ((input(world).held & kLeftOrRight) != 0) {
        if (frame == look.off_frame) {
            return stepped_off(world);
        }
        if (frame == look.nearly_off_frame) {
            world.play(*this, look.steps_off_more);
            return climb(world);
        }
        return fall(world);
    }
    climb(world);
}

Animation Viking::next_step(const std::array<Animation, 3>& from, Animation otherwise) const {
    for (std::size_t i = 0; i < from.size(); ++i) {
        if (sprite.packed_frame == looks().rung_frames.at(i)) {
            return from.at(i);
        }
    }
    return otherwise;
}

void Viking::stepped_off(World& world) {
    s.doing = Doing::kOther;
    world.play(*this, s.shield_up ? anim::kOlafSteppedOffShieldUp : looks().stepped_off);
    stand(world);
}

namespace {

// Olaf's shield: an entity of its own that goes where he goes. In front of him it stops what comes at him; held
// up, others can stand on it. While he can't hold it (climbing, dead) it is nothing to anyone.
class Shield : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            stay_active();
            return hold(false);
        }
        target = creator;
        const auto* olaf = creator == kNobody ? nullptr : dynamic_cast<const Viking*>(world.at(creator));
        const bool olaf_is_gone = olaf == nullptr || olaf->health == 0;
        if (olaf_is_gone && !world.title.cheat) {
            world.remove(slot);
            return;
        }
        if (olaf_is_gone && !s.watching) {  // with the cheat on it stays, put away, to watch the keys
            put_away();
            s.watching = true;
            return;
        }
        if (world.title.cheat && level_keys(world)) {
            return;
        }
        if (olaf_is_gone) {
            return;
        }
        s.watching = false;
        moved_by = creator;
        push_x = push_y = 0;
        if (olaf->shield_away()) {
            put_away();
        } else if (s.away || s.up != olaf->shield_up()) {
            hold(olaf->shield_up());
        }
        if (message != 0) {  // what hits the shield, Olaf is told
            Entity* bearer = world.at(creator);
            tell(*bearer, message, message_value);
            message = 0;
            message_value = 0;
        }
    }

  private:
    void put_away() {
        s.away = true;
        acts_as = tile::kBody;
        hit_class = 0;
        hit_mask = 0;
    }
    // The cheat's other level keys (the title's password turns them on, like F5 and F6): F, D, E and S held
    // together, and nothing else, end the level for the next one; with Left as well, for the one before. Not
    // from the last level on, nor back from the first. A Viking without health gets his full health first.
    // True if the level ends.
    static bool level_keys(World& world) {
        constexpr int kLastLevel = 36;
        if (world.host().held == key::kAction) {
            if (world.scene == kLastLevel) {
                return false;
            }
        } else if (world.host().held == (key::kAction | key::kLeft)) {
            if (world.scene == 0) {
                return false;
            }
            world.next_scene = world.scene - 1;
        } else {
            return false;
        }
        for (int viking = 0; viking < kVikingSlots; ++viking) {
            if (Entity* entity = world.at(viking); entity != nullptr && entity->health == 0) {
                entity->health = kFullHealth;
            }
        }
        world.level_done = true;
        return true;
    }
    void hold(bool up) {
        s.away = false;
        s.up = up;
        if (up) {  // a platform over his head
            acts_as = tile::kShieldOverhead;
            hit_class = hit_class::kShieldOverhead;
            hit_mask = hit::kShieldOverhead;
            box = box_of(x - 13, y - 18, x + 13, y - 13);
        } else {  // a wall before him
            acts_as = tile::kShield;
            hit_class = hit_class::kViking;
            hit_mask = hit::kShield;
            box = box_of(facing_left() ? x - 18 : x + 2, y - 16, facing_left() ? x - 2 : x + 18, y + 16);
        }
    }

    struct Saved {
        bool away = false;
        bool up = false;
        bool watching = false;  // Olaf is gone and the cheat is on: it only watches the keys
    } s;
    LVR_SAVES(Entity)
};

}  // namespace

std::unique_ptr<Entity> make_shield() {
    return make_entity<Shield>();
}

bool olaf_shield_away(const World& world) {
    const auto* olaf = dynamic_cast<const Viking*>(world.at(2));
    return olaf != nullptr && olaf->shield_away();
}

bool olaf_holds_shield_up(const World& world) {
    const auto* olaf = dynamic_cast<const Viking*>(world.at(2));
    return olaf != nullptr && olaf->shield_up();
}

std::unique_ptr<Entity> make_viking(std::uint16_t kind) {
    return make_entity<Viking>(kind);
}

}  // namespace lvr
