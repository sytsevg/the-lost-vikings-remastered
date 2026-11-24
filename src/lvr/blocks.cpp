// The blocks: the ones the Vikings push, the ones that fall, drop or sink, and the walls and ledges that break.

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

// The four cells a block is in become a block of the map.
void block_into_map(World& world, const Entity& entity) {
    const int column = cell_of(entity.x - 8);
    const int row = cell_of(entity.y - 8);
    const std::array<std::array<int, 3>, 4> cells{{{0, 0, 1}, {1, 0, 2}, {1, 1, 5}, {0, 1, 4}}};
    for (const auto& [across, down, metatile] : cells) {
        if (Cell* cell = world.map.cell(column + across, row + down)) {
            *cell = Cell{.metatile = static_cast<std::uint16_t>(metatile), .tile_type = tile::kGround};
        }
    }
}

// A block come to rest for good: it becomes a block of the map, and is gone.
void become_map(World& world, Entity& entity) {
    block_into_map(world, entity);
    gone_for_good(world, entity);
}

// A block the Vikings push. Pushed off an edge it falls (on a Viking: he is squashed; on Olaf's raised shield it
// rests), and where it lands it becomes part of the map.
class SettlingBlock : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                world.play(*this, anim::kEgyptPushedBlockLooks);
                stays_active_ = stays_active();
                acts_as = tile::kGround;
                return rest(world);
            case State::kResting:
                return when_resting(world);
            case State::kFalling:
                return when_falling(world);
        }
    }
    void when_resting(World& world) {
        if (saw.wall) {
            speed_x = 0;
        }
        pushed_ = message == message::kPushed && take_push(world, *this);
        message = 0;
        scrape_while_pushed(world, *this, pushed_);
        if (!find_beside(world, *this, Side::kBelow, ground())) {
            speed_x = 0;
            start_falling();
            stay_active();
            stop_scraping(world, *this);
            state_ = State::kFalling;
            world.animate(*this);
            return;
        }
        return rest(world);
    }
    void when_falling(World& world) {
        if (saw.on_shield) {
            stop_falling();
            return rest(world);
        }
        if (saw.landed) {
            stop_falling();
            world.sound(*this, effect::kBlockLands);
            return become_map(world, *this);
        }
        squash_below(world, *this);
        world.animate(*this);
    }
    Looks looks_for() const override {
        switch (state_) {
            case State::kResting:
                return {stopped_by(solid())};
            case State::kFalling:
                return {lands_on(shield_overhead(), &Saw::on_shield), lands_on(solid())};
            default:
                return {};
        }
    }
    void outside_active_area(World& world) override { leave(world, *this); }

  private:
    enum class State : std::uint8_t { kStart, kResting, kFalling };

    void rest(World& world) {
        stay_active_under_a_viking(world, *this, stays_active_);
        world.animate(*this);
        state_ = State::kResting;
    }

    State state_ = State::kStart;
    bool stays_active_ = false;
    bool pushed_ = false;
};

// A block the Vikings push. Off an edge it falls (a Viking under it is squashed) and lands, to be pushed on.
// One kind can't be moved until its switches are on.
class PushedBlock : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                return at_start(world);
            case State::kFixed:
                if (switches_on(world, argument)) {
                    return rest(world);
                }
                world.animate(*this);
                return;
            case State::kResting:
                return when_resting(world);
            case State::kFalling:
                return when_falling(world);
        }
    }
    void at_start(World& world) {
        stays_active_ = stays_active();
        world.play(*this, kind == wacky_kind::kPushedBlockWaitingForSwitch ? anim::kPushedBlockFixedLooks
                                                                           : anim::kWackyPushedBlockLooks);
        acts_as = tile::kGround;
        if (is_falling()) {
            return fall(world);
        }
        if (kind == wacky_kind::kPushedBlockWaitingForSwitch) {
            state_ = State::kFixed;
            world.animate(*this);
            return;
        }
        return rest(world);
    }
    void when_resting(World& world) {
        if (saw.wall) {
            speed_x = 0;
        }
        pushed_ = message == message::kPushed && take_push(world, *this);
        message = 0;
        scrape_while_pushed(world, *this, pushed_);
        if (!find_beside(world, *this, Side::kBelow, ground())) {
            return fall(world);
        }
        return rest(world);
    }
    void when_falling(World& world) {
        if (saw.wall) {
            speed_x = 0;
        }
        if (saw.landed) {
            stay_active(stays_active_);
            stop_falling();
            world.sound(*this, effect::kBlockLands);
            return rest(world);
        }
        squash_below(world, *this);
        world.animate(*this);
    }
    Looks looks_for() const override {
        switch (state_) {
            case State::kResting:
                return {stopped_by(solid())};
            case State::kFalling:
                return {stopped_by(solid()), lands_on(ground())};
            default:
                return {};
        }
    }
    void outside_active_area(World& world) override { leave(world, *this); }

  private:
    enum class State : std::uint8_t { kStart, kFixed, kResting, kFalling };

    void rest(World& world) {
        world.animate(*this);
        stay_active_under_a_viking(world, *this, stays_active_);
        state_ = State::kResting;
    }

    void fall(World& world) {
        start_falling();
        stay_active();
        stop_scraping(world, *this);
        state_ = State::kFalling;
        world.animate(*this);
    }

    State state_ = State::kStart;
    bool stays_active_ = false;
    bool pushed_ = false;
};

