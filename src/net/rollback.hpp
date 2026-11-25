#pragma once

// Rollback, as GGPO does it (ADR-0012; GGPO is MIT: its design is taken, the code is ours). Every machine runs the
// game, and only the players' inputs travel. An input that hasn't come yet is taken to be like that player's last
// one (Game::predict); when it comes otherwise, the game goes back to the first step it got wrong and runs the
// steps from there again, without showing them. Pure: it knows neither the network nor the clock. The front end
// hands it what arrives, asks it to run the next step when that is due, and sends what it says is to be sent.

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace lvr::net {

inline constexpr std::size_t kInputBytes = 16;
inline constexpr int kMostPlayers = 3;

// One player's input for one step, as bytes: what they mean is the game's.
struct Input {
    std::array<std::uint8_t, kInputBytes> bytes{};

    bool operator==(const Input&) const = default;
};

// What rollback drives: the game, a step at a time.
class Game {
  public:
    Game() = default;
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    Game(Game&&) = delete;
    Game& operator=(Game&&) = delete;
    virtual ~Game() = default;

    // The game as it is now kept in `slot` (fewer than Rollback::slots()), and brought back from it.
    virtual void save(std::size_t slot) = 0;
    virtual void load(std::size_t slot) = 0;
    // One step, with every player's input. `shown`: it runs for the first time, and its pictures and sounds are
    // wanted; else it is run again (its sounds go to the cue ledger, which plays only what is new).
    virtual void advance(std::span<const Input> inputs, std::uint32_t step, bool shown) = 0;
    // The state as it is now, hashed (Play::state_hash).
    virtual std::uint64_t hash() = 0;
    // The input a player is taken to give when his hasn't come yet, from his last one: by default the same
    // (holding a direction is the common case). A game clears what is pressed once (a character typed).
    virtual Input predict(const Input& last) { return last; }
    // Whether the next step may only run on every player's input, never on a prediction (it changes the scene:
    // what is never to be run again).
    virtual bool waits_for_inputs() { return false; }
};

struct Settings {
    int players = 1;
    int local = 0;       // this machine's player
    int delay = 0;       // steps of input delay: the local input given now is for that many steps on
    int window = 8;      // steps the game can run ahead of the inputs it has
    int hash_every = 8;  // the steps whose states are compared (every 8th)
};

class Rollback {
  public:
    Rollback(Game& game, const Settings& settings);

    // The snapshots it keeps: the window's steps and two more.
    std::size_t slots() const noexcept { return static_cast<std::size_t>(settings_.window) + 2; }

    // This machine's player's input for the next step he hasn't given one for (local_known()), as long as that is
    // no more than `delay` steps on from the next step to run. False: not now (the game waits).
    bool add_local(const Input& input);
    // A remote player's input for a step. They come more than once and in any order: the first counts. One for a
    // step that was run with another input (a prediction that was wrong) has that step run again at the next
    // advance.
    void add_remote(int player, std::uint32_t step, const Input& input);
    // From step `from` on the player is gone: his input is nothing (Input{}), known for every step.
    void leave(int player, std::uint32_t from);

    // The next step: run it, after running again what a wrong prediction spoiled. False: it can't be run yet (this
    // machine's input for it isn't given, or the window is used up waiting for another player's: a stall).
    bool advance();
    // Only what a wrong prediction spoiled, run again (advance does it first): while the game waits, or at its end.
    void catch_up();

    std::uint32_t step() const noexcept { return step_; }  // the next to run
    // Every player's input is known for every step before this one: no step before it is run again.
    std::uint32_t confirmed() const;
    // A player's inputs are known, without a gap, for every step before this one.
    std::uint32_t known(int player) const;
    std::uint32_t local_known() const { return known(settings_.local); }
    // The local inputs from step `from` on, as they are to be sent (all the others haven't acknowledged).
    std::vector<std::pair<std::uint32_t, Input>> local_from(std::uint32_t from) const {
        return known_from(settings_.local, from);
    }
    // Any player's inputs known without a gap from step `from` on: what the host passes on to the others.
    std::vector<std::pair<std::uint32_t, Input>> known_from(int player, std::uint32_t from) const;
    // How many steps were run again, all told.
    std::uint32_t steps_run_again() const noexcept { return run_again_; }

    // Time sync. The step a remote player is at now (the front end works it out from what he reports and the
    // round trip), and the frame advantage he reports for himself (how far he is ahead of this machine).
    void report(int player, std::uint32_t step_there, int his_advantage);
    // Whether this machine is ahead of the others by more than a step, on average over the last reports: it then
    // waits a refresh before its next step, so neither drifts too far ahead.
    bool ahead() const;
    // This machine's frame advantage over the player: how many steps it is ahead of him (negative: behind).
    int advantage(int player) const;

    // Out of step. This machine's hash of the state after `step`, for every `hash_every`th step, once that step is
    // confirmed; a remote player's for the same; and the first step whose hashes differed, if one did.
    std::optional<std::uint64_t> hash_at(std::uint32_t step) const;
    void add_remote_hash(int player, std::uint32_t step, std::uint64_t hash);
    std::optional<std::uint32_t> out_of_step() const noexcept { return out_of_step_; }

  private:
    struct Known {
        std::vector<std::optional<Input>> inputs;  // by step: what has come
        std::vector<Input> used;                   // by step: what the step was last run with
        std::optional<std::uint32_t> gone_from;
        std::uint32_t contiguous = 0;  // known without a gap before this step
        // Time sync: his frame advantage as this machine sees it, and as he reports it; the last few of each.
        std::array<int, 16> seen{};
        std::array<int, 16> reported{};
        std::size_t reports = 0;
    };

    std::optional<Input> input_of(int player, std::uint32_t step) const;
    std::vector<Input> inputs_for(std::uint32_t step) const;
    void run(std::uint32_t step, bool shown);
    void compare_hashes();

    Game* game_;
    Settings settings_;
    std::array<Known, kMostPlayers> players_;
    std::uint32_t step_ = 0;
    std::optional<std::uint32_t> first_wrong_;
    std::uint32_t run_again_ = 0;
    // By step: each player's hash of the state after it (this machine's among them).
    std::map<std::uint32_t, std::array<std::optional<std::uint64_t>, kMostPlayers>> hashes_;
    std::optional<std::uint32_t> out_of_step_;
};

}  // namespace lvr::net
