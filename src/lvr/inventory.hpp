#pragma once

// The inventory screen: opened from a level, it holds the game while the player looks through what the Vikings
// carry, hands an item to a Viking within reach, or throws it away (docs/original/menus.md). Its squares are the
// HUD's: four per Viking, then the trash can.

#include <array>
#include <cstdint>

namespace lvr {

inline constexpr std::uint16_t kBlinkStart = 17;  // what the cursor's square shows, shown at the next pass

class World;

struct Inventory {
    static constexpr int kTrashCan = 12;  // the square after the Vikings'

    bool open = false;
    int cursor = 0;                      // the square it is on
    std::uint16_t item = 0;              // under the cursor while choosing; the one being carried
    std::uint16_t blink = 0;             // counts down a pass at a time; what the cursor's square shows turns with it
    bool choosing = true;                // false: an item was picked up and is being carried
    int giver = 0;                       // the Viking the carried item came from
    std::array<bool, 3> within_reach{};  // of the player's Viking: who can be handed an item
    bool lit = true;                     // what the blinking last left: the picker (choosing) or the carried item shown

    bool operator==(const Inventory&) const = default;
};

// Each player has one (ADR-0012), on the Viking he plays. Offline it holds the game, as the original's; online it
// doesn't: its passes are part of the tick (inventories_in_tick), the player's Viking standing while it is open.

// Whether the player's key that opens it was just pressed, in a level that has the HUD, and he plays a Viking.
bool inventory_wanted(const World& world, int player);
// Opens it on the square his Viking has chosen.
void open_inventory(World& world, int player);
// One pass of its loop, with the keys as the world last read them. True when it closes.
bool inventory_pass(World& world, int player);
// Online: a tick's part of every player's inventory: opened by its key, a pass with the tick's keys (and two more
// that only blink, so it goes at the original's pace of a pass a refresh), and the keys then taken from his Viking.
void inventories_in_tick(World& world, int part);

}  // namespace lvr
