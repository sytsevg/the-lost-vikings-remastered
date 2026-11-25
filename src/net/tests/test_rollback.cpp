// Tests for src/net/rollback.*: GGPO's way of keeping machines in step (ADR-0012), on a toy game over a simulated
// network: latency, jitter, lost and reordered packets. Every machine must come to the same state, show every step
// once, and find a machine out of step when one is.
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <random>
#include <span>
#include <utility>
#include <vector>

#include "net/rollback.hpp"

using namespace lvr::net;

namespace {

// A game whose state is all its inputs, in order: any input run otherwise shows in it.
class Toy : public Game {
  public:
    std::uint64_t state = 1;
    std::vector<std::uint64_t> saved;
    std::map<std::uint32_t, int> shown;     // by step: how often it was shown
    std::optional<std::uint32_t> spoil_at;  // this machine goes wrong at that step (out of step)

    void save(std::size_t slot) override {
        if (saved.size() <= slot) {
            saved.resize(slot + 1);
        }
        saved.at(slot) = state;
    }
    void load(std::size_t slot) override { state = saved.at(slot); }
    void advance(std::span<const Input> inputs, std::uint32_t step, bool shown_now) override {
        for (std::size_t player = 0; player < inputs.size(); ++player) {
            state = (state * 1099511628211ULL) + inputs[player].bytes[0] + (player * 7) + 1;
        }
        state ^= step;
        if (spoil_at && *spoil_at == step) {
            ++state;
        }
        if (shown_now) {
            ++shown[step];
        }
    }
    std::uint64_t hash() override { return state; }
};

// What a player holds at a step: a direction changed every few steps, as players do.
Input held(int player, std::uint32_t step) {
    Input input;
    input.bytes[0] = static_cast<std::uint8_t>(((step / 7) + static_cast<std::uint32_t>(player) * 3) % 4);
    return input;
}

struct Packet {
    int deliver_at = 0;
    int from = 0;
    int to = 0;
    std::vector<std::pair<std::uint32_t, Input>> inputs;
    std::uint32_t knows = 0;  // the inputs of `to` the sender has, without a gap: an acknowledgement
    std::uint32_t step_there = 0;
    int advantage = 0;
    std::vector<std::pair<std::uint32_t, std::uint64_t>> hashes;
};

struct Network {
    int latency = 0;                 // in refreshes (a step is three)
    int jitter = 0;                  // up to this many more
    int loss = 0;                    // percent of packets lost
    std::minstd_rand random{12345};  // NOLINT(bugprone-random-generator-seed): the same run every time
};

struct Peer {
    Toy game;
    Rollback rollback;
    std::vector<std::uint32_t> acked;      // by player: how far he has acknowledged this one's inputs
    std::vector<std::uint32_t> hash_sent;  // the next step whose hash is still to be sent
    Peer(int players, int local, int delay)
        : rollback{game, Settings{.players = players, .local = local, .delay = delay}} {
        acked.assign(static_cast<std::size_t>(players), 0);
    }
};

// Peers stepping every three refreshes (one of them a refresh slow now and then), until each has run `steps` and has
// every input for them; what each sends goes out every refresh.
struct Run {
    std::deque<Peer> peers;  // (built in place: each one's rollback points at its game)
    Network network;
    std::vector<Packet> in_flight;
    int stalls = 0;

    Run(int players, int delay, Network with) : network{with} {
        for (int local = 0; local < players; ++local) {
            peers.emplace_back(players, local, delay);
        }
    }

    // True if every machine got there, and every input for it.
    bool play(std::uint32_t steps, int refreshes_most = 20000) {
        for (int now = 0; now < refreshes_most; ++now) {
            deliver(now);
            bool all_done = true;
            for (std::size_t me = 0; me < peers.size(); ++me) {
                Peer& peer = peers[me];
                Rollback& rollback = peer.rollback;
                const bool slow = me == 1 && now % 97 == 0;  // a clock a little behind
                if (now % 3 == 0 && !slow && rollback.step() < steps && !rollback.ahead()) {
                    while (rollback.local_known() <= rollback.step() + 2 &&
                           rollback.add_local(held(static_cast<int>(me), rollback.local_known()))) {
                    }
                    if (!rollback.advance()) {
                        ++stalls;
                    }
                }
                if (rollback.step() < steps || rollback.confirmed() < steps) {
                    all_done = false;
                }
                send(now, static_cast<int>(me));
            }
            if (all_done) {
                for (int later = now + 1; !in_flight.empty() && later < now + 100; ++later) {
                    deliver(later);  // (what is still on the way: the last hashes)
                }
                for (Peer& peer : peers) {
                    peer.rollback.catch_up();  // the last corrections, and the hashes compared
                }
                return true;
            }
        }
        return false;
    }

