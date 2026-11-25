// Tests for src/lvr/coop.* (co-op online, ADR-0012): the game itself under rollback. Two machines play level 1 over a
// network that delays and loses what is sent; they must come to the very state of a third that had every input
// from the start.
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <random>
#include <vector>

#include "lvr/coop.hpp"
#include "lvr/names.hpp"
#include "lvr/session.hpp"
#include "net/rollback.hpp"

using namespace lvr;

namespace {

constexpr std::uint16_t kF = 0x8000;
constexpr std::uint16_t kRight = 0x0100;
constexpr std::uint16_t kLeft = 0x0200;
constexpr std::uint16_t kNext = 0x0010;
constexpr std::uint16_t kInventory = 0x2000;

// What each player holds at each step: the host answers the level's opening boxes, then both walk, jump, switch,
// and the second looks into his inventory.
std::uint16_t held(int player, std::uint32_t step) {
    if (player == 0) {
        if (step < 80) {
            return step % 10 == 5 ? kF : 0;  // each box of the opening, answered
        }
        if (step < 140) {
            return static_cast<std::uint16_t>(kRight | (step % 30 == 0 ? kF : 0));
        }
        return step % 20 < 10 ? kLeft : 0;
    }
    if (step >= 90 && step < 120) {
        return kLeft;
    }
    if (step == 125) {
        return kInventory;
    }
    if (step == 128 || step == 131) {
        return kRight;  // the cursor, in his inventory
    }
    if (step == 134) {
        return kInventory;
    }
    return step == 150 ? kNext : 0;
}

std::unique_ptr<Session> machine(int player) {
    auto opened =
        Session::open(LVR_ORIGINAL_DIR, 0, {}, Rules::kRemaster, draw, Online{.on = true, .players = 2}, player);
    REQUIRE(opened);
    return std::move(*opened);
}

}  // namespace

TEST_CASE("real data: two machines online come to the state of one that had every input", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    constexpr std::uint32_t kSteps = 180;
    constexpr int kLatency = 2;  // steps on the way, and up to two more
    struct Machine {
        std::unique_ptr<Session> session;
        std::unique_ptr<CoopGame> game;
        std::unique_ptr<net::Rollback> rollback;
        std::uint32_t acked = 0;  // how far the other has acknowledged this one's inputs
    };
    std::vector<Machine> machines;
    for (int player = 0; player < 2; ++player) {
        Machine one;
        one.session = machine(player);
        const net::Settings settings{.players = 2, .local = player, .delay = 1};
        one.game = std::make_unique<CoopGame>(*one.session, static_cast<std::size_t>(settings.window) + 2);
        one.rollback = std::make_unique<net::Rollback>(*one.game, settings);
        machines.push_back(std::move(one));
    }
    REQUIRE(machines[0].session->online());
    struct Packet {
        std::uint32_t at;
        int to;
        int from;
        std::vector<std::pair<std::uint32_t, net::Input>> inputs;
        std::uint32_t knows;
    };
    std::vector<Packet> in_flight;
    std::minstd_rand random{7};                     // NOLINT(bugprone-random-generator-seed): the same run every time
    std::vector<std::vector<net::Input>> given(2);  // by player, by step: what each machine's player gave
    for (std::uint32_t now = 0; now < kSteps + 40; ++now) {
        std::vector<Packet> later;
        for (Packet& packet : in_flight) {
            if (packet.at > now) {
                later.push_back(std::move(packet));
                continue;
            }
            Machine& to = machines.at(static_cast<std::size_t>(packet.to));
            for (const auto& [step, input] : packet.inputs) {
                to.rollback->add_remote(packet.from, step, input);
            }
            to.acked = std::max(to.acked, packet.knows);
        }
        in_flight = std::move(later);
        for (int me = 0; me < 2; ++me) {
            Machine& mine = machines.at(static_cast<std::size_t>(me));
            net::Rollback& rollback = *mine.rollback;
            const std::uint32_t next = rollback.local_known();
            if (next < kSteps + 1) {
                const StepInput local = mine.session->take_input(held(me, next), {}, {});
                const net::Input packed = pack(local, me);
                if (rollback.add_local(packed)) {
                    given.at(static_cast<std::size_t>(me)).resize(next + 1);
                    given.at(static_cast<std::size_t>(me)).at(next) = packed;
                }
            }
            if (rollback.step() < kSteps) {
                (void)rollback.advance();
            }
            const int other = 1 - me;
            if (random() % 10 != 0) {  // one in ten lost
                in_flight.push_back({.at = now + kLatency + static_cast<std::uint32_t>(random() % 3),
                                     .to = other,
                                     .from = me,
                                     .inputs = rollback.local_from(mine.acked),
                                     .knows = rollback.known(other)});
            }
        }
    }
    for (Machine& one : machines) {
        one.rollback->catch_up();
        REQUIRE(one.rollback->step() == kSteps);
        REQUIRE(one.rollback->confirmed() >= kSteps);
        CHECK_FALSE(one.game->take_pictures().empty());
    }
    // The straight run: every input known at every step.
    auto truth = machine(0);
    const int erik_from = truth->world().at(0)->x;
    const int baleog_from = truth->world().at(1)->x;
    for (std::uint32_t step = 0; step < kSteps; ++step) {
        const std::vector<net::Input> both{given[0].at(step), given[1].at(step)};
        (void)truth->run(unpack(both), step);
    }
    const std::uint64_t expected = truth->state_hash();
    CHECK(machines[0].session->state_hash() == expected);
    CHECK(machines[1].session->state_hash() == expected);
    CHECK(machines[0].rollback->steps_run_again() + machines[1].rollback->steps_run_again() > 0);
    CHECK(truth->world().player_count == kMostPlayers);  // a place for each player there can be ...
    CHECK_FALSE(truth->world().players[2].here);         // ... the third nobody's
    // What they did happened: past the opening's boxes the host's Erik walked, and the second player's Baleog.
    CHECK(truth->world().at(0)->x != erik_from);
    CHECK(truth->world().at(1)->x != baleog_from);
    CHECK(truth->world().lines.empty());
}