// A block that is solid and falls when nothing is under it. A Viking it comes down on is squashed; not Olaf
// under his shield.
class FallingBlock : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                world.play(*this, kind == prehistoric_kind::kOtherFallingBlock ? anim::kFallingBlockOtherLooks
                                                                               : anim::kFallingBlockLooks);
                world.animate(*this);
                acts_as = tile::kGround;
                if (!is_falling()) {
                    state_ = State::kResting;
                    return;
                }
                break;
            case State::kResting:
                if (find_beside(world, *this, Side::kBelow, ground())) {
                    return;
                }
                break;
            case State::kFalling:
                if (saw.landed) {
                    stop_falling();
                    world.sound(*this, effect::kThud);
                    state_ = State::kResting;
                    return;
                }
                squash(world);
                world.animate(*this);
                return;
        }
        start_falling();
        state_ = State::kFalling;
        world.animate(*this);
    }
    Looks looks_for() const override { return state_ == State::kFalling ? Looks{lands_on(ground())} : Looks{}; }
    void outside_active_area(World& world) override { leave(world, *this); }

  private:
    enum class State : std::uint8_t { kStart, kResting, kFalling };

    void squash(World& world) {
        for (const int under : vikings_in_row(world, *this, Row::kBelow)) {
            if (under == slot::kOlaf && olaf_holds_shield_up(world)) {
                break;
            }
            Entity* other = world.at(under);
            if (other == nullptr || speed_y <= 0 || other->hit_class != hit_class::kViking ||
                other->box.top < box.bottom - 16) {
                continue;
            }
            tell(*other, message::kSquashed, speed_y);
        }
    }

    State state_ = State::kStart;
};

// A block that falls when its switches come on, taking the Vikings on it along and squashing one under it;
// where it lands on solid ground its four cells of the map become solid.
class DroppedBlock : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                world.play(*this, anim::kDroppedBlockLooks);
                acts_as = tile::kGround;
                if (is_falling()) {
                    return fall(world);
                }
                state_ = State::kWaiting;
                break;
            case State::kWaiting:
                if (switches_on(world, argument)) {
                    return fall(world);
                }
                break;
            case State::kFalling:
                if (saw.landed) {
                    stop_falling();
                    stay_active(false);
                    world.sound(*this, effect::kThud);
                    count_down_from(4);
                    state_ = State::kSettling;
                    return;
                }
                if (saw.held) {
                    speed_y = 0;
                }
                squash_below(world, *this);
                take_riders_down(world, *this);
                break;
            case State::kSettling: {
                if (countdown != 0) {
                    return;
                }
                const int column = cell_of(x - 8);
                const int row = cell_of(y - 8);
                for (int i = 0; i < 4; ++i) {
                    if (Cell* cell = world.map.cell(column + (i % 2), row + (i / 2))) {
                        cell->tile_type = tile::kGround;
                    }
                }
                stay_active();
                state_ = State::kSettled;
                return;
            }
            case State::kSettled:
                return;
        }
        world.animate(*this);
    }
    // Falling: solid ground ends the fall; anything else that can be stood on only holds it.
    Looks looks_for() const override {
        return state_ == State::kFalling ? Looks{lands_on(solid()), lands_on(ground(), &Saw::held)} : Looks{};
    }
    void outside_active_area(World& world) override { leave(world, *this); }

  private:
    enum class State : std::uint8_t { kStart, kWaiting, kFalling, kSettling, kSettled };

    void fall(World& world) {
        start_falling();
        stay_active();
        state_ = State::kFalling;
        take_riders_down(world, *this);
        world.animate(*this);
    }

    State state_ = State::kStart;
};

