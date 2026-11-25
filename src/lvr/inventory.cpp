#include "lvr/inventory.hpp"

#include <optional>

#include "lvr/world.hpp"

namespace lvr {

namespace {

constexpr std::uint16_t kCloses = key::kInventory | key::kEnter;  // the inventory key, or the pause key

constexpr std::uint16_t kBlinkPickedUp = 9;  // the square stays empty a while before the carried item shows
constexpr int kReach = 64;                   // across and down added up
constexpr std::uint8_t kOpened = 0;          // sound effects
constexpr std::uint8_t kMoved = 1;
constexpr std::uint8_t kTakenOrGiven = 2;
constexpr std::uint8_t kRefused = 3;
constexpr std::uint8_t kThrownAway = 4;

// One player's inventory, and what it is opened on: his Viking, his keys.
struct Owner {
    World& world;
    int who;
    Player& player;
    Inventory& inventory;

    Owner(World& in, int player_number)
        : world(in),
          who(player_number),
          player(in.players.at(static_cast<std::size_t>(player_number))),
          inventory(player.inventory) {}
    int viking() const noexcept { return player.viking; }
    // Its sounds: everyone's offline; online only his to hear.
    void sound(std::uint8_t effect) const {
        world.sound(effect);
        if (world.online) {
            world.cues.back().heard_by = who;
        }
    }
};

std::uint16_t& item_in(World& world, int square) {
    return world.items.at(static_cast<std::size_t>(square / World::kSquares))
        .at(static_cast<std::size_t>(square % World::kSquares));
}

int chosen_square_of(const World& world, int viking) {
    return (viking * World::kSquares) + world.chosen_square.at(static_cast<std::size_t>(viking));
}

bool alive(const World& world, int viking) {
    const Entity* entity = world.at(viking);
    return entity != nullptr && entity->health != 0;
}

// The living Vikings near enough to his to be handed something; his is one of them.
void find_within_reach(const Owner& owner) {
    Inventory& inventory = owner.inventory;
    inventory.within_reach = {};
    const Entity* current = owner.world.at(owner.viking());
    for (int viking = 0; viking < kVikingSlots; ++viking) {
        const Entity* other = owner.world.at(viking);
        if (current == nullptr || other == nullptr || other->health == 0) {
            continue;
        }
        const auto across = static_cast<std::int16_t>(other->x - current->x);
        const auto down = static_cast<std::int16_t>(other->y - current->y);
        const auto apart = static_cast<std::int16_t>((across < 0 ? -across : across) + (down < 0 ? -down : down));
        inventory.within_reach.at(static_cast<std::size_t>(viking)) = apart < kReach;
    }
}

void cursor_to_chosen(const Owner& owner) {
    Inventory& inventory = owner.inventory;
    inventory.cursor = chosen_square_of(owner.world, owner.viking());
    inventory.item = item_in(owner.world, inventory.cursor);
    inventory.blink = kBlinkStart;
}

// The previous or next Viking with health left is played, as in the level. Not online, where the others play
// theirs: there he leaves the inventory to change Vikings.
void switch_viking(const Owner& owner, bool previous) {
    if (owner.world.online) {
        return;
    }
    int viking = owner.viking();
    for (int i = 0; i < kVikingSlots; ++i) {
        viking =
            previous ? (viking == 0 ? kVikingSlots - 1 : viking - 1) : (viking == kVikingSlots - 1 ? 0 : viking + 1);
        if (alive(owner.world, viking)) {
            break;
        }
    }
    if (viking == owner.viking()) {
        return;
    }
    owner.world.move_player(owner.who, viking);
    cursor_to_chosen(owner);
    find_within_reach(owner);
    owner.sound(kMoved);
}

// The arrow keys. A Viking's squares are two by two: 0 and 1 above 2 and 3. `step` is 1 across, 2 down; `forward`
// is to the right or down.
void move(const Owner& owner, int step, bool forward) {
    int& chosen = owner.world.chosen_square.at(static_cast<std::size_t>(owner.viking()));
    const bool there = step == 1 ? chosen % 2 == 1 : chosen >= 2;
    if (there == forward) {
        return;
    }
    Inventory& inventory = owner.inventory;
    chosen += forward ? step : -step;
    inventory.cursor = chosen_square_of(owner.world, owner.viking());
    inventory.item = item_in(owner.world, inventory.cursor);
    inventory.blink = kBlinkStart;
    owner.sound(kMoved);
}

bool keys_choosing(const Owner& owner) {
    Inventory& inventory = owner.inventory;
    const std::uint16_t pressed = owner.player.pressed;
    if (pressed == 0) {
        return false;
    }
    if ((pressed & key::kPrevious) != 0) {
        switch_viking(owner, true);
    }
    if ((pressed & key::kNext) != 0) {
        switch_viking(owner, false);
    }
    if ((pressed & key::kLeft) != 0) {
        move(owner, 1, false);
    }
    if ((pressed & key::kRight) != 0) {
        move(owner, 1, true);
    }
    if ((pressed & key::kUp) != 0) {
        move(owner, 2, false);
    }
    if ((pressed & key::kDown) != 0) {
        move(owner, 2, true);
    }
    if ((pressed & key::kF) != 0) {  // the chosen square's item is picked up
        inventory.giver = owner.viking();
        const int square = chosen_square_of(owner.world, owner.viking());
        if (const std::uint16_t taken = item_in(owner.world, square); taken != 0) {
            inventory.item = taken;
            item_in(owner.world, square) = 0;
            inventory.cursor = square;
            inventory.choosing = false;
            inventory.blink = kBlinkPickedUp;
            inventory.lit = false;
            owner.sound(kTakenOrGiven);
        }
    }
    // Closing is tested after the pick-up: both keys in one pass close it with the item in hand, and it is lost.
    if ((pressed & kCloses) != 0) {
        inventory.blink = kBlinkStart;
        return true;
    }
    return false;
}

// Where a carried item can go with that owner (0-2 a Viking, 3 the trash can): the can always; a Viking within
// reach, into his first free square.
std::optional<int> free_square(const Owner& owner, int whose) {
    if (whose == kVikingSlots) {
        return Inventory::kTrashCan;
    }
    if (!owner.inventory.within_reach.at(static_cast<std::size_t>(whose))) {
        return std::nullopt;
    }
    for (int square = whose * World::kSquares; square < (whose + 1) * World::kSquares; ++square) {
        if (item_in(owner.world, square) == 0) {
            return square;
        }
    }
    return std::nullopt;
}

// The cursor, carrying, on to the next owner that can take the item, to the left or the right.
void carry_on(const Owner& owner, bool left) {
    Inventory& inventory = owner.inventory;
    int whose = inventory.cursor / World::kSquares;
    std::optional<int> square;
    while (!square) {
        whose = left ? (whose == 0 ? kVikingSlots : whose - 1) : (whose == kVikingSlots ? 0 : whose + 1);
        square = free_square(owner, whose);
    }
    inventory.cursor = *square;
}

// The trash can takes food and the like; keys and what a level needs it refuses.
bool can_be_thrown_away(std::uint16_t item) {
    return (item >= 1 && item <= 5) || (item >= 9 && item <= 11);
}

// The carried item into the cursor's square. False if the trash can refused it. Online the square may have been
// filled meanwhile, by another player's inventory: the item goes into its Viking's next free square, if he has
// one, else it is refused.
bool drop(const Owner& owner) {
    World& world = owner.world;
    Inventory& inventory = owner.inventory;
    if (inventory.cursor == Inventory::kTrashCan) {
        if (!can_be_thrown_away(inventory.item)) {
            owner.sound(kRefused);
            return false;
        }
        owner.sound(kThrownAway);
    } else {
        if (item_in(world, inventory.cursor) != 0) {
            const std::optional<int> free = free_square(owner, inventory.cursor / World::kSquares);
            if (!free) {
                owner.sound(kRefused);
                return false;
            }
            inventory.cursor = *free;
        }
        owner.sound(kTakenOrGiven);
        item_in(world, inventory.cursor) = inventory.item;
        const int taker = inventory.cursor / World::kSquares;
        if (item_in(world, chosen_square_of(world, taker)) == 0) {  // his chosen square is empty: the new one is chosen
            world.chosen_square.at(static_cast<std::size_t>(taker)) = inventory.cursor % World::kSquares;
        }
    }
    // The cursor back on the giver's chosen square; if he has anything left, on his first item, which is then chosen.
    const int giver = inventory.giver;
    inventory.cursor = chosen_square_of(world, giver);
    inventory.item = 0;
    for (int square = 0; square < World::kSquares; ++square) {
        if (const std::uint16_t found = item_in(world, (giver * World::kSquares) + square); found != 0) {
            inventory.item = found;
            world.chosen_square.at(static_cast<std::size_t>(giver)) = square;
            inventory.cursor = (giver * World::kSquares) + square;
            break;
        }
    }
    inventory.choosing = true;
    inventory.lit = true;
    return true;
}

bool keys_carrying(const Owner& owner) {
    Inventory& inventory = owner.inventory;
    const std::uint16_t pressed = owner.player.pressed;
    const bool left = (pressed & key::kLeft) != 0;
    if (left || (pressed & key::kRight) != 0) {  // on to the next owner that can take it
        inventory.blink = kBlinkStart;
        carry_on(owner, left);
        owner.sound(kMoved);
        return false;
    }
    if ((pressed & key::kF) != 0) {
        (void)drop(owner);
        return false;
    }
    if ((pressed & key::kInventory) != 0) {
        return drop(owner);
    }
    return false;
}

// Online, before a pass: the Vikings have moved since the last. Who is within reach is found again; the cursor
// leaves a Viking gone out of reach for the next one that can take the item; choosing, it shows the square as it
// is now (another player may have put something there).
void keep_up(const Owner& owner) {
    Inventory& inventory = owner.inventory;
    find_within_reach(owner);
    if (inventory.choosing) {
        inventory.item = item_in(owner.world, inventory.cursor);
        return;
    }
    if (const int whose = inventory.cursor / World::kSquares;
        whose < kVikingSlots && !inventory.within_reach.at(static_cast<std::size_t>(whose))) {
        carry_on(owner, false);
    }
}

}  // namespace

bool inventory_wanted(const World& world, int player) {
    const Player& whose = world.players.at(static_cast<std::size_t>(player));
    return world.has_hud && whose.controls && whose.viking != kNobody && (whose.pressed & key::kInventory) != 0;
}

void open_inventory(World& world, int player) {
    const Owner owner{world, player};
    Inventory& inventory = owner.inventory;
    owner.sound(kOpened);
    inventory.open = true;
    inventory.choosing = true;
    // His Viking is shown, whatever his blink was at.
    if (const Entity* viking = world.at(owner.viking())) {
        Piece& piece = world.piece_at(viking->sprite.first);
        piece.blinked_off = false;
        piece.redraw = 2;
        piece.erase = 0;
    }
    cursor_to_chosen(owner);
    find_within_reach(owner);
    inventory.lit = true;
}

bool inventory_pass(World& world, int player) {
    const Owner owner{world, player};
    Inventory& inventory = owner.inventory;
    if (world.online) {
        keep_up(owner);
    }
    const bool closes = inventory.choosing ? keys_choosing(owner) : keys_carrying(owner);
    --inventory.blink;
    if ((inventory.blink & 0x0fU) == 0) {
        inventory.lit = (inventory.blink & 0x10U) != 0;
    }
    if (closes) {
        inventory.open = false;
    }
    return closes;
}

void inventories_in_tick(World& world, int part) {
    for (int who = 0; who < world.player_count; ++who) {
        Player& player = world.players.at(static_cast<std::size_t>(who));
        Inventory& inventory = player.inventory;
        if (!player.controls || player.viking == kNobody) {
            if (inventory.open) {  // he no longer plays the Viking it was open on: what he carried goes back
                inventory.open = false;
                if (!inventory.choosing && inventory.item != 0) {
                    for (int square = inventory.giver * World::kSquares;
                         square < (inventory.giver + 1) * World::kSquares; ++square) {
                        if (item_in(world, square) == 0) {
                            item_in(world, square) = inventory.item;
                            break;
                        }
                    }
                    inventory.item = 0;
                }
            }
            continue;
        }
        if (part != 0) {
            if (inventory.open) {  // (his keys were taken in the first part: these passes only blink)
                (void)inventory_pass(world, who);
            }
            continue;
        }
        const bool was_open = inventory.open;
        if (!was_open && inventory_wanted(world, who)) {
            open_inventory(world, who);
        }
        if (inventory.open) {
            if (was_open) {
                (void)inventory_pass(world, who);
            }
            player.held = player.pressed = 0;  // his Viking stands while it is open
        }
    }
}

}  // namespace lvr
