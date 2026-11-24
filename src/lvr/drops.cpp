// What keeps coming: the drops and the things that fall from above, the bubbles, and the unseen places that
// make them.

#include <array>
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

// An unseen place that makes the things that fall (the next kind), one after another: by its argument, at five
// fixed places of the level in turn, every 10 ticks; or around itself, every 20.
class DropMaker : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            count_down();
            return;
        }
        if (countdown != 0) {
            return;
        }
        if (argument == 0) {
            static constexpr std::array<int, 5> kAcross{584, 440, 632, 488, 536};
            countdown = 10;
            turn_ = turn_ >= 4 ? 0 : turn_ + 1;
            world.spawn(*this, egypt_kind::kDropper, kAcross.at(static_cast<std::size_t>(turn_)), 280);
        } else if (argument == 1) {
            static constexpr std::array<int, 6> kAcross{-32, 0, 32, -32, 32, 0};
            countdown = 20;
            turn_ = turn_ >= 5 ? 0 : turn_ + 1;
            world.spawn(*this, egypt_kind::kDropper, x + kAcross.at(static_cast<std::size_t>(turn_)), y, 0, 0);
        } else {
            world.remove(slot);
        }
    }

  private:
    int turn_ = 0;
};

// One that is seen, and switched: while the switches of its argument's high byte are on and those of its low byte
// aren't, it makes one every tick, just under itself.
class SwitchedDropMaker : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            world.play(*this, anim::kSwitchedDropMakerLooks);
            unless_ = argument & kLowByte;
            argument &= kHighByte;
            count_down();
        } else if (switches_on(world, argument) && !switches_on(world, unless_)) {
            world.spawn(*this, egypt_kind::kDropper, x, y + 2);
        }
        world.animate(*this);
    }

  private:
    std::uint16_t unless_ = 0;
};

// Something that hangs until nothing is under it, then falls: it hurts a Viking it hits, and breaks where it
// lands.
class Dropper : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                world.play(*this, anim::kDropperHangs);
                state_ = State::kHanging;
                break;
            case State::kHanging:
                if (!find_beside(world, *this, Side::kBelow, ground())) {
                    start_falling();
                    state_ = State::kFalling;
                }
                break;
            case State::kFalling:
                if (saw.viking != kNobody && !saw.landed) {
                    target = saw.viking;
                    if (Entity* other = world.at(saw.viking)) {
                        harm(*other, message::kShot, 2, x >= other->x);
                    }
                    world.spawn(*this, kind::kSpark, x, y, 0, entity_flag::kLargePieces);
                }
                if (saw.landed || saw.viking != kNobody) {
                    world.play(*this, anim::kDropperBreaks);
                    stop_falling();
                    state_ = State::kBreaking;
                }
                break;
            case State::kBreaking:
                if (animation_over()) {
                    world.remove(slot);
                    return;
                }
                break;
        }
        world.animate(*this);
    }
    Looks looks_for() const override {
        return state_ == State::kFalling
                   ? Looks{lands_on(floor_or_raised_shield()), touching_one_of_the_vikings_class()}
                   : Looks{};
    }

  private:
    enum class State : std::uint8_t { kStart, kHanging, kFalling, kBreaking };

    State state_ = State::kStart;
};

// A place that drips while a Viking is in it: a drop every so often, from fixed points of its level or from its
// own top.
class DripPlace : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            count_down();
            mode_ = argument;
            acts_as = tile::kBody;
            const int across = argument == 0 ? 56 : 64;
            const int down = argument == 0 ? 216 : 128;
            box = box_of(x - across, y - down, x + across, y + down);
            return;
        }
        if (saw.viking == kNobody || countdown != 0) {
            return;
        }
        target = saw.viking;
        if (mode_ == 10) {
            world.spawn(*this, egypt_kind::kDrop, 70, 592);
            countdown = 25;
            mode_ = 0;
        } else if (mode_ == 1) {
            world.spawn(*this, egypt_kind::kDrop, x, box.top);
            countdown = 40;
        } else {
            world.spawn(*this, egypt_kind::kDrop, 48, 592);
            countdown = 15;
            mode_ = 10;
        }
    }
    Looks looks_for() const override { return started() ? Looks{touching_a_viking()} : Looks{}; }

  private:
    std::uint16_t mode_ = 0;
};

