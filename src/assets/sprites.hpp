#pragma once

// Sprite formats (docs/formats/graphics.md#sprites): raw, unpacked, packed32 and tiled frames. These are the
// generic decoders; which entry holds which sprites, and in which format, is in the catalogue (catalog.hpp).

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

#include "assets/graphics.hpp"
#include "assets/reader.hpp"

namespace lvr::assets {

// How a sprite entry stores its frames (docs/formats/graphics.md#sprites). All four are planar.
enum class SpriteFormat : std::uint8_t {
    kRaw,       // one byte per pixel, no transparency (HUD portraits, health, items)
    kUnpacked,  // a mask byte before every 8 pixels of a plane; masked-out pixels are stored as 0
    kPacked32,  // 32x32 frames behind a u16 offset table; per plane row a mask byte, then 4-bit pixels
    kTiled,     // a sprite sheet of 8x8 tiles, each 8 mask bytes then 64 pixels of 4 bits (entries 293, 342)
};

std::string_view to_string(SpriteFormat format) noexcept;  // "raw", "unpacked", "packed32", "tiled"

// A run of equally sized frames, stored back to back. Raw and unpacked entries don't say how big their frames are:
// the game picks the size when it draws (8x8, 16x16, or 32 pixels wide by any height for unpacked sprites), and one
// entry can mix sizes. The sprite catalogue therefore lists the runs, found by decoding and looking at every entry.
struct FrameRun {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t count = 0;
};

struct SpriteFrame {
    std::size_t offset = 0;  // where the frame starts in the entry; the game's animations address frames this way
    std::size_t size = 0;    // bytes the frame takes
    // Colour 0 is transparent for unpacked, packed32 and tiled frames: no sprite in the game has an opaque colour 0.
    // Unpacked and raw pixels are palette indices; packed32 and tiled pixels are 1..15, added to a colour base the
    // game picks when it draws the frame.
    IndexedImage image;
};

// One unpacked frame: for each plane, the plane's pixels in row-major order (x = plane, plane + 4, ...), in groups
// of 8 behind a mask byte whose most significant bit is the first pixel. Width must be a multiple of 4 and each
// plane a whole number of groups (width * height a multiple of 32). Reads width * height * 9 / 8 bytes.
std::expected<IndexedImage, Error> decode_unpacked_frame(std::span<const std::byte> data, std::size_t width,
                                                         std::size_t height);

// A raw or unpacked entry: the frames of `runs`, back to back. The runs must use up the entry exactly.
// A tiled entry takes exactly one run (see decode_tiled); a packed32 entry ignores the runs (see decode_packed32).
std::expected<std::vector<SpriteFrame>, Error> decode_sprite_frames(std::span<const std::byte> data,
                                                                    SpriteFormat format,
                                                                    std::span<const FrameRun> runs);

// One tile of a tiled entry: 72 bytes. 8 mask bytes, then the 64 pixel bytes of a planar 8x8 tile (plane 0 first:
// x = 0 and 4 of each row). Mask byte k covers pixel bytes 8k..8k+7, most significant bit first; masked-out pixels
// are stored as 0.
inline constexpr std::size_t kMaskedTileBytes = 72;

// A tiled entry: a sprite sheet `run.count` frames wide (frames side by side, `run.width` x `run.height` each),
// stored as 8x8 masked tiles row by row across the whole sheet. Width and height must be multiples of 8, and the
// tiles must use up the entry exactly. A frame's tiles are not contiguous: its offset is its top-left tile and its
// size the bytes of all its tiles.
std::expected<std::vector<SpriteFrame>, Error> decode_tiled(std::span<const std::byte> data, const FrameRun& run);

// A packed32 entry: u16 frame offsets (the first offset / 2 is the frame count), then the frames. Each frame is
// 4 planes x 32 rows of: a mask byte (bit 7 = x = plane), then one 4-bit pixel per set bit, high nibble first,
// padded to whole bytes. Bytes after the last frame are allowed (most entries end in 640 bytes of 0xff).
std::expected<std::vector<SpriteFrame>, Error> decode_packed32(std::span<const std::byte> data);

}  // namespace lvr::assets
