// The moving platforms of Egypt, the factory and the wacky world: the ones that go up and down or left and right
// between walls while their switches are on, and the ones that go round in a circle. Read from Egypt's object
// database (451), where all of them are; the other worlds have some of them, with the same behaviour.

#include <array>
#include <cstdint>
#include <memory>
#include <span>

#include "lvr/hits.hpp"
#include "lvr/kinds.hpp"
#include "lvr/shared.hpp"
#include "lvr/terrain.hpp"
#include "lvr/world.hpp"

namespace lvr {

namespace {

constexpr int kHome = database::kEgypt;  // the database the animations are named in: Egypt's

// What an up-and-down platform's argument holds besides its switches (kLowByte) and whether it squashes (kTopBit).
constexpr unsigned kSolidBit = 0x4000U;  // it is solid all round, not only stood on
constexpr unsigned kSpeedBits = 0x3f00U;
constexpr unsigned kPaceBits = 0x7f00U;  // the one-way platform's

// The kinds: each one's number in Egypt's database, the animation it shows there, and the way it goes.
enum class Way : std::uint8_t { kUpAndDown, kAcross, kRound };

struct PlatformKind {
    std::uint16_t kind;
    Animation looks;
    Way way;
    bool crushes = false;  // solid all round: what it meets is crushed
};

constexpr std::array<PlatformKind, 19> kPlatforms{{
    {0x6e, {0x2776, kHome}, Way::kUpAndDown}, {0x6f, {0x2788, kHome}, Way::kUpAndDown},
    {0x70, {0x2776, kHome}, Way::kRound},     {0x7b, {0x27a4, kHome}, Way::kUpAndDown},
    {0x7c, {0x27b6, kHome}, Way::kUpAndDown}, {0x7d, {0x27a4, kHome}, Way::kRound},
    {0x7f, {0x27a4, kHome}, Way::kAcross},    {0x80, {0x27b6, kHome}, Way::kAcross},
    {0xa0, {0x27a4, kHome}, Way::kUpAndDown}, {0xa1, {0x27b6, kHome}, Way::kUpAndDown},
    {0xa2, {0x27a4, kHome}, Way::kAcross},    {0xa3, {0x27b6, kHome}, Way::kAcross},
    {0xa4, {0x27a4, kHome}, Way::kRound},     {0xa5, {0x27d2, kHome}, Way::kAcross, true},
    {0xb0, {0x27e7, kHome}, Way::kUpAndDown}, {0xb1, {0x27f9, kHome}, Way::kUpAndDown},
    {0xb2, {0x27e7, kHome}, Way::kAcross},    {0xb3, {0x27f9, kHome}, Way::kAcross},
    {0xb4, {0x27e7, kHome}, Way::kRound},
}};

// The row of a kind; none if it isn't one of these.
const PlatformKind* platform_kind(std::uint16_t kind) {
    for (const PlatformKind& row : kPlatforms) {
        if (row.kind == kind) {
            return &row;
        }
    }
    return nullptr;
}

// A Viking in a platform (it came up under him, or he fell into it), falling or standing: onto its top.
// `whole_pixels`: what was left of a pixel from his last move is dropped. The platforms that go back and forth do
// that; the wacky world's one-way platform doesn't (its script, 453:31A9, has no such step), so a Viking who falls
// into it can come to stand a pixel lower.
bool lift_out(const Entity& platform, Entity& viking, bool whole_pixels = true) {
    if (static_cast<std::uint16_t>(viking.box.bottom) >= static_cast<std::uint16_t>(platform.box.bottom) ||
        viking.hit_class != hit_class::kViking || viking.speed_y < 0) {
        return false;
    }
    const int into = viking.box.bottom - platform.box.top + 1;
    viking.push_y = static_cast<std::int16_t>(viking.push_y - (into * 256) - viking.speed_y);
    if (whole_pixels) {
        viking.fraction_y = 0;
    }
    return true;
}

// Every Viking over the platform goes with it.
void carry_vikings(World& world, Entity& platform, bool whole_pixels = true) {
    for (const int rider : vikings_in_row(world, platform, Row::kAbove)) {
        Entity* viking = world.at(rider);
        if (viking == nullptr) {
            continue;
        }
        if (viking->box.bottom + 1 == platform.box.top) {
            viking->push_x += platform.speed_x;
            viking->push_y += platform.speed_y;
        } else if (static_cast<std::uint16_t>(viking->box.bottom) < static_cast<std::uint16_t>(platform.box.bottom) &&
                   viking->hit_class == hit_class::kViking && viking->speed_y >= 0) {
            viking->push_y += platform.speed_y;
            (void)lift_out(platform, *viking, whole_pixels);
        }
    }
}

// A platform that goes up and down, or left and right, while its switches are on (its argument's low byte; none:
// always), turning where it meets a wall. It carries the Vikings on it. Its argument's high byte is its speed;
// of one that goes up and down, the top bits make it a wall instead of a platform, and one that squashes the
// Viking it comes down on. While a Viking stands on it, it stays active wherever the view is.
class MovingPlatform : public Entity {
  public:
    void update(World& world) override {
        if (s.state == State::kStart) {
            start(world);
        }
        if (s.state == State::kWaiting) {
            if (saw.viking != kNobody) {
                if (Entity* viking = world.at(saw.viking)) {
                    target = saw.viking;
                    (void)lift_out(*this, *viking);
                }
                saw.viking = kNobody;
            }
            if (!switches_on(world, argument)) {
                stay_active_under_a_viking(world);
                return;
            }
            set_off();
            go(world);
            return;
        }
        // On its way.
        if (!switches_on(world, argument)) {
            speed_y = 0;
            s.state = State::kWaiting;
            stay_active_under_a_viking(world);
            return;
        }
        static const TileTypes walls = tile_types({tile::kGround, tile::kPlatformStop, tile::kSpikes});
        static const TileTypes ceilings = tile_types({tile::kGround, tile::kPlatformStop});
        if (s.across) {
            if (find_beside(world, *this, s.forwards ? Side::kAhead : Side::kBehind, walls)) {
                s.forwards = !s.forwards;
                set_off();
            }
        } else if (s.down) {
            if (find_beside(world, *this, Side::kBelow, walls)) {
                s.down = false;
                set_off();
            } else if (s.squashes) {
                squash_below(world, *this);
            }
        } else if (find_beside(world, *this, Side::kAbove, ceilings)) {
            s.down = true;
            set_off();
        }
        go(world);
    }
    Looks looks_for() const override { return s.state == State::kWaiting ? Looks{touching_a_viking()} : Looks{}; }
    void outside_active_area(World& world) override { leave(world, *this); }

