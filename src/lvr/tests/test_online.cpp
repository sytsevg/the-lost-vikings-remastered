// Tests for src/lvr/online.* (co-op online, ADR-0012): a host and a client in one process, each with its own session,
// through the lobby and into level 1; they play the same game. Each test runs twice: over 127.0.0.1 through SDL3_net,
// and on the simulated network (what NetGame does mustn't depend on which).
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <thread>

#include "lvr/coop.hpp"
#include "lvr/names.hpp"
#include "lvr/online.hpp"
#include "lvr/session.hpp"
#include "net/simulated.hpp"
#include "net/transport.hpp"

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

// Where a test's machines meet: 127.0.0.1 through SDL3_net, or the simulated network (a little late, now and then a
// fast message lost). Its clock: the real one's milliseconds, or the simulated one's, which a pass moves on.
struct Wire {
    bool simulated = false;
    net::SimulatedNetwork network{net::SimulatedNetwork::Conditions{.delay_ms = 2, .jitter_ms = 2, .loss_percent = 10}};
    std::uint16_t port = 0;

    std::unique_ptr<NetGame> host(std::uint16_t first_port, int players, int slot, const NetGame::Identity& identity) {
        if (simulated) {
            return NetGame::host(network.host(), players, slot, false, std::nullopt, identity);
        }
        for (port = first_port; port < first_port + 20; ++port) {
            if (auto opened = net::SdlTransport::host(port)) {
                return NetGame::host(std::move(*opened), players, slot, false, std::nullopt, identity);
            }
        }
        return nullptr;
    }
    std::unique_ptr<NetGame> join(const NetGame::Identity& identity) {
        if (simulated) {
            return NetGame::join(network.join(), identity);
        }
        auto joined = net::SdlTransport::join("127.0.0.1", port);
        return joined ? NetGame::join(std::move(*joined), identity) : nullptr;
    }
    void pass(std::uint32_t ms) {
        if (simulated) {
            network.advance(ms);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds{ms});
        }
    }
    std::uint64_t now() const {
        if (simulated) {
            return network.now();
        }
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }
};

// A test's wire, once on each.
Wire each_wire() {
    Wire wire;
    wire.simulated = GENERATE(false, true);
    UNSCOPED_INFO((wire.simulated ? "the simulated network" : "SDL3_net over 127.0.0.1"));
    return wire;
}

}  // namespace

TEST_CASE("real data: a host and a client play level 1 online, the same game", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    Wire wire = each_wire();
    const NetGame::Identity identity{.build = "test", .game = 1, .name = "host"};
    std::unique_ptr<NetGame> host = wire.host(kFirstPort, 2, 0, identity);
    REQUIRE(host);
    // Another build is turned away.
    auto other = wire.join({.build = "other", .game = 1, .name = "x"});
    REQUIRE(other);
    auto client = wire.join({.build = "test", .game = 1, .name = "guest"});
    REQUIRE(client);
    std::optional<NetGame::Joined> at_host;
    std::optional<NetGame::Joined> at_client;
    bool refused = false;
    const auto until = wire.now() + 5'000;
    while ((!at_host || !at_client || !refused) && wire.now() < until) {
        if (!at_host) {
            auto lobby = host->lobby();
            REQUIRE(lobby);
            at_host = *lobby;
        }
        if (!at_client) {
            auto lobby = client->lobby();
            REQUIRE(lobby);
            at_client = *lobby;
        }
        if (!refused) {
            refused = !other->lobby().has_value();
        }
        wire.pass(2);
    }
    REQUIRE(at_host);
    REQUIRE(at_client);
    CHECK(refused);
    CHECK(at_client->player == 1);
    CHECK(at_host->names == std::vector<std::string>{"HOST", "GUEST"});
    CHECK(at_client->names == at_host->names);
    CHECK(at_client->delay == at_host->delay);

    std::vector<std::unique_ptr<Session>> sessions;
    std::vector<NetGame*> games{host.get(), client.get()};
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
    const auto end = wire.now() + 300'000;
    while ((steps[0] < kSteps || steps[1] < kSteps) && wire.now() < end) {
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
        wire.pass(1);
    }
    REQUIRE(steps[0] == kSteps);
    REQUIRE(steps[1] == kSteps);
    // The last inputs on their way, and any step they put right run again.
    const auto settle = wire.now() + 500;
    while (wire.now() < settle) {
        for (NetGame* game : games) {
            REQUIRE(game->poll());
        }
        wire.pass(2);
    }
    CHECK(sessions[0]->state_hash() == sessions[1]->state_hash());
    // The round trips to the host, as the host measures them, the same on both machines; the host's own: none.
    CHECK(host->network_ms(1).has_value());
    CHECK(host->engine_ms(1).has_value());
    CHECK(client->network_ms(1).has_value());
    CHECK(client->engine_ms(1).has_value());
    CHECK_FALSE(host->network_ms(0).has_value());
    CHECK_FALSE(client->network_ms(0).has_value());
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
void run_until(Wire& wire, std::vector<Machine*> machines, std::uint32_t until) {
    const auto end = wire.now() + 20'000;  // (a guard against hanging)
    const auto behind = [&] {
        return std::ranges::any_of(machines, [until](Machine* one) { return one->steps < until; });
    };
    while (behind() && wire.now() < end) {
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
                one->steps = one->net->at_step();  // (one joining under way catches up several at once)
            }
        }
        wire.pass(1);
    }
    for (Machine* one : machines) {
        UNSCOPED_INFO("player " << one->net->player() << ": " << one->steps << " steps, waiting for "
                                << one->net->waiting_for().value_or("nobody"));
    }
    REQUIRE_FALSE(behind());
}

