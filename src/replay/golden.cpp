#include "replay/golden.hpp"

#include <algorithm>
#include <charconv>
#include <format>

namespace lvr::replay {

namespace {

constexpr std::uint64_t kFnvOffset = 0xcbf29ce484222325ULL;
constexpr std::uint64_t kFnvPrime = 0x100000001b3ULL;

void add(std::uint64_t& value, std::uint8_t byte) noexcept {
    value = (value ^ byte) * kFnvPrime;
}

std::size_t differing_pixels(const Refresh& first, const Refresh& second) {
    std::size_t count = first.pixels.size() > second.pixels.size() ? first.pixels.size() - second.pixels.size()
                                                                   : second.pixels.size() - first.pixels.size();
    for (std::size_t i = 0; i < std::min(first.pixels.size(), second.pixels.size()); ++i) {
        count += first.pixels[i] != second.pixels[i] ? 1 : 0;
    }
    return count;
}

std::string sounds_text(const std::optional<Refresh>& refresh) {
    if (!refresh) {
        return "(ended)";
    }
    if (refresh->sounds.empty()) {
        return "(none)";
    }
    std::string text;
    for (const Sound& sound : refresh->sounds) {
        text += (text.empty() ? "" : ", ") + describe(sound);
    }
    return text;
}

}  // namespace

std::string describe(const Sound& sound) {
    switch (sound.kind) {
        case Sound::Kind::kEffect:
            return std::format("effect {}", sound.value);
        case Sound::Kind::kStopEffect:
            return std::format("stop effect {}", sound.value);
        case Sound::Kind::kMusicAction:
            return std::format("music action {} tune {}", sound.value, sound.tune);
        case Sound::Kind::kRestartMusic:
            return "restart music";
        case Sound::Kind::kFadeMusic:
            return "fade music";
        case Sound::Kind::kStopAll:
            return std::format("stop all ({})", sound.value);
        case Sound::Kind::kEffectsOn:
            return std::format("effects {}", sound.value != 0 ? "on" : "off");
        case Sound::Kind::kMusicOn:
            return std::format("music {}", sound.value != 0 ? "on" : "off");
    }
    return "?";
}

std::uint32_t hash(const Refresh& refresh) noexcept {
    std::uint64_t value = kFnvOffset;
    for (const std::uint8_t pixel : refresh.pixels) {
        add(value, pixel);
    }
    for (const assets::Rgb& colour : refresh.palette) {
        add(value, colour.r);
        add(value, colour.g);
        add(value, colour.b);
    }
    for (const Sound& sound : refresh.sounds) {
        add(value, static_cast<std::uint8_t>(sound.kind));
        add(value, static_cast<std::uint8_t>(sound.value & 0xffU));
        add(value, static_cast<std::uint8_t>(sound.value >> 8U));
        add(value, sound.tune);
    }
    return static_cast<std::uint32_t>(value ^ (value >> 32U));
}

Result compare(Player& first, Player& second) {
    Result result;
    while (true) {
        auto a = first.next();
        auto b = second.next();
        if (a && b && !*a && !*b) {
            return result;  // both ended
        }
        Difference difference;
        difference.refresh = result.refreshes;
        if (!a || !b) {
            difference.length = true;
            difference.error = !a ? "first: " + a.error() : "second: " + b.error();
        } else if (!*a || !*b) {
            difference.length = true;
        } else {
            difference.pixels = differing_pixels(**a, **b);
            difference.picture = difference.pixels != 0;
            difference.palette = (*a)->palette != (*b)->palette;
            difference.sounds = (*a)->sounds != (*b)->sounds;
        }
        if (!difference.length && !difference.picture && !difference.palette && !difference.sounds) {
            ++result.refreshes;
            continue;
        }
        if (a && *a) {
            difference.first = std::move(**a);
        }
        if (b && *b) {
            difference.second = std::move(**b);
        }
        difference.first_entities = first.entities();
        difference.second_entities = second.entities();
        result.difference = std::move(difference);
        return result;
    }
}

std::expected<std::vector<std::uint32_t>, std::string> record(Player& player) {
    std::vector<std::uint32_t> hashes;
    while (true) {
        const auto refresh = player.next();
        if (!refresh) {
            return std::unexpected{std::format("refresh {}: {}", hashes.size(), refresh.error())};
        }
        if (!*refresh) {
            return hashes;
        }
        hashes.push_back(hash(**refresh));
    }
}

std::string format_golden(const std::vector<std::uint32_t>& hashes) {
    std::string text;
    for (std::size_t i = 0; i < hashes.size();) {
        std::size_t run = 1;
        while (i + run < hashes.size() && hashes[i + run] == hashes[i]) {
            ++run;
        }
        text += run == 1 ? std::format("{:08x}\n", hashes[i]) : std::format("{:08x} {}\n", hashes[i], run);
        i += run;
    }
    return text;
}

std::expected<std::vector<std::uint32_t>, std::string> parse_golden(std::string_view text) {
    // A run can't be longer than this: a wrong number must not ask for all the memory there is.
    constexpr std::size_t kLongestRun = 10'000'000;
    std::vector<std::uint32_t> hashes;
    std::size_t line_number = 0;
    while (!text.empty()) {
        ++line_number;
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty()) {
            continue;
        }
        const std::size_t space = line.find(' ');
        const std::string_view digits = line.substr(0, space);
        std::uint32_t value = 0;
        const auto [stop, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
        if (error != std::errc{} || stop != digits.data() + digits.size()) {
            return std::unexpected{std::format("line {}: not a hash", line_number)};
        }
        std::size_t run = 1;
        if (space != std::string_view::npos) {
            const std::string_view length = line.substr(space + 1);
            const auto [run_stop, run_error] = std::from_chars(length.data(), length.data() + length.size(), run, 10);
            if (run_error != std::errc{} || run_stop != length.data() + length.size() || run == 0 ||
                run > kLongestRun) {
                return std::unexpected{std::format("line {}: not a run's length", line_number)};
            }
        }
        hashes.insert(hashes.end(), run, value);
    }
    return hashes;
}

std::optional<std::size_t> first_difference(const std::vector<std::uint32_t>& first,
                                            const std::vector<std::uint32_t>& second) noexcept {
    const std::size_t shared = std::min(first.size(), second.size());
    for (std::size_t i = 0; i < shared; ++i) {
        if (first[i] != second[i]) {
            return i;
        }
    }
    return first.size() == second.size() ? std::nullopt : std::optional{shared};
}

std::string describe(const Difference& difference) {
    std::string text = std::format("refresh {}:", difference.refresh);
    if (difference.length) {
        text += !difference.error.empty()
                    ? " stopped, " + difference.error
                    : std::format(" the {} ended, the other goes on", difference.first ? "second" : "first");
    }
    if (difference.picture) {
        text += std::format(" {} pixels differ;", difference.pixels);
    }
    if (difference.palette) {
        text += " the palettes differ;";
    }
    if (difference.sounds) {
        text +=
            std::format(" sounds: first {}, second {};", sounds_text(difference.first), sounds_text(difference.second));
    }
    return text;
}

std::vector<std::uint8_t> difference_mask(const Refresh& first, const Refresh& second) {
    std::vector<std::uint8_t> mask(kWidth * kHeight, 0);
    for (std::size_t i = 0; i < mask.size(); ++i) {
        const bool same = i < first.pixels.size() && i < second.pixels.size() && first.pixels[i] == second.pixels[i];
        mask[i] = same ? 0 : 1;
    }
    return mask;
}

}  // namespace lvr::replay