  private:
    enum class State : std::uint8_t { kStart, kWaiting, kMoving };

    void start(World& world) {
        const PlatformKind& mine = *platform_kind(kind);
        world.play(*this, mine.looks);
        s.stays_active = stays_active();
        s.across = mine.way == Way::kAcross;
        if (s.across) {
            s.crushes = mine.crushes;
            acts_as = s.crushes ? tile::kGround : tile::kPlatform;
            s.speed = static_cast<std::int16_t>(argument & kHighByte);
        } else {
            s.squashes = (argument & kTopBit) != 0;
            acts_as = (argument & kSolidBit) != 0 ? tile::kGround : tile::kPlatform;
            s.speed = static_cast<std::int16_t>(argument & kSpeedBits);
        }
        argument &= kLowByte;
        world.animate(*this);
        s.state = State::kWaiting;
    }

    void set_off() {
        s.state = State::kMoving;
        if (s.across) {
            speed_x = static_cast<std::int16_t>(s.forwards ? s.speed : -s.speed);
        } else {
            speed_y = static_cast<std::int16_t>(s.down ? s.speed : -s.speed);
        }
    }

    // The rest of a turn on its way.
    void go(World& world) {
        carry_vikings(world, *this);
        stay_active_under_a_viking(world);
        world.animate(*this);
        if (!s.crushes) {
            return;
        }
        const auto in_the_way = find_viking_beside(world, *this, s.forwards ? Side::kAhead : Side::kBehind, vikings());
        if (!in_the_way || in_the_way->entity < 0) {
            return;
        }
        target = in_the_way->entity;
        if (Entity* viking = world.at(target)) {
            tell(*viking, message::kCrushed, speed_x + (kind_value == 0 ? push_x : kind_value));
            kind_value = 0;
        }
    }

