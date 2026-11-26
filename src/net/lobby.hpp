#pragma once

// The lobby's side of a game online (ADR-0012): where everyone waits before a game, over the connection. The host
// gives each guest who says hello a place (or turns him away), tells everyone now and then who is there, their round
// trips and what the game starts with (net::LobbyStatus), counts down when asked and starts the game (net::Start).
// A guest follows. One who says hello while a game is under way is the game's (net::Match), not the lobby's.

#include <cstdint>
#include <expected>
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

// The host's lobby on `connection` (which it keeps until it is dropped), the game to start in `slot`. Those there
// already (back from a game) are told their places again, closed up.
class HostLobby {
  public:
    HostLobby(Connection& connection, int slot, bool cheat);

    // A pass: what came, and what is due. Once the game starts: what it is (what comes after the word to start is
    // left for the game). An error: the host's connection broken.
    std::expected<std::optional<Started>, std::string> poll();
    // The scene the game starts in, and the countdown's seconds left (3, 2, 1; 0: none).
    int slot() const noexcept;
    int countdown() const;
    // The scene to start in; START, after a countdown everyone is told of; and its cancelling.
    void set_slot(int slot);
    void start();
    void cancel_start();

  private:
    void welcome(int peer, const Hello& hello);
    void send_status();

    Connection* connection_;
    std::optional<std::uint32_t> status_sent_at_;
    std::optional<std::uint32_t> counting_until_;  // when the countdown ends
    int slot_ = 0;
    int countdown_sent_ = 0;  // the seconds everyone was last told
    bool cheat_ = false;
    bool start_wanted_ = false;
};

// A guest's lobby: one connecting says hello first; one back from a game has his place (and the scene he last heard
// of). He follows the host: who is there, the start, the countdown.
class GuestLobby {
  public:
    GuestLobby(Connection& connection, bool back, int slot = 0);

    // A pass: what came. Once the game starts (or, joining, the game under way): what it is. An error: refused, or
    // the host gone.
    std::expected<std::optional<Started>, std::string> poll();
    // The host has given him a place (until then he is only connected).
    bool welcomed() const noexcept;
    // The scene the game starts in, and the countdown's seconds left, as the host says.
    int slot() const noexcept;
    int countdown() const;

  private:
    Connection* connection_;
    int slot_ = 0;
    int countdown_ = 0;
    bool welcomed_ = false;
};

}  // namespace lvr::net
