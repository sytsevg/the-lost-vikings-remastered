// Tests for the Vikings and what goes with them (src/lvr/vikings.cpp, weapons.cpp, items.cpp, switches.cpp), on
// hand-made maps. How they play a whole level is held by the replays' goldens (./scripts/goldens.sh).
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <vector>

#include "lvr/entity.hpp"
#include "lvr/kinds.hpp"
#include "lvr/world.hpp"

using namespace lvr;

namespace {

constexpr std::uint16_t kE = 0x0040;
constexpr std::uint16_t kS = 0x0080;
constexpr std::uint16_t kRight = 0x0100;
constexpr std::uint16_t kLeft = 0x0200;
constexpr std::uint16_t kUp = 0x0800;
constexpr std::uint16_t kD = 0x4000;
constexpr std::uint16_t kF = 0x8000;

class Still : public Entity {
  public:
    void update(World& /*world*/) override {}
};

// A room 40 cells wide and 20 high: free inside, a floor along row 18, walls at both ends. Animation programs
// end at once, wherever a kind starts one.
struct Room {
    std::vector<std::byte> programs = std::vector<std::byte>(0x10000, std::byte{0x1A});
    World world;
    static constexpr int kFloor = 18 * 16;  // the floor's top

    Room() {
        world.programs = programs;
        world.camera.max_x = world.camera.max_y = 2000;
        world.gravity = 0xf8;
        world.map =
            Map{.width = 40, .height = 20, .cells = std::vector<Cell>(800, Cell{.metatile = 0, .tile_type = 0})};
        for (int column = 0; column < 40; ++column) {
            cell(column, 18).tile_type = 0x01;
            cell(column, 19).tile_type = 0x01;
        }
        for (int row = 0; row < 20; ++row) {
            cell(0, row).tile_type = 0x01;
            cell(39, row).tile_type = 0x01;
        }
        world.make = [](std::uint16_t kind) -> std::unique_ptr<Entity> {
            if (auto made = make_kind(kind)) {
                return made;
            }
            return make_entity<Still>();
        };
        for (std::size_t k = 0; k < world.kinds.size(); ++k) {
            world.kinds.at(k).pieces = 1;
            world.kinds.at(k).width = world.kinds.at(k).height = 16;
            world.kinds.at(k).sprite_entry = KindRecord::kPackedSprites;
            world.kinds.at(k).max_step_x = world.kinds.at(k).max_step_y = 0x1000;
        }
        for (const std::uint16_t k : {kind::kErik, kind::kBaleog, kind::kOlaf}) {
            world.kinds.at(k).hit_class = hit_class::kViking;
            world.kinds.at(k).hit_mask = 0x0404;
            world.kinds.at(k).health = 6;
            world.kinds.at(k).height = 32;
        }
        world.kinds.at(kind::kShield).pieces = 0;
        world.kinds.at(kind::kSword).pieces = 0;
    }
    Cell& cell(int column, int row) { return world.map.cells.at(static_cast<std::size_t>((row * 40) + column)); }
    // The three Vikings on the floor, Erik at `x`; the others out of the way.
    Entity* vikings(int x = 100) {
        world.camera.y = 140;  // the floor in view
        Entity* erik = world.create({.kind = kind::kErik, .x = x, .y = kFloor - 16});
        world.create({.kind = kind::kBaleog, .x = 400, .y = kFloor - 16});
        world.create({.kind = kind::kOlaf, .x = 500, .y = kFloor - 16});
        return erik;
    }
    // Something else in the room; it stays wherever the view is.
    Entity* put(Creation creation) {
        creation.flags |= entity_flag::kAlwaysActive;
        return world.create(creation);
    }
    void tick(std::uint16_t keys = 0) {
        for (int part = 0; part < 3; ++part) {
            world.step(part, keys);
        }
    }
    void ticks(int count, std::uint16_t keys = 0) {
        for (int i = 0; i < count; ++i) {
            tick(keys);
        }
    }
    bool has(std::uint16_t kind) const {
        for (const auto& entity : world.entities) {
            if (entity && entity->kind == kind) {
                return true;
            }
        }
        return false;
    }
};

}  // namespace

TEST_CASE("vikings: each starts standing, found by others as a Viking; Olaf brings his shield") {
    Room room;
    World& world = room.world;
    Entity* erik = room.vikings();
    Entity* olaf = world.entities[2].get();
    room.ticks(3);
    CHECK(erik->acts_as == tile::kViking);
    CHECK(world.entities[1]->acts_as == tile::kViking);
    CHECK(erik->y == Room::kFloor - 16);
    REQUIRE(world.entities[3]);
    const Entity& shield = *world.entities[3];
    CHECK(shield.kind == kind::kShield);
    CHECK(shield.argument == 0x0f);
    CHECK((shield.flags & entity_flag::kAlwaysActive) != 0);
    CHECK(olaf->last_created == 3);
    // Held before him: a wall to the others, and it stops what flies at him.
    CHECK(shield.acts_as == tile::kShield);
    CHECK(shield.hit_mask == 0x0008);
    CHECK(shield.box.left == olaf->x + 2);
    CHECK_FALSE(world.entities[4]);  // one shield only
    CHECK(erik->clone()->kind == kind::kErik);
    CHECK(world.entities[3]->clone()->kind == kind::kShield);
    // The engine never tells a Viking he left the active area; his turn for it is the one every entity has when
    // the scene ends, and a living Viking is gone then.
    world.mark_outside_active_area();
    CHECK((erik->flags & entity_flag::kLeftTheActiveArea) == 0);
    world.entities[1]->flags |= entity_flag::kLeftTheActiveArea;
    room.tick();
    CHECK(world.entities[0]);
    CHECK_FALSE(world.entities[1]);
}

TEST_CASE("erik: he runs up to his top speed, skids to a stop, and is stopped by a wall") {
    Room room;
    Entity* erik = room.vikings();
    room.ticks(2);
    room.tick(kRight);
    CHECK(erik->speed_x == px(0.75));
    room.ticks(20, kRight);
    CHECK(erik->speed_x == px(8));
    CHECK(erik->x > 200);
    CHECK_FALSE(erik->facing_left());
    room.tick();
    CHECK(erik->speed_x == px(4));
    room.ticks(2);
    CHECK(erik->speed_x == 0);
    const int stopped_at = erik->x;
    room.tick();
    CHECK(erik->x == stopped_at);
    // The other way: he turns, and runs into the wall at the room's end.
    room.ticks(60, kLeft);
    CHECK(erik->facing_left());
    CHECK(erik->box.left == 16);
    CHECK(erik->speed_x == px(2));  // pushing against it
    // Another Viking doesn't take the keys.
    CHECK(room.world.entities[1]->x == 400);
}

