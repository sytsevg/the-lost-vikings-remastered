// What the switches work: the walls, doors, floors and bridges that open and close, the barriers and beams that
// go on and off, and the unseen things that pass a switch on.

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

// A cell that is solid while its switches are on, and something else (tile type 0A) while they are off.
class SwitchedCell : public Entity {
  public:
    void update(World& world) override {
        const bool on = switches_on(world, argument);
        switch (state_) {
            case State::kStart:
                world.play(*this, anim::kSwitchedCellAppears);
                world.animate(*this);
                if (on) {
                    turn_on(world);
                } else {
                    world.play(*this, anim::kSwitchedCellIsOff);
                    acts_as = kOffType;
                    state_ = State::kOff;
                }
                break;
            case State::kOff:
                if (on) {
                    turn_on(world);
                }
                break;
            case State::kOn:
                if (!on) {
                    world.play(*this, anim::kSwitchedCellGoesOff);
                    acts_as = kOffType;
                    set_cell(world, kOffType);
                    state_ = State::kOff;
                }
                break;
        }
        world.animate(*this);
    }

  private:
    enum class State : std::uint8_t { kStart, kOff, kOn };
    static constexpr std::uint8_t kOffType = 10;

    void set_cell(World& world, std::uint8_t tile_type) const {
        if (Cell* cell = world.map.cell(cell_of(x), cell_of(y))) {
            cell->tile_type = tile_type;
        }
    }
    void turn_on(World& world) {
        world.play(*this, anim::kSwitchedCellGoesOn);
        acts_as = tile::kGround;
        set_cell(world, tile::kGround);
        state_ = State::kOn;
    }

    State state_ = State::kStart;
};

// Two cells of the map that are solid while its switches are on (one kind reads the level's second set), and
// something else (tile type 0A, without a picture) while they are off.
class SwitchedCellPair : public Entity {
  public:
    void update(World& world) override {
        const std::uint16_t switches = kind == wacky_kind::kSwitchedCells ? world.switches : world.more_switches;
        const bool on = (switches & argument) == argument;
        if (!first_turn() && on == on_) {
            return;
        }
        on_ = on;
        const int column = cell_of(x - 8);
        const int row = cell_of(y);
        for (int i = 0; i < 2; ++i) {
            if (Cell* cell = world.map.cell(column + i, row)) {
                *cell = on ? Cell{.metatile = static_cast<std::uint16_t>(1 + i), .tile_type = tile::kGround}
                           : Cell{.metatile = 0, .tile_type = tile::kPlatformStop};
            }
        }
    }

  private:
    bool on_ = false;
};

// A wall that a switch opens, or a floor that a switch builds: when its switches come on it goes from its cell to
// the next cell of the same tile type (ahead, behind, above, below: the first that is), one a tick, and clears
// each (the opening kind) or makes each solid (the building kind), until there is none left.
class OpeningWall : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                stay_active();
                if (kind == kind::kOpeningWall) {
                    loud_ = (argument & kLoudBit) != 0;
                    argument &= static_cast<std::uint16_t>(~kLoudBit);
                }
                column_ = cell_of(x);
                row_ = cell_of(y);
                world.play(*this, kind == kind::kOpeningWall ? anim::kSwitchedCellsOpeningWaits
                                                             : anim::kSwitchedCellsBuildingWaits);
                state_ = State::kWaiting;
                break;
            case State::kWaiting:
                if (!switches_on(world, argument)) {
                    break;
                }
                type_ = type_at(world, column_, row_);
                change(world);
                break;
            case State::kGoing: {
                const int ahead = facing_left() ? -1 : 1;
                const std::array<std::array<int, 2>, 4> next{{{ahead, 0}, {-ahead, 0}, {0, -1}, {0, 1}}};
                bool found = false;
                for (const auto& [across, down] : next) {
                    if (type_at(world, column_ + across, row_ + down) == type_) {
                        column_ += across;
                        row_ += down;
                        push_x += (across * ahead * kCell);
                        push_y += (down * kCell);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    gone_for_good(world, *this);
                    return;
                }
                if (loud_) {
                    world.shake_x = {.amount = 3, .offset = 0, .ticks = 6};
                    rumble(world);
                }
                change(world);
                break;
            }
        }
        world.animate(*this);
    }

  private:
    enum class State : std::uint8_t { kStart, kWaiting, kGoing };
    static constexpr std::uint16_t kLoudBit = 0x8000;  // the opening kind: with a rumble and a shake
    static constexpr int kCell = px(16);               // 16 pixels, in 256ths

    static int type_at(const World& world, int column, int row) {
        return world.map.tile_type_at(static_cast<std::int16_t>(column * 16), static_cast<std::int16_t>(row * 16));
    }
    void rumble(World& world) const { world.sound(*this, effect::kRumble); }

    void change(World& world) {
        if (kind == kind::kBuildingFloor) {
            rumble(world);
        }
        if (column_ >= 0 && column_ < world.map.width && row_ >= 0 && row_ < world.map.height) {
            world.map.cells.at((static_cast<std::size_t>(row_) * static_cast<std::size_t>(world.map.width)) +
                               static_cast<std::size_t>(column_)) =
                kind == kind::kOpeningWall ? world.cleared_cell : world.filled_cell;
        }
        state_ = State::kGoing;
    }

    State state_ = State::kStart;
    bool loud_ = false;
    int column_ = 0;
    int row_ = 0;
    int type_ = 0;  // the tile type of the cells it goes through
};

