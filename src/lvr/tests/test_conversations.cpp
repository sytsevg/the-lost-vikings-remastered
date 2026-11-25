// Tests for the conversation player (World::show_lines, Play::step) and the kinds in src/lvr/conversations.cpp.
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "lvr/dialogue.hpp"
#include "lvr/entity.hpp"
#include "lvr/kinds.hpp"
#include "lvr/play.hpp"
#include "lvr/world.hpp"

using namespace lvr;

namespace {

// Texts as VIKINGS.EXE has them: 0x110 of them, each a 6 x 3 box saying "T" and its number's last digit.
std::vector<std::uint8_t> sample_texts() {
    constexpr std::size_t kCount = 0x110;
    std::vector<std::uint8_t> bytes(kCount * 2);
    for (std::size_t i = 0; i < kCount; ++i) {
        bytes[2 * i] = static_cast<std::uint8_t>(bytes.size() & 0xffU);
        bytes[(2 * i) + 1] = static_cast<std::uint8_t>(bytes.size() >> 8U);
        bytes.insert(bytes.end(), {6, 3, 'T', static_cast<std::uint8_t>('0' + (i % 10)), 0});
    }
    return bytes;
}

class Still : public Entity {
  public:
    void update(World& /*world*/) override {}
};

// A world with the three Vikings standing in a row, and the level's start as its fourth entity.
struct Stage {
    std::vector<std::uint8_t> texts = sample_texts();
    World world;

    explicit Stage(int level) {
        world.texts = texts;
        world.level = level;
        world.password = {'T', 'L', 'P', 'T'};
        world.host().camera.max_x = world.host().camera.max_y = 1000;
        world.make = [](std::uint16_t kind) -> std::unique_ptr<Entity> {
            if (auto made = make_kind(kind)) {
                return made;
            }
            return make_entity<Still>();
        };
        for (int viking = 0; viking < kVikingSlots; ++viking) {
            Entity* entity =
                world.create({.kind = static_cast<std::uint16_t>(viking), .x = 100 + (40 * viking), .y = 120});
            entity->health = 2;
        }
        world.placements = {Placement{.kind = kind::kLevelStart, .flags = 0x0828}};
        world.create_always_active();
    }
    void tick() {
        for (int part = 0; part < 3; ++part) {
            world.step(part);
        }
    }
    std::vector<Line::Kind> kinds() const {
        std::vector<Line::Kind> kinds;
        for (const Line& line : world.lines) {
            kinds.push_back(line.kind);
        }
        return kinds;
    }
};

using Kind = Line::Kind;

}  // namespace

TEST_CASE("conversation: lines are shown up to a wait; an action key goes on; the end empties the list") {
    Stage stage{1};
    World& world = stage.world;
    world.remove(3);
    world.say({.kind = Kind::kColour, .value = 0x12});
    world.say_at_cell(5, tail::kBelowMiddle, 10, 5);
    world.say({.kind = Kind::kWaitForKey});
    world.say({.kind = Kind::kClear});
    world.say_at_cell(999, tail::kBelowMiddle, 0, 0);  // no such text: nothing said
    CHECK(world.lines.size() == 4);

    CHECK(world.show_lines({}));
    CHECK(world.waiting_for_key);
    CHECK(world.text.cells[(6 * kTextLayerColumns) + 11] == 'T');
    CHECK(world.text.colour3.has_value());
    CHECK(world.next_line == 3);
    CHECK_FALSE(world.show_lines({}));  // the rest: the clear
    CHECK(world.text.cells[(6 * kTextLayerColumns) + 11] == 0);
    CHECK(world.lines.empty());
    CHECK(world.next_line == 0);
}

