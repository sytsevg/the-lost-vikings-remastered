#include "lvr/places.hpp"

#include <array>
#include <deque>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lvr {

namespace {

constexpr std::size_t kKinds = 235;  // records in a database
constexpr std::size_t kRecordSize = 0x15;
constexpr std::size_t kScriptAt = 3;  // in a record: where the kind's script is
constexpr std::uint8_t kStartAnimation = 0x19;

// What follows each of the scripts' instruction codes: b a byte, w a word, j a place in the scripts (a jump or a
// call), 1 / 2 a byte that says what kind one / two operands are, and then those (docs/original/script-vm.md).
constexpr std::array<std::string_view, 0xD8> kOperands{
    "",    "",    "w",   "j",   "b",   "j",   "",    "",     // 00
    "",    "",    "",    "",    "",    "",    "",    "",     // 08
    "",    "",    "",    "bw",  "22b", "2",   "2",   "bb",   // 10
    "bb",  "w",   "bj",  "bb",  "j",   "wj",  "bj",  "bj",   // 18
    "bj",  "bj",  "bj",  "bj",  "bj",  "bj",  "22",  "21",   // 20
    "22",  "21",  "21",  "21",  "bj",  "",    "w",   "",     // 28
    "bj",  "bj",  "bj",  "bj",  "2",   "bj",  "",    "bj",   // 30
    "wj",  "bw",  "",    "w",   "bj",  "bbb", "",    "",     // 38
    "",    "22",  "",    "",    "22",  "12",  "w",   "",     // 40
    "2",   "2bj", "2bj", "",    "bbb", "",    "j",   "j",    // 48
    "21",  "w",   "b",   "w",   "b",   "",    "b",   "w",    // 50
    "b",   "b",   "w",   "b",   "b",   "w",   "b",   "b",    // 58
    "w",   "b",   "b",   "w",   "b",   "b",   "w",   "b",    // 60
    "wj",  "bj",  "wj",  "bj",  "j",   "wj",  "bj",  "wj",   // 68
    "bj",  "j",   "wj",  "bj",  "wj",  "bj",  "j",   "wj",   // 70
    "bj",  "wj",  "bj",  "j",   "wj",  "bj",  "wj",  "bj",   // 78
    "j",   "wj",  "bj",  "wj",  "bj",  "j",   "wj",  "bj",   // 80
    "wj",  "bj",  "j",   "wj",  "bj",  "wj",  "bj",  "j",    // 88
    "b",   "w",   "b",   "b",   "w",   "b",   "",    "bw",   // 90
    "bb",  "bw",  "bb",  "",    "bb",  "bw",  "bb",  "bb",   // 98
    "bw",  "bb",  "bb",  "bw",  "bb",  "bb",  "bw",  "bb",   // A0
    "bwj", "bbj", "bwj", "bbj", "j",   "bwj", "bbj", "bwj",  // A8
    "bbj", "j",   "bwj", "bbj", "bwj", "bbj", "j",   "bwj",  // B0
    "bbj", "bwj", "bbj", "j",   "b",   "w",   "b",   "bj",   // B8
    "bj",  "bj",  "bj",  "bj",  "bj",  "bj",  "bj",  "",     // C0
    "",    "",    "",    "",    "j",   "j",   "j",   "j",    // C8
    "bj",  "bj",  "",    "",    "2b",  "b",   "",    "bw",   // D0
};

std::optional<std::uint16_t> word_at(std::span<const std::byte> bytes, std::size_t at) {
    if (at + 2 > bytes.size()) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(std::to_integer<unsigned>(bytes[at]) |
                                      (std::to_integer<unsigned>(bytes[at + 1]) << 8U));
}

// One instruction, as far as pairing needs it.
struct Step {
    std::uint8_t code = 0;
    std::size_t length = 0;
    std::vector<std::uint16_t> places;  // the script places it names
    std::vector<std::uint8_t> bytes;    // its byte operands: which field, which list of tile types ...
    std::optional<std::uint16_t> animation;
    bool ends = false;  // what follows it isn't reached from it
};

std::optional<Step> read_step(std::span<const std::byte> bytes, std::size_t at) {
    if (at >= bytes.size()) {
        return std::nullopt;
    }
    Step step;
    step.code = std::to_integer<std::uint8_t>(bytes[at]);
    if (step.code >= kOperands.size()) {
        return std::nullopt;
    }
    std::size_t next = at + 1;
    for (const char operand : kOperands.at(step.code)) {
        if (operand == 'b') {
            if (next < bytes.size()) {
                step.bytes.push_back(std::to_integer<std::uint8_t>(bytes[next]));
            }
            ++next;
        } else if (operand == 'w' || operand == 'j') {
            const auto value = word_at(bytes, next);
            if (!value) {
                return std::nullopt;
            }
            if (operand == 'j') {
                step.places.push_back(*value);
            } else if (step.code == kStartAnimation) {
                step.animation = *value;
            }
            next += 2;
        } else {  // a byte of kinds, three bits each, then the operands: a word, a byte, a word, a byte, nothing
            if (next >= bytes.size()) {
                return std::nullopt;
            }
            auto kinds = std::to_integer<unsigned>(bytes[next++]);
            for (int i = 0; i < (operand == '2' ? 2 : 1); ++i) {
                constexpr std::array<std::size_t, 8> kSizes{2, 1, 2, 1, 0, 0, 0, 0};
                next += kSizes.at(kinds & 7U);
                kinds >>= 3U;
            }
        }
    }
    if (next > bytes.size()) {
        return std::nullopt;
    }
    step.length = next - at;
    step.ends = step.code == 0x03 || step.code == 0x06 || step.code == 0x0f || step.code == 0x10;
    return step;
}

// The same instruction in both worlds: the places it names and its words (numbers that differ from world to
// world) aside.
bool same_instruction(const Step& a, const Step& b) {
    return a.code == b.code && a.length == b.length && a.bytes == b.bytes;
}

// Whether the next few instructions from the two places are the same (up to where a script's flow ends).
bool same_run(std::span<const std::byte> ours, std::size_t a, std::span<const std::byte> theirs, std::size_t b) {
    constexpr int kInARow = 4;
    for (int i = 0; i < kInARow; ++i) {
        const auto first = read_step(ours, a);
        const auto second = read_step(theirs, b);
        if (!first || !second || !same_instruction(*first, *second)) {
            return false;
        }
        if (first->ends) {
            return true;
        }
        a += first->length;
        b += second->length;
    }
    return true;
}

// The nearest places after `a` and `b` (at most ten instructions on, on either side, without passing the end of
// a script's flow) from which the two read the same again.
std::optional<std::pair<std::size_t, std::size_t>> same_again(std::span<const std::byte> ours, std::size_t a,
                                                              std::span<const std::byte> theirs, std::size_t b) {
    constexpr int kFurthest = 10;
    std::size_t at_ours = a;
    for (int i = 0; i <= kFurthest; ++i) {
        std::size_t at_theirs = b;
        for (int j = 0; j <= kFurthest; ++j) {
            if ((i != 0 || j != 0) && same_run(ours, at_ours, theirs, at_theirs)) {
                return std::pair{at_ours, at_theirs};
            }
            const auto step = read_step(theirs, at_theirs);
            if (!step || step->ends) {
                break;
            }
            at_theirs += step->length;
        }
        const auto step = read_step(ours, at_ours);
        if (!step || step->ends) {
            break;
        }
        at_ours += step->length;
    }
    return std::nullopt;
}

}  // namespace

