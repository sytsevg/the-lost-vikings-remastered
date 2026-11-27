// Tests for the world's players (co-op online; src/lvr/world.*): each one's keys and Viking, and the
// active area as every player's view together. With one player all of it is the original's: the replays' goldens
// hold that (./scripts/goldens.sh).
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <vector>

#include "lvr/camera.hpp"
#include "lvr/entity.hpp"
#include "lvr/kinds.hpp"
#include "lvr/world.hpp"

using namespace lvr;

namespace {

constexpr std::uint16_t kNext = 0x0010;
constexpr std::uint16_t kRight = 0x0100;
constexpr std::uint16_t kLeft = 0x0200;

class Still : public Entity {
  public:
    void update(World& /*world*/) override {}
};

// A room 60 cells wide and 20 high: free inside, a floor along row 18. Animation programs end at once, wherever a
// kind starts one. The three Vikings stand on the floor, apart.
struct Room {
    std::vector<std::byte> programs = std::vector<std::byte>(0x10000, std::byte{0x1A});
    World world;
    static constexpr int kFloor = 18 * 16;

    explicit Room(int players) {
        world.programs = programs;
        world.gravity = 0xf8;
        world.map = Map{.width = 60, .height = 20, .cells = std::vector<Cell>(1200, Cell{})};
        for (int column = 0; column < 60; ++column) {
            world.map.cells.at((std::size_t{18} * 60) + static_cast<std::size_t>(column)).tile_type = 0x01;
        }
        world.make = [](std::uint16_t kind) -> std::unique_ptr<Entity> {
            if (auto made = make_kind(kind)) {
                return made;
            }
            return make_entity<Still>();
        };
        for (auto& record : world.kinds) {
            record.pieces = 1;
            record.width = record.height = 16;
            record.sprite_entry = KindRecord::kPackedSprites;
            record.max_step_x = record.max_step_y = 0x1000;
        }
        for (const std::uint16_t kind : {kind::kErik, kind::kBaleog, kind::kOlaf}) {
            world.kinds.at(kind).hit_class = hit_class::kViking;
            world.kinds.at(kind).hit_mask = 0x0404;
            world.kinds.at(kind).health = 6;
            world.kinds.at(kind).height = 32;
        }
        world.kinds.at(kind::kShield).pieces = 0;
        world.kinds.at(kind::kSword).pieces = 0;
        (void)world.create({.kind = kind::kErik, .x = 100, .y = kFloor - 16});
        (void)world.create({.kind = kind::kBaleog, .x = 400, .y = kFloor - 16});
        (void)world.create({.kind = kind::kOlaf, .x = 700, .y = kFloor - 16});
        world.player_count = players;
        world.choose_vikings();
        for (Player& player : world.playing()) {  // each view on its player's Viking
            set_camera_limits(player.camera, 60, 20);
            const Entity* viking = world.at(player.viking);
            start_camera(player.camera, viking->x, viking->y);
        }
    }
    void tick(const StepInput& input = {}) {
        for (int part = 0; part < 3; ++part) {
            world.step(part, input);
        }
    }
    void ticks(int count, const StepInput& input = {}) {
        for (int i = 0; i < count; ++i) {
            tick(input);
        }
    }
    int x_of(int slot) const { return world.at(slot)->x; }
};

StepInput keys(std::uint16_t first, std::uint16_t second = 0, std::uint16_t third = 0) {
    StepInput input;
    input.players[0].keys = first;
    input.players[1].keys = second;
    input.players[2].keys = third;
    return input;
}

}  // namespace

TEST_CASE("players: at the start each plays his own Viking, the first Erik; one player plays Erik and reads for all") {
    Room one(1);
    CHECK(one.world.host().viking == 0);
    CHECK(one.world.is_played(0));
    CHECK_FALSE(one.world.is_played(1));
    for (int viking = 0; viking < kVikingSlots; ++viking) {  // the original's one keyboard
        CHECK(&one.world.keys_of(viking) == &one.world.host());
    }
    Room three(3);
    for (int who = 0; who < 3; ++who) {
        CHECK(three.world.players.at(static_cast<std::size_t>(who)).viking == who);
        CHECK(&three.world.keys_of(who) == &three.world.players.at(static_cast<std::size_t>(who)));
    }
}