TEST_CASE("play: a tick is three pictures; a conversation holds the game until an action key") {
    // Driven by hand here, as Play::step drives a real level.
    Stage stage{2};
    World& world = stage.world;
    stage.tick();  // the level's start has its turn: the password box
    CHECK(stage.kinds() == std::vector<Kind>{Kind::kColour, Kind::kBox, Kind::kCharacter, Kind::kCharacter,
                                             Kind::kCharacter, Kind::kCharacter, Kind::kWaitForKey, Kind::kClear});
    CHECK(world.lines[2] == Line{.kind = Kind::kCharacter, .column = 0x12, .row = 0x0c, .value = 'T'});
    CHECK(world.lines[5] == Line{.kind = Kind::kCharacter, .column = 0x15, .row = 0x0c, .value = 'T'});
    CHECK(world.lines[1].column == 0x0f);
    CHECK(world.lines[1].row == 0x0a);
    CHECK(world.show_lines({}));
    CHECK(world.text.cells[(0x0c * kTextLayerColumns) + 0x13] == 'L');
    // The Vikings start the level with three points of health each.
    CHECK(world.entities[0]->health == 6);
    CHECK(world.entities[2]->health == 6);
}

TEST_CASE("level start: level 1 has no password box; Erik's two opening lines, then it is gone for good") {
    Stage stage{1};
    World& world = stage.world;
    stage.tick();
    CHECK(world.lines.empty());
    CHECK(world.entities[0]->message == 0);
    stage.tick();  // the Vikings are told
    CHECK(world.entities[0]->message == message::kConversation);
    CHECK(world.entities[2]->message == message::kConversation);
    CHECK(world.lines.empty());
    world.cues.clear();
    stage.tick();
    REQUIRE(world.cues.size() == 1);
    CHECK(world.cues[0].value == 0x2f);
    CHECK(stage.kinds() == std::vector<Kind>{Kind::kColour, Kind::kBox, Kind::kWaitForKey, Kind::kClear, Kind::kBox,
                                             Kind::kWaitForKey, Kind::kClear});
    CHECK(world.lines[0].value == 0x0012);  // Erik's colour
    CHECK(world.lines[1].anchor == 1);
    CHECK(world.lines[1].column == (100 / 8) - 1);                                // the tail at Erik
    CHECK(world.lines[1].row == ((120 - 0x1c - world.host().camera.y) / 8) - 2);  // above his head, as the view has it
    CHECK_FALSE(world.entities[3]);
    CHECK(world.gone.test(0));
}

TEST_CASE("level start: other levels only show the password; after a game over, or in a demo, not even that") {
    Stage plain{7};
    plain.tick();
    CHECK(plain.world.lines.size() == 8);
    plain.world.lines.clear();
    plain.tick();
    CHECK(plain.world.lines.empty());
    CHECK_FALSE(plain.world.entities[3]);

    Stage again{7};
    again.world.after_game_over = true;
    again.world.retries = 2;
    again.tick();
    CHECK(again.world.lines.empty());
    CHECK(again.world.retries == 3);
    again.tick();
    CHECK_FALSE(again.world.entities[3]);  // what they say after losing again isn't written yet

    Stage first_again{1};
    first_again.world.after_game_over = true;
    first_again.tick();
    CHECK_FALSE(first_again.world.entities[3]);

    Stage demo{7};
    demo.world.attract = true;
    demo.tick();
    CHECK(demo.world.lines.empty());
    CHECK_FALSE(demo.world.entities[3]);
}