TEST_CASE("vikings: under Remaster rules a stick pushed part way walks him slower, his walk's animation too") {
    for (const Rules rules : {Rules::kVanilla, Rules::kRemaster}) {
        Room room;
        room.world.rules = rules;
        Entity* erik = room.vikings();
        room.ticks(2);
        room.world.pace.across = kFullPace / 2;  // (Vanilla rules don't have it)
        room.ticks(20, kRight);
        const bool paced = rules == Rules::kRemaster;
        CHECK(erik->speed_x == (paced ? px(4) : px(8)));
        CHECK(erik->sprite.pace == (paced ? 12 : kFullPace));  // his legs at the root of it: 16 x 0.71, rounded up
        // Pushed all the way while walking at that pace: at full speed at once.
        room.world.pace.across = kFullPace;
        room.tick(kRight);
        CHECK(erik->speed_x == px(8));
        CHECK(erik->sprite.pace == kFullPace);
        // Pushed a little, from standing: never faster than that.
        room.ticks(6);
        room.world.pace.across = 1;
        room.tick(kRight);
        CHECK(erik->speed_x == (paced ? px(0.5) : px(0.75)));
        CHECK(erik->sprite.pace == (paced ? 4 : kFullPace));  // a sixteenth of his speed, a quarter of his steps
    }
}

TEST_CASE("vikings: under Remaster rules a stick pushed part way steers him slower in the air, and climbs slower") {
    for (const Rules rules : {Rules::kVanilla, Rules::kRemaster}) {
        const bool paced = rules == Rules::kRemaster;
        {
            // Falling from high up, steered right with the stick a quarter of the way: no faster than he'd walk so.
            Room room;
            room.world.rules = rules;
            room.world.camera.y = 0;
            Entity* erik = room.world.create({.kind = kind::kErik, .x = 100, .y = 40});
            room.world.create({.kind = kind::kBaleog, .x = 400, .y = Room::kFloor - 16});
            room.world.create({.kind = kind::kOlaf, .x = 500, .y = Room::kFloor - 16});
            room.ticks(2);
            room.world.pace.across = kFullPace / 4;
            room.ticks(6, kRight);
            REQUIRE(erik->y < Room::kFloor - 40);  // still in the air
            CHECK(erik->speed_x == (paced ? px(2) : px(5.25)));
        }
        {
            // Up a ladder with the stick half way up: each step of the climb at half its pace.
            Room room;
            room.world.rules = rules;
            for (int row = 12; row < 18; ++row) {
                room.cell(10, row).tile_type = room.cell(11, row).tile_type = 0x03;
            }
            room.cell(10, 11).tile_type = room.cell(11, 11).tile_type = 0x04;
            Entity* erik = room.vikings(11 * 16 - 3);
            room.ticks(2);
            room.world.pace.up_down = kFullPace / 2;
            room.ticks(6, kUp);
            CHECK(erik->sprite.pace == (paced ? kFullPace / 2 : kFullPace));
            room.world.pace.up_down = kFullPace / 4;  // the stick moved: at once, not when the step is over
            room.tick(kUp);
            CHECK(erik->sprite.pace == (paced ? kFullPace / 4 : kFullPace));
            room.tick();  // let go: the step he is in ends at full pace
            CHECK(erik->sprite.pace == kFullPace);
        }
    }
}

TEST_CASE("erik: he jumps, higher while the key is held, and lands on his feet") {
    Room room;
    Entity* erik = room.vikings();
    room.ticks(2);
    room.tick(kF);
    CHECK(erik->speed_y < 0);
    CHECK((erik->flags & entity_flag::kFalls) != 0);
    int highest = erik->y;
    for (int i = 0; i < 40; ++i) {
        room.tick(kRight);
        highest = erik->y < highest ? erik->y : highest;
    }
    CHECK(highest < Room::kFloor - 16 - 30);
    CHECK(erik->y == Room::kFloor - 16);
    CHECK((erik->flags & entity_flag::kFalls) == 0);
    CHECK(erik->health == 6);
    CHECK(erik->x > 100);  // steered in the air
}

TEST_CASE("vikings: a long fall ends hard: the screen shakes and it costs a point; with none left he dies") {
    Room room;
    World& world = room.world;
    Entity* erik = world.create({.kind = kind::kErik, .x = 100, .y = 40});
    Entity* baleog = world.create({.kind = kind::kBaleog, .x = 200, .y = 40});
    world.create({.kind = kind::kOlaf, .x = 500, .y = Room::kFloor - 16});
    baleog->health = 2;
    // The room is too low for a fall to become a long one by itself.
    room.ticks(3);
    erik->speed_y = baleog->speed_y = px(16);
    bool shook = false;
    for (int i = 0; i < 40; ++i) {
        room.tick();
        shook = shook || world.shake_y.ticks != 0;
    }
    CHECK(shook);
    CHECK(erik->health == 4);
    CHECK_FALSE(erik->dead);
    CHECK(erik->y == Room::kFloor - 16);
    CHECK(baleog->health == 0);
    CHECK(baleog->dead);
    CHECK(baleog->acts_as == tile::kBody);
    CHECK(baleog->hit_class == 0);
    // The dead stay where they are.
    room.ticks(3);
    CHECK(world.entities[1]);
}

TEST_CASE("vikings: what they are told: hurt, electrocuted, dropped, thrown, taken away") {
    Room room;
    World& world = room.world;
    Entity* erik = room.vikings();
    Entity* baleog = world.entities[1].get();
    Entity* olaf = world.entities[2].get();
    room.ticks(2);
    // A blow from the left knocks him to the right.
    erik->message = message::kStruck;
    erik->message_value = 2;
    room.tick();
    CHECK(erik->health == 4);
    CHECK(erik->message == 0);
    room.ticks(4);
    CHECK(erik->x != 100);
    CHECK_FALSE(erik->dead);
    // The same with no health left to lose kills.
    erik->message = message::kStruck;
    erik->message_value = 0x8000 | 9;
    room.ticks(3);
    CHECK(erik->health == 0);
    CHECK(erik->dead);
    // The next one is played from then on.
    CHECK(world.current_viking == 1);

    baleog->message = message::kElectrocuted;
    room.ticks(3);
    CHECK(baleog->dead);
    CHECK(world.current_viking == 2);

    olaf->message = message::kThrown;  // thrown up
    olaf->message_value = static_cast<std::uint16_t>(-0x600);
    room.tick();
    CHECK(olaf->speed_y < 0);
    olaf->message = message::kDropped;  // what he stood on is gone
    room.tick();
    CHECK((olaf->flags & entity_flag::kFalls) != 0);
    olaf->message = message::kConversation;
    room.tick();
    CHECK(olaf->message == 0);
    room.ticks(30);
    olaf->message = message::kDissolved;  // taken out of the level
    room.tick();
    CHECK_FALSE(world.entities[2]);
    CHECK_FALSE(room.has(kind::kShield));
}

TEST_CASE("vikings: squashed on the ground, pushed down in the air; crushed against a wall") {
    Room room;
    World& world = room.world;
    Entity* erik = room.vikings(24);
    Entity* baleog = world.entities[1].get();
    room.ticks(2);
    erik->message = message::kCrushed;  // pushed to the left (back), against the wall behind him
    erik->message_value = 0xff00;
    room.ticks(6);
    CHECK(erik->dead);
    baleog->message = message::kSquashed;
    baleog->message_value = 0x400;
    room.ticks(3);
    CHECK(baleog->dead);
}

