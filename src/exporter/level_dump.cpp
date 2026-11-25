#include "exporter/level_dump.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <optional>
#include <utility>

#include "lvr/backdrop.hpp"
#include "lvr/camera.hpp"

namespace lvr::exporter {

namespace {

constexpr int kGlyphSize = 8;

bool inside(const assets::IndexedImage& image, int x, int y) noexcept {
    return x >= 0 && y >= 0 && std::cmp_less(x, image.width) && std::cmp_less(y, image.height);
}

void put(assets::IndexedImage& image, int x, int y, std::uint8_t colour) {
    if (inside(image, x, y)) {
        image.set(static_cast<std::size_t>(x), static_cast<std::size_t>(y), colour);
    }
}

void fill(assets::IndexedImage& image, const Box& box, std::uint8_t colour) {
    for (int y = box.top; y <= box.bottom; ++y) {
        for (int x = box.left; x <= box.right; ++x) {
            put(image, x, y, colour);
        }
    }
}

// A hue (0-359) at full strength.
assets::Rgb from_hue(int hue) noexcept {
    const double h = hue / 60.0;
    const double x = 1.0 - std::abs(std::fmod(h, 2.0) - 1.0);
    const auto level = [](double value) { return static_cast<std::uint8_t>(64 + std::lround(value * 191)); };
    const auto rgb = [&](double r, double g, double b) { return assets::Rgb{level(r), level(g), level(b)}; };
    switch (static_cast<int>(h) % 6) {
        case 0:
            return rgb(1, x, 0);
        case 1:
            return rgb(x, 1, 0);
        case 2:
            return rgb(0, 1, x);
        case 3:
            return rgb(0, x, 1);
        case 4:
            return rgb(x, 0, 1);
        default:
            return rgb(1, 0, x);
    }
}

}  // namespace

// --- Drawing ---

void draw_text(assets::IndexedImage& image, std::span<const assets::IndexedImage> font, int x, int y,
               std::string_view text, std::uint8_t ink, std::uint8_t shadow) {
    constexpr int kFirstCharacter = 0x20;
    constexpr int kGlyphOffset = 0x10;  // glyph = character - 0x10, as the game's text layer has it
    for (const char c : text) {
        int character = static_cast<unsigned char>(c);
        if (character >= 'a' && character <= 'z') {
            character -= 'a' - 'A';
        }
        const auto glyph = static_cast<std::size_t>(character - kGlyphOffset);
        if (character > kFirstCharacter && glyph < font.size()) {
            const assets::IndexedImage& picture = font[glyph];
            for (std::size_t py = 0; py < picture.height; ++py) {
                for (std::size_t px = 0; px < picture.width; ++px) {
                    const std::uint8_t pixel = picture.at(px, py);
                    if (pixel == kFontInk || pixel == kFontShadow) {
                        put(image, x + static_cast<int>(px), y + static_cast<int>(py),
                            pixel == kFontInk ? ink : shadow);
                    }
                }
            }
        }
        x += kGlyphSize;
    }
}

void draw_outline(assets::IndexedImage& image, const Box& box, std::uint8_t colour) {
    for (int x = box.left; x <= box.right; ++x) {
        put(image, x, box.top, colour);
        put(image, x, box.bottom, colour);
    }
    for (int y = box.top; y <= box.bottom; ++y) {
        put(image, box.left, y, colour);
        put(image, box.right, y, colour);
    }
}

// --- The collision map ---

assets::Rgb tile_type_colour(std::uint8_t tile_type) noexcept {
    switch (tile_type) {
        case 0x00:
            return {.r = 12, .g = 12, .b = 20};
        case 0x03:
            return {.r = 36, .g = 36, .b = 60};
        case 0x0c:
            return {.r = 56, .g = 36, .b = 72};
        case 0x01:
            return {.r = 128, .g = 128, .b = 128};
        case 0x02:
            return {.r = 168, .g = 168, .b = 168};
        case 0x04:
            return {.r = 208, .g = 208, .b = 128};
        case 0x05:
            return {.r = 112, .g = 112, .b = 152};
        case 0x20:
            return {.r = 88, .g = 88, .b = 88};
        // The types the levels use that the engine's own code doesn't name: strong colours, none of them green.
        case 0x0a:
            return {.r = 255, .g = 150, .b = 0};
        case 0x0b:
            return {.r = 176, .g = 104, .b = 40};
        case 0x0d:
            return {.r = 176, .g = 136, .b = 255};
        case 0x10:
            return {.r = 232, .g = 40, .b = 40};
        case 0x11:
            return {.r = 255, .g = 112, .b = 184};
        case 0x12:
            return {.r = 255, .g = 88, .b = 0};
        case 0x13:
            return {.r = 0, .g = 208, .b = 232};
        case 0x14:
            return {.r = 64, .g = 120, .b = 255};
        case 0x15:
            return {.r = 152, .g = 64, .b = 255};
        case 0x16:
            return {.r = 232, .g = 0, .b = 200};
        case 0x17:
            return {.r = 255, .g = 232, .b = 0};
        default:
            break;
    }
    if (terrain(tile_type) == Terrain::kSlope) {  // greens, lighter by slope
        const auto step = static_cast<std::uint8_t>((tile_type - tile::kFirstSlope) * 10);
        return {.r = static_cast<std::uint8_t>(step * 2U), .g = static_cast<std::uint8_t>(100U + step), .b = 40};
    }
    return from_hue(180 + ((tile_type * 47) % 180));  // no level uses these: from cyan through blue to red
}

CollisionMap collision_map(const Map& map, std::span<const TerrainEntity> terrain_entities,
                           std::span<const assets::IndexedImage> font) {
    constexpr int kLineHeight = 10;
    constexpr int kColumnWidth = 232;  // a swatch and 28 characters
    constexpr int kMargin = 4;

    CollisionMap result;
    std::map<std::uint8_t, std::size_t> cells;
    for (const Cell& cell : map.cells) {
        ++cells[cell.tile_type];
    }
    for (const auto& [tile_type, count] : cells) {
        result.legend.push_back(TileTypeUse{.tile_type = tile_type,
                                            .name = tile_type_name(tile_type),
                                            .cells = count,
                                            .colour = tile_type_colour(tile_type)});
    }
    for (std::size_t tile_type = 0; tile_type < tile::kCount; ++tile_type) {
        result.palette.at(tile_type) = tile_type_colour(static_cast<std::uint8_t>(tile_type));
    }
    result.palette.at(kOutlineColour) = {.r = 255, .g = 255, .b = 255};
    result.palette.at(kTextColour) = {.r = 232, .g = 232, .b = 232};
    result.palette.at(kLegendBackground) = {.r = 0, .g = 0, .b = 0};

    // The legend's lines: the types, then the outline if any entity has one.
    const int lines = static_cast<int>(result.legend.size()) + (terrain_entities.empty() ? 0 : 1);
    const int width = std::max(map.pixel_width(), kColumnWidth);
    const int columns = width / kColumnWidth;
    const int rows = (lines + columns - 1) / columns;
    const int legend_top = map.pixel_height();
    const int height = legend_top + (2 * kMargin) + (rows * kLineHeight);
    result.image = assets::make_image(static_cast<std::size_t>(width), static_cast<std::size_t>(height));
    fill(result.image, {.left = 0, .top = 0, .right = width - 1, .bottom = height - 1}, kLegendBackground);

    for (int row = 0; row < map.height; ++row) {
        for (int column = 0; column < map.width; ++column) {
            fill(result.image,
                 {.left = column * kCellSize,
                  .top = row * kCellSize,
                  .right = (column * kCellSize) + kCellSize - 1,
                  .bottom = (row * kCellSize) + kCellSize - 1},
                 map.at(column, row).tile_type);
        }
    }
    const auto draw_entity = [&](const Box& box, std::uint8_t tile_type) {
        draw_outline(result.image, box, kOutlineColour);
        draw_outline(result.image,
                     {.left = box.left + 1, .top = box.top + 1, .right = box.right - 1, .bottom = box.bottom - 1},
                     tile_type);
    };
    for (const TerrainEntity& entity : terrain_entities) {
        draw_entity(entity.box, entity.tile_type);
    }

    const auto line_at = [&](int line) {
        return std::pair{kMargin + ((line / rows) * kColumnWidth),
                         legend_top + kMargin + ((line % rows) * kLineHeight)};
    };
    int line = 0;
    for (const TileTypeUse& use : result.legend) {
        const auto [x, y] = line_at(line++);
        const Box swatch{.left = x, .top = y, .right = x + 7, .bottom = y + 7};
        fill(result.image, swatch, use.tile_type);
        draw_outline(result.image, swatch, kTextColour);
        draw_text(result.image, font, x + 12, y,
                  std::format("{:02X} {:<16}{:6}", use.tile_type, use.name.empty() ? "?" : use.name, use.cells),
                  kTextColour, kLegendBackground);
    }
    if (!terrain_entities.empty()) {
        const auto [x, y] = line_at(line);
        draw_entity({.left = x, .top = y, .right = x + 7, .bottom = y + 7}, tile::kGround);
        draw_text(result.image, font, x + 12, y, "ENTITY, AS ITS TILE TYPE", kTextColour, kLegendBackground);
    }
    return result;
}

// --- The world picture ---

assets::IndexedImage world_picture(const Level& level, const assets::IndexedImage& map_image,
                                   std::span<const assets::IndexedImage> font) {
    assets::IndexedImage world = map_image;
    for (const Placement& placement : level.placements) {
        const Box box{.left = placement.x - (placement.width / 2),
                      .top = placement.y - (placement.height / 2),
                      .right = placement.x + (placement.width / 2) - 1,
                      .bottom = placement.y + (placement.height / 2) - 1};
        // In the colours the game's own text has (the HUD's part of every level's palette).
        draw_outline(world, box, kFontInk);
        draw_text(world, font, box.left + 2, box.top + 2, std::format("{:02X}", placement.kind), kFontInk, kFontShadow);
    }
    return world;
}

// --- The SNES backdrop behind the map ---

BackdropPreview backdrop_preview(const Map& map, const assets::IndexedImage& map_image,
                                 const assets::IndexedImage& backdrop, std::uint16_t scroll_x, std::uint16_t scroll_y) {
    BackdropPreview result{.image = map_image, .shown = 0};
    if (backdrop.width == 0 || backdrop.height == 0) {
        return result;
    }
    Camera camera;
    set_camera_limits(camera, map.width, map.height);
    const auto width = static_cast<int>(backdrop.width);
    const auto height = static_cast<int>(backdrop.height);
    // The camera for the screen a pixel is in, and where the backdrop starts in that screen.
    const auto screen = [](int at, int view, int limit) {
        return std::clamp((at / view) * view, 0, std::max(limit, 0));
    };
    for (std::size_t y = 0; y < result.image.height; ++y) {
        const int camera_y = screen(static_cast<int>(y), camera.view_height, camera.max_y);
        const auto top = static_cast<int>(backdrop_offset(scroll_y, camera_y, 0, height));  // whole pixels, as the SNES
        for (std::size_t x = 0; x < result.image.width; ++x) {
            std::uint8_t& pixel = result.image.pixels[(y * result.image.width) + x];
            if (!shows_backdrop(pixel)) {
                continue;
            }
            const int camera_x = screen(static_cast<int>(x), camera.view_width, camera.max_x);
            const auto left = static_cast<int>(backdrop_offset(scroll_x, camera_x, 0, width));
            pixel = backdrop.at(static_cast<std::size_t>((left + static_cast<int>(x) - camera_x) % width),
                                static_cast<std::size_t>((top + static_cast<int>(y) - camera_y) % height));
            ++result.shown;
        }
    }
    return result;
}

}  // namespace lvr::exporter
