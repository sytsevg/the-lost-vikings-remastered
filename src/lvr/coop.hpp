#pragma once

// Co-op online (ADR-0012): the game (a Session) as what rollback drives (net::Game), and a step's input as the bytes
// the machines send each other. A player's 16 bytes: his keys, his stick's pace, his zoom (the view he chose and the
// view from this step on), and, the host's only, the character typed and the menu keys.

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "lvr/session.hpp"
#include "net/rollback.hpp"

namespace lvr {

// A player's part of a step's input, as bytes; and every player's, back into a step's input (the host's, the
// first, has the character typed and the menu keys).
net::Input pack(const StepInput& input, int player);
StepInput unpack(std::span<const net::Input> inputs);

// A log of a game online (NetGame::log) played again: the game started as it was, every step run on every player's
// input as the log has it. The first step whose state's hash isn't the log's; none: all of them are.
std::expected<std::optional<std::uint32_t>, std::string> replay_log(const std::filesystem::path& game_files,
                                                                    std::string_view log);

class CoopGame : public net::Game {
  public:
    explicit CoopGame(Session& session, std::size_t slots) : session_{&session}, saved_(slots) {}

    void save(std::size_t slot) override { saved_.at(slot) = session_->snapshot(); }
    void load(std::size_t slot) override { session_->restore(saved_.at(slot)); }
    void advance(std::span<const net::Input> inputs, std::uint32_t step, bool shown) override;
    std::uint64_t hash() override { return session_->state_hash(); }
    // What isn't held but pressed once is taken to be pressed no more: the character typed, the menu keys.
    net::Input predict(const net::Input& last) override;
    bool waits_for_inputs() override { return session_->changing_scene(); }

    // The play as it was before `step` (one of the window's: a snapshot kept), or as it is if that is the step to run
    // next (`now`): what one joining under way is given.
    Play::Snapshot snapshot_before(std::uint32_t step, std::uint32_t now) const {
        return step == now ? session_->snapshot() : saved_.at(step % saved_.size());
    }
    // The pictures of the steps shown since this was last asked, a refresh each.
    std::vector<Screen> take_pictures() { return std::exchange(pictures_, {}); }

  private:
    Session* session_;
    std::vector<Play::Snapshot> saved_;
    std::vector<Screen> pictures_;
};

}  // namespace lvr