TEST_CASE("erik: at a run the D key sends him charging; a wall ends it head first") {
    Room room;
    World& world = room.world;
    Entity* erik = room.vikings(300);
    // The animations must run for the charge to last: a program that waits a long time.
    room.programs[0x4d40] = std::byte{0x0f};
    room.programs[0x4d41] = std::byte{100};
    room.ticks(2);
    room.ticks(10, kRight);
    CHECK(erik->speed_x >= 0x600);
    room.tick(kRight | kD);
    bool shook = false;
    for (int i = 0; i < 40 && !shook; ++i) {
        room.tick(kRight);
        shook = world.shake_x.ticks != 0;
    }
    CHECK(shook);
    // Thrown back from the wall, he ends on his feet again.
    room.ticks(40);
    CHECK(erik->x < 39 * 16 - 8);
    CHECK((erik->flags & entity_flag::kFalls) == 0);
    CHECK(erik->y == Room::kFloor - 16);
}

TEST_CASE("baleog: the sword swings in front of him; the bow looses an arrow when the key is let go") {
    Room room;
    World& world = room.world;
    room.vikings();
    world.current_viking = 1;
    world.camera.x = 240;
    Entity* baleog = world.entities[1].get();
    Entity* enemy = room.put({.kind = 0xea, .x = 400 + 20, .y = Room::kFloor - 16});
    enemy->hit_mask = 0x0101;  // hurt by sword and arrow
    room.ticks(2);
    room.tick(kF);
    CHECK(room.has(kind::kSword));
    room.tick();
    CHECK(enemy->message == message::kSwordBlow);
    CHECK((enemy->message_value & 0x7fffU) == 2);
    CHECK(room.has(kind::kSpark));  // where the blow landed, for a moment
    room.ticks(4);
    CHECK_FALSE(room.has(kind::kSword));
    CHECK_FALSE(room.has(kind::kSpark));
    enemy->message = 0;

    room.ticks(3, kD);
    CHECK_FALSE(room.has(kind::kArrow));
    room.tick();
    REQUIRE(room.has(kind::kArrow));
    CHECK(world.cues.back().value == 0x1b);
    room.ticks(3);
    CHECK(enemy->message == message::kArrowBlow);
    CHECK_FALSE(room.has(kind::kArrow));
    // With nothing in its way an arrow flies to the wall and breaks there.
    world.remove(enemy->slot);
    room.ticks(3, kD);
    room.ticks(40);
    CHECK_FALSE(room.has(kind::kArrow));
    CHECK(baleog->x == 400);
}

TEST_CASE("items: the Viking that touches one takes it into his first free square") {
    Room room;
    World& world = room.world;
    world.placements = {
        Placement{.x = 150, .y = Room::kFloor - 8, .width = 16, .height = 16, .kind = kind::kItem, .argument = 0x0003}};
    room.vikings();
    world.items[0] = {5, 0, 0, 0};
    REQUIRE(world.create_placed(0) != nullptr);
    room.ticks(2);
    CHECK(room.has(kind::kItem));
    room.ticks(12, kRight);
    CHECK_FALSE(room.has(kind::kItem));
    CHECK(world.items[0][1] == 3);
    CHECK(world.shown_items[1] == 3);
    CHECK(world.chosen_square[0] == 0);
    CHECK(world.gone.test(0));
    // With nothing to use, the E key only makes a sound.
    world.cues.clear();
    room.tick(kE);
    REQUIRE(world.cues.size() == 1);
    CHECK(world.cues[0].value == 0x03);

    // One that falls lands on the floor; an unknown one is removed.
    Entity* falling = room.put({.kind = kind::kItem, .x = 300, .y = 100, .argument = 0x8002});
    Entity* unknown = room.put({.kind = kind::kItem, .x = 300, .y = 100, .argument = 0x0013});
    const int unknown_slot = unknown->slot;
    room.ticks(40);
    CHECK(falling->box.bottom == Room::kFloor - 1);
    CHECK_FALSE(world.entities.at(static_cast<std::size_t>(unknown_slot)));
}

TEST_CASE("doors, guns: a door opens for a Viking near it; a gun's shot hurts him") {
    Room room;
    World& world = room.world;
    Entity* erik = room.vikings();
    Entity* door = room.put({.kind = kind::kDoor, .x = 200, .y = Room::kFloor - 16});
    Entity* switched = room.put({.kind = kind::kSwitchedDoor, .x = 600, .y = 100, .argument = 0x0002});
    room.ticks(2);
    CHECK(door->acts_as == tile::kGround);
    CHECK(switched->acts_as == tile::kGround);
    world.switches = 0x0002;
    room.ticks(16, kRight);
    CHECK(door->acts_as == 0);
    CHECK(switched->acts_as == 0);
    world.switches = 0;
    room.ticks(30, kRight);
    CHECK(erik->x > 232);
    CHECK(door->acts_as == tile::kGround);
    CHECK(switched->acts_as == tile::kGround);

    Entity* gun = room.put({.kind = kind::kGun, .x = erik->x + 60, .y = Room::kFloor - 8, .flags = 0x0040});
    gun->health = 4;
    room.ticks(3);
    CHECK(room.has(kind::kBullet));
    for (int i = 0; i < 60 && erik->health == 6; ++i) {
        room.tick();
    }
    CHECK(erik->health == 4);
    // Shot at, it takes so much.
    gun->message = message::kBlast;
    gun->message_value = 3;
    room.tick();
    CHECK(gun->health == 1);
    const int slot = gun->slot;
    gun->message = message::kBlast;
    gun->message_value = 3;
    room.tick();
    CHECK_FALSE(world.entities.at(static_cast<std::size_t>(slot)));
}

TEST_CASE("vikings: the S key asks the thing he stands at") {
    Room room;
    Entity* erik = room.vikings();
    Entity* thing = room.put({.kind = 0xea, .x = 104, .y = Room::kFloor - 16});
    thing->acts_as = tile::kThing;
    thing->hit_mask = 0x0040;
    room.ticks(2);
    room.tick(kS);
    room.ticks(3);
    CHECK(thing->message == message::kAsked);
    CHECK(thing->message_value == 0);
    CHECK(erik->target == thing->slot);
    // He goes on as before.
    room.ticks(5, kRight);
    CHECK(erik->x > 100);
}

TEST_CASE("vikings: up a ladder and off its top") {
    Room room;
    World& world = room.world;
    // A ladder two cells wide from the floor up to a ledge.
    for (int row = 12; row < 18; ++row) {
        room.cell(10, row).tile_type = room.cell(11, row).tile_type = 0x03;
    }
    room.cell(10, 11).tile_type = room.cell(11, 11).tile_type = 0x04;
    Entity* erik = room.vikings(11 * 16 - 3);
    room.ticks(2);
    room.ticks(3, kUp);
    CHECK(erik->x == 11 * 16);  // at the ladder's middle
    CHECK(erik->speed_x == 0);
    room.ticks(3, kLeft);  // off the ladder: he falls back to the floor
    room.ticks(20);
    CHECK(erik->y == Room::kFloor - 16);
    CHECK(world.entities[0]);
}