TEST_CASE("coop: a step's input as bytes and back; what is pressed once isn't predicted again") {
    StepInput input;
    input.players[0] = {
        .keys = 0x8100, .pace = Pace{.across = 8, .up_down = 16}, .zoom = {480, 360}, .view = {480, 360}};
    input.players[1] = {.keys = 0x0200, .pace = {}, .zoom = {320, 176}, .view = {}};
    input.typed = 'Y';
    input.menu.yes = true;
    input.menu.next_level = true;
    const std::vector<net::Input> packed{pack(input, 0), pack(input, 1)};
    const StepInput back = unpack(packed);
    CHECK(back.players[0].keys == 0x8100);
    CHECK(back.players[0].pace.across == 8);
    CHECK(back.players[0].zoom == std::array<std::int16_t, 2>{480, 360});
    CHECK(back.players[1].keys == 0x0200);
    CHECK(back.players[1].view == std::array<std::int16_t, 2>{0, 0});
    CHECK(back.typed == 'Y');
    CHECK(back.menu.yes);
    CHECK(back.menu.next_level);
    CHECK_FALSE(back.menu.quit);
    CHECK(pack(input, 1).bytes[12] == 0);  // only the host's has them
}

TEST_CASE("coop: a player gone is so in the step's input") {
    const std::vector<net::Input> inputs{net::Input{}, net::gone_input()};
    const StepInput input = unpack(inputs);
    CHECK_FALSE(input.players[0].gone);
    CHECK(input.players[1].gone);
}

TEST_CASE("real data: online, the intro is the first player's to play, as the recording has it; the others watch",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    auto alone = Session::open(LVR_ORIGINAL_DIR, scene::kIntro, {}, Rules::kRemaster);
    auto guest =
        Session::open(LVR_ORIGINAL_DIR, scene::kIntro, {}, Rules::kRemaster, draw, Online{.on = true, .players = 2}, 1);
    REQUIRE(alone);
    REQUIRE(guest);
    const World& world = (*guest)->world();
    CHECK(world.attract);
    CHECK(world.players[1].viking == kNobody);  // no Viking of his own ...
    for (int step = 0; step < 300; ++step) {
        (*alone)->step(0);
        (*guest)->step(static_cast<std::uint16_t>(kRight | kF));  // ... and what he presses is nothing meanwhile
        REQUIRE(world.players[1].lines.empty());                  // what is said is everyone's
    }
    // The Vikings as with one player; his view where the first player's is, on the Viking the recording plays.
    for (int slot = 0; slot < 3; ++slot) {
        CHECK(world.at(slot)->x == (*alone)->world().at(slot)->x);
        CHECK(world.at(slot)->y == (*alone)->world().at(slot)->y);
    }
    CHECK(world.followed_by(world.players[1]) == world.players[0].viking);
    CHECK(world.players[1].camera.x == world.players[0].camera.x);
    CHECK(world.players[1].camera.y == world.players[0].camera.y);
}
