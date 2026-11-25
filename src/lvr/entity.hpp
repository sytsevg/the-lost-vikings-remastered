#pragma once

// An entity: a live thing in a world that has a behaviour (ADR-0018). Each kind is a class derived
// from Entity with its own states and data; what the engine itself reads and moves of every entity is here, in
// plain members.

#include <cstdint>
#include <memory>
#include <utility>

#include "lvr/animations.hpp"
#include "lvr/archive.hpp"
#include "lvr/looks.hpp"
#include "lvr/names.hpp"
#include "lvr/sprites.hpp"

namespace lvr {

class World;

// An entity's box in the level, in pixels; the edges are inside it.
struct Box {
    std::int16_t left = 0;
    std::int16_t top = 0;
    std::int16_t right = 0;
    std::int16_t bottom = 0;

    bool operator==(const Box&) const = default;
};

// A box from its edges, each kept in 16 bits as the original's are.
constexpr Box box_of(int left, int top, int right, int bottom) noexcept {
    return {.left = static_cast<std::int16_t>(left),
            .top = static_cast<std::int16_t>(top),
            .right = static_cast<std::int16_t>(right),
            .bottom = static_cast<std::int16_t>(bottom)};
}

// The engine's own bits of an entity's flags; the others are its kind's.
namespace entity_flag {
inline constexpr std::uint16_t kLargePieces = 0x0001;  // its pieces start 32 pixels wide, not 8
inline constexpr std::uint16_t kColour = 0x000e;       // which 16 colours its packed frames are drawn in
inline constexpr std::uint16_t kFacingLeft = 0x0040;
inline constexpr std::uint16_t kUpsideDown = 0x0080;
inline constexpr std::uint16_t kCreatedAgainAtOnce = 0x0100;
inline constexpr std::uint16_t kLeftTheActiveArea = 0x0200;  // its kind decides what that means (most are removed)
inline constexpr std::uint16_t kAlwaysActive = 0x0800;
inline constexpr std::uint16_t kCountingDown = 0x1000;
inline constexpr std::uint16_t kPutOnGround = 0x2000;  // after this tick's move, onto the ground under it
inline constexpr std::uint16_t kPushed = 0x4000;       // the level's push is added to its speed every tick
inline constexpr std::uint16_t kFalls = 0x8000;        // gravity is added to its speed every tick
// What of its flags an entity that leaves the active area writes back into its placement.
inline constexpr std::uint16_t kKeptInPlacement = 0xcdff;
}  // namespace entity_flag

// What goes with a blow (Entity::message_value): how strong it is, and in the top bit the side it came from.
inline constexpr std::uint16_t kFromTheRight = 0x8000;  // from the right: from someone facing left
inline constexpr std::uint16_t kStrength = 0x7fff;

// Who moves an entity this tick.
inline constexpr int kMovesItself = -1;     // its own speed
inline constexpr int kMovedFreely = 0x100;  // by exactly its push, without the speed limit
// Otherwise: the slot of the entity it is attached to.

// What every entity is and remembers, as plain data: what the engine reads and moves of it, and what is written when
// an entity is (lvr/archive.hpp). Its kind's own state is beside it, in the kind's `Saved`.
struct EntityState {
    // What it saw after its last move and in the last tick's hit tests (looks_for).
    Saw saw;
    // Entities refer to each other by slot (target, message_value, creator, last_created). As in the original a
    // slot outlives what was in it: when an entity is removed, only its creator's `last_created` and what it
    // created's `creator` are cleared. A `target` (or a slot in `message_value`) can name a slot that is empty by
    // now, or has another entity in it; a kind that looks its target up takes what it finds there, as the
    // original's scripts do. That is behaviour the replays hold, not an oversight.
    int slot = -1;                    // where it is in its world
    std::uint16_t kind = 0;           // the original's object type, until the kinds have names
    std::uint16_t acts_as = 0;        // not 0: the others' terrain tests find it, as cells of this tile type
    std::uint16_t hit_class = 0;      // what it is to the others' hit tests (a Viking, an arrow, an enemy ...)
    std::uint16_t hit_mask = 0;       // bits the others' hit tests can ask for
    int placement = -1;               // the placement it came from; -1: none
    std::uint16_t flags = 0;          // entity_flag, and its kind's own bits
    std::uint16_t argument = 0;       // its placement's: which item, which switch ...
    std::uint16_t countdown = 0;      // counts down a tick at a time while entity_flag::kCountingDown
    std::uint16_t message = 0;        // what another entity tells it; each kind answers in its own way
    std::uint16_t message_value = 0;  // what goes with the message: who sent it (a slot), or how much
    int target = kNobody;             // the slot of the entity it is dealing with: the one that hit it, or it aims at
    std::uint16_t health = 0;         // from its kind's record: a Viking's health x 2; other kinds use it as they like
    bool ignores_weightlessness = false;  // a Viking that used the boots
    std::uint16_t wanted_item = 0;        // a thing that takes an item: which one
    bool dead = false;                    // a Viking that died: it stays where it is, and can't be chosen
    bool inflated = false;                // a Viking blown up like a balloon (the wacky world)
    bool held = false;                    // a crate a magnet holds
    std::uint16_t pose = 0;               // Tomator's: what his attached parts show with him
    std::uint16_t stage = 0;              // and which part of the fight he is in: his shots fly by it
    std::uint16_t kind_value = 0;         // from its kind's record; what it means is the kind's
    int creator = kNobody;                // the slot of the entity that created it
    int last_created = kNobody;           // the slot of the last entity it created