    // Coming down on a Viking that stands under it.
    void stay_active_under_a_viking(const World& world) {
        if (s.stays_active) {
            return;
        }
        const bool ridden = find_viking_beside(world, *this, Side::kAbove, vikings()).has_value();
        stay_active(ridden);
    }

    struct Saved {
        State state = State::kStart;
        bool stays_active = false;
        bool across = false;
        bool crushes = false;   // one that goes across: a Viking in its way is crushed
        bool squashes = false;  // one that goes up and down: a Viking under it
        bool forwards = true;
        bool down = true;
        std::int16_t speed = 0;
    } s;
    LVR_SAVES(Entity)
};

// A platform that goes round in a circle, always: twenty stretches of three ticks, each with its own speed across
// and down. Its argument is the stretch it starts with.
class CirclingPlatform : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            world.play(*this, platform_kind(kind)->looks);
            stay_active();
            acts_as = tile::kPlatform;
            next_stretch();
            count_down();
        } else if (countdown == 0) {
            next_stretch();
        }
        carry_vikings(world, *this);
        world.animate(*this);
    }
    void outside_active_area(World& world) override { leave(world, *this); }

  private:
    struct Speed {
        std::int16_t across;
        std::int16_t down;
    };
    static constexpr std::array<Speed, 20> kStretches{{
        {px(1), px(-5)},  {px(3), px(-4)},  {px(4), px(-3)},  {px(5), px(-1)},  {px(5), px(0)},
        {px(5), px(1)},   {px(4), px(3)},   {px(3), px(4)},   {px(1), px(5)},   {px(0), px(5)},
        {px(-1), px(5)},  {px(-3), px(4)},  {px(-4), px(3)},  {px(-5), px(1)},  {px(-5), px(0)},
        {px(-5), px(-1)}, {px(-4), px(-3)}, {px(-3), px(-4)}, {px(-1), px(-5)}, {px(0), px(-5)},
    }};
    static constexpr std::uint16_t kTicksAStretch = 3;

    void next_stretch() {
        if (argument < kStretches.size()) {
            const Speed& speed = kStretches.at(argument);
            speed_x = speed.across;
            speed_y = speed.down;
            argument = static_cast<std::uint16_t>((argument + 1U) % kStretches.size());
        }
        countdown = kTicksAStretch;
    }
};