TEST_CASE("level start: level 34 waits for the Vikings to come in; level 36 talks in three parts") {
    Stage arriving{34};
    World& world = arriving.world;
    arriving.tick();
    CHECK(world.vikings_arriving == 7);
    world.lines.clear();
    arriving.tick();
    arriving.tick();
    CHECK(world.entities[0]->message == 0);  // still waiting
    world.vikings_arriving = 0;
    arriving.tick();
    CHECK(world.entities[0]->message == message::kConversation);
    arriving.tick();
    // Baleog (below him, in his colour), Erik, Olaf.
    REQUIRE(world.lines.size() == 12);
    CHECK(world.lines[0].value == 0x01e3);
    CHECK(world.lines[1].anchor == 4);
    CHECK(world.lines[1].row == (120 + 0x14 - world.host().camera.y) / 8);
    CHECK(world.lines[4].value == 0x0012);
    CHECK(world.lines[8].value == 0x110d);
    CHECK_FALSE(world.entities[3]);

    Stage three{36};
    three.tick();
    three.world.lines.clear();
    three.tick();
    three.tick();
    CHECK(three.world.lines.size() == 1 + (3 * 3));
    CHECK(three.world.lines[0].value == 0x7c00);
    three.world.lines.clear();
    three.tick();
    CHECK(three.world.lines.size() == 4 + 4 + 3);
    CHECK(three.world.entities[3]);
    three.world.lines.clear();
    three.tick();
    CHECK(three.world.lines.size() == 8);
    CHECK_FALSE(three.world.entities[3]);
}

TEST_CASE("level start: gone when it leaves the active area; a copy is its own") {
    auto start = make_level_start();
    start->placement = -1;
    auto copy = start->clone();
    CHECK(copy.get() != start.get());
    Stage stage{1};
    Entity* entity = stage.world.entities[3].get();
    REQUIRE(entity != nullptr);
    entity->outside_active_area(stage.world);
    CHECK_FALSE(stage.world.entities[3]);
    CHECK(make_kind(0xea) == nullptr);  // a kind whose behaviour isn't written yet
}

TEST_CASE("dialogues: every row names a text the game has and a place for its box's tail") {
    constexpr int kTexts = 390;  // in VIKINGS.EXE (docs/original/dialogue.md)
    int rows = 0;
    const auto check = [&rows](const talk::Said& said) {
        ++rows;
        CHECK(said.text > 0);
        // A row for one of three texts, by Viking, needs the two after it too.
        CHECK(said.text + (said.who == talk::Who::kVikingBesideHisOwn ? 2 : 0) < kTexts);
        CHECK(said.anchor >= 0);
        CHECK(said.anchor <= tail::kNone);
        CHECK(said.below >= -40);
        CHECK(said.below <= 40);
    };
    const auto by_level = [&check](std::span<const talk::LevelSaid> talk) {
        for (const talk::LevelSaid& one : talk) {
            CHECK(one.level >= level::kFirst);
            CHECK(one.level <= level::kLast);
            check(one.said);
        }
    };
    by_level(talk::kOpenings);
    by_level(talk::kWorldEndings);
    for (const talk::LevelSaid& one : talk::kEndings) {
        CHECK(one.level >= 0);  // a scene
        CHECK(one.level <= scene::kLastLevel);
        check(one.said);
    }
    for (const auto& talk : talk::kRetries) {
        for (const talk::Said& said : talk) {
            if (said.text != 0) {  // 0 ends a talk shorter than the longest
                check(said);
            }
        }
    }
    const auto all = [&check](const auto& talk) {
        for (const talk::Said& said : talk) {
            check(said);
        }
    };
    all(talk::kWayHomeFirst);
    all(talk::kWayHomeSecond);
    all(talk::kWayHomeThird);
    all(talk::kShipFirstStop);
    all(talk::kShipSecondStop);
    all(talk::kShipSecondStopAfter);
    all(talk::kJanitor);
    all(talk::kJanitorAgain);
    all(talk::kElder);
    all(talk::kElderAgain);
    all(talk::kPrisoner);
    all(talk::kCaveman);
    all(talk::kCavemanAgain);
    all(talk::kEgyptian);
    CHECK(rows > 250);

    // The levels that have an opening are the first of each world, and the endings at an exit the last.
    for (const talk::LevelSaid& one : talk::kOpenings) {
        CHECK((one.level == level::kFirst || level::begins_a_later_world(one.level)));
    }
    for (const talk::LevelSaid& one : talk::kWorldEndings) {
        CHECK(level::ends_a_world(one.level));
    }
}