// A block that drops again and again while its switches are on: it crumbles away from the top for 60 ticks, is
// back whole 64 pixels up, and comes down, squashing a Viking under it, until it lands. With nothing under it
// it comes down too.
class DroppingBlock : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                world.play(*this, anim::kDroppingBlockLooks);
                acts_as = tile::kGround;
                state_ = State::kResting;
                break;
            case State::kResting:
                if (switches_on(world, argument)) {
                    world.play(*this, anim::kDroppingBlockCrumbles);
                    count_down_from(60);
                    stay_active();
                    state_ = State::kCrumbling;
                } else if (!find_beside(world, *this, Side::kBelow, floors())) {
                    drop(world);
                }
                break;
            case State::kCrumbling:
                if (countdown == 0) {
                    count_down(false);
                    acts_as = tile::kFree;
                    speed_y = px(-64);
                    state_ = State::kBack;
                } else if (static_cast<std::uint16_t>(box.top) < static_cast<std::uint16_t>(box.bottom)) {
                    box.top += 4;
                }
                break;
            case State::kBack:
                acts_as = tile::kGround;
                box.top = static_cast<std::int16_t>(y - 32);
                drop(world);
                break;
            case State::kFalling:
                if (saw.landed) {
                    world.sound(*this, effect::kThud);
                    stay_active(false);
                    speed_y = 0;
                    state_ = State::kResting;
                } else {
                    squash_below(world, *this);
                }
                break;
        }
        world.animate(*this);
    }
    Looks looks_for() const override { return state_ == State::kFalling ? Looks{lands_on(floors())} : Looks{}; }

  private:
    enum class State : std::uint8_t { kStart, kResting, kCrumbling, kBack, kFalling };

    static const TileTypes& floors() {
        static const TileTypes types = tile_types({tile::kGround, tile::kPlatform, tile::kShieldOverhead});
        return types;
    }
    void drop(World& world) {
        speed_y = px(2);
        world.play(*this, anim::kDroppingBlockFalls);
        state_ = State::kFalling;
    }

    State state_ = State::kStart;
};

// A block that sinks under a Viking who stands on it, down to the next floor, and stays there until its
// switches come on: then it falls to the ground, and becomes part of the map where it lands.
class SinkingBlock : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                world.play(*this, anim::kSinkingBlockLooks);
                acts_as = tile::kGround;
                if (is_falling()) {
                    return fall(world);
                }
                state_ = State::kWaiting;
                break;
            case State::kWaiting:
                if (find_viking_beside(world, *this, Side::kAbove, vikings())) {
                    speed_y = kSpeed;
                    state_ = State::kSinking;
                    take_riders_down(world, *this);
                }
                break;
            case State::kSinking: {
                static const TileTypes floors = tile_types({tile::kGround, tile::kPlatformStop, tile::kSpikes});
                if (find_beside(world, *this, Side::kBelow, floors)) {
                    speed_y = 0;
                    state_ = State::kSunk;
                    break;
                }
                take_riders_down(world, *this);
                break;
            }
            case State::kSunk:
                if (switches_on(world, argument)) {
                    return fall(world);
                }
                break;
            case State::kFalling:
            case State::kFallingOn:
                if (saw.landed) {
                    if (state_ == State::kFallingOn) {
                        stop_falling();
                        world.sound(*this, effect::kThud);
                        state_ = State::kLanded;
                        break;
                    }
                    state_ = State::kFallingOn;
                } else {
                    squash_below(world, *this);
                }
                take_riders_down(world, *this);
                break;
            case State::kLanded:
                if (find_beside(world, *this, Side::kBelow, solid())) {
                    return become_map(world, *this);
                }
                break;
        }
        world.animate(*this);
    }
    Looks looks_for() const override {
        switch (state_) {
            case State::kFalling:
                return {lands_on(ground())};
            case State::kFallingOn:
                return {lands_on(floor_or_raised_shield())};
            default:
                return {};
        }
    }
    void outside_active_area(World& world) override { leave(world, *this); }

  private:
    enum class State : std::uint8_t { kStart, kWaiting, kSinking, kSunk, kFalling, kFallingOn, kLanded };
    static constexpr std::int16_t kSpeed = px(2);

    void fall(World& world) {
        speed_y = kSpeed;
        start_falling();
        state_ = State::kFalling;
        take_riders_down(world, *this);
        world.animate(*this);
    }

    State state_ = State::kStart;
};