// A lift: a platform in a shaft, between two or three stops. The Viking being played, standing on it, sends it up
// or down with the arrow keys; it carries whoever stands on it. Some only run while their switches are on. Where
// it is in its shaft is kept when it leaves the active area, and the shaft's cells there are solid meanwhile.
class Lift : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            world.play(*this, kind == kind::kOtherLift ? anim::kLiftOtherPlatform : anim::kLiftPlatform);
            set_cells(world, tile::kFree);
            acts_as = tile::kPlatform;
            // Its argument: which lift of the level it is, the switches it needs, and where it was left.
            s.switches = argument & kSwitchBits;
            const std::uint16_t left_at = argument & kLeftAtBits;
            argument &= kNumberBits;
            if (left_at == 0) {  // new: each lift starts at its top or its bottom, or between
                s.at_top = argument == 1 || argument == 2 || argument == 3 || argument == 5 || argument == 9 ||
                           argument == 12 || argument == 14;
                s.at_bottom = argument == 4 || argument == 6 || argument == 8 || argument == 11 || argument == 13 ||
                              argument == 15;
            } else {
                s.at_top = left_at == kLeftAtTop;
                s.at_bottom = left_at == kLeftAtBottom;
            }
            world.animate(*this);
            return;
        }
        if ((world.switches & s.switches) != s.switches) {
            speed_y = 0;
            world.animate(*this);
            return;
        }
        steer(world);
        bool ridden = false;
        for (const int rider : vikings_in_row(world, *this, Row::kAbove)) {
            carry(world, rider, ridden);
        }
        if (argument == kComesBackDown && !ridden) {
            go(world, false);
        }
        world.animate(*this);
    }
    void outside_active_area(World& world) override {
        set_cells(world, tile::kGround);
        if (placement >= 0) {
            Placement& mine = world.placements.at(static_cast<std::size_t>(placement));
            mine.x = x;
            mine.y = y;
            mine.flags = flags & entity_flag::kKeptInPlacement;
            const std::uint16_t left_at = s.at_bottom ? kLeftAtBottom : (s.at_top ? kLeftAtTop : kLeftBetween);
            mine.argument = static_cast<std::uint16_t>(unsigned{argument} | s.switches | left_at);
        }
        world.remove(slot);
    }

  private:
    static constexpr std::uint16_t kNumberBits = 0x00ff;
    static constexpr std::uint16_t kSwitchBits = 0x3f00;
    static constexpr std::uint16_t kLeftAtBits = 0xc000;
    static constexpr std::uint16_t kLeftAtTop = 0x4000;
    static constexpr std::uint16_t kLeftAtBottom = 0x8000;
    static constexpr std::uint16_t kLeftBetween = 0xc000;
    static constexpr std::uint16_t kComesBackDown = 15;  // the lift that goes down by itself when nobody is on it
    static constexpr std::int16_t kSpeed = px(4);

    // Where each of the spaceship levels' lifts stops: its top, a stop between (0: none) and its bottom.
    // (The factory's lifts run the same script, with the same table.)
    struct Stops {
        int top;
        int between;
        int bottom;
    };
    static constexpr std::array<Stops, 16> kStops{{{232, 440, 760},
                                                   {232, 408, 728},
                                                   {104, 0, 312},
                                                   {424, 0, 760},
                                                   {392, 0, 600},
                                                   {296, 792, 1096},
                                                   {216, 0, 344},
                                                   {248, 440, 648},
                                                   {152, 0, 728},
                                                   {152, 0, 728},
                                                   {840, 1016, 1192},
                                                   {200, 0, 744},
                                                   {136, 248, 488},
                                                   {472, 0, 776},
                                                   {88, 0, 376},
                                                   {440, 0, 616}}};

    void sound(World& world, std::uint8_t effect) const { world.sound(*this, effect); }
    void stop_sound(World& world, std::uint8_t effect) const { world.stop_sound(*this, effect); }

    // The two cells of the shaft it is in.
    void set_cells(World& world, std::uint8_t tile_type) const {
        const int column = cell_of(x - 4);
        const int row = cell_of(y + 4);
        for (const int at : {column, column + 1}) {
            if (at < world.map.width && row < world.map.height) {
                world.map.cells
                    .at((static_cast<std::size_t>(row) * static_cast<std::size_t>(world.map.width)) +
                        static_cast<std::size_t>(at))
                    .tile_type = tile_type;
            }
        }
    }

    void go(World& world, bool up) {
        if (up ? s.at_top : s.at_bottom) {
            return;
        }
        if (speed_y == (up ? -kSpeed : kSpeed)) {
            return;
        }
        stop_sound(world, up ? effect::kGoingDown : effect::kGoingUp);
        sound(world, up ? effect::kGoingUp : effect::kGoingDown);
        s.going_up = up;
        s.going_down = !up;
        s.just_set_off = true;
    }

    // On its way it is active wherever the view is; at a stop it halts.
    void steer(World& world) {
        stay_active();
        if (s.going_up) {
            speed_y = -kSpeed;
            s.at_bottom = false;
        } else if (s.going_down) {
            speed_y = kSpeed;
            s.at_top = false;
        } else {
            stay_active(false);
            return;
        }
        if (s.just_set_off) {
            s.just_set_off = false;
            return;
        }
        if (argument >= kStops.size()) {
            return;
        }
        const Stops& stops = kStops.at(argument);
        if (y == stops.top) {
            s.at_top = true;
            s.going_up = false;
            stop_sound(world, effect::kGoingUp);
        } else if (y == stops.bottom) {
            s.at_bottom = true;
            s.going_down = false;
            stop_sound(world, effect::kGoingDown);
        } else if (stops.between != 0 && y == stops.between) {
            s.going_up = s.going_down = false;
            stop_sound(world, effect::kGoingUp);
            stop_sound(world, effect::kGoingDown);
        } else {
            return;
        }
        speed_y = 0;
        sound(world, effect::kStopped);
        stay_active(false);
    }

    // A Viking over it goes with it; the one being played steers it.
    void carry(World& world, int rider, bool& ridden) {
        Entity* viking = world.at(rider);
        if (viking == nullptr || viking->x < box.left || box.right < viking->x) {
            return;
        }
        if (viking->box.bottom + 1 != box.top) {
            // Not standing on it but in it (it came up under him, or he fell into it): onto its top.
            if (static_cast<std::uint16_t>(viking->box.bottom) >= static_cast<std::uint16_t>(box.bottom) ||
                viking->hit_class != hit_class::kViking || viking->speed_y < 0) {
                return;
            }
            const int into = viking->box.bottom - box.top + 1;
            viking->push_y += speed_y - (into * 256) - viking->speed_y;
            viking->fraction_y = 0;
            return;
        }
        ridden = true;
        viking->push_y += speed_y;
        if (world.is_played(rider)) {
            if ((world.keys_of(rider).held & key::kUp) != 0) {
                go(world, true);
            } else if ((world.keys_of(rider).held & key::kDown) != 0) {
                go(world, false);
            }
        }
    }

    struct Saved {
        std::uint16_t switches = 0;  // it runs while these are on
        bool at_top = false;
        bool at_bottom = false;
        bool going_up = false;
        bool going_down = false;
        bool just_set_off = false;
    } s;
    LVR_SAVES(Entity)
};