TEST_CASE("olaf: the shield before him or over his head; under it he glides down") {
    Room room;
    World& world = room.world;
    room.vikings();
    world.current_viking = 2;
    world.camera.x = 340;
    Entity* olaf = world.entities[2].get();
    room.ticks(3);
    const Entity* shield = world.entities[3].get();
    REQUIRE(shield != nullptr);
    CHECK(shield->acts_as == tile::kShield);
    room.tick(kF);
    room.tick();
    CHECK(shield->acts_as == tile::kShieldOverhead);  // a platform now
    CHECK(shield->hit_mask == 0x0010);
    CHECK(shield->box.bottom == olaf->y - 0x0d);
    room.ticks(8, kLeft);
    CHECK(olaf->x < 500);
    CHECK(olaf->facing_left());
    CHECK(shield->x == olaf->x);  // it goes where he goes
    room.tick(kD);
    room.tick();
    CHECK(shield->acts_as == tile::kShield);
    CHECK(shield->box.right == olaf->x - 2);  // before him, the way he faces

    // In the air with the shield up he falls slowly, however far.
    olaf->y = 170;
    olaf->box.top = 154;
    olaf->box.bottom = 185;
    room.tick();
    room.tick(kF);
    int fastest = 0;
    for (int i = 0; i < 200 && (olaf->flags & entity_flag::kFalls) != 0; ++i) {
        room.tick(i % 2 == 0 ? kRight : 0);
        fastest = olaf->speed_y > fastest ? olaf->speed_y : fastest;
    }
    CHECK(fastest <= 0x200 + 0xf8);
    CHECK(olaf->y == Room::kFloor - 16);
    CHECK(olaf->health == 6);

    // What hits the shield, Olaf is told; when he dies it is nothing to anyone, and goes.
    world.entities[3]->message = message::kStruck;
    world.entities[3]->message_value = 2;
    room.ticks(2);
    CHECK(olaf->health == 4);
    olaf->message = message::kHeldInTheAir;  // held in the air: only Olaf is
    room.tick();
    room.tick(kLeft);
    room.tick(kRight);
    CHECK_FALSE(olaf->facing_left());
    room.tick(kF);
    olaf->message = message::kBlownBack;  // blown back
    room.ticks(3);
    olaf->message = message::kElectrocuted;
    room.ticks(4);
    CHECK(olaf->dead);
    CHECK_FALSE(room.has(kind::kShield));
}

TEST_CASE("gunners: he walks, shoots at a Viking he faces, is hurt, and killed") {
    Room room;
    World& world = room.world;
    // His animations move him and take time: walking, 2 pixels a tick; the others wait a while.
    const auto program = [&room](std::size_t at, std::initializer_list<int> bytes) {
        for (const int byte : bytes) {
            room.programs[at++] = static_cast<std::byte>(byte);
        }
    };
    program(0x263f, {0x07, 0x02, 0x0f, 0x00, 0x03, 0x3f, 0x26});
    program(0x266d, {0x0f, 0x03, 0x1a});
    program(0x1722, {0x0f, 0x03, 0x1a});
    Entity* erik = room.vikings(100);
    world.kinds.at(kind::kGunner).health = 4;
    world.kinds.at(kind::kGunner).hit_class = 2;
    world.placements = {
        Placement{.x = 220, .y = Room::kFloor - 8, .width = 16, .height = 16, .kind = kind::kGunner, .flags = 0x0040}};
    Entity* gunner = world.create_placed(0);
    REQUIRE(gunner != nullptr);
    room.ticks(4);
    CHECK(gunner->acts_as == tile::kEnemy);
    CHECK(gunner->x < 220);  // walking, towards Erik
    bool shot = false;
    for (int i = 0; i < 40 && !shot; ++i) {
        room.tick();
        shot = room.has(kind::kBullet);
    }
    CHECK(shot);
    // A blow from the front throws him back; he is hurt, not killed.
    gunner->message = message::kSwordBlow;
    gunner->message_value = 2;
    room.tick();
    CHECK(gunner->health == 2);
    CHECK(gunner->speed_x == px(-4));
    room.ticks(6);
    // He leaves the active area: his place and his health are kept for when he comes back.
    const int was_at = gunner->x;
    gunner->flags |= entity_flag::kLeftTheActiveArea;
    room.tick();
    CHECK_FALSE(room.has(kind::kGunner));
    CHECK(world.placements[0].x == was_at);
    CHECK(world.placements[0].argument == 2);
    gunner = world.create_placed(0);
    REQUIRE(gunner != nullptr);
    room.ticks(2);
    CHECK(gunner->health == 2);
    // Another blow kills: what is left of him lies there for a moment.
    gunner->message = message::kArrowBlow;
    gunner->message_value = 0x8002;
    room.tick();
    CHECK_FALSE(room.has(kind::kGunner));
    CHECK(room.has(kind::kRemains));
    CHECK(world.gone.test(0));
    room.ticks(3);
    CHECK_FALSE(room.has(kind::kRemains));
    CHECK(erik->health <= 6);

    // The kind that only shoots from close by; one that starts in the air falls to the floor first.
    Entity* close = room.put({.kind = kind::kCloseGunner, .x = 300, .y = 100, .flags = entity_flag::kFalls});
    room.ticks(40);
    CHECK(close->box.bottom == Room::kFloor - 1);
    CHECK((close->flags & entity_flag::kFalls) == 0);
}

TEST_CASE("inventory: choosing a square, handing an item to a Viking within reach, the trash can") {
    Room room;
    World& world = room.world;
    room.vikings();
    world.entities[1]->x = 120;  // Baleog beside Erik; Olaf is too far away
    world.has_hud = true;
    world.items[0] = {1, 6, 0, 0};
    room.ticks(2);
    const auto pass = [&world](std::uint16_t keys) {
        world.read_keys(keys);
        const bool closed = inventory_pass(world);
        world.read_keys(0);
        return closed;
    };
    world.read_keys(0x2000);
    REQUIRE(inventory_wanted(world));
    open_inventory(world);
    Inventory& inventory = world.inventory;
    CHECK(inventory.open);
    CHECK(inventory.cursor == 0);
    CHECK(inventory.item == 1);
    CHECK(inventory.within_reach == std::array<bool, 3>{true, true, false});
    CHECK(world.cues.back().value == 0);
    // The arrows move the chosen square; the picker blinks on it.
    CHECK_FALSE(pass(kRight));
    CHECK(world.chosen_square[0] == 1);
    CHECK(inventory.item == 6);
    CHECK_FALSE(pass(0x0400));  // down
    CHECK(inventory.cursor == 3);
    CHECK_FALSE(pass(kUp));
    CHECK_FALSE(pass(kLeft));
    CHECK(inventory.cursor == 0);
    for (int i = 0; i < 16; ++i) {
        CHECK_FALSE(pass(0));
    }
    CHECK_FALSE(inventory.lit);
    // Fire picks the item up; Right carries it to the next owner with room: Baleog, then the trash can (not Olaf).
    CHECK_FALSE(pass(kF));
    CHECK_FALSE(inventory.choosing);
    CHECK(world.items[0][0] == 0);
    CHECK_FALSE(pass(kRight));
    CHECK(inventory.cursor == 4);
    CHECK_FALSE(pass(kRight));
    CHECK(inventory.cursor == Inventory::kTrashCan);
    CHECK_FALSE(pass(kLeft));
    CHECK(inventory.cursor == 4);
    CHECK_FALSE(pass(kF));  // dropped: Baleog has it, and the cursor is back on Erik's first item
    CHECK(world.items[1][0] == 1);
    CHECK(inventory.choosing);
    CHECK(inventory.cursor == 1);
    CHECK(inventory.item == 6);
    // The trash can refuses what a level may need, and takes food.
    CHECK_FALSE(pass(kF));
    CHECK_FALSE(pass(kLeft));
    CHECK(inventory.cursor == Inventory::kTrashCan);
    CHECK_FALSE(pass(kF));
    CHECK_FALSE(inventory.choosing);
    CHECK(world.cues.back().value == 3);
    CHECK_FALSE(pass(0x2000));  // Tab drops too, and closes unless it was refused
    CHECK(inventory.open);
    CHECK_FALSE(pass(kRight));
    CHECK(inventory.cursor == 0);
    CHECK(pass(0x2000));
    CHECK_FALSE(inventory.open);
    CHECK(world.items[0][0] == 6);
    // The next Viking is played from inside it; his food goes into the can.
    world.read_keys(0x2000);
    open_inventory(world);
    CHECK_FALSE(pass(0x0010));
    CHECK(world.current_viking == 1);
    CHECK(inventory.cursor == 4);
    CHECK_FALSE(pass(0x0020));
    CHECK(world.current_viking == 0);
    CHECK_FALSE(pass(0x0010));
    CHECK_FALSE(pass(kF));
    CHECK_FALSE(pass(kRight));
    CHECK(inventory.cursor == Inventory::kTrashCan);
    CHECK_FALSE(pass(kF));
    CHECK(world.cues.back().value == 4);
    CHECK(world.items[1] == std::array<std::uint16_t, 4>{0, 0, 0, 0});
    CHECK(pass(0x1000));
    CHECK_FALSE(inventory_wanted(world));
}