// A platform that drops when its switches come on, down to the next floor. There it rests until a Viking stands
// on it with nothing solid under it: then it drops again, and breaks where it lands.
class DroppingPlatform : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                stays_active_ = stays_active();
                world.play(*this, anim::kDroppingPlatformLooks);
                acts_as = tile::kPlatform;
                if (is_falling()) {
                    return drop_again(world);
                }
                state_ = world.values.at(0) != 0 ? State::kResting : State::kWaiting;
                break;
            case State::kWaiting:
                if (switches_on(world, argument)) {
                    start_falling();
                    stay_active();
                    world.values.at(0) = 1;
                    state_ = State::kDropping;
                    world.animate(*this);
                    return;
                }
                break;
            case State::kDropping:
                if (saw.landed) {
                    stay_active(stays_active_);
                    stop_falling();
                    world.sound(*this, effect::kThud);
                    state_ = State::kResting;
                    break;
                }
                take_riders_down(world, *this);
                squash_below(world, *this);
                world.animate(*this);
                return;
            case State::kResting: {
                if (find_beside(world, *this, Side::kBelow, solid())) {
                    break;
                }
                const auto rider = find_viking_beside(world, *this, Side::kAbove, vikings());
                if (!rider || rider->entity < 0) {
                    break;
                }
                target = rider->entity;
                const Entity* viking = world.at(target);
                if (viking == nullptr || viking->box.left < box.left || box.right < viking->box.right) {
                    break;
                }
                return drop_again(world);
            }
            case State::kDroppingAgain:
                if (saw.landed) {
                    stop_falling();
                    never_again(world, *this);
                    world.play(*this, anim::kDroppingPlatformBreaks);
                    state_ = State::kBreaking;
                    world.animate(*this);
                    return;
                }
                take_riders_down(world, *this);
                world.animate(*this);
                return;
            case State::kBreaking:
                if (animation_over()) {
                    world.remove(slot);
                    return;
                }
                world.animate(*this);
                return;
        }
        stay_active_under_a_viking(world, *this, stays_active_);
        world.animate(*this);
    }
    Looks looks_for() const override {
        switch (state_) {
            case State::kDropping:
                return {lands_on(end_of_its_way())};
            case State::kDroppingAgain:
                return {lands_on(solid())};
            default:
                return {};
        }
    }
    void outside_active_area(World& world) override { leave(world, *this); }

  private:
    enum class State : std::uint8_t { kStart, kWaiting, kDropping, kResting, kDroppingAgain, kBreaking };

    void drop_again(World& world) {
        world.sound(*this, effect::kSwitch);
        start_falling();
        stay_active();
        state_ = State::kDroppingAgain;
        world.animate(*this);
    }

    State state_ = State::kStart;
    bool stays_active_ = false;
};

// A plate that gives way under a weight (Egypt, the factory, the wacky world): a platform until a block comes to
// rest on it; then it sinks with the block, flips its switches and is gone for good. One that needs a switch only
// gives way while that is on. While a Viking stands on it, it stays active wherever the view is.
class SinkingPlate : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                stays_active_ = stays_active();
                needs_ = argument & kNeedsSwitch;
                argument &= static_cast<std::uint16_t>(~kNeedsSwitch);
                world.play(*this, anim::kSinkingPlateRests);
                world.animate(*this);
                acts_as = tile::kPlatform;
                state_ = State::kWaiting;
                stay_active_under_a_viking(world);
                return;
            case State::kWaiting:
                if ((world.switches & needs_) != needs_) {
                    stay_active_under_a_viking(world);
                    return;
                }
                state_ = State::kArmed;
                stay_active_under_a_viking(world);
                return;
            case State::kArmed: {
                static const TileTypes weights = tile_types({tile::kGround});
                const auto above = find_beside(world, *this, Side::kAbove, weights);
                const int on_it = above ? above->entity : kNobody;
                Entity* weight = world.at(on_it);
                if (weight == nullptr || weight->speed_y < 0 || weight->box.bottom + 1 != box.top) {
                    stay_active_under_a_viking(world);
                    return;
                }
                target = on_it;
                weight->hit_mask &= static_cast<std::uint16_t>(~hit::kWeight);
                world.play(*this, anim::kSinkingPlateSinks);
                world.sound(*this, effect::kScrape);
                speed_y = kSinkingSpeed;
                state_ = State::kSinking;
                break;
            }
            case State::kSinking:
                if (!animation_over()) {
                    break;
                }
                world.switches ^= argument;
                world.stop_sound(*this, effect::kScrape);
                gone_for_good(world, *this);
                return;
        }
        world.animate(*this);
    }

  private:
    enum class State : std::uint8_t { kStart, kWaiting, kArmed, kSinking };
    static constexpr std::uint16_t kNeedsSwitch = 0x8000;
    static constexpr std::int16_t kSinkingSpeed = px(2);

    void stay_active_under_a_viking(const World& world) {
        if (stays_active_) {
            return;
        }
        static const TileTypes vikings = tile_types({tile::kViking});
        const bool ridden = find_viking_beside(world, *this, Side::kAbove, vikings).has_value();
        stay_active(ridden);
    }

    State state_ = State::kStart;
    bool stays_active_ = false;
    std::uint16_t needs_ = 0;
};