// What differs between the walls that go: the two animations, and whether one stays solid while it goes.
struct WallThatGoesLooks {
    Animation stands;
    Animation goes;
    bool solid_while_it_goes = false;
};

// A wall that goes for good when its switches come on: Egypt's gate (solid until it is gone) and the wacky world's
// wall (open as soon as it starts to go). What differs between them is what the maker is given.
class WallThatGoes : public Entity {
  public:
    explicit WallThatGoes(const WallThatGoesLooks& looks) : looks_{looks} {}

    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                world.play(*this, looks_.stands);
                world.animate(*this);
                acts_as = tile::kGround;
                state_ = State::kStanding;
                return;
            case State::kStanding:
                if (!switches_on(world, argument)) {
                    return;
                }
                world.play(*this, looks_.goes);
                if (!looks_.solid_while_it_goes) {
                    acts_as = tile::kFree;
                }
                never_again(world, *this);
                state_ = State::kGoing;
                break;
            case State::kGoing:
                if (animation_over()) {
                    world.remove(slot);
                    return;
                }
                break;
        }
        world.animate(*this);
    }

  private:
    enum class State : std::uint8_t { kStart, kStanding, kGoing };

    WallThatGoesLooks looks_;
    State state_ = State::kStart;
};

// Egypt's gate: solid until it is gone.
class EgyptGate : public WallThatGoes {
  public:
    EgyptGate()
        : WallThatGoes({.stands = anim::kEgyptGateStands, .goes = anim::kEgyptGateGoes, .solid_while_it_goes = true}) {}
};

// The wacky world's wall: open as soon as it starts to go.
class WackyWall : public WallThatGoes {
  public:
    WackyWall()
        : WallThatGoes(
              {.stands = anim::kSwitchedWallStands, .goes = anim::kSwitchedWallGoes, .solid_while_it_goes = false}) {}
};

// A gate: solid while its switches are off, open while they are on. One kind stays open once it is.
class Gate : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                world.play(*this, anim::kGateAppears);
                stays_open_ = kind == prehistoric_kind::kGateThatStaysOpen;
                world.animate(*this);
                if (switches_on(world, argument)) {
                    world.play(*this, anim::kGateIsOpen);
                    open();
                } else {
                    world.play(*this, anim::kGateIsShut);
                    shut();
                }
                break;
            case State::kOpen:
                if (!stays_open_ && !switches_on(world, argument)) {
                    world.play(*this, anim::kGateShuts);
                    world.sound(*this, effect::kCreak);
                    shut();
                }
                break;
            case State::kShut:
                if (switches_on(world, argument)) {
                    world.sound(*this, effect::kCreak);
                    world.play(*this, anim::kGateOpens);
                    open();
                }
                break;
        }
        world.animate(*this);
    }

  private:
    enum class State : std::uint8_t { kStart, kOpen, kShut };

    void open() {
        acts_as = tile::kBody;
        hit_mask &= static_cast<std::uint16_t>(~hit::kStopsArrow);
        state_ = State::kOpen;
    }
    void shut() {
        acts_as = tile::kGround;
        hit_mask |= hit::kStopsArrow;
        state_ = State::kShut;
    }

    State state_ = State::kStart;
    bool stays_open_ = false;
};