// A spring: a Viking that comes down on it is thrown back up as fast as he came, and never slowly. While it is
// there its two cells are solid; when it leaves the active area they are spring cells again.
class Spring : public Entity {
  public:
    void update(World& world) override {
        switch (s.state) {
            case State::kStart:
                set_cells(world, tile::kGround);
                // A Viking already in it is put on top.
                for_each_viking_above(world, [this](Entity& viking) {
                    if (static_cast<std::uint16_t>(viking.box.bottom) < static_cast<std::uint16_t>(box.bottom)) {
                        viking.push_y =
                            static_cast<std::int16_t>(viking.push_y - ((viking.box.bottom - box.top + 1) * 256));
                    }
                });
                world.play(*this, anim::kSpringRests);
                acts_as = tile::kSpring;
                s.state = State::kResting;
                break;
            case State::kResting: {
                bool landed = false;
                for_each_viking_above(world,
                                      [&landed](const Entity& viking) { landed = landed || viking.speed_y >= 0; });
                if (!landed) {
                    break;
                }
                // It gives way under him ...
                world.play(*this, anim::kSpringGivesWay);
                for_each_viking_above(world, [](Entity& viking) {
                    if (viking.speed_y >= 0) {
                        viking.push_y += (kGive * 256);
                    }
                });
                box.top += kGive;
                s.state = State::kPressed;
                break;
            }
            case State::kPressed:
                // ... and throws him.
                world.sound(*this, effect::kSprung);
                for_each_viking_above(world, [](Entity& viking) {
                    if (viking.speed_y < 0) {
                        return;
                    }
                    viking.push_y -= (kGive * 256);
                    const int back = -viking.speed_y;
                    viking.speed_y = static_cast<std::int16_t>(back < kLeastThrow ? back : kLeastThrow);
                });
                s.state = State::kSprung;
                return;
            case State::kSprung:
                box.top -= kGive;
                s.state = State::kResting;
                break;
        }
        world.animate(*this);
    }
    void outside_active_area(World& world) override {
        set_cells(world, kSpringCells);
        world.remove(slot);
    }

  private:
    enum class State : std::uint8_t { kStart, kResting, kPressed, kSprung };
    static constexpr std::uint8_t kSpringCells = 22;
    static constexpr int kGive = 11;             // pixels it gives way
    static constexpr int kLeastThrow = px(-14);  // the least speed it throws at

