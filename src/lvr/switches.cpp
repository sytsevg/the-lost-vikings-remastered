// What the Vikings work: levers, buttons and locks, the things an arrow or a blow sets off, the places that take
// an item, and the teleporter.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "lvr/enemies.hpp"
#include "lvr/hits.hpp"
#include "lvr/kinds.hpp"
#include "lvr/shared.hpp"
#include "lvr/terrain.hpp"
#include "lvr/world.hpp"

namespace lvr {

namespace {

bool worked(std::uint16_t message) {
    return message == message::kAsked || message == message::kArrowBlow;
}

void click(World& world, const Entity& from) {
    world.sound(from, effect::kSwitch);
}

// A lever: each time a Viking works it (or an arrow hits it) it goes over and flips its switches. Where it stands
// is kept when it leaves the active area.
class Lever : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            acts_as = tile::kThing;
            s.on = (argument & kOnBit) != 0;
            argument &= static_cast<std::uint16_t>(~kOnBit);
            world.play(*this, s.on ? stands_on() : stands_off());
        } else if (worked(message)) {
            message = 0;
            message_value = 0;
            click(world, *this);
            s.on = !s.on;
            world.play(*this, s.on ? goes_on() : goes_off());
            // One kind flips the level's second set of switches.
            (kind == kind::kLeverOfSecondSet ? world.more_switches : world.switches) ^= argument;
        }
        world.animate(*this);
    }
    void outside_active_area(World& world) override {
        if (placement >= 0) {
            world.placements.at(static_cast<std::size_t>(placement)).argument =
                static_cast<std::uint16_t>(s.on ? (argument | kOnBit) : argument);
        }
        world.remove(slot);
    }

  private:
    static constexpr std::uint16_t kOnBit = 0x8000;
    // The third kind looks different.
    Animation stands_on() const { return kind == kind::kOtherLever ? anim::kOtherLeverOn : anim::kLeverStandsOn; }
    Animation stands_off() const { return kind == kind::kOtherLever ? anim::kOtherLeverOff : anim::kLeverStandsOff; }
    Animation goes_on() const { return kind == kind::kOtherLever ? anim::kOtherLeverOn : anim::kLeverGoesOn; }
    Animation goes_off() const { return kind == kind::kOtherLever ? anim::kOtherLeverOff : anim::kLeverGoesOff; }

    struct Saved {
        bool on = false;
    } s;
    LVR_SAVES(Entity)
};

// A button: worked, it flips its switches. One kind only for a moment, one once and for all, one for a while.
class Button : public Entity {
  public:
    void update(World& world) override {
        switch (s.state) {
            case State::kStart:
                acts_as = tile::kThing;
                world.play(*this, anim::kButtonOut);
                s.state = State::kOut;
                break;
            case State::kOut:
                if (!worked(message)) {
                    break;
                }
                message = 0;
                message_value = 0;
                click(world, *this);
                world.switches ^= argument;
                world.play(*this, anim::kButtonGoesIn);
                if (kind == kind::kButtonForGood) {
                    argument = 0;
                    if (placement >= 0) {
                        world.placements.at(static_cast<std::size_t>(placement)).argument = 0;
                    }
                    s.state = State::kPressed;
                } else if (kind == kind::kButtonForAWhile) {
                    countdown = kAWhile;
                    set_flag(kHeldInFlags);
                    s.state = State::kGoingInForAWhile;
                } else {
                    s.state = State::kPressedForAMoment;
                }
                break;
            case State::kPressedForAMoment:
                world.switches ^= argument;
                s.state = State::kIn;
                break;
            case State::kPressed:
                s.state = State::kIn;
                break;
            case State::kIn:
                if (animation_over()) {
                    click(world, *this);
                    world.play(*this, anim::kButtonComesOut);
                    s.state = State::kOut;
                }
                break;
            case State::kGoingInForAWhile:
                if (!animation_over()) {
                    break;
                }
                s.state = State::kInForAWhile;
                return;
            case State::kInForAWhile:
                if (countdown != 0) {
                    return;
                }
                message = 0;
                world.switches ^= argument;
                clear_flag(kHeldInFlags);
                world.play(*this, anim::kButtonComesOut);
                click(world, *this);
                s.state = State::kOut;
                break;
        }
        world.animate(*this);
    }

