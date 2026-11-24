// Tomator, the last level's: his body with its three attached parts, his force field, what he shoots, and the
// air lock that ends the fight; and the level's electric bars and sliding walls. Read from the spaceship's object
// database (449).

#include <array>
#include <cstdint>
#include <memory>

#include "lvr/enemies.hpp"
#include "lvr/shared.hpp"

namespace lvr {

namespace {

// What Tomator's three attached parts show with him (Entity::pose): he sets it, each part follows.
namespace tomator_pose {
constexpr std::uint16_t kStands = 0;
constexpr std::uint16_t kBlownOut = 1;
constexpr std::uint16_t kAims = 2;
constexpr std::uint16_t kKnocked = 3;
constexpr std::uint16_t kReels = 4;
constexpr std::uint16_t kHidden = 5;  // the parts are not drawn
constexpr std::uint16_t kShown = 6;   // drawn again; then they stand
constexpr std::uint16_t kWalks = 7;
constexpr std::uint16_t kComesBack = 8;
constexpr std::uint16_t kVanishes = 9;
}  // namespace tomator_pose

const TileTypes& wall() {
    static const TileTypes types = tile_types({tile::kGround});
    return types;
}

class Tomator : public Entity {
  public:
    void update(World& world) override {
        switch (at_) {
            case At::kStart:
                return at_start(world);
            case At::kOpening:
                talk(world);
                at_ = At::kAfterTalk;
                return;
            case At::kAfterTalk:
                at_ = At::kAfterTalkAgain;
                return;
            case At::kAfterTalkAgain:
                stage = 1;
                return vanish(world, Then::kFirstPlace);
            case At::kVanishing:
                return when_vanishing(world);
            case At::kAppearing:
                if (!animation_over()) {
                    world.animate(*this);
                    return;
                }
                return arrived(world);
            case At::kPause:
                return shield_or_go_on(world);
            case At::kWaiting:
            case At::kWaitingOnce:
            case At::kWaitingShielded:
            case At::kAnimating:
                return when_waiting(world);
            case At::kWalking:
                return when_walking(world);
            case At::kIdle:
                return when_idle(world);
            case At::kLoweringField:
                return when_lowering_field(world);
            case At::kFalling:
                return when_falling(world);
            case At::kSettling:
                return vanish(world, Then::kThirdStage);
            case At::kStunned:
                return when_stunned(world);
            case At::kKnocked:
                return when_knocked(world);
            case At::kKnockedOver:
                return when_knocked_over(world);
            case At::kBlasted:
                return when_blasted(world);
            case At::kSuckedUp:
                return when_sucked_up(world);
            case At::kDefeated:
                return when_defeated(world);
        }
    }
    void at_start(World& world) {
        world.play(*this, anim::kTomatorAppears);
        world.animate(*this);
        acts_as = tile::kEnemy;
        count_down();
        world.tomator.force_field = kNobody;
        stand_still(world);
        world.animate(*this);
        at_ = At::kOpening;
    }
    void when_vanishing(World& world) {
        if (!animation_over()) {
            world.animate(*this);
            return;
        }
        arrive(world);
        world.play(*this, anim::kTomatorComesBack);
        pose = tomator_pose::kComesBack;
        at_ = At::kAppearing;
        world.animate(*this);
    }
    void when_waiting(World& world) {
        if (guarded(world)) {
            return;
        }
        if (at_ == At::kWaitingShielded && world.tomator.force_field == kNobody) {
            raise_force_field(world);
        }
        if (at_ == At::kAnimating ? !animation_over() : (at_ != At::kWaitingOnce && countdown != 0)) {
            world.animate(*this);
            return;
        }
        return go_on(world);
    }
    void when_walking(World& world) {
        if (guarded(world)) {
            return;
        }
        if (find_beside(world, *this, Side::kAhead, walls())) {
            world.flip_across(*this);
            speed_x = 0;
            return fourth_stage(world);
        }
        world.animate(*this);
    }
    void when_idle(World& world) {
        touch(world);
        if (!find_beside(world, *this, Side::kBelow, solid())) {
            return fall(world);
        }
        world.animate(*this);
    }
    void when_lowering_field(World& world) {
        touch(world);
        if (world.tomator.force_field != kNobody) {
            world.animate(*this);
            return;
        }
        raise_force_field(world);
        return back_to_stage(world);
    }
    void when_falling(World& world) {
        if (saw.landed) {
            return landed(world);
        }
        touch(world);
        if (message != 0 && told(world, true)) {
            return;
        }
        world.animate(*this);
    }
    void when_stunned(World& world) {
        touch(world);
        if (!find_beside(world, *this, Side::kBelow, solid())) {
            return fall(world);
        }
        if (message != 0 && told(world, false)) {
            return;
        }
        if (countdown != 0) {
            world.animate(*this);
            return;
        }
        message = 0;
        return back_to_stage(world);
    }
    void when_knocked(World& world) {
        if (saw.wall) {
            speed_x = 0;
        }
        touch(world);
        if (saw.other != kNobody) {
            return blasted(world);
        }
        if (saw.another != kNobody && sucked_up(world)) {
            return;
        }
        if (!find_beside(world, *this, Side::kBelow, solid())) {
            return fall(world);
        }
        if (animation_over()) {
            speed_x = 0;
            at_ = At::kKnockedOver;
        }
        world.animate(*this);
    }
    void when_knocked_over(World& world) {
        touch(world);
        if (!find_beside(world, *this, Side::kBelow, solid())) {
            return fall(world);
        }
        countdown = 60;
        return back_to_stage(world);
    }
    void when_blasted(World& world) {
        touch(world);
        if (countdown == 0) {
            return after_volley_of_four(world);
        }
        world.animate(*this);
    }
    void when_sucked_up(World& world) {
        if (saw.ceiling) {
            speed_y = 0;
            return vanish(world, Then::kBackInTheMiddle);
        }
        speed_y -= px(0.125);
        world.animate(*this);
    }
    void when_defeated(World& world) {
        if (countdown != 0) {
            world.animate(*this);
            return;
        }
        for (int viking = 0; viking < kVikingSlots; ++viking) {
            target = viking;
            const Entity* entity = world.at(viking);
            if (entity != nullptr && entity->health == 0) {
                world.game_over = true;
                return;
            }
        }
        world.level_done = true;
    }
    Looks looks_for() const override {
        const Look viking = touching_one_of_the_vikings_class();
        switch (at_) {
            case At::kWaiting:
            case At::kWaitingOnce:
            case At::kWaitingShielded:
            case At::kAnimating:
            case At::kWalking:
            case At::kIdle:
            case At::kLoweringField:
            case At::kStunned:
            case At::kKnockedOver:
            case At::kBlasted:
                return {viking};
            case At::kFalling:
                return {lands_on(stage == 2 ? solid() : wall()), viking};
            case At::kSettling:
                return {lands_on(ground())};
            case At::kKnocked:  // thrown back: into a wall, onto his own bomb, or into the air lock
                return {stopped_by(wall()), viking, touching(hit_class::kTomatorsBomb, &Saw::other),
                        touching(hit_class::kAirStream, &Saw::another)};
            case At::kSuckedUp:
                return {stopped_above_or_below(wall())};
            default:
                return {};
        }
    }
    void outside_active_area(World& world) override { world.remove(slot); }

