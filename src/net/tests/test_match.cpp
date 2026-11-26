// Tests for src/net/match.* (co-op online, ADR-0012): games online on the toy game over the simulated network, the
// host in the middle. Every machine comes to the same game through a network that loses, delays and reorders; one
// that goes its own way is found out; what isn't a player's own isn't taken as his; back to the lobby and another
// game; joining under way, leaving and coming back.
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <vector>

#include "net/tests/table.hpp"

using namespace lvr::net;
using namespace lvr::net::test;

namespace {

// The host's START: everyone there into the game, after the countdown.
void start(Table<>& table, Machine<>& host) {
    host.hosting->start();
    REQUIRE(table.until([&] {
        return std::ranges::all_of(table.machines, [](const Machine<>& one) { return one.gone || one.match; });
    }));
}

// The steps before `steps` a machine didn't show exactly once.
int not_shown_once(const Machine<>& machine, std::uint32_t steps) {
    int wrong = 0;
    for (std::uint32_t step = 0; step < steps; ++step) {
        const auto found = machine.game->shown.find(step);
        wrong += found == machine.game->shown.end() || found->second != 1 ? 1 : 0;
    }
    return wrong;
}

}  // namespace

TEST_CASE(
    "match: a host and a guest over a network that loses, delays and reorders: the same game, each step shown "
    "once") {
    Table<> table{{.delay_ms = 30, .jitter_ms = 25, .loss_percent = 20, .late_ms = 150, .behind_ms = 0}};
    Machine<>& host = table.host();
    Machine<>& guest = table.join("guest");
    REQUIRE(table.until([&] { return guest.lobby->welcomed(); }));
    start(table, host);
    REQUIRE(table.run_to(300));
    table.wait(60);
    CHECK(table.same_game());
    CHECK(not_shown_once(host, 300) == 0);
    CHECK(not_shown_once(guest, 300) == 0);
}

TEST_CASE("match: three machines a long way apart (150 ms each way): the same game, two steps of input delay") {
    Table<> table{{.delay_ms = 150, .jitter_ms = 20, .loss_percent = 10, .late_ms = 150, .behind_ms = 0}};
    Machine<>& host = table.host();
    Machine<>& first = table.join("first");
    Machine<>& second = table.join("second");
    REQUIRE(table.until([&] { return first.lobby->welcomed() && second.lobby->welcomed(); }));
    start(table, host);
    CHECK(host.started->delay == 2);
    REQUIRE(table.run_to(200));
    table.wait(120);
    CHECK(table.same_game());
}

TEST_CASE(
    "match: a machine that goes its own way is found out of step at the first step compared after it; the log "
    "says what went before") {
    Table<> table{{.delay_ms = 10, .jitter_ms = 5, .loss_percent = 0, .late_ms = 100, .behind_ms = 0}};
    Machine<>& host = table.host();
    Machine<>& guest = table.join("guest");
    REQUIRE(table.until([&] { return guest.lobby->welcomed(); }));
    start(table, host);
    guest.game->spoil_at = 100;
    REQUIRE(table.until([&] { return !host.error.empty() && !guest.error.empty(); }));
    CHECK(host.error == "out of step with another machine at step 103");
    CHECK(guest.error == host.error);
    const std::string log = host.match->log();
    CHECK(log.starts_with("coop 1\nbuild test\nslot 0\nplayers 2\ncheat 0\nplayer 0\nfirst 0\nstep 0 "));
    CHECK(log.find("hash 95 ") != std::string::npos);
}