TEST_CASE("switches: levers go over, buttons flip for a moment, for good or for a while, locks open once") {
    Room room;
    World& world = room.world;
    room.vikings();
    world.placements = {
        Placement{.x = 150, .y = 200, .width = 16, .height = 16, .kind = kind::kLever, .argument = 0x8001},
        Placement{.x = 170, .y = 200, .width = 16, .height = 16, .kind = kind::kButtonForGood, .argument = 0x0010},
        Placement{.x = 190, .y = 200, .width = 16, .height = 16, .kind = kind::kLock, .argument = 0x0100}};
    Entity* lever = world.create_placed(0);
    Entity* for_good = world.create_placed(1);
    Entity* lock = world.create_placed(2);
    Entity* second = room.put({.kind = kind::kLeverOfSecondSet, .x = 150, .y = 220, .argument = 0x0002});
    Entity* button = room.put({.kind = kind::kButton, .x = 170, .y = 220, .argument = 0x0004});
    Entity* a_while = room.put({.kind = kind::kButtonForAWhile, .x = 190, .y = 220, .argument = 0x0008});
    Entity* other = room.put({.kind = kind::kOtherLever, .x = 210, .y = 220, .argument = 0x0020});
    REQUIRE(lock != nullptr);
    room.ticks(2);
    CHECK(lever->acts_as == tile::kThing);
    CHECK(lever->argument == 1);  // it started on; its switch is the level's to set
    CHECK(lock->acts_as == tile::kLock);
    const auto work = [&room](Entity* thing, std::uint16_t how = message::kAsked) {
        thing->message = how;
        room.tick();
    };
    work(lever);
    CHECK(world.switches == 0x0001);
    work(lever, 0x03);  // an arrow works it too
    CHECK(world.switches == 0);
    work(second);
    CHECK(world.more_switches == 0x0002);
    work(other);
    CHECK(world.switches == 0x0020);
    work(other);
    CHECK(world.switches == 0);
    lever->message = 0x77;  // anything else it is told stays unanswered
    room.tick();
    CHECK(world.switches == 0);

    work(button);
    CHECK(world.switches == 0x0004);
    room.tick();
    CHECK(world.switches == 0);  // only for a moment
    room.ticks(3);
    work(button);
    CHECK(world.switches == 0x0004);
    room.ticks(4);

    work(for_good);
    CHECK(world.switches == 0x0010);
    CHECK(world.placements[1].argument == 0);
    room.ticks(4);
    work(for_good);  // it has nothing left to flip
    CHECK(world.switches == 0x0010);
    room.ticks(4);

    work(a_while);
    CHECK(world.switches == 0x0018);
    CHECK((a_while->flags & entity_flag::kAlwaysActive) != 0);
    room.ticks(100);
    CHECK(world.switches == 0x0018);
    room.ticks(60);
    CHECK(world.switches == 0x0010);
    CHECK((a_while->flags & entity_flag::kAlwaysActive) == 0);

    lock->message = 0x05;
    room.tick();
    CHECK(lock->message == 0);
    const int lock_slot = lock->slot;
    work(lock);
    CHECK(world.switches == 0x0110);
    CHECK(world.gone.test(2));
    CHECK_FALSE(world.entities.at(static_cast<std::size_t>(lock_slot)));

    // A lever that leaves the active area is kept as it stands.
    work(lever);
    work(lever);
    lever->flags |= entity_flag::kLeftTheActiveArea;
    room.tick();
    CHECK(world.placements[0].argument == 0x8001);
}

TEST_CASE("lifts: the Viking being played rides one down and up between its stops") {
    Room room;
    World& world = room.world;
    // The shaft goes through the floor: lift 2 runs from y 104 down to 312.
    for (int row = 0; row < 20; ++row) {
        room.cell(9, row).tile_type = room.cell(10, row).tile_type = 0;
    }
    world.camera.y = 0;
    Entity* erik = world.create({.kind = kind::kErik, .x = 160, .y = 80});
    world.create({.kind = kind::kBaleog, .x = 400, .y = Room::kFloor - 16});
    world.create({.kind = kind::kOlaf, .x = 500, .y = Room::kFloor - 16});
    world.placements = {Placement{.x = 160, .y = 104, .width = 16, .height = 16, .kind = kind::kLift5F, .argument = 2}};
    Entity* lift = world.create_placed(0);
    REQUIRE(lift != nullptr);
    lift->flags |= entity_flag::kAlwaysActive;
    room.ticks(3);
    CHECK(lift->acts_as == tile::kPlatform);
    CHECK(erik->y == 80);  // standing on it
    CHECK((erik->flags & entity_flag::kFalls) == 0);
    room.tick(kUp);  // at its top already
    CHECK(lift->speed_y == 0);
    world.cues.clear();
    room.ticks(2, 0x0400);
    CHECK(lift->speed_y == px(4));
    CHECK(world.cues.size() == 2);  // the other way's sound stopped, this way's started
    room.ticks(70, 0x0400);
    CHECK(lift->y == 312);
    CHECK(lift->speed_y == 0);
    CHECK(erik->y == 288);  // carried down with it
    CHECK(world.cues.back().value == 0x5e);
    room.ticks(20, kUp);
    CHECK(lift->speed_y == px(-4));
    CHECK(erik->box.bottom + 1 == lift->box.top);
    // It leaves the active area on its way: its place, and that it is between its stops, are kept; the shaft's
    // cells there are solid until it is back.
    const int at = lift->y;
    lift->flags =
        static_cast<std::uint16_t>((lift->flags & ~entity_flag::kAlwaysActive) | entity_flag::kLeftTheActiveArea);
    room.tick();
    CHECK(world.placements[0].y == at);
    CHECK(world.placements[0].argument == 0xc002);
    CHECK(world.map.tile_type_at(160, static_cast<std::int16_t>(at + 4)) == 0x01);
    lift = world.create_placed(0);
    REQUIRE(lift != nullptr);
    room.ticks(2);
    CHECK(world.map.tile_type_at(160, static_cast<std::int16_t>(at + 4)) == 0x00);

    // One that needs a switch stands still without it; the one that looks different is the same lift.
    Entity* switched = room.put({.kind = kind::kOtherLift, .x = 300, .y = 100, .argument = 0x0104});
    room.ticks(3);
    CHECK(switched->speed_y == 0);
    CHECK((switched->flags & entity_flag::kAlwaysActive) != 0);
}

