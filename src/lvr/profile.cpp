#include "lvr/profile.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <format>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <system_error>
#include <utility>

#include "lvr/archive.hpp"

namespace lvr {

namespace {

constexpr std::string_view kSeenKind = "lvr-minimap 1";
constexpr std::string_view kSeenFile = "minimap.txt";
constexpr int kMostSide = 4096;  // more than any level's minimap

// The next word of `text` as a number; none at the text's end or if it isn't one.
std::optional<std::size_t> number(std::string_view& text) {
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1);
    }
    std::size_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end == text.data()) {
        return std::nullopt;
    }
    text.remove_prefix(static_cast<std::size_t>(end - text.data()));
    return value;
}

std::string_view line_of(std::string_view& text) {
    const std::size_t end = text.find('\n');
    const std::string_view line = text.substr(0, end);
    text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
    return line;
}

}  // namespace

std::string format_seen(const SeenLevels& levels) {
    std::string text{kSeenKind};
    text += '\n';
    for (const auto& [level, map] : levels) {
        text += std::format("level {} {} {}\n", level, map.width, map.height);
        bool counting = false;  // the first run is of pixels not seen
        std::size_t run = 0;
        for (const bool seen : map.seen) {
            if (seen != counting) {
                text += std::format("{} ", run);
                counting = seen;
                run = 0;
            }
            ++run;
        }
        text += std::format("{}\n", run);
    }
    return text;
}

std::expected<SeenLevels, std::string> parse_seen(std::string_view text) {
    if (line_of(text) != kSeenKind) {
        return std::unexpected{std::string{"not a minimap file of this kind"}};
    }
    SeenLevels levels;
    while (!text.empty()) {
        std::string_view head = line_of(text);
        if (head.empty()) {
            continue;
        }
        if (!head.starts_with("level ")) {
            return std::unexpected{std::string{"a level's line was expected"}};
        }
        head.remove_prefix(6);
        const auto level = number(head);
        const auto width = number(head);
        const auto height = number(head);
        if (!level || !width || !height || *width > kMostSide || *height > kMostSide) {
            return std::unexpected{std::string{"a level's line isn't its number and a size"}};
        }
        SeenLevel map{.width = static_cast<int>(*width), .height = static_cast<int>(*height), .seen = {}};
        const std::size_t pixels = *width * *height;
        map.seen.reserve(pixels);
        std::string_view runs = line_of(text);
        bool seen = false;
        while (const auto run = number(runs)) {
            if (*run > pixels - map.seen.size()) {
                return std::unexpected{std::format("level {}: more pixels than its size has", *level)};
            }
            map.seen.insert(map.seen.end(), *run, seen);
            seen = !seen;
        }
        if (map.seen.size() != pixels) {
            return std::unexpected{std::format("level {}: fewer pixels than its size has", *level)};
        }
        levels[static_cast<int>(*level)] = std::move(map);
    }
    return levels;
}

std::expected<SeenLevels, std::string> load_seen(const std::filesystem::path& folder) {
    const std::filesystem::path path = folder / kSeenFile;
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return SeenLevels{};  // nothing was kept yet
    }
    std::stringstream text;
    text << file.rdbuf();
    return parse_seen(text.str()).transform_error([&path](const std::string& error) {
        return std::format("{}: {}", path.string(), error);
    });
}

namespace {

// A file written beside its place and then put there, so a write cut short doesn't leave half a file.
std::expected<void, std::string> write_file(const std::filesystem::path& folder, std::string_view name,
                                            std::string_view contents) {
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const std::filesystem::path path = folder / name;
    std::filesystem::path beside = path;
    beside += ".new";
    {
        std::ofstream file{beside, std::ios::binary | std::ios::trunc};
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!file) {
            return std::unexpected{std::format("can't write {}", beside.string())};
        }
    }
    std::filesystem::rename(beside, path, error);
    if (error) {
        return std::unexpected{std::format("can't write {}: {}", path.string(), error.message())};
    }
    return {};
}

constexpr std::string_view kQuicksaveFile = "quicksave.bin";
constexpr std::array<std::uint8_t, 4> kQuicksaveKind{'L', 'V', 'R', 'Q'};

// A sum over the world's bytes (FNV-1a), written after them: a file that was damaged isn't read as a world.
std::uint64_t sum_of(const std::vector<std::uint8_t>& bytes) noexcept {
    constexpr std::uint64_t kPrime = 0x100000001b3ULL;
    std::uint64_t sum = 0xcbf29ce484222325ULL;
    for (const std::uint8_t byte : bytes) {
        sum = (sum ^ byte) * kPrime;
    }
    return sum;
}

}  // namespace

std::expected<void, std::string> save_seen(const std::filesystem::path& folder, const SeenLevels& levels) {
    return write_file(folder, kSeenFile, format_seen(levels));
}

std::expected<std::optional<SavedPlay>, std::string> load_quicksave(const std::filesystem::path& folder) {
    const std::filesystem::path path = folder / kQuicksaveFile;
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::optional<SavedPlay>{};
    }
    std::stringstream text;
    text << file.rdbuf();
    const std::string contents = text.str();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): the file's bytes as bytes
    Reader from{std::span{reinterpret_cast<const std::uint8_t*>(contents.data()), contents.size()}};
    std::array<std::uint8_t, 4> kind{};
    std::uint32_t version = 0;
    SavedPlay play;
    from(kind);
    from(version);
    from(play.remaster);
    from(play.scene);
    from(play.database_entry);
    from(play.world);
    std::uint64_t sum = 0;
    from(sum);
    if (kind != kQuicksaveKind) {
        return std::unexpected{std::format("{}: not a quicksave", path.string())};
    }
    if (version != kStateVersion) {
        return std::unexpected{
            std::format("{}: written by another version of the game (its state is version {}, "
                        "this game's is {}): it can't be read",
                        path.string(), version, kStateVersion)};
    }
    if (!from.done() || sum != sum_of(play.world)) {
        return std::unexpected{std::format("{}: the quicksave is damaged", path.string())};
    }
    return play;
}