  private:
    enum class At : std::uint8_t {
        kStart,
        kOpening,
        kAfterTalk,
        kAfterTalkAgain,
        kVanishing,
        kAppearing,
        kPause,
        kWaiting,          // for his countdown
        kWaitingOnce,      // for a tick
        kWaitingShielded,  // for his countdown; his force field comes back if it is gone
        kAnimating,        // for his animation's end
        kWalking,
        kIdle,
        kLoweringField,
        kFalling,
        kSettling,
        kStunned,
        kKnocked,
        kKnockedOver,
        kBlasted,
        kSuckedUp,
        kDefeated,
    };
    // Where he comes back after vanishing, and what he does there.
    enum class Then : std::uint8_t { kFirstPlace, kThirdStage, kFourthStage, kFifthStage, kBackInTheMiddle };
    // What follows a wait.
    enum class Next : std::uint8_t {
        kAim,
        kFire,
        kShoot,
        kFirstVolley,
        kLongVolley,
        kThirdVolley,
        kFourthVolley,
        kFifthVolley,
        kAfterFour,
        kFifthStage
    };
    // A volley: how many shots, how long between them, whether he turns to Olaf for each.
    struct Volley {
        std::uint16_t until;
        std::uint16_t pause;
        bool faces_olaf;
    };

    void stand_still(World& world) {
        world.play(*this, anim::kTomatorStands);
        pose = tomator_pose::kStands;
    }

    void his_line(World& world, int text) const {
        world.say_at_point(text, tail::kAboveRight, x, y + 36);
        world.wait_for_key();
        world.clear_text();
    }
    void viking_line(World& world, int viking, int text, int anchor, std::uint16_t colour) const {
        const Entity* speaker = world.at(viking);
        if (speaker == nullptr) {
            return;
        }
        world.speak_at_point(colour, text, anchor, speaker->x, speaker->y - 28);
    }