// A drop: it forms, falls, and splashes on the ground. A Viking it falls on is gone.
class Drop : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                stay_active();
                world.play(*this, anim::kDropForms);
                state_ = State::kForming;
                break;
            case State::kForming:
                if (animation_over()) {
                    world.play(*this, anim::kDropFalls);
                    start_falling();
                    speed_y = px(1.5);
                    state_ = State::kFalling;
                }
                break;
            case State::kFalling:
                if (!saw.landed && saw.viking != kNobody) {
                    target = saw.viking;
                    if (Entity* other = world.at(saw.viking)) {
                        harm(*other, message::kDissolved, 1, facing_left());
                    }
                    world.spawn(*this, kind::kSpark, x, y, 0, entity_flag::kLargePieces);
                }
                if (saw.landed || saw.viking != kNobody) {
                    world.sound(*this, effect::kDropLands);
                    world.play(*this, anim::kDropSplashes);
                    stop_falling();
                    state_ = State::kSplashing;
                }
                break;
            case State::kSplashing:
                if (animation_over()) {
                    world.remove(slot);
                    return;
                }
                break;
        }
        world.animate(*this);
    }
    Looks looks_for() const override {
        return state_ == State::kFalling ? Looks{lands_on(solid()), touching_one_of_the_vikings_class()} : Looks{};
    }

  private:
    enum class State : std::uint8_t { kStart, kForming, kFalling, kSplashing };

    State state_ = State::kStart;
};

// A drop that falls (at one of two speeds, by chance): it hurts a Viking it hits, and splashes on the ground.
class FallingDrop : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                world.play(*this, anim::kFallingDropFalls);
                speed_y = world.random() <= 32768 ? px(12) : px(10);
                state_ = State::kFalling;
                break;
            case State::kFalling:
                if (saw.on_shield) {
                    return splash(world);
                }
                if (saw.viking != kNobody) {
                    target = saw.viking;
                    if (Entity* other = world.at(saw.viking)) {
                        harm(*other, message::kDrowning, 2, facing_left());
                    }
                    world.spawn(*this, kind::kSpark, x, y, 0, entity_flag::kLargePieces);
                    world.hide_pieces(*this);
                    world.play(*this, anim::kFallingDropGone);
                    state_ = State::kEnding;
                    break;
                }
                if (saw.ceiling) {
                    return splash(world);
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
    // Falling: Olaf's shield, held up, catches it; the ground ends it; a Viking is hit by it.
    Looks looks_for() const override {
        return state_ == State::kFalling
                   ? Looks{stopped_above_or_below(shield_overhead(), &Saw::on_shield), stopped_above_or_below(solid()),
                           touching_any(hit::kViking, &Saw::viking)}
                   : Looks{};
    }

  private:
    enum class State : std::uint8_t { kStart, kFalling, kEnding };

    void splash(World& world) {
        speed_y = 0;
        world.play(*this, anim::kFallingDropSplashes);
        state_ = State::kEnding;
        world.animate(*this);
    }

    State state_ = State::kStart;
};