    void send(int now, int from) {
        Peer& peer = peers[static_cast<std::size_t>(from)];
        Rollback& rollback = peer.rollback;
        if (peer.hash_sent.empty()) {
            peer.hash_sent.assign(peers.size(), 7);
        }
        for (int to = 0; std::cmp_less(to, peers.size()); ++to) {
            if (to == from) {
                continue;
            }
            Packet packet{.deliver_at = 0,
                          .from = from,
                          .to = to,
                          .inputs = rollback.local_from(peer.acked[static_cast<std::size_t>(to)]),
                          .knows = rollback.known(to),
                          .step_there = rollback.step(),
                          .advantage = rollback.advantage(to),
                          .hashes = {}};
            std::uint32_t& next_hash = peer.hash_sent[static_cast<std::size_t>(to)];
            while (const std::optional<std::uint64_t> hash = rollback.hash_at(next_hash)) {
                packet.hashes.emplace_back(next_hash, *hash);
                next_hash += 8;
            }
            if (std::cmp_less(network.random() % 100, network.loss)) {
                continue;
            }
            const int jitter =
                network.jitter > 0 ? static_cast<int>(network.random() % static_cast<unsigned>(network.jitter + 1)) : 0;
            packet.deliver_at = now + network.latency + jitter;
            in_flight.push_back(std::move(packet));
        }
    }

    void deliver(int now) {
        std::vector<Packet> later;
        for (Packet& packet : in_flight) {
            if (packet.deliver_at > now) {
                later.push_back(std::move(packet));
                continue;
            }
            Peer& peer = peers[static_cast<std::size_t>(packet.to)];
            for (const auto& [step, input] : packet.inputs) {
                peer.rollback.add_remote(packet.from, step, input);
            }
            std::uint32_t& acked = peer.acked[static_cast<std::size_t>(packet.from)];
            acked = std::max(acked, packet.knows);
            peer.rollback.report(packet.from, packet.step_there, packet.advantage);
            for (const auto& [step, hash] : packet.hashes) {
                peer.rollback.add_remote_hash(packet.from, step, hash);
            }
        }
        in_flight = std::move(later);
    }

    bool same_everywhere() const {
        for (const Peer& peer : peers) {
            if (peer.game.state != peers.front().game.state) {
                return false;
            }
        }
        return true;
    }
    bool shown_once(std::uint32_t steps) const {
        for (const Peer& peer : peers) {
            for (std::uint32_t step = 0; step < steps; ++step) {
                const auto found = peer.game.shown.find(step);
                if (found == peer.game.shown.end() || found->second != 1) {
                    return false;
                }
            }
        }
        return true;
    }
};

}  // namespace

TEST_CASE("rollback: alone, every step runs once, with nothing to wait for or run again") {
    Toy game;
    Rollback rollback{game, Settings{.players = 1, .local = 0, .delay = 0}};
    for (std::uint32_t step = 0; step < 50; ++step) {
        REQUIRE(rollback.add_local(held(0, step)));
        REQUIRE(rollback.advance());
    }
    CHECK(rollback.step() == 50);
    CHECK(rollback.confirmed() == 50);
    CHECK(rollback.steps_run_again() == 0);
    CHECK_FALSE(rollback.advance());  // no input for the next yet
}

