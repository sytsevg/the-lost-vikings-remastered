// Tests for src/net/lobby.* and src/net/connection.* (co-op online): the lobby on the simulated network.
// Who is given a place and who is turned away; one who leaves, the others closed up; the countdown and the start.
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "net/tests/table.hpp"

using namespace lvr::net;
using namespace lvr::net::test;

TEST_CASE(
    "lobby: guests are given places; another version, build or game files are turned away, as is a fourth; "
    "one who leaves, the others closed up; the host gone") {
    Table<> table{{.delay_ms = 20, .jitter_ms = 10, .loss_percent = 10, .late_ms = 100, .behind_ms = 0}};
    Machine<>& host = table.host();
    Machine<>& first = table.join("first");
    Identity old = identity("");
    old.state_version = 0;
    Identity other_build = identity("");
    other_build.build = "other";
    Identity other_files = identity("");
    other_files.game = 2;
    Machine<>& outdated = table.join("x", old);
    Machine<>& built = table.join("x", other_build);
    Machine<>& files = table.join("x", other_files);
    REQUIRE(table.until([&] {
        return first.lobby->welcomed() && !outdated.error.empty() && !built.error.empty() && !files.error.empty();
    }));
    CHECK(outdated.error == "refused: another version of the game");
    CHECK(built.error == "refused: another build (other; the host's is test)");
    CHECK(files.error == "refused: other game files");
    CHECK(first.player() == 1);

    Machine<>& second = table.join("second");
    REQUIRE(table.until([&] { return second.lobby->welcomed(); }));
    Machine<>& third = table.join("third");
    REQUIRE(table.until([&] { return !third.error.empty(); }));
    CHECK(third.error == "refused: the game is full");
    REQUIRE(table.until([&] { return first.connection->names().size() == 3 && first.connection->network_ms(2); }));
    CHECK(first.connection->names() == std::vector<std::string>{"HOST", "FIRST", "SECOND"});
    CHECK(*first.connection->network_ms(2) == 40);  // (the host's round trip to the second, as the host has it)

    // The first leaves: the second is player 1 now, on every machine.
    first.lobby.reset();
    first.connection.reset();
    first.gone = true;
    REQUIRE(table.until([&] { return second.player() == 1 && second.connection->names().size() == 2; }));
    CHECK(host.connection->names() == std::vector<std::string>{"HOST", "SECOND"});
    CHECK(second.connection->names() == host.connection->names());

    host.hosting.reset();
    host.connection.reset();
    host.gone = true;
    REQUIRE(table.until([&] { return !second.error.empty(); }));
    CHECK(second.error == "the host is gone");
}

TEST_CASE(
    "lobby: START counts down, everyone told; cancelled, nothing starts; started, every machine the same game, "
    "its delay from the round trips") {
    Table<> table{{.delay_ms = 40, .jitter_ms = 0, .loss_percent = 0, .late_ms = 100, .behind_ms = 0}};
    Machine<>& host = table.host();
    Machine<>& guest = table.join("guest");
    REQUIRE(table.until([&] { return guest.lobby->welcomed(); }));
    host.hosting->set_slot(5);
    REQUIRE(table.until([&] { return guest.lobby->slot() == 5; }));

    host.hosting->start();
    REQUIRE(table.until([&] { return guest.lobby->countdown() == 3; }));
    host.hosting->cancel_start();
    REQUIRE(table.until([&] { return guest.lobby->countdown() == 0; }));
    table.wait(300);  // (five seconds: nothing starts)
    CHECK(host.match == nullptr);
    CHECK(guest.match == nullptr);

    host.hosting->start();
    bool counted = false;
    REQUIRE(table.until([&] {
        counted = counted || (guest.lobby != nullptr && guest.lobby->countdown() == 1);  // (3, 2, 1)
        return host.started && guest.started;
    }));
    CHECK(counted);
    CHECK(guest.started->player == 1);
    CHECK(guest.started->players == 2);
    CHECK(guest.started->slot == 5);
    CHECK(guest.started->delay == 1);  // (a round trip of 80 ms)
    CHECK(guest.started->delay == host.started->delay);
    CHECK(guest.started->names == host.started->names);
    CHECK(guest.connection->game() == host.connection->game());
}
