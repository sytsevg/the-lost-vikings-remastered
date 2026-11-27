// Tests for src/net/protocol.*: every message written and read back the same; what isn't a whole message, or isn't
// this game's, read as nothing; names as the game shows them.
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

#include "net/protocol.hpp"

using namespace lvr::net;

TEST_CASE("protocol: every message comes back as it was written") {
    Input input;
    input.bytes[0] = 7;
    input.bytes[15] = 0xff;
    const std::vector<Message> all{
        Hello{.protocol = kProtocol,
              .state_version = 7,
              .build = "7654737",
              .game = 0x0123456789abcdefULL,
              .name = "SYTSE"},
        Refused{.why = "another version"},
        Welcome{.player = 2},
        LobbyStatus{.names = {"PLAYER 1", "PLAYER 2"},
                    .network = {kUnknown, 23, kUnknown},
                    .engine = {kUnknown, 40, 7},
                    .slot = 43,
                    .countdown = 2},
        Back{},
        Start{.game = 2, .players = 3, .delay = 1, .slot = 39, .cheat = true, .names = {"A", "B", "C"}},
        Inputs{.game = 3, .player = 1, .first = 1234, .inputs = {input, Input{}, input}},
        Status{.game = 6,
               .player = 0,
               .step = 99,
               .advantage = -3,
               .knows = {10, 11, 12},
               .network = {0, 3, kUnknown},
               .engine = {0, 20, 37}},
        Hash{.game = 4, .player = 1, .step = 103, .hash = 0xfeedfacecafebeefULL, .resyncs = 2},
        Leave{.player = 2, .from = 500},
        Quit{},
        Echo{.sent = 123456},
        Echoed{.sent = 654321},
        Arrive{.player = 2, .at = 777, .name = "LATE"},
        Joining{.game = 5,
                .player = 2,
                .step = 760,
                .arrives = 777,
                .delay = 1,
                .names = {"A", "B", "LATE"},
                .state = std::vector<std::uint8_t>(70000, 0xab),
                .presence = {{.player = 1, .at = 780, .there = true}, {.player = 2, .at = 777, .there = true}}},
        Decided{.game = 7, .until = 4321, .presence = {{.player = 2, .at = 4300, .there = false}}},
        Bundle{.parts = {encode(Echo{.sent = 1}), encode(Echoed{.sent = 2})}},
        Resync{.game = 8, .player = 1, .step = 1200, .resyncs = 3, .state = std::vector<std::uint8_t>(5000, 0x5a)},
    };
    for (const Message& message : all) {
        const std::vector<std::uint8_t> bytes = encode(message);
        const std::optional<Message> read = decode(bytes);
        REQUIRE(read.has_value());
        CHECK(*read == message);
    }
}

TEST_CASE("protocol: a message cut short, one too long, another game's hello: nothing") {
    const std::vector<std::uint8_t> hash = encode(Hash{.player = 1, .step = 2, .hash = 3});
    CHECK_FALSE(decode(std::span{hash}.first(hash.size() - 1)).has_value());
    std::vector<std::uint8_t> longer = hash;
    longer.push_back(0);
    CHECK_FALSE(decode(longer).has_value());
    std::vector<std::uint8_t> hello = encode(Hello{});
    hello[1] ^= 0xff;  // another magic number
    CHECK_FALSE(decode(hello).has_value());
    CHECK_FALSE(decode(std::vector<std::uint8_t>{200}).has_value());  // no such message
    CHECK_FALSE(decode(std::vector<std::uint8_t>{}).has_value());
}

TEST_CASE("protocol: a name is shown in capitals, what the font has, at most eight; none is PLAYER and a number") {
    CHECK(shown_name("sytse", 0) == "SYTSE");
    CHECK(shown_name("a-b_c!1", 0) == "ABC1");
    CHECK(shown_name("Erik the Swift", 0) == "ERIK THE");
    CHECK(shown_name("", 1) == "PLAYER 2");
    CHECK(shown_name("???", 2) == "PLAYER 3");
}

TEST_CASE("protocol: a bundle's messages come out as they went in; one that can't be read is left out") {
    const Bundle bundle{.parts = {encode(Echo{.sent = 5}), {0xff, 0x00}, encode(Quit{})}};
    const std::optional<Message> back = decode(encode(bundle));
    REQUIRE(back);
    const std::vector<Message> messages = unbundled(*back);
    REQUIRE(messages.size() == 2);
    CHECK(std::get<Echo>(messages[0]).sent == 5);
    CHECK(std::holds_alternative<Quit>(messages[1]));
    CHECK(unbundled(Message{Quit{}}).size() == 1);  // (not a bundle: itself)
}
