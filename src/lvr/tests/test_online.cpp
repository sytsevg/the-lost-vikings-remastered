// Tests for src/lvr/online.* (co-op online, ADR-0012): a host and a client in one process over 127.0.0.1, each
// with its own session, through the lobby and into level 1; they play the same game.
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <thread>

#include "lvr/coop.hpp"
#include "lvr/online.hpp"
#include "lvr/session.hpp"

using namespace lvr;

namespace {

constexpr std::uint16_t kFirstPort = 47261;
constexpr std::uint16_t kF = 0x8000;
constexpr std::uint16_t kRight = 0x0100;
constexpr std::uint16_t kLeft = 0x0200;

std::uint16_t held(int player, std::uint32_t step) {
    if (player == 0) {
        return step < 80 ? (step % 10 == 5 ? kF : 0) : kRight;
    }
    return step >= 90 ? kLeft : 0;
}

}  // namespace

TEST_CASE("real data: a host and a client play level 1 online, the same game", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    const NetGame::Identity identity{.build = "test", .game = 1, .name = "host"};
    std::unique_ptr<NetGame> host;
    std::uint16_t port = kFirstPort;
    for (; port < kFirstPort + 20 && !host; ++port) {
        if (auto opened = NetGame::host(port, 2, 0, false, std::nullopt, identity)) {
            host = std::move(*opened);
        }
    }
    REQUIRE(host);
    --port;
    // Another build is turned away.
    auto other = NetGame::join("127.0.0.1", port, {.build = "other", .game = 1, .name = "x"});
    REQUIRE(other);
    auto client = NetGame::join("127.0.0.1", port, {.build = "test", .game = 1, .name = "guest"});
    REQUIRE(client);
    std::optional<NetGame::Joined> at_host;
    std::optional<NetGame::Joined> at_client;
    bool refused = false;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while ((!at_host || !at_client || !refused) && std::chrono::steady_clock::now() < until) {
        if (!at_host) {
            auto lobby = host->lobby();
            REQUIRE(lobby);
            at_host = *lobby;
        }
        if (!at_client) {
            auto lobby = (*client)->lobby();
            REQUIRE(lobby);
            at_client = *lobby;
        }
        if (!refused) {
            refused = !(*other)->lobby().has_value();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    REQUIRE(at_host);
    REQUIRE(at_client);
    CHECK(refused);
    CHECK(at_client->player == 1);
    CHECK(at_host->names == std::vector<std::string>{"HOST", "GUEST"});
    CHECK(at_client->names == at_host->names);
    CHECK(at_client->delay == at_host->delay);

    std::vector<std::unique_ptr<Session>> sessions;
    std::vector<NetGame*> games{host.get(), client->get()};
    for (int player = 0; player < 2; ++player) {
        auto opened = Session::open(LVR_ORIGINAL_DIR, at_host->slot, {}, Rules::kRemaster, draw,
                                    Online{.on = true, .players = 2}, player);
        REQUIRE(opened);
        sessions.push_back(std::move(*opened));
        games.at(static_cast<std::size_t>(player))->begin(*sessions.back());
    }
    constexpr std::uint32_t kSteps = 100;
    std::array<std::uint32_t, 2> steps{};
    // (A guard against hanging only: a sanitizers' build with every core busy runs it slowly.)
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{300};
    while ((steps[0] < kSteps || steps[1] < kSteps) && std::chrono::steady_clock::now() < end) {
        for (int player = 0; player < 2; ++player) {
            NetGame& game = *games.at(static_cast<std::size_t>(player));
            Session& session = *sessions.at(static_cast<std::size_t>(player));
            REQUIRE(game.poll());
            std::uint32_t& at = steps.at(static_cast<std::size_t>(player));
            if (at >= kSteps) {
                continue;
            }
            if (game.wants_input()) {
                game.give(session.take_input(held(player, at + static_cast<std::uint32_t>(at_host->delay)), {}, {}));
            }
            if (game.step()) {
                ++at;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    REQUIRE(steps[0] == kSteps);
    REQUIRE(steps[1] == kSteps);
    // The last inputs on their way, and any step they put right run again.
    const auto settle = std::chrono::steady_clock::now() + std::chrono::milliseconds{500};
    while (std::chrono::steady_clock::now() < settle) {
        for (NetGame* game : games) {
            REQUIRE(game->poll());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    CHECK(sessions[0]->state_hash() == sessions[1]->state_hash());
    // The round trips to the host, as the host measures them, the same on both machines; the host's own: none.
    CHECK(host->network_ms(1).has_value());
    CHECK(host->engine_ms(1).has_value());
    CHECK((*client)->network_ms(1).has_value());
    CHECK((*client)->engine_ms(1).has_value());
    CHECK_FALSE(host->network_ms(0).has_value());
    CHECK_FALSE((*client)->network_ms(0).has_value());
    // The game's log, as it would be written if the machines went out of step: played again it comes to the same
    // states; one hash spoiled, the replay says at which step.
    const std::string log = host->log();
    REQUIRE(log.find("hash 7 ") != std::string::npos);
    const auto replayed = replay_log(LVR_ORIGINAL_DIR, log);
    REQUIRE(replayed);
    CHECK_FALSE(replayed->has_value());
    std::string spoiled = log;
    const std::size_t at = spoiled.find("hash 15 ") + std::string_view{"hash 15 "}.size();
    spoiled.at(at) = spoiled.at(at) == '0' ? '1' : '0';
    const auto found = replay_log(LVR_ORIGINAL_DIR, spoiled);
    REQUIRE(found);
    CHECK(*found == std::optional<std::uint32_t>{15});
}