// A hatch: open while its switches are on, solid while they are off.
class Hatch : public Entity {
  public:
    void update(World& world) override {
        const bool on = switches_on(world, argument);
        switch (state_) {
            case State::kStart:
                world.play(*this, anim::kHatchAppears);
                world.animate(*this);
                world.play(*this, on ? anim::kHatchIsOpen : anim::kHatchIsShut);
                acts_as = on ? 0 : tile::kGround;
                state_ = on ? State::kOpen : State::kShut;
                break;
            case State::kOpen:
                if (!on) {
                    world.play(*this, anim::kHatchShuts);
                    acts_as = tile::kGround;
                    state_ = State::kShut;
                }
                break;
            case State::kShut:
                if (on) {
                    world.play(*this, anim::kHatchOpens);
                    acts_as = tile::kFree;
                    state_ = State::kOpen;
                }
                break;
        }
        world.animate(*this);
    }

  private:
    enum class State : std::uint8_t { kStart, kOpen, kShut };

    State state_ = State::kStart;
};

// A door. One kind opens while a living Viking is near it; the other while its switches are on. Closed, it is a
// wall to everyone.
class Door : public Entity {
  public:
    void update(World& world) override {
        if (state_ == State::kStart) {
            world.play(*this, anim::kDoorFrame);
            world.animate(*this);
            if (kind == kind::kSwitchedDoor && wanted_open(world)) {
                world.play(*this, anim::kDoorStandsOpen);
                return stay(world, true);
            }
            world.play(*this, anim::kDoorStandsClosed);
            return stay(world, false);
        }
        const bool open = state_ == State::kOpen;
        if (wanted_open(world) != open) {
            world.play(*this, open ? anim::kDoorCloses : anim::kDoorOpens);
            world.sound(*this, effect::kDoor);
            return stay(world, !open);
        }
        world.animate(*this);
    }

  private:
    enum class State : std::uint8_t { kStart, kClosed, kOpen };
    static constexpr int kNearAcross = 32;
    static constexpr int kNearDown = 24;

    bool wanted_open(World& world) const {
        if (kind == kind::kSwitchedDoor) {
            return (argument & world.switches) == argument;
        }
        const World::Offset viking = world.offset_to(*this, world.nearest_viking(*this));
        return viking.below >= -kNearDown && viking.below < kNearDown && viking.ahead < kNearAcross &&
               viking.ahead >= -kNearAcross;
    }

    void stay(World& world, bool open) {
        state_ = open ? State::kOpen : State::kClosed;
        acts_as = open ? 0 : tile::kGround;
        hit_mask =
            static_cast<std::uint16_t>(open ? (hit_mask & ~unsigned{hit::kStopsArrow}) : (hit_mask | hit::kStopsArrow));
        world.animate(*this);
    }

    State state_ = State::kStart;
};

// A plug: when its switches come on, the cells above and below it open, and it is gone.
class Plug : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                world.play(*this, anim::kPlugStands);
                world.animate(*this);
                state_ = State::kStanding;
                return;
            case State::kStanding: {
                if (!switches_on(world, argument)) {
                    return;
                }
                world.play(*this, anim::kPlugGoes);
                const int column = cell_of(x);
                const int row = cell_of(y);
                for (const int at : {row - 1, row + 1}) {
                    if (Cell* cell = world.map.cell(column, at)) {
                        cell->tile_type = tile::kFree;
                    }
                }
                never_again(world, *this);
                state_ = State::kGoing;
                break;
            }
            case State::kGoing:
                if (animation_over()) {
                    world.remove(slot);
                    return;
                }
                break;
        }
        world.animate(*this);
    }

  private:
    enum class State : std::uint8_t { kStart, kStanding, kGoing };

    State state_ = State::kStart;
};

