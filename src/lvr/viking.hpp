#pragma once

// The Vikings: Erik, Baleog and Olaf (docs/original/vikings.md). What the three share is here once: standing,
// walking, falling and landing, ladders, acting on things, getting hurt and dying. Each one's abilities are his own.
//
// A Viking's turn ends where he waits for the next tick (`At`). What stops him after the tick's move, and who
// touches him, is tested later in the tick (`Test`) and acted on when his next turn starts.
//
// The class is declared here and written in five files, by what the code is about: vikings.cpp (the turn, standing
// and walking, in the air, ladders), viking_abilities.cpp (Erik's, Olaf's and Baleog's own), viking_things.cpp
// (acting on things, items), viking_harm.cpp (what he is told, dying), viking_worlds.cpp (what only some worlds
// have: the balloon, the cranes, the scenes).

#include <array>
#include <cstdint>

#include "lvr/hits.hpp"
#include "lvr/kinds.hpp"
#include "lvr/shared.hpp"
#include "lvr/sprites.hpp"
#include "lvr/terrain.hpp"
#include "lvr/viking_looks.hpp"
#include "lvr/world.hpp"

namespace lvr {

inline constexpr std::uint16_t kVikingMaskBits =
    hit::kViking | hit::kVikingInField;            // what a Viking's hit mask loses while nothing can hit him
inline constexpr std::uint16_t kCameraAtRest = 4;  // the camera moved less than this in the last tick
inline constexpr std::uint16_t kShieldArgument = 15;
inline constexpr std::uint16_t kLeftOrRight = key::kLeft | key::kRight;

inline constexpr std::uint16_t kArmour = 2;  // the shield item's extra point
// Speeds, in 256ths of a pixel a tick.
inline constexpr int kSinkingSpeed = px(1);
inline constexpr int kLongFall = px(16);  // falling this fast shows the long fall, and the landing is hard
inline constexpr int kFall = px(3);
inline constexpr int kRunning = px(6);  // from here Erik can charge, and a Viking skids to a stop
inline constexpr int kKnockedBack = px(2);
inline constexpr int kPushing = px(2);

// Tile types, by what they are to a Viking.
struct Types {
    TileTypes solid = tile_types({tile::kGround, tile::kGround02, tile::kGround20});
    TileTypes breakable = tile_types({tile::kGround02, tile::kGround20, tile::kBreakable21, tile::kBreakable22});
    TileTypes ground = tile_types({tile::kGround, tile::kGround02, tile::kLadderTop, tile::kPlatform,
                                   tile::kShieldOverhead, tile::kGround20}) |
                       slopes();  // he stands on
    TileTypes landing =
        tile_types({tile::kGround, tile::kGround02, tile::kLadderTop, tile::kPlatform, tile::kGround20}) | slopes();
    TileTypes ladder = tile_types({tile::kLadder, tile::kLadderTop, tile::kLadder0D});
    TileTypes ladder_top = tile_types({tile::kLadderTop});
    TileTypes no_way_down = tile_types({tile::kFree, tile::kGround, tile::kPlatform}) | slopes();
    TileTypes deadly = tile_types({tile::kWater, tile::kLava11, tile::kLava, tile::kQuicksand, tile::kSpikes,
                                   tile::kElectricity, tile::kSpring, tile::kRotatingSpikes});
    TileTypes water = tile_types({tile::kWater});
    TileTypes lava = tile_types({tile::kLava11, tile::kLava});
    TileTypes quicksand = tile_types({tile::kQuicksand});
    TileTypes spikes = tile_types({tile::kSpikes});
    TileTypes electric = tile_types({tile::kElectricity});
    TileTypes spring = tile_types({tile::kSpring});
    TileTypes rotating_spikes = tile_types({tile::kRotatingSpikes});
    TileTypes thing = tile_types({tile::kThing});  // what a Viking can act on: a switch, a hint ...
    TileTypes lock = tile_types({tile::kLock});
    TileTypes taker = tile_types({tile::kTaker});  // what takes one of the things a level asks for
};

inline const Types& types() {
    static const Types all;
    return all;
}

class Viking : public Entity {
  public:
    explicit Viking(std::uint16_t who) { s.who = who; }
    void update(World& world) override { resume(world); }
    // The terrain tests of the place he waits at, in their order; each may push him back.
    void after_move(World& world) override;
    void check_hits(World& world) override;
    // The Vikings are never told they left the active area (World::mark_outside_active_area passes them by); this
    // is his turn when the scene ends, as every entity has it then: a living one is gone before it fades out.
    void outside_active_area(World& world) override;
    // Olaf's shield asks: held up, or not held at all.
    bool shield_up() const noexcept { return s.shield_up; }
    bool shield_away() const noexcept { return s.shield_away; }

