#pragma once

// The player's profile: what is theirs and not the simulation's, kept in a folder from one run of the game to the
// next (ADR-0011, Remaster rules): how much of each level the minimap has seen, the quicksave, and the player's
// settings.

#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "input/input.hpp"

namespace lvr {

// What the minimap has seen of a level: a flag for every pixel of the minimap, row by row (minimap.hpp).
struct SeenLevel {
    int width = 0;
    int height = 0;
    std::vector<bool> seen;

    bool operator==(const SeenLevel&) const = default;
};
using SeenLevels = std::map<int, SeenLevel>;  // by level, 1-37

// As text: a line naming the file's kind, then for each level a line with its number and the size, and a line of
// runs: how many pixels in a row were not seen, then how many were, in turn.
std::string format_seen(const SeenLevels& levels);
std::expected<SeenLevels, std::string> parse_seen(std::string_view text);

// The profile's files in `folder`. Reading: what there is; a file that is missing gives nothing, one that can't be
// understood an error (and is then best left alone). Writing makes the folder if need be.
std::expected<SeenLevels, std::string> load_seen(const std::filesystem::path& folder);
std::expected<void, std::string> save_seen(const std::filesystem::path& folder, const SeenLevels& levels);

// The quicksave's file: the version of the game's state (kStateVersion, archive.hpp), then which rules and which
// scene it is of and the world written out (World::save), with a sum over it. Reading: none if there is no file;
// an error that says why if it is of another version, damaged, or no quicksave at all.
struct SavedPlay {
    bool remaster = false;
    int scene = 0;           // the scene's slot: which level is loaded for it
    int database_entry = 0;  // the object database the level was loaded with
    std::vector<std::uint8_t> world;

    bool operator==(const SavedPlay&) const = default;
};
std::expected<std::optional<SavedPlay>, std::string> load_quicksave(const std::filesystem::path& folder);
std::expected<void, std::string> save_quicksave(const std::filesystem::path& folder, const SavedPlay& play);

// The player's settings (Remaster rules): what the game's menu sets (menu.hpp), kept from one run to the next.
inline constexpr int kLoudest = 10;  // a volume's top: as loud as the game asks for (0: not heard)
// The port a game online is hosted on unless another is given, and joined at.
inline constexpr int kDefaultPort = 47230;

struct Settings {
    bool modern_sound = false;    // the Modern sound (if there is a SoundFont), or the Classic
    int music = kLoudest;         // how loud the music is, 0 to kLoudest
    int sound_fx = kLoudest;      // and the sound effects
    std::string screen = "none";  // the screen's look, by its name (GpuRenderer::name_of)
    int zoom = 0;                 // the zoom's step (Session::zoom_level)
    bool minimap = true;          // shown in a level
    bool visual_fx = true;        // the Modern visuals' effects (effects.hpp): light, glow and particles
    input::Controls controls = input::remaster_controls();
    // Co-op online (ADR-0012): the player's name (none: PLAYER and his number), and what the menu's HOST GAME and
    // JOIN GAME were last given: a port, a level's password; an address with its port.
    std::string name;
    int host_port = kDefaultPort;
    std::string host_password;
    std::string join_address;

    bool operator==(const Settings&) const = default;
};
// As text: a line naming the file's kind, then a line a setting ("sound modern", "music 7", "screen tv", "zoom 1",
// "minimap on", ..., "name ERIK", "host-port 47230", "host-password ABCD", "join-address 10.0.0.2:47230"), and a line
// for each key of an action ("key ACTION KEY NAME") and each gamepad input
// ("pad ACTION INPUT"); an action with none has its word and name alone. Read leniently, as a player may edit it:
// a line that isn't understood is said in `problems` and skipped, and what a file doesn't name stays as it is by
// default (an action it names has only the keys it gives).
std::string format_settings(const Settings& settings);
Settings parse_settings(std::string_view text, std::vector<std::string>& problems);
// `settings.txt` in the profile's folder (the defaults if there is none); writing makes the folder if need be.
Settings load_settings(const std::filesystem::path& folder, std::vector<std::string>& problems);
std::expected<void, std::string> save_settings(const std::filesystem::path& folder, const Settings& settings);

}  // namespace lvr