TEST_CASE("rollback: a remote input that comes otherwise than predicted has the steps from it run again") {
    Toy alone;  // the same inputs, all known: what the state must come to
    Toy game;
    Rollback rollback{game, Settings{.players = 2, .local = 0, .delay = 0}};
    Rollback straight{alone, Settings{.players = 2, .local = 0, .delay = 0}};
    for (std::uint32_t step = 0; step < 6; ++step) {
        REQUIRE(rollback.add_local(held(0, step)));
        REQUIRE(rollback.advance());  // the second player's predicted: his first (none yet), then the same
        REQUIRE(straight.add_local(held(0, step)));
        straight.add_remote(1, step, held(1, 10 + step));
        REQUIRE(straight.advance());
    }
    for (std::uint32_t step = 0; step < 6; ++step) {
        rollback.add_remote(1, step, held(1, 10 + step));
    }
    REQUIRE(rollback.add_local(held(0, 6)));
    straight.add_remote(1, 6, held(1, 16));
    REQUIRE(straight.add_local(held(0, 6)));
    REQUIRE(rollback.advance());
    REQUIRE(straight.advance());
    CHECK(rollback.steps_run_again() > 0);
    CHECK(game.state == alone.state);
    CHECK(game.shown.size() == 7);  // each shown once, the ones run again not again
}

TEST_CASE("rollback: no further than the window ahead of another player's inputs: then it waits") {
    Toy game;
    Rollback rollback{game, Settings{.players = 2, .local = 0, .delay = 0, .window = 8}};
    std::uint32_t ran = 0;
    for (std::uint32_t step = 0; step < 20; ++step) {
        (void)rollback.add_local(held(0, step));
        if (rollback.advance()) {
            ++ran;
        }
    }
    CHECK(ran == 8);
    CHECK(rollback.step() == 8);
    rollback.add_remote(1, 0, held(1, 0));
    CHECK(rollback.advance());  // one more now
}

TEST_CASE("rollback: the input delay: the local input given now is for that many steps on") {
    Toy game;
    Rollback rollback{game, Settings{.players = 1, .local = 0, .delay = 2}};
    CHECK(rollback.local_known() == 2);           // the first two steps: nothing
    CHECK(rollback.add_local(held(0, 2)));        // the input given now: for step 2
    CHECK_FALSE(rollback.add_local(held(0, 3)));  // no further ahead than the delay
    CHECK(rollback.advance());
    CHECK(rollback.add_local(held(0, 3)));
}

TEST_CASE("rollback: two machines over a network that loses, delays and reorders come to the same state") {
    for (const int delay : {0, 1, 2}) {
        Run run{2, delay, Network{.latency = 4, .jitter = 3, .loss = 10}};
        REQUIRE(run.play(300));
        Toy truth;  // every input known from the start: what they must come to
        for (std::uint32_t step = 0; step < 300; ++step) {
            const bool before = std::cmp_less(step, delay);  // (the first steps of the delay: nothing)
            const std::vector<Input> both{before ? Input{} : held(0, step), before ? Input{} : held(1, step)};
            truth.advance(both, step, false);
        }
        INFO("delay " << delay);
        CHECK(run.same_everywhere());
        CHECK(run.peers.front().game.state == truth.state);
        CHECK(run.shown_once(300));
        for (const Peer& peer : run.peers) {
            CHECK(peer.rollback.confirmed() >= 300);
            CHECK_FALSE(peer.rollback.out_of_step());
        }
    }
}

TEST_CASE("rollback: three machines, a long way apart (300 ms), come to the same state") {
    Run run{3, 1, Network{.latency = 9, .jitter = 4, .loss = 10}};
    REQUIRE(run.play(300));
    CHECK(run.same_everywhere());
    CHECK(run.shown_once(300));
    for (const Peer& peer : run.peers) {
        CHECK_FALSE(peer.rollback.out_of_step());
        CHECK(peer.rollback.steps_run_again() > 0);  // they were wrong now and then, and put it right
    }
}

TEST_CASE("rollback: a machine that goes its own way is found out of step, at the first step compared after it") {
    Run run{2, 1, Network{.latency = 2, .jitter = 1, .loss = 0}};
    run.peers[1].game.spoil_at = 100;
    REQUIRE(run.play(200));
    for (const Peer& peer : run.peers) {
        REQUIRE(peer.rollback.out_of_step());
        CHECK(*peer.rollback.out_of_step() == 103);  // the first 8th step after 100: 103 (the state after it)
    }
}