  private:
    enum class State : std::uint8_t {
        kStart,
        kOut,
        kPressedForAMoment,
        kPressed,
        kIn,
        kGoingInForAWhile,
        kInForAWhile,
    };
    static constexpr std::uint16_t kAWhile = 150;  // ticks
    static constexpr std::uint16_t kHeldInFlags = entity_flag::kCountingDown | entity_flag::kAlwaysActive;

    struct Saved {
        State state = State::kStart;
    } s;
    LVR_SAVES(Entity)
};

// A lock: worked (by a Viking using the key that fits), it flips its switches and is gone for good.
class Lock : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            acts_as = tile::kLock;
            world.play(*this, anim::kLockSetUp);
            world.animate(*this);
            world.play(*this, kind == kind::kLock
                                  ? anim::kLockFirstColour
                                  : (kind == kind::kSecondLock ? anim::kLockSecondColour : anim::kLockThirdColour));
            world.animate(*this);
            return;
        }
        if (message == message::kAsked) {
            click(world, *this);
            world.switches ^= argument;
            gone_for_good(world, *this);
            return;
        }
        message = 0;
    }
};

// A dial with four pictures: a Viking who acts on it, or an arrow, turns it on to the next. While it shows the
// right one (its argument's low byte) its switch is on. Where each dial stands is one of the level's values (by
// which switch it is), so it is kept when the dial leaves the active area.
class Dial : public Entity {
  public:
    void update(World& world) override {
        switch (s.state) {
            case State::kStart:
                return at_start(world);
            case State::kWaiting:
                if (message == message::kAsked || message == message::kArrowBlow) {
                    return turn(world);
                }
                return;
            case State::kTurning:
                s.state = State::kTurned;
                return;
            case State::kTurned:
                return when_turned(world);
        }
    }
    void at_start(World& world) {
        acts_as = tile::kThing;
        s.right = argument & kLowByte;
        argument &= kHighByte;
        world.play(*this, facing_left() ? anim::kDialFacingLeft : anim::kDialFacingRight);
        world.animate(*this);
        for (std::size_t bit = 0; bit < 8; ++bit) {  // which of the level's values is its own
            if ((argument & (0x0100U << bit)) != 0) {
                s.own = bit;
                break;
            }
        }
        s.shows = world.values.at(s.own);
        if (s.shows > 3) {  // (a value that isn't a picture: it is turned at once)
            return turn(world);
        }
        show(world);
        s.state = State::kWaiting;
    }
    void when_turned(World& world) {
        message = 0;
        message_value = 0;
        s.shows = static_cast<std::uint16_t>((s.shows + 1) % 4);
        show(world);
        s.state = State::kWaiting;
    }

  private:
    enum class State : std::uint8_t { kStart, kWaiting, kTurning, kTurned };

    void turn(World& world) {
        world.play(*this, anim::kDialTurns);
        world.sound(*this, effect::kSwitch);
        world.animate(*this);
        s.state = State::kTurning;
    }
    void show(World& world) {
        world.play(*this,
                   {static_cast<std::uint16_t>(anim::kDialPictures.place + (6 * s.shows)), anim::kDialPictures.home});
        world.animate(*this);
        if (s.shows == s.right) {
            world.switches |= argument;
        } else {
            world.switches &= static_cast<std::uint16_t>(~argument);
        }
        world.values.at(s.own) = s.shows;
    }

    struct Saved {
        State state = State::kStart;
        std::uint16_t right = 0;
        std::uint16_t shows = 0;
        std::size_t own = 0;
    } s;
    LVR_SAVES(Entity)
};

// A sign on a wall: one of a few pictures, by its argument. It shows what a dial has to show.
class WallSign : public Entity {
  public:
    void update(World& world) override {
        if (!first_turn()) {
            return;
        }
        const bool tall = kind == egypt_kind::kTallWallSign;
        world.play(*this, tall ? anim::kTallWallSignSetUp : anim::kWallSignSetUp);
        world.animate(*this);
        const std::span<const Animation> pictures =
            tall ? std::span<const Animation>{anim::kTallWallSignPictures} : anim::kWallSignPictures;
        if (argument == 0 || argument > pictures.size()) {
            world.remove(slot);
            return;
        }
        world.play(*this, pictures[argument - 1U]);
        world.animate(*this);
    }
};

