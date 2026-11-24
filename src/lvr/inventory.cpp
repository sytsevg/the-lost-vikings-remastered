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

void sound(World& world, std::uint8_t effect) {
    world.sound(effect);
}

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

// The living Vikings near enough to the one being played to be handed something; he is one of them.
void find_within_reach(World& world) {
    Inventory& inventory = world.inventory;
    inventory.within_reach = {};
    const Entity* current = world.at(world.current_viking);
    for (int viking = 0; viking < kVikingSlots; ++viking) {
        const Entity* other = world.at(viking);
        if (current == nullptr || other == nullptr || other->health == 0) {
            continue;
        }
        const auto across = static_cast<std::int16_t>(other->x - current->x);
        const auto down = static_cast<std::int16_t>(other->y - current->y);
        const auto apart = static_cast<std::int16_t>((across < 0 ? -across : across) + (down < 0 ? -down : down));
        inventory.within_reach.at(static_cast<std::size_t>(viking)) = apart < kReach;
    }
}

void cursor_to_chosen(World& world) {
    Inventory& inventory = world.inventory;
    inventory.cursor = chosen_square_of(world, world.current_viking);
    inventory.item = item_in(world, inventory.cursor);
    inventory.blink = kBlinkStart;
}

// The previous or next Viking with health left is played, as in the level.
void switch_viking(World& world, bool previous) {
    int viking = world.current_viking;
    for (int i = 0; i < kVikingSlots; ++i) {
        viking =
            previous ? (viking == 0 ? kVikingSlots - 1 : viking - 1) : (viking == kVikingSlots - 1 ? 0 : viking + 1);
        if (alive(world, viking)) {
            break;
        }
    }
    if (viking == world.current_viking) {
        return;
    }
    world.current_viking = viking;
    cursor_to_chosen(world);
    find_within_reach(world);
    sound(world, kMoved);
}

// The arrow keys. A Viking's squares are two by two: 0 and 1 above 2 and 3. `step` is 1 across, 2 down; `forward`
// is to the right or down.
void move(World& world, int step, bool forward) {
    int& chosen = world.chosen_square.at(static_cast<std::size_t>(world.current_viking));
    const bool there = step == 1 ? chosen % 2 == 1 : chosen >= 2;
    if (there == forward) {
        return;
    }
    Inventory& inventory = world.inventory;
    chosen += forward ? step : -step;
    inventory.cursor = chosen_square_of(world, world.current_viking);
    inventory.item = item_in(world, inventory.cursor);
    inventory.blink = kBlinkStart;
    sound(world, kMoved);
}

bool keys_choosing(World& world) {
    Inventory& inventory = world.inventory;
    const std::uint16_t pressed = world.pressed;
    if (pressed == 0) {
        return false;
    }
    if ((pressed & key::kPrevious) != 0) {
        switch_viking(world, true);
    }
    if ((pressed & key::kNext) != 0) {
        switch_viking(world, false);
    }
    if ((pressed & key::kLeft) != 0) {
        move(world, 1, false);
    }
    if ((pressed & key::kRight) != 0) {
        move(world, 1, true);
    }
    if ((pressed & key::kUp) != 0) {
        move(world, 2, false);
    }
    if ((pressed & key::kDown) != 0) {
        move(world, 2, true);
    }
    if ((pressed & key::kF) != 0) {  // the chosen square's item is picked up
        inventory.giver = world.current_viking;
        const int square = chosen_square_of(world, world.current_viking);
        if (const std::uint16_t taken = item_in(world, square); taken != 0) {
            inventory.item = taken;
            item_in(world, square) = 0;
            inventory.cursor = square;
            inventory.choosing = false;
            inventory.blink = kBlinkPickedUp;
            inventory.lit = false;
            sound(world, kTakenOrGiven);
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
std::optional<int> free_square(World& world, int owner) {
    if (owner == kVikingSlots) {
        return Inventory::kTrashCan;
    }
    if (!world.inventory.within_reach.at(static_cast<std::size_t>(owner))) {
        return std::nullopt;
    }
    for (int square = owner * World::kSquares; square < (owner + 1) * World::kSquares; ++square) {
        if (item_in(world, square) == 0) {
            return square;
        }
    }
    return std::nullopt;
}

// The trash can takes food and the like; keys and what a level needs it refuses.
bool can_be_thrown_away(std::uint16_t item) {
    return (item >= 1 && item <= 5) || (item >= 9 && item <= 11);
}

// The carried item into the cursor's square. False if the trash can refused it.
bool drop(World& world) {
    Inventory& inventory = world.inventory;
    if (inventory.cursor == Inventory::kTrashCan) {
        if (!can_be_thrown_away(inventory.item)) {
            sound(world, kRefused);
            return false;
        }
        sound(world, kThrownAway);
    } else {
        sound(world, kTakenOrGiven);
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

bool keys_carrying(World& world) {
    Inventory& inventory = world.inventory;
    const std::uint16_t pressed = world.pressed;
    const bool left = (pressed & key::kLeft) != 0;
    if (left || (pressed & key::kRight) != 0) {  // on to the next owner that can take it
        inventory.blink = kBlinkStart;
        int owner = inventory.cursor / World::kSquares;
        std::optional<int> square;
        while (!square) {
            owner = left ? (owner == 0 ? kVikingSlots : owner - 1) : (owner == kVikingSlots ? 0 : owner + 1);
            square = free_square(world, owner);
        }
        inventory.cursor = *square;
        sound(world, kMoved);
        return false;
    }
    if ((pressed & key::kF) != 0) {
        (void)drop(world);
        return false;
    }
    if ((pressed & key::kInventory) != 0) {
        return drop(world);
    }
    return false;
}

}  // namespace

bool inventory_wanted(const World& world) noexcept {
    return world.has_hud && world.current_viking != kNobody && (world.pressed & key::kInventory) != 0;
}

void open_inventory(World& world) {
    Inventory& inventory = world.inventory;
    sound(world, kOpened);
    inventory.open = true;
    inventory.choosing = true;
    // The Viking being played is shown, whatever his blink was at.
    if (const Entity* viking = world.at(world.current_viking)) {
        Piece& piece = world.piece_at(viking->sprite.first);
        piece.blinked_off = false;
        piece.redraw = 2;
        piece.erase = 0;
    }
    cursor_to_chosen(world);
    find_within_reach(world);
    inventory.lit = true;
}

bool inventory_pass(World& world) {
    Inventory& inventory = world.inventory;
    const bool closes = inventory.choosing ? keys_choosing(world) : keys_carrying(world);
    --inventory.blink;
    if ((inventory.blink & 0x0fU) == 0) {
        inventory.lit = (inventory.blink & 0x10U) != 0;
    }
    if (closes) {
        inventory.open = false;
    }
    return closes;
}

}  // namespace lvr
