// Tests for src/lvr/world.*: creating and removing entities and the active area, by hand and against the reference
// engine's on random sequences.
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "assets/objects.hpp"
#include "lvr/entity.hpp"
#include "lvr/world.hpp"

using namespace lvr;

namespace {

// What a kind's record holds, as bytes of an object database.
struct Record {
    std::uint16_t sprite = 0xffff;
    std::uint8_t pieces = 0;
    std::uint16_t health = 0;
    std::uint8_t width = 16;
    std::uint8_t height = 16;
    std::array<std::uint16_t, 5> words{};  // the kind's value, hit class, hit mask, the most it moves across, down
};

assets::ObjectDatabase make_database(const std::vector<Record>& records) {
    constexpr std::size_t kScript = assets::kObjectTypeCount * assets::kObjectRecordSize;
    assets::ObjectDatabase database;
    database.records.resize(assets::kObjectTypeCount);
    database.bytes.assign(kScript + 8, std::byte{0});
    const auto put = [&database](std::size_t at, std::uint16_t value) {
        database.bytes[at] = static_cast<std::byte>(value & 0xffU);
        database.bytes[at + 1] = static_cast<std::byte>(value >> 8U);
    };
    for (std::size_t kind = 0; kind < records.size(); ++kind) {
        const std::size_t at = kind * assets::kObjectRecordSize;
        const Record& record = records[kind];
        put(at, record.sprite);
        database.bytes[at + 2] = static_cast<std::byte>(record.pieces);
        put(at + 3, static_cast<std::uint16_t>(kScript));
        put(at + 7, record.health);
        database.bytes[at + 9] = static_cast<std::byte>(record.width);
        database.bytes[at + 10] = static_cast<std::byte>(record.height);
        for (std::size_t i = 0; i < record.words.size(); ++i) {
            put(at + 0x0B + (2 * i), record.words.at(i));
        }
    }
    for (std::size_t kind = 0; kind < assets::kObjectTypeCount; ++kind) {
        for (std::size_t b = 0; b < assets::kObjectRecordSize; ++b) {
            database.records[kind].bytes.at(b) =
                std::to_integer<std::uint8_t>(database.bytes[(kind * assets::kObjectRecordSize) + b]);
        }
    }
    return database;
}

std::vector<Record> sample_records() {
    return {
        {.sprite = 0xfffe, .pieces = 1, .health = 6, .width = 16, .height = 28, .words = {1, 2, 3, 0x800, 0x1000}},
        {.sprite = 286, .pieces = 4, .width = 16, .height = 48, .words = {0, 7, 0x10, 0, 0x400}},
        {.sprite = 303, .pieces = 1, .width = 16, .height = 16, .words = {9, 0, 0, 0, 0}},
        {.sprite = 0xffff, .pieces = 0, .width = 96, .height = 96, .words = {}},
        {.sprite = 999, .pieces = 2, .width = 8, .height = 8, .words = {}},  // sprites the level doesn't load
        {.sprite = 0xffff, .pieces = 0x83, .width = 255, .height = 3, .words = {0, 0, 0, 0xff00, 0x8000}},
        {.sprite = 0xfffe, .pieces = 0x81, .width = 30, .height = 32, .words = {}},
        {.sprite = 286, .pieces = 12, .width = 64, .height = 8, .words = {}},
    };
}

}  // namespace