// A mark for an arrow: hit, it turns its switches on and is gone.
class ArrowMark : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            return;
        }
        if (message != message::kArrowBlow) {
            return;
        }
        message = 0;
        message_value = 0;
        world.switches |= argument;
        gone_for_good(world, *this);
    }
};

// A target for an arrow: hit, it flips its switches, and its cell of the map shows it shot.
class ArrowTarget : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            return;
        }
        const bool shot = message == message::kArrowBlow;
        message = 0;
        if (!shot) {
            return;
        }
        world.switches ^= argument;
        if (Cell* cell = cell_at(world, x, y)) {
            cell->metatile = world.shot_picture;
        }
        world.sound(*this, effect::kSwitch);
        gone_for_good(world, *this);
    }
};

// A target: a blow flips its switches and breaks it; its cell of the map shows it broken.
class Target : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            return;
        }
        if (message != message::kSwordBlow && message != message::kArrowBlow && message != message::kHeadButt &&
            message != message::kBlast) {
            message = 0;
            return;
        }
        world.switches ^= argument;
        if (Cell* cell = cell_at(world, x, y)) {
            cell->metatile = world.broken_picture;
        }
        world.sound(*this, effect::kTargetBreaks);
        gone_for_good(world, *this);
    }
};

// Something that takes three blows: each shows on it; the third breaks it, and its switches change.
class Breakable : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            world.play(*this, anim::kBreakableWhole);
            world.animate(*this);
            return;
        }
        if (message == 0) {
            return;
        }
        if (message != message::kSwordBlow && message != message::kArrowBlow && message != message::kHeadButt &&
            message != message::kBlast) {
            message = 0;
            return;
        }
        world.sound(*this, effect::kBurst);
        message = 0;
        health -= 1;
        if (health == 0) {
            world.switches ^= argument;
            return gone_for_good(world, *this);
        }
        if (health == 1 || health == 2) {
            world.play(*this, health == 1 ? anim::kBreakableNearlyBroken : anim::kBreakableCracked);
            world.animate(*this);
        }
    }
};

// A machine to destroy: blows take its health; when it is gone it blows up, flips its switches, and the four
// cells it stood in show its wreck.
class MachineToDestroy : public Entity {
  public:
    void update(World& world) override {
        if (s.wrecked) {
            const int column = cell_of(x - 8);
            const int row = cell_of(y - 8);
            const std::array<std::array<int, 3>, 4> cells{{{0, 0, 18}, {1, 0, 19}, {1, 1, 21}, {0, 1, 20}}};
            for (const auto& [across, down, metatile] : cells) {
                if (Cell* cell = world.map.cell(column + across, row + down)) {
                    cell->metatile = static_cast<std::uint16_t>(metatile);
                }
            }
            return gone_for_good(world, *this);
        }
        if (first_turn()) {
            return;
        }
        if (message != message::kSwordBlow && message != message::kArrowBlow && message != message::kHeadButt &&
            message != message::kBlast) {
            return;
        }
        if (survives_blow(*this)) {
            return;
        }
        stay_active();
        world.spawn(*this, factory_kind::kBlast, x, y, 0, entity_flag::kLargePieces);
        world.switches ^= argument;
        s.wrecked = true;
    }

  private:
    struct Saved {
        bool wrecked = false;
    } s;
    LVR_SAVES(Entity)
};