// What a level's bubbles (and other things that keep coming) are sent out by: a list of steps, each creating
// something somewhere and waiting before the next. Its argument is the step it starts at; each level's list
// goes round.
class Sender : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            count_down();
            step_ = argument;
            if (argument == kWaitsForRoom) {
                box = box_of(x - 48, y - 152, x + 48, y + 152);
            }
            return;
        }
        if (waiting_for_room_) {
            if (saw.other != kNobody) {
                return;
            }
            waiting_for_room_ = false;
            send(world, {kind::kRisingPlatform, 872, 792, kUpTwoATick, entity_flag::kAlwaysActive});
            return next(20, static_cast<std::uint16_t>(step_ + 1));
        }
        if (countdown != 0) {
            return;
        }
        const auto after = static_cast<std::uint16_t>(step_ + 1);
        switch (step_) {
            case 25: {
                countdown = 20;
                world.sound(*this, effect::kWhoosh);
                const std::uint16_t dice = world.random();
                const int at = dice <= 16384 ? 664 : (dice <= 32768 ? 704 : (dice <= 49152 ? 744 : 624));
                return send(world, {prehistoric_kind::kFireDrop, at, 480, 0, 0});
            }
            case 30:
                stay_active();
                [[fallthrough]];
            case 32:
                send(world, {kind::kSwitchedRisingPlatform, 144, 816, px(1), entity_flag::kAlwaysActive});
                send(world, {kind::kSwitchedRisingPlatform, 192, 816, px(2), entity_flag::kAlwaysActive});
                return next(80, after);
            case 31:
                send(world, {kind::kSwitchedRisingPlatform, 96, 816, px(2), entity_flag::kAlwaysActive});
                return next(55, after);
            case 33:
                send(world, {kind::kSwitchedRisingPlatform, 96, 816, px(2), entity_flag::kAlwaysActive});
                return next(65, 30);
            case 54:
                return next(40, 50);
            case kWaitsForRoom:
                waiting_for_room_ = true;
                return;
            case 61:
                send(world, {kind::kRisingPlatform, 920, 792, kUpTwoATick, entity_flag::kAlwaysActive});
                return next(20, after);
            case 62:
                send(world, {kind::kRisingPlatform, 872, 792, kUpTwoATick, entity_flag::kAlwaysActive});
                return next(160, kWaitsForRoom);
            default:
                break;
        }
        struct Step {
            std::uint16_t step;
            Sent sent;
            std::uint16_t wait;
            std::uint16_t then;  // 0xffff: the next step
        };
        static constexpr std::uint16_t kNext = 0xffff;
        static constexpr std::uint16_t kAnyStep = 0xffff;
        static constexpr std::array<Step, 23> kSteps{{
            {0, {prehistoric_kind::kBubble, 608, 726, 0, entity_flag::kLargePieces}, 20, kNext},
            {1, {prehistoric_kind::kBubble, 448, 726, 0, entity_flag::kLargePieces}, 20, kNext},
            {2, {prehistoric_kind::kBubble, 416, 726, 0, entity_flag::kLargePieces}, 20, kNext},
            {3, {prehistoric_kind::kSmallBubble, 550, 726, 0, 0}, 20, kNext},
            {4, {prehistoric_kind::kBubble, 496, 726, 0, entity_flag::kLargePieces}, 20, kNext},
            {5, {prehistoric_kind::kBubble, 640, 726, 0, entity_flag::kLargePieces}, 20, kNext},
            {6, {prehistoric_kind::kBubble, 464, 726, 0, entity_flag::kLargePieces}, 20, kNext},
            {7, {prehistoric_kind::kMiddleBubble, 500, 726, 0, 0}, 20, kNext},
            {8, {prehistoric_kind::kBubble, 552, 726, 0, entity_flag::kLargePieces}, 20, kNext},
            {9, {prehistoric_kind::kBubble, 416, 726, 0, entity_flag::kLargePieces}, 20, kNext},
            {10, {prehistoric_kind::kMiddleBubble, 430, 726, 0, 0}, 20, kNext},
            {11, {prehistoric_kind::kBubble, 672, 726, 0, entity_flag::kLargePieces}, 20, kNext},
            {12, {prehistoric_kind::kBubble, 496, 726, 0, entity_flag::kLargePieces}, 20, 0},
            {20, {prehistoric_kind::kBubble, 600, 760, 450, entity_flag::kLargePieces}, 240, kNext},
            {21, {prehistoric_kind::kBubble, 600, 760, 450, entity_flag::kLargePieces}, 240, 20},
            {40, {prehistoric_kind::kBubble, 624, 880, 400, entity_flag::kLargePieces}, 125, kNext},
            {41, {prehistoric_kind::kBubble, 624, 880, 400, entity_flag::kLargePieces}, 125, kNext},
            {42, {prehistoric_kind::kBubble, 624, 880, 400, entity_flag::kLargePieces}, 125, 40},
            {50, {prehistoric_kind::kBubble, 576, 1232, 160, entity_flag::kLargePieces}, 40, kNext},
            {51, {prehistoric_kind::kBubble, 688, 1232, 160, entity_flag::kLargePieces}, 40, kNext},
            {52, {prehistoric_kind::kBubble, 480, 1232, 160, entity_flag::kLargePieces}, 40, kNext},
            {53, {prehistoric_kind::kBubble, 608, 1232, 160, entity_flag::kLargePieces}, 40, kNext},
            {kAnyStep,
             {prehistoric_kind::kBubble, 608, 726, 0, entity_flag::kLargePieces},
             20,
             kNext},  // any other step does as the first
        }};
        for (const Step& step : kSteps) {
            if (step.step == step_ || step.step == kAnyStep) {
                send(world, step.sent);
                return next(step.wait, step.then == kNext ? after : step.then);
            }
        }
    }
    Looks looks_for() const override {
        return waiting_for_room_ ? Looks{touching(hit_class::kInTheWay, &Saw::other)} : Looks{};
    }

  private:
    static constexpr std::uint16_t kWaitsForRoom = 60;
    // A rising platform's argument is its speed: two pixels a tick, upwards.
    static constexpr auto kUpTwoATick = static_cast<std::uint16_t>(px(-2));

    struct Sent {
        std::uint16_t kind;
        int x;
        int y;
        std::uint16_t argument;
        std::uint16_t flags;
    };

    void send(World& world, const Sent& sent) {
        world.spawn(*this, sent.kind, sent.x, sent.y, sent.argument, sent.flags);
    }
    void next(std::uint16_t wait, std::uint16_t step) {
        countdown = wait;
        step_ = step;
    }

    bool waiting_for_room_ = false;
    std::uint16_t step_ = 0;
};

