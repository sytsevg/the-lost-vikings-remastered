// Tests for src/lvr/online.* (co-op online, ADR-0012): a host and a client in one process over 127.0.0.1, each
// with its own session, through the lobby and into level 1; they play the same game.
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <thread>

#include "lvr/coop.hpp"
#include "lvr/names.hpp"
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

namespace {

// One machine of a game online in a test: its connection, its session, the steps it has run.
struct Machine {
    std::unique_ptr<NetGame> net;
    std::unique_ptr<Session> session;
    std::uint32_t steps = 0;
    int delay = 0;
};

// Runs the machines until each has run `until` steps (each gives what `held` says for his player).
void run_until(std::vector<Machine*> machines, std::uint32_t until) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{20};  // (a guard against hanging)
    const auto behind = [&] {
        return std::ranges::any_of(machines, [until](Machine* one) { return one->steps < until; });
    };
    while (behind() && std::chrono::steady_clock::now() < end) {
        for (Machine* one : machines) {
            REQUIRE(one->net->poll());
            if (one->steps >= until) {
                continue;
            }
            if (one->net->wants_input()) {
                one->net->give(one->session->take_input(
                    held(one->net->player(), one->steps + static_cast<std::uint32_t>(one->delay)), {}, {}));
            }
            if (one->net->step()) {
                ++one->steps;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    for (Machine* one : machines) {
        UNSCOPED_INFO("player " << one->net->player() << ": " << one->steps << " steps, waiting for "
                                << one->net->waiting_for().value_or("nobody"));
    }
    REQUIRE_FALSE(behind());
}

void settle(const std::vector<Machine*>& machines) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds{500};
    while (std::chrono::steady_clock::now() < until) {
        for (Machine* one : machines) {
            REQUIRE(one->net->poll());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
}

// Joins the game at `port` while it is under way: what the host gave, taken (the others go on meanwhile).
Machine join_under_way(std::uint16_t port, const std::string& name, const std::vector<Machine*>& playing) {
    auto client = NetGame::join("127.0.0.1", port, {.build = "test", .game = 1, .name = name});
    REQUIRE(client);
    std::optional<NetGame::Joined> joined;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (!joined && std::chrono::steady_clock::now() < until) {
        for (Machine* one : playing) {
            REQUIRE(one->net->poll());
        }
        auto lobby = (*client)->lobby();
        REQUIRE(lobby);
        joined = *lobby;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    REQUIRE(joined);
    REQUIRE_FALSE(joined->state.empty());
    auto opened = Session::open(LVR_ORIGINAL_DIR, joined->slot, {}, Rules::kRemaster, draw,
                                Online{.on = true, .players = joined->players}, joined->player);
    REQUIRE(opened);
    REQUIRE((*opened)->take_state(joined->state));
    Machine machine{
        .net = std::move(*client), .session = std::move(*opened), .steps = joined->first_step, .delay = joined->delay};
    machine.net->begin(*machine.session);
    return machine;
}

}  // namespace

TEST_CASE("real data: one joins the game under way, and one who left comes back", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    const NetGame::Identity identity{.build = "test", .game = 1, .name = "host"};
    Machine host;
    std::uint16_t port = kFirstPort + 20;
    for (; port < kFirstPort + 40 && !host.net; ++port) {
        if (auto opened = NetGame::host(port, 2, 0, false, std::nullopt, identity)) {
            host.net = std::move(*opened);
        }
    }
    REQUIRE(host.net);
    --port;
    Machine guest;
    auto client = NetGame::join("127.0.0.1", port, {.build = "test", .game = 1, .name = "guest"});
    REQUIRE(client);
    guest.net = std::move(*client);
    std::optional<NetGame::Joined> at_host;
    std::optional<NetGame::Joined> at_guest;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while ((!at_host || !at_guest) && std::chrono::steady_clock::now() < until) {
        if (!at_host) {
            at_host = host.net->lobby().value_or(std::nullopt);
        }
        if (!at_guest) {
            at_guest = guest.net->lobby().value_or(std::nullopt);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    REQUIRE(at_host);
    REQUIRE(at_guest);
    for (Machine* one : {&host, &guest}) {
        auto opened = Session::open(LVR_ORIGINAL_DIR, at_host->slot, {}, Rules::kRemaster, draw,
                                    Online{.on = true, .players = 2}, one->net->player());
        REQUIRE(opened);
        one->session = std::move(*opened);
        one->delay = at_host->delay;
        one->net->begin(*one->session);
    }
    CHECK_FALSE(host.session->world().players[2].here);
    run_until({&host, &guest}, 60);

    // A third comes into the free place: Olaf, whom nobody played.
    Machine late = join_under_way(port, "late", {&host, &guest});
    CHECK(late.net->player() == 2);
    CHECK(late.steps <= 60);
    run_until({&host, &guest, &late}, 110);
    settle({&host, &guest, &late});
    CHECK(guest.session->state_hash() == host.session->state_hash());
    CHECK(late.session->state_hash() == host.session->state_hash());
    CHECK(late.session->world().players[2].here);
    CHECK(late.session->world().players[2].viking == 2);
    CHECK(host.net->names().at(2) == "LATE");
    CHECK(guest.net->names().at(2) == "LATE");

    // The guest leaves (Baleog nobody's), and someone comes back into his place: Baleog again.
    guest.net->quit();
    guest.net.reset();
    run_until({&host, &late}, 140);
    Machine back = join_under_way(port, "back", {&host, &late});
    CHECK(back.net->player() == 1);
    run_until({&host, &late, &back}, 200);
    settle({&host, &late, &back});
    CHECK(late.session->state_hash() == host.session->state_hash());
    CHECK(back.session->state_hash() == host.session->state_hash());
    CHECK(back.session->world().players[1].viking == 1);
    CHECK(host.net->names().at(1) == "BACK");

    // The host's log has it all, from step 0: played again, the same states.
    const auto replayed = replay_log(LVR_ORIGINAL_DIR, host.net->log());
    REQUIRE(replayed);
    CHECK_FALSE(replayed->has_value());
    // A late one's, from the state he was given, can't be.
    CHECK_FALSE(replay_log(LVR_ORIGINAL_DIR, late.net->log()).has_value());
}

TEST_CASE("real data: a lobby the host starts (a new game: the intro, the same on both); back to it, and another game",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    const NetGame::Identity identity{.build = "test", .game = 1, .name = "host"};
    Machine host;
    std::uint16_t port = kFirstPort + 40;
    for (; port < kFirstPort + 60 && !host.net; ++port) {
        if (auto opened =
                NetGame::host(port, 0, scene::kIntro, false, std::nullopt, identity)) {  // (its word starts it)
            host.net = std::move(*opened);
        }
    }
    REQUIRE(host.net);
    --port;
    Machine guest;
    auto client = NetGame::join("127.0.0.1", port, {.build = "test", .game = 1, .name = "guest"});
    REQUIRE(client);
    guest.net = std::move(*client);
    // The lobby: until the guest is in it and has the host's word on the start and his own round trip; nothing starts
    // meanwhile.
    const auto in_lobby = [&](const auto& until) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        std::optional<NetGame::Joined> at_host;
        std::optional<NetGame::Joined> at_guest;
        while (!until(at_host, at_guest) && std::chrono::steady_clock::now() < end) {
            if (!at_host) {
                auto lobby = host.net->lobby();
                REQUIRE(lobby);
                at_host = *lobby;
            }
            if (!at_guest) {
                auto lobby = guest.net->lobby();
                REQUIRE(lobby);
                at_guest = *lobby;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        REQUIRE(until(at_host, at_guest));
        return std::pair{at_host, at_guest};
    };
    in_lobby([&](const auto& at_host, const auto& at_guest) {
        return guest.net->welcomed() && guest.net->slot() == scene::kIntro && guest.net->network_ms(1).has_value() &&
               !at_host && !at_guest;
    });
    CHECK(guest.net->names() == std::vector<std::string>{"HOST", "GUEST"});
    CHECK(guest.net->player() == 1);

    // Started by the host's word: a new game, the intro played from its recording on both.
    const auto start = [&](int slot) {
        host.net->start();
        const auto [at_host, at_guest] =
            in_lobby([](const auto& at_host, const auto& at_guest) { return at_host && at_guest; });
        REQUIRE(at_guest->slot == slot);
        for (Machine* one : {&host, &guest}) {
            auto opened = Session::open(LVR_ORIGINAL_DIR, slot, {}, Rules::kRemaster, draw,
                                        Online{.on = true, .players = 2}, one->net->player());
            REQUIRE(opened);
            one->session = std::move(*opened);
            one->steps = 0;
            one->delay = at_host->delay;
            one->net->begin(*one->session);
        }
    };
    start(scene::kIntro);
    CHECK(host.session->demo());
    run_until({&host, &guest}, 60);
    settle({&host, &guest});
    CHECK(guest.session->state_hash() == host.session->state_hash());
    CHECK(guest.session->scene() == scene::kIntro);

    // Back to the lobby: the guest is told, and both are there again, the same two.
    host.net->back_to_lobby();
    const auto told = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (!guest.net->back_asked() && std::chrono::steady_clock::now() < told) {
        REQUIRE(guest.net->poll());
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    REQUIRE(guest.net->back_asked());
    guest.net->to_lobby();
    host.net->set_slot(0);  // level 1 this time
    in_lobby([&](const auto& /*at_host*/, const auto& /*at_guest*/) { return guest.net->slot() == 0; });
    CHECK(guest.net->names() == std::vector<std::string>{"HOST", "GUEST"});

    // Another game, from its step 0: the same on both (what was late of the one before not taken).
    start(0);
    run_until({&host, &guest}, 120);
    settle({&host, &guest});
    CHECK(guest.session->scene() == 0);
    CHECK(guest.session->state_hash() == host.session->state_hash());
}