std::expected<void, std::string> save_quicksave(const std::filesystem::path& folder, const SavedPlay& play) {
    std::vector<std::uint8_t> bytes;
    Writer to{bytes};
    to(kQuicksaveKind);
    to(kStateVersion);  // first after the file's kind: read before anything else is believed
    to(play.remaster);
    to(play.scene);
    to(play.database_entry);
    to(play.world);
    to(sum_of(play.world));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): the bytes as a file's
    return write_file(folder, kQuicksaveFile, {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}

namespace {

constexpr std::string_view kSettingsKind = "lvr-settings 1";
constexpr std::string_view kSettingsFile = "settings.txt";

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

// The first word of `text`, taken off it (and the spaces after it).
std::string_view word_of(std::string_view& text) {
    const std::size_t end = text.find(' ');
    const std::string_view word = text.substr(0, end);
    text = trimmed(end == std::string_view::npos ? std::string_view{} : text.substr(end + 1));
    return word;
}

std::optional<int> whole_number(std::string_view text) {
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < 0) {
        return std::nullopt;
    }
    return value;
}

std::optional<bool> on_or_off(std::string_view text) {
    if (text == "on" || text == "off") {
        return text == "on";
    }
    return std::nullopt;
}

}  // namespace

std::string format_settings(const Settings& settings) {
    std::string text = std::format("{}\n", kSettingsKind);
    const auto on = [](bool b) { return b ? "on" : "off"; };
    text += std::format("sound {}\nmusic {}\nsound-fx {}\nscreen {}\nzoom {}\nminimap {}\nvisual-fx {}\n",
                        settings.modern_sound ? "modern" : "classic", settings.music, settings.sound_fx,
                        settings.screen, settings.zoom, on(settings.minimap), on(settings.visual_fx));
    const auto table = [&text](std::string_view word, const auto& bound) {
        for (std::size_t action = 0; action < input::kActionCount; ++action) {
            const std::string_view name = input::name_of(static_cast<input::Action>(action));
            if (bound.at(action).empty()) {
                text += std::format("{} {}\n", word, name);
            }
            for (const std::string& input : bound.at(action)) {
                text += std::format("{} {} {}\n", word, name, input);
            }
        }
    };
    table("key", settings.controls.keys);
    table("pad", settings.controls.pad);
    return text;
}

Settings parse_settings(std::string_view text, std::vector<std::string>& problems) {
    Settings settings;
    if (trimmed(line_of(text)) != kSettingsKind) {
        problems.emplace_back("not a settings file of this kind: nothing of it is used");
        return settings;
    }
    // An action the file names gets the keys (or inputs) it gives, and none else; the rest keep their defaults.
    std::array<bool, input::kActionCount> keys_named{};
    std::array<bool, input::kActionCount> pad_named{};
    for (int number = 2; !text.empty(); ++number) {
        std::string_view line = trimmed(line_of(text));
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::string_view name = word_of(line);
        const auto said = [&](std::string_view what) { problems.push_back(std::format("line {}: {}", number, what)); };
        if (name == "key" || name == "pad") {
            const bool pad = name == "pad";
            const auto action = input::action_named(word_of(line));
            if (!action) {
                said("no such action");
                continue;
            }
            const auto index = static_cast<std::size_t>(*action);
            auto& bound = pad ? settings.controls.pad : settings.controls.keys;
            bool& named = pad ? pad_named.at(index) : keys_named.at(index);
            if (!std::exchange(named, true)) {
                bound.at(index).clear();
            }
            if (line.empty()) {
                continue;  // (none)
            }
            if (pad ? input::fixed_pad_input(line) || !input::pad_input_index(line) : input::fixed_key(line)) {
                said(pad ? "not a gamepad's input that can be bound" : "a key that can't be bound");
                continue;
            }
            bound.at(index).emplace_back(line);
        } else if (name == "sound" && (line == "classic" || line == "modern")) {
            settings.modern_sound = line == "modern";
        } else if (const auto volume = whole_number(line); volume && *volume <= kLoudest && name == "music") {
            settings.music = *volume;
        } else if (volume && *volume <= kLoudest && name == "sound-fx") {
            settings.sound_fx = *volume;
        } else if (volume && name == "zoom") {
            settings.zoom = *volume;
        } else if (name == "screen" && !line.empty()) {
            settings.screen = line;
        } else if (const auto flag = on_or_off(line); flag && name == "minimap") {
            settings.minimap = *flag;
        } else if (flag && name == "visual-fx") {
            settings.visual_fx = *flag;
        } else {
            said("not understood");
        }
    }
    return settings;
}

Settings load_settings(const std::filesystem::path& folder, std::vector<std::string>& problems) {
    const std::filesystem::path path = folder / kSettingsFile;
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return Settings{};
    }
    std::stringstream read;
    read << file.rdbuf();
    const std::string contents = read.str();
    std::vector<std::string> found;
    Settings settings = parse_settings(contents, found);
    for (const std::string& problem : found) {
        problems.push_back(std::format("{}: {}", path.string(), problem));
    }
    return settings;
}

std::expected<void, std::string> save_settings(const std::filesystem::path& folder, const Settings& settings) {
    return write_file(folder, kSettingsFile, format_settings(settings));
}

}  // namespace lvr
