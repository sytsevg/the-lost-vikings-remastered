#pragma once

// The tests' table for co-op online: machines on one simulated network, each with its connection, its lobby until a
// game starts, the game and the match over it, played a refresh at a time as the game's window loop plays them: each
// polls every refresh, and runs a step every third (every refresh while it waits). What is played is G: the toy game
// (ToyGame) here, the real one in src/lvr's tests; it is made for each game as it starts (G::made, under way at the
// state given) and gives each step's input (G::input, the inputs given so far counted).

#include <algorithm>
#include <catch2/catch_message.hpp>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "net/connection.hpp"
#include "net/lobby.hpp"
#include "net/match.hpp"
#include "net/simulated.hpp"
#include "net/tests/toy.hpp"

namespace lvr::net::test {

inline constexpr std::uint32_t kRefreshMs = 17;

inline Identity identity(std::string name) {
    return Identity{.build = "test", .game = 1, .state_version = 1, .name = std::move(name)};
}

// The toy game at the table: what `held` says, for its player.
struct ToyGame : Toy {
    static std::unique_ptr<ToyGame> made(const Started& started) {
        auto game = std::make_unique<ToyGame>();
        if (!started.state.empty()) {
            game->take(started.state);
        }
        return game;
    }
    static Input input(ToyGame& /*game*/, int player, std::uint32_t given) { return held(player, given); }
};

// One machine of a game online in a test.
template <typename G = ToyGame>
struct Machine {
    std::unique_ptr<Connection> connection;
    std::unique_ptr<HostLobby> hosting;  // the host's lobby, until a game starts
    std::unique_ptr<GuestLobby> lobby;   // a guest's
    std::unique_ptr<G> game;             // the game's, made afresh for each
    std::unique_ptr<Match> match;
    std::optional<Started> started;
    std::uint32_t given = 0;  // the inputs given so far
    int due = 0;              // refreshes until its next step is tried
    std::string error;        // what ended the game (or the lobby) for it
    bool back = false;        // told to go back to the lobby
    bool gone = false;        // it left

    int player() const { return connection->player(); }
    std::uint32_t at_step() const { return match != nullptr ? match->at_step() : 0; }
    // Its hashes of the game's state, by step, as its log has them (what the machines compare).
    std::map<std::uint32_t, std::string> hashes() const {
        std::map<std::uint32_t, std::string> by_step;
        std::istringstream lines{match != nullptr ? match->log() : std::string{}};
        std::string word;
        while (lines >> word) {
            if (word == "hash") {
                std::uint32_t step = 0;
                std::string hash;
                lines >> step >> hash;
                by_step[step] = hash;
            }
        }
        return by_step;
    }
};

template <typename G = ToyGame>
class Table {
  public:
    explicit Table(SimulatedNetwork::Conditions conditions = {}) : network{conditions} {}

    SimulatedNetwork network;
    std::deque<Machine<G>> machines;  // (in place: a match points at its game)

    Machine<G>& host(int slot = 0) {
        Machine<G>& machine = machines.emplace_back();
        machine.connection = std::make_unique<Connection>(network.host(), identity("host"));
        machine.hosting = std::make_unique<HostLobby>(*machine.connection, slot, false);
        return machine;
    }
    Machine<G>& join(const std::string& name, Identity as = identity("")) {
        Machine<G>& machine = machines.emplace_back();
        as.name = name;
        machine.connection = std::make_unique<Connection>(network.join(), std::move(as));
        machine.lobby = std::make_unique<GuestLobby>(*machine.connection, false);
        return machine;
    }
    // Back to the lobby: the host's word to everyone (the host's machine), or a guest's, told.
    void to_lobby(Machine<G>& machine, int slot = 0) {
        if (machine.connection->hosting()) {
            machine.match->back_to_lobby();
        }
        machine.match.reset();
        machine.started.reset();
        machine.back = false;
        if (machine.connection->hosting()) {
            machine.hosting = std::make_unique<HostLobby>(*machine.connection, slot, false);
        } else {
            machine.lobby = std::make_unique<GuestLobby>(*machine.connection, true);
        }
    }
    void leave(Machine<G>& machine) {
        machine.match->quit();
        machine.match.reset();
        machine.lobby.reset();
        machine.hosting.reset();
        machine.connection.reset();
        machine.gone = true;
    }

