// Items: what lies in a level for the Vikings to pick up. A Viking that touches one takes it if he has a free
// square; using and dropping them is the Vikings' and the inventory's.

#include <array>

#include "lvr/hits.hpp"
#include "lvr/kinds.hpp"
#include "lvr/shared.hpp"
#include "lvr/terrain.hpp"
#include "lvr/world.hpp"

namespace lvr {

namespace {

// An item's argument says which one it is (1-18), and with its top bit that it falls to the ground.
class Item : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                return at_start(world);
            case State::kLying:
                return when_lying(world);
            case State::kFalling:
                return when_falling(world);
            case State::kTaken:
                return when_taken(world);
        }
    }
    void at_start(World& world) {
        falls_ = (argument & item::kFalling) != 0;
        argument &= static_cast<std::uint16_t>(~item::kFalling);
        world.play(*this, kind == kind::kOtherItem ? anim::kOtherItemSetUp : anim::kItemSetUp);
        world.animate(*this);
        if (argument == 0 || argument > anim::kItemPictures.size()) {
            world.remove(slot);
            return;
        }
        world.play(*this, anim::kItemPictures.at(argument - 1U));
        world.animate(*this);
        state_ = State::kLying;
    }
    void when_lying(World& world) {
        if (saw.viking != kNobody && take(world)) {
            return;
        }
        if (falls_ && !find_beside(world, *this, Side::kBelow, ground())) {
            start_falling();
            state_ = State::kFalling;
            world.animate(*this);
        }
    }
    void when_falling(World& world) {
        if (saw.landed) {
            stop_falling();
            state_ = State::kLying;
            return;
        }
        if (saw.viking != kNobody && take(world)) {
            return;
        }
        world.animate(*this);
    }
    void when_taken(World& world) {
        // If the square he has chosen is empty, the one with the new item is chosen instead.
        const auto who = static_cast<std::size_t>(owner_);
        if (world.items.at(who).at(static_cast<std::size_t>(world.chosen_square.at(who))) == 0) {
            world.chosen_square.at(who) = square_;
        }
        gone_for_good(world, *this);
    }
    Looks looks_for() const override {
        switch (state_) {
            case State::kLying:
                return {touching_a_viking()};
            case State::kFalling:
                return {lands_on(ground()), touching_a_viking()};
            default:
                return {};
        }
    }

  private:
    enum class State : std::uint8_t { kStart, kLying, kFalling, kTaken };

    static const TileTypes& inside() {
        static const TileTypes types =
            tile_types({tile::kGround, tile::kGround02, tile::kLadderTop, tile::kPlatform, tile::kGround20}) | slopes();
        return types;
    }

    bool overlaps(const Entity& other) const {
        return other.box.bottom >= box.top && box.bottom >= other.box.top && other.box.right >= box.left &&
               box.right >= other.box.left;
    }

    // The Viking that touches it takes it. If his squares are full, one of the next Vikings who touches it too
    // (Erik, then Baleog, then Olaf). True if it was taken.
    bool take(World& world) {
        target = saw.viking;
        if (find_at(world, *this, x, y, inside())) {  // one inside the ground can't be taken
            return false;
        }
        for (int who = saw.viking; who < kVikingSlots; ++who) {
            const Entity* viking = world.at(who);
            if (who != saw.viking) {
                target = who;
                if (viking == nullptr || !overlaps(*viking)) {
                    continue;
                }
            }
            auto& squares = world.items.at(static_cast<std::size_t>(who));
            for (std::size_t square = 0; square < squares.size(); ++square) {
                if (squares.at(square) == 0) {
                    squares.at(square) = argument;
                    owner_ = who;
                    square_ = static_cast<int>(square);
                    world.sound(*this, effect::kTaken);
                    state_ = State::kTaken;
                    return true;
                }
            }
        }
        return false;
    }

    State state_ = State::kStart;
    bool falls_ = false;
    int owner_ = 0;   // the Viking that took it
    int square_ = 0;  // into this square of his
};