    // His words with the Vikings before the fight; after a game over only his last line.
    void talk(World& world) {
        world.sound(*this, effect::kTalk);
        if (!world.after_game_over) {
            world.colour_boxes(text_colour::kOthers);
            his_line(world, 0x111);
            his_line(world, 0x112);
            target = slot::kOlaf;
            viking_line(world, slot::kOlaf, 0x113, tail::kBelowLeft, text_colour::kOlaf);
            world.colour_boxes(text_colour::kOthers);
            his_line(world, 0x114);
            his_line(world, 0x115);
            target = slot::kErik;
            viking_line(world, slot::kErik, 0x116, tail::kBelowMiddle, text_colour::kErik);
            world.colour_boxes(text_colour::kOthers);
            world.say_at_point(0x117, tail::kAboveRight, x, y + 36);
            world.wait_for_key();
        }
        world.colour_boxes(text_colour::kOthers);
        world.clear_text();
        his_line(world, 0x118);
    }

    // --- Vanishing and coming back somewhere else ---

    void vanish(World& world, Then then) {
        then_ = then;
        world.play(*this, anim::kTomatorVanishes);
        pose = tomator_pose::kVanishes;
        at_ = At::kVanishing;
        world.animate(*this);
    }

    void move_to(int to_x, int to_y) {
        push_x = static_cast<std::int16_t>(to_x - x);
        push_y = static_cast<std::int16_t>(to_y - y);
        moved_by = kMovedFreely;
    }

    void arrive(World& world) {
        switch (then_) {
            case Then::kFirstPlace:
                return move_to(616, 120);
            case Then::kThirdStage:
                return move_to(1088, 488);
            case Then::kFourthStage:
                return move_to(1144, 824);
            case Then::kFifthStage:
                move_to(1512, 520);
                if (!facing_left()) {
                    world.flip_across(*this);
                }
                return;
            case Then::kBackInTheMiddle:
                move_to(1328, 152);
                if (facing_left()) {
                    world.flip_across(*this);
                }
                return;
        }
    }

    void arrived(World& world) {
        switch (then_) {
            case Then::kFirstPlace:
                stand_still(world);
                stage = 2;
                world.values.at(0) = 0;
                if (!facing_left()) {
                    world.flip_across(*this);
                }
                return wait(world, At::kWaiting, Next::kFirstVolley);
            case Then::kThirdStage:
                if (!facing_left()) {
                    world.flip_across(*this);
                }
                stage = 3;
                return shield_or_go_on(world);
            case Then::kFourthStage:
                stage = 4;
                world.values.at(0) = 0;
                stand_still(world);
                world.animate(*this);
                at_ = At::kPause;
                return;
            case Then::kFifthStage:
                stage = 5;
                world.values.at(0) = 0;
                world.values.at(1) = 0;
                stand_still(world);
                return shield_or_go_on(world);
            case Then::kBackInTheMiddle:
                world.values.at(0) = 1;
                return shield_or_go_on(world);
        }
    }

    // --- The force field ---

    void lower_force_field(World& world) {
        if (world.tomator.force_field == kNobody) {
            return;
        }
        target = world.tomator.force_field;
        world.tell(world.tomator.force_field, message::kArrowBlow);
    }
    void raise_force_field(World& world) {
        world.spawn(*this, tomator_kind::kForceField, x + (facing_left() ? -40 : 40), y, 0, entity_flag::kAlwaysActive);
    }

    // His old force field goes first; then a new one comes up before him and he goes on with his stage.
    void shield_or_go_on(World& world) {
        if (world.tomator.force_field != kNobody) {
            lower_force_field(world);
            stand_still(world);
            at_ = At::kLoweringField;
            world.animate(*this);
            return;
        }
        raise_force_field(world);
        back_to_stage(world);
    }

    void back_to_stage(World& world) {
        stand_still(world);
        switch (stage) {
            case 2:
                return wait(world, At::kWaiting, Next::kFirstVolley);
            case 3:
                return third_stage(world);
            case 4:
                return fourth_stage(world);
            case 5:
                if (world.values.at(1) == 0) {
                    at_ = At::kIdle;
                    world.animate(*this);
                    return;
                }
                return fifth_stage(world);
            default:
                return stunned(world);
        }
    }

    // --- Waiting and shooting ---

    void wait(World& world, At how, Next next) {
        next_ = next;
        at_ = how;
        world.animate(*this);
    }

    void third_stage(World& world) {
        stand_still(world);
        wait(world, At::kWaiting, Next::kThirdVolley);
    }
    void fourth_stage(World& world) {
        stand_still(world);
        wait(world, At::kWaitingShielded, Next::kFourthVolley);
    }
    void fifth_stage(World& world) {
        world.values.at(1) = 1;
        stand_still(world);
        wait(world, At::kWaitingShielded, Next::kFifthVolley);
    }

