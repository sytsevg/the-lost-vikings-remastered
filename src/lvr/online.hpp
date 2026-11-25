#pragma once

// Co-op online's front end (ADR-0012): the transport, the lobby, and rollback over the session, as the game's loop
// asks for them, a refresh at a time. The host is player 0 and in the middle: each client talks to the host, which
// passes every player's inputs on to the others. Joining is checked: the same protocol, the same version of the
// game's state, the same build, the same game files.

#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "lvr/coop.hpp"
#include "lvr/session.hpp"
#include "net/protocol.hpp"
#include "net/rollback.hpp"
#include "net/transport.hpp"

namespace lvr {

class NetGame {
  public:
    // What every machine starts the game with (net::Start), and which player this one is.
    struct Joined {
        int player = 0;
        int players = 1;
        int delay = 0;
        int slot = 0;
        bool cheat = false;
        std::vector<std::string> names;
    };
    // Who this build and these game files are, as a client says in its hello.
    struct Identity {
        std::string build;
        std::uint64_t game = 0;
        std::string name;
    };

    // Hosts on `port`; the game starts once `players` are there (the host one of them), at `slot`. `delay`: the input
    // delay in steps; none: chosen from the round trips (under 50 ms none, under 150 one, else two).
    static std::expected<std::unique_ptr<NetGame>, std::string> host(std::uint16_t port, int players, int slot,
                                                                     bool cheat, std::optional<int> delay,
                                                                     Identity identity);
    // Joins the game hosted at `address`.
    static std::expected<std::unique_ptr<NetGame>, std::string> join(const std::string& address, std::uint16_t port,
                                                                     Identity identity);

    // The lobby, a poll a call: once the game starts, what it is. An error: refused, or the host gone.
    std::expected<std::optional<Joined>, std::string> lobby();
    // Who has joined so far, by player (the lobby's).
    const std::vector<std::string>& names() const noexcept { return names_; }

    // The game: rollback over `session` (made for the Joined game), from step 0.
    void begin(Session& session);
    // Each refresh: what came, and what is due to be sent. An error ends the game: the host gone, out of step.
    std::expected<void, std::string> poll();
    // What is due to be sent, now: right after a step took this machine's input, so it is on its way at once
    // rather than at the next refresh's poll.
    std::expected<void, std::string> flush();
    // Whether this machine's input for its next step can be given now (the delay ahead, no more).
    bool wants_input() const;
    void give(const StepInput& input);
    // The next step if it can run now: its pictures. None: this refresh waits (for another player's input, or to
    // let the others catch up).
    std::optional<std::vector<Screen>> step();
    // Since when it has been waiting for a player's input (refreshes), and for whom (his name); none: it isn't.
    std::optional<std::string> waiting_for() const;
    int waited() const noexcept { return waited_; }
    // The round trip to a player, in milliseconds: the network's (answered at once by the transport's thread) and
    // the game's (answered by his game as it next takes what came, once a refresh). A client has the host's to the
    // other clients from the host. None: not measured yet, or this machine's own player.
    std::optional<int> network_ms(int player) const;
    std::optional<int> engine_ms(int player) const;
    int player() const noexcept { return player_; }
    bool hosting() const noexcept { return transport_->hosting(); }

  private:
    NetGame(std::unique_ptr<net::Transport> transport, Identity identity);
    void send(int peer, const net::Message& message, net::Channel channel);
    void broadcast(const net::Message& message, net::Channel channel);
    std::expected<void, std::string> take(int peer, const net::Message& message);
    void leaves(int player);
    int peer_of(int player) const noexcept;  // the transport's number for a player (on a client, the host's: 0)
    int player_of(int peer) const noexcept;

    std::unique_ptr<net::Transport> transport_;
    Identity identity_;
    // The lobby.
    int wanted_ = 1;  // host: how many players start the game
    int slot_ = 0;
    bool cheat_ = false;
    std::optional<int> delay_;
    std::vector<std::string> names_;
    std::vector<int> peers_;  // host: by player, the transport's number of his connection (the host's own: -1)
    int player_ = 0;
    std::optional<Joined> joined_;
    // The game.
    std::unique_ptr<CoopGame> game_;
    std::unique_ptr<net::Rollback> rollback_;
    // By peer, by player: how far that peer has acknowledged the player's inputs (he knows them before that step).
    std::vector<std::array<std::uint32_t, net::kMostPlayers>> acked_;
    std::uint32_t hash_sent_ = 7;  // the next step whose hash is still to be sent
    std::vector<bool> gone_;       // by player
    int waited_ = 0;
    int delay_steps_ = 0;  // the game's input delay
    // The game's round trips: this machine's, by player; and on a client the host's, as his Status says.
    std::vector<std::optional<int>> engine_;
    std::array<std::uint16_t, net::kMostPlayers> host_network_{net::kUnknown, net::kUnknown, net::kUnknown};
    std::array<std::uint16_t, net::kMostPlayers> host_engine_{net::kUnknown, net::kUnknown, net::kUnknown};
    std::uint32_t echoed_at_ = 0;  // when the last echoes went out
};

}  // namespace lvr
