// Tests for the kinds of the worlds after the spaceship (src/lvr/platforms.cpp and the families' files) and the exit
// (conversations.cpp), each on a small stage of its own. How they play in the levels is checked by the replays
// against their goldens (./scripts/goldens.sh).
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <vector>

#include "lvr/entity.hpp"
#include "lvr/kinds.hpp"
#include "lvr/play.hpp"
#include "lvr/world.hpp"

using namespace lvr;

namespace {

class Still : public Entity {
  public:
    void update(World& /*world*/) override {}
};

constexpr int kColumns = 40;
constexpr int kRows = 20;

// A stage of free cells, with stand-ins for the three Vikings (they stand still) far from everything, and
// animation programs that end at once wherever a kind starts one.
struct Stage {
    std::vector<std::byte> programs = std::vector<std::byte>(0x10000, std::byte{0x1A});
    World world;

    explicit Stage(int database = database::kPrehistoric, int vikings_at = 600) {
        world.programs = programs;
        world.database = database;
        world.host().camera.max_x = world.host().camera.max_y = 2000;
        world.map = Map{.width = kColumns,
                        .height = kRows,
                        .cells = std::vector<Cell>(std::size_t{kColumns} * kRows, Cell{.metatile = 0, .tile_type = 0})};
        world.make = [database](std::uint16_t kind) -> std::unique_ptr<Entity> {
            if (kind >= kVikingSlots) {
                if (auto made = make_kind(kind, database)) {
                    return made;
                }
            }
            return make_entity<Still>();
        };
        for (KindRecord& record : world.kinds) {
            record.pieces = 1;
            record.width = record.height = 16;
            record.sprite_entry = KindRecord::kPackedSprites;
            record.max_step_x = record.max_step_y = 0x1000;
            record.health = 4;
        }
        for (std::uint16_t viking = 0; viking < kVikingSlots; ++viking) {
            Entity* stand_in = world.create({.kind = viking, .x = vikings_at, .y = 24 + (40 * viking)});
            stand_in->hit_class = 1;
            stand_in->hit_mask = 0x0404;
            stand_in->acts_as = tile::kViking;
            stand_in->health = 6;
        }
    }
    // Always active, unless `pinned` is false: the stage is larger than the view.
    Entity* put(std::uint16_t kind, int x, int y, std::uint16_t argument = 0, std::uint16_t flags = 0,
                bool pinned = true) {
        if (pinned) {
            flags |= entity_flag::kAlwaysActive;
        }
        world.placements.push_back(Placement{.x = x, .y = y, .kind = kind, .flags = flags, .argument = argument});
        return world.create_placed(static_cast<int>(world.placements.size()) - 1);
    }
    Entity& viking(int slot) { return *world.entities.at(static_cast<std::size_t>(slot)); }
    // Puts a Viking's stand-in somewhere.
    void move_viking(int slot, int x, int y) {
        Entity& entity = viking(slot);
        entity.x = entity.previous_x = static_cast<std::int16_t>(x);
        entity.y = entity.previous_y = static_cast<std::int16_t>(y);
        entity.box = {.left = static_cast<std::int16_t>(x - 8),
                      .top = static_cast<std::int16_t>(y - 8),
                      .right = static_cast<std::int16_t>(x + 7),
                      .bottom = static_cast<std::int16_t>(y + 7)};
    }
    void solid(int column, int row, int columns = 1, int rows = 1) {
        for (int r = row; r < row + rows; ++r) {
            for (int c = column; c < column + columns; ++c) {
                world.map.cell(c, r)->tile_type = 0x01;
            }
        }
    }
    void tick(int ticks = 1) {
        for (int i = 0; i < ticks; ++i) {
            for (int part = 0; part < 3; ++part) {
                world.step(part);
            }
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

TEST_CASE("platform: up and down between floor and ceiling while its switches are on; it carries a Viking") {
    Stage stage{database::kEgypt, 250};  // the Vikings near it: it isn't always active
    World& world = stage.world;
    stage.solid(0, 2, kColumns);                                     // a ceiling
    stage.solid(0, 12, kColumns);                                    // a floor
    Entity* platform = stage.put(0xb0, 104, 100, 0x0204, 0, false);  // two pixels a tick, on switch 4
    REQUIRE(platform != nullptr);
    stage.tick(3);
    CHECK(platform->y == 100);  // its switch is off
    CHECK(platform->acts_as == tile::kPlatform);
    world.switches = 0x0004;
    stage.move_viking(0, 104, 84);  // standing on it
    stage.tick(5);
    CHECK(platform->y > 100);
    CHECK(stage.viking(0).y - platform->y == 84 - 100);          // carried along
    CHECK((platform->flags & entity_flag::kAlwaysActive) != 0);  // active while a Viking rides it
    stage.move_viking(0, 250, 24);
    int lowest = 0;
    int highest = 1000;
    for (int i = 0; i < 300; ++i) {
        stage.tick();
        lowest = platform->y > lowest ? platform->y : lowest;
        highest = platform->y < highest ? platform->y : highest;
    }
    CHECK(lowest == 184);  // its bottom edge on the floor
    CHECK(highest == 56);  // its top edge under the ceiling
    CHECK((platform->flags & entity_flag::kAlwaysActive) == 0);
    world.switches = 0;
    stage.tick();
    const int stopped_at = platform->y;
    stage.tick(5);
    CHECK(platform->y == stopped_at);
}

TEST_CASE("platform: one that acts as a wall and squashes; one that goes across and crushes") {
    Stage stage{database::kEgypt};
    World& world = stage.world;
    stage.solid(0, 12, kColumns);
    Entity* press = stage.put(0xa0, 104, 100, 0xc100);  // squashes, a wall, one pixel a tick, no switch
    REQUIRE(press != nullptr);
    stage.move_viking(1, 104, 150);  // under it
    stage.tick(40);
    CHECK(press->acts_as == tile::kGround);
    CHECK(stage.viking(1).message == message::kSquashed);
    CHECK(stage.viking(1).message_value == 0x0100);

    stage.solid(30, 0, 1, kRows);
    stage.solid(14, 0, 1, kRows);
    Entity* crusher = stage.put(0xa5, 300, 40, 0x0200);
    REQUIRE(crusher != nullptr);
    stage.move_viking(2, 330, 40);  // in its way
    stage.tick(12);
    CHECK(crusher->acts_as == tile::kGround);
    CHECK(stage.viking(2).message == message::kCrushed);
    int leftmost = 1000;
    int rightmost = 0;
    stage.move_viking(2, 600, 104);
    for (int i = 0; i < 300; ++i) {
        stage.tick();
        leftmost = crusher->x < leftmost ? crusher->x : leftmost;
        rightmost = crusher->x > rightmost ? crusher->x : rightmost;
    }
    CHECK(leftmost == 248);  // between the two walls
    CHECK(rightmost == 472);
    (void)world;
}

TEST_CASE("platform: the one that goes round comes back to where it started") {
    Stage stage{database::kEgypt};
    Entity* platform = stage.put(0x7d, 300, 150, 0);
    REQUIRE(platform != nullptr);
    stage.tick();
    CHECK((platform->flags & entity_flag::kAlwaysActive) != 0);
    const int x = platform->x;
    const int y = platform->y;
    int furthest = 0;
    for (int i = 0; i < 60; ++i) {  // twenty stretches of three ticks
        stage.tick();
        furthest = platform->x - x > furthest ? platform->x - x : furthest;
    }
    CHECK(furthest > 60);
    CHECK(platform->x == x);
    CHECK(platform->y == y);
    CHECK(make_platform(0x01) == nullptr);
}

TEST_CASE("prehistoric: the brawler walks, turns at a wall, strikes a Viking he touches, and can be killed") {
    Stage stage;
    World& world = stage.world;
    stage.solid(0, 12, kColumns);
    stage.solid(20, 0, 1, 12);
    Entity* brawler = stage.put(prehistoric_kind::kBrawler, 200, 184);
    REQUIRE(brawler != nullptr);
    stage.tick(3);
    CHECK(brawler->acts_as == tile::kEnemy);
    CHECK(brawler->speed_x == px(3));
    CHECK(brawler->x > 200);
    stage.tick(60);
    CHECK(brawler->facing_left());  // the wall turned him
    // A Viking in his way, level with him: struck when the strike's animation ends.
    stage.move_viking(0, brawler->x - 12, 184);
    stage.tick(3);
    CHECK(brawler->speed_x == 0);
    CHECK(stage.viking(0).message == message::kStruck);
    CHECK((stage.viking(0).message_value & 0x7fff) == 2);
    CHECK(stage.has(kind::kSpark));
    stage.move_viking(0, 600, 24);
    // A blow he survives throws him; one he doesn't leaves his remains, and he doesn't come back.
    brawler->message = message::kSwordBlow;
    brawler->message_value = 1;
    stage.tick();
    CHECK(brawler->health == 3);
    brawler->message = message::kSwordBlow;
    brawler->message_value = 5;
    const int placement = brawler->placement;
    stage.tick(3);
    CHECK(!stage.has(prehistoric_kind::kBrawler));
    CHECK(world.gone.test(static_cast<std::size_t>(placement)));

    world.gravity = 0x40;
    Entity* fast = stage.put(prehistoric_kind::kFastBrawler, 100, 170, 0, entity_flag::kFalls);
    REQUIRE(fast != nullptr);
    stage.tick(12);
    CHECK(fast->speed_x == px(4));
    // What the cutscenes tell him.
    fast->message = message::kLedStand;
    stage.tick();
    CHECK(fast->speed_x == 0);
    fast->message = message::kLedTurn;
    stage.tick();
    CHECK(fast->speed_x == px(4));
    fast->message = message::kLedAttack;
    stage.tick();
    CHECK(fast->speed_x == 0);
    fast->message = message::kLedLeave;
    stage.tick();
    CHECK(!stage.has(prehistoric_kind::kFastBrawler));
}

TEST_CASE("prehistoric: the spitter is held up by a Viking in his way; his spit hurts a Viking and bursts on a wall") {
    Stage stage;
    World& world = stage.world;
    stage.solid(0, 12, kColumns);
    Entity* spitter = stage.put(prehistoric_kind::kSpitter, 200, 184);
    REQUIRE(spitter != nullptr);
    stage.tick(2);
    CHECK(spitter->speed_x == px(1));
    stage.move_viking(0, spitter->box.right + 9, 184);
    stage.tick(2);
    CHECK(spitter->speed_x == 0);
    stage.move_viking(0, 600, 24);
    spitter->message = message::kHeadButt;
    spitter->message_value = 0x8001;  // from someone facing left: from the front
    stage.tick();
    CHECK(spitter->health == 3);
    CHECK(spitter->speed_x == px(-4));  // hit from the front: thrown back
    stage.tick(3);
    CHECK(spitter->speed_x == px(1));
    for (const int told : {0x23, 0x22, 0x26, 0x25}) {
        spitter->message = static_cast<std::uint16_t>(told);
        stage.tick(2);
    }
    spitter->message = message::kBlast;
    spitter->message_value = 9;
    stage.tick();
    CHECK(!stage.has(prehistoric_kind::kSpitter));

    stage.solid(30, 0, 1, 12);
    Entity* spit = world.create({.kind = 0x64, .x = 400, .y = 100, .flags = entity_flag::kAlwaysActive});
    REQUIRE(spit != nullptr);
    spit->hit_mask = 0;
    stage.tick(3);
    CHECK(spit->speed_x == px(8));
    stage.tick(12);
    CHECK(!stage.has(prehistoric_kind::kSpit));  // burst on the wall and gone
    spit = world.create({.kind = 0x64, .x = 400, .y = 100, .flags = entity_flag::kAlwaysActive});
    REQUIRE(spit != nullptr);
    spit->hit_mask = 0;
    stage.move_viking(1, 440, 100);
    stage.tick(6);
    CHECK(stage.viking(1).message == message::kShot);
    CHECK(!stage.has(prehistoric_kind::kSpit));
}

TEST_CASE("prehistoric: the roller rolls and walks by turns; rolling, blows glance off him") {
    Stage stage;
    stage.solid(0, 12, kColumns);
    Entity* roller = stage.put(prehistoric_kind::kRoller, 200, 184);
    REQUIRE(roller != nullptr);
    stage.tick(2);
    CHECK(roller->speed_x == px(4.5));
    CHECK((roller->hit_mask & 0x4002) == 0x4002);
    roller->message = message::kSwordBlow;
    roller->message_value = 0x8001;  // from the right, and he faces right: he turns
    stage.tick();
    CHECK(roller->facing_left());
    CHECK(roller->health == 4);
    roller->message = message::kArrowBlow;
    roller->message_value = 2;  // too weak
    stage.tick();
    CHECK(stage.has(prehistoric_kind::kRoller));
    // Rolling into a Viking hurts him and turns the roller.
    stage.move_viking(1, roller->x - 14, 184);
    stage.tick(2);
    CHECK(stage.viking(1).message == message::kStruck);
    CHECK(!roller->facing_left());
    stage.move_viking(1, 600, 64);
    stage.tick(110);  // sooner or later he walks
    int walking = 0;
    for (int i = 0; i < 150 && walking == 0; ++i) {
        stage.tick();
        walking = roller->speed_x == px(3.5) ? 1 : 0;
    }
    REQUIRE(walking == 1);
    CHECK((roller->hit_mask & 0x0101) == 0x0101);
    roller->message = message::kSwordBlow;
    roller->message_value = 1;
    stage.tick();
    CHECK(roller->health == 3);
    stage.tick(3);
    CHECK(roller->speed_x == px(4.5));  // over it, he rolls again
    for (const int told : {0x23, 0x22, 0x26, 0x25}) {
        roller->message = static_cast<std::uint16_t>(told);
        stage.tick(2);
    }
    roller->message = message::kLedColours;
    stage.tick();
    CHECK(!stage.has(prehistoric_kind::kRoller));
    Entity* other = stage.put(prehistoric_kind::kRoller, 200, 184);
    REQUIRE(other != nullptr);
    stage.tick(2);
    other->message = message::kBlast;
    other->message_value = 4;
    stage.tick();
    CHECK(!stage.has(prehistoric_kind::kRoller));
    CHECK(stage.has(kind::kRemains));
}

TEST_CASE("prehistoric: the bridge stands, solid, until its switches come on; then it lies down as floor") {
    Stage stage;
    World& world = stage.world;
    Entity* bridge = stage.put(prehistoric_kind::kFallingBridge, 104, 168, 0x0002);
    REQUIRE(bridge != nullptr);
    stage.tick(2);
    CHECK(world.map.tile_type_at(104, 150) == 0x01);
    CHECK(world.map.tile_type_at(104, 104) == 0x01);
    CHECK(world.map.tile_type_at(120, 168) == 0x00);
    world.switches = 0x0002;
    stage.tick(3);
    CHECK(world.map.tile_type_at(104, 150) == 0x00);
    for (const int x : {120, 136, 152, 168}) {
        CHECK(world.map.tile_type_at(x, 168) == 0x01);
    }
    CHECK(!stage.has(prehistoric_kind::kFallingBridge));  // part of the map now
    // The kind that stays, with its switches on from the start, facing left.
    Entity* other = stage.put(prehistoric_kind::kFallingBridge, 400, 168, 0x8002, entity_flag::kFacingLeft);
    REQUIRE(other != nullptr);
    stage.tick(3);
    CHECK(world.map.tile_type_at(340, 168) == 0x01);
    CHECK(stage.has(prehistoric_kind::kFallingBridge));
}

TEST_CASE("prehistoric: a gate is solid while its switches are off; a block falls and squashes") {
    Stage stage;
    World& world = stage.world;
    Entity* gate = stage.put(prehistoric_kind::kGate, 104, 100, 0x0001);
    Entity* stays_open = stage.put(prehistoric_kind::kGateThatStaysOpen, 140, 100, 0x0001);
    REQUIRE(gate != nullptr);
    REQUIRE(stays_open != nullptr);
    stage.tick(2);
    CHECK(gate->acts_as == tile::kGround);
    CHECK((gate->hit_mask & 0x0002) != 0);
    world.switches = 0x0001;
    stage.tick(2);
    CHECK(gate->acts_as == tile::kBody);
    CHECK((gate->hit_mask & 0x0002) == 0);
    world.switches = 0;
    stage.tick(2);
    CHECK(gate->acts_as == tile::kGround);
    CHECK(stays_open->acts_as == tile::kBody);

    stage.solid(0, 12, kColumns);
    Entity* block = stage.put(prehistoric_kind::kFallingBlock, 300, 60);
    REQUIRE(block != nullptr);
    world.gravity = 0x40;
    stage.move_viking(0, 300, 150);
    stage.tick(60);
    CHECK(block->acts_as == tile::kGround);
    CHECK(stage.viking(0).message == message::kSquashed);
    stage.move_viking(0, 600, 24);
    stage.tick(60);
    CHECK(block->y == 184);  // on the floor
    CHECK((block->flags & entity_flag::kFalls) == 0);
    CHECK(stage.put(prehistoric_kind::kOtherFallingBlock, 340, 184) != nullptr);
    stage.tick(2);
}

TEST_CASE("prehistoric: the ground shakes now and then until the switches are on") {
    Stage stage;
    World& world = stage.world;
    Entity* quake = stage.put(prehistoric_kind::kQuake, 0, 0, 0x0008);
    REQUIRE(quake != nullptr);
    stage.tick();
    CHECK(quake->countdown >= 0x168);
    quake->countdown = 1;
    stage.tick(2);
    CHECK(world.shake_y.amount == 2);
    world.switches = 0x0008;
    stage.tick(2);
    CHECK(!stage.has(prehistoric_kind::kQuake));
}

TEST_CASE("prehistoric: a bubble rises, dips under a Viking and bursts; the sender sends them one after the other") {
    Stage stage;
    World& world = stage.world;
    Entity* bubble = stage.put(prehistoric_kind::kBubble, 104, 200, 0x30);
    REQUIRE(bubble != nullptr);
    stage.tick(4);
    CHECK(bubble->acts_as == tile::kPlatform);
    CHECK(bubble->y == 196);
    stage.move_viking(0, 104, bubble->y - 16);
    stage.tick(2);
    CHECK(bubble->speed_y == px(2.5));  // it dips
    CHECK(stage.viking(0).y > 179);
    stage.tick(8);
    CHECK(bubble->speed_y == px(-1));
    stage.move_viking(0, 600, 24);
    stage.tick(2);
    CHECK(bubble->speed_y == px(-2.5));  // and bobs up when he steps off
    stage.tick(60);
    CHECK(!stage.has(prehistoric_kind::kBubble));  // burst
    Entity* small = stage.put(prehistoric_kind::kSmallBubble, 150, 200);
    Entity* middle = stage.put(prehistoric_kind::kMiddleBubble, 180, 200, 5);
    REQUIRE(small != nullptr);
    REQUIRE(middle != nullptr);
    stage.tick(3);
    CHECK(small->speed_y == px(-2));
    CHECK(middle->speed_y == px(-1.5));
    stage.tick(10);
    CHECK(!stage.has(prehistoric_kind::kMiddleBubble));

    Entity* sender = stage.put(prehistoric_kind::kSender, 20, 20, 0x0c);
    REQUIRE(sender != nullptr);
    stage.tick(2);
    CHECK(stage.has(prehistoric_kind::kBubble));
    CHECK(sender->countdown >= 0x12);
    for (const int first : {0x14, 0x19, 0x1e, 0x21, 0x28, 0x36, 0x3c, 0x3d, 0x3e, 0x50}) {
        Entity* another = stage.put(prehistoric_kind::kSender, 20, 20, static_cast<std::uint16_t>(first));
        REQUIRE(another != nullptr);
        stage.tick(3);
        world.remove(another->slot);
    }
    (void)world;
}

TEST_CASE("prehistoric: fire comes out of a wall while the switches are on, flies, and burns a Viking") {
    Stage stage;
    World& world = stage.world;
    stage.solid(6, 6);
    Entity* shooter = stage.put(prehistoric_kind::kFireShooterInAWall, 104, 104, 0x0001);
    REQUIRE(shooter != nullptr);
    stage.tick(3);
    CHECK(!stage.has(prehistoric_kind::kFireball));
    world.switches = 0x0001;
    stage.tick(2);
    CHECK(stage.has(prehistoric_kind::kFireball));
    CHECK(shooter->countdown > 0x20);
    stage.move_viking(2, 180, 104);
    stage.tick(12);
    CHECK(stage.viking(2).message == message::kBurned);
    CHECK((stage.viking(2).message_value & 0x7fff) == 4);
    stage.move_viking(2, 600, 104);
    stage.tick(80);                       // the next ones give out on their own
    world.map.cell(6, 6)->tile_type = 0;  // its wall is gone: so is the shooter
    stage.tick(2);
    CHECK(!stage.has(prehistoric_kind::kFireShooterInAWall));

    // A drop falls and splashes on the floor, or on a Viking.
    stage.solid(0, 12, kColumns);
    Entity* drop = world.create({.kind = 0x2b, .x = 300, .y = 100, .flags = entity_flag::kAlwaysActive});
    REQUIRE(drop != nullptr);
    drop->hit_mask = 0;
    stage.tick(3);
    CHECK(drop->speed_y == px(6));
    stage.tick(30);
    CHECK(!stage.has(prehistoric_kind::kFireDrop));
    drop = world.create({.kind = 0x2b, .x = 300, .y = 100, .flags = entity_flag::kAlwaysActive});
    REQUIRE(drop != nullptr);
    drop->hit_mask = 0;
    stage.move_viking(1, 300, 130);
    stage.tick(8);
    CHECK(stage.viking(1).message == message::kBurned);
}

TEST_CASE("prehistoric: the relay turns its other switches on once its own are") {
    Stage stage;
    World& world = stage.world;
    Entity* relay = stage.put(prehistoric_kind::kSwitchRelay, 104, 104, 0x0401);
    REQUIRE(relay != nullptr);
    stage.tick(3);
    CHECK(world.switches == 0);
    world.switches = 0x0001;
    stage.tick(4);
    CHECK(world.switches == 0x0401);
    CHECK(stage.put(prehistoric_kind::kSwitchRelay, 150, 104, 0x0401) != nullptr);  // one that finds them on already
    stage.tick(2);
    CHECK(make_of(Home::kPrehistoric, 0x01) == nullptr);
}

TEST_CASE("exit: the level is done when the three Vikings are at it; a world's last level has a word first") {
    Stage stage;
    World& world = stage.world;
    world.level = 6;
    Entity* way_out = stage.put(kind::kExit, 104, 104);
    REQUIRE(way_out != nullptr);
    stage.tick(3);
    CHECK(!world.level_done);
    stage.move_viking(0, 100, 104);
    stage.move_viking(1, 120, 104);
    stage.tick(2);
    CHECK(!world.level_done);  // Olaf isn't there yet
    stage.move_viking(2, 90, 104);
    stage.tick(2);
    CHECK(world.level_done);

    for (const int level : {4, 11, 17, 25, 33}) {
        Stage last;
        last.world.level = level;
        std::vector<std::uint8_t> texts(0x100 * 2, 0);
        for (std::size_t i = 0; i < 0x100; ++i) {
            texts[(2 * i) + 1] = 0x02;
        }
        texts.insert(texts.end(), {6, 3, 'H', 'I', 0});
        last.world.texts = texts;
        REQUIRE(last.put(kind::kExit, 104, 104) != nullptr);
        for (int viking = 0; viking < kVikingSlots; ++viking) {
            last.move_viking(viking, 100, 104);
        }
        last.tick(2);
        CHECK(!last.world.level_done);
        CHECK(last.viking(0).message == message::kConversation);
        last.tick();
        CHECK(!last.world.lines.empty());
    }
}

TEST_CASE("prehistoric: fire in arcs, three ways by turns; a ledge that falls when hit twice; a pool that drains") {
    Stage stage;
    World& world = stage.world;
    stage.solid(0, 12, kColumns);
    world.gravity = 0x40;
    world.switches = 0x0001;
    Entity* shooter = stage.put(prehistoric_kind::kArcingFireShooter, 104, 104, 0x0001);
    Entity* plain = stage.put(prehistoric_kind::kFireShooter, 200, 40, 0x0001);
    REQUIRE(shooter != nullptr);
    REQUIRE(plain != nullptr);
    stage.tick(3);
    CHECK(stage.has(prehistoric_kind::kArcingFire));
    CHECK(stage.has(prehistoric_kind::kFireball));
    stage.tick(90);  // the next two arcs, each gone by itself or on the floor
    world.switches = 0;
    stage.tick(60);
    CHECK(!stage.has(prehistoric_kind::kArcingFire));
    Entity* fire = world.create({.kind = 0x2a, .x = 300, .y = 100, .flags = entity_flag::kAlwaysActive, .argument = 1});
    REQUIRE(fire != nullptr);
    fire->hit_mask = 0;
    stage.move_viking(0, 300, 80);
    stage.tick(4);
    CHECK(stage.viking(0).message == message::kBurned);
    stage.move_viking(0, 600, 24);

    stage.solid(30, 8, 3);  // a floor under it, well inside the active area
    Entity* ledge = stage.put(prehistoric_kind::kHangingLedge, 500, 60);
    REQUIRE(ledge != nullptr);
    stage.tick(2);
    CHECK(ledge->acts_as == tile::kPlatform);
    ledge->message = message::kArrowBlow;
    stage.tick(2);
    CHECK(ledge->y == 60);
    ledge->message = message::kSwordBlow;
    stage.tick(40);
    CHECK(ledge->y > 100);  // down on the floor
    CHECK(world.map.tile_type_at(500, ledge->y) == 0x05);

    Entity* pool = stage.put(prehistoric_kind::kDeadlyPool, 560, 100, 0x0004);
    REQUIRE(pool != nullptr);
    stage.tick(2);
    CHECK(pool->acts_as == tile::kLava);
    world.switches = 0x0004;
    stage.tick(3);
    CHECK(!stage.has(prehistoric_kind::kDeadlyPool));
}

TEST_CASE("egypt: the hopper runs, hops, strikes a Viking he touches and jumps away") {
    Stage stage{database::kEgypt};
    World& world = stage.world;
    world.gravity = 0x40;
    stage.solid(0, 12, kColumns);
    Entity* hopper = stage.put(egypt_kind::kHopper, 200, 184);
    REQUIRE(hopper != nullptr);
    stage.tick(3);
    CHECK(hopper->speed_x == px(3.5));
    bool hopped = false;
    for (int i = 0; i < 40 && !hopped; ++i) {
        stage.tick();
        hopped = hopper->speed_y < 0;
    }
    CHECK(hopped);
    stage.tick(60);
    stage.move_viking(0, hopper->x, hopper->y);
    stage.tick(2);
    stage.tick(3);
    CHECK(stage.viking(0).message == message::kStruck);
    stage.move_viking(0, 600, 24);
    stage.tick(40);
    hopper->message = message::kSwordBlow;
    hopper->message_value = 1;
    stage.tick(4);
    CHECK(hopper->health == 3);
    for (const int told : {0x23, 0x22, 0x26, 0x25}) {
        hopper->message = static_cast<std::uint16_t>(told);
        stage.tick(2);
    }
    hopper->message = message::kLedLeave;
    stage.tick();
    CHECK(!stage.has(egypt_kind::kHopper));
    // Spikes under one kill him.
    stage.solid(0, 6, 4);
    world.map.cell(1, 6)->tile_type = 0x14;
    Entity* unlucky = stage.put(egypt_kind::kHopper, 24, 88);
    REQUIRE(unlucky != nullptr);
    stage.tick(4);
    CHECK(!stage.has(egypt_kind::kHopper));
    CHECK(make_of(Home::kEgypt, 0x01) == nullptr);
}

TEST_CASE("egypt: the one in the pot keeps down, comes up and throws; only up he can be touched") {
    Stage stage{database::kEgypt};
    Entity* dweller = stage.put(egypt_kind::kPotDweller, 200, 184);
    REQUIRE(dweller != nullptr);
    stage.tick(2);
    CHECK(dweller->hit_mask == 0x0200);
    CHECK(dweller->box.top == dweller->box.bottom);
    dweller->message = message::kSwordBlow;
    dweller->message_value = 1;
    stage.tick();
    CHECK(dweller->health == 3);
    bool came_up = false;
    bool threw = false;
    for (int i = 0; i < 0x40; ++i) {
        stage.tick();
        came_up = came_up || dweller->hit_mask == 0x1381;
        threw = threw || stage.has(kind::kBomb);  // as soon as he is up: the animation ends at once here
    }
    CHECK(came_up);
    CHECK(threw);
    stage.tick(60);
    dweller->message = message::kBlast;
    dweller->message_value = 8;
    stage.tick();
    CHECK(!stage.has(egypt_kind::kPotDweller));
}

TEST_CASE("egypt: a ball hit twice falls, rolls and fills its hole; a block is pushed off an edge and lands") {
    Stage stage{database::kEgypt};
    World& world = stage.world;
    world.gravity = 0x40;
    stage.solid(0, 12, kColumns);
    world.map.cell(8, 12)->tile_type = 0x13;  // the hole
    Entity* ball = stage.put(egypt_kind::kBall, 104, 60);
    REQUIRE(ball != nullptr);
    stage.tick(2);
    ball->message = message::kArrowBlow;
    stage.tick(2);
    CHECK(ball->y == 60);
    ball->message = message::kArrowBlow;
    stage.tick(80);
    CHECK(!stage.has(egypt_kind::kBall));  // it rolled to the hole and became part of the map there
    int filled = 0;
    for (const Cell& cell : world.map.cells) {
        filled += cell.metatile == 7 ? 1 : 0;
    }
    CHECK(filled == 1);

    stage.solid(14, 8, 4);  // a ledge
    Entity* block = stage.put(egypt_kind::kPushedBlock, 264, 120);
    REQUIRE(block != nullptr);
    stage.tick(2);
    CHECK(block->acts_as == tile::kGround);
    for (int i = 0; i < 30 && stage.has(egypt_kind::kPushedBlock) && block->y == 120; ++i) {
        block->message = message::kPushed;
        block->message_value = 0x0200;
        stage.tick();
    }
    CHECK(world.pushing_sound == false);  // it fell: the scrape has stopped
    stage.tick(40);
    CHECK(!stage.has(egypt_kind::kPushedBlock));  // part of the map where it landed
    CHECK(world.map.tile_type_at(296, 180) == 0x01);
    // A Viking on the far side of a pushed block is told.
    Entity* other = stage.put(egypt_kind::kPushedBlock, 100, 184);
    REQUIRE(other != nullptr);
    stage.tick(2);
    stage.move_viking(1, other->box.right + 9, 184);
    other->message = message::kPushed;
    other->message_value = 0x0200;
    stage.tick();
    CHECK(stage.viking(1).message == message::kCrushed);
}

TEST_CASE("egypt: a switched cell; a floor of spikes that folds away; a block that sinks under a Viking") {
    Stage stage{database::kEgypt};
    World& world = stage.world;
    Entity* cell = stage.put(egypt_kind::kSwitchedCell, 104, 104, 0x0001);
    REQUIRE(cell != nullptr);
    stage.tick(2);
    CHECK(cell->acts_as == tile::kPlatformStop);
    world.switches = 0x0001;
    stage.tick(2);
    CHECK(world.map.tile_type_at(104, 104) == 0x01);
    world.switches = 0;
    stage.tick(2);
    CHECK(world.map.tile_type_at(104, 104) == 0x0a);
    CHECK(stage.put(egypt_kind::kSwitchedCell, 136, 104, 0) != nullptr);  // one that is on from the start
    stage.tick(2);

    Entity* spikes = stage.put(egypt_kind::kSpikedFloor, 300, 104, 0x0002);
    REQUIRE(spikes != nullptr);
    stage.tick(2);
    CHECK(spikes->acts_as == tile::kSpikes);
    stage.move_viking(0, 300, 88);
    stage.tick(2);
    CHECK(stage.viking(0).message == message::kImpaled);
    world.switches = 0x0002;
    stage.tick(3);
    CHECK(!stage.has(egypt_kind::kSpikedFloor));

    world.gravity = 0x40;
    stage.solid(0, 12, kColumns);
    world.map.cell(25, 9)->tile_type = 0x0a;
    Entity* block = stage.put(egypt_kind::kSinkingBlock, 408, 60, 0x0008);
    REQUIRE(block != nullptr);
    stage.tick(2);
    stage.move_viking(1, 408, 44);
    stage.tick(60);
    CHECK(block->y == 136);  // sunk to the floor under it
    stage.move_viking(1, 600, 64);
    world.switches = 0x0008;
    world.map.cell(25, 9)->tile_type = 0;
    stage.tick(60);
    CHECK(!stage.has(egypt_kind::kSinkingBlock));
    CHECK(world.map.tile_type_at(408, 184) == 0x01);
}

TEST_CASE("egypt: a platform drops with its switch, rests, and drops for good under a Viking") {
    Stage stage{database::kEgypt};
    World& world = stage.world;
    world.gravity = 0x40;
    stage.solid(0, 12, kColumns);
    world.map.cell(6, 6)->tile_type = 0x0a;
    Entity* platform = stage.put(egypt_kind::kDroppingPlatform, 104, 40, 0x0001);
    REQUIRE(platform != nullptr);
    stage.tick(3);
    CHECK(platform->y == 40);
    world.switches = 0x0001;
    stage.tick(40);
    CHECK(platform->y == 88);  // on the floor of type 0A
    CHECK(world.values.at(0) == 1);
    stage.move_viking(0, 104, 72);
    stage.tick(60);
    CHECK(!stage.has(egypt_kind::kDroppingPlatform));  // it dropped to the ground and broke
    CHECK(stage.put(egypt_kind::kDroppingPlatform, 200, 40, 0x0001) !=
          nullptr);  // one created later rests from the start
    stage.tick(2);
}

TEST_CASE("egypt: someone to talk to speaks to the Viking being played who comes up to him") {
    Stage stage{database::kEgypt, 150};
    World& world = stage.world;
    std::vector<std::uint8_t> texts(0x100 * 2, 0);
    for (std::size_t i = 0; i < 0x100; ++i) {
        texts[(2 * i) + 1] = 0x02;
    }
    texts.insert(texts.end(), {6, 3, 'H', 'I', 0});
    world.texts = texts;
    Entity* talker = stage.put(egypt_kind::kTalker, 100, 100, 0x0100);
    REQUIRE(talker != nullptr);
    stage.tick(3);
    CHECK(world.lines.empty());
    stage.move_viking(0, 110, 100);
    stage.tick(2);
    CHECK((world.hints_seen & 0x0100) != 0);
    world.lines.clear();
    world.waiting_for_key = false;
    talker->message = message::kAsked;
    talker->message_value = 0;
    stage.tick();
    talker->message = 0x05;
    stage.tick();
    CHECK(talker->message == 0);
}

TEST_CASE("factory: a pair of plates: pressing the one that is up flips the switches and sends the other up") {
    Stage stage{database::kFactory};
    World& world = stage.world;
    Entity* left =
        stage.put(factory_kind::kPlate, 104, 104, 0x0004, entity_flag::kFacingLeft);  // up while the switch is on
    Entity* right = stage.put(factory_kind::kPlate, 136, 104, 0x0004);                // up while it is off
    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);
    stage.tick(2);
    CHECK(right->box.top == 104 - 8 - 12);
    CHECK(left->box.top == 104 - 8 + 12);
    stage.move_viking(1, 104, left->box.top - 8);   // on the one that is down
    stage.move_viking(0, 136, right->box.top - 8);  // standing on the one that is up: it goes down
    bool thrown = false;
    for (int i = 0; i < 4; ++i) {
        stage.tick();
        thrown = thrown || stage.viking(1).speed_y == px(-16);  // the other comes up under him
    }
    CHECK(world.switches == 0x0004);
    CHECK(thrown);
    stage.tick(5);
    CHECK(right->box.top == 104 - 8 + 12);
    CHECK(left->box.top == 104 - 8 - 12);
    // Pressing the left one turns the switch off again.
    stage.move_viking(0, 104, left->box.top - 8);
    stage.move_viking(1, 600, 64);
    stage.tick(6);
    CHECK(world.switches == 0);
    CHECK(make_of(Home::kFactory, 0x01) == nullptr);
}

TEST_CASE("factory: a press comes down while its switches are on and goes back up; a piston slams and waits") {
    Stage stage{database::kFactory};
    World& world = stage.world;
    stage.solid(0, 2, kColumns);
    stage.solid(0, 12, kColumns);
    Entity* press = stage.put(factory_kind::kSlammingPress, 104, 56, 0x0401);
    Entity* bouncer = stage.put(factory_kind::kPress, 200, 56, 0x0401);
    REQUIRE(press != nullptr);
    REQUIRE(bouncer != nullptr);
    stage.tick(3);
    CHECK(press->y == 56);
    world.switches = 0x0001;
    stage.move_viking(0, 104, 150);
    stage.move_viking(2, 200, 150);
    int lowest = 0;
    for (int i = 0; i < 80; ++i) {
        stage.tick();
        lowest = press->y > lowest ? press->y : lowest;
    }
    CHECK(lowest == 184);  // down on the floor
    CHECK(stage.viking(0).message == message::kSquashed);
    CHECK(stage.viking(2).message == message::kSquashed);
    CHECK(world.shake_y.amount == 1);
    world.switches = 0;
    stage.tick(80);
    CHECK(press->y == 56);  // back under the ceiling, waiting

    for (const int kind : {0xa9, 0xaa}) {
        Entity* piston =
            stage.put(static_cast<std::uint16_t>(kind), kind == 0xa9 ? 300 : 360, kind == 0xa9 ? 56 : 184, 0x0205);
        REQUIRE(piston != nullptr);
        stage.tick(2);
        CHECK(piston->acts_as == tile::kGround);
        const int rest = piston->y;
        world.switches = 0x0200;
        bool moved = false;
        for (int i = 0; i < 40; ++i) {
            stage.tick();
            moved = moved || piston->y != rest;
        }
        CHECK(moved);
        piston->message = message::kSwordBlow;
        piston->message_value = 1;
        stage.tick();
        CHECK(piston->health == 3);
        piston->message = message::kBlast;
        piston->message_value = 9;
        stage.tick();
        world.switches = 0;
    }
    CHECK(!stage.has(factory_kind::kPiston));
    CHECK(!stage.has(factory_kind::kUpwardPiston));
}

TEST_CASE("factory: tiles that crumble or are shot away, a conveyor, a hatch, a light, things that take items") {
    Stage stage{database::kFactory};
    World& world = stage.world;
    stage.solid(6, 6);
    Entity* tile = stage.put(factory_kind::kCrumblingTile, 104, 104);
    Entity* shot_away = stage.put(factory_kind::kShotAwayTile, 136, 104);
    REQUIRE(tile != nullptr);
    REQUIRE(shot_away != nullptr);
    stage.tick(2);
    stage.move_viking(0, 104, 88);
    stage.tick(14);
    CHECK(!stage.has(factory_kind::kCrumblingTile));
    CHECK(world.map.tile_type_at(104, 104) == 0x00);
    stage.move_viking(0, 600, 24);
    shot_away->message = message::kSwordBlow;  // a sword does nothing
    stage.tick();
    CHECK(stage.has(factory_kind::kShotAwayTile));
    shot_away->message = message::kArrowBlow;
    stage.tick(3);
    CHECK(!stage.has(factory_kind::kShotAwayTile));

    Entity* belt = stage.put(factory_kind::kConveyor8E, 200, 104, 0, entity_flag::kFacingLeft);
    REQUIRE(belt != nullptr);
    stage.move_viking(1, 200, 88);
    stage.tick(3);
    CHECK(stage.viking(1).x < 200);  // carried to the left
    stage.move_viking(1, 600, 64);

    Entity* hatch = stage.put(factory_kind::kHatch, 300, 104, 0x0001);
    REQUIRE(hatch != nullptr);
    stage.tick(2);
    CHECK(hatch->acts_as == 1);
    world.switches = 0x0001;
    stage.tick(2);
    CHECK(hatch->acts_as == 0);
    world.switches = 0;
    stage.tick(2);
    CHECK(hatch->acts_as == 1);

    Entity* light = stage.put(factory_kind::kBlinkingLight, 400, 104);
    Entity* fixture = stage.put(factory_kind::kCraneControls, 440, 104);
    REQUIRE(light != nullptr);
    REQUIRE(fixture != nullptr);
    stage.tick(2);
    CHECK(fixture->acts_as == tile::kThing);
    light->message = message::kAsked;
    stage.tick();
    CHECK(world.map.cell(25, 5)->metatile == 3);
    stage.tick(2);
    CHECK(world.map.cell(25, 5)->metatile == 4);
    stage.tick(30);

    // Given its item, a place shows it and flips its switches; the vendor gives another item for it.
    Entity* place = stage.put(factory_kind::kItemPlace, 500, 40, 0x0810);
    Entity* vendor = stage.put(factory_kind::kVendor, 540, 40, 0x0003);
    REQUIRE(place != nullptr);
    REQUIRE(vendor != nullptr);
    stage.tick(2);
    CHECK(place->wanted_item == 0x10);
    CHECK(vendor->wanted_item == 0x0e);
    place->message = message::kAsked;
    vendor->message = message::kAsked;
    stage.tick();
    CHECK(world.switches == 0x0800);
    CHECK(world.map.cell(31, 2)->metatile == 0x10);
    CHECK(world.map.cell(33, 2)->metatile == 0x11);
    CHECK(stage.has(kind::kItem));
    // The one that takes item 0D runs a machine, which a bomb blows up.
    world.cycles.resize(2);
    Entity* machine = stage.put(factory_kind::kItemPlace, 500, 120, 0x040d);
    REQUIRE(machine != nullptr);
    stage.tick(2);
    machine->message = message::kAsked;
    stage.tick();
    CHECK(world.factory.part_fitted);
    CHECK(world.cycles.at(1).running);
    CHECK((machine->hit_mask & 0x1202) == 0x1202);
    machine->message = message::kBlast;
    machine->message_value = 2;  // not enough
    stage.tick();
    CHECK(stage.has(factory_kind::kItemPlace));
    machine->message = message::kBlast;
    machine->message_value = 3;
    stage.tick();
    CHECK(!stage.has(factory_kind::kItemPlace));
    CHECK(stage.has(factory_kind::kBlast));
    CHECK(!world.cycles.at(1).running);
    stage.tick(3);
    CHECK(!stage.has(factory_kind::kBlast));
}

TEST_CASE("factory: a dispenser, a furnace, a machine to wreck, a target, a block that drops") {
    Stage stage{database::kFactory};
    World& world = stage.world;
    world.gravity = 0x40;
    stage.solid(0, 12, kColumns);
    Entity* dispenser = stage.put(factory_kind::kDispenser, 104, 104, 0x0105);
    REQUIRE(dispenser != nullptr);
    stage.tick(3);
    CHECK(!stage.has(kind::kItem));
    world.switches = 0x0100;
    stage.tick(0x22);
    CHECK(stage.has(kind::kItem));
    CHECK(!stage.has(factory_kind::kDispenser));

    Entity* furnace = stage.put(factory_kind::kFurnace, 200, 104, 0x0100);
    REQUIRE(furnace != nullptr);
    stage.move_viking(0, 200, 104);
    stage.tick(3);
    CHECK(stage.viking(0).message == message::kScorched);
    CHECK(stage.viking(0).message_value == 0x0a);
    stage.move_viking(0, 600, 24);

    Entity* machine = stage.put(factory_kind::kMachine, 300, 104, 0x0002);
    REQUIRE(machine != nullptr);
    stage.tick(2);
    machine->message = message::kSwordBlow;
    machine->message_value = 1;
    stage.tick();
    CHECK(machine->health == 3);
    machine->message = message::kBlast;
    machine->message_value = 0x8005;
    stage.tick(2);
    CHECK(!stage.has(factory_kind::kMachine));
    CHECK((world.switches & 0x0002) != 0);
    CHECK(world.map.cell(18, 6)->metatile == 0x12);

    world.broken_picture = 0x0b;
    Entity* target = stage.put(factory_kind::kTarget, 400, 104, 0x0008);
    REQUIRE(target != nullptr);
    stage.tick(2);
    target->message = message::kArrowBlow;
    stage.tick();
    CHECK((world.switches & 0x0008) != 0);
    CHECK(world.map.cell(25, 6)->metatile == 0x0b);

    Entity* block = stage.put(factory_kind::kDroppedBlock, 500, 60, 0x0010);
    REQUIRE(block != nullptr);
    stage.tick(3);
    CHECK(block->y == 60);
    world.switches |= 0x0010;
    stage.move_viking(1, 500, 150);
    stage.tick(60);
    CHECK(stage.viking(1).message == message::kSquashed);
    CHECK(block->y == 184);
    CHECK(world.map.tile_type_at(500, 184) == 0x01);
    CHECK(stage.put(factory_kind::kNothing8F, 20, 20) != nullptr);
    stage.tick();
    CHECK(!stage.has(factory_kind::kNothing8F));
}

TEST_CASE("factory: its robots: one walks into the Vikings, one shoots, one flies and drops bombs; a turret") {
    Stage stage{database::kFactory};
    World& world = stage.world;
    world.gravity = 0x40;
    stage.solid(0, 12, kColumns);
    Entity* walker = stage.put(factory_kind::kWalker, 200, 184);
    REQUIRE(walker != nullptr);
    stage.tick(3);
    CHECK(walker->speed_x == px(3));
    stage.move_viking(0, walker->x + 14, 184);
    stage.tick(2);
    CHECK(stage.viking(0).message == message::kStruck);
    CHECK(walker->speed_x == 0);
    stage.move_viking(0, 600, 24);
    walker->message = message::kSwordBlow;
    walker->message_value = 0x8001;
    stage.tick();
    CHECK(walker->health == 3);
    stage.tick(3);
    for (const int told : {0x23, 0x22, 0x26, 0x25}) {
        walker->message = static_cast<std::uint16_t>(told);
        stage.tick(2);
    }
    walker->message = message::kLedLeave;
    stage.tick();
    CHECK(!stage.has(factory_kind::kWalker));

    Entity* shooter = stage.put(factory_kind::kShooter, 300, 184);
    REQUIRE(shooter != nullptr);
    stage.tick(3);
    CHECK(shooter->speed_x == px(3));
    shooter->message = message::kSwordBlow;  // a sword does nothing
    stage.tick();
    CHECK(shooter->health == 4);
    shooter->message = message::kArrowBlow;
    shooter->message_value = 1;
    stage.tick();
    CHECK(shooter->health == 3);
    stage.tick(4);
    stage.move_viking(1, shooter->box.right + 9, 184);  // in its way: held up
    stage.tick(2);
    CHECK(shooter->speed_x == 0);
    stage.move_viking(1, 600, 64);
    for (const int told : {0x23, 0x22, 0x26, 0x25}) {
        shooter->message = static_cast<std::uint16_t>(told);
        stage.tick(2);
    }
    shooter->message = message::kLedColours;
    stage.tick();
    CHECK(!stage.has(factory_kind::kShooter));

    Entity* turret = stage.put(factory_kind::kTurret, 400, 100);
    REQUIRE(turret != nullptr);
    stage.tick(0x2b);
    CHECK(stage.has(factory_kind::kShot));
    turret->message = message::kBlast;
    turret->message_value = 1;
    stage.tick();
    CHECK(turret->health == 3);
    turret->message = message::kBlast;
    turret->message_value = 7;
    stage.tick();
    CHECK(!stage.has(factory_kind::kTurret));
    stage.tick(60);  // its shots have left or burst

    Entity* shot = world.create({.kind = 0x8a, .x = 300, .y = 100, .flags = entity_flag::kAlwaysActive});
    REQUIRE(shot != nullptr);
    shot->hit_mask = 0;
    stage.move_viking(2, 340, 100);
    stage.tick(6);
    CHECK(stage.viking(2).message == message::kShot);
    stage.move_viking(2, 600, 104);

    Entity* flyer = stage.put(factory_kind::kFlyer, 300, 80);
    REQUIRE(flyer != nullptr);
    bool dropped = false;
    for (int i = 0; i < 40; ++i) {
        stage.tick();
        dropped = dropped || stage.has(factory_kind::kDroppedShot);
    }
    CHECK(dropped);
    flyer->message = message::kArrowBlow;
    flyer->message_value = 1;
    stage.tick();
    CHECK(flyer->health == 3);
    flyer->message = 0x05;
    stage.tick(4);
    flyer->message = message::kArrowBlow;
    flyer->message_value = 9;
    stage.tick(3);
    CHECK(!stage.has(factory_kind::kFlyer));
}

TEST_CASE("wacky: walls, cells and plugs that switches work; a target for an arrow; a burner and its flame") {
    Stage stage{database::kWacky};
    World& world = stage.world;
    Entity* wall = stage.put(wacky_kind::kWall, 104, 104, 0x0001);
    Entity* cells = stage.put(wacky_kind::kSwitchedCells, 200, 104, 0x0002);
    Entity* more_cells = stage.put(wacky_kind::kSwitchedCellsOfSecondSet, 264, 104, 0x0002);
    Entity* plug = stage.put(wacky_kind::kPlug, 328, 104, 0x0004);
    stage.solid(20, 5);
    stage.solid(20, 7);
    REQUIRE(wall != nullptr);
    REQUIRE(cells != nullptr);
    REQUIRE(more_cells != nullptr);
    REQUIRE(plug != nullptr);
    stage.tick(2);
    CHECK(wall->acts_as == tile::kGround);
    CHECK(world.map.tile_type_at(200, 104) == 0x0a);
    world.switches = 0x0007;
    stage.tick(3);
    CHECK(!stage.has(wacky_kind::kWall));
    CHECK(!stage.has(wacky_kind::kPlug));
    CHECK(world.map.tile_type_at(200, 104) == 0x01);
    CHECK(world.map.tile_type_at(264, 104) == 0x0a);  // this one reads the second set
    CHECK(world.map.tile_type_at(328, 88) == 0x00);
    CHECK(world.map.tile_type_at(328, 120) == 0x00);
    world.more_switches = 0x0002;
    world.switches = 0;
    stage.tick(2);
    CHECK(world.map.tile_type_at(200, 104) == 0x0a);
    CHECK(world.map.tile_type_at(264, 104) == 0x01);

    world.shot_picture = 6;
    Entity* target = stage.put(wacky_kind::kArrowTarget, 400, 104, 0x0010);
    REQUIRE(target != nullptr);
    stage.tick(2);
    target->message = message::kSwordBlow;  // only an arrow counts
    stage.tick();
    CHECK(stage.has(wacky_kind::kArrowTarget));
    target->message = message::kArrowBlow;
    stage.tick();
    CHECK(world.switches == 0x0010);
    CHECK(world.map.cell(25, 6)->metatile == 6);

    Entity* burner = stage.put(wacky_kind::kBurner, 104, 40, 0x0010);
    REQUIRE(burner != nullptr);
    stage.move_viking(0, 170, 40);
    stage.tick(3);
    CHECK(stage.has(wacky_kind::kFlame));
    stage.tick(10);
    CHECK(stage.viking(0).message == message::kShot);
    stage.move_viking(0, 600, 24);
    world.switches = 0;
    stage.tick(60);
    CHECK(!stage.has(wacky_kind::kFlame));
    stage.solid(30, 0, 1, 6);
    Entity* shot = world.create({.kind = 0xc8, .x = 400, .y = 40, .flags = entity_flag::kAlwaysActive});
    REQUIRE(shot != nullptr);
    shot->hit_mask = 0;
    stage.tick(12);
    CHECK(!stage.has(wacky_kind::kFlameShot));  // burst on the wall
    CHECK(make_of(Home::kWacky, 0x01) == nullptr);
}

TEST_CASE("wacky: a block is pushed, falls and lands to be pushed on; one waits for its switches; fans blow") {
    Stage stage{database::kWacky};
    World& world = stage.world;
    world.gravity = 0x40;
    stage.solid(0, 12, kColumns);
    stage.solid(14, 8, 4);
    Entity* block = stage.put(wacky_kind::kPushedBlock, 264, 120);
    REQUIRE(block != nullptr);
    stage.tick(2);
    CHECK(block->acts_as == tile::kGround);
    for (int i = 0; i < 30 && block->y == 120; ++i) {
        block->message = message::kPushed;
        block->message_value = 0x0200;
        stage.tick();
    }
    stage.move_viking(0, block->x, 170);
    stage.tick(40);
    CHECK(stage.viking(0).message == message::kSquashed);
    CHECK(block->y == 184);  // landed, still a block
    stage.move_viking(0, 600, 24);
    stage.move_viking(1, block->box.right + 9, 184);
    block->message = message::kPushed;
    block->message_value = 0x0200;
    stage.tick();
    CHECK(stage.viking(1).message == message::kCrushed);
    stage.move_viking(1, 600, 64);

    Entity* fixed = stage.put(wacky_kind::kPushedBlockWaitingForSwitch, 100, 184, 0x0001);
    REQUIRE(fixed != nullptr);
    stage.tick(2);
    fixed->message = message::kPushed;
    fixed->message_value = 0x0200;
    stage.tick(2);
    CHECK(fixed->x == 100);
    world.switches = 0x0001;
    stage.tick(2);
    fixed->message = message::kPushed;
    fixed->message_value = 0x0200;
    stage.tick(2);
    CHECK(fixed->x > 100);

    Entity* fan = stage.put(wacky_kind::kFan, 400, 100, 0x0002);
    Entity* lifting = stage.put(wacky_kind::kUpwardFan, 500, 150, 0x0002);
    REQUIRE(fan != nullptr);
    REQUIRE(lifting != nullptr);
    stage.move_viking(1, 440, 100);
    stage.move_viking(2, 500, 100);
    stage.viking(2).flags |= entity_flag::kFalls;
    stage.tick(3);
    CHECK(stage.viking(1).speed_x == 0);
    world.switches = 0x0003;
    stage.tick(3);
    CHECK(stage.viking(1).speed_x > 0);
    CHECK(stage.viking(2).speed_y < 0x40 * 3);  // held against gravity
    CHECK((fan->flags & entity_flag::kAlwaysActive) != 0);
    world.switches = 0x0001;
    stage.tick(2);
}

TEST_CASE("wacky: its enemies: one throws, one runs, strikes and shoots") {
    Stage stage{database::kWacky};
    World& world = stage.world;
    world.gravity = 0x40;
    stage.solid(0, 12, kColumns);
    Entity* thrower = stage.put(wacky_kind::kThrower, 200, 184);
    REQUIRE(thrower != nullptr);
    stage.tick(3);
    CHECK(thrower->speed_x == px(4));
    stage.move_viking(0, thrower->box.right + 9, 184);
    stage.tick(2);
    CHECK(thrower->speed_x == 0);  // held up
    stage.move_viking(0, 600, 24);
    thrower->message = message::kSwordBlow;
    thrower->message_value = 1;
    stage.tick();
    CHECK(thrower->health == 3);
    stage.tick(6);
    for (const int told : {0x23, 0x22, 0x26, 0x25}) {
        thrower->message = static_cast<std::uint16_t>(told);
        stage.tick(2);
    }
    thrower->message = message::kLedColours;
    stage.tick();
    CHECK(!stage.has(wacky_kind::kThrower));
    stage.solid(30, 0, 1, 12);
    Entity* thrown = world.create({.kind = 0x83, .x = 400, .y = 100, .flags = entity_flag::kAlwaysActive});
    REQUIRE(thrown != nullptr);
    thrown->hit_mask = 0;
    stage.move_viking(1, 430, 100);
    stage.tick(6);
    CHECK(stage.viking(1).message == message::kShot);
    stage.move_viking(1, 600, 64);
    thrown = world.create({.kind = 0x83, .x = 400, .y = 60, .flags = entity_flag::kAlwaysActive});
    REQUIRE(thrown != nullptr);
    thrown->hit_mask = 0;
    stage.tick(14);
    CHECK(!stage.has(wacky_kind::kThrown));

    Entity* clown = stage.put(wacky_kind::kClown, 200, 184);
    REQUIRE(clown != nullptr);
    stage.tick(3);
    CHECK(clown->speed_x == px(5));
    clown->countdown = 50;  // not looking for someone to shoot at for a while
    stage.move_viking(0, clown->x + 12, 184);
    stage.tick(4);
    CHECK(stage.viking(0).message == message::kStruck);
    stage.move_viking(0, 600, 24);
    stage.tick(6);
    clown->message = message::kArrowBlow;
    clown->message_value = 1;
    stage.tick();
    CHECK(clown->health == 3);
    bool shot = false;
    for (int i = 0; i < 12; ++i) {
        stage.tick();
        shot = shot || stage.has(wacky_kind::kFlameShot);
    }
    CHECK(shot);  // over the blow, he shoots
    for (const int told : {0x23, 0x22, 0x26, 0x25}) {
        clown->message = static_cast<std::uint16_t>(told);
        stage.tick(2);
    }
    clown->message = message::kLedLeave;
    stage.tick();
    CHECK(!stage.has(wacky_kind::kClown));
    // Spikes kill them.
    stage.solid(0, 6, 4);
    world.map.cell(1, 6)->tile_type = 0x14;
    REQUIRE(stage.put(wacky_kind::kClown, 24, 88) != nullptr);
    REQUIRE(stage.put(wacky_kind::kThrower, 40, 88) != nullptr);
    stage.tick(4);
    CHECK(!stage.has(wacky_kind::kClown));
}

TEST_CASE("factory: the crane's magnet pulls a crate up, carries it along its rail and lets it go") {
    Stage stage{database::kFactory, 250};  // the Vikings near it: a magnet doesn't stay active by itself
    World& world = stage.world;
    world.gravity = 0x40;
    stage.solid(0, 12, kColumns);
    stage.solid(0, 2, 1, 4);  // the rail's ends
    stage.solid(24, 2, 1, 4);
    Entity* magnet = stage.put(factory_kind::kMagnet, 200, 72, 0);
    Entity* crate = stage.put(factory_kind::kCrate, 200, 184);
    REQUIRE(magnet != nullptr);
    REQUIRE(crate != nullptr);
    crate->hit_mask = 0x0800;  // magnetic
    stage.tick(3);
    CHECK(crate->y == 184);
    CHECK(crate->acts_as == tile::kGround);
    world.factory.cranes.at(0) = 1;  // the magnet on
    bool held = false;
    for (int i = 0; i < 60 && !held; ++i) {
        stage.tick();
        held = crate->held;
    }
    REQUIRE(held);
    CHECK(world.factory.magnets_on == 1);
    CHECK(crate->box.top == magnet->box.bottom + 1);
    for (int i = 0; i < 20; ++i) {  // to the right, the crate with it
        world.factory.cranes.at(0) |= 4;
        stage.tick();
    }
    CHECK(magnet->x == 280);
    CHECK(crate->x == 280);
    stage.tick(2);
    for (int i = 0; i < 10; ++i) {  // back to the left
        world.factory.cranes.at(0) |= 2;
        stage.tick();
    }
    CHECK(magnet->x == 240);
    stage.tick(2);
    world.factory.cranes.at(0) = 0;  // off: the crate falls and lands
    stage.tick(40);
    CHECK(!crate->held);
    CHECK(crate->y == 184);
    CHECK(world.factory.magnets_on == 0);
    // With the magnet off it only runs along the rail, to its end.
    for (int i = 0; i < 80; ++i) {
        world.factory.cranes.at(0) |= 4;
        stage.tick();
    }
    CHECK(magnet->box.right == (24 * 16) - 1);
    stage.tick(2);

    // A crate is pushed like a block; one with switches is gone when they come on.
    crate->message = message::kPushed;
    crate->message_value = 0x0200;
    stage.tick();
    CHECK(crate->x > 240);
    Entity* other = stage.put(factory_kind::kCrate, 100, 184, 0x0004);
    Entity* stays = stage.put(factory_kind::kCrate, 60, 184, 0x8004);
    REQUIRE(other != nullptr);
    REQUIRE(stays != nullptr);
    stage.tick(2);
    world.switches = 0x0004;
    stage.tick(2);
    CHECK(other != nullptr);
    int crates = 0;
    for (const auto& entity : world.entities) {
        crates += entity && entity->kind == 0x94 ? 1 : 0;
    }
    CHECK(crates == 2);  // one gone; the other stays, fixed
    CHECK(stays->hit_mask == 0);
}

TEST_CASE("factory: a trolley runs with a crane's controls; a block becomes magnetic with its switch") {
    Stage stage{database::kFactory};
    World& world = stage.world;
    world.gravity = 0x40;
    stage.solid(0, 12, kColumns);
    Entity* trolley = stage.put(factory_kind::kTrolley, 300, 100, 1);
    REQUIRE(trolley != nullptr);
    stage.tick(2);
    stage.move_viking(0, 300, 84);
    for (int i = 0; i < 10; ++i) {
        world.factory.cranes.at(1) |= 2;
        stage.tick();
    }
    CHECK(trolley->x < 300);
    CHECK(stage.viking(0).x == trolley->x);  // carried
    const int stopped_at = trolley->x;
    stage.tick(3);
    CHECK(trolley->x >= stopped_at - 4);  // it stops when the key is let go
    stage.move_viking(0, 600, 24);

    Entity* block = stage.put(factory_kind::kMagneticBlockWaitingForSwitch, 500, 100, 0x0001);
    Entity* loose = stage.put(factory_kind::kMagneticBlock, 540, 100);
    REQUIRE(block != nullptr);
    REQUIRE(loose != nullptr);
    stage.tick(30);
    CHECK(block->y == 100);  // waits for its switch
    CHECK(loose->y == 184);  // this kind fell at once
    world.switches = 0x0001;
    stage.tick(30);
    CHECK(block->y == 184);
    CHECK((block->hit_mask & 0x0800) != 0);
    // Pulled up by a magnet (told so), it rises, comes down again and lands.
    loose->message = message::kDropped;
    loose->speed_y = px(-3);
    stage.tick(3);
    CHECK(loose->y < 184);
    loose->held = true;
    stage.tick(2);
    loose->held = false;
    stage.tick(40);
    CHECK(loose->y == 184);
    // In an electric cell it is dead.
    world.map.cell(33, 11)->tile_type = 0x15;
    stage.tick(3);
    loose->message = message::kDropped;
    stage.tick(3);
    CHECK(loose->y == 184);
}

TEST_CASE("tomator: his words, his volleys, the floor going from under him; his parts follow his pose") {
    Stage stage{449, 300};
    World& world = stage.world;
    world.gravity = 0x40;
    std::vector<std::uint8_t> texts(0x120 * 2, 0);
    for (std::size_t i = 0; i < 0x120; ++i) {
        texts[2 * i] = 0x40;
        texts[(2 * i) + 1] = 0x02;
    }
    texts.insert(texts.end(), {6, 3, 'H', 'I', 0});
    world.texts = texts;
    stage.solid(0, 12, kColumns);
    Entity* tomator = stage.put(0xc9, 200, 184);
    Entity* part = stage.put(0xca, 200, 184);
    REQUIRE(tomator != nullptr);
    REQUIRE(part != nullptr);
    REQUIRE(tomator->slot == 3);
    stage.tick(2);
    CHECK(!world.lines.empty());  // he speaks first
    world.lines.clear();
    world.waiting_for_key = false;
    world.next_line = 0;
    // He vanishes and comes back where the fight starts; then he shoots, again and again.
    stage.solid(34, 9, 6);
    bool shot = false;
    for (int i = 0; i < 60; ++i) {
        stage.tick();
        shot = shot || stage.has(0xce);
    }
    CHECK(tomator->stage == 2);
    CHECK(tomator->x == 0x268);
    CHECK(part->x == 0x268);  // attached
    CHECK(shot);
    // A blow stuns him; Erik's head knocks him back.
    tomator->message = message::kArrowBlow;
    stage.tick(2);
    CHECK(tomator->pose == 4);
    CHECK(part->pose == 4);
    tomator->message = message::kHeadButt;
    stage.tick(2);
    CHECK(tomator->pose == 3);
    stage.tick(80);
    // The floor goes from under him: he falls to the next one, three times, and then moves on.
    for (int fall = 0; fall < 3; ++fall) {
        for (int column = 34; column < 40; ++column) {
            world.map.cell(column, 9)->tile_type = 0;
        }
        stage.tick(30);
        stage.solid(34, 9, 6);
        tomator->y = 100;
        tomator->box.top = 92;
        tomator->box.bottom = 107;
        stage.tick(4);
    }
    CHECK(world.values.at(0) >= 2);
    // The air lock ends it.
    tomator->message = message::kVanish;
    stage.tick(2);
    CHECK(tomator->pose == 1);
    stage.tick(0x40);
    CHECK(world.level_done);
    CHECK(make_tomator(0x01) == nullptr);
}

TEST_CASE("tomator: his force field, his shots and the air lock") {
    Stage stage{database::kSpaceship};
    World& world = stage.world;
    world.gravity = 0x40;
    stage.solid(0, 12, kColumns);
    Entity* boss = stage.put(0xea, 300, 100);  // a stand-in for Tomator
    REQUIRE(boss != nullptr);
    Entity* field =
        world.create({.kind = 0xcd, .x = 340, .y = 100, .flags = entity_flag::kAlwaysActive, .creator = boss->slot});
    REQUIRE(field != nullptr);
    stage.move_viking(0, 340, 100);
    stage.tick(3);
    CHECK(world.tomator.force_field == field->slot);
    CHECK(stage.viking(0).message == message::kElectrocuted);
    stage.move_viking(0, 600, 24);
    field->message = message::kArrowBlow;
    stage.tick(3);
    CHECK(world.tomator.force_field == kNobody);
    CHECK(!stage.has(0xcd));

    // Straight, in an arc, and along the floor, by the stage of the fight and the shot's number.
    for (const auto& [boss_stage, number] :
         {std::pair{2, 0}, std::pair{2, 3}, std::pair{2, 0x0b}, std::pair{3, 4}, std::pair{4, 1}, std::pair{0, 2},
          std::pair{0, 5}, std::pair{5, 1}, std::pair{5, 3}}) {
        boss->stage = static_cast<std::uint16_t>(boss_stage);
        world.values.at(0) = 1;
        Entity* shot = world.create({.kind = 0xce,
                                     .x = 300,
                                     .y = 150,
                                     .flags = entity_flag::kAlwaysActive,
                                     .argument = static_cast<std::uint16_t>(number),
                                     .creator = boss->slot});
        REQUIRE(shot != nullptr);
        shot->hit_mask = 0;
        stage.tick();
        stage.tick(150);
        CHECK(!stage.has(0xce));
    }
    boss->stage = 2;
    Entity* shot =
        world.create({.kind = 0xce, .x = 300, .y = 150, .flags = entity_flag::kAlwaysActive, .creator = boss->slot});
    REQUIRE(shot != nullptr);
    shot->hit_mask = 0;
    stage.move_viking(1, 330, 150);
    stage.tick(8);
    CHECK(stage.viking(1).message == message::kShot);
    stage.move_viking(1, 600, 64);

    Entity* lock = stage.put(0xcf, 500, 60, 0x0080);
    REQUIRE(lock != nullptr);
    Entity* under = stage.put(0xea, 500, 100);
    REQUIRE(under != nullptr);
    under->hit_class = 2;
    stage.tick(3);
    CHECK(under->message == 0);
    world.switches = 0x0080;
    stage.tick(5);
    CHECK(under->message == message::kVanish);
}

namespace {

void tick_with(World& world, std::uint16_t keys) {
    for (int part = 0; part < 3; ++part) {
        world.step(part, keys);
    }
}

std::vector<std::uint8_t> short_texts() {
    std::vector<std::uint8_t> texts(0x120 * 2, 0);
    for (std::size_t i = 0; i < 0x120; ++i) {
        texts[2 * i] = 0x40;
        texts[(2 * i) + 1] = 0x02;
    }
    texts.insert(texts.end(), {6, 3, 'H', 'I', 0});
    return texts;
}

}  // namespace

TEST_CASE("game over: a key brings the dead Vikings back one by one, and the level starts again") {
    Stage stage{database::kSpaceship};
    World& world = stage.world;
    world.has_vikings = false;  // a cutscene: the keys are the scene's
    const std::vector<std::uint8_t> texts = short_texts();
    world.texts = texts;
    world.game_over_scene.kept_health = {0, 6, 0};  // Erik and Olaf are dead
    Entity* director = stage.put(0xd2, 100, 100);
    Entity* erik = stage.put(0xd4, 120, 100);
    Entity* baleog = stage.put(0xd3, 140, 100);
    Entity* ship = stage.put(0xd6, 200, 100, 1);
    REQUIRE(director != nullptr);
    REQUIRE(erik != nullptr);
    REQUIRE(baleog != nullptr);
    REQUIRE(ship != nullptr);
    stage.tick(3);
    CHECK(world.pieces.at(static_cast<std::size_t>(erik->sprite.first)).hidden);
    CHECK(!world.pieces.at(static_cast<std::size_t>(baleog->sprite.first)).hidden);
    CHECK(ship->x > 200);  // it drifts
    // S shows the password; a key puts it away again.
    tick_with(world, 0x0080);
    stage.tick(2);
    CHECK(!world.lines.empty());
    world.lines.clear();
    world.next_line = 0;
    tick_with(world, 0x4000);
    stage.tick(2);
    world.lines.clear();
    world.next_line = 0;
    // F: the lightning, on Erik, then on Olaf.
    tick_with(world, 0x8000);
    stage.tick(4);
    CHECK(world.game_over_scene.kept_health.at(0) == 6);
    CHECK(!world.pieces.at(static_cast<std::size_t>(erik->sprite.first)).hidden);
    CHECK(!world.game_over_scene.try_again);
    stage.tick(0x28);
    CHECK(world.game_over_scene.kept_health.at(2) == 6);
    stage.tick(0x28);
    CHECK(world.game_over_scene.try_again);
    CHECK(make_game_over(0x01) == nullptr);
    // With everyone alive there is no burning ship.
    Stage alive{database::kSpaceship};
    alive.world.game_over_scene.kept_health = {6, 6, 6};
    REQUIRE(alive.put(0xd6, 200, 100) != nullptr);
    alive.tick(2);
    CHECK(!alive.has(0xd6));
}

TEST_CASE("game over: left alone it asks; No lets GAME OVER drop in and gives up") {
    Stage stage{database::kSpaceship};
    World& world = stage.world;
    world.has_vikings = false;
    world.gravity = 0x40;
    const std::vector<std::uint8_t> texts = short_texts();
    world.texts = texts;
    Entity* director = stage.put(0xd2, 100, 100);
    Entity* letters = stage.put(0xd7, 200, 20, 2);
    REQUIRE(director != nullptr);
    REQUIRE(letters != nullptr);
    stage.tick(0xcc);
    CHECK(!world.lines.empty());  // TRY AGAIN, with YES and NO
    world.lines.clear();
    world.next_line = 0;
    stage.tick(10);            // the chosen one blinks
    tick_with(world, 0x0100);  // right: NO
    tick_with(world, 0x0080);  // S: the password, over the question
    stage.tick(2);
    tick_with(world, 0x0040);
    stage.tick(3);
    CHECK(letters->y == 20);
    tick_with(world, 0x1000);  // and Enter
    stage.tick(2);
    CHECK(world.game_over_scene.letters_fall);
    CHECK(!world.game_over_scene.give_up);
    stage.tick(110);
    CHECK(letters->y >= 0x50);  // dropped in, bounced and at rest
    CHECK((letters->flags & entity_flag::kFalls) == 0);
    stage.tick(0xc8);
    CHECK(world.game_over_scene.give_up);
    CHECK(stage.put(0xd7, 240, 20, 9) != nullptr);  // not one of the four parts: gone at once
    stage.tick(2);
    // Typing Y answers the question at once.
    Stage other{database::kSpaceship};
    other.world.has_vikings = false;
    other.world.texts = texts;
    other.world.game_over_scene.kept_health = {6, 6, 6};
    REQUIRE(other.put(0xd2, 100, 100) != nullptr);
    other.tick(0xcc);
    other.world.typed = 'Y';
    other.tick(4);
    CHECK(other.world.game_over_scene.try_again);
}

TEST_CASE("play: without the game's files a level can't go on to the scene after it") {
    LoadedLevel loaded;
    loaded.level.map = Map{.width = 20, .height = 11, .cells = std::vector<Cell>(220)};
    Play play{loaded};
    int pictures = 0;
    CHECK(play.step(0, [&pictures] { ++pictures; }) == 3);
    CHECK(pictures == 3);
    play.world.game_over = true;
    pictures = 0;
    CHECK(play.step(0, [&pictures] { ++pictures; }) == 70);  // the fade out; then nothing to go on to
    CHECK(pictures == 70);
    CHECK(play.ended);
    CHECK(!play.error.empty());
    CHECK(!play.fade.has_value());
}

// The ones to talk to, in every state of their hints and switches and for each Viking: what they say, where and
// in which order, as one number. No replay reaches most of these conversations; the number was taken when they
// moved into the dialogue table (2026-11-24), from the code before and after, which gave the same lines.
TEST_CASE("the ones to talk to say what they said before the dialogue table") {
    struct Case {
        int database;
        std::uint16_t kind;
        std::uint16_t argument;
    };
    const std::array<Case, 9> cases{{{database::kSpaceship, kind::kJanitor, 0x0100},
                                     {database::kSpaceship, kind::kMechanic, 0x0100},
                                     {database::kPrehistoric, kind::kElder, 0x0100},
                                     {database::kPrehistoric, kind::kCaveman, 0x0100},
                                     {database::kSpaceship, kind::kPrisoner, 0x0100},
                                     {database::kSpaceship, kind::kRemark, 0x0002},
                                     {database::kSpaceship, kind::kShipStop, 0},
                                     {database::kSpaceship, kind::kShipStop, 1},
                                     {database::kEgypt, egypt_kind::kTalker, 0x0100}}};
    std::string out;
    for (const Case& one : cases) {
        for (const std::uint16_t seen : {std::uint16_t{0}, std::uint16_t{0xffff}}) {
            for (const std::uint16_t switches : {std::uint16_t{0}, std::uint16_t{0xffff}}) {
                for (int current = 0; current < 3; ++current) {
                    Stage stage{one.database, 110};
                    World& world = stage.world;
                    std::vector<std::uint8_t> texts(0x100 * 2, 0);
                    for (std::size_t i = 0; i < 0x100; ++i) {  // each text its own, so a line says which it is
                        texts[2 * i] = static_cast<std::uint8_t>(texts.size() & 0xffU);
                        texts[(2 * i) + 1] = static_cast<std::uint8_t>(texts.size() >> 8U);
                        texts.insert(texts.end(), {6, 3, 'H', 'I', 0});
                    }
                    world.texts = texts;
                    world.hints_seen = seen;
                    world.switches = switches;
                    world.host().viking = current;
                    for (int v = 0; v < 3; ++v) {
                        stage.move_viking(v, 104 + (4 * v), 100 + (2 * v));
                    }
                    Entity* talker = stage.put(one.kind, 100, 100, one.argument);
                    out += "case " + std::to_string(one.database) + " " + std::to_string(one.kind) + " " +
                           std::to_string(one.argument) + " " + std::to_string(seen) + " " + std::to_string(switches) +
                           " " + std::to_string(current) + (talker == nullptr ? " none\n" : "\n");
                    for (int tick = 0; tick < 14; ++tick) {
                        if ((tick == 5 || tick == 9) && world.at(talker == nullptr ? 19 : talker->slot) == talker &&
                            talker != nullptr) {
                            talker->message = message::kAsked;
                            talker->message_value = static_cast<std::uint16_t>(current);
                        }
                        stage.tick();
                        for (const Line& line : world.lines) {
                            out += " " + std::to_string(tick) + ":" + std::to_string(static_cast<int>(line.kind)) +
                                   "," + std::to_string(line.column) + "," + std::to_string(line.row) + "," +
                                   std::to_string(line.anchor) + "," + std::to_string(line.text) + "," +
                                   std::to_string(line.value) + "\n";
                        }
                        out += " t" + std::to_string(tick) + " hints=" + std::to_string(world.hints_seen) +
                               " sw=" + std::to_string(world.switches) +
                               " m0=" + std::to_string(stage.viking(0).message) +
                               " n=" + std::to_string(world.lines.size()) + "\n";
                        world.lines.clear();
                        world.next_line = 0;
                        world.waiting_for_key = false;
                        talker = nullptr;
                        for (const auto& entity : world.entities) {
                            if (entity && entity->kind == one.kind) {
                                talker = entity.get();
                            }
                        }
                    }
                }
            }
        }
    }
    std::uint32_t hash = 2166136261U;  // FNV-1a
    for (const char c : out) {
        hash = (hash ^ static_cast<std::uint8_t>(c)) * 16777619U;
    }
    CHECK(out.size() == 91741u);
    CHECK(hash == 3594340790u);
}