// A tile that gives way ten ticks after a Viking has stepped on it.
class CrumblingTile : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                world.play(*this, anim::kCrumblingTileLooks);
                world.animate(*this);
                shot_away_ = kind == factory_kind::kShotAwayTile || kind == factory_kind::kShotAwayTileB8;
                acts_as = shot_away_ ? tile::kPlatform : tile::kGround;
                state_ = State::kWhole;
                return;
            case State::kWhole:
                if (shot_away_) {  // a platform that an arrow breaks
                    const bool shot = message == message::kArrowBlow;
                    message = 0;
                    if (!shot) {
                        return;
                    }
                    countdown = 0;
                    state_ = State::kStepped;
                    return update(world);
                }
                if (find_viking_beside(world, *this, Side::kAbove, vikings())) {
                    stay_active();
                    count_down_from(10);
                    state_ = State::kStepped;
                }
                return;
            case State::kStepped:
                if (countdown != 0) {
                    return;
                }
                if (!shot_away_) {
                    start_falling();
                    speed_y = px(1.5);
                }
                never_again(world, *this);
                world.play(*this, anim::kCrumblingTileCrumbles);
                acts_as = tile::kFree;
                if (Cell* cell = cell_at(world, x, y)) {
                    cell->tile_type = tile::kFree;
                }
                state_ = State::kCrumbling;
                break;
            case State::kCrumbling:
                if (animation_over()) {
                    world.remove(slot);
                    return;
                }
                break;
        }
        world.animate(*this);
    }

  private:
    enum class State : std::uint8_t { kStart, kWhole, kStepped, kCrumbling };

    State state_ = State::kStart;
    bool shot_away_ = false;
};

// A block of the rain of blocks (kind 6C): it falls, squashing a Viking under it, and where it lands it becomes a
// block of the map. It stays as long as a Viking touches it: one whose feet are level with its foot is squashed.
class RainedBlock : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                stay_active();
                world.play(*this, anim::kRainedBlockLooks);
                acts_as = tile::kPlatform;
                state_ = State::kResting;
                break;
            case State::kResting:
                if (!find_beside(world, *this, Side::kBelow, ground())) {
                    start_falling();
                    state_ = State::kFalling;
                }
                break;
            case State::kFalling:
                if (saw.landed) {
                    stop_falling();
                    world.sound(*this, effect::kBlockLands);
                    block_into_map(world, *this);
                    state_ = State::kLanded;
                    return;
                }
                squash_below(world, *this);
                break;
            case State::kLanded: {
                if (saw.viking == kNobody) {
                    world.remove(slot);
                    return;
                }
                target = saw.viking;
                Entity* other = world.at(saw.viking);
                if (other != nullptr && other->hit_class == hit_class::kViking && other->box.bottom == box.bottom) {
                    tell(*other, message::kSquashed, px(6));
                }
                return;
            }
        }
        world.animate(*this);
    }
    Looks looks_for() const override {
        switch (state_) {
            case State::kFalling:
                return {lands_on(solid())};
            case State::kLanded:
                return {touching_a_viking()};
            default:
                return {};
        }
    }

  private:
    enum class State : std::uint8_t { kStart, kResting, kFalling, kLanded };

    State state_ = State::kStart;
};

// The rain of blocks (kind 6B): once a Viking has been in its part of the level, blocks come down from the top, one
// to four every 20 ticks, 31 times; then it is over, for good.
class BlockRain : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                box = {.left = 256, .top = 64, .right = 608, .bottom = 192};
                stay_active();
                never_again(world, *this);
                state_ = State::kWaiting;
                return;
            case State::kWaiting:
                if (saw.viking != kNobody) {
                    target = saw.viking;
                    count_down();
                    state_ = State::kRaining;
                }
                return;
            case State::kRaining:
                if (countdown != 0) {
                    return;
                }
                for (const int column : kRain.at(wave_)) {
                    if (column != 0) {
                        world.spawn(*this, egypt_kind::kRainedBlock, column, 16, 0, entity_flag::kLargePieces);
                    }
                }
                if (++wave_ == kRain.size()) {
                    world.remove(slot);
                    return;
                }
                countdown = 20;
                return;
        }
    }
    Looks looks_for() const override { return state_ == State::kWaiting ? Looks{touching_a_viking()} : Looks{}; }

  private:
    enum class State : std::uint8_t { kStart, kWaiting, kRaining };
    // Where the blocks of each wave come down.
    static constexpr std::array<std::array<int, 4>, 31> kRain{{
        {528},
        {592},
        {368},
        {400},
        {432},
        {464},
        {528},
        {560},
        {368},
        {432},
        {400},
        {496},
        {336},
        {368},
        {592},
        {304},
        {560, 496, 432, 336},
        {592, 368},
        {528, 400, 272},
        {592, 304},
        {560, 464, 336},
        {528, 432},
        {496},
        {272},
        {304},
        {464, 336},
        {400},
        {560},
        {464, 272},
        {496, 304},
        {272},
    }};

    State state_ = State::kStart;
    std::size_t wave_ = 0;
};