AnimationPlaces pair_animation_places(std::span<const std::byte> spaceship, std::span<const std::byte> other) {
    AnimationPlaces places;
    std::deque<std::pair<std::size_t, std::size_t>> to_read;
    // The Vikings and Olaf's shield first, then every other kind: what the first pairing says stands.
    std::vector<std::size_t> kinds{1, 0, 2, 3};
    for (std::size_t kind = 4; kind < kKinds; ++kind) {
        kinds.push_back(kind);
    }
    for (const std::size_t kind : kinds) {
        const auto ours = word_at(spaceship, (kind * kRecordSize) + kScriptAt);
        const auto theirs = word_at(other, (kind * kRecordSize) + kScriptAt);
        if (ours && theirs) {
            // A kind's script starts with a jump (its turn outside the active area); a new entity starts after it.
            to_read.emplace_back(*ours, *theirs);
            to_read.emplace_back(*ours + 3U, *theirs + 3U);
        }
    }
    std::unordered_set<std::uint64_t> read;
    while (!to_read.empty()) {
        auto [ours, theirs] = to_read.front();
        to_read.pop_front();
        while (!read.contains((std::uint64_t{ours} << 32U) | theirs)) {
            const auto a = read_step(spaceship, ours);
            const auto b = read_step(other, theirs);
            if (!a || !b) {
                break;
            }
            if (!same_instruction(*a, *b)) {
                // The two worlds differ here. Often one only has a few instructions more: go on where they
                // are the same again, if that is near.
                const auto again = same_again(spaceship, ours, other, theirs);
                if (!again) {
                    break;
                }
                ours = again->first;
                theirs = again->second;
                continue;
            }
            read.insert((std::uint64_t{ours} << 32U) | theirs);
            if (a->animation && b->animation) {
                places.try_emplace(*a->animation, *b->animation);
            }
            for (std::size_t i = 0; i < a->places.size() && i < b->places.size(); ++i) {
                to_read.emplace_back(a->places[i], b->places[i]);
            }
            if (a->ends) {
                break;
            }
            ours += a->length;
            theirs += b->length;
        }
    }
    return places;
}

}  // namespace lvr