// A bomb a Viking has put down: after 60 ticks it blows up. The blast hurts the Vikings near it and everything
// that can be blown up, breaks the first cell of a breakable wall it reaches, shakes the screen and flashes red.
class PlacedBomb : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                world.play(*this, anim::kPlacedBombTicks);
                stay_active();
                count_down_from(60);
                if (facing_left()) {
                    world.flip_across(*this);
                }
                state_ = State::kTicking;
                break;
            case State::kTicking:
                if (saw.other != kNobody) {  // what it lies against stays active until it has gone off
                    target = saw.other;
                    if (held_active_ == kNobody) {
                        held_active_ = saw.other;
                        if (Entity* other = world.at(saw.other)) {
                            other->stay_active();
                        }
                    }
                }
                if (!find_beside(world, *this, Side::kBelow, ground())) {
                    start_falling();
                    state_ = State::kFalling;
                    break;
                }
                if (countdown == 0) {
                    world.play(*this, anim::kPlacedBombBlowsUp);
                    stop_falling();
                    countdown = 255;
                    box = box_of(box.left - kReach, box.top - kReach, box.right + kReach, box.bottom + kReach);
                    world.shake_y = {.amount = 3, .offset = 0, .ticks = 24};
                    world.sound(*this, effect::kLandedInScene);
                    state_ = State::kBlast;
                }
                break;
            case State::kFalling:
                if (saw.landed) {
                    stop_falling();
                    state_ = State::kTicking;
                }
                break;
            case State::kBlast:
                if (saw.other >= kVikingSlots) {
                    target = saw.other;
                    strike(world, saw.other);
                }
                world.tint2 =
                    (countdown & 2U) == 0 ? std::array<std::uint8_t, 3>{0, 112, 112} : std::array<std::uint8_t, 3>{};
                if (animation_over()) {
                    if (held_active_ != kNobody) {
                        target = held_active_;
                        Entity* other = world.at(held_active_);
                        if (other != nullptr && other->hit_class == hit_class::kEnemy) {
                            other->stay_active(false);
                        }
                    }
                    world.tint2 = {};
                    world.remove(slot);
                    return;
                }
                if (!struck_) {
                    struck_ = true;
                    for (const int viking : {1, 0, 2}) {
                        target = viking;
                        const Entity* other = world.at(viking);
                        if (other == nullptr || other->hit_class != hit_class::kViking) {
                            continue;
                        }
                        const World::Offset to = world.offset_to(*this, viking);
                        if (to.ahead >= -21 && to.ahead <= 21 && to.below >= -32 && to.below <= 32) {
                            strike(world, viking);
                        }
                    }
                    break_wall(world);
                }
                break;
        }
        world.animate(*this);
    }
    Looks looks_for() const override {
        switch (state_) {
            case State::kTicking:
            case State::kBlast:
                return {touching_any(hit::kBombable, &Saw::other)};
            case State::kFalling:
                return {lands_on(ground())};
            default:
                return {};
        }
    }

  private:
    enum class State : std::uint8_t { kStart, kTicking, kFalling, kBlast };
    static constexpr int kReach = 12;

    void strike(World& world, int who) const {
        if (Entity* other = world.at(who)) {
            harm(*other, message::kBlast, 50, facing_left());
        }
    }

    // The first of twelve points around it that is a cell of a breakable wall breaks.
    void break_wall(World& world) {
        static const TileTypes breakable =
            tile_types({tile::kGround02, tile::kGround20, tile::kBreakable21, tile::kBreakable22});
        for (const int at_x : {int{box.left}, x - 1, x + 1, int{box.right}}) {
            for (const int at_y : {int{box.bottom}, int{y}, int{box.top}}) {
                const auto found = find_at(world, *this, at_x, at_y, breakable);
                if (!found) {
                    continue;
                }
                if (found->entity < 0) {
                    // The middle of the cell the point is in.
                    const int cell_x = static_cast<int>((static_cast<std::uint16_t>(at_x) / 16U) * 16U) + 8;
                    const int cell_y = static_cast<int>((static_cast<std::uint16_t>(at_y) / 16U) * 16U) + 8;
                    world.spawn(*this, kind::kRubble, cell_x, cell_y);
                }
                return;
            }
        }
    }

    State state_ = State::kStart;
    bool struck_ = false;
    int held_active_ = kNobody;
};

// The bomb that strikes every enemy at once: the screen goes dark in five steps and black with a jolt, every
// enemy is told, and it is light again.
class FlashBomb : public Entity {
  public:
    void update(World& world) override {
        static constexpr std::array<std::uint8_t, 6> kSteps{2, 4, 6, 8, 10, 239};
        if (step_ == 0) {
            stay_active();
            world.sound(*this, effect::kFlashBomb);
        }
        if (step_ < kSteps.size()) {
            const auto by = static_cast<std::uint8_t>(kSteps.at(step_) * 2U);
            world.tint = {by, by, by};
            if (step_ + 1 == kSteps.size()) {
                world.shake_y = {.amount = 2, .offset = 0, .ticks = 8};
            }
        } else if (step_ == kSteps.size() + 1) {
            for (int other = kVikingSlots; other < world.slots(); ++other) {
                target = other;
                Entity* enemy = world.at(other);
                if (enemy != nullptr && enemy->hit_class == hit_class::kEnemy) {
                    tell(*enemy, message::kBlast, 50);
                }
            }
        } else if (step_ == kSteps.size() + 3) {
            world.tint = {};
            world.remove(slot);
            return;
        }
        ++step_;
    }

  private:
    std::size_t step_ = 0;
};

}  // namespace

std::unique_ptr<Entity> make_item() {
    return make_entity<Item>();
}

std::unique_ptr<Entity> make_bomb(std::uint16_t kind) {
    if (kind == kind::kLitBomb) {
        return make_entity<FlashBomb>();
    }
    return make_entity<PlacedBomb>();
}

}  // namespace lvr
