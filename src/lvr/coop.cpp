#include "lvr/coop.hpp"

#include <array>
#include <utility>

namespace lvr {

namespace {

constexpr std::size_t kTyped = 12;  // where the host's own bytes are
constexpr std::size_t kMenu = 14;

void put16(net::Input& input, std::size_t at, std::uint16_t value) {
    input.bytes.at(at) = static_cast<std::uint8_t>(value & 0xffU);
    input.bytes.at(at + 1) = static_cast<std::uint8_t>(value >> 8U);
}

std::uint16_t get16(const net::Input& input, std::size_t at) {
    return static_cast<std::uint16_t>(input.bytes.at(at) | (unsigned{input.bytes.at(at + 1)} << 8U));
}

std::uint8_t menu_bits(const MenuKeys& menu) {
    unsigned bits = 0;
    const std::array<bool, 7> each{menu.yes,       menu.no, menu.quit, menu.effects, menu.music, menu.previous_level,
                                   menu.next_level};
    for (std::size_t i = 0; i < each.size(); ++i) {
        bits |= each.at(i) ? 1U << i : 0U;
    }
    return static_cast<std::uint8_t>(bits);
}

MenuKeys menu_of(std::uint8_t bits) {
    const auto bit = [bits](unsigned i) { return (bits & (1U << i)) != 0; };
    return MenuKeys{.yes = bit(0),
                    .no = bit(1),
                    .quit = bit(2),
                    .effects = bit(3),
                    .music = bit(4),
                    .previous_level = bit(5),
                    .next_level = bit(6)};
}

}  // namespace

net::Input pack(const StepInput& input, int player) {
    const PlayerInput& own = input.players.at(static_cast<std::size_t>(player));
    net::Input packed;
    put16(packed, 0, own.keys);
    packed.bytes[2] = own.pace.across;
    packed.bytes[3] = own.pace.up_down;
    put16(packed, 4, static_cast<std::uint16_t>(own.zoom[0]));
    put16(packed, 6, static_cast<std::uint16_t>(own.zoom[1]));
    put16(packed, 8, static_cast<std::uint16_t>(own.view[0]));
    put16(packed, 10, static_cast<std::uint16_t>(own.view[1]));
    if (player == 0) {  // the host's: he drives the game
        put16(packed, kTyped, input.typed);
        packed.bytes[kMenu] = menu_bits(input.menu);
    }
    return packed;
}

StepInput unpack(std::span<const net::Input> inputs) {
    StepInput input;
    for (std::size_t player = 0; player < inputs.size() && player < input.players.size(); ++player) {
        const net::Input& packed = inputs[player];
        PlayerInput& own = input.players.at(player);
        own.keys = get16(packed, 0);
        own.pace = Pace{.across = packed.bytes[2], .up_down = packed.bytes[3]};
        own.zoom = {static_cast<std::int16_t>(get16(packed, 4)), static_cast<std::int16_t>(get16(packed, 6))};
        own.view = {static_cast<std::int16_t>(get16(packed, 8)), static_cast<std::int16_t>(get16(packed, 10))};
    }
    if (!inputs.empty()) {
        input.typed = get16(inputs.front(), kTyped);
        input.menu = menu_of(inputs.front().bytes[kMenu]);
    }
    return input;
}

void CoopGame::advance(std::span<const net::Input> inputs, std::uint32_t step, bool shown) {
    const StepInput input = unpack(inputs);
    if (!shown) {
        session_->run_again(input, step);
        return;
    }
    std::vector<Screen> made = session_->run(input, step);
    pictures_.insert(pictures_.end(), std::make_move_iterator(made.begin()), std::make_move_iterator(made.end()));
    // A rollback goes back no further than the window: what is older is never run again.
    const std::size_t window = saved_.size() - 2;
    session_->confirmed(step > window ? step - static_cast<std::uint32_t>(window) : 0);
}

net::Input CoopGame::predict(const net::Input& last) {
    net::Input predicted = last;
    put16(predicted, kTyped, 0);
    predicted.bytes[kMenu] = 0;
    return predicted;
}

}  // namespace lvr