    void aim(World& world) {
        stand_still(world);
        if (volley_.faces_olaf) {
            target = slot::kOlaf;
            if (world.offset_to(*this, slot::kOlaf).ahead <= 0) {
                world.flip_across(*this);
            }
        }
        wait(world, At::kWaiting, Next::kFire);
    }
    void start_volley(World& world, std::uint16_t first, Volley volley, Next after) {
        argument = first;
        volley_ = volley;
        after_volley_ = after;
        aim(world);
    }

    // After a wait.
    void go_on(World& world) {
        switch (next_) {
            case Next::kAim:
                return aim(world);
            case Next::kFire:
                world.play(*this, anim::kTomatorAims);
                pose = tomator_pose::kAims;
                return wait(world, At::kAnimating, Next::kShoot);
            case Next::kShoot:
                world.spawn(*this, tomator_kind::kTomatorShot, x + (facing_left() ? -28 : 28), y, argument, 1);
                countdown = volley_.pause;
                ++argument;
                if (argument < volley_.until) {
                    return aim(world);
                }
                return after_volley(world);
            case Next::kFirstVolley:
                return start_volley(world, 0, {.until = 5, .pause = 15, .faces_olaf = true}, Next::kFirstVolley);
            case Next::kLongVolley:
                return start_volley(world, 10, {.until = 12, .pause = 15, .faces_olaf = true}, Next::kLongVolley);
            case Next::kThirdVolley:
                if (world.tomator.force_field == kNobody) {
                    raise_force_field(world);
                }
                return start_volley(world, 0, {.until = 10, .pause = 10, .faces_olaf = false}, Next::kThirdVolley);
            case Next::kFourthVolley:
                return start_volley(world, 0, {.until = 4, .pause = 30, .faces_olaf = false}, Next::kFourthVolley);
            case Next::kFifthVolley:
                return start_volley(world, 0, {.until = 6, .pause = 20, .faces_olaf = false}, Next::kFifthVolley);
            case Next::kAfterFour:
                return after_volley_of_four(world);
            case Next::kFifthStage:
                return fifth_stage(world);
        }
    }

    void after_volley(World& world) {
        switch (after_volley_) {
            case Next::kFirstVolley:  // again and again, until the floor is gone from under him
                argument = 0;
                return aim(world);
            case Next::kLongVolley:
                argument = 0;
                return wait(world, At::kWaiting, Next::kFirstVolley);
            case Next::kThirdVolley:
                argument = 0;
                countdown = 75;
                return third_stage(world);
            case Next::kFourthVolley:
                argument = 0;
                stand_still(world);
                countdown = 80;
                lower_force_field(world);
                return wait(world, At::kWaiting, Next::kAfterFour);
            default:  // the fifth stage's
                stand_still(world);
                countdown = 80;
                return wait(world, At::kWaiting, Next::kFifthStage);
        }
    }

    // In the fourth stage: after four blasts he drops an item and moves on; before that he walks to the wall.
    void after_volley_of_four(World& world) {
        if (world.values.at(0) >= 4) {
            world.spawn(*this, kind::kItem, facing_left() ? box.left : box.right, y, 6, entity_flag::kAlwaysActive);
            return vanish(world, Then::kFifthStage);
        }
        world.play(*this, anim::kTomatorWalks);
        pose = tomator_pose::kWalks;
        countdown = 80;
        speed_x = px(2);
        at_ = At::kWalking;
        world.animate(*this);
    }

    // --- What happens to him ---

    // A Viking who touches him is hurt (not Erik in his charge).
    void touch(World& world) {
        if (saw.viking == kNobody) {
            return;
        }
        target = saw.viking;
        Entity* other = world.at(saw.viking);
        if (other == nullptr) {
            return;
        }
        if (saw.viking == 0 && other->sprite.packed_frame >= 12 && other->sprite.packed_frame <= 21) {
            return;
        }
        const bool ahead = world.offset_to(*this, saw.viking).ahead >= 0;
        const bool from_the_right = ahead ? facing_left() : !facing_left();
        harm(*other, message::kStruck, 2, from_the_right);
        world.spawn(*this, kind::kSpark, other->x, other->y, 0, entity_flag::kLargePieces);
    }

    // What every wait starts with. True if his turn ended in it.
    bool guarded(World& world) {
        touch(world);
        if (!find_beside(world, *this, Side::kBelow, solid())) {
            fall(world);
            return true;
        }
        return message != 0 && told(world, true);
    }

    // True if his turn ended. `all`: stunned he only answers Erik's head and the air lock.
    bool told(World& world, bool all) {
        switch (message) {
            case message::kArrowBlow:
                if (all) {
                    stunned(world);
                    return true;
                }
                break;
            case message::kHeadButt:
                knocked(world);
                return true;
            case message::kBlast:
                if (all) {
                    message = 0;
                    if (message_value <= 50) {
                        stunned(world);
                        return true;
                    }
                    return false;
                }
                break;
            case message::kVanish:
                pose = tomator_pose::kBlownOut;
                world.play(*this, anim::kTomatorIsBlownOut);
                speed_y = px(-3);
                countdown = 60;
                at_ = At::kDefeated;
                world.animate(*this);
                return true;
            default:
                break;
        }
        message = 0;
        return false;
    }

