#pragma once

// Graphics formats (docs/formats/graphics.md): VGA palettes, Mode X planar images, full-screen images, 8x8 tiles,
// 16x16 metatiles and level tilemaps (sprites are in sprites.hpp). Everything stays indexed (palette indices), so
// palette effects keep working; colours are applied only when an image is written out or drawn.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

#include "assets/data_dat.hpp"
#include "assets/reader.hpp"

namespace lvr::assets {

// --- Palettes ---

struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    friend bool operator==(Rgb, Rgb) = default;
};

using Palette = std::array<Rgb, 256>;

// A palette entry is N x 3 bytes of VGA DAC values (6 bits, 0..63). Writes the colours into `palette` starting at
// `first`; colours that would land past index 255 are ignored. 6-bit values are expanded to 8 bits as
// (v << 2) | (v >> 4), so 63 becomes 255.
std::expected<void, Error> load_palette(Palette& palette, std::span<const std::byte> data, std::size_t first);

// --- Indexed images ---

struct IndexedImage {
    std::size_t width = 0;
    std::size_t height = 0;
    std::vector<std::uint8_t> pixels;  // row-major palette indices, width * height

    std::uint8_t at(std::size_t x, std::size_t y) const { return pixels[(y * width) + x]; }
    void set(std::size_t x, std::size_t y, std::uint8_t value) { pixels[(y * width) + x] = value; }
};

IndexedImage make_image(std::size_t width, std::size_t height);

// VGA Mode X planar layout: the image is split into 4 planes by x % 4. All bytes of plane 0 come first (pixels
// x = 0, 4, 8, ... in row-major order), then plane 1, and so on. Width * height must be a multiple of 4.
std::expected<IndexedImage, Error> decode_planar(std::span<const std::byte> data, std::size_t width,
                                                 std::size_t height);

// --- Full-screen images ---

// The game's screens are 344 pixels wide (VGA Mode X drawing width; 320 of them are on screen at a time).
inline constexpr std::size_t kScreenWidth = 344;

// A stored full-screen image (title screen, starfield, HUD-area strips): u16le plane size, then the 4 planes of a
// planar image kScreenWidth pixels wide. The height follows from the size (e.g. 176 for the top part of a screen,
// 64 for the HUD area below it).
std::expected<IndexedImage, Error> decode_screen_image(std::span<const std::byte> data);

// --- Tiles and metatiles ---

inline constexpr std::size_t kTileSize = 8;                       // tiles are 8x8 pixels
inline constexpr std::size_t kTileBytes = kTileSize * kTileSize;  // 64 bytes, planar
inline constexpr std::size_t kMetatileSize = 2 * kTileSize;       // metatiles are 16x16 (2x2 tiles)
inline constexpr std::size_t kMetatileBytes = 8;                  // 4 x u16le

// A tileset entry: a sequence of 8x8 planar tiles.
std::expected<std::vector<IndexedImage>, Error> decode_tileset(std::span<const std::byte> data);

// One quarter of a metatile: u16le, bits 15-6 tile index, bits 5-0 flags.
struct TileRef {
    static constexpr std::uint8_t kForeground = 0x08;  // drawn in front of the Vikings
    static constexpr std::uint8_t kFlipH = 0x10;
    static constexpr std::uint8_t kFlipV = 0x20;

    std::uint16_t tile = 0;
    std::uint8_t flags = 0;  // all 6 bits as stored; bits 2-0 are unused by The Lost Vikings

    bool flip_h() const noexcept { return (flags & kFlipH) != 0; }
    bool flip_v() const noexcept { return (flags & kFlipV) != 0; }
    bool foreground() const noexcept { return (flags & kForeground) != 0; }
};

// A 16x16 metatile ("prefab" in RyanMallon's tools): top-left, top-right, bottom-left, bottom-right.
struct Metatile {
    std::array<TileRef, 4> parts;
};

std::expected<std::vector<Metatile>, Error> decode_metatiles(std::span<const std::byte> data);

// Draws one metatile at (x, y) into `target`. Fails with kBadReference if it uses a tile that doesn't exist.
std::expected<void, Error> draw_metatile(IndexedImage& target, std::size_t x, std::size_t y, const Metatile& metatile,
                                         std::span<const IndexedImage> tileset);

// --- Tilemaps ---

// One map cell: u16le, bits 8-0 metatile index, bits 15-9 attributes. The attributes vary per cell and are most
// likely collision/terrain information; their meaning is documented in docs/original/ once known (M2).
struct MapCell {
    std::uint16_t metatile = 0;
    std::uint8_t attributes = 0;
};

struct Tilemap {
    std::size_t width = 0;   // in metatiles
    std::size_t height = 0;  // in metatiles
    std::vector<MapCell> cells;
    std::size_t stored_cells = 0;  // cells actually present in the data; see decode_tilemap()

    const MapCell& at(std::size_t x, std::size_t y) const { return cells[(y * width) + x]; }
};

// The map entry is normally exactly width * height cells (true for all 37 levels). A few scene maps are shorter than
// their header says (the Interplay logo lacks its last row); the missing cells are kept as default cells and
// counted in stored_cells, and render_tilemap() leaves them undrawn (colour 0). What the original draws there is
// not known yet (docs/formats/graphics.md). Longer or odd-sized data is an error.
std::expected<Tilemap, Error> decode_tilemap(std::span<const std::byte> data, std::size_t width, std::size_t height);

// Renders the whole map: (width * 16) x (height * 16) pixels. Cells past stored_cells stay colour 0.
std::expected<IndexedImage, Error> render_tilemap(const Tilemap& map, std::span<const Metatile> metatiles,
                                                  std::span<const IndexedImage> tileset);

}  // namespace lvr::assets