TEST_CASE("players: each Viking goes by his own player's keys, in the same tick") {
    Room room(2);
    room.ticks(2);
    const int erik = room.x_of(0);
    const int baleog = room.x_of(1);
    const int olaf = room.x_of(2);
    room.ticks(10, keys(kRight, kLeft));
    CHECK(room.x_of(0) > erik);    // the first player's, to the right
    CHECK(room.x_of(1) < baleog);  // the second's, to the left
    CHECK(room.x_of(2) == olaf);   // nobody plays him: he stands
}

TEST_CASE("players: the key for the next Viking moves only the player who pressed it") {
    Room room(2);
    room.ticks(2);
    room.tick(keys(0, kNext));
    CHECK(room.world.host().viking == 0);
    CHECK(room.world.players[1].viking == 2);
    CHECK(room.world.keys_from[2] == 1);               // Olaf goes by the second player's keys now
    CHECK(room.world.keys_from[1] == 1);               // and Baleog still by those of the last one who played him
    CHECK(room.world.players[1].camera.moved_x == 5);  // his view goes after Olaf; the first player's stays
    CHECK(room.world.host().camera.moved_x != 5);
}

TEST_CASE("players: onto a Viking someone plays, a player joins his line; the first in it plays him when he is left") {
    Room room(2);
    room.ticks(2);
    constexpr std::uint16_t kPrevious = 0x0020;
    room.tick(keys(0, kPrevious));  // the second player from Baleog back to Erik, whom the first plays
    const Player& second = room.world.players[1];
    CHECK(second.viking == 0);
    CHECK_FALSE(second.controls);
    CHECK(room.world.player_of(0) == 0);
    CHECK_FALSE(room.world.is_played(1));  // Baleog is nobody's now
    // A viewer's keys move nobody.
    const int erik = room.x_of(0);
    room.ticks(10, keys(0, kRight));
    CHECK(room.x_of(0) == erik);
    // The first player goes on to Baleog: Erik is the second's to play.
    room.tick(keys(kNext));
    CHECK(room.world.host().viking == 1);
    CHECK(room.world.host().controls);
    CHECK(second.controls);
    CHECK(room.world.player_of(0) == 1);
    CHECK(room.world.keys_from[0] == 1);
    room.ticks(10, keys(0, kRight));
    CHECK(room.x_of(0) > erik);
}

TEST_CASE("players: a dead Viking's player moves on first, then his viewers, in their line's order") {
    Room room(3);
    room.world.move_player(1, 0);  // the second and the third watch Erik, in that order; Baleog and Olaf are free
    room.world.move_player(2, 0);
    REQUIRE(room.world.players[2].in_line > room.world.players[1].in_line);
    room.world.at(0)->dead = true;
    room.world.fall_back_viking();
    CHECK(room.world.host().viking == 1);  // his player: on to Baleog, nobody's: he plays him
    CHECK(room.world.host().controls);
    CHECK(room.world.players[1].viking == 1);  // the first viewer: Baleog too, whom the first player plays now
    CHECK_FALSE(room.world.players[1].controls);
    CHECK(room.world.players[2].viking == 1);
    CHECK(room.world.players[2].in_line > room.world.players[1].in_line);  // still behind him
    CHECK_FALSE(room.world.game_over);
}

TEST_CASE("players: a dead Viking's player moves on to the next living one; with none left the game is over") {
    Room room(2);
    room.world.at(1)->dead = true;
    room.world.fall_back_viking();
    CHECK(room.world.players[1].viking == 2);
    CHECK(room.world.host().viking == 0);
    CHECK_FALSE(room.world.game_over);
    room.world.at(0)->dead = true;
    room.world.at(2)->dead = true;
    room.world.fall_back_viking();
    CHECK(room.world.host().viking == kNobody);
    CHECK(room.world.players[1].viking == kNobody);
    CHECK(room.world.game_over);
}