// A ledge that hangs until it has been hit twice (sword or arrow); then it falls, and where it lands its two
// cells of the map become a platform. Hit again, it falls again.
class HangingLedge : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                acts_as = tile::kPlatform;
                world.play(*this, anim::kHangingLedgeHangs);
                box.bottom = y;
                state_ = State::kHanging;
                break;
            case State::kHanging:
                if (message == message::kSwordBlow || message == message::kArrowBlow) {
                    world.sound(*this, effect::kBurst);
                    world.play(*this, anim::kHangingLedgeIsHit);
                    if (hit_before_) {
                        message = 0;
                        message_value = 0;
                        start_falling();
                        stay_active();
                        state_ = State::kFalling;
                        break;
                    }
                    hit_before_ = true;
                }
                message = 0;
                message_value = 0;
                break;
            case State::kFalling: {
                if (!saw.landed) {
                    break;
                }
                world.sound(*this, effect::kBurnt);
                clear_flag(entity_flag::kFalls);
                stay_active(false);
                speed_y = 0;
                const int column = cell_of(x - 8);
                const int row = cell_of(y);
                for (const int at : {column, column + 1}) {
                    if (Cell* cell = world.map.cell(at, row)) {
                        cell->tile_type = tile::kPlatform;
                    }
                }
                state_ = State::kHanging;
                break;
            }
        }
        world.animate(*this);
    }
    static const TileTypes& deadly() {
        static const TileTypes types =
            tile_types({tile::kWater, tile::kLava11, tile::kLava, tile::kQuicksand, tile::kSpikes, tile::kElectricity,
                        tile::kSpring, tile::kRotatingSpikes});
        return types;
    }
    Looks looks_for() const override {
        return state_ == State::kFalling ? Looks{lands_on(solid()), lands_on(deadly())} : Looks{};
    }
    void outside_active_area(World& world) override { leave(world, *this); }

  private:
    enum class State : std::uint8_t { kStart, kHanging, kFalling };

    State state_ = State::kStart;
    bool hit_before_ = false;
};

// A ball that hangs until it has been hit twice (sword or arrow); then it falls, rolls on where it lands, and
// falls again at an edge. Where it lands on a hole made for it, it fills the hole.
class Ball : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                world.play(*this, anim::kBallHangs);
                state_ = State::kHanging;
                break;
            case State::kHanging:
                if (message == message::kSwordBlow || message == message::kArrowBlow) {
                    world.play(*this, anim::kBallIsHit);
                    message = 0;
                    world.sound(*this, effect::kBallIsHit);
                    if (hit_before_) {
                        return drop(world);
                    }
                    world.sound(*this, effect::kBurst);
                    hit_before_ = true;
                }
                message = 0;
                break;
            case State::kFalling:
                if (!saw.landed && saw.shield == kNobody) {
                    break;
                }
                stop_falling();
                speed_x = px(2);
                if (find_beside(world, *this, Side::kBelow, hole())) {
                    world.sound(*this, effect::kBurnt);
                    if (Cell* cell = world.map.cell(cell_of(x), cell_of(y))) {
                        *cell = Cell{.metatile = 7, .tile_type = tile::kGround};
                    }
                    return gone_for_good(world, *this);
                }
                world.sound(*this, effect::kThud);
                state_ = State::kRolling;
                set_flag(entity_flag::kPutOnGround);
                break;
            case State::kRolling:
                if (!find_beside(world, *this, Side::kBelow, ground())) {
                    return drop(world);
                }
                set_flag(entity_flag::kPutOnGround);
                break;
        }
        world.animate(*this);
    }
    static const TileTypes& deadly() {
        static const TileTypes types =
            tile_types({tile::kWater, tile::kLava11, tile::kLava, tile::kQuicksand, tile::kSpikes, tile::kElectricity,
                        tile::kSpring, tile::kRotatingSpikes});
        return types;
    }
    Looks looks_for() const override {
        return state_ == State::kFalling
                   ? Looks{lands_on(solid()), lands_on(deadly()), touching_any(hit::kShieldOverhead, &Saw::shield)}
                   : Looks{};
    }

  private:
    enum class State : std::uint8_t { kStart, kHanging, kFalling, kRolling };

    static const TileTypes& hole() {
        static const TileTypes types = tile_types({tile::kQuicksand});
        return types;
    }

    void drop(World& world) {
        stay_active();
        start_falling();
        speed_x = 0;
        speed_y = px(1.5);
        world.play(*this, anim::kBallFalls);
        state_ = State::kFalling;
        world.animate(*this);
    }

    State state_ = State::kStart;
    bool hit_before_ = false;
};