// A floor of spikes over six cells: a Viking on it is impaled. When its switches come on it folds away.
class SpikedFloor : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                world.play(*this, anim::kSpikedFloorLooks);
                world.animate(*this);
                acts_as = tile::kSpikes;
                state_ = State::kOut;
                return;
            case State::kOut: {
                for (const int on_it : vikings_in_row(world, *this, Row::kAbove)) {
                    world.tell(on_it, message::kImpaled);
                }
                if (!switches_on(world, argument)) {
                    return;
                }
                stay_active();
                never_again(world, *this);
                const int column = cell_of(box.left + 4);
                const int row = cell_of(y);
                for (int along = 0; along < 6; ++along) {
                    if (Cell* cell = world.map.cell(column + along, row)) {
                        cell->tile_type = tile::kFree;
                    }
                }
                world.play(*this, anim::kSpikedFloorFoldsAway);
                state_ = State::kFolding;
                break;
            }
            case State::kFolding:
                if (animation_over()) {
                    world.remove(slot);
                    return;
                }
                break;
        }
        world.animate(*this);
    }

  private:
    enum class State : std::uint8_t { kStart, kOut, kFolding };

    State state_ = State::kStart;
};

// A bridge that stands on end, solid, until its switches are on; then it falls over, to the side it faces, and
// becomes four cells of floor. One kind of it stays as an entity; the other becomes part of the map for good.
class FallingBridge : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                return at_start(world);
            case State::kStanding:
                return when_standing(world);
            case State::kFalling:
                if (animation_over()) {
                    return lie_down(world);
                }
                world.animate(*this);
                return;
            case State::kLying:
                return;
        }
    }
    void at_start(World& world) {
        world.play(*this, anim::kFallingBridgeAppears);
        world.animate(*this);
        stays_ = (argument & kTopBit) != 0;
        argument &= ~kTopBit;
        acts_as = tile::kGround;
        if (switches_on(world, argument)) {
            world.play(*this, anim::kFallingBridgeLies);
            world.animate(*this);
            set_upright_cells(world, tile::kFree);
            return lie_down(world);
        }
        world.play(*this, anim::kFallingBridgeStands);
        world.animate(*this);
        set_upright_cells(world, tile::kGround);
        box = {.left = x, .top = y, .right = x, .bottom = y};
        state_ = State::kStanding;
        world.animate(*this);
    }
    void when_standing(World& world) {
        if (!switches_on(world, argument)) {
            world.animate(*this);
            return;
        }
        set_upright_cells(world, tile::kFree);
        world.play(*this, anim::kFallingBridgeFalls);
        state_ = State::kFalling;
        world.animate(*this);
    }

  private:
    enum class State : std::uint8_t { kStart, kStanding, kFalling, kLying };
    static constexpr int kCells = 4;

    // The four cells above its foot.
    void set_upright_cells(World& world, std::uint8_t tile_type) const {
        const int column = cell_of(x);
        const int row = cell_of(y);
        for (int up = 1; up <= kCells; ++up) {
            if (Cell* cell = world.map.cell(column, row - up)) {
                cell->tile_type = tile_type;
            }
        }
    }

    void lie_down(World& world) {
        box.top = static_cast<std::int16_t>(y - 8);
        box.bottom = static_cast<std::int16_t>(y + 8);
        box.left = static_cast<std::int16_t>(facing_left() ? x - 72 : x + 8);
        box.right = static_cast<std::int16_t>(facing_left() ? x - 8 : x + 72);
        const int column = cell_of(box.left + 4);
        const int row = cell_of(y);
        for (int along = 0; along < kCells; ++along) {
            if (Cell* cell = world.map.cell(column + along, row)) {
                if (stays_) {
                    cell->tile_type = tile::kGround;
                } else {
                    *cell = Cell{.metatile = 1, .tile_type = tile::kGround};
                }
            }
        }
        if (stays_) {
            state_ = State::kLying;
            return;
        }
        gone_for_good(world, *this);
    }

    State state_ = State::kStart;
    bool stays_ = false;
};

