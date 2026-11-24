// Tests for the kinds written so far besides the level's start: the Vikings' start, the hint box, the electric
// barrier and the items (src/lvr/vikings.cpp, conversations.cpp, switches.cpp, switched.cpp, items.cpp).
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <typeinfo>
#include <vector>

#include "lvr/entity.hpp"
#include "lvr/kinds.hpp"
#include "lvr/world.hpp"

using namespace lvr;

namespace {

class Still : public Entity {
  public:
    void update(World& /*world*/) override {}
};

// Animation programs that end at once, wherever a kind starts one: 64 KiB of "the end".
struct Stage {
    std::vector<std::byte> programs = std::vector<std::byte>(0x10000, std::byte{0x1A});
    std::vector<std::uint8_t> texts;
    World world;

    Stage() {
        // One text, at every number up to 0x60: a 6 x 3 box saying "HI".
        texts.assign(0x60 * 2, 0);
        for (std::size_t i = 0; i < 0x60; ++i) {
            texts[2 * i] = 0xc0;
        }
        texts.insert(texts.end(), {6, 3, 'H', 'I', 0});
        world.texts = texts;
        world.programs = programs;
        world.camera.max_x = world.camera.max_y = 2000;
        world.map =
            Map{.width = 20, .height = 10, .cells = std::vector<Cell>(200, Cell{.metatile = 0, .tile_type = 0x0c})};
        world.make = [](std::uint16_t kind) -> std::unique_ptr<Entity> {
            if (auto made = make_kind(kind)) {
                return made;
            }
            return make_entity<Still>();
        };
        for (std::uint16_t k : {kind::kErik, kind::kBaleog, kind::kOlaf, kind::kItem, kind::kHint, kind::kBarrier}) {
            world.kinds.at(k).pieces = 1;
            world.kinds.at(k).width = world.kinds.at(k).height = 16;
            world.kinds.at(k).sprite_entry = KindRecord::kPackedSprites;
        }
        world.kinds.at(kind::kErik).hit_class = 1;
        world.kinds.at(kind::kErik).hit_mask = 0x0400;
    }
    void tick() {
        for (int part = 0; part < 3; ++part) {
            world.step(part);
        }
    }
};

}  // namespace

TEST_CASE("barrier: on with its switches; it electrocutes; switched off it frees its cells and goes") {
    Stage stage;
    World& world = stage.world;
    world.switches = 0x0004;
    world.placements = {
        Placement{.x = 112, .y = 72, .width = 64, .height = 8, .kind = kind::kBarrier, .argument = 0x0004}};
    Entity* erik = world.create({.kind = kind::kErik, .x = 300, .y = 72});
    world.create({.kind = kind::kBaleog, .x = 0, .y = 0});
    world.create({.kind = kind::kOlaf, .x = 0, .y = 0});
    Entity* barrier = world.create_placed(0);
    REQUIRE(barrier != nullptr);
    stage.tick();
    CHECK(barrier->acts_as == tile::kBarrier);
    stage.tick();
    CHECK(erik->message == 0);
    // Erik walks into it: told in the tick after they touch.
    erik->box = barrier->box;
    stage.tick();
    stage.tick();
    CHECK(erik->message == message::kElectrocuted);
    CHECK(barrier->target == erik->slot);
    // Its switch goes off: its cells (two each side of its middle) are free, and it doesn't come back.
    world.switches = 0;
    stage.tick();
    CHECK(world.map.at(5, 4).tile_type == tile::kFree);
    CHECK(world.map.at(8, 4).tile_type == tile::kFree);
    CHECK(world.map.at(4, 4).tile_type == 0x0c);
    CHECK(world.map.at(9, 4).tile_type == 0x0c);
    CHECK(world.gone.test(0));
    stage.tick();  // the fade has played
    CHECK_FALSE(world.entities[3]);

    // One whose switch is off from the start never appears.
    world.gone.reset(0);
    world.map.cells.assign(200, Cell{.metatile = 0, .tile_type = 0x0c});
    REQUIRE(world.create_placed(0) != nullptr);
    stage.tick();
    CHECK_FALSE(world.entities[3]);
    CHECK(world.map.at(6, 4).tile_type == tile::kFree);
}