// A bubble that rises for a while and then bursts. The large kind carries the Vikings: it dips when one steps
// on, and bobs up when he steps off. The two small kinds only rise.
class Bubble : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                return at_start(world);
            case State::kDrifting:
                return when_drifting(world);
            case State::kDipping:
            case State::kComingBack:
                return when_dipping(world);
            case State::kBobbingUp:
                return when_bobbing_up(world);
            case State::kBursting:
                return when_bursting(world);
        }
    }
    void at_start(World& world) {
        world.play(*this,
                   kind == prehistoric_kind::kSmallBubble
                       ? anim::kBubbleSmallRises
                       : (kind == prehistoric_kind::kMiddleBubble ? anim::kBubbleMiddleRises : anim::kBubbleRises));
        speed_y = static_cast<std::int16_t>(kind == prehistoric_kind::kSmallBubble
                                                ? px(-2)
                                                : (kind == prehistoric_kind::kMiddleBubble ? px(-1.5) : kDrift));
        if (kind == prehistoric_kind::kBubble) {
            acts_as = tile::kPlatform;
        }
        stay_active();
        count_down_from(argument != 0 ? argument : 110);
        return drift(world);
    }
    void when_drifting(World& world) {
        if (countdown == 0) {
            stay_active(false);
            loaded_ = true;
            world.play(*this, kind == prehistoric_kind::kSmallBubble
                                  ? anim::kBubbleSmallBursts
                                  : (kind == prehistoric_kind::kMiddleBubble ? anim::kBubbleMiddleBursts
                                                                             : anim::kBubbleBursts));
            state_ = State::kBursting;
            world.animate(*this);
            return;
        }
        if (ridden_ && !loaded_) {
            loaded_ = true;
            speed_y = kBob;
            ticks_ = 0;
            state_ = State::kDipping;
            carry(world);
            world.animate(*this);
            return;
        }
        if (!ridden_ && loaded_) {
            loaded_ = false;
            speed_y = -kBob;
            ticks_ = 0;
            state_ = State::kBobbingUp;
            world.animate(*this);
            return;
        }
        return drift(world);
    }
    void when_dipping(World& world) {
        if (ticks_ == 2) {
            if (state_ == State::kComingBack) {
                speed_y = kDrift;
                return drift(world);
            }
            speed_y = -kBob;
            ticks_ = 0;
            state_ = State::kComingBack;
        } else {
            ++ticks_;
        }
        carry(world);
        world.animate(*this);
    }
    void when_bobbing_up(World& world) {
        if (ticks_ == 3) {
            speed_y = kDrift;
            return drift(world);
        }
        ++ticks_;
        world.animate(*this);
    }
    void when_bursting(World& world) {
        if (animation_over()) {
            world.remove(slot);
            return;
        }
        carry(world);
        world.animate(*this);
    }

  private:
    enum class State : std::uint8_t { kStart, kDrifting, kDipping, kComingBack, kBobbingUp, kBursting };
    static constexpr std::int16_t kDrift = px(-1);
    static constexpr std::int16_t kBob = px(2.5);

    void drift(World& world) {
        ridden_ = false;
        carry(world);
        world.animate(*this);
        state_ = State::kDrifting;
    }

    void carry(World& world) {
        if (kind != prehistoric_kind::kBubble) {
            return;
        }
        for (const int rider : vikings_in_row(world, *this, Row::kAbove)) {
            Entity* other = world.at(rider);
            if (other != nullptr && world.offset_to(*this, rider).below < -10) {
                other->push_y += speed_y;
                ridden_ = true;
            }
        }
    }

    State state_ = State::kStart;
    bool ridden_ = false;
    bool loaded_ = false;
    int ticks_ = 0;
};

}  // namespace

std::span<const KindOf> drops() {
    static constexpr KindOf kList[] = {
        {Home::kEgypt, egypt_kind::kDropMaker, make<DropMaker>},
        {Home::kEgypt, egypt_kind::kSwitchedDropMaker, make<SwitchedDropMaker>},
        {Home::kEgypt, egypt_kind::kDropper, make<Dropper>},
        {Home::kEgypt, egypt_kind::kDripPlace, make<DripPlace>},
        {Home::kEgypt, egypt_kind::kDrop, make<Drop>},
        {Home::kAnywhere, kind::kFallingDrop, make<FallingDrop>},
        {Home::kPrehistoric, prehistoric_kind::kSender, make<Sender>},
        {Home::kPrehistoric, prehistoric_kind::kSmallBubble, make<Bubble>},
        {Home::kPrehistoric, prehistoric_kind::kMiddleBubble, make<Bubble>},
        {Home::kPrehistoric, prehistoric_kind::kBubble, make<Bubble>},
    };
    return kList;
}

}  // namespace lvr