// A bridge of light, four cells wide: there while its switches are on. When they go off (or were off from the
// start) its cells are solid for good and it shows that.
class LightBridge : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                world.play(*this, anim::kLightBridgeAppears);
                world.animate(*this);
                if (!switches_on(world, argument)) {
                    return close(world);
                }
                acts_as = tile::kRotatingSpikes;
                world.play(*this, anim::kLightBridgeShines);
                world.animate(*this);
                state_ = State::kOn;
                return;
            case State::kOn: {
                if (switches_on(world, argument)) {
                    world.animate(*this);
                    return;
                }
                const int column = cell_of(x - 24);
                const int row = cell_of(y - 8);
                for (int across = 0; across < 4; ++across) {
                    if (Cell* cell = world.map.cell(column + across, row)) {
                        cell->tile_type = tile::kGround;
                    }
                }
                return close(world);
            }
            case State::kClosed:
                return;
        }
    }

  private:
    enum class State : std::uint8_t { kStart, kOn, kClosed };

    void close(World& world) {
        acts_as = tile::kGround;
        world.play(*this, anim::kLightBridgeSolid);
        world.animate(*this);
        state_ = State::kClosed;
    }

    State state_ = State::kStart;
};

// A deadly surface (tile type 12) that drains away for good when its switches come on.
class DeadlyPool : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                world.play(*this, anim::kDeadlyPoolLooks);
                world.animate(*this);
                acts_as = tile::kLava;
                state_ = State::kFull;
                return;
            case State::kFull:
                if (!switches_on(world, argument)) {
                    return;
                }
                acts_as = tile::kBody;
                world.sound(*this, effect::kPoolDrains);
                world.play(*this, anim::kDeadlyPoolDrains);
                never_again(world, *this);
                state_ = State::kDraining;
                break;
            case State::kDraining:
                if (animation_over()) {
                    world.remove(slot);
                    return;
                }
                break;
        }
        world.animate(*this);
    }

  private:
    enum class State : std::uint8_t { kStart, kFull, kDraining };

    State state_ = State::kStart;
};

// An electric barrier across a passage, on while its switches are. A Viking that touches it is electrocuted.
// Switched off, it fades and is gone for good. Three kinds: the barrier that also blocks its four cells; a field
// that blocks nothing and electrocutes whoever touches it, Viking or not; and an unseen one.
class Barrier : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                return at_start(world);
            case State::kOn:
                return when_on(world);
            case State::kFading:
                return when_fading(world);
        }
    }
    void at_start(World& world) {
        if (!on(world)) {
            if (kind != kind::kElectricField) {
                free_cells(world);
            }
            gone(world);
            world.remove(slot);
            return;
        }
        if (kind != kind::kUnseenBarrier) {
            world.play(*this, kind == kind::kElectricField ? anim::kBarrierFieldHumming : anim::kBarrierHumming);
        }
        acts_as = tile::kBarrier;
        state_ = State::kOn;
        if (kind != kind::kUnseenBarrier) {
            world.animate(*this);
        }
    }
    void when_on(World& world) {
        if (saw.viking != kNobody) {
            target = saw.viking;
            world.tell(saw.viking, message::kElectrocuted);
        }
        if (!on(world)) {
            if (kind == kind::kUnseenBarrier) {
                world.remove(slot);
                return;
            }
            world.play(*this, kind == kind::kElectricField ? anim::kBarrierFieldFading : anim::kBarrierFading);
            if (kind == kind::kBarrier) {
                free_cells(world);
            }
            gone(world);
            state_ = State::kFading;
        }
        if (kind != kind::kUnseenBarrier) {
            world.animate(*this);
        }
    }
    void when_fading(World& world) {
        if (animation_over()) {  // the fade has played
            world.remove(slot);
            return;
        }
        world.animate(*this);
    }
    Looks looks_for() const override {
        return state_ == State::kOn
                   ? Looks{touching_any(hit::kVikingInField, &Saw::viking, kind != kind::kElectricField)}
                   : Looks{};
    }

  private:
    enum class State : std::uint8_t { kStart, kOn, kFading };

    // Its argument names its switches: on while all of them are.
    bool on(const World& world) const { return (argument & world.switches) == argument; }

    void gone(World& world) const { never_again(world, *this); }

    // The four cells it spans: two each side of its middle.
    void free_cells(World& world) const {
        const int column = cell_of(x);
        const int row = cell_of(y);
        for (const int at : {column, column + 1, column - 2, column - 1}) {
            if (at >= 0 && at < world.map.width && row < world.map.height) {
                world.map.cells
                    .at((static_cast<std::size_t>(row) * static_cast<std::size_t>(world.map.width)) +
                        static_cast<std::size_t>(at))
                    .tile_type = tile::kFree;
            }
        }
    }

    State state_ = State::kStart;
};