TEST_CASE("world: a kind's record; an entity created from it in the lowest free slot") {
    const assets::ObjectDatabase database = make_database(sample_records());
    World world;
    world.kinds = kind_records(database);
    world.sprite_sets = {{.entry = 286, .base = 1}, {.entry = 303, .base = 0x2001}};
    CHECK(world.kinds[0].pool == Pool::kMiddle);
    CHECK(world.kinds[1].pool == Pool::kBack);
    CHECK(world.kinds[5].pool == Pool::kFront);
    CHECK(world.kinds[5].pieces == 3);
    CHECK(world.kinds[6].pool == Pool::kFront);
    CHECK(world.kinds[5].max_step_y == -0x8000);

    Entity* viking = world.create({.kind = 0, .x = 100, .y = 50, .flags = 0x6f});
    REQUIRE(viking != nullptr);
    CHECK(viking->slot == 0);
    CHECK(viking->health == 6);
    CHECK(viking->kind_value == 1);
    CHECK(viking->hit_class == 2);
    CHECK(viking->hit_mask == 3);
    CHECK(viking->max_step_x == 0x800);
    CHECK(viking->box == Box{.left = 92, .top = 36, .right = 107, .bottom = 63});
    CHECK(viking->half_width == 8);
    CHECK(viking->half_height == 14);
    CHECK(viking->sprite.first == 24);
    CHECK(world.pieces[24].in_use);
    CHECK(world.pieces[24].mirrored);  // it faces left
    CHECK(world.pieces[24].colour == 7);
    CHECK(world.pieces[24].shape == 2);

    Entity* door = world.create({.kind = 1, .x = 168, .y = 248, .flags = 0x22, .half_width = 24, .half_height = 24});
    REQUIRE(door != nullptr);
    CHECK(door->slot == 1);
    CHECK(door->half_width == 24);
    CHECK(door->sprite.first == 36);
    CHECK(door->sprite.base == 1);
    CHECK(world.pieces[39].frame == 1);
    CHECK(world.create({.kind = 2, .x = 0, .y = 0})->sprite.base == 0x2001);
    CHECK(world.create({.kind = 4, .x = 0, .y = 0}) == nullptr);  // its sprites aren't loaded
    CHECK(world.create({.kind = 3, .x = 0, .y = 0})->slot == 3);

    // Removing frees the slot; the next entity takes the lowest free one.
    world.remove(1);
    CHECK_FALSE(world.pieces[36].in_use);
    CHECK(world.create({.kind = 3, .x = 0, .y = 0})->slot == 1);
    world.remove(17);  // nobody there
}

TEST_CASE("world: no slot left, no pieces left, a placement that is gone") {
    const assets::ObjectDatabase database = make_database(sample_records());
    World world;
    world.kinds = kind_records(database);
    world.sprite_sets = {{.entry = 286, .base = 1}};
    world.placements = {Placement{.x = 10, .y = 10, .width = 16, .height = 16, .kind = 3}};
    world.gone.set(0);
    CHECK(world.create_placed(0) == nullptr);
    world.gone.reset(0);
    CHECK(world.create_placed(0) != nullptr);
    for (int i = 0; i < 7; ++i) {  // 12 pieces each from the back pool's 92
        CHECK(world.create({.kind = 7, .x = 0, .y = 0}) != nullptr);
    }
    CHECK(world.create({.kind = 7, .x = 0, .y = 0}) == nullptr);
    CHECK_FALSE(world.entities[8]);
    for (int i = 0; i < 12; ++i) {
        CHECK(world.create({.kind = 3, .x = 0, .y = 0}) != nullptr);
    }
    CHECK(world.create({.kind = 3, .x = 0, .y = 0}) == nullptr);  // 20 entities
}

TEST_CASE("world: creator and created are unlinked when one goes") {
    const assets::ObjectDatabase database = make_database(sample_records());
    World world;
    world.kinds = kind_records(database);
    Entity* gun = world.create({.kind = 3, .x = 0, .y = 0});
    Entity* shot = world.create({.kind = 3, .x = 0, .y = 0, .creator = gun->slot});
    gun->last_created = shot->slot;
    world.remove(shot->slot);
    CHECK(gun->last_created == kNobody);
    shot = world.create({.kind = 3, .x = 0, .y = 0, .creator = gun->slot});
    gun->last_created = shot->slot;
    world.remove(gun->slot);
    CHECK(shot->creator == kNobody);
}

// --- A level's rules ---

namespace {

class Still : public Entity {
  public:
    void update(World& /*world*/) override {}
};

struct Dice {
    std::uint32_t seed;
    int below(int limit) {
        seed = (seed * 1664525U) + 1013904223U;
        return static_cast<int>((seed >> 9U) % static_cast<std::uint32_t>(limit));
    }
};

}  // namespace

// --- The tick ---

namespace {

// Notes its turns in a shared log, and does what its test tells it to.
class Logged : public Entity {
  public:
    explicit Logged(std::vector<std::string>* log) : log_{log} {}
    void update(World& world) override {
        log_->push_back("update " + std::to_string(slot));
        if (on_update) {
            on_update(world, *this);
        }
    }
    void after_move(World& /*world*/) override { log_->push_back("after_move " + std::to_string(slot)); }
    void check_hits(World& /*world*/) override { log_->push_back("check_hits " + std::to_string(slot)); }

    std::function<void(World&, Logged&)> on_update;

  private:
    std::vector<std::string>* log_;
};

struct Ticking {
    std::vector<std::string> log;
    World world;

    Ticking() {
        world.make = [this](std::uint16_t /*kind*/) { return make_entity<Logged>(&log); };
        world.has_vikings = false;
        world.kinds[1].pieces = 1;  // kind 1 has a sprite
        world.kinds[1].max_step_x = world.kinds[1].max_step_y = 0x1000;
    }
    Logged& add(std::uint16_t kind = 0, int x = 0, int y = 0) {
        return static_cast<Logged&>(*world.create({.kind = kind, .x = x, .y = y}));
    }
};

}  // namespace