void settle(Wire& wire, const std::vector<Machine*>& machines) {
    const auto until = wire.now() + 500;
    while (wire.now() < until) {
        for (Machine* one : machines) {
            REQUIRE(one->net->poll());
        }
        wire.pass(2);
    }
}

// Joins the game on the wire while it is under way: what the host gave, taken (the others go on meanwhile).
Machine join_under_way(Wire& wire, const std::string& name, const std::vector<Machine*>& playing) {
    auto client = wire.join({.build = "test", .game = 1, .name = name});
    REQUIRE(client);
    std::optional<NetGame::Joined> joined;
    const auto until = wire.now() + 10'000;
    while (!joined && wire.now() < until) {
        for (Machine* one : playing) {
            REQUIRE(one->net->poll());
        }
        auto lobby = client->lobby();
        REQUIRE(lobby);
        joined = *lobby;
        wire.pass(1);
    }
    REQUIRE(joined);
    REQUIRE_FALSE(joined->state.empty());
    auto opened = Session::open(LVR_ORIGINAL_DIR, joined->slot, {}, Rules::kRemaster, draw,
                                Online{.on = true, .players = joined->players}, joined->player);
    REQUIRE(opened);
    REQUIRE((*opened)->take_state(joined->state));
    Machine machine{
        .net = std::move(client), .session = std::move(*opened), .steps = joined->first_step, .delay = joined->delay};
    machine.net->begin(*machine.session);
    return machine;
}

}  // namespace