// A beam that is there while its switches are on: a Viking who touches it is electrocuted (one kind: anything
// that can be). Switched off it fades and is unseen.
class SwitchedBeam : public Entity {
  public:
    void update(World& world) override {
        const bool wanted = switches_on(world, argument);
        switch (state_) {
            case State::kStart:
                if (wanted) {
                    turn_on(world);
                } else {
                    turn_off(world);
                }
                break;
            case State::kOn:
                if (saw.other != kNobody) {
                    target = saw.other;
                    world.tell(saw.other, message::kElectrocuted);
                }
                if (!wanted) {
                    world.play(*this,
                               kind == kind::kSwitchedBeam ? anim::kSwitchedBeamFades : anim::kSwitchedBeamFadesToo);
                    state_ = State::kFading;
                }
                break;
            case State::kFading:
                if (animation_over()) {
                    turn_off(world);
                }
                break;
            case State::kOff:
                if (wanted) {
                    turn_on(world);
                }
                break;
        }
        world.animate(*this);
    }
    Looks looks_for() const override {
        return state_ == State::kOn
                   ? Looks{touching_any(hit::kVikingInField, &Saw::other, kind == kind::kOtherSwitchedBeam)}
                   : Looks{};
    }

  private:
    enum class State : std::uint8_t { kStart, kOn, kFading, kOff };

    void turn_on(World& world) {
        world.show_pieces(*this);
        world.play(*this, kind == kind::kSwitchedBeam ? anim::kSwitchedBeamShines : anim::kSwitchedBeamShinesToo);
        acts_as = tile::kBarrier;
        state_ = State::kOn;
    }
    void turn_off(World& world) {
        acts_as = tile::kFree;
        world.hide_pieces(*this);
        state_ = State::kOff;
    }

    State state_ = State::kStart;
};

