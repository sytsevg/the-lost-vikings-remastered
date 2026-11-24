#pragma once

// The pixel-match harness (ADR-0017): two engines play the same replay, and what each
// shows is compared refresh by refresh: the Classic picture (320 x 240 palette indices), its palette, and what the
// game asked of the sound. The first refresh that differs ends the run; each side's summary of its entities helps
// to find why.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "assets/graphics.hpp"

namespace lvr::replay {

inline constexpr std::size_t kWidth = 320;
inline constexpr std::size_t kHeight = 240;

// Something the game asks of the sound side. The numbers are the original's (effects, tunes, a level header's
// music action); Classic sound plays exactly these.
struct Sound {
    enum class Kind : std::uint8_t {
        kEffect,       // value: the effect to play
        kStopEffect,   // value: the effect to stop
        kMusicAction,  // value: the action (0 play, 2 fade out, 3 load only), tune: the tune
        kRestartMusic,
        kFadeMusic,
        kStopAll,    // every effect stopped, and the music unless value is 1
        kEffectsOn,  // value: 1 on, 0 off
        kMusicOn,    // value: 1 on, 0 off
    };
    Kind kind = Kind::kEffect;
    std::uint16_t value = 0;
    std::uint8_t tune = 0;

    bool operator==(const Sound&) const = default;
};

std::string describe(const Sound& sound);

// What an engine shows in one refresh of the display (59.71 Hz).
struct Refresh {
    std::vector<std::uint8_t> pixels;  // kWidth x kHeight palette indices, row by row
    assets::Palette palette{};
    std::vector<Sound> sounds;  // asked for in this refresh, in order

    bool operator==(const Refresh&) const = default;
};

// A hash of a refresh over the pixels, the palette and the sounds: what a golden recording keeps of it. FNV-1a of
// 64 bits, its two halves folded into 32 (xor): enough to tell a refresh from the one expected, and a quarter of
// the size in the repository.
std::uint32_t hash(const Refresh& refresh) noexcept;

// An engine playing a replay.
class Player {
  public:
    Player() = default;
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    Player(Player&&) = delete;
    Player& operator=(Player&&) = delete;
    virtual ~Player() = default;

    // The next refresh; nullopt when the replay is over. An error if the engine stopped.
    virtual std::expected<std::optional<Refresh>, std::string> next() = 0;
    // The entities as they are now, a line each, in the engine's own words.
    virtual std::string entities() = 0;
};

struct Difference {
    std::size_t refresh = 0;  // counted from 0
    bool picture = false;     // the pixels differ ...
    std::size_t pixels = 0;   // ... this many
    bool palette = false;
    bool sounds = false;
    bool length = false;  // one side ended, or stopped with an error
    std::string error;    // that side's error
    // What each side showed there (empty if it had ended), and its entities.
    std::optional<Refresh> first;
    std::optional<Refresh> second;
    std::string first_entities;
    std::string second_entities;
};

struct Result {
    std::size_t refreshes = 0;  // that matched
    std::optional<Difference> difference;

    bool matched() const noexcept { return !difference.has_value(); }
};

// Plays both to the end of the replay, or to the first refresh that differs.
Result compare(Player& first, Player& second);

// --- Goldens: a run kept as a hash per refresh, to compare against when the engine that made it is gone ---

// Plays to the end of the replay: every refresh's hash.
std::expected<std::vector<std::uint32_t>, std::string> record(Player& player);

// The text of a golden file: a line per run of refreshes with the same hash, the hash in hex (8 digits) and, when
// the run is longer than one refresh, its length after a space ("6a37ace2 5"); and reading it back (an error names
// the line).
std::string format_golden(const std::vector<std::uint32_t>& hashes);
std::expected<std::vector<std::uint32_t>, std::string> parse_golden(std::string_view text);

// The first refresh at which two recordings differ (their length included); nullopt if they are the same.
std::optional<std::size_t> first_difference(const std::vector<std::uint32_t>& first,
                                            const std::vector<std::uint32_t>& second) noexcept;

// What differs between two refreshes, in words: for the report.
std::string describe(const Difference& difference);

// The pixels that differ, for a picture: 0 where they are the same, 1 where they differ.
std::vector<std::uint8_t> difference_mask(const Refresh& first, const Refresh& second);

}  // namespace lvr::replay