TEST_CASE("rollback: a player who leaves gives nothing from then on, and nobody waits for him") {
    Toy game;
    Rollback rollback{game, Settings{.players = 2, .local = 0, .delay = 0}};
    for (std::uint32_t step = 0; step < 4; ++step) {
        rollback.add_remote(1, step, held(1, step));
    }
    rollback.leave(1, 4);
    for (std::uint32_t step = 0; step < 30; ++step) {
        REQUIRE(rollback.add_local(held(0, step)));
        REQUIRE(rollback.advance());
    }
    CHECK(rollback.confirmed() == 30);
}

TEST_CASE("rollback: the one ahead of the other on average is told to wait a refresh") {
    Toy game;
    Rollback rollback{game, Settings{.players = 2, .local = 0, .delay = 0}};
    for (std::uint32_t step = 0; step < 10; ++step) {
        (void)rollback.add_local(held(0, step));
        rollback.add_remote(1, step, held(1, step));
        REQUIRE(rollback.advance());
    }
    for (int i = 0; i < 16; ++i) {
        rollback.report(1, 6, -4);  // he is at step 6 and sees this one 4 ahead of him
    }
    CHECK(rollback.advantage(1) == 4);
    CHECK(rollback.ahead());
    for (int i = 0; i < 16; ++i) {
        rollback.report(1, 10, 0);
    }
    CHECK_FALSE(rollback.ahead());
}

TEST_CASE("rollback: a step the game says is never to be run again waits for every player's input") {
    class Waits : public Toy {
      public:
        bool waits = false;
        bool waits_for_inputs() override { return waits; }
    };
    Waits game;
    Rollback rollback{game, Settings{.players = 2, .local = 0, .delay = 0}};
    REQUIRE(rollback.add_local(held(0, 0)));
    REQUIRE(rollback.advance());  // on a prediction
    game.waits = true;
    REQUIRE(rollback.add_local(held(0, 1)));
    CHECK_FALSE(rollback.advance());  // not on a prediction
    rollback.add_remote(1, 0, held(1, 0));
    rollback.add_remote(1, 1, held(1, 1));
    CHECK(rollback.advance());
}

TEST_CASE("rollback: one who arrives after being away: the steps from then are run again, and wait for his inputs") {
    Toy game;
    Rollback rollback{game, Settings{.players = 2, .local = 0, .delay = 0}};
    rollback.leave(1, 0);  // nobody in the second place yet
    for (std::uint32_t step = 0; step < 8; ++step) {
        REQUIRE(rollback.add_local(held(0, step)));
        REQUIRE(rollback.advance());
    }
    CHECK(rollback.confirmed() == 8);
    CHECK(rollback.steps_run_again() == 0);
    rollback.arrive(1, 5);  // he comes at step 5 (the host says): the steps from 5 were run without him
    CHECK(rollback.there(1, 5));
    CHECK_FALSE(rollback.there(1, 4));
    CHECK(rollback.confirmed() == 5);
    REQUIRE(rollback.add_local(held(0, 8)));
    REQUIRE(rollback.advance());
    CHECK(rollback.steps_run_again() == 3);  // 5, 6, 7
    for (std::uint32_t step = 5; step < 9; ++step) {
        rollback.add_remote(1, step, held(1, step));
    }
    CHECK(rollback.confirmed() == 9);
    // And away again from 12: known from there without his inputs.
    rollback.leave(1, 12);
    for (std::uint32_t step = 9; step < 12; ++step) {
        rollback.add_remote(1, step, held(1, step));
    }
    CHECK(rollback.known(1) > 1000);
}

TEST_CASE("rollback: one joining under way starts at the step of the state he was given") {
    Toy game;
    Rollback rollback{game, Settings{.players = 2, .local = 1, .delay = 1, .first_step = 100}};
    CHECK(rollback.step() == 100);
    CHECK(rollback.local_known() == 101);  // the delay's step: nothing
    rollback.add_remote(0, 100, held(0, 100));
    REQUIRE(rollback.add_local(held(1, 101)));
    CHECK(rollback.advance());
    CHECK(rollback.step() == 101);
    CHECK(rollback.local_from(100).size() == 2);
}