TEST_CASE("players: the active area is every player's view together") {
    Room room(2);
    room.world.host().camera.x = 0;
    room.world.players[1].camera.x = 600;
    Entity* near_first = room.world.create({.kind = 99, .x = 200, .y = 200});
    Entity* near_second = room.world.create({.kind = 99, .x = 800, .y = 200});
    Entity* in_neither = room.world.create({.kind = 99, .x = 450, .y = 200});
    REQUIRE((near_first != nullptr && near_second != nullptr && in_neither != nullptr));
    room.world.mark_outside_active_area();
    CHECK_FALSE(near_first->has_flag(entity_flag::kLeftTheActiveArea));
    CHECK_FALSE(near_second->has_flag(entity_flag::kLeftTheActiveArea));
    CHECK(in_neither->has_flag(entity_flag::kLeftTheActiveArea));
    // A placement comes into the second view only: it is made there.
    room.world.placements = {Placement{.x = 900, .y = 200, .width = 16, .height = 16, .kind = 99}};
    room.world.first_placed_slot = kVikingSlots;
    room.world.create_in_views();
    bool made = false;
    for (int slot = 0; slot < room.world.slots(); ++slot) {
        made = made || (room.world.at(slot) != nullptr && room.world.at(slot)->placement == 0);
    }
    CHECK(made);
}

TEST_CASE("players: an enemy turns where he would leave every view he is in") {
    Room room(2);
    room.world.host().camera.x = 0;        // 0-320 across
    room.world.players[1].camera.x = 300;  // 300-620: the two overlap
    room.world.host().camera.y = room.world.players[1].camera.y = 100;
    Entity* enemy = room.world.create({.kind = 99, .x = 340, .y = 200});
    // Past the first view's edge and its 16 pixels, but well in the second: on he goes.
    CHECK_FALSE(room.world.at_edge_of_view(*enemy, true, true, 16, 16));
    enemy->x = 640;  // at the second's edge too
    CHECK(room.world.at_edge_of_view(*enemy, true, true, 16, 16));
    // Going left from inside both, he turns only past the first view's left edge.
    enemy->x = 310;
    CHECK_FALSE(room.world.at_edge_of_view(*enemy, true, false, 16, 16));
    enemy->x = -20;
    CHECK(room.world.at_edge_of_view(*enemy, true, false, 16, 16));
    // One player: the view's own edge, as the original's.
    room.world.player_count = 1;
    enemy->x = 340;
    CHECK(room.world.at_edge_of_view(*enemy, true, true, 16, 16));
    enemy->x = 330;
    CHECK_FALSE(room.world.at_edge_of_view(*enemy, true, true, 16, 16));
    // Down: at the view's bottom, without the 16 pixels.
    enemy->y = 100 + kViewHeight;
    CHECK(room.world.at_edge_of_view(*enemy, false, true, 0, 16));
}

