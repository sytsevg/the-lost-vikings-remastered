#include "replay/replay.hpp"

#include <charconv>
#include <format>
#include <fstream>
#include <iterator>
#include <ranges>
#include <utility>

namespace lvr::replay {

namespace {

constexpr std::size_t kMaxSteps = 10'000'000;

// A number in decimal, or in hex with 0x.
std::optional<std::uint32_t> number(std::string_view word) {
    int base = 10;
    if (word.starts_with("0x") || word.starts_with("0X")) {
        word.remove_prefix(2);
        base = 16;
    }
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value, base);
    if (error != std::errc{} || end != word.data() + word.size() || word.empty()) {
        return std::nullopt;
    }
    return value;
}

// "A" or "A-B": a step, or the steps from A to B.
std::optional<std::pair<std::size_t, std::size_t>> step_range(std::string_view word) {
    const std::size_t dash = word.find('-');
    const auto first = number(word.substr(0, dash));
    const auto last = dash == std::string_view::npos ? first : number(word.substr(dash + 1));
    if (!first || !last || *last < *first) {
        return std::nullopt;
    }
    return std::pair{std::size_t{*first}, std::size_t{*last}};
}

std::vector<std::string_view> words_of(std::string_view line) {
    std::vector<std::string_view> words;
    for (const auto part : line | std::views::split(' ')) {
        if (!part.empty()) {
            words.emplace_back(part.begin(), part.end());
        }
    }
    return words;
}

}  // namespace

std::expected<Replay, std::string> parse_replay(std::string_view text) {
    Replay replay;
    bool sized = false;
    std::size_t line_number = 0;
    for (const auto part : text | std::views::split('\n')) {
        ++line_number;
        std::string_view line{part.begin(), part.end()};
        line = line.substr(0, line.find('#'));
        while (!line.empty() && (line.back() == '\r' || line.back() == '\t')) {
            line.remove_suffix(1);
        }
        const std::vector<std::string_view> words = words_of(line);
        if (words.empty()) {
            continue;
        }
        const auto bad = [&](std::string_view why) {
            return std::unexpected{std::format("line {}: {}", line_number, why)};
        };
        const std::string_view what = words[0];
        if (what == "slot" || what == "steps") {
            const auto value = words.size() == 2 ? number(words[1]) : std::nullopt;
            if (!value || (what == "slot" && *value > 0xffff) || *value > kMaxSteps) {
                return bad("a number is expected");
            }
            if (what == "slot") {
                replay.slot = static_cast<std::uint16_t>(*value);
            } else {
                replay.steps.resize(*value);
                sized = true;
            }
            continue;
        }
        if (what != "key" && what != "held" && what != "typed" && what != "place") {
            return bad("not a replay line (slot, steps, key, held, typed, place)");
        }
        if (!sized) {
            return bad("`steps` must come first");
        }
        if (what == "place") {
            const auto at = words.size() == 4 ? number(words[1]) : std::nullopt;
            const auto x = words.size() == 4 ? number(words[2]) : std::nullopt;
            const auto y = words.size() == 4 ? number(words[3]) : std::nullopt;
            if (!at || !x || !y || *x > 0x7fff || *y > 0x7fff) {
                return bad("expected: a step, x and y");
            }
            if (*at >= replay.steps.size()) {
                return bad("a step past the replay's last");
            }
            replay.steps[*at].place = {static_cast<std::int16_t>(*x), static_cast<std::int16_t>(*y)};
            continue;
        }
        const auto range = words.size() == 3 ? step_range(words[1]) : std::nullopt;
        const auto value = words.size() == 3 ? number(words[2]) : std::nullopt;
        if (!range || !value || *value > 0xffff || (what == "held" && *value > 0xff)) {
            return bad("expected: steps (N or N-M) and a value");
        }
        if (range->second >= replay.steps.size()) {
            return bad("a step past the replay's last");
        }
        for (std::size_t step = range->first; step <= range->second; ++step) {
            Step& target = replay.steps[step];
            if (what == "key") {
                target.keyboard = static_cast<std::uint16_t>(target.keyboard | *value);
            } else if (what == "held") {
                target.held.push_back(static_cast<std::uint8_t>(*value));
            } else {
                target.typed = static_cast<std::uint16_t>(*value);
            }
        }
    }
    if (!sized) {
        return std::unexpected{std::string{"no `steps` line"}};
    }
    return replay;
}

std::expected<Replay, std::string> load_replay(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return std::unexpected{std::format("can't read {}", path.string())};
    }
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    return parse_replay(text).transform_error(
        [&path](const std::string& error) { return std::format("{}: {}", path.string(), error); });
}

std::string format_replay(const Replay& replay) {
    std::string text = std::format("slot {}\nsteps {}\n", replay.slot, replay.steps.size());
    const auto range = [](std::size_t first, std::size_t last) {
        return first == last ? std::format("{}", first) : std::format("{}-{}", first, last);
    };
    for (std::size_t step = 0; step < replay.steps.size();) {
        std::size_t last = step;
        while (last + 1 < replay.steps.size() && replay.steps[last + 1].keyboard == replay.steps[step].keyboard) {
            ++last;
        }
        if (replay.steps[step].keyboard != 0) {
            text += std::format("key {} 0x{:04x}\n", range(step, last), replay.steps[step].keyboard);
        }
        step = last + 1;
    }
    for (std::size_t step = 0; step < replay.steps.size(); ++step) {
        for (const std::uint8_t scancode : replay.steps[step].held) {
            text += std::format("held {} 0x{:02x}\n", step, scancode);
        }
    }
    for (std::size_t step = 0; step < replay.steps.size(); ++step) {
        if (const std::optional<std::uint16_t> typed = replay.steps[step].typed; typed) {
            text += std::format("typed {} 0x{:02x}\n", step, *typed);
        }
    }
    for (std::size_t step = 0; step < replay.steps.size(); ++step) {
        if (const auto& place = replay.steps[step].place; place) {
            text += std::format("place {} {} {}\n", step, (*place)[0], (*place)[1]);
        }
    }
    return text;
}

}  // namespace lvr::replay