// A place without gravity, on while its switches are: a Viking in it is drawn upwards, and the first of the level's
// colour cycles runs (the stream is seen). Its argument: which of the game's places it is, and its switches.
class Weightlessness : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            place(static_cast<std::uint8_t>(argument & kLowByte));
            argument &= kHighByte;
        } else if (on_) {
            stay_active(false);
            if (wanted(world)) {
                for (const int viking : {1, 0, 2}) {
                    draw_up(world, viking);
                }
                return;
            }
        } else if (!wanted(world)) {
            return;
        }
        on_ = wanted(world);
        if (!world.cycles.empty()) {
            world.cycles.front().running = on_;
        }
        hit_class = on_ ? hit_class::kAirStream : 0;
    }

  private:
    static constexpr int kPull = px(1);  // off his speed down, every tick

    bool wanted(const World& world) const { return (argument & world.switches) == argument; }

    // Place 0 is around where it stands; the others are written out.
    // Only the spaceship's levels have these places.
    void place(std::uint8_t number) {
        struct Place {
            std::uint8_t number;
            Box box;
        };
        static constexpr std::array<Place, 19> kPlaces{{
            {1, {256, 16, 384, 144}},     {2, {448, 16, 1120, 144}},    {7, {16, 32, 112, 288}},
            {8, {288, 32, 384, 800}},     {9, {16, 384, 288, 768}},     {10, {16, 320, 144, 384}},
            {11, {752, 480, 816, 672}},   {12, {128, 416, 384, 480}},   {13, {224, 1072, 384, 1408}},
            {14, {608, 1072, 768, 1408}}, {15, {736, 32, 880, 448}},    {16, {304, 32, 400, 176}},
            {17, {80, 560, 224, 976}},    {20, {272, 16, 304, 256}},    {21, {80, 480, 208, 544}},
            {22, {384, 416, 544, 480}},   {24, {1312, 224, 1376, 608}}, {25, {1536, 80, 1600, 608}},
            {26, {1328, 784, 1408, 848}},
        }};
        if (number == 0) {
            box = box_of(x - 32, y - 128, x + 32, y + 128);
            return;
        }
        for (const Place& known : kPlaces) {
            if (known.number == number) {
                box = known.box;
            }
        }
    }

    // A Viking whose middle is inside: a standing one is told to let go of the ground; all are pulled up.
    void draw_up(World& world, int slot_of) {
        Entity* viking = world.at(slot_of);
        if (viking == nullptr || viking->hit_class != hit_class::kViking || viking->ignores_weightlessness) {
            return;
        }
        const auto vx = static_cast<std::uint16_t>(viking->x);
        const auto vy = static_cast<std::uint16_t>(viking->y);
        if (vy < static_cast<std::uint16_t>(box.top) || static_cast<std::uint16_t>(box.bottom) < vy ||
            vx < static_cast<std::uint16_t>(box.left) || static_cast<std::uint16_t>(box.right) < vx) {
            return;
        }
        target = slot_of;
        if (!viking->is_falling()) {
            viking->message = message::kDropped;
            world.sound(*this, effect::kLifted);
            viking->speed_y -= kPull;
        }
        stay_active();
        viking->speed_y -= kPull;
    }

    bool on_ = false;
};

// A thing that, once its switches (its argument's low byte) are on, plays its animation through and then turns
// on the switches of its argument's high byte.
class SwitchRelay : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                world.play(*this, anim::kSwitchRelayAppears);
                world.animate(*this);
                turns_on_ = argument & kHighByte;
                argument &= kLowByte;
                if (switches_on(world, argument)) {
                    world.play(*this, anim::kSwitchRelayIsDone);
                    world.animate(*this);
                    state_ = State::kDone;
                    return;
                }
                world.play(*this, anim::kSwitchRelayWaits);
                state_ = State::kWaiting;
                break;
            case State::kWaiting:
                if (switches_on(world, argument)) {
                    world.sound(*this, effect::kWorks);
                    world.play(*this, anim::kSwitchRelayGoes);
                    state_ = State::kGoing;
                }
                break;
            case State::kGoing:
                if (animation_over()) {
                    world.switches |= turns_on_;
                    state_ = State::kDone;
                    return;
                }
                break;
            case State::kDone:
                return;
        }
        world.animate(*this);
    }

  private:
    enum class State : std::uint8_t { kStart, kWaiting, kGoing, kDone };

    State state_ = State::kStart;
    std::uint16_t turns_on_ = 0;
};

// An unseen thing that turns on four switches in turn (bits 8 to 11, those of them its argument has), ten ticks
// each, again and again.
class SwitchSequence : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            count_down();
            world.switches &= static_cast<std::uint16_t>(~argument);
            bit_ = kLastSwitch;
        } else {
            if (countdown != 0) {
                return;
            }
            world.switches &= static_cast<std::uint16_t>(~argument);
            bit_ = bit_ == kLastSwitch ? kFirstSwitch : static_cast<std::uint16_t>(bit_ << 1U);
        }
        world.switches |= static_cast<std::uint16_t>(bit_ & argument);
        countdown = 10;
    }

  private:
    static constexpr std::uint16_t kFirstSwitch = 0x0100;  // in World::switches: it lights these four in turn
    static constexpr std::uint16_t kLastSwitch = 0x0800;
    std::uint16_t bit_ = 0;
};