// The ship's generator: blows wear it down; the last one blows it up. Its switches change, its four cells show
// the wreck, and the lights go: the screen turns red and dark, then stays dim for a long while before it clears.
class Generator : public Entity {
  public:
    void update(World& world) override {
        switch (s.state) {
            case State::kStart:
                world.play(*this, anim::kGeneratorHums);
                world.animate(*this);
                s.state = State::kWhole;
                return;
            case State::kWhole:
                return when_whole(world);
            case State::kBlown:
                return when_blown(world);
            case State::kDimming:
                if (countdown != 0) {
                    return;
                }
                return next(world);
        }
    }
    void when_whole(World& world) {
        if (message == 0) {
            return;
        }
        if (message != message::kSwordBlow && message != message::kArrowBlow && message != message::kHeadButt &&
            message != message::kBlast) {
            return;
        }
        forget_blow_side();
        if (message_value < health) {
            health -= message_value;
            message = 0;
            message_value = 0;
            return;
        }
        stay_active();
        hit_mask = 0;
        world.spawn(*this, kind::kBlast, x, y, 0, entity_flag::kLargePieces);
        count_down_from(20);
        s.state = State::kBlown;
    }
    void when_blown(World& world) {
        if (countdown != 0) {
            return;
        }
        world.switches ^= argument;
        const int column = cell_of(x - 8);
        const int row = cell_of(y - 8);
        const std::array<std::array<int, 3>, 4> cells{{{0, 0, 7}, {1, 0, 8}, {1, 1, 10}, {0, 1, 9}}};
        for (const auto& [across, down, metatile] : cells) {
            if (Cell* cell = world.map.cell(column + across, row + down)) {
                cell->metatile = static_cast<std::uint16_t>(metatile);
            }
        }
        world.sound(*this, effect::kAlarm);
        s.step = 0;
        s.state = State::kDimming;
        return next(world);
    }

  private:
    enum class State : std::uint8_t { kStart, kWhole, kBlown, kDimming };
    struct Step {
        std::array<std::uint8_t, 3> tint;
        std::uint16_t ticks;
    };
    static constexpr std::size_t kPowerDownAt = 13;  // the step that starts with the second sound
    static constexpr std::array<Step, 41> kSteps{{
        {{2, 2, 2}, 1},     {{4, 4, 4}, 1},    {{6, 7, 7}, 1},     {{8, 9, 9}, 1},    {{10, 11, 11}, 1},
        {{12, 14, 14}, 1},  {{14, 16, 16}, 1}, {{16, 19, 19}, 1},  {{17, 20, 20}, 1}, {{18, 22, 22}, 1},
        {{19, 23, 23}, 1},  {{20, 25, 25}, 1}, {{20, 25, 25}, 30}, {{18, 25, 25}, 1}, {{17, 25, 25}, 1},
        {{16, 24, 24}, 1},  {{15, 24, 24}, 1}, {{14, 23, 23}, 1},  {{12, 22, 22}, 1}, {{10, 21, 21}, 1},
        {{8, 20, 20}, 1},   {{6, 19, 19}, 1},  {{4, 18, 18}, 1},   {{2, 17, 17}, 1},  {{0, 16, 16}, 1},
        {{0, 16, 16}, 400}, {{0, 15, 15}, 1},  {{0, 14, 14}, 1},   {{0, 13, 13}, 1},  {{0, 12, 12}, 1},
        {{0, 11, 11}, 1},   {{0, 10, 10}, 1},  {{0, 9, 9}, 1},     {{0, 8, 8}, 1},    {{0, 7, 7}, 1},
        {{0, 6, 6}, 1},     {{0, 5, 5}, 1},    {{0, 4, 4}, 1},     {{0, 3, 3}, 1},    {{0, 2, 2}, 1},
        {{0, 1, 1}, 1},
    }};

    void next(World& world) {
        if (s.step == kSteps.size()) {
            world.tint = {};
            world.stop_sound(*this, effect::kPowerDown);
            gone_for_good(world, *this);
            return;
        }
        if (s.step == kPowerDownAt) {
            world.sound(*this, effect::kPowerDown);
        }
        const Step& step = kSteps.at(s.step);
        world.tint = {static_cast<std::uint8_t>(step.tint[0] << 1U), static_cast<std::uint8_t>(step.tint[1] << 1U),
                      static_cast<std::uint8_t>(step.tint[2] << 1U)};
        countdown = step.ticks;
        ++s.step;
    }

    struct Saved {
        State state = State::kStart;
        std::size_t step = 0;
    } s;
    LVR_SAVES(Entity)
};