// A boulder: it waits behind what holds it; once that is gone it rolls on, squashing a Viking it touches, falls
// where the ground ends (squashing one under it), and breaks at a wall or where it lands.
class Boulder : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                world.play(*this, anim::kBoulderLooks);
                if (is_falling()) {
                    state_ = State::kFalling;
                    break;
                }
                state_ = State::kHeld;
                break;
            case State::kHeld:
                if (!find_beside(world, *this, Side::kAhead, solid())) {
                    world.sound(*this, effect::kRumble56);
                    never_again(world, *this);
                    speed_x = px(3);
                    set_flag(entity_flag::kPutOnGround);
                    state_ = State::kRolling;
                }
                break;
            case State::kRolling:
                if (saw.wall) {
                    stop_rumble(world);
                    speed_x = 0;
                    return shatter(world);
                }
                if (saw.viking != kNobody) {
                    target = saw.viking;
                    if (Entity* other = world.at(saw.viking)) {
                        other->message = message::kSquashed;
                        other->message_value =
                            speed_y == 0 ? std::uint16_t{px(4)} : static_cast<std::uint16_t>(speed_y);
                    }
                }
                if (!find_beside(world, *this, Side::kBelow, ground())) {
                    start_falling();
                    state_ = State::kFalling;
                    break;
                }
                set_flag(entity_flag::kPutOnGround);
                break;
            case State::kFalling:
                if (saw.landed) {
                    stop_falling();
                    return shatter(world);
                }
                squash_below(world, *this);
                break;
            case State::kShattering:
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
            case State::kRolling:
                return {stopped_by(solid()), touching_a_viking()};
            case State::kFalling:
                return {lands_on(floor_or_raised_shield())};
            default:
                return {};
        }
    }
    // Out of the active area its rumble stops with it.
    void outside_active_area(World& world) override {
        stop_rumble(world);
        leave(world, *this);
    }

  private:
    enum class State : std::uint8_t { kStart, kHeld, kRolling, kFalling, kShattering };

    void stop_rumble(World& world) const { world.stop_sound(*this, effect::kRumble56); }
    void shatter(World& world) {
        world.play(*this, anim::kBoulderShatters);
        speed_x = 0;
        stop_falling();
        state_ = State::kShattering;
        world.animate(*this);
    }

    State state_ = State::kStart;
};

// A wall a bomb blows up. Once the blast no longer touches it, it crumbles: rubble starts at its place.
class BombableWall : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                if ((argument & kLeavesBit) == 0) {
                    stay_active();
                }
                argument &= static_cast<std::uint16_t>(~kLeavesBit);
                if (argument == 1) {  // a wider one
                    box.left -= 8;
                    box.right += 8;
                }
                state_ = State::kStanding;
                break;
            case State::kStanding:
                if (message == message::kBlast) {
                    message = 0;
                    state_ = State::kHit;
                    return;
                }
                break;
            case State::kHit:
                state_ = State::kInTheBlast;
                return;
            case State::kInTheBlast:
                if ((saw.other != kNobody)) {
                    return;
                }
                world.spawn(*this, kind::kRubble,
                            static_cast<int>((static_cast<std::uint16_t>(box.left) & 0xfff0U) | 8U),
                            static_cast<int>((static_cast<std::uint16_t>(y) & 0xfff0U) | 8U));
                gone_for_good(world, *this);
                return;
        }
        world.animate(*this);
    }
    Looks looks_for() const override {
        return state_ == State::kInTheBlast ? Looks{touching(hit_class::kBlast, &Saw::other)} : Looks{};
    }

  private:
    enum class State : std::uint8_t { kStart, kStanding, kHit, kInTheBlast };
    static constexpr std::uint16_t kLeavesBit = 0x8000;  // in its argument: it isn't kept active out of view

    State state_ = State::kStart;
};

