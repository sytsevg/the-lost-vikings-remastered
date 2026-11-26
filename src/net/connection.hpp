#pragma once

// A machine's connection to the others of a game online (ADR-0012), through its transport: who is who and their
// names, the round trips, the game's number, and what came, a message at a time. It outlives the games played over
// it: the lobby (net::Lobby) has it, a game (net::Match) has it while it lasts, and the lobby has it back. What one
// of them leaves unread (what came after the word to start a game, or to go back to the lobby) is the other's.
//
// The host is in the middle: a guest talks to the host only. The host's players are numbered from 0 (himself), each
// with his machine's peer number in the transport; a guest's only peer is the host (0).

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "net/protocol.hpp"
#include "net/transport.hpp"

namespace lvr::net {

// Who a machine is, as its hello says: what it must have in common with the host's to join him.
struct Identity {
    std::string build;                // the build's ID (a hash of its sources)
    std::uint64_t game = 0;           // the game files' release
    std::uint32_t state_version = 0;  // of the game's state as it is written out
    std::string name;                 // the player's, as typed
};

// What came: a guest's machine connected (on the host), a message from a machine, or a machine gone.
struct Incoming {
    enum class Kind : std::uint8_t { kJoined, kMessage, kLeft };
    Kind kind = Kind::kMessage;
    int peer = 0;     // its machine, as the transport numbers it
    int player = -1;  // whose machine it is (-1: a guest's that has no place yet)
    Message message;  // (kMessage's)
};

class Connection {
  public:
    Connection(std::unique_ptr<Transport> transport, Identity identity);

    bool hosting() const noexcept { return transport_->hosting(); }
    const Identity& identity() const noexcept { return identity_; }
    std::uint32_t now_ms() const { return transport_->now_ms(); }
    // Why the host turns a hello away (another protocol, another version of the game's state, another build, other
    // game files); none: it is this game's.
    std::optional<std::string> refusal(const Hello& hello) const;
    // This machine's hello, as a guest says it.
    Hello hello() const;

    // What came since the last receive, read: a pass begins with it.
    void receive();
    // What came, a message at a time (a bundle's in its order); none: nothing more this pass. An Echo is answered
    // here and an Echoed taken here (the game's round trip): neither comes out.
    std::optional<Incoming> next();

    // To a player's machine (a guest's: only the host's, player 0); to a machine by its peer number (one that has no
    // place yet: a Welcome, a Refused); and to every other player's that has one. Fast messages go out with the rest
    // of the pass's as one, at flush (the transport takes only the newest).
    void send(int player, const Message& message, Channel channel);
    void send_to_peer(int peer, const Message& message, Channel channel);
    void broadcast(const Message& message, Channel channel);
    void flush();
    // The host lets a machine go.
    void drop_peer(int peer);
    // A guest says he goes, and waits a moment for it to go.
    void quit();
    // Now and then (every half second) an Echo to the others: the game's round trips.
    void echo();

    // Who is who. This machine's player, and the names by player.
    int player() const noexcept { return player_; }
    void set_player(int player) noexcept { player_ = player; }
    const std::vector<std::string>& names() const noexcept { return names_; }
    void set_names(std::vector<std::string> names) { names_ = std::move(names); }
    void set_name(int player, std::string name);
    // Host: each player's machine (none: -1); a guest's: the host's is 0, nobody else's is his.
    int peer_of(int player) const noexcept;
    int player_of(int peer) const noexcept;
    void seat(int player, int peer);
    void unseat(int player) { seat(player, -1); }
    // Host: those still there, in their order, as players 0, 1, ... (their names and round trips with them).
    void close_up();
    int seated() const noexcept { return static_cast<int>(peers_.size()); }  // (places, taken or not)
    // The game's number (net::Start): what a game's messages carry.
    std::uint16_t game() const noexcept { return game_; }
    void set_game(std::uint16_t game) noexcept { game_ = game; }

    // A player's round trip to the host in milliseconds: the network's (the transport's) and the game's (an Echo
    // answered by the other game as it next reads), as the host measures them: the same on every machine (a guest
    // has them from the host, reported). None: the host himself, or not measured yet.
    std::optional<int> network_ms(int player) const;
    std::optional<int> engine_ms(int player) const;
    void reported(const std::array<std::uint16_t, kMostPlayers>& network,
                  const std::array<std::uint16_t, kMostPlayers>& engine);
    void forget_round_trip(int player);
    void forget_reported();
    // The transport's round trip to a machine.
    std::optional<int> link_ms(int peer) const { return transport_->round_trip_ms(peer); }

  private:
    void send_to(int peer, const Message& message, Channel channel);

    std::unique_ptr<Transport> transport_;
    Identity identity_;
    std::deque<Incoming> inbox_;
    std::map<int, std::vector<Message>> outgoing_;  // by peer: what this pass says to him fast
    std::vector<int> peers_{-1};                    // host: by player, his machine (his own: -1)
    std::vector<std::string> names_;
    std::array<std::optional<int>, kMostPlayers> engine_{};  // the game's round trips this machine measured
    std::array<std::uint16_t, kMostPlayers> reported_network_{kUnknown, kUnknown, kUnknown};
    std::array<std::uint16_t, kMostPlayers> reported_engine_{kUnknown, kUnknown, kUnknown};
    std::optional<std::uint32_t> echoed_at_;
    int player_ = 0;
    std::uint16_t game_ = 0;
};

}  // namespace lvr::net