    void stunned(World& world) {
        message = 0;
        world.play(*this, anim::kTomatorReels);
        pose = tomator_pose::kReels;
        countdown = 60;
        at_ = At::kStunned;
        world.animate(*this);
    }

    // Erik's head: he slides back, into whatever is there.
    void knocked(World& world) {
        message = 0;
        world.play(*this, anim::kTomatorIsKnocked);
        pose = tomator_pose::kKnocked;
        if (blow_from_the_right() == facing_left()) {
            world.flip_across(*this);
        }
        speed_x = px(-6);
        at_ = At::kKnocked;
        world.animate(*this);
    }

    void blasted(World& world) {
        target = saw.other;
        const Entity* bomb = world.at(saw.other);
        speed_x = 0;
        if (bomb != nullptr) {
            world.spawn(*this, kind::kBlast, bomb->x, bomb->y, 1, entity_flag::kLargePieces);
        }
        countdown = 45;
        ++world.values.at(0);
        world.play(*this, anim::kTomatorReels);
        pose = tomator_pose::kReels;
        at_ = At::kBlasted;
        world.animate(*this);
    }

    // Under the open air lock he is drawn up through it. True if he was.
    bool sucked_up(World& world) {
        target = saw.another;
        const Entity* lock = world.at(saw.another);
        if (lock == nullptr || box.left - 8 < lock->box.left || lock->box.right < box.right) {
            return false;
        }
        world.switches &= static_cast<std::uint16_t>(~0x0080U);
        speed_x = speed_y = 0;
        at_ = At::kSuckedUp;
        world.animate(*this);
        return true;
    }

    // The floor is gone from under him: an item is left behind (once he has been dropped twice, in the second
    // stage), his force field goes, and he falls to the next floor.
    void fall(World& world) {
        if (stage == 2 && world.values.at(0) >= 2) {
            world.spawn(*this, kind::kItem, 616, 176, 8, entity_flag::kAlwaysActive);
        }
        if (stage == 3) {
            world.spawn(*this, kind::kItem, 1088, 496, 7, entity_flag::kAlwaysActive);
        }
        lower_force_field(world);
        start_falling();
        at_ = At::kFalling;
        world.animate(*this);
    }

    void landed(World& world) {
        stop_falling();
        switch (stage) {
            case 2:
                ++world.values.at(0);
                if (world.values.at(0) < 3) {
                    return wait(world, At::kWaitingOnce, Next::kLongVolley);
                }
                at_ = At::kSettling;
                world.animate(*this);
                return;
            case 3:
                return vanish(world, Then::kFourthStage);
            case 5:
                return fifth_stage(world);
            default:
                world.remove(slot);
                return;
        }
    }

    At at_ = At::kStart;
    Then then_ = Then::kFirstPlace;
    Next next_ = Next::kAim;
    Next after_volley_ = Next::kFirstVolley;
    Volley volley_{};
};

// One of the three parts of Tomator that go with him: attached to him, it shows what goes with his pose.
class TomatorPart : public Entity {
  public:
    void update(World& world) override {
        const std::size_t part =
            kind == tomator_kind::kTomatorsFirstPart ? 0 : (kind == tomator_kind::kTomatorsSecondPart ? 1 : 2);
        if (first_turn()) {
            world.play(*this, anim::kTomatorPartLooks.at(part).at(0));
            world.animate(*this);
            world.play(*this, anim::kTomatorStands);
        }
        target = kHisSlot;
        moved_by = kHisSlot;
        push_x = push_y = 0;
        const Entity* tomator = world.at(kHisSlot);
        if (tomator != nullptr && tomator->pose != pose) {
            pose = tomator->pose;
            switch (pose) {
                case tomator_pose::kStands:
                    world.play(*this, anim::kTomatorStands);
                    break;
                case tomator_pose::kBlownOut:
                    world.play(*this, anim::kTomatorPartLooks.at(part).at(1));
                    break;
                case tomator_pose::kAims:
                    world.play(*this, anim::kTomatorAims);
                    break;
                case tomator_pose::kKnocked:
                    world.play(*this, anim::kTomatorPartLooks.at(part).at(2));
                    break;
                case tomator_pose::kReels:
                    world.play(*this, anim::kTomatorReels);
                    break;
                case tomator_pose::kHidden:
                case tomator_pose::kShown:
                    for (int index = sprite.first; index < sprite.first + sprite.count; ++index) {
                        Piece& piece = world.piece_at(index);
                        piece.hidden = pose == tomator_pose::kHidden;
                        if (pose == tomator_pose::kHidden) {
                            piece.erase |= 2U;
                        } else {
                            piece.blinked_off = false;
                            piece.redraw = 2;
                            piece.erase = 0;
                        }
                    }
                    pose = tomator_pose::kStands;
                    break;
                case tomator_pose::kWalks:
                    world.play(*this, anim::kTomatorWalks);
                    break;
                case tomator_pose::kComesBack:
                    world.play(*this, anim::kTomatorPartLooks.at(part).at(3));
                    break;
                case tomator_pose::kVanishes:
                    world.play(*this, anim::kTomatorPartLooks.at(part).at(4));
                    break;
                default:
                    break;
            }
        }
        world.animate(*this);
    }
    void outside_active_area(World& world) override { world.remove(slot); }

