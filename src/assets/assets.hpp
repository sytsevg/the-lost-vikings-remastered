#pragma once

// Game-ready data, straight from the user's original files (ADR-0014): finds and checks VIKINGS.EXE and DATA.DAT,
// then builds levels, scenes, images, sprites and object databases from DATA.DAT entries, and the dialogue texts from
// VIKINGS.EXE, using the format decoders (data_dat, graphics, sprites, level, objects, text) and the tables in
// catalog.hpp. The game loads everything through this at startup; lv-export
// uses it to write PNG + JSON for inspection. Plain C++: no SDL; the only file access is GameFiles::open.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "assets/catalog.hpp"
#include "assets/data_dat.hpp"
#include "assets/graphics.hpp"
#include "assets/level.hpp"
#include "assets/objects.hpp"
#include "assets/sprites.hpp"
#include "assets/text.hpp"

namespace lvr::assets {

// --- The original files ---

inline constexpr std::string_view kExeName = "VIKINGS.EXE";
inline constexpr std::string_view kDataName = "DATA.DAT";

// VIKINGS.EXE and DATA.DAT of a known release, with DATA.DAT's container parsed. It owns the bytes the archive points
// into, so it can be moved but not copied.
class GameFiles {
  public:
    // Finds VIKINGS.EXE and DATA.DAT in `dir` (DOS names may come in any letter case), reads and checks them.
    static std::expected<GameFiles, std::string> open(const std::filesystem::path& dir);
    // Checks files already in memory: the release must be known (docs/formats/releases.md), DATA.DAT must parse.
    static std::expected<GameFiles, std::string> from_bytes(std::span<const std::byte> exe,
                                                            std::vector<std::byte> data);

    GameFiles(const GameFiles&) = delete;
    GameFiles& operator=(const GameFiles&) = delete;
    GameFiles(GameFiles&&) noexcept = default;
    GameFiles& operator=(GameFiles&&) noexcept = default;
    ~GameFiles() = default;

    const Release& release() const noexcept { return release_; }
    const Archive& archive() const noexcept { return archive_; }
    std::span<const std::byte> exe() const noexcept { return exe_; }

  private:
    GameFiles(std::vector<std::byte> exe, std::vector<std::byte> data, Archive archive, Release release) noexcept
        : exe_{std::move(exe)}, data_{std::move(data)}, archive_{std::move(archive)}, release_{release} {}

    std::vector<std::byte> exe_;
    std::vector<std::byte> data_;  // moving a vector keeps its buffer, so archive_ stays valid
    Archive archive_;
    Release release_;
};

// --- Building blocks ---
// They take the parsed archive rather than GameFiles, so tests can feed them a hand-made DATA.DAT. Error messages
// name the entry or table row that failed.

std::expected<std::vector<std::byte>, std::string> load_entry(const Archive& archive, std::size_t index);
std::expected<LevelHeader, std::string> load_header(const Archive& archive, std::uint16_t header_entry);

// The palette entries applied in order, as the game does: later ones overwrite earlier colours.
std::expected<Palette, std::string> load_palette(const Archive& archive, std::span<const PaletteRef> palettes);

// A full-screen image (docs/formats/graphics.md#full-screen-images).
std::expected<IndexedImage, std::string> load_screen_image(const Archive& archive, std::uint16_t image_entry);

// A tilemap with its tileset and metatiles, and the whole map rendered.
struct TiledMap {
    Tilemap map;
    std::vector<IndexedImage> tileset;
    std::vector<Metatile> metatiles;
    IndexedImage image;
};

std::expected<TiledMap, std::string> load_tiled_map(const Archive& archive, const LevelHeader& header);

// A sprite entry decoded with its catalogue record.
struct SpriteSet {
    SpriteInfo info;
    std::vector<SpriteFrame> frames;
};

std::expected<SpriteSet, std::string> load_sprites(const Archive& archive, const SpriteInfo& info);

// A sprite entry by number; an error if the catalogue doesn't know it.
std::expected<SpriteSet, std::string> load_sprites(const Archive& archive, std::uint16_t entry);

// A world's object database (LevelInfo::object_db_entry, kSceneObjectDbEntry).
std::expected<ObjectDatabase, std::string> load_object_database(const Archive& archive, std::uint16_t entry);

// The dialogue texts, from where `release` keeps them in VIKINGS.EXE.
std::expected<std::vector<DialogueText>, std::string> load_dialogue_texts(std::span<const std::byte> exe,
                                                                          const Release& release);

// The Vikings' start positions, from VIKINGS.EXE.
std::expected<std::array<VikingStartTable, 3>, std::string> load_viking_starts(std::span<const std::byte> exe,
                                                                               const Release& release);

// --- Levels, scenes and images ---

struct LevelData {
    LevelInfo info;
    LevelHeader header;
    Palette palette;
    TiledMap map;
    std::vector<SpriteSet> sprite_sets;  // the header's sprite sets, in its order
    std::vector<SpriteSet> packed_sets;  // the header's packed sets, in its order
};

std::expected<LevelData, std::string> load_level(const Archive& archive, const LevelInfo& info);

struct SceneData {
    SceneInfo info;
    LevelHeader header;
    Palette palette;
    std::optional<TiledMap> map;  // tiled scenes only
    // The whole scene: the rendered map, or for image scenes the screen image with its HUD-area part below.
    IndexedImage image;
    std::vector<SpriteSet> sprite_sets;
    std::vector<SpriteSet> packed_sets;
};

std::expected<SceneData, std::string> load_scene(const Archive& archive, const SceneInfo& info);

// A stand-alone image (the HUD panel, the HUD-area logo) with its palette loaded at colour 0.
struct Image {
    ImageInfo info;
    IndexedImage image;
    Palette palette;
};

std::expected<Image, std::string> load_image(const Archive& archive, const ImageInfo& info);

}  // namespace lvr::assets