    // A refresh: each machine there has its turn, and the clock moves on.
    void pass() {
        for (Machine<G>& machine : machines) {
            if (!machine.gone) {
                turn(machine);
            }
        }
        network.advance(kRefreshMs);
        ++refresh_;
    }
    void wait(int refreshes) {
        for (int refresh = 0; refresh < refreshes; ++refresh) {
            pass();
        }
    }
    // Refreshes pass until `done`, at most `most`: whether it was.
    bool until(const std::function<bool()>& done, int most = 20000) {
        for (int refresh = 0; refresh < most && !done(); ++refresh) {
            pass();
        }
        return done();
    }
    // Until every machine there and in its game has run `steps` steps (and nobody's game ended); if not, how each
    // machine is, in the test's report.
    bool run_to(std::uint32_t steps) {
        const bool ran = until([&] {
            return std::ranges::all_of(machines, [&](const Machine<G>& one) {
                return one.gone || (one.match != nullptr && one.error.empty() && one.at_step() >= steps);
            });
        });
        if (!ran) {
            UNSCOPED_INFO(described());
        }
        return ran;
    }
    // Each machine as it is: its player, its step, whom it waits for, what ended its game (for a test's message).
    std::string described() const {
        std::ostringstream text;
        for (const Machine<G>& one : machines) {
            if (one.gone) {
                text << "(gone) ";
                continue;
            }
            text << "player " << one.player() << " at " << one.at_step();
            if (one.match != nullptr) {
                text << " waiting " << one.match->waited() << " for " << one.match->waiting_for().value_or(-1);
            } else {
                text << " in the lobby";
            }
            text << (one.error.empty() ? "" : ": " + one.error) << "; ";
        }
        return text.str();
    }
    // Whether the machines there have the same game: each two compared at the steps both hashed (at least three).
    bool same_game() const {
        for (const Machine<G>& one : machines) {
            for (const Machine<G>& other : machines) {
                if (one.gone || other.gone || &one == &other) {
                    continue;
                }
                const auto mine = one.hashes();
                const auto theirs = other.hashes();
                int compared = 0;
                for (const auto& [step, hash] : mine) {
                    if (const auto found = theirs.find(step); found != theirs.end()) {
                        if (found->second != hash) {
                            return false;
                        }
                        ++compared;
                    }
                }
                if (compared < 3) {
                    return false;
                }
            }
        }
        return true;
    }

  private:
    void turn(Machine<G>& machine) {
        if (machine.match != nullptr) {
            if (!machine.error.empty() || machine.back) {
                return;
            }
            auto polled = machine.match->poll();
            if (!polled) {
                machine.error = polled.error();
                return;
            }
            if (*polled == Match::Polled::kBack) {
                machine.back = true;
                return;
            }
            if (machine.due > 0) {
                --machine.due;
                return;
            }
            if (machine.match->wants_input()) {
                machine.match->give(G::input(*machine.game, machine.player(), machine.given++));
            }
            bool ran = false;
            if (!machine.match->joining() || machine.match->run_to_arrival()) {
                ran = machine.match->step();
            }
            machine.due = ran ? 2 : 0;  // (a step's three refreshes; a refresh while it waits)
            if (auto flushed = machine.match->flush(); !flushed) {
                machine.error = flushed.error();
            }
            return;
        }
        if ((machine.lobby != nullptr || machine.hosting != nullptr) && machine.error.empty()) {
            auto polled = machine.hosting != nullptr ? machine.hosting->poll() : machine.lobby->poll();
            if (!polled) {
                machine.error = polled.error();
            } else if (*polled) {
                machine.started = **polled;
                machine.game = G::made(*machine.started);
                machine.match = std::make_unique<Match>(*machine.connection, *machine.game, *machine.started);
                machine.lobby.reset();
                machine.hosting.reset();
            }
        }
    }

    int refresh_ = 0;
};

}  // namespace lvr::net::test
