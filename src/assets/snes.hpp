#pragma once

// The SNES version's data, straight from the user's own ROM image (docs/formats/snes.md; ADR-0014): the chunk table,
// and the backdrop every level has behind its map there, where the DOS game shows black. Optional: the game runs
// without the image, and the backdrops are for the Modern visuals only. Plain C++: the only file access is
// SnesRom::open.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "assets/graphics.hpp"
#include "assets/reader.hpp"

namespace lvr::assets {

// --- The ROM image ---

struct SnesRelease {
    std::string_view name;
    std::size_t size = 0;
    std::uint64_t hash = 0;  // fnv1a64 of the whole image
};

// The images the game accepts (docs/formats/snes.md).
inline constexpr std::array kKnownSnesRoms{
    SnesRelease{.name = "snes-usa-1993", .size = 0x100000, .hash = 0xff87c75cc9437b46ULL},
};

// Where the chunk table starts: one (u16 offset, u16 bank) per chunk, counted from here in banks of 0x8000 bytes.
inline constexpr std::size_t kSnesChunkTable = 0x58000;
inline constexpr std::size_t kSnesBankSize = 0x8000;

// Chunks stored as they are; every other one is LZSS-compressed. Nothing in the image says which.
inline constexpr std::array<std::size_t, 17> kSnesStoredChunks{4,  5,  6,   7,   8,   9,   10,  11, 12,
                                                               13, 14, 134, 137, 138, 139, 140, 141};

// Each level's header chunk, by level number - 1 (the DOS levels' order, catalog.hpp's kLevels).
inline constexpr std::array<std::uint16_t, 37> kSnesLevelHeaders{
    111, 113, 115, 117,                    // spaceship
    26,  28,  30,  32,  34, 36, 38,        // prehistoric
    47,  49,  51,  53,  55, 57,            // Egypt
    65,  67,  69,  71,  73, 75, 77,  79,   // factory
    89,  91,  93,  95,  97, 99, 101, 103,  // wacky
    119, 121, 123, 125,                    // spaceship again
};

// The image with its chunk table parsed. It owns the bytes, so it can be moved but not copied.
class SnesRom {
  public:
    // Finds the one *.sfc file in `dir`, reads and checks it. An error names what is wrong; "no image" is one.
    static std::expected<SnesRom, std::string> open(const std::filesystem::path& dir);
    // Checks an image already in memory: it must be a known one, and its table must parse.
    static std::expected<SnesRom, std::string> from_bytes(std::vector<std::byte> image);
    // Only parses the table (tests feed this hand-made images).
    static std::expected<SnesRom, Error> parse(std::vector<std::byte> image);

    SnesRom(const SnesRom&) = delete;
    SnesRom& operator=(const SnesRom&) = delete;
    SnesRom(SnesRom&&) noexcept = default;
    SnesRom& operator=(SnesRom&&) noexcept = default;
    ~SnesRom() = default;

    std::size_t chunk_count() const noexcept { return offsets_.size(); }
    // A chunk's bytes as stored: up to the next chunk, or the image's end for the last (which is padded with zeros).
    std::expected<std::span<const std::byte>, Error> raw_chunk(std::size_t index) const noexcept;
    // A chunk decoded (a stored one as it is).
    std::expected<std::vector<std::byte>, Error> chunk(std::size_t index) const;

  private:
    SnesRom(std::vector<std::byte> image, std::vector<std::size_t> offsets) noexcept
        : image_{std::move(image)}, offsets_{std::move(offsets)} {}

    std::vector<std::byte> image_;
    std::vector<std::size_t> offsets_;
};

// --- Backdrops ---

// The backdrop's fields of a level header (offsets 0x34-0x42), which the DOS headers carry too, without the chunks.
struct BackdropHeader {
    std::uint16_t width = 0;  // in quads of 16 x 16 pixels
    std::uint16_t height = 0;
    std::uint16_t map_chunk = 0;    // 0xffff: none
    std::uint16_t tiles_chunk = 0;  // 0xffff: the level's own tiles
    std::uint16_t quads_chunk = 0;  // 0xffff: the level's own quads
    // How far the backdrop moves per pixel of the camera, in 256ths (0x40 a quarter, 0x100 with the camera). Bit 15
    // (the spaceship levels' scroll_x) is said to make it move by itself; not checked (docs/formats/snes.md#open).
    std::uint16_t scroll_x = 0;
    std::uint16_t scroll_y = 0;
};

std::expected<BackdropHeader, Error> parse_backdrop_header(std::span<const std::byte> header);

// 8 x 8 tiles of 4 bits a pixel, 32 bytes each (values 0-15; 0 shows what is behind).
std::expected<std::vector<IndexedImage>, Error> decode_snes_tiles(std::span<const std::byte> data);

// A palette chunk: u16 words of 5 bits red, green, blue (red lowest), written from colour `first`; colours past 255
// are ignored. 5 bits become 8 as (v << 3) | (v >> 2).
std::expected<void, Error> load_snes_palette(Palette& palette, std::span<const std::byte> data, std::size_t first);

// A map of quads drawn: `map` is width x height u16 cells (the low 10 bits the quad), a quad four tile words (bits
// 0-9 the tile, 10-12 the palette row, 14 mirrored, 15 flipped). A pixel is palette row x 16 + the tile's value, or
// colour 0 where the tile shows what is behind.
std::expected<IndexedImage, Error> draw_snes_map(std::span<const std::byte> map, std::size_t width, std::size_t height,
                                                 std::span<const std::byte> quads, std::span<const IndexedImage> tiles);

struct Backdrop {
    BackdropHeader header;
    IndexedImage image;  // the whole backdrop, header.width x 16 by header.height x 16 pixels
    Palette palette;     // the SNES level's colours
};

// The backdrop of the level whose header is chunk `header_chunk`. Error messages name the chunk that failed.
std::expected<Backdrop, std::string> load_backdrop(const SnesRom& rom, std::uint16_t header_chunk);

// The backdrop of level `number` (1-37).
std::expected<Backdrop, std::string> load_level_backdrop(const SnesRom& rom, std::uint8_t number);

// The scenes with a backdrop of their own (docs/formats/snes.md, "The scenes"): their name in catalog.hpp's kScenes,
// and their header chunk. (The night the Vikings are taken, home-demo, has one too: the SNES's night sky with the ship
// flying in, which behind the DOS scene's houses is the ship scrolling past them; left out.)
struct SnesScene {
    std::string_view name;
    std::uint16_t header_chunk = 0;
};
inline constexpr std::array kSnesSceneHeaders{
    SnesScene{.name = "home-intro", .header_chunk = 343},
    SnesScene{.name = "home-ending", .header_chunk = 346},
    SnesScene{.name = "ship", .header_chunk = 130},
    SnesScene{.name = "game-over", .header_chunk = 284},
};

// The backdrop of the scene named `name` (as kScenes names it); an error for a scene without one.
std::expected<Backdrop, std::string> load_scene_backdrop(const SnesRom& rom, std::string_view name);

}  // namespace lvr::assets