TEST_CASE("teleporters: a Viking who acts on one fades, is put where it leads, and appears") {
    Room room;
    Entity* erik = room.vikings();
    // Its programs take a while, so the fading and the appearing can be seen.
    room.programs[0x69ff] = room.programs[0x6a57] = std::byte{0x0f};
    room.programs[0x6a00] = room.programs[0x6a58] = std::byte{0x02};
    // Teleporter 2 leads to (0x40, 0x82): a ledge is there.
    for (int column = 1; column < 8; ++column) {
        room.cell(column, 9).tile_type = 0x01;
    }
    Entity* teleporter = room.put({.kind = kind::kTeleporter, .x = 104, .y = Room::kFloor - 16, .argument = 2});
    teleporter->hit_mask = 0x0040;
    room.ticks(2);
    CHECK(teleporter->acts_as == tile::kThing);
    room.tick(kS);
    room.ticks(2);
    CHECK(erik->hit_class == 0);  // nothing hits him on the way
    CHECK((erik->hit_mask & 0x0404U) == 0);
    for (int i = 0; i < 40 && erik->hit_class == 0; ++i) {
        room.tick();
    }
    CHECK(erik->x == 0x40);
    CHECK(erik->hit_class == 1);
    CHECK((erik->hit_mask & 0x0404U) == 0x0404);
    room.ticks(10);
    CHECK(erik->box.bottom == (9 * 16) - 1);  // standing on the ledge
    room.ticks(4, kRight);
    CHECK(erik->x > 0x40);
    CHECK(teleporter->message == 0);
    CHECK_FALSE(teleporter_destination(0x1234));
}

TEST_CASE("walls: a bombed wall crumbles cell after cell; an electric field spares no one") {
    Room room;
    World& world = room.world;
    world.cleared_cell = Cell{.metatile = 7, .tile_type = 0};
    room.vikings();
    // A wall three cells high that crumbles, one cell of another kind beside it, and a wall entity before it.
    for (int row = 10; row < 13; ++row) {
        room.cell(12, row).tile_type = 0x20;
    }
    room.cell(13, 12).tile_type = 0x02;
    world.placements = {Placement{
        .x = 12 * 16 + 8, .y = 10 * 16 + 8, .width = 16, .height = 16, .kind = kind::kBombableWall, .argument = 0}};
    Entity* wall = world.create_placed(0);
    REQUIRE(wall != nullptr);
    Entity* wider = room.put({.kind = kind::kBombableWall, .x = 500, .y = 100, .argument = 1});
    room.ticks(2);
    CHECK((wall->flags & entity_flag::kAlwaysActive) != 0);  // bit 15 of the argument clear: kept active
    CHECK(wall->box.right - wall->box.left == 15);
    CHECK(wider->box.right - wider->box.left == 31);
    wider->message = 0x05;  // nothing to it
    room.tick();
    world.remove(wider->slot);
    wall->message = message::kBlast;
    room.ticks(4);
    CHECK_FALSE(room.has(kind::kBombableWall));
    CHECK(world.gone.test(0));
    REQUIRE(room.has(kind::kRubble));
    room.ticks(30);
    CHECK_FALSE(room.has(kind::kRubble));
    for (int row = 10; row < 13; ++row) {
        CHECK(room.cell(12, row).tile_type == 0);
        CHECK(room.cell(12, row).metatile == 7);
    }
    CHECK(room.cell(13, 12).tile_type == 0);  // the last cell it found
    // Rubble where there is no wall is gone at once; in a wall of one cell it clears that cell.
    room.cell(20, 5).tile_type = 0x02;
    room.put({.kind = kind::kRubble, .x = 20 * 16 + 8, .y = 5 * 16 + 8});
    Entity* nowhere = room.put({.kind = kind::kRubble, .x = 22 * 16 + 8, .y = 5 * 16 + 8});
    const int nowhere_slot = nowhere->slot;
    room.ticks(2);
    CHECK_FALSE(world.entities.at(static_cast<std::size_t>(nowhere_slot)));
    room.ticks(6);
    CHECK(room.cell(20, 5).tile_type == 0);
    CHECK_FALSE(room.has(kind::kRubble));

    // The field: on with its switch, it electrocutes anything that can be; off, it fades away for good.
    world.switches = 0x0001;
    world.placements.push_back(
        Placement{.x = 300, .y = 100, .width = 16, .height = 16, .kind = kind::kElectricField, .argument = 0x0001});
    world.placements.push_back(
        Placement{.x = 340, .y = 100, .width = 16, .height = 16, .kind = kind::kUnseenBarrier, .argument = 0x0001});
    Entity* field = world.create_placed(1);
    Entity* unseen = world.create_placed(2);
    REQUIRE(unseen != nullptr);
    field->flags |= entity_flag::kAlwaysActive;
    unseen->flags |= entity_flag::kAlwaysActive;
    Entity* enemy = room.put({.kind = 0xea, .x = 300, .y = 100});
    enemy->hit_mask = 0x0400;
    Entity* other = room.put({.kind = 0xea, .x = 340, .y = 100});
    other->hit_mask = 0x0400;
    room.ticks(3);
    CHECK(enemy->message == message::kElectrocuted);
    CHECK(other->message == 0);  // the unseen one only minds the Vikings
    CHECK(room.cell(18, 6).tile_type == 0);
    world.switches = 0;
    room.ticks(3);
    CHECK_FALSE(room.has(kind::kElectricField));
    CHECK_FALSE(room.has(kind::kUnseenBarrier));
    CHECK(world.gone.test(1));
    CHECK_FALSE(world.gone.test(2));
    // Created with its switch off, the field never appears.
    world.gone.reset(1);
    REQUIRE(world.create_placed(1) != nullptr);
    room.tick();
    CHECK(world.gone.test(1));
}