// Rubble: where a wall breaks. It clears the cell it is in; in a wall made to crumble it then goes on to the next
// cell of it (above, below, ahead, behind: the first that is wall), one after the other, until none is left.
class Rubble : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                stay_active();
                column_ = cell_of(x);
                row_ = cell_of(y);
                wall_ = type_at(world, column_, row_);
                if (wall_ != kCrumbles && wall_ != kBreaks) {
                    world.remove(slot);
                    return;
                }
                world.play(*this, anim::kRubbleDust);
                world.animate(*this);
                return clear(world);
            case State::kFirstTick:
                state_ = State::kCrumbling;
                break;
            case State::kCrumbling: {
                if (!animation_over()) {
                    break;
                }
                if (wall_ != kCrumbles) {
                    world.remove(slot);
                    return;
                }
                const int ahead = facing_left() ? -1 : 1;
                const std::array<std::array<int, 2>, 4> next{{{0, -1}, {0, 1}, {ahead, 0}, {-ahead, 0}}};
                for (const auto& [across, down] : next) {
                    const int type = type_at(world, column_ + across, row_ + down);
                    wall_ = type;
                    if (type == kCrumbles || type == kBreaks) {
                        column_ += across;
                        row_ += down;
                        // A whole cell on; across, the push counts the way it faces.
                        push_x += (across * ahead * kCell);
                        push_y += (down * kCell);
                        world.animate(*this);
                        state_ = State::kMoving;
                        return;
                    }
                }
                world.remove(slot);
                return;
            }
            case State::kMoving:
                return clear(world);
        }
        world.animate(*this);
    }

  private:
    enum class State : std::uint8_t { kStart, kFirstTick, kCrumbling, kMoving };
    static constexpr int kCrumbles = 32;  // tile types: a wall that goes cell after cell; one that is one cell
    static constexpr int kBreaks = 2;
    static constexpr int kCell = px(16);

    static int type_at(const World& world, int column, int row) {
        return world.map.tile_type_at(static_cast<std::int16_t>(column * 16), static_cast<std::int16_t>(row * 16));
    }

    void clear(World& world) {
        if (column_ >= 0 && column_ < world.map.width && row_ >= 0 && row_ < world.map.height) {
            world.map.cells.at((static_cast<std::size_t>(row_) * static_cast<std::size_t>(world.map.width)) +
                               static_cast<std::size_t>(column_)) = world.cleared_cell;
        }
        world.play(*this, anim::kRubbleFalls);
        world.animate(*this);
        state_ = State::kFirstTick;
    }

    State state_ = State::kStart;
    int column_ = 0;
    int row_ = 0;
    int wall_ = 0;  // the tile type of the cell it is clearing
};

}  // namespace

std::span<const KindOf> blocks() {
    static constexpr KindOf kList[] = {
        {Home::kEgypt, egypt_kind::kPushedBlock, make<SettlingBlock>},
        {Home::kWacky, wacky_kind::kPushedBlock, make<PushedBlock>},
        {Home::kWacky, wacky_kind::kPushedBlockB5, make<PushedBlock>},
        {Home::kWacky, wacky_kind::kPushedBlockWaitingForSwitch, make<PushedBlock>},
        {Home::kPrehistoric, prehistoric_kind::kFallingBlock, make<FallingBlock>},
        {Home::kPrehistoric, prehistoric_kind::kOtherFallingBlock, make<FallingBlock>},
        {Home::kFactory, factory_kind::kDroppedBlock, make<DroppedBlock>},
        {Home::kEgypt, egypt_kind::kDroppingBlock, make<DroppingBlock>},
        {Home::kEgypt, egypt_kind::kSinkingBlock, make<SinkingBlock>},
        {Home::kEgypt, egypt_kind::kDroppingPlatform, make<DroppingPlatform>},
        {Home::kAnywhere, kind::kSinkingPlate, make<SinkingPlate>},
        {Home::kFactory, factory_kind::kShotAwayTile, make<CrumblingTile>},
        {Home::kFactory, factory_kind::kCrumblingTile, make<CrumblingTile>},
        {Home::kFactory, factory_kind::kCrumblingTileB6, make<CrumblingTile>},
        {Home::kFactory, factory_kind::kShotAwayTileB8, make<CrumblingTile>},
        {Home::kEgypt, egypt_kind::kRainedBlock, make<RainedBlock>},
        {Home::kEgypt, egypt_kind::kBlockRain, make<BlockRain>},
        {Home::kPrehistoric, prehistoric_kind::kHangingLedge, make<HangingLedge>},
        {Home::kEgypt, egypt_kind::kBall, make<Ball>},
        {Home::kEgypt, egypt_kind::kBoulder, make<Boulder>},
        {Home::kAnywhere, kind::kBombableWall, make<BombableWall>},
        {Home::kAnywhere, kind::kRubble, make<Rubble>},
    };
    return kList;
}

}  // namespace lvr