TEST_CASE("hint: it greets the Viking being played once; later it answers when asked") {
    Stage stage;
    World& world = stage.world;
    Entity* erik = world.create({.kind = kind::kErik, .x = 160, .y = 100});
    world.create({.kind = kind::kBaleog, .x = 0, .y = 0});
    world.create({.kind = kind::kOlaf, .x = 0, .y = 0});
    world.shown_viking = 0;
    Entity* hint = world.create({.kind = kind::kHint, .x = 230, .y = 100, .argument = 0x0105});
    stage.tick();
    CHECK(hint->acts_as == tile::kThing);
    CHECK(hint->argument == 5);
    stage.tick();
    CHECK(world.lines.empty());
    // Erik touches it, in the middle of the view with the camera at rest.
    hint->x = 170;
    hint->box = erik->box;
    world.cues.clear();
    stage.tick();
    stage.tick();
    REQUIRE(world.lines.size() == 4);
    CHECK(world.lines[1].kind == Line::Kind::kBox);
    CHECK(world.lines[1].anchor == 6);
    CHECK(world.cues.size() == 1);
    CHECK(world.hints_seen == 0x0100);
    world.lines.clear();
    stage.tick();
    stage.tick();
    CHECK(world.lines.empty());  // only once
    // Asked by a Viking; anything else it is told is forgotten.
    hint->message = message::kAsked;
    hint->message_value = 0;
    stage.tick();
    CHECK(world.lines.size() == 4);
    CHECK(hint->target == 0);
    world.lines.clear();
    hint->message = message::kConversation;
    stage.tick();
    CHECK(world.lines.empty());
    CHECK(hint->message == 0);

    // One already seen in this level doesn't greet; nor does the kind that only answers; nor one touched while the
    // Viking can't be spoken to.
    Entity* seen = world.create({.kind = kind::kHint, .x = 160, .y = 100, .argument = 0x0105});
    Entity* quiet = world.create({.kind = kind::kHintOnAsking, .x = 160, .y = 100, .argument = 7});
    seen->box = quiet->box = erik->box;
    stage.tick();
    stage.tick();
    stage.tick();
    CHECK(world.lines.empty());
    world.hints_seen = 0;
    Entity* far = world.create({.kind = kind::kHint, .x = 160, .y = 100, .argument = 0x0203});
    far->box = erik->box;
    world.camera.moved_x = 9;  // the camera is still moving
    world.camera.locked_x = world.camera.locked_y = true;
    stage.tick();
    world.camera.moved_x = 9;
    stage.tick();
    CHECK(world.lines.empty());
}

TEST_CASE("world: who can be spoken to") {
    Stage stage;
    World& world = stage.world;
    Entity* erik = world.create({.kind = kind::kErik, .x = 160, .y = 100});
    Entity* baleog = world.create({.kind = kind::kBaleog, .x = 160, .y = 100});
    CHECK(world.can_be_spoken_to(*erik));
    CHECK_FALSE(world.can_be_spoken_to(*baleog));  // not the one being played
    erik->x = 20;                                  // too near the view's edge
    CHECK_FALSE(world.can_be_spoken_to(*erik));
    erik->x = 160;
    world.camera.moved_y = 4;
    CHECK_FALSE(world.can_be_spoken_to(*erik));
}

TEST_CASE("kinds: every kind that is written copies as its own class") {
    // A snapshot of a world copies its entities (Entity::clone): each must come back as what it was.
    int written = 0;
    for (int database = database::kSpaceship; database <= 454; ++database) {
        for (std::uint16_t kind = 0; kind < kKinds; ++kind) {
            const auto made = make_kind(kind, database);
            if (!made) {
                continue;
            }
            ++written;
            made->x = 123;
            const auto copy = made->clone();
            REQUIRE(copy);
            const Entity& original = *made;
            const Entity& copied = *copy;
            CHECK(typeid(original) == typeid(copied));
            CHECK(copy->x == 123);
        }
    }
    CHECK(written > 400);
}
