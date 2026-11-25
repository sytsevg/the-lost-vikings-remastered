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
        Lobby{.names = {"PLAYER 1", "PLAYER 2"}},
        Start{.players = 3, .delay = 1, .slot = 39, .cheat = true, .names = {"A", "B", "C"}},
        Inputs{.player = 1, .first = 1234, .inputs = {input, Input{}, input}},
        Status{.player = 0,
               .step = 99,
               .advantage = -3,
               .knows = {10, 11, 12},
               .network = {0, 3, kUnknown},
               .engine = {0, 20, 37}},
        Hash{.player = 1, .step = 103, .hash = 0xfeedfacecafebeefULL},
        Leave{.player = 2, .from = 500},
        Quit{},
        Echo{.sent = 123456},
        Echoed{.sent = 654321},
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