TEST_CASE("players: online, a player's inventory doesn't hold the game; he hands an item to a Viking within reach") {
    constexpr std::uint16_t kInventory = 0x2000;
    constexpr std::uint16_t kF = 0x8000;
    Room room(2);
    room.world.online = true;
    room.world.has_hud = true;
    room.world.at(0)->x = 380;  // Erik near Baleog (at 400), Olaf far off
    room.world.items[1][0] = 9;
    room.ticks(2);
    const int baleog = room.x_of(1);
    const int erik = room.x_of(0);
    room.tick(keys(0, kInventory));  // the second player opens his
    REQUIRE(room.world.players[1].inventory.open);
    CHECK_FALSE(room.world.host().inventory.open);
    room.tick(keys(0, kF));  // picks up Baleog's item
    CHECK_FALSE(room.world.players[1].inventory.choosing);
    room.tick(keys(0, kLeft));  // carries it on to Erik, within reach (Olaf isn't)
    CHECK(room.world.players[1].inventory.cursor / World::kSquares == 0);
    room.tick(keys(0, kF));  // and gives it to him
    CHECK(room.world.items[0][0] == 9);
    CHECK(room.world.items[1][0] == 0);
    // With his inventory still open the game goes on: the first player walks, Baleog stands.
    room.ticks(4, keys(kLeft, kLeft));
    REQUIRE(room.world.players[1].inventory.open);
    CHECK(room.x_of(0) < erik);
    CHECK(room.x_of(1) == baleog);
    // A Viking who walks out of reach while the item is held over him: his squares close.
    room.world.items[1][1] = 10;
    room.world.chosen_square[1] = 1;
    room.tick(keys(0, kF));
    room.tick(keys(0, kLeft));
    REQUIRE(room.world.players[1].inventory.cursor / World::kSquares == 0);
    room.world.at(0)->x = 100;
    room.tick();
    CHECK(room.world.players[1].inventory.cursor / World::kSquares != 0);
}

TEST_CASE("players: online, what is said to one player is his alone: it waits for his key and holds nobody") {
    constexpr std::uint16_t kF = 0x8000;
    Room room(2);
    room.world.online = true;
    room.ticks(2);
    {
        const World::Aside aside{room.world, 1};  // said to Baleog
        room.world.colour_boxes(5);
        room.world.wait_for_key();
        room.world.clear_text();
    }
    CHECK(room.world.lines.empty());  // nothing for everyone: nothing holds the game
    REQUIRE(room.world.players[1].lines.size() == 3);
    const int baleog = room.x_of(1);
    room.tick(keys(0, kRight));
    const Player& second = room.world.players[1];
    CHECK(second.waiting_for_key);
    CHECK(second.said.size() == 1);
    CHECK(room.x_of(1) == baleog);  // he stands while it waits
    room.tick(keys(0, kF));         // his action key answers it
    CHECK_FALSE(second.waiting_for_key);
    CHECK(second.said.empty());
    CHECK(second.lines.empty());
    // Said to a Viking nobody plays, or offline: everyone's, as the original's.
    {
        const World::Aside aside{room.world, 2};
        room.world.wait_for_key();
    }
    CHECK(room.world.lines.size() == 1);
    room.world.lines.clear();
    room.world.online = false;
    {
        const World::Aside aside{room.world, 1};
        room.world.wait_for_key();
    }
    CHECK(room.world.lines.size() == 1);
    CHECK(room.world.players[1].lines.empty());
}

TEST_CASE("players: a line at a place of the level gives its layer that place") {
    TextLayer layer;
    layer.show(Line{.kind = Line::Kind::kCharacter,
                    .column = 1,
                    .row = 1,
                    .value = 'A',
                    .by_point = true,
                    .in_level = true,
                    .origin_x = 10,
                    .origin_y = 3},
               {});
    CHECK(layer.in_level);
    CHECK(layer.origin_x == 10);
    CHECK(layer.origin_y == 3);
    layer.show(Line{.kind = Line::Kind::kClear}, {});
    CHECK_FALSE(layer.in_level);
}

