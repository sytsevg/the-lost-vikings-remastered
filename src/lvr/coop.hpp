#pragma once

// Co-op online (ADR-0012): the game (a Session) as what rollback drives (net::Game), and a step's input as the bytes
// the machines send each other. A player's 16 bytes: his keys, his stick's pace, his zoom (the view he chose and the
// view from this step on), and, the host's only, the character typed and the menu keys.

#include <cstdint>
#include <span>
#include <vector>

#include "lvr/session.hpp"
#include "net/rollback.hpp"

namespace lvr {

// A player's part of a step's input, as bytes; and every player's, back into a step's input (the host's, the
// first, has the character typed and the menu keys).
net::Input pack(const StepInput& input, int player);
StepInput unpack(std::span<const net::Input> inputs);

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

    // The pictures of the steps shown since this was last asked, a refresh each.
    std::vector<Screen> take_pictures() { return std::exchange(pictures_, {}); }

  private:
    Session* session_;
    std::vector<Play::Snapshot> saved_;
    std::vector<Screen> pictures_;
};

}  // namespace lvr