// The ground shakes every so often (one of four waits, at random), until its switches are on.
class Quake : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            count_down();
            stay_active();
            wait(world);
            return;
        }
        if (countdown == 0) {
            world.shake_x = {.amount = 2, .offset = 0, .ticks = 10};
            world.shake_y = {.amount = 2, .offset = 0, .ticks = 20};
            world.sound(*this, effect::kRumble53);
            wait(world);
            return;
        }
        if (switches_on(world, argument)) {
            gone_for_good(world, *this);
        }
    }

  private:
    void wait(World& world) {
        const std::uint16_t dice = world.random();
        countdown = dice < 16384 ? 660 : (dice < 32768 ? 560 : (dice < 49152 ? 460 : 360));
    }
};

// Where an item (its argument's low byte) drops out, thirty ticks after its switches (the high byte) come on.
class Dispenser : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                item_ = argument & kLowByte;
                argument &= kHighByte;
                state_ = State::kWaiting;
                return;
            case State::kWaiting:
                if (switches_on(world, argument)) {
                    world.sound(*this, effect::kDispenserWorks);
                    count_down_from(30);
                    state_ = State::kWorking;
                }
                return;
            case State::kWorking:
                if (countdown != 0) {
                    return;
                }
                world.spawn(*this, kind::kItem, x, y, bits(item_, item::kFalling), entity_flag::kAlwaysActive);
                gone_for_good(world, *this);
                return;
        }
    }

  private:
    enum class State : std::uint8_t { kStart, kWaiting, kWorking };

    State state_ = State::kStart;
    std::uint16_t item_ = 0;
};

}  // namespace

std::span<const KindOf> switched() {
    static constexpr KindOf kList[] = {
        {Home::kEgypt, egypt_kind::kSwitchedCell, make<SwitchedCell>},
        {Home::kWacky, wacky_kind::kSwitchedCells, make<SwitchedCellPair>},
        {Home::kWacky, wacky_kind::kSwitchedCellsOfSecondSet, make<SwitchedCellPair>},
        {Home::kAnywhere, kind::kOpeningWall, make<OpeningWall>},
        {Home::kAnywhere, kind::kBuildingFloor, make<OpeningWall>},
        {Home::kEgypt, egypt_kind::kGate, make<EgyptGate>},
        {Home::kWacky, wacky_kind::kWall, make<WackyWall>},
        {Home::kPrehistoric, prehistoric_kind::kGate, make<Gate>},
        {Home::kPrehistoric, prehistoric_kind::kGateThatStaysOpen, make<Gate>},
        {Home::kFactory, factory_kind::kHatch, make<Hatch>},
        {Home::kAnywhere, kind::kDoor, make<Door>},
        {Home::kAnywhere, kind::kSwitchedDoor, make<Door>},
        {Home::kWacky, wacky_kind::kPlug, make<Plug>},
        {Home::kEgypt, egypt_kind::kSpikedFloor, make<SpikedFloor>},
        {Home::kPrehistoric, prehistoric_kind::kFallingBridge, make<FallingBridge>},
        {Home::kAnywhere, kind::kLightBridge, make<LightBridge>},
        {Home::kPrehistoric, prehistoric_kind::kDeadlyPool, make<DeadlyPool>},
        {Home::kAnywhere, kind::kBarrier, make<Barrier>},
        {Home::kAnywhere, kind::kElectricField, make<Barrier>},
        {Home::kAnywhere, kind::kUnseenBarrier, make<Barrier>},
        {Home::kAnywhere, kind::kSwitchedBeam, make<SwitchedBeam>},
        {Home::kAnywhere, kind::kOtherSwitchedBeam, make<SwitchedBeam>},
        {Home::kAnywhere, kind::kWeightlessness, make<Weightlessness>},
        {Home::kPrehistoric, prehistoric_kind::kSwitchRelay, make<SwitchRelay>},
        {Home::kAnywhere, kind::kSwitchSequence, make<SwitchSequence>},
        {Home::kPrehistoric, prehistoric_kind::kQuake, make<Quake>},
        {Home::kFactory, factory_kind::kDispenser, make<Dispenser>},
    };
    return kList;
}

}  // namespace lvr