    template <typename Each>
    void for_each_viking_above(World& world, const Each& each) {
        for (const int found : vikings_in_row(world, *this, Row::kAbove)) {
            if (Entity* viking = world.at(found)) {
                each(*viking);
            }
        }
    }

    void set_cells(World& world, std::uint8_t tile_type) const {
        const int column = cell_of(box.left);
        const int row = cell_of(y);
        for (const int at : {column, column + 1}) {
            if (at < world.map.width && row < world.map.height) {
                world.map.cells
                    .at((static_cast<std::size_t>(row) * static_cast<std::size_t>(world.map.width)) +
                        static_cast<std::size_t>(at))
                    .tile_type = tile_type;
            }
        }
    }

    struct Saved {
        State state = State::kStart;
    } s;
    LVR_SAVES(Entity)
};

}  // namespace

// The wacky world's platforms that only go one way, down or up at the speed in their argument, carrying the
// Vikings on them, until something solid is over them: then they are gone. One kind waits for its switches first,
// and can squash a Viking under it.
class OneWayPlatform : public Entity {
  public:
    void update(World& world) override {
        switch (s.state) {
            case State::kStart:
                return at_start(world);
            case State::kWaiting:
                if (!switches_on(world, argument)) {
                    return;
                }
                return go(world);
            case State::kGoing:
                return when_going(world);
        }
    }
    void at_start(World& world) {
        const bool switched = kind == kind::kSwitchedRisingPlatform;
        world.play(*this, switched ? anim::kOneWayPlatformSwitchedLooks : anim::kOneWayPlatformLooks);
        world.animate(*this);
        acts_as = tile::kPlatform;
        s.stays_active = stays_active();
        if (switched) {
            s.squashes = (argument & kTopBit) != 0;
            s.pace = static_cast<std::int16_t>(argument & kPaceBits);
            argument &= kLowByte;
            if (!switches_on(world, argument)) {
                s.state = State::kWaiting;
                return;
            }
        } else {
            s.pace = static_cast<std::int16_t>(argument & kHighByte);
        }
        return go(world);
    }
    void when_going(World& world) {
        static const TileTypes solid = tile_types({tile::kGround, tile::kGround02, tile::kGround20});
        if (find_beside(world, *this, Side::kAbove, solid)) {
            world.remove(slot);
            return;
        }
        if (s.squashes) {
            squash_below(world, *this);
        }
        return going(world);
    }

  private:
    enum class State : std::uint8_t { kStart, kWaiting, kGoing };

    void go(World& world) {
        speed_y = s.pace;
        going(world);
    }
    void going(World& world) {
        carry_vikings(world, *this, false);
        if (!s.stays_active) {
            const bool ridden = find_viking_beside(world, *this, Side::kAbove, vikings()).has_value();
            stay_active(ridden);
        }
        world.animate(*this);
        s.state = State::kGoing;
    }
    struct Saved {
        State state = State::kStart;
        bool stays_active = false;
        bool squashes = false;
        std::int16_t pace = 0;
    } s;
    LVR_SAVES(Entity)
};

std::unique_ptr<Entity> make_one_way_platform() {
    return make_entity<OneWayPlatform>();
}

std::unique_ptr<Entity> make_platform(std::uint16_t kind) {
    const PlatformKind* row = platform_kind(kind);
    if (row == nullptr) {
        return nullptr;
    }
    if (row->way == Way::kRound) {
        return make_entity<CirclingPlatform>();
    }
    return make_entity<MovingPlatform>();
}

std::span<const KindOf> carriers() {
    static constexpr KindOf kList[] = {
        {Home::kAnywhere, kind::kLift, make<Lift>},     {Home::kAnywhere, kind::kLift5F, make<Lift>},
        {Home::kAnywhere, kind::kLiftA8, make<Lift>},   {Home::kAnywhere, kind::kOtherLift, make<Lift>},
        {Home::kAnywhere, kind::kSpring, make<Spring>},
    };
    return kList;
}

}  // namespace lvr