TEST_CASE("match: what a guest says is another player's (his inputs, his hash) isn't taken as his") {
    Table<> table{{.delay_ms = 10, .jitter_ms = 5, .loss_percent = 0, .late_ms = 100, .behind_ms = 0}};
    Machine<>& host = table.host();
    Machine<>& first = table.join("first");
    Machine<>& second = table.join("second");
    REQUIRE(table.until([&] { return first.lobby->welcomed() && second.lobby->welcomed(); }));
    start(table, host);
    REQUIRE(table.run_to(20));
    // The first says what the second gives the next forty steps, and the second's hash of a step to come.
    const std::uint32_t at = first.at_step();
    Input other;
    other.bytes[0] = 9;
    first.connection->send(
        0, Inputs{.game = first.connection->game(), .player = 2, .first = at + 1, .inputs = std::vector(40, other)},
        Channel::kFast);
    first.connection->send(
        0, Hash{.game = first.connection->game(), .player = 2, .step = (((at + 40) / 8) * 8) + 7, .hash = 12345},
        Channel::kReliable);
    REQUIRE(table.run_to(at + 100));
    table.wait(60);
    CHECK(table.same_game());
}

TEST_CASE("match: back to the lobby with the game's messages still on their way; another game, the same on both") {
    Table<> table{{.delay_ms = 20, .jitter_ms = 10, .loss_percent = 10, .late_ms = 100, .behind_ms = 0}};
    Machine<>& host = table.host();
    Machine<>& guest = table.join("guest");
    REQUIRE(table.until([&] { return guest.lobby->welcomed(); }));
    start(table, host);
    REQUIRE(table.run_to(60));
    table.to_lobby(host);
    REQUIRE(table.until([&] { return guest.back; }));
    table.to_lobby(guest);
    REQUIRE(table.until([&] { return guest.connection->names() == std::vector<std::string>{"HOST", "GUEST"}; }));
    start(table, host);
    CHECK(host.connection->game() == 2);
    CHECK(guest.connection->game() == 2);
    REQUIRE(table.run_to(60));
    table.wait(60);
    CHECK(table.same_game());
}

TEST_CASE(
    "match: one joins under way, the guest playing told of it later than the inputs past it come; one leaves "
    "(nobody waits for him) and another comes into his place: every machine the same game") {
    // (Every reliable message a second and a half behind the fast ones: longer than a joiner's 16 steps of lead.)
    Table<> table{{.delay_ms = 20, .jitter_ms = 10, .loss_percent = 10, .late_ms = 100, .behind_ms = 1500}};
    Machine<>& host = table.host();
    Machine<>& first = table.join("first");
    REQUIRE(table.until([&] { return first.lobby->welcomed(); }));
    start(table, host);
    REQUIRE(table.run_to(60));

    Machine<>& late = table.join("late");
    REQUIRE(table.until([&] { return late.match != nullptr; }));
    CHECK(late.player() == 2);
    CHECK_FALSE(late.started->state.empty());
    REQUIRE(table.run_to(120));

    table.leave(first);
    REQUIRE(table.run_to(160));
    Machine<>& back = table.join("back");
    REQUIRE(table.until([&] { return back.match != nullptr; }));
    CHECK(back.player() == 1);
    REQUIRE(table.run_to(240));
    table.wait(60);
    CHECK(table.same_game());
    CHECK(host.connection->names().at(1) == "BACK");
    CHECK(late.connection->names().at(1) == "BACK");
}

TEST_CASE(
    "match: two join under way right after each other on a slow network, where word of an arrival can come "
    "later than the inputs past it: every machine the same game, nobody out of step") {
    // (Every reliable message a second and a half behind the fast ones: longer than a joiner's 16 steps of lead.)
    Table<> table{{.delay_ms = 60, .jitter_ms = 60, .loss_percent = 20, .late_ms = 200, .behind_ms = 1500}};
    Machine<>& host = table.host();
    start(table, host);  // (alone)
    REQUIRE(table.run_to(40));
    Machine<>& first = table.join("first");
    Machine<>& second = table.join("second");
    REQUIRE(table.until([&] { return first.match != nullptr && second.match != nullptr; }));
    CHECK(first.player() + second.player() == 3);  // (1 and 2, as their hellos came)
    REQUIRE(table.run_to(200));
    table.wait(120);
    CHECK(table.same_game());
}