TEST_CASE("weightlessness: on with its switch, it draws a Viking in it upwards and runs the colour cycle") {
    Room room;
    World& world = room.world;
    world.cycles.push_back({.delay = 2, .left = 2, .from = 1, .to = 5, .running = false});
    Entity* erik = room.vikings(120);
    // Place 0: 0x20 either side of where it stands, 0x80 above and below.
    Entity* place = room.put({.kind = kind::kWeightlessness, .x = 120, .y = 200, .argument = 0x0100});
    room.ticks(3);
    CHECK(place->box.left == 120 - 0x20);
    CHECK(place->box.bottom == 200 + 0x80);
    CHECK(place->hit_class == 0);
    CHECK_FALSE(world.cycles[0].running);
    CHECK(erik->y == Room::kFloor - 16);
    world.switches = 0x0100;
    room.ticks(2);
    CHECK(place->hit_class == 0x0c);
    CHECK(world.cycles[0].running);
    room.ticks(20);
    CHECK(erik->y < Room::kFloor - 16 - 20);  // off the ground and rising
    CHECK(erik->speed_y < 0);
    CHECK((place->flags & entity_flag::kAlwaysActive) != 0);
    world.switches = 0;
    room.ticks(60);
    CHECK_FALSE(world.cycles[0].running);
    CHECK(erik->y == Room::kFloor - 16);  // back down
    // One of the places that are written out.
    Entity* other = room.put({.kind = kind::kWeightlessness, .x = 0, .y = 0, .argument = 0x0001});
    room.tick();
    CHECK(other->box == Box{.left = 0x100, .top = 0x10, .right = 0x180, .bottom = 0x90});
}

TEST_CASE("bombers: he strolls and runs, throws three bombs at a Viking level with him; a bomb hurts") {
    Room room;
    World& world = room.world;
    const auto program = [&room](std::size_t at, std::initializer_list<int> bytes) {
        for (const int byte : bytes) {
            room.programs[at++] = static_cast<std::byte>(byte);
        }
    };
    // He throws a bomb a tick, three times, and then stands a long while; the bomb's burst takes two ticks.
    program(0x19ee, {0x0f, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x0f, 0x7f, 0x1a});
    program(0xbf0d, {0x0f, 0x01, 0x1a});
    program(0x1722, {0x0f, 0x02, 0x1a});
    Entity* erik = room.vikings(100);
    world.kinds.at(kind::kBomber).health = 4;
    world.kinds.at(kind::kBomber).hit_class = 2;
    Entity* bomber = room.put({.kind = kind::kBomber, .x = 240, .y = Room::kFloor - 8, .flags = 0x0040});
    room.ticks(3);
    CHECK(bomber->acts_as == tile::kEnemy);
    CHECK(bomber->speed_x == px(2.5));  // strolling
    bool threw = false;
    for (int i = 0; i < 60 && !threw; ++i) {
        room.tick();
        threw = room.has(kind::kBomb);
    }
    CHECK(threw);
    CHECK(bomber->speed_x == 0);
    CHECK(world.cues.back().value == 0x54);
    for (int i = 0; i < 80 && erik->health == 6; ++i) {
        room.tick();
    }
    CHECK(erik->health < 6);
    // Hurt, he runs when he is over it.
    bomber->message = message::kSwordBlow;
    bomber->message_value = 1;
    room.ticks(6);
    CHECK(bomber->health == 3);
    // A bomb thrown at a wall bursts there; one sent straight goes for the nearest Viking.
    Entity* bomb = room.put({.kind = kind::kBomb, .x = 600, .y = 200, .argument = 1});
    bomb->speed_x = px(6);
    Entity* straight = room.put({.kind = kind::kBomb, .x = 300, .y = 150, .flags = 0x0040, .argument = 0});
    room.ticks(2);
    CHECK(straight->speed_y > 0);
    CHECK(straight->speed_x != 0);  // towards the nearest Viking
    room.ticks(40);
    CHECK_FALSE(room.has(kind::kBomb));
    // One that starts in the air falls first.
    Entity* dropped = room.put({.kind = kind::kBomber, .x = 500, .y = 150, .flags = entity_flag::kFalls});
    room.ticks(40);
    CHECK(dropped->box.bottom == Room::kFloor - 1);
}

TEST_CASE("springs: a Viking coming down on one is thrown back up") {
    Room room;
    World& world = room.world;
    Entity* erik = world.create({.kind = kind::kErik, .x = 208, .y = 150});
    world.create({.kind = kind::kBaleog, .x = 400, .y = Room::kFloor - 16});
    world.create({.kind = kind::kOlaf, .x = 500, .y = Room::kFloor - 16});
    world.camera.y = 100;
    world.placements = {Placement{.x = 208, .y = Room::kFloor - 8, .width = 32, .height = 16, .kind = kind::kSpring}};
    world.kinds.at(kind::kSpring).width = 32;
    Entity* spring = world.create_placed(0);
    REQUIRE(spring != nullptr);
    spring->flags |= entity_flag::kAlwaysActive;
    room.ticks(2);
    CHECK(spring->acts_as == tile::kSpring);
    CHECK(room.cell(12, 17).tile_type == 0x01);  // its cells are solid while it is there
    int highest = erik->y;
    bool sprung = false;
    for (int i = 0; i < 80; ++i) {
        room.tick();
        sprung = sprung || (!world.cues.empty() && world.cues.back().value == 0x2d);
        if (sprung) {
            highest = erik->y < highest ? erik->y : highest;
        }
    }
    CHECK(sprung);
    CHECK(highest < 200);  // well above the spring again
    CHECK(erik->health == 6);
    spring->flags =
        static_cast<std::uint16_t>((spring->flags & ~entity_flag::kAlwaysActive) | entity_flag::kLeftTheActiveArea);
    room.tick();
    CHECK(room.cell(12, 17).tile_type == 0x16);
    CHECK(room.cell(13, 17).tile_type == 0x16);
}

TEST_CASE("items: food heals, the shield adds a point, keys open their locks, the rest as each does") {
    Room room;
    World& world = room.world;
    Entity* erik = room.vikings();
    Entity* baleog = world.entities[1].get();
    room.ticks(2);
    const auto use = [&](std::uint16_t item) {
        world.items[0] = {item, 0, 0, 0};
        world.chosen_square[0] = 0;
        world.cues.clear();
        room.tick(kE);
        room.tick();
    };
    // Food with nothing to heal is kept.
    use(1);
    CHECK(world.items[0][0] == 1);
    CHECK(world.cues.front().value == 0x03);
    erik->health = 2;
    use(1);
    CHECK(erik->health == 4);
    CHECK(world.items[0][0] == 0);
    use(5);
    CHECK(erik->health == 6);  // never more than he can have
    // The shield: a point on top, shown by the other health pictures; lost first.
    use(9);
    CHECK(erik->health == 8);
    CHECK(world.armour[0] == 2);
    use(9);
    CHECK(world.items[0][0] == 9);  // he has one already
    erik->message = message::kStruck;
    erik->message_value = 2;
    room.ticks(12);
    CHECK(erik->health == 6);
    CHECK(world.armour[0] == 0);
    // Used up, the next square that holds something is chosen.
    erik->health = 2;
    world.items[0] = {2, 0, 7, 0};
    world.chosen_square[0] = 0;
    room.tick(kE);
    room.tick();
    CHECK(world.chosen_square[0] == 2);
    // A key without its lock beside him does nothing; with it, the lock is told.
    use(6);
    CHECK(world.items[0][0] == 6);
    Entity* lock = room.put({.kind = kind::kLock, .x = erik->x + 4, .y = erik->y, .argument = 0x0001});
    lock->hit_class = 7;
    room.ticks(2);
    use(6);
    room.ticks(2);
    CHECK(world.items[0][0] == 0);
    CHECK(world.switches == 0x0001);
    CHECK_FALSE(room.has(kind::kLock));
    // The bombs are put down; the boots keep his feet down; fire arrows are Baleog's alone.
    use(3);
    CHECK(room.has(kind::kLitBomb));
    use(0x0a);
    CHECK(room.has(kind::kSmartBomb));
    use(0x0c);
    CHECK(erik->ignores_weightlessness);
    use(0x0b);
    CHECK(world.items[0][0] == 0x0b);
    use(0x0d);  // nothing here takes it
    CHECK(world.items[0][0] == 0x0d);
    Entity* taker = room.put({.kind = 0xea, .x = erik->x, .y = erik->y - 20});
    taker->acts_as = tile::kTaker;
    taker->hit_class = 0x0b;
    taker->wanted_item = 0x0e;
    room.ticks(2);
    use(0x0d);  // not the one it wants
    CHECK(world.items[0][0] == 0x0d);
    CHECK(taker->message == 0);
    use(0x0e);
    CHECK(world.items[0][0] == 0);
    CHECK(taker->message == message::kAsked);
    world.current_viking = 1;
    world.camera.x = 240;
    world.items[1] = {0x0b, 0, 0, 0};
    room.tick(kE);
    CHECK(world.items[1][0] == 0);
    world.items[1] = {0x0b, 0, 0, 0};
    room.tick();
    room.tick(kE);
    CHECK(world.items[1][0] == 0x0b);  // he has them already
    room.ticks(4, kD);
    room.tick();
    CHECK(room.has(kind::kFireArrow));
    CHECK(baleog->health == 6);
}