TEST_CASE("players: the cue ledger plays what a step asks once, however often rollback runs it again") {
    const SoundCue jump{.kind = SoundCue::Kind::kEffect, .value = 3, .entity = 0, .x = 10, .y = 20};
    const SoundCue hit{.kind = SoundCue::Kind::kEffect, .value = 7, .entity = 4};
    CueLedger ledger;
    CueLedger::Heard first = ledger.heard(10, {jump, hit});
    CHECK(first.play.size() == 2);
    CHECK(first.undone.empty());
    // Run again with a remote player's input corrected: the jump again (a pixel elsewhere), no hit, a new sound.
    SoundCue moved = jump;
    moved.x = 11;
    const SoundCue land{.kind = SoundCue::Kind::kEffect, .value = 9, .entity = 0};
    const CueLedger::Heard again = ledger.heard(10, {moved, land});
    REQUIRE(again.play.size() == 1);
    CHECK(again.play[0] == land);
    REQUIRE(again.undone.size() == 1);
    CHECK(again.undone[0] == hit);
    // Twice the same sound in a step is two: the second run asks for both, the first had one.
    (void)ledger.heard(11, {jump});
    CHECK(ledger.heard(11, {jump, jump}).play.size() == 1);
    ledger.confirm(11);
    CHECK(ledger.steps_kept() == 1);
    // Two players' inventory clicks (nobody's entity, each heard by his own player) aren't one another's: the run
    // again with the other's click as well plays just that one.
    const SoundCue mine{.kind = SoundCue::Kind::kEffect, .value = 5, .entity = kNobody, .heard_by = 1};
    SoundCue theirs = mine;
    theirs.heard_by = 0;
    (void)ledger.heard(12, {mine});
    const CueLedger::Heard both = ledger.heard(12, {theirs, mine});
    REQUIRE(both.play.size() == 1);
    CHECK(both.play[0].heard_by == 0);
    CHECK(both.undone.empty());
}

TEST_CASE("players: one who leaves hands his Viking to the first in its line, and his view keeps nothing active") {
    Room room(3);
    room.world.move_player(1, 0);  // the second watches Erik, whom the first plays
    room.world.host().camera.x = 0;
    room.world.players[1].camera.x = 0;
    room.world.players[2].camera.x = 600;  // the third's view: far off, with something in it
    Entity* there = room.world.create({.kind = 99, .x = 800, .y = 200});
    REQUIRE(there != nullptr);
    room.world.player_leaves(0);
    CHECK_FALSE(room.world.host().here);
    CHECK(room.world.host().viking == kNobody);
    CHECK(room.world.player_of(0) == 1);  // Erik is the second's now
    room.world.player_leaves(2);          // the third goes too: nobody's view has it any more
    CHECK(room.world.player_of(2) == kNobody);
    room.world.mark_outside_active_area();
    CHECK(there->has_flag(entity_flag::kLeftTheActiveArea));
}

TEST_CASE("players: two in a Viking's line: each plays him in turn, in the order they joined it") {
    Room room(3);
    room.ticks(2);
    constexpr std::uint16_t kPrevious = 0x0020;
    room.tick(keys(0, kPrevious));  // the second from Baleog to Erik, whom the first plays: in his line
    room.tick(keys(0, 0, kNext));   // the third from Olaf on to Erik too: behind him
    REQUIRE(room.world.players[1].viking == 0);
    REQUIRE(room.world.players[2].viking == 0);
    CHECK_FALSE(room.world.players[1].controls);
    CHECK_FALSE(room.world.players[2].controls);
    CHECK(room.world.players[2].in_line > room.world.players[1].in_line);
    room.tick(keys(kNext));  // the first on to Baleog: Erik the second's, the third still waiting
    CHECK(room.world.host().viking == 1);
    CHECK(room.world.players[1].controls);
    CHECK_FALSE(room.world.players[2].controls);
    CHECK(room.world.keys_from[0] == 1);
    room.tick(keys(0, kNext));  // the second on to Baleog (the first's: in his line); Erik the third's
    CHECK(room.world.players[1].viking == 1);
    CHECK_FALSE(room.world.players[1].controls);
    CHECK(room.world.players[2].viking == 0);
    CHECK(room.world.players[2].controls);
    CHECK(room.world.keys_from[0] == 2);
    room.ticks(40);  // (his view comes over to Erik from Olaf: until it is there, his keys do nothing, the original)
    const int erik = room.x_of(0);
    room.ticks(10, keys(0, 0, kRight));
    CHECK(room.x_of(0) > erik);
}
