#pragma once

// One game online (ADR-0012), over the connection from its start to its end: rollback over the game (net::Game),
// every player's inputs and their acknowledgements, who is there when, the hashes compared, and the game's log. The
// host is in the middle: he passes every player's inputs on to the others, decides who is there when (net::Decided),
// and lets one who says hello while the game goes on join it under way. Pure: it knows the game only as net::Game, and
// the network only as the connection.

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

    // The game `started`, rollback over `game` (whose state, joining under way, is already the one given).
    Match(Connection& connection, Game& game, const Started& started);

    // Each pass of the window: what came; and what is due to be sent, at most once a refresh. A guest told to go back
    // to the lobby: kBack (what comes after that word is the lobby's). An error ends the game: the host gone, out of
    // step.
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
    // Since when (refreshes) it has waited for a player's input, and for whose (the one known least far).
    int waited() const noexcept { return waited_; }
    std::optional<int> waiting_for() const;
    // The host: everyone back to the lobby. A guest: he leaves.
    void back_to_lobby();
    void quit();
    // The first step whose state differed from another machine's, if one did.
    std::optional<std::uint32_t> out_of_step() const noexcept { return rollback_.out_of_step(); }
    // The game's start, every player's input for every confirmed step, and this machine's hashes: what lv-replay
    // --coop plays again (one who joined under way has the steps from the state he was given only).
    std::string log() const;

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
};

}  // namespace lvr::net
