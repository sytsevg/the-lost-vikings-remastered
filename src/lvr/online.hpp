#pragma once

// Co-op online's front end (ADR-0012): the transport, the lobby, and rollback over the session, as the game's loop
// asks for them, a refresh at a time. The host is player 0 and in the middle: each client talks to the host, which
// passes every player's inputs on to the others. Joining is checked: the same protocol, the same version of the
// game's state, the same build, the same game files.

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
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
        // Joining under way: the game's state to start from (Session::take_state), its step, and the step he arrives
        // at. Empty: the game starts at step 0 from the lobby.
        std::vector<std::uint8_t> state;
        std::uint32_t first_step = 0;
        std::uint32_t arrives = 0;
    };
    // Who this build and these game files are, as a client says in its hello.
    struct Identity {
        std::string build;
        std::uint64_t game = 0;
        std::string name;
    };

    // Hosts on `port`; the game starts once `players` are there (the host one of them), at `slot`; with 0 players
    // when the host says so (start), with whoever is there. `delay`: the input delay in steps; none: chosen from the
    // round trips (under 50 ms none, under 150 one, else two).
    static std::expected<std::unique_ptr<NetGame>, std::string> host(std::uint16_t port, int players, int slot,
                                                                     bool cheat, std::optional<int> delay,
                                                                     Identity identity);
    // Joins the game hosted at `address`.
    static std::expected<std::unique_ptr<NetGame>, std::string> join(const std::string& address, std::uint16_t port,
                                                                     Identity identity);

    // The lobby, a poll a call: once the game starts, what it is. An error: refused, or the host gone. The host tells
    // everyone now and then who is there, their round trips and the start (net::Lobby).
    std::expected<std::optional<Joined>, std::string> lobby();
    // Who has joined so far, by player (the lobby's).
    const std::vector<std::string>& names() const noexcept { return names_; }
    // A client: the host has given him a place in the lobby (until then he is only connected).
    bool welcomed() const noexcept { return hosting() || welcomed_; }
    // The scene the game starts in: the host's choice (set_slot), as a client has it from the lobby.
    int slot() const noexcept { return slot_; }
    void set_slot(int slot) noexcept { slot_ = slot; }
    // Host: the game starts with the next lobby(), with whoever is there.
    void start() noexcept { start_wanted_ = true; }
    // Host, in the game: everyone back to the lobby (net::Back), this machine at once (to_lobby). A client is told
    // in a poll (back_asked; nothing after it is taken until he is in the lobby again).
    void back_to_lobby();
    bool back_asked() const noexcept { return back_; }
    // The game over for this machine, the connections kept: the lobby again (the host's: those still there, in
    // their order, the places left empty closed up).
    void to_lobby();

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
    // A player's round trip to the host, in milliseconds, as the host measures it (the same on every machine: a
    // client has it from the host's Status): the network's (answered at once by the transport's thread) and the
    // game's (answered by the other game as it next takes what came, once a refresh). None: the host himself, or
    // not measured yet.
    std::optional<int> network_ms(int player) const;
    std::optional<int> engine_ms(int player) const;
    int player() const noexcept { return player_; }
    // This machine leaves the game: a client tells the host (and waits a moment for that to go); the host's leaving
    // the others see as the host gone.
    void quit();
    // Where a log is written if the machines go out of step (a folder; none: not written): the game's start, every
    // player's input for every confirmed step, and this machine's hashes, for `lv-replay --coop` to play again.
    void set_log_folder(std::filesystem::path folder) { log_folder_ = std::move(folder); }
    // The log as it would be written now.
    std::string log() const;
    bool hosting() const noexcept { return transport_->hosting(); }

  private:
    NetGame(std::unique_ptr<net::Transport> transport, Identity identity);
    void send(int peer, const net::Message& message, net::Channel channel);
    void broadcast(const net::Message& message, net::Channel channel);
    std::expected<void, std::string> take(int peer, const net::Message& message);
    void leaves(int player);
    // Host: one says hello while the game is under way: a place, the game's state, and the step he arrives at.
    void welcome_under_way(int peer, const net::Hello& hello);
    int peer_of(int player) const noexcept;  // the transport's number for a player (on a client, the host's: 0)
    int player_of(int peer) const noexcept;
    // Host, in the lobby: who is there, their round trips and the start, to everyone (net::Lobby).
    void send_lobby();
    // The lobby's handling of one event: an error ends it.
    std::expected<std::optional<Joined>, std::string> lobby_event(const net::Event& event);

    std::unique_ptr<net::Transport> transport_;
    Identity identity_;
    // The lobby.
    int wanted_ = 1;          // host: how many players start the game ...
    bool auto_start_ = true;  // ... as they are there; else when the host says (start_wanted_)
    bool start_wanted_ = false;
    bool welcomed_ = false;
    std::uint32_t lobby_sent_at_ = 0;  // host: when the lobby was last told
    std::uint16_t game_number_ = 0;    // the game's number (net::Start): what a game's messages carry
    bool back_ = false;                // a client: told to go back to the lobby
    std::vector<net::Event> pending_;  // what came after that, for the lobby
    int slot_ = 0;
    bool cheat_ = false;
    std::optional<int> delay_;
    std::vector<std::string> names_;
    std::vector<int> peers_;  // host: by player, the transport's number of his connection (the host's own: -1)
    int player_ = 0;
    std::optional<Joined> joined_;
    // The game.
    Session* session_ = nullptr;
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
    std::uint32_t echoed_at_ = 0;                                  // when the last echoes went out
    std::vector<std::pair<std::uint32_t, std::uint64_t>> hashes_;  // this machine's, as they were sent
    std::filesystem::path log_folder_;
    bool logged_ = false;
};

}  // namespace lvr
