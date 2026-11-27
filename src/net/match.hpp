#pragma once

// One game online (ADR-0012), over the connection from its start to its end: rollback over the game (net::Game),
// every player's inputs and their acknowledgements, who is there when, the hashes compared, and the game's log. The
// host is in the middle and leads: he passes every player's inputs on to the others, decides who is there when
// (net::Decided), lets one who says hello while the game goes on join it under way, puts a guest out of step right
// with his own state (net::Resync), and lets one go who holds the game up too long. Pure: it knows the game only as
// net::Game, and the network only as the connection.

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "log/log.hpp"
#include "net/connection.hpp"
#include "net/lobby.hpp"
#include "net/rollback.hpp"

namespace lvr::net {

// A step: three refreshes of the original's display (25.175 MHz, 800 × 527 a refresh; lvr::kDisplayHz).
inline constexpr double kStepMs = 3000.0 * 800.0 * 527.0 / 25'175'000.0;

class Match {
  public:
    static constexpr int kWindow = 8;                   // the steps rollback can run ahead of the inputs
    static constexpr std::size_t kSlots = kWindow + 2;  // the game's states kept (Rollback::slots)
    // Held up (no step run) this long: the host lets the one it waits for go; a guest goes if the host's game said
    // nothing meanwhile (it is stuck), and in any case after the longer wait (the host didn't let the one go).
    static constexpr std::uint32_t kHeldMs = 10000;
    static constexpr std::uint32_t kGuestHeldMs = 15000;
    // Held up this long, it is seen: the box says whom the game waits for, and the log says it (once it goes on).
    static constexpr std::uint32_t kNoticedMs = 300;
    // A guest put right from the host's state this often (each a jump): unsteady, and told so.
    static constexpr int kUnsteadyResyncs = 3;
    static constexpr std::uint32_t kUnsteadyMs = 30000;

    // The game `started`, rollback over `game` (whose state, joining under way, is already the one given).
    Match(Connection& connection, Game& game, const Started& started);

    // Each pass of the window: what came; and what is due to be sent, at most once a refresh. A guest told to go back
    // to the lobby: kBack (what comes after that word is the lobby's). An error ends the game for this machine: the
    // host gone or not answering, this one let go by the host, the game held up too long, a state given that can't be
    // taken.
    enum class Polled : std::uint8_t { kGoingOn, kBack };
    std::expected<Polled, std::string> poll();
    // What is due to be sent, now: right after a step took this machine's input, so it is on its way at once.
    std::expected<void, std::string> flush();
    // Whether this machine's input for its next step can be given now (the delay ahead, no more).
    bool wants_input() const;
    void give(const Input& input);
    // One joining under way: the steps before his arrival are still to run. They run at once and unseen (their
    // effects' sounds are to be dropped), as far as the others' inputs have come; true once he is there, his game
    // shown from the next step on.
    bool joining() const noexcept { return rollback_.step() < catch_up_to_; }
    bool run_to_arrival();
    // The next step if it can run now (shown); false: this refresh waits (for another player's input, or to let the
    // others catch up).
    bool step();
    std::uint32_t at_step() const noexcept { return rollback_.step(); }
    // Since when (refreshes) it has waited for a player's input, and for whose (the one known least far); how long
    // (milliseconds) the game has been held up, no step run, if it is.
    int waited() const noexcept { return waited_; }
    std::optional<int> waiting_for() const;
    std::optional<std::uint32_t> held_ms() const;
    // The host: everyone back to the lobby. A guest: he leaves.
    void back_to_lobby();
    void quit();
    // The first step whose state differed from another machine's, if one did this game (the host's word put it right
    // since): what the game's log is written for.
    std::optional<std::uint32_t> out_of_step() const noexcept { return out_of_step_; }
    // How many times the host's state was taken (a guest), or sent to a player (the host); and whether this machine
    // took it kUnsteadyResyncs times in the last kUnsteadyMs (a guest: his game jumps, he may rather leave).
    int resyncs(int player) const { return resyncs_.at(static_cast<std::size_t>(player)); }
    bool unsteady() const;
    // The game's start, every player's input for every confirmed step, and this machine's hashes: what lv-replay
    // --coop plays again (one who joined under way has the steps from the state he was given only).
    std::string log() const;
    // What the game was like, for the log when it ends: its last step, the states the host sent (a guest: took), the
    // times it was held up and the longest, the rollbacks and the deepest. (The round trips are the transport's.)
    std::string summary() const;

  private:
    void take(const Incoming& incoming);
    void take_inputs(int sender, const Inputs& inputs);
    void take_status(int sender, int peer, const Status& status);
    void take_hash(int sender, const Hash& hash);
    // The host's own: one says hello while the game goes on (a free place, the game's state, the step he arrives
    // at); one leaves; who is there decided before his own next step, said to everyone.
    void welcome_under_way(int peer, const Hello& hello);
    void leaves(int player);
    void decide();
    // Out of step: noted (for the log); the host sends each guest who is his state. A guest takes it.
    void put_right();
    std::expected<void, std::string> take_resync(const Resync& resync);
    // Held up: the host lets the one it waits for go; a guest says why he goes, if he does.
    std::optional<std::string> held_up();
    // The game, and the step it is at, as every line of its log begins.
    std::string game_and_step() const;
    // A player as the log names him: his number, and his name (never his machine's address).
    std::string who(int player) const;

    Connection* connection_;
    Started started_;
    Rollback rollback_;
    // By peer player, by player: how far that one has acknowledged the player's inputs (he knows them before it).
    std::array<std::array<std::uint32_t, kMostPlayers>, kMostPlayers> acked_{};
    std::array<bool, kMostPlayers> gone_{};
    std::vector<std::pair<std::uint32_t, std::uint64_t>> hashes_;  // this machine's, as they were sent
    std::uint32_t hash_sent_ = 7;                                  // the next step whose hash is still to be sent
    std::uint32_t catch_up_to_ = 0;  // joining under way: the steps before his arrival, run unseen at once
    int waited_ = 0;
    std::optional<std::uint32_t> sent_at_;  // when what was due last went (the connection's clock)
    std::optional<std::uint32_t> out_of_step_;
    std::array<int, kMostPlayers> resyncs_{};             // by player: the host's states sent (a guest: his own, taken)
    std::optional<std::uint32_t> held_since_;             // since when no step has run (the connection's clock)
    std::uint32_t heard_host_ = 0;                        // a guest: when the host's game last said something
    std::vector<std::uint32_t> taken_at_;                 // a guest: when he took the host's states, the last few
    std::vector<std::pair<int, std::uint32_t>> parting_;  // the host: machines let go, and when they are dropped
    // For the log: the times the game was held up long enough to be seen, the longest, and for whom it waits now;
    // what is said again and again, at most every few seconds.
    int holds_ = 0;
    std::uint32_t longest_hold_ = 0;
    std::optional<int> held_for_;
    log::Throttle holds_said_{5000};
    log::Throttle resyncs_said_{2000};
};

}  // namespace lvr::net