  private:
    static constexpr int kHisSlot = 3;
};

// Tomator's force field: it stands before him and electrocutes a Viking who touches it, until he lowers it.
class ForceField : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart: {
                world.play(*this, anim::kForceFieldStands);
                world.tomator.force_field = slot;
                target = creator;
                const Entity* tomator = world.at(creator);
                if (tomator != nullptr && tomator->stage == 3) {
                    box.top = static_cast<std::int16_t>(y - 32);
                    box.bottom = static_cast<std::int16_t>(y + 32);
                }
                state_ = State::kUp;
                break;
            }
            case State::kUp:
                if (saw.viking != kNobody) {
                    target = saw.viking;
                    world.tell(saw.viking, message::kElectrocuted);
                }
                if (message == message::kArrowBlow) {
                    world.play(*this, anim::kForceFieldGoes);
                    hit_mask = 0;
                    state_ = State::kGoing;
                }
                message = 0;
                break;
            case State::kGoing:
                if (animation_over()) {
                    world.tomator.force_field = kNobody;
                    world.remove(slot);
                    return;
                }
                break;
        }
        world.animate(*this);
    }
    Looks looks_for() const override {
        return state_ == State::kUp ? Looks{touching_one_of_the_vikings_class()} : Looks{};
    }
    void outside_active_area(World& world) override { world.remove(slot); }

  private:
    enum class State : std::uint8_t { kStart, kUp, kGoing };

    State state_ = State::kStart;
};

