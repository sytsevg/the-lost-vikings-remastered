#pragma once

// The tests' game for co-op online (src/net): a net::Game whose state is all its inputs, in order, so any input run
// otherwise shows in it; it can be written out for one joining under way, and knows who is in it as the real game
// does (a player's gone input: away; any other: here).

#include <array>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "net/rollback.hpp"

namespace lvr::net::test {

class Toy : public Game {
  public:
    std::uint64_t state = 1;
    std::array<bool, kMostPlayers> in{true, true, true};  // who is in it
    std::map<std::uint32_t, int> shown;                   // by step: how often it was shown
    std::optional<std::uint32_t> spoil_at;                // this machine goes wrong at that step (out of step)

    void save(std::size_t slot) override {
        if (saved_.size() <= slot) {
            saved_.resize(slot + 1);
        }
        saved_.at(slot) = {.state = state, .in = in};
    }
    void load(std::size_t slot) override {
        state = saved_.at(slot).state;
        in = saved_.at(slot).in;
    }
    void advance(std::span<const Input> inputs, std::uint32_t step, bool shown_now) override {
        for (std::size_t player = 0; player < inputs.size(); ++player) {
            state = (state * 1099511628211ULL) + inputs[player].bytes[0] + (player * 7) + 1;
            if (player < in.size()) {
                in.at(player) = (inputs[player].bytes[kGoneByte] & 1U) == 0;
            }
        }
        state ^= step;
        if (spoil_at && *spoil_at == step) {
            ++state;
        }
        if (shown_now) {
            ++shown[step];
        }
    }
    std::uint64_t hash() override { return state; }
    std::vector<std::uint8_t> state_bytes(std::size_t slot) override {
        return written(saved_.at(slot).state, saved_.at(slot).in);
    }
    std::vector<std::uint8_t> state_bytes_now() override { return written(state, in); }
    bool here(int player) override { return in.at(static_cast<std::size_t>(player)); }
    std::expected<void, std::string> take_state(std::span<const std::uint8_t> bytes) override {
        if (bytes.size() != 8 + in.size()) {
            return std::unexpected{std::string{"not a toy's state"}};
        }
        take(bytes);
        ++taken;
        return {};
    }
    int taken = 0;  // states taken as the game went on (the host's)

    // One joining under way: the state he was given.
    void take(std::span<const std::uint8_t> bytes) {
        state = 0;
        for (std::size_t i = 0; i < 8; ++i) {
            state |= std::uint64_t{bytes[i]} << (8 * i);
        }
        for (std::size_t player = 0; player < in.size(); ++player) {
            in.at(player) = bytes[8 + player] != 0;
        }
    }

  private:
    struct Kept {
        std::uint64_t state = 0;
        std::array<bool, kMostPlayers> in{};
    };
    static std::vector<std::uint8_t> written(std::uint64_t state, const std::array<bool, kMostPlayers>& in) {
        std::vector<std::uint8_t> bytes;
        bytes.reserve(8 + in.size());
        for (std::size_t i = 0; i < 8; ++i) {
            bytes.push_back(static_cast<std::uint8_t>(state >> (8 * i)));
        }
        for (const bool here : in) {
            bytes.push_back(here ? 1 : 0);
        }
        return bytes;
    }
    std::vector<Kept> saved_;
};

// What a player holds at a step: a direction changed every few steps, as players do.
inline Input held(int player, std::uint32_t step) {
    Input input;
    input.bytes[0] = static_cast<std::uint8_t>(((step / 7) + static_cast<std::uint32_t>(player) * 3) % 4);
    return input;
}

}  // namespace lvr::net::test
