#include "lvr/coop.hpp"

#include <array>
#include <charconv>
#include <format>
#include <map>
#include <sstream>
#include <string>
#include <utility>

namespace lvr {

namespace {

constexpr std::size_t kTyped = 12;  // where the host's own bytes are
constexpr std::size_t kMenu = 14;
static_assert(net::kGoneByte == 15);  // the last: whether he is gone (net::gone_input)
constexpr double kStepsApart = net::kStepMs - (1000.0 * 3.0 / kDisplayHz);
static_assert(kStepsApart < 1e-9 && kStepsApart > -1e-9);  // (the step the match times by is the game's)

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
        own.gone = (packed.bytes[net::kGoneByte] & 1U) != 0;
    }
    for (std::size_t player = inputs.size(); player < input.players.size(); ++player) {
        input.players.at(player).gone = true;  // (no input for the place: nobody there)
    }
    if (!inputs.empty()) {
        input.typed = get16(inputs.front(), kTyped);
        input.menu = menu_of(inputs.front().bytes[kMenu]);
    }
    return input;
}

std::expected<std::optional<std::uint32_t>, std::string> replay_log(const std::filesystem::path& game_files,
                                                                    std::string_view log) {
    int slot = 0;
    int players = 1;
    bool cheat = false;
    std::vector<std::vector<net::Input>> steps;
    std::map<std::uint32_t, std::uint64_t> hashes;
    std::istringstream lines{std::string{log}};
    std::string line;
    int number = 0;
    while (std::getline(lines, line)) {
        ++number;
        std::istringstream words{line};
        std::string word;
        words >> word;
        const auto bad = [&] { return std::unexpected{std::format("line {}: can't be read: {}", number, line)}; };
        if (word == "slot") {
            words >> slot;
        } else if (word == "players") {
            words >> players;
        } else if (word == "cheat") {
            int on = 0;
            words >> on;
            cheat = on != 0;
        } else if (word == "first") {
            std::uint32_t first = 0;
            words >> first;
            if (first != 0) {
                return std::unexpected{std::format(
                    "the log of one who joined the game under way, from step {}: play the host's again", first)};
            }
        } else if (word == "step") {
            std::uint32_t step = 0;
            words >> step;
            if (step != steps.size()) {
                return bad();
            }
            std::vector<net::Input>& inputs = steps.emplace_back();
            std::string hex;
            while (words >> hex) {
                net::Input& input = inputs.emplace_back();
                if (hex.size() != input.bytes.size() * 2) {
                    return bad();
                }
                for (std::size_t i = 0; i < input.bytes.size(); ++i) {
                    const char* begin = hex.data() + (i * 2);
                    if (const auto [end, error] = std::from_chars(begin, begin + 2, input.bytes.at(i), 16);
                        error != std::errc{} || end != begin + 2) {
                        return bad();  // (not two hex digits)
                    }
                }
            }
        } else if (word == "hash") {
            std::uint32_t step = 0;
            std::string hex;
            words >> step >> hex;
            std::uint64_t hash = 0;
            const char* end = hex.data() + hex.size();
            if (const auto [read_to, error] = std::from_chars(hex.data(), end, hash, 16);
                hex.empty() || error != std::errc{} || read_to != end) {
                return bad();  // (a log cut short, say)
            }
            hashes[step] = hash;
        }
        if (words.fail() && !words.eof()) {
            return bad();
        }
    }
    auto opened = Session::open(game_files, slot, {}, Rules::kRemaster, draw, Online{.on = true, .players = players});
    if (!opened) {
        return std::unexpected{opened.error()};
    }
    Session& session = **opened;
    if (cheat) {
        session.cheat();
    }
    for (std::uint32_t step = 0; step < steps.size(); ++step) {
        session.run_again(unpack(steps.at(step)), step);
        if (const auto logged = hashes.find(step); logged != hashes.end() && session.state_hash() != logged->second) {
            return step;
        }
    }
    return std::nullopt;
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