// What Tomator shoots. By the stage of the fight and which shot of a volley it is, it flies straight, in an arc
// (bursting where it comes down, shaking the screen), or rolls along the floor.
class TomatorShot : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                return start(world);
            case State::kRising:
            case State::kComingDown:
                if (saw.wall) {
                    return burst(world);
                }
                if (state_ == State::kRising ? false : saw.landed) {
                    return burst(world);
                }
                if (saw.ceiling) {
                    speed_y = 0;
                }
                if (saw.shield != kNobody && (state_ == State::kRising || past_shield(world))) {
                    return burst(world);
                }
                if (saw.other != kNobody) {
                    return burst(world);
                }
                if (saw.viking != kNobody) {
                    hit(world, x);
                    return burst(world);
                }
                if (speed_y >= 0) {
                    state_ = State::kComingDown;
                }
                break;
            case State::kRollingFalling:
            case State::kRolling:
                if (saw.wall) {
                    return burst(world);
                }
                if (state_ == State::kRollingFalling && saw.landed) {
                    stop_falling();
                    state_ = State::kRolling;
                    set_flag(entity_flag::kPutOnGround);
                    break;
                }
                if (saw.shield != kNobody && past_shield(world)) {
                    return burst(world);
                }
                if (saw.other != kNobody) {
                    return burst(world);
                }
                if (saw.viking != kNobody) {
                    hit(world, x);
                    return burst(world);
                }
                if (state_ == State::kRolling) {
                    if (!find_beside(world, *this, Side::kBelow, ground())) {
                        start_falling();
                        state_ = State::kRollingFalling;
                    } else {
                        set_flag(entity_flag::kPutOnGround);
                    }
                }
                break;
            case State::kStraight:
                if (saw.wall) {
                    return fizzle(world);
                }
                if (stopped_by_what_it_touches(world, *this)) {
                    return fizzle(world);
                }
                if (saw.viking != kNobody) {
                    hit(world, x + (facing_left() ? -8 : 8));
                    return fizzle(world);
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
        switch (state_) {
            case State::kRising:
                return {stopped_by(solid()), stopped_above_or_below(solid()), touching_any(hit::kShield, &Saw::shield),
                        touching_any(hit::kShieldOverhead, &Saw::other), touching_any(hit::kViking, &Saw::viking)};
            case State::kComingDown:
            case State::kRollingFalling:
                return {stopped_by(solid()), lands_on(solid()), touching_any(hit::kShield, &Saw::shield),
                        touching_any(hit::kShieldOverhead, &Saw::other), touching_any(hit::kViking, &Saw::viking)};
            case State::kRolling:
                return {stopped_by(solid()), touching_any(hit::kShield, &Saw::shield),
                        touching_any(hit::kShieldOverhead, &Saw::other), touching_any(hit::kViking, &Saw::viking)};
            case State::kStraight:
                return {stopped_by(solid()), touching_any(hit::kShield, &Saw::shield),
                        touching_any(hit::kViking, &Saw::viking)};
            default:
                return {};
        }
    }
    void outside_active_area(World& world) override { world.remove(slot); }

  private:
    enum class State : std::uint8_t { kStart, kRising, kComingDown, kRollingFalling, kRolling, kStraight, kEnding };

    void arc(World& world, std::int16_t across, std::int16_t up) {
        speed_x = across;
        speed_y = up;
        start_falling();
        world.play(*this, anim::kTomatorShotArcs);
        state_ = State::kRising;
        world.animate(*this);
    }

    void start(World& world) {
        target = creator;
        const Entity* tomator = world.at(creator);
        std::uint16_t stage = tomator != nullptr ? tomator->stage : 0;
        if (stage == 5) {
            if (world.values.at(0) != 0) {
                if (argument == 1 || argument == 2) {  // along the floor
                    world.play(*this, anim::kTomatorShotArcs);
                    speed_x = px(8);
                    start_falling();
                    state_ = State::kRollingFalling;
                    world.animate(*this);
                    return;
                }
                stage = 0xff;
            } else {
                stage = 0;
            }
        }
        switch (stage) {
            case 2:
                switch (argument) {
                    case 3:
                        return arc(world, px(6), px(-17));
                    case 4:
                        return arc(world, px(3), px(-14));
                    case 0x0a:
                        return arc(world, px(2), px(-17));
                    case 0x0b:
                        return arc(world, px(-2), px(-17));
                    default:
                        break;
                }
                break;
            case 3:
                if (argument == 4 || argument == 9) {
                    return arc(world, px(3), px(-13));
                }
                break;
            case 4:
                if (argument == 0 || argument == 1) {
                    return arc(world, px(4), px(-13));
                }
                break;
            case 0xff:
                break;
            default:
                if (argument == 2) {
                    return arc(world, px(3), px(-12));
                }
                if (argument == 5) {
                    return arc(world, px(4), px(-12));
                }
                break;
        }
        world.play(*this, anim::kTomatorShotFlies);
        speed_x = px(7);
        state_ = State::kStraight;
        world.animate(*this);
    }

    // Olaf's shield before him stops it only from the front, four pixels or more ahead.
    bool past_shield(World& world) {
        target = slot::kOlaf;
        const Entity* olaf = world.at(slot::kOlaf);
        return olaf != nullptr && olaf->facing_left() != facing_left() &&
               world.offset_to(*this, slot::kOlaf).ahead >= 4;
    }

    void hit(World& world, int spark_x) {
        target = saw.viking;
        if (Entity* viking = world.at(saw.viking)) {
            harm(*viking, message::kShot, 2, facing_left());
        }
        // NOTE: where an arcing or rolling shot hits a Viking, the original puts the spark at whatever a scratch
        // variable last held across; here: at the shot.
        world.spawn(*this, kind::kSpark, spark_x, y, 0, entity_flag::kLargePieces);
    }

    void burst(World& world) {
        speed_x = 0;
        stop_falling();
        world.sound(*this, effect::kTargetBreaks);
        world.play(*this, anim::kTomatorShotBursts);
        world.shake_y = {.amount = 1, .offset = 0, .ticks = 8};
        state_ = State::kEnding;
        world.animate(*this);
    }
    void fizzle(World& world) {
        world.sound(*this, effect::kBurst);
        world.play(*this, anim::kTomatorShotFizzles);
        speed_x = 0;
        state_ = State::kEnding;
        world.animate(*this);
    }

    State state_ = State::kStart;
};