    std::int16_t x = 0;  // its centre
    std::int16_t y = 0;
    std::int16_t previous_x = 0;  // where it was before this tick's move
    std::int16_t previous_y = 0;
    Box box;
    std::int16_t width = 0;  // its kind's size
    std::int16_t height = 0;
    std::int16_t half_width = 0;  // for leaving the active area: its placement's half size, or its kind's
    std::int16_t half_height = 0;

    // Speeds and moves are in 256ths of a pixel a tick.
    std::int16_t speed_x = 0;
    std::int16_t speed_y = 0;     // down is positive
    std::int16_t max_step_x = 0;  // the most it moves in a tick
    std::int16_t max_step_y = 0;
    // This tick's move, gathered from its speed, its animation and its behaviour; after the move: the pixels moved.
    std::int16_t push_x = 0;
    std::int16_t push_y = 0;
    std::uint8_t fraction_x = 0;  // what is left of a pixel after the last move
    std::uint8_t fraction_y = 0;
    int moved_by = kMovesItself;

    Sprite sprite;
    bool had_first_turn = false;  // its first turn is over (first_turn)
};

class Entity : public EntityState {
  public:
    Entity() = default;
    Entity(const Entity&) = default;
    Entity& operator=(const Entity&) = default;
    Entity(Entity&&) = default;
    Entity& operator=(Entity&&) = default;
    virtual ~Entity() = default;

    // One tick of its behaviour.
    virtual void update(World& world) = 0;
    // What it looks for after this tick's move, for what it is doing now (lvr/looks.hpp); the answers are in
    // `saw` at its next update.
    virtual Looks looks_for() const { return {}; }
    // After the tick's moves, for an entity that moved itself: where the map and the others stop it. As it is:
    // the looks of looks_for() that stop it. (A kind written before looks_for() does its own looking here.)
    virtual void after_move(World& world);
    // Later in the tick, for every entity: which others touch it. As it is: the touching looks.
    virtual void check_hits(World& world);
    // Its turn when it has left the active area (entity_flag::kLeftTheActiveArea), instead of update: most kinds
    // are removed; some first note where they were in their placement; a few carry on.
    virtual void outside_active_area(World& world);
    // A copy, for a snapshot of the world. How its kind is copied was noted when it was made (make_entity).
    std::unique_ptr<Entity> clone() const { return copy_ != nullptr ? copy_(*this) : nullptr; }
    // Written out and read back: what every entity has, then (a kind with LVR_SAVES) what its kind remembers. What
    // is read is of an entity of the same kind.
    virtual void save(Writer& to) const { to(static_cast<const EntityState&>(*this)); }
    virtual void load(Reader& from) { from(static_cast<EntityState&>(*this)); }

    // Whether its animation has played to its end (or to where it waits to be started again).
    bool animation_over() const noexcept { return sprite.delay == 0; }
    // It falls no further: gravity leaves it alone, and it has no speed down.
    void stop_falling() noexcept {
        clear_flag(entity_flag::kFalls);
        speed_y = 0;
    }

    // Put at (x, y), its box with it: development tools' (a replay's `place`, the showcase's deaths). The game
    // itself never moves anything so.
    void move_to(int to_x, int to_y) noexcept {
        const auto across = static_cast<std::int16_t>(to_x - x);
        const auto down = static_cast<std::int16_t>(to_y - y);
        x = static_cast<std::int16_t>(to_x);
        y = static_cast<std::int16_t>(to_y);
        box.left = static_cast<std::int16_t>(box.left + across);
        box.right = static_cast<std::int16_t>(box.right + across);
        box.top = static_cast<std::int16_t>(box.top + down);
        box.bottom = static_cast<std::int16_t>(box.bottom + down);
    }

    // True once, at its first turn: for what a kind sets up as it starts.
    bool first_turn() noexcept { return !std::exchange(had_first_turn, true); }
    bool started() const noexcept { return had_first_turn; }

