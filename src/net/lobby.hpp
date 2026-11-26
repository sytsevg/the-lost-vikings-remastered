#pragma once

// The lobby's side of a game online (ADR-0012): where everyone waits before a game, over the connection. The host
// gives each guest who says hello a place (or turns him away), tells everyone now and then who is there, their round
// trips and what the game starts with (net::LobbyStatus), counts down when asked and starts the game (net::Start).
// A guest follows. One who says hello while a game is under way is the game's (net::Match), not the lobby's.

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "net/connection.hpp"
#include "net/protocol.hpp"

namespace lvr::net {

// A game online as it starts for this machine: from the lobby at step 0, or (a guest joining under way) from the
// game's state the host gave, at its step.
struct Started {
    int player = 0;   // this machine's
    int players = 1;  // the places taken at the start (joining under way: every place, those not there away)
    int delay = 0;    // steps of input delay
    int slot = 0;     // the scene it starts in (from the lobby; under way the state has it)
    bool cheat = false;
    std::vector<std::string> names;
    // Joining under way: the game's state to start from, its step, the step he arrives at, and the comings and
    // goings of the others the host has from that step on. Empty: from the lobby.
    std::vector<std::uint8_t> state;
    std::uint32_t first_step = 0;
    std::uint32_t arrives = 0;
    std::vector<Presence> presence;
};

class Lobby {
  public:
    Lobby() = default;
    Lobby(const Lobby&) = delete;
    Lobby& operator=(const Lobby&) = delete;
    Lobby(Lobby&&) = delete;
    Lobby& operator=(Lobby&&) = delete;
    virtual ~Lobby() = default;

    // A pass: what came, and what is due. Once the game starts: what it is (what comes after the word to start is
    // left for the game). An error: refused, or the host gone.
    virtual std::expected<std::optional<Started>, std::string> poll() = 0;
    // A guest: the host has given him a place (until then he is only connected). The host: always.
    virtual bool welcomed() const noexcept = 0;
    // The scene the game starts in: the host's choice, as a guest has it from the host.
    virtual int slot() const noexcept = 0;
    // The countdown's seconds left (3, 2, 1; 0: none), the host's, as a guest has it.
    virtual int countdown() const = 0;
    // The host's (a guest's: nothing): the scene to start in; START, after a countdown everyone is told of; and its
    // cancelling.
    virtual void set_slot(int slot) = 0;
    virtual void start() = 0;
    virtual void cancel_start() = 0;
};

// The host's lobby on `connection` (which it keeps until it is dropped), the game to start in `slot`. Those there
// already (back from a game) are told their places again, closed up.
std::unique_ptr<Lobby> host_lobby(Connection& connection, int slot, bool cheat);
// A guest's: one connecting says hello first; one back from a game has his place (and the scene he last heard of).
std::unique_ptr<Lobby> guest_lobby(Connection& connection, bool back, int slot = 0);

}  // namespace lvr::net