// A place that takes an item (its argument's low byte: which): given it, it flips its switches, its cell of the
// map shows the item in place, and it is gone. The one that takes item 0D becomes a running machine instead,
// which a bomb or a fire arrow blows up: the switches flip back and nine cells show the wreck.
class ItemPlace : public Entity {
  public:
    void update(World& world) override {
        switch (s.state) {
            case State::kStart:
                return at_start(world);
            case State::kWaiting:
                return when_waiting(world);
            case State::kRunning:
                return when_running(world);
        }
    }
    void at_start(World& world) {
        acts_as = tile::kTaker;
        wanted_item = argument & kLowByte;
        argument &= kHighByte;
        if (wanted_item == kStartsMachine && world.factory.part_fitted) {
            return run();
        }
        s.state = State::kWaiting;
    }
    void when_waiting(World& world) {
        const bool given = message == message::kAsked;
        message = 0;
        if (!given) {
            return;
        }
        world.switches ^= argument;
        static constexpr std::array<std::array<std::uint16_t, 2>, 6> kShown{
            {{15, 15}, {16, 16}, {17, 12}, {18, 14}, {13, 13}, {14, 17}}};
        for (const auto& [item, metatile] : kShown) {
            if (item == wanted_item) {
                if (Cell* cell = cell_at(world, x, y)) {
                    cell->metatile = metatile;
                }
            }
        }
        if (wanted_item != kStartsMachine) {
            return gone_for_good(world, *this);
        }
        if (world.cycles.size() > 1) {
            world.cycles.at(1).running = true;
        }
        world.factory.part_fitted = true;
        return run();
    }
    void when_running(World& world) {
        if (message != message::kBlast && message != message::kArrowBlow) {
            message = 0;
            return;
        }
        forget_blow_side();
        if (message_value < 3) {
            return;
        }
        world.spawn(*this, factory_kind::kBlast, x - 16, y, 0, entity_flag::kLargePieces);
        world.switches ^= argument;
        if (world.cycles.size() > 1) {
            world.cycles.at(1).running = false;
        }
        const int column = (cell_of(x - 16)) - 1;
        const int row = (cell_of(y)) - 1;
        for (int i = 0; i < 9; ++i) {
            if (Cell* cell = world.map.cell(column + (i % 3), row + (i / 3))) {
                cell->metatile = static_cast<std::uint16_t>(22 + i);
            }
        }
        return gone_for_good(world, *this);
    }

  private:
    enum class State : std::uint8_t { kStart, kWaiting, kRunning };
    static constexpr std::uint16_t kStartsMachine = 13;

    void run() {
        static constexpr std::uint16_t kCanBeBlownUp = bits(hit::kHurtByFire, hit::kBombable, hit::kStopsArrow);
        hit_mask |= kCanBeBlownUp;
        s.state = State::kRunning;
    }

    struct Saved {
        State state = State::kStart;
    } s;
    LVR_SAVES(Entity)
};

// A machine that takes item 0E and gives the item of its argument, which drops out beside it.
class Vendor : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            acts_as = tile::kTaker;
            wanted_item = 14;
            return;
        }
        const bool given = message == message::kAsked;
        message = 0;
        if (!given) {
            return;
        }
        world.spawn(*this, kind::kItem, x + 48, y + 8, bits(argument, item::kFalling), entity_flag::kAlwaysActive);
        if (Cell* cell = cell_at(world, x, y)) {
            cell->metatile = 17;
        }
        gone_for_good(world, *this);
    }
};

// A light that blinks for a while when a Viking acts on it: the cell above it shows two pictures by turns.
class BlinkingLight : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            acts_as = tile::kThing;
            count_down();
            s.column = cell_of(x);
            s.row = (cell_of(y)) - 1;
            return;
        }
        if (s.phase < 0) {
            const bool asked = message == message::kAsked;
            message = 0;
            if (!asked) {
                return;
            }
            countdown = 16;
            s.phase = 0;
        } else if (++s.phase == 4) {
            if (countdown == 0) {
                s.phase = -1;
                return;
            }
            s.phase = 0;
        }
        if (s.phase == 0 || s.phase == 2) {
            if (Cell* cell = world.map.cell(s.column, s.row)) {
                cell->metatile = s.phase == 0 ? 3 : 4;
            }
        }
    }

  private:
    struct Saved {
        int column = 0;
        int row = 0;
        int phase = -1;  // which of a blink's four ticks; -1: not blinking
    } s;
    LVR_SAVES(Entity)
};

// Something a Viking can act on that does nothing itself.
class Fixture : public Entity {
  public:
    void update(World& /*world*/) override { acts_as = tile::kThing; }
};