TEST_CASE("vikings: each world's deadly cells: water, lava, spikes, quicksand, rotating spikes") {
    for (const int hazard : {0x10, 0x12, 0x14, 0x13, 0x17, 0x15}) {
        Room room;
        World& world = room.world;
        // A pit in the floor with the hazard at its bottom.
        for (int column = 8; column < 12; ++column) {
            room.cell(column, 18).tile_type = static_cast<std::uint8_t>(hazard);
            room.cell(column, 19).tile_type = static_cast<std::uint8_t>(hazard);
        }
        Entity* erik = world.create({.kind = kind::kErik, .x = 160, .y = 200});
        world.create({.kind = kind::kBaleog, .x = 400, .y = Room::kFloor - 16});
        world.create({.kind = kind::kOlaf, .x = 500, .y = Room::kFloor - 16});
        world.camera.y = 140;
        room.ticks(60);
        INFO("hazard " << hazard);
        CHECK(erik->health == 0);
        CHECK(erik->dead);
        CHECK(world.current_viking == 1);
    }
    // Spikes overhead kill a Viking thrown up into them.
    Room room;
    for (int column = 4; column < 12; ++column) {
        room.cell(column, 12).tile_type = 0x14;
    }
    Entity* erik = room.vikings();
    room.ticks(2);
    erik->message = message::kThrown;
    erik->message_value = static_cast<std::uint16_t>(-0x0c00);
    room.ticks(30);
    CHECK(erik->dead);
}

TEST_CASE("switched cells: a wall that opens and a floor that is built, cell after cell") {
    Room room;
    World& world = room.world;
    world.cleared_cell = Cell{.metatile = 3, .tile_type = 0};
    world.filled_cell = Cell{.metatile = 9, .tile_type = 1};
    room.vikings();
    for (int row = 5; row < 9; ++row) {
        room.cell(20, row).tile_type = 0x0a;  // the wall's cells
    }
    for (int column = 24; column < 28; ++column) {
        room.cell(column, 10).tile_type = 0x0b;  // where the floor will be
    }
    world.placements = {Placement{.x = 20 * 16 + 8,
                                  .y = 5 * 16 + 8,
                                  .width = 16,
                                  .height = 16,
                                  .kind = kind::kOpeningWall,
                                  .argument = 0x8001},
                        Placement{.x = 24 * 16 + 8,
                                  .y = 10 * 16 + 8,
                                  .width = 16,
                                  .height = 16,
                                  .kind = kind::kBuildingFloor,
                                  .argument = 0x0002}};
    REQUIRE(world.create_placed(0) != nullptr);
    REQUIRE(world.create_placed(1) != nullptr);
    room.ticks(5);
    CHECK(room.cell(20, 5).tile_type == 0x0a);  // nothing until their switches are on
    world.switches = 0x0001;
    bool shook = false;
    for (int i = 0; i < 10; ++i) {
        room.tick();
        shook = shook || world.shake_x.ticks != 0;
    }
    CHECK(shook);
    for (int row = 5; row < 9; ++row) {
        CHECK(room.cell(20, row) == Cell{.metatile = 3, .tile_type = 0});
    }
    CHECK(world.gone.test(0));
    CHECK_FALSE(room.has(kind::kOpeningWall));
    CHECK(room.cell(24, 10).tile_type == 0x0b);
    world.switches = 0x0003;
    room.ticks(10);
    for (int column = 24; column < 28; ++column) {
        CHECK(room.cell(column, 10) == Cell{.metatile = 9, .tile_type = 1});
    }
    CHECK_FALSE(room.has(kind::kBuildingFloor));
}

TEST_CASE("sinking plates: a block that comes to rest on one sinks it, and its switches flip") {
    Room room;
    World& world = room.world;
    room.programs[0x8f71] = std::byte{0x0f};  // the sinking takes a few ticks
    room.programs[0x8f72] = std::byte{0x03};
    Entity* erik = room.vikings();
    world.placements = {Placement{
        .x = 100, .y = Room::kFloor - 40, .width = 16, .height = 16, .kind = kind::kSinkingPlate, .argument = 0x0004}};
    Entity* plate = world.create_placed(0);
    REQUIRE(plate != nullptr);
    (void)room.put({.kind = kind::kSinkingPlate, .x = 300, .y = 100, .argument = 0x8002});  // one that needs a switch
    room.ticks(3);
    CHECK(plate->acts_as == tile::kPlatform);
    // A Viking on it keeps it active; it doesn't sink under him.
    erik->y = static_cast<std::int16_t>(plate->box.top - 16);
    erik->box.top = static_cast<std::int16_t>(erik->y - 16);
    erik->box.bottom = static_cast<std::int16_t>(erik->y + 15);
    room.ticks(3);
    CHECK((plate->flags & entity_flag::kAlwaysActive) != 0);
    CHECK(world.switches == 0);
    erik->x = 200;
    erik->box.left = 192;
    erik->box.right = 207;
    room.ticks(3);
    // A block resting on it does.
    Entity* block = room.put({.kind = 0xea, .x = 100, .y = plate->box.top - 8});
    block->acts_as = tile::kGround;
    block->hit_mask = 0x0800;
    room.ticks(2);
    CHECK(block->hit_mask == 0);
    CHECK(plate->speed_y == px(2));
    room.ticks(6);
    CHECK(world.switches == 0x0004);
    CHECK(world.gone.test(0));
    CHECK_FALSE(world.entities.at(3));
}

TEST_CASE("world: random numbers as the original draws them, and the HUD's items") {
    World world;
    CHECK(world.random() == 0);
    CHECK(world.random() == 0x015a);
    world.attract = true;
    const std::uint16_t first = world.random();
    CHECK(world.random() != first);
    world.items[2][3] = 7;
    world.items[0][0] = 4;
    world.chosen_square[2] = 1;
    world.update_hud();
    CHECK(world.shown_items[0] == 4);
    CHECK(world.shown_items[11] == 7);
}