TEST_CASE("tick: every entity's turn in slot order, then the moves, then what stops them; hits in the second part") {
    Ticking t;
    t.add();
    Logged& carried = t.add();
    t.add();
    carried.on_update = [](World& /*world*/, Logged& self) { self.moved_by = kMovedFreely; };
    t.world.step(0);
    CHECK(t.log == std::vector<std::string>{"update 0", "update 1", "update 2", "after_move 0", "after_move 2"});
    CHECK(carried.moved_by == kMovesItself);  // moved, and on its own again
    t.log.clear();
    t.world.step(1);
    CHECK(t.log == std::vector<std::string>{"check_hits 0", "check_hits 1", "check_hits 2"});
    t.log.clear();
    t.world.step(2);
    CHECK(t.log.empty());
}

TEST_CASE("tick: an entity created in a lower slot has its turn right after its creator") {
    Ticking t;
    t.add();
    Logged& gone = t.add();
    Logged& gun = t.add();
    t.world.remove(gone.slot);  // slot 1 is free
    bool fired = false;
    gun.on_update = [&fired](World& world, Logged& self) {
        if (!fired) {
            fired = true;
            Entity* low = world.create_by(self, 0, 10, 10, 7, 0x0801);  // into slot 1
            Entity* high = world.create_by(self, 0, 20, 20);            // into slot 3
            REQUIRE(low != nullptr);
            REQUIRE(high != nullptr);
            CHECK(low->slot == 1);
            CHECK(low->argument == 7);
            CHECK(low->creator == self.slot);
            CHECK(self.last_created == high->slot);
        }
    };
    gun.flags = 0x4142;  // faces left; its kind's bit 0x4000 isn't passed on
    t.world.step(0);
    CHECK(t.log == std::vector<std::string>{"update 0", "update 2", "update 1", "update 3", "after_move 0",
                                            "after_move 1", "after_move 2", "after_move 3"});
    CHECK(t.world.entities[1]->flags == 0x0843);
    CHECK(t.world.entities[3]->flags == 0x0042);
}

TEST_CASE("tick: the countdown, and the turn of an entity that left the active area") {
    Ticking t;
    Logged& timer = t.add();
    timer.flags = entity_flag::kCountingDown;
    timer.countdown = 2;
    Logged& idle = t.add();
    idle.countdown = 5;
    t.world.step(0);
    CHECK(timer.countdown == 1);
    CHECK(idle.countdown == 5);
    t.world.step(0);
    t.world.step(0);
    CHECK(timer.countdown == 0);

    // Far outside: marked at the tick's end, removed (as most kinds are) at its next turn; not a dead one. The first
    // three slots are the Vikings' and never leave.
    Logged& third = t.add(0, 5000, 5000);
    Logged& far = t.add(0, 5000, 5000);
    Logged& dead = t.add(0, 5000, 5000);
    dead.dead = true;
    const int far_slot = far.slot;
    const int dead_slot = dead.slot;
    for (int part = 0; part < 3; ++part) {
        t.world.step(part);
    }
    CHECK((t.world.entities.at(static_cast<std::size_t>(far_slot))->flags & entity_flag::kLeftTheActiveArea) != 0);
    CHECK((third.flags & entity_flag::kLeftTheActiveArea) == 0);
    t.log.clear();
    t.world.step(0);
    CHECK_FALSE(t.world.entities.at(static_cast<std::size_t>(far_slot)));
    CHECK(t.world.entities.at(static_cast<std::size_t>(dead_slot)));
    CHECK(std::ranges::find(t.log, "update " + std::to_string(dead_slot)) == t.log.end());
}