  private:
    // Where his turn ended.
    enum class At : std::uint8_t {
        kStart,
        kStarted,
        kAboutToFallIn,
        kFallingIn,
        kStanding,
        kRising,
        kFalling,
        kOntoLadder,
        kClimbing,
        kArriving,
        kUsingKey,
        kHandingOver,
        kFadingAway,
        kTeleported,
        kBeamed,
        kAppearing,
        kActing,
        kActed,
        kHurt,
        kDizzy,
        kDizzyInTheAir,
        kDying,
        kSinking,
        kDyingOnTheGround,
        kDyingInTheAir,
        kCrushed,
        kDead,
        // Erik
        kCharging,
        kHeadHit,
        kBouncing,
        kBouncingDown,
        kGettingUp,
        kStoppingCharge,
        // Baleog
        kDrawingBow,
        kAiming,
        kLoosing,
        kSwinging,
        kSwingingOn,
        // Olaf
        kHanging,
        kBlownBack,
        // the factory
        kTakingControls,
        kAtControls,
        // Egypt
        kBecomingAMummy,
        kBecameAMummy,
        // the scenes outside the levels
        kLedWalking,
        kLedStanding,
        kCheering,
        // the wacky world
        kInflating,
        kInflated,
    };
    // What he is doing, as far as the ways back into it need to know (after acting, after being hurt ...).
    enum class Doing : std::uint8_t {
        kStanding,
        kWalkingLeft,
        kWalkingRight,
        kPushing,
        kClimbing,
        kFalling,
        kTeleporting,
        kHanging,
        kDazedInTheAir,
        kHurtFalling,
        kHurt,
        kDizzy,
        kDizzyRising,
        kDizzyFalling,
        kCharging,
        kHeadHit,
        kBouncing,
        kBouncingAgain,
        kGettingUp,
        kStoppingCharge,
        kOther,
    };
    enum class Test : std::uint8_t {
        kNone,
        kWall,            // stopped by a wall he moved into
        kBreakableWall,   // the same, a wall Erik's head breaks
        kFloorOrCeiling,  // stopped going up or down
        kDeadly,          // moved up or down into something deadly
        kLands,           // landed, on what he can stand on
        kLandsAnywhere,
        kTouchesClass,  // something of the hit class he looks for (s.touch_class)
        kTouchesEnemy,  // someone Erik's charge hits
        kTouchesThing,  // something that answers when acted on
    };
    const VikingLooks& looks() const;
    void wait(At at, std::array<Test, 4> tests = {});
    // What a test of the place he waits at found, by the test's name; false for one this place doesn't make.
    bool found(Test test) const;
    bool landed() const { return found(Test::kLands) || found(Test::kLandsAnywhere); }
    bool animation_waits() const { return !animation_over(); }
    void sound(World& world, std::uint8_t effect) const { world.sound(*this, effect); }
    void set_falling(bool on) { set_flag(entity_flag::kFalls, on); }
    void keep_on_ground() { set_flag(entity_flag::kPutOnGround); }
    void face(World& world, bool left);
    bool played(const World& world) const { return world.is_played(slot); }
    // The keys he goes by (ADR-0012): his player's, or those of the last one who played him (as the original's
    // Vikings not being played read its one keyboard in some of their states).
    const Player& input(const World& world) const { return world.keys_of(slot); }
    bool on_ground(const World& world) const;
    // Where each one's own behaviour asks: Olaf doesn't stand on a shield held up.
    bool on_own_ground(const World& world) const;
    std::uint8_t type_at(const World& world, int at_x, int at_y) const;
    std::uint16_t by_who(std::uint16_t erik, std::uint16_t baleog, std::uint16_t olaf) const;
    const Animation& by_who(const anim::ByViking& each) const;
    void hide_pieces(World& world) const;
    void show_pieces(World& world) const;
    // NOLINTNEXTLINE(readability-function-cognitive-complexity): one case per place a turn ends
    void resume(World& world);
    void resume_moving(World& world);    // vikings.cpp
    void resume_acting(World& world);    // viking_things.cpp
    void resume_in_world(World& world);  // viking_worlds.cpp
    void resume_harmed(World& world);    // viking_harm.cpp
    void resume_ability(World& world);   // viking_abilities.cpp
    void start(World& world);
    // The Vikings come into the level falling: down until he lands, hard, but unhurt.
    void fall_in(World& world);
    void stand(World& world);
    void standing(World& world);
    void grounded(World& world);
    // What the player asks of him.
    void keys(World& world);
    void standing_after_acting(World& world);
    void standing_after_item(World& world);
    void arrow_keys(World& world);
    void walk(World& world, bool left);
    // Against a wall, or something that gives way.
    void push(World& world, const Found& wall);
    void slow_down(World& world);
    void fall(World& world);
    void falling(World& world);
    void falling_after_acting(World& world);
    void falling_after_item(World& world);
    // No key steers him: his speed across wears off.
    void drift(World& world);
    void fall_on(World& world);
    void landed(World& world);
    // After a long fall: the screen shakes, he loses a point of health and sees stars; with none left he dies.
    void land_hard(World& world);
    void after_landing_hard(World& world);
    void dizzy(World& world);
    void dizzy_in_the_air(World& world);
    void dizzy_after_acting(World& world);
    void dizzy_after_item(World& world);
    // The step sideways to the ladder's middle: its two cells meet there.
    void onto_ladder(World& world, bool up);
    void climb(World& world);
    void pace_climb(World& world);
    // His top speed across as a gamepad's stick is pushed (Remaster rules), walking or steered in the air.
    int paced_speed(const World& world, int top_speed) const;
    // A climbing step has ended: the next one, by the keys.
    void climbing(World& world);
    Animation next_step(const std::array<Animation, 3>& from, Animation otherwise) const;
    void stepped_off(World& world);
    // The S key: something beside him, or where he stands, that can be acted on. True if his turn ended.
    bool act(World& world);
    void act_on(World& world);
    // He touches it: it is told he asks.
    void ask(World& world);
    // At a pump he is blown up: for 100 ticks he floats, steered left and right, rising slowly. F or D lets the
    // air out; when it runs out he wobbles first. A blow lets it out too; something deadly pops him.
    void use_pump(World& world, Entity& pump);
    void inflated_float(World& world);
    void let_air_out(World& world);
    void inflated_slow_down();
    // Left and right push him along; without them he slows down.
    void inflated_steer(World& world);
    void inflated_keys(World& world, bool also_act);
    void pop(World& world);
    // Something deadly above or below pops him. True if it did.
    bool inflated_deadly(World& world);
    // What he is told while blown up. True if his turn ended.
    bool told_inflated(World& world);
    void inflated_turn(World& world);
    // At a crane's controls (the factory): he stands at them and the keys drive the crane: left and right, F
    // turns its magnet on and off. D or S lets go of them, and the magnet with it.
    void take_controls(World& world);
    void at_controls(World& world, bool first);
    // Into a teleporter: he fades where he stands, is put where it leads, waits there for the view to catch up
    // (if he is the one being played), and appears. Nothing hits him meanwhile.
    void step_into(World& world, const Entity& teleporter);
    void fade_away(World& world);
    void appear(World& world);
    // Back to what he was doing, after the S key's part of it.
    void after_acting(World& world);
    // Back to what he was doing, after the E key's part of it.
    void after_item(World& world);
    std::uint16_t& chosen_item(World& world) const;
    // The item is used up; the next square that holds something is chosen.
    void used_up(World& world) const;
    // The E key: the item in his chosen square. True if his turn ended.
    bool use_item(World& world);
    bool use_key(World& world, std::uint16_t lock_class);
    // What he reached with the item is told; the item is gone.
    void hand_over(World& world);
    // True if his turn ended (or he is gone).
    bool told(World& world);
    // Each world's Vikings take their own set of these: Egypt's know nothing of electricity or of the ground going
    // from under them, the wacky world's nothing of the latter; the drop and the mummy's grip (9 and 0B) are
    // unknown to the spaceship's and the wacky world's.
    bool told_while_hurt(World& world);
    bool told_while_dizzy(World& world);
    void lose_health(World& world);
    // Hit: he loses the health it says and is knocked back, away from where it came from.
    void hurt(World& world);
    void being_hurt(World& world);
    void hurt_after_acting(World& world);
    void hurt_after_item(World& world);
    // Back to what he was doing, after something took him out of it.
    void carry_on(World& world);
    // Told to walk on: to the right, until told to stand or to cheer.
    void led_walk(World& world);
    void led_walking(World& world);
    void led_stand(World& world);
    void led_standing(World& world);
    // The scene's end: he cheers, and nothing touches him any more.
    void cheer(World& world);
    void cheering(World& world);
    void arriving(World& world);
    void die(World& world);
    // Something deadly above or below. True if it killed him.
    bool deadly(World& world);
    void die_where_he_is(World& world, bool drowned);
    void squashed(World& world);
    // Pushed (message_value: how, and which way) against a wall. True if it killed him.
    bool crushed(World& world);
    // Its animation waits four times.
    void being_crushed(World& world);
    // In quicksand: slowly down until it closes over him.
    void sink(World& world);
    void dying(World& world);
    // Wrapped up where he stands (or as he falls); then a mummy walks off in his place.
    void becoming_a_mummy(World& world);
    void dying_on_the_ground(World& world);
    void dying_in_the_air(World& world);
    // He stays where he died, unseen. What he carried is gone with him.
    void dead(World& world);
    // The faster he runs, the higher he jumps.
    void jump(World& world);
    void charge(World& world);
    void charging(World& world);
    void charging_after_acting(World& world);
    void charging_after_item(World& world);
    void head_hits_enemy(World& world);
    void head_hits_wall(World& world, bool breakable);
    void head_hits(World& world);
    void head_hit(World& world);
    void head_hit_after_acting(World& world);
    void head_hit_after_item(World& world);
    void bounce(World& world);
    void bouncing_after_acting(World& world);
    void bouncing_after_item(World& world) { bounce(world); }
    // He lands on his back and bounces once more.
    void bounce_again(World& world);
    void get_up(World& world);
    void on_his_feet(World& world);
    void on_his_feet_after_acting(World& world);
    void on_his_feet_after_item(World& world) { on_his_feet(world); }
    // Before him, or over his head: the F and D keys change between the two. The shield itself is an entity that
    // looks at shield_up() and shield_away().
    void turn_shield(World& world);
    void lower_shield(const World& world);
    // Held in the air by something: he can turn, and raise or lower his shield to get out of it.
    void hang(World& world);
    void hanging(World& world);
    void hanging_after_item(World& world);
    // One of his two swings. The sword is an entity of its own, in front of him for as long as it can hit.
    void swing(World& world);
    // The bow is drawn for as long as the key is held; let go, the arrow flies.
    void draw_bow(World& world);
    void baleog_waits(World& world, At at);
    void baleog_goes_on(World& world);
    struct Saved {
        std::uint16_t who;
        At at = At::kStart;
        Doing doing = Doing::kStanding;
        std::array<Test, 4> tests{};
        std::array<bool, 4> hit{};
        int touched = kNobody;
        bool fall_shown = false;  // which animation of falling is playing
        bool long_fall_shown = false;
        bool returns_to_landing = false;  // dizzy from a hard landing, and nothing called since (see dizzy_after_item)
        Animation after_step{};           // the animation that follows the step onto a ladder
        int crush_parts_left = 0;
        TeleporterDestination teleports_to;
        bool beamed = false;              // told to go to World::beam (the ship's last level), not by a teleporter
        std::uint16_t touch_class = 0;    // what Test::kTouchesClass looks for
        bool hit_from_the_front = false;  // the last blow: he is knocked back; from behind, forward
        std::size_t swing = 0;            // Baleog: which of his two swings
        bool has_fire_arrows = false;     // Baleog used the fire arrows item
        bool shield_up = false;           // Olaf: over his head, not before him
        bool warned = false;              // blown up: the air is running out
        bool deflating = false;           // ... or is on its way out
        std::uint16_t crane = 0;          // at a crane's controls: which crane
        bool shield_away = false;         // he can't hold it now: climbing, pushing, dead
        bool glides_fast = false;         // which animation of gliding is playing
        // Remaster rules' stick (World::pace): the pace he last walked at, and which way the step of the climb
        // being played goes (1 up, -1 down, 0 none).
        std::uint8_t walked_at = kFullPace;
        std::int8_t climbs = 0;
    } s;
    LVR_SAVES(Entity)
};

}  // namespace lvr
