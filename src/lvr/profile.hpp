#pragma once

// The player's profile: what is theirs and not the simulation's, kept in a folder from one run of the game to the
// next (ADR-0011, Remaster rules). So far: how much of each level the minimap has seen, the quicksave, and the
// screen's look.

#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

// The screen's look (GpuRenderer::Look), by its name: a line naming the file's kind, then `look NAME`. Reading:
// none if there is no file; an error if it can't be understood.
std::expected<std::optional<std::string>, std::string> load_screen_look(const std::filesystem::path& folder);
std::expected<void, std::string> save_screen_look(const std::filesystem::path& folder, std::string_view look);

}  // namespace lvr