    // Its flags, one or several at a time.
    bool has_flag(std::uint16_t flag) const noexcept { return (flags & flag) != 0; }
    void set_flag(std::uint16_t flag, bool on = true) noexcept {
        flags = static_cast<std::uint16_t>(on ? (unsigned{flags} | flag) : (unsigned{flags} & ~unsigned{flag}));
    }
    void clear_flag(std::uint16_t flag) noexcept { set_flag(flag, false); }

    // The flags the kinds set most, by what they mean.
    // Of the blow it was told of: how strong, whether it came from the right, and forgetting the side (its
    // strength is left).
    std::uint16_t blow_strength() const { return message_value & kStrength; }
    bool blow_from_the_right() const { return (message_value & kFromTheRight) != 0; }
    void forget_blow_side() { message_value &= kStrength; }
    bool is_falling() const noexcept { return has_flag(entity_flag::kFalls); }  // gravity pulls it
    void start_falling() noexcept { set_flag(entity_flag::kFalls); }
    bool stays_active() const noexcept { return has_flag(entity_flag::kAlwaysActive); }  // out of view too
    void stay_active(bool on = true) noexcept { set_flag(entity_flag::kAlwaysActive, on); }
    // `countdown` goes down a tick at a time.
    void count_down(bool on = true) noexcept { set_flag(entity_flag::kCountingDown, on); }
    void count_down_from(std::uint16_t ticks) noexcept {
        count_down();
        countdown = ticks;
    }

    bool facing_left() const noexcept { return has_flag(entity_flag::kFacingLeft); }
    bool upside_down() const noexcept { return has_flag(entity_flag::kUpsideDown); }

  private:
    template <class Kind, class... Args>
    friend std::unique_ptr<Kind> make_entity(Args&&... args);
    std::unique_ptr<Entity> (*copy_)(const Entity&) = nullptr;
};

// A new entity of a kind's class. Every entity is made this way: it is what lets a world be copied.
template <class Kind, class... Args>
std::unique_ptr<Kind> make_entity(Args&&... args) {
    auto made = std::make_unique<Kind>(std::forward<Args>(args)...);
    made->copy_ = [](const Entity& entity) -> std::unique_ptr<Entity> {
        return std::make_unique<Kind>(static_cast<const Kind&>(entity));
    };
    return made;
}

// A kind keeps everything it remembers in one plain struct named `Saved`, as its member `s`, with this line after
// it: the kind is then written out and read back without its fields being named anywhere, and adding a field is
// only adding it to the struct. `Base` is the class it is derived from. (A kind others are derived from names its
// member otherwise, and says so: LVR_SAVES_AS.) That a kind has no state outside its `Saved` is held by
// tools/kind_state.py in the checks; that it has no other member at all, here, by its size.
#define LVR_SAVES_AS(Base, member)                                                                             \
    void save(::lvr::Writer& to) const override {                                                              \
        static_assert(sizeof(std::remove_cvref_t<decltype(*this)>) == sizeof(::lvr::WithSaved<Base, Saved>),   \
                      "a kind's state is all in its Saved");                                                   \
        static_assert(std::is_standard_layout_v<Saved> && std::is_trivially_copyable_v<Saved>,                 \
                      "a kind's Saved is plain data");                                                         \
        static_assert(alignof(Saved) <= alignof(std::uint64_t), "nothing in a Saved asks for more alignment"); \
        Base::save(to);                                                                                        \
        to(member);                                                                                            \
    }                                                                                                          \
    void load(::lvr::Reader& from) override {                                                                  \
        Base::load(from);                                                                                      \
        from(member);                                                                                          \
    }
#define LVR_SAVES(Base) LVR_SAVES_AS(Base, s)

// An entity a world owns: a slot's. Copying it copies the entity (Entity::clone), which is what makes a world a
// thing that can be copied: a snapshot for the rewind, a quicksave. Otherwise it is used as the pointer it holds.
class OwnedEntity {
  public:
    OwnedEntity() = default;
    template <class Kind>
    OwnedEntity(std::unique_ptr<Kind> entity) noexcept : entity_{std::move(entity)} {}  // NOLINT(*-explicit-*)
    OwnedEntity(const OwnedEntity& other) : entity_{other.entity_ ? other.entity_->clone() : nullptr} {}
    OwnedEntity& operator=(const OwnedEntity& other) {
        if (this != &other) {
            entity_ = other.entity_ ? other.entity_->clone() : nullptr;
        }
        return *this;
    }
    OwnedEntity(OwnedEntity&&) noexcept = default;
    OwnedEntity& operator=(OwnedEntity&&) noexcept = default;
    ~OwnedEntity() = default;

    Entity* get() const noexcept { return entity_.get(); }
    Entity* operator->() const noexcept { return entity_.get(); }
    Entity& operator*() const noexcept { return *entity_; }
    explicit operator bool() const noexcept { return entity_ != nullptr; }
    void reset() noexcept { entity_.reset(); }

  private:
    std::unique_ptr<Entity> entity_;
};

}  // namespace lvr