TEST_CASE("tick: keys newly pressed; a push becomes a move, its sprite follows in three shares, the camera too") {
    Ticking t;
    t.world.has_vikings = true;
    t.world.camera.max_x = 2000;
    t.world.camera.max_y = 2000;
    t.world.camera.x = 400;
    t.world.camera.y = 400;
    Logged& viking = t.add(1, 400 + 176, 400 + 88);  // at the right edge of the camera's box
    t.world.shown_viking = 0;
    std::uint16_t seen_pressed = 0;
    viking.on_update = [&seen_pressed](World& world, Logged& self) {
        seen_pressed = world.pressed;
        CHECK(self.push_x == 0);  // a tick's push starts from nothing
        self.push_x = 7 * 256;
    };
    const int piece = viking.sprite.first;
    t.world.pieces.at(static_cast<std::size_t>(piece)).x = 568;
    t.world.step(0, 0x0100);
    CHECK(seen_pressed == 0x0100);
    CHECK(viking.x == 583);
    CHECK(viking.push_x == 7);
    CHECK(t.world.pieces.at(static_cast<std::size_t>(piece)).x == 568 + 3);
    CHECK(t.world.camera.x == 403);
    t.world.step(1);
    t.world.step(2);
    CHECK(t.world.pieces.at(static_cast<std::size_t>(piece)).x == 568 + 7);
    CHECK(t.world.camera.x == 407);
    t.world.step(0, 0x0100);  // still held: not newly pressed
    CHECK(seen_pressed == 0);
    t.world.step(0, 0x8100);
    CHECK(seen_pressed == 0x8000);
}

TEST_CASE("tick: an animation step plays the sprite's program and adds the entity's speed to its push") {
    Ticking t;
    Logged& thing = t.add(1, 100, 100);
    const std::vector<std::byte> program{
        std::byte{0x07}, std::byte{3},    std::byte{0x02}, std::byte{9},
        std::byte{0},    std::byte{0x0F}, std::byte{2}};  // move 3 across, sound 9, wait 2
    t.world.programs = program;
    t.world.gravity = 0xf8;
    thing.flags = entity_flag::kFalls;
    thing.speed_x = px(0.5);
    start_animation(thing.sprite, 0);
    t.world.animate(thing);
    CHECK(thing.push_x == (3 * 256) + 0x80);
    CHECK(thing.speed_y == px(0.96875));
    CHECK(thing.push_y == 0xf8);
    REQUIRE(t.world.cues.size() == 1);
    CHECK(t.world.cues[0] == SoundCue{.kind = SoundCue::Kind::kEffect, .value = 9, .tune = 0, .entity = thing.slot});
    CHECK(thing.sprite.delay == 2);
}

TEST_CASE("palette: ranges of colours go round, each at its own pace, up or down") {
    World world;
    for (std::uint8_t colour = 0; colour < 20; ++colour) {
        world.palette.at(colour) = {.r = colour, .g = 0, .b = 0};
    }
    world.cycles = {{.delay = 2, .left = 2, .from = 4, .to = 6, .running = true},    // up
                    {.delay = 1, .left = 1, .from = 12, .to = 10, .running = true},  // down
                    {.delay = 1, .left = 1, .from = 16, .to = 18, .running = false}};
    world.turn_colours();
    CHECK(world.palette[4].r == 4);  // not its turn yet
    CHECK(world.palette[12].r == 10);
    CHECK(world.palette[11].r == 12);
    CHECK(world.palette[10].r == 11);
    world.turn_colours();
    CHECK(world.palette[4].r == 6);  // the last comes round to the first
    CHECK(world.palette[5].r == 4);
    CHECK(world.palette[6].r == 5);
    CHECK(world.palette[12].r == 11);
    CHECK(world.palette[16].r == 16);  // one that doesn't run
    CHECK(world.cycles[0].left == 2);
}

TEST_CASE("remove: the slot is free at once, the removed one is still there until its turn is over") {
    Ticking t;
    Logged& first = t.add();
    t.add();
    int taken = -1;
    first.on_update = [&taken](World& world, Logged& self) {
        const int mine = self.slot;
        world.remove(mine);
        CHECK(world.at(mine) == nullptr);  // as the original has it: free in the middle of the turn
        // What it does on its way out finds it there (the sanitizer build fails this test otherwise).
        self.countdown = 7;
        CHECK(self.countdown == 7);
        // And what is created now takes the slot it left.
        const Entity* next = world.create({.kind = 0, .x = 0, .y = 0});
        REQUIRE(next != nullptr);
        taken = next->slot;
    };
    t.world.step(0);
    CHECK(taken == 0);
    CHECK(t.world.at(0) != nullptr);
    CHECK(t.world.at(1) != nullptr);
}

TEST_CASE("world: Vanilla rules have the original's 20 slots; Remaster rules have room no level fills") {
    World world;
    CHECK(world.slots() == kSlots);
    CHECK(world.at(kSlots) == nullptr);
    world.set_rules(Rules::kRemaster);
    CHECK(world.rules == Rules::kRemaster);
    CHECK(world.slots() == kRemasterSlots);
    CHECK(world.at(kRemasterSlots - 1) == nullptr);
    CHECK(world.at(kRemasterSlots) == nullptr);
    world.set_rules(Rules::kVanilla);
    CHECK(world.slots() == kSlots);
}