// A kind the factory's levels place that does nothing: it is gone at once.
class Nothing : public Entity {
  public:
    void update(World& world) override { world.remove(slot); }
};

// A teleporter: a Viking who acts on it is taken to where it leads. Which one of the game's it is, is its argument.
class Teleporter : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            acts_as = tile::kThing;
            world.play(*this, anim::kTeleporterStands);
            world.animate(*this);
        }
    }
};

}  // namespace

std::optional<TeleporterDestination> teleporter_destination(std::uint16_t argument) {
    struct Leads {
        std::uint16_t argument;
        TeleporterDestination to;
    };
    // Only the spaceship's levels have teleporters.
    static constexpr std::array<Leads, 41> kTeleporters{{
        {0, {656, 402}},    {1, {656, 402}},    {2, {64, 130}},    {3, {1264, 434}},   {4, {80, 578}},
        {5, {96, 450}},     {98, {80, 770}},    {99, {1040, 178}}, {100, {800, 578}},  {101, {128, 642}},
        {102, {144, 290}},  {103, {680, 1218}}, {104, {768, 834}}, {105, {592, 322}},  {1051, {432, 50}},
        {106, {640, 1106}}, {107, {48, 1074}},  {108, {688, 706}}, {109, {1200, 66}},  {110, {720, 226}},
        {111, {848, 178}},  {112, {880, 642}},  {113, {512, 66}},  {114, {1200, 642}}, {115, {192, 114}},
        {116, {880, 786}},  {117, {576, 66}},   {118, {704, 642}}, {119, {880, 786}},  {120, {880, 786}},
        {121, {736, 128}},  {150, {704, 754}},  {151, {320, 354}}, {152, {1200, 34}},  {153, {96, 66}},
        {154, {224, 610}},  {200, {240, 338}},  {211, {832, 834}}, {212, {880, 738}},  {213, {1488, 290}},
        {214, {1568, 754}},
    }};
    for (const Leads& teleporter : kTeleporters) {
        if (teleporter.argument == argument) {
            return teleporter.to;
        }
    }
    return std::nullopt;
}

std::span<const KindOf> switches() {
    static constexpr KindOf kList[] = {
        {Home::kAnywhere, kind::kLever, make<Lever>},
        {Home::kAnywhere, kind::kLeverOfSecondSet, make<Lever>},
        {Home::kAnywhere, kind::kOtherLever, make<Lever>},
        {Home::kAnywhere, kind::kButton, make<Button>},
        {Home::kAnywhere, kind::kButtonForGood, make<Button>},
        {Home::kAnywhere, kind::kButtonForAWhile, make<Button>},
        {Home::kAnywhere, kind::kLock, make<Lock>},
        {Home::kAnywhere, kind::kSecondLock, make<Lock>},
        {Home::kAnywhere, kind::kThirdLock, make<Lock>},
        {Home::kEgypt, egypt_kind::kDial, make<Dial>},
        {Home::kEgypt, egypt_kind::kTallWallSign, make<WallSign>},
        {Home::kEgypt, egypt_kind::kWallSign, make<WallSign>},
        {Home::kEgypt, egypt_kind::kArrowMark, make<ArrowMark>},
        {Home::kWacky, wacky_kind::kArrowTarget, make<ArrowTarget>},
        {Home::kFactory, factory_kind::kTarget, make<Target>},
        {Home::kPrehistoric, prehistoric_kind::kBreakable, make<Breakable>},
        {Home::kFactory, factory_kind::kMachine, make<MachineToDestroy>},
        {Home::kAnywhere, kind::kGenerator, make<Generator>},
        {Home::kFactory, factory_kind::kItemPlace, make<ItemPlace>},
        {Home::kFactory, factory_kind::kVendor, make<Vendor>},
        {Home::kFactory, factory_kind::kBlinkingLight, make<BlinkingLight>},
        {Home::kFactory, factory_kind::kCraneControls, make<Fixture>},
        {Home::kFactory, factory_kind::kNothing8F, make<Nothing>},
        {Home::kFactory, factory_kind::kNothing90, make<Nothing>},
        {Home::kAnywhere, kind::kTeleporter, make<Teleporter>},
    };
    return kList;
}

}  // namespace lvr
