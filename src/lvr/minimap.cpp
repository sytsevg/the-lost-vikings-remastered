#include "lvr/minimap.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>

#include "assets/graphics.hpp"

namespace lvr {

namespace {

struct Colour {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

constexpr Colour kOpen{.r = 24, .g = 28, .b = 44};  // nothing there
constexpr Colour kGround{.r = 124, .g = 128, .b = 142};
constexpr Colour kLadder{.r = 62, .g = 64, .b = 74};  // dark, with its rungs lighter
constexpr Colour kRung{.r = 168, .g = 172, .b = 188};
constexpr Colour kPlatform{.r = 176, .g = 184, .b = 208};
constexpr Colour kWater{.r = 64, .g = 120, .b = 255};
constexpr Colour kLava{.r = 255, .g = 104, .b = 0};
constexpr Colour kQuicksand{.r = 200, .g = 168, .b = 88};
constexpr Colour kSpikes{.r = 255, .g = 56, .b = 56};
constexpr Colour kElectricity{.r = 255, .g = 240, .b = 96};
constexpr Colour kSpring{.r = 80, .g = 220, .b = 120};
// What a cell is shown as; none: open.
std::optional<Colour> colour_of(std::uint8_t tile_type) noexcept {
    switch (tile_type) {
        case tile::kLadder:
        case tile::kLadderTop:
        case tile::kLadder0D:
            return kLadder;
        case tile::kPlatform:
        case tile::kHiddenPlatform:
            return kPlatform;
        case tile::kWater:
            return kWater;
        case tile::kLava11:
        case tile::kLava:
            return kLava;
        case tile::kQuicksand:
            return kQuicksand;
        case tile::kSpikes:
        case tile::kRotatingSpikes:
            return kSpikes;
        case tile::kElectricity:
            return kElectricity;
        case tile::kSpring:
            return kSpring;
        default:
            break;
    }
    const Terrain what = terrain(tile_type);
    if (what == Terrain::kGround || what == Terrain::kSlope || tile_type == tile::kBreakable21 ||
        tile_type == tile::kBreakable22) {
        return kGround;
    }
    return std::nullopt;
}

class Picture {
  public:
    Picture(int width, int height) {
        map_.width = width;
        map_.height = height;
        map_.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    }
    void put(int x, int y, const Colour& colour) {
        if (x < 0 || y < 0 || x >= map_.width || y >= map_.height) {
            return;
        }
        const std::size_t at =
            ((static_cast<std::size_t>(y) * static_cast<std::size_t>(map_.width)) + static_cast<std::size_t>(x)) * 4;
        map_.pixels[at] = colour.r;
        map_.pixels[at + 1] = colour.g;
        map_.pixels[at + 2] = colour.b;
        map_.pixels[at + 3] = colour.a;
    }
    Minimap take() { return std::move(map_); }

  private:
    Minimap map_;
};

// A place of the level on the minimap.
int small(int position) noexcept {
    return position >= 0 ? (position * kMinimapCell) / kCellSize : -1;
}

}  // namespace

bool reveal(std::vector<bool>& seen, const World& world, int player) {
    const Map& map = world.map;
    const int width = map.width * kMinimapCell;
    const int height = map.height * kMinimapCell;
    const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    bool changed = false;
    if (seen.size() != pixels) {
        seen.assign(pixels, false);
        changed = true;
    }
    const Camera& camera = world.players.at(static_cast<std::size_t>(player)).camera;
    const int left = std::max(0, small(std::max(camera.x, 0)));
    const int top = std::max(0, small(std::max(camera.y, 0)));
    const int right = std::min(width - 1, small(camera.x + camera.view_width - 1));
    const int bottom = std::min(height - 1, small(camera.y + camera.view_height - 1));
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            auto pixel =
                seen[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x)];
            changed = changed || !pixel;
            pixel = true;
        }
    }
    return changed;
}

Minimap draw_minimap(const World& world, const std::vector<bool>& seen) {
    const Map& map = world.map;
    const int width = map.width * kMinimapCell;
    const int height = map.height * kMinimapCell;
    Picture picture{width, height};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t pixel =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x);
            const std::uint8_t tile_type = map.at(x / kMinimapCell, y / kMinimapCell).tile_type;
            const bool ladder =
                tile_type == tile::kLadder || tile_type == tile::kLadderTop || tile_type == tile::kLadder0D;
            // A ladder has a rung to a cell: its upper half lighter. (Finer than that doesn't survive being shown
            // small.)
            Colour colour =
                ladder && (y % kMinimapCell) < kMinimapCell / 2 ? kRung : colour_of(tile_type).value_or(kOpen);
            colour.a = pixel < seen.size() && seen[pixel] ? 255 : 0;
            picture.put(x, y, colour);
        }
    }
    return picture.take();
}

}  // namespace lvr