// The air lock: when its switches come on it opens, and an enemy under it just then is told so: Tomator is
// drawn out through it.
class AirLock : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                count_down();
                box.bottom += 96;
                world.play(*this, anim::kAirLockShut);
                world.animate(*this);
                state_ = State::kShut;
                return;
            case State::kShut:
                if (!switches_on(world, argument)) {
                    return;
                }
                stay_active();
                world.play(*this, anim::kAirLockOpens);
                world.sound(*this, effect::kScrape);
                state_ = State::kOpening;
                break;
            case State::kOpening:
                if (animation_over()) {
                    world.stop_sound(*this, effect::kScrape);
                    state_ = State::kOpen;
                }
                break;
            case State::kOpen:
                if (saw.enemy != kNobody) {
                    target = saw.enemy;
                    world.tell(saw.enemy, message::kVanish);
                }
                state_ = State::kAfter;
                break;
            case State::kAfter:
                if (animation_over()) {
                    state_ = State::kDone;
                    return;
                }
                break;
            case State::kDone:
                return;
        }
        world.animate(*this);
    }
    Looks looks_for() const override { return state_ == State::kOpen ? Looks{touching_an_enemy()} : Looks{}; }
    void outside_active_area(World& world) override { world.remove(slot); }

  private:
    enum class State : std::uint8_t { kStart, kShut, kOpening, kOpen, kAfter, kDone };

    State state_ = State::kStart;
};

// A bar of electricity (kind 58) that runs up and down at its argument's speed, back when it meets the floor or
// the ceiling. Whatever it touches that can be electrocuted is told so.
class ElectricBar : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            world.play(*this, anim::kElectricBarLooks);
            speed_y = static_cast<std::int16_t>(argument);
            world.animate(*this);
            return;
        }
        if (saw.other != kNobody) {
            target = saw.other;
            world.tell(saw.other, message::kElectrocuted);
        }
        if (saw.ceiling) {
            speed_y = static_cast<std::int16_t>(-speed_y);
        }
        world.animate(*this);
    }
    static const TileTypes& ends() {
        static const TileTypes types = tile_types({tile::kGround, tile::kPlatformStop});
        return types;
    }
    Looks looks_for() const override {
        return started() ? Looks{stopped_above_or_below(ends()), touching_any(hit::kVikingInField, &Saw::other)}
                         : Looks{};
    }
};

// A wall that slides aside (kind 51): when its switches come on its four cells open and it moves 60 pixels on;
// when they go off it moves back, and its cells are solid again once it is there.
class SlidingWall : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                acts_as = tile::kGround;
                world.play(*this, anim::kSlidingWallLooks);
                world.animate(*this);
                state_ = switches_on(world) ? State::kAside : State::kInPlace;
                return;
            case State::kInPlace:
                if (switches_on(world)) {
                    set_cells(world, 0);
                    move(world, kSpeed, State::kGoingAside);
                }
                return;
            case State::kAside:
                if (!switches_on(world)) {
                    move(world, static_cast<std::int16_t>(-kSpeed), State::kComingBack);
                }
                return;
            case State::kGoingAside:
            case State::kComingBack:
                return when_going_aside(world);
        }
    }
    void when_going_aside(World& world) {
        if (countdown != 0) {
            world.animate(*this);
            return;
        }
        speed_x = 0;
        clear_flag(kMovingFlags);
        if (state_ == State::kComingBack) {
            set_cells(world, 1);
            state_ = State::kInPlace;
        } else {
            state_ = State::kAside;
        }
    }

  private:
    enum class State : std::uint8_t { kStart, kInPlace, kGoingAside, kAside, kComingBack };
    static constexpr std::int16_t kSpeed = px(6);
    static constexpr std::uint16_t kMovingTicks = 10;
    static constexpr unsigned kMovingFlags = entity_flag::kCountingDown | entity_flag::kAlwaysActive;

    bool switches_on(const World& world) const { return lvr::switches_on(world, argument); }
    void move(World& world, std::int16_t speed, State state) {
        speed_x = speed;
        countdown = kMovingTicks;
        set_flag(kMovingFlags);
        state_ = state;
        world.animate(*this);
    }
    // The four cells in a row it covers, from its left edge on: their type, their pictures kept.
    void set_cells(World& world, std::uint8_t type) const {
        const int column = cell_of(box.left + 8);
        const int row = cell_of(y);
        for (int i = 0; i < 4; ++i) {
            if (Cell* cell = world.map.cell(column + i, row)) {
                cell->tile_type = type;
            }
        }
    }

    State state_ = State::kStart;
};

}  // namespace

std::unique_ptr<Entity> make_tomator(std::uint16_t kind) {
    switch (kind) {
        case tomator_kind::kTomator:
            return make_entity<Tomator>();
        case tomator_kind::kTomatorsFirstPart:
        case tomator_kind::kTomatorsSecondPart:
        case tomator_kind::kTomatorsThirdPart:
            return make_entity<TomatorPart>();
        case tomator_kind::kForceField:
            return make_entity<ForceField>();
        case tomator_kind::kTomatorShot:
            return make_entity<TomatorShot>();
        case tomator_kind::kAirLock:
            return make_entity<AirLock>();
        case tomator_kind::kElectricBar:
            return make_entity<ElectricBar>();
        case tomator_kind::kSlidingWall:
            return make_entity<SlidingWall>();
        default:
            return nullptr;
    }
}

}  // namespace lvr