TEST_CASE("real data: one joins the game under way, and one who left comes back", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    Wire wire = each_wire();
    const NetGame::Identity identity{.build = "test", .game = 1, .name = "host"};
    Machine host;
    host.net = wire.host(kFirstPort + 20, 2, 0, identity);
    REQUIRE(host.net);
    Machine guest;
    guest.net = wire.join({.build = "test", .game = 1, .name = "guest"});
    REQUIRE(guest.net);
    std::optional<NetGame::Joined> at_host;
    std::optional<NetGame::Joined> at_guest;
    const auto until = wire.now() + 5'000;
    while ((!at_host || !at_guest) && wire.now() < until) {
        if (!at_host) {
            at_host = host.net->lobby().value_or(std::nullopt);
        }
        if (!at_guest) {
            at_guest = guest.net->lobby().value_or(std::nullopt);
        }
        wire.pass(2);
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
    run_until(wire, {&host, &guest}, 60);

    // A third comes into the free place: Olaf, whom nobody played.
    Machine late = join_under_way(wire, "late", {&host, &guest});
    CHECK(late.net->player() == 2);
    CHECK(late.steps <= 60);
    run_until(wire, {&host, &guest, &late}, 110);
    settle(wire, {&host, &guest, &late});
    CHECK(guest.session->state_hash() == host.session->state_hash());
    CHECK(late.session->state_hash() == host.session->state_hash());
    CHECK(late.session->world().players[2].here);
    CHECK(late.session->world().players[2].viking == 2);
    CHECK(host.net->names().at(2) == "LATE");
    CHECK(guest.net->names().at(2) == "LATE");

    // The guest leaves (Baleog nobody's), and someone comes back into his place: Baleog again.
    guest.net->quit();
    guest.net.reset();
    run_until(wire, {&host, &late}, 140);
    Machine back = join_under_way(wire, "back", {&host, &late});
    CHECK(back.net->player() == 1);
    run_until(wire, {&host, &late, &back}, 200);
    settle(wire, {&host, &late, &back});
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
    Wire wire = each_wire();
    const NetGame::Identity identity{.build = "test", .game = 1, .name = "host"};
    Machine host;
    host.net = wire.host(kFirstPort + 40, 0, scene::kIntro, identity);  // (its word starts it)
    REQUIRE(host.net);
    Machine guest;
    guest.net = wire.join({.build = "test", .game = 1, .name = "guest"});
    REQUIRE(guest.net);
    // The lobby: until the guest is in it and has the host's word on the start and his own round trip; nothing starts
    // meanwhile.
    const auto in_lobby = [&](const auto& until) {
        const auto end = wire.now() + 10'000;
        std::optional<NetGame::Joined> at_host;
        std::optional<NetGame::Joined> at_guest;
        while (!until(at_host, at_guest) && wire.now() < end) {
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
            wire.pass(2);
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

    // The host's START counts down, everyone told; cancelled, it doesn't start.
    host.net->start();
    in_lobby([&](const auto& at_host, const auto& at_guest) {
        return guest.net->countdown() == 3 && !at_host && !at_guest;
    });
    host.net->cancel_start();
    in_lobby([&](const auto& at_host, const auto& at_guest) {
        return guest.net->countdown() == 0 && !at_host && !at_guest;
    });

    // Started by the host's word, after the countdown: a new game, the intro played from its recording on both.
    const auto start = [&](int slot) {
        host.net->start();
        bool counted = false;
        const auto [at_host, at_guest] = in_lobby([&](const auto& at_host, const auto& at_guest) {
            counted = counted || guest.net->countdown() == 1;  // (3, 2, 1, as the guest sees it)
            return at_host && at_guest;
        });
        CHECK(counted);
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
    run_until(wire, {&host, &guest}, 60);
    settle(wire, {&host, &guest});
    CHECK(guest.session->state_hash() == host.session->state_hash());
    CHECK(guest.session->scene() == scene::kIntro);

    // Back to the lobby: the guest is told, and both are there again, the same two.
    host.net->back_to_lobby();
    const auto told = wire.now() + 10'000;
    while (!guest.net->back_asked() && wire.now() < told) {
        REQUIRE(guest.net->poll());
        wire.pass(2);
    }
    REQUIRE(guest.net->back_asked());
    guest.net->to_lobby();
    host.net->set_slot(0);  // level 1 this time
    in_lobby([&](const auto& /*at_host*/, const auto& /*at_guest*/) { return guest.net->slot() == 0; });
    CHECK(guest.net->names() == std::vector<std::string>{"HOST", "GUEST"});

    // Another game, from its step 0: the same on both (what was late of the one before not taken).
    start(0);
    run_until(wire, {&host, &guest}, 120);
    settle(wire, {&host, &guest});
    CHECK(guest.session->scene() == 0);
    CHECK(guest.session->state_hash() == host.session->state_hash());
}

TEST_CASE("real data: two join the game under way one right after the other: every machine the same game",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    Wire wire = each_wire();
    const NetGame::Identity identity{.build = "test", .game = 1, .name = "host"};
    Machine host;
    host.net = wire.host(kFirstPort + 60, 1, 0, identity);  // (alone: it starts at once)
    REQUIRE(host.net);
    auto started = host.net->lobby();
    REQUIRE(started);
    REQUIRE(started->has_value());
    auto opened = Session::open(LVR_ORIGINAL_DIR, 0, {}, Rules::kRemaster, draw, Online{.on = true, .players = 1}, 0);
    REQUIRE(opened);
    host.session = std::move(*opened);
    host.delay = (*started)->delay;
    host.net->begin(*host.session);
    run_until(wire, {&host}, 40);
    // The second's hello before the first has arrived (his arrival is 16 steps on, and nobody steps meanwhile): he
    // is told of the first's arrival to come with the game's state.
    Machine first = join_under_way(wire, "first", {&host});
    Machine second = join_under_way(wire, "second", {&host, &first});
    CHECK(first.net->player() == 1);
    CHECK(second.net->player() == 2);
    run_until(wire, {&host, &first, &second}, 140);
    settle(wire, {&host, &first, &second});
    CHECK(first.session->state_hash() == host.session->state_hash());
    CHECK(second.session->state_hash() == host.session->state_hash());
    CHECK(second.session->world().players[1].here);
}
