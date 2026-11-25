#include "lvr/render.hpp"

#include <algorithm>
#include <atomic>
#include <format>
#include <utility>

#include "lvr/sprites.hpp"

namespace lvr {

namespace {

// A pixel, if it is on screen above row `bottom`.
void put(Screen& screen, int x, int y, std::uint8_t colour, int bottom) {
    if (x >= 0 && y >= 0 && x < kScreenWidth && y < bottom) {
        screen.pixels[(static_cast<std::size_t>(y) * kScreenWidth) + static_cast<std::size_t>(x)] = colour;
    }
}

// A picture as it is (colour 0 too), clipped to the screen.
void blit(Screen& screen, const assets::IndexedImage& image, int x, int y, int bottom = kScreenHeight) {
    for (std::size_t iy = 0; iy < image.height; ++iy) {
        for (std::size_t ix = 0; ix < image.width; ++ix) {
            put(screen, x + static_cast<int>(ix), y + static_cast<int>(iy), image.at(ix, iy), bottom);
        }
    }
}

// The map's tiles in view. `foreground`: only the pixels the tileset's masks put in front of the sprites, of the
// tiles marked so.
void draw_tiles(Screen& screen, const View& view, const SceneGraphics& scene, bool foreground) {
    if (view.map == nullptr || view.map->width == 0) {
        return;
    }
    const Map& map = *view.map;
    const int first_column = std::max(0, view.camera_x / kCellSize);
    const int first_row = std::max(0, view.camera_y / kCellSize);
    const int last_column = std::min(map.width - 1, (view.camera_x + kScreenWidth - 1) / kCellSize);
    const int last_row = std::min(map.height - 1, (view.camera_y + kPlayfieldHeight - 1) / kCellSize);
    for (int row = first_row; row <= last_row; ++row) {
        for (int column = first_column; column <= last_column; ++column) {
            std::size_t metatile = map.at(column, row).metatile;
            if (metatile >= scene.metatiles.size()) {
                metatile &= 0x1ffU;  // as the original's tables wrap
            }
            if (metatile >= scene.metatiles.size()) {
                continue;
            }
            for (std::size_t part = 0; part < 4; ++part) {
                const assets::TileRef& ref = scene.metatiles[metatile].parts.at(part);
                if (ref.tile >= scene.tileset.size() || (foreground && !ref.foreground())) {
                    continue;
                }
                const assets::IndexedImage& tile = scene.tileset[ref.tile];
                const int left = (column * kCellSize) + (static_cast<int>(part % 2) * kTileSize) - view.camera_x;
                const int top = (row * kCellSize) + (static_cast<int>(part / 2) * kTileSize) - view.camera_y;
                for (std::size_t ty = 0; ty < kTileSize; ++ty) {
                    for (std::size_t tx = 0; tx < kTileSize; ++tx) {
                        const std::size_t sx = ref.flip_h() ? kTileSize - 1 - tx : tx;
                        const std::size_t sy = ref.flip_v() ? kTileSize - 1 - ty : ty;
                        if (foreground && !in_front(scene.tile_masks, ref.tile, sx, sy)) {
                            continue;
                        }
                        put(screen, left + static_cast<int>(tx), top + static_cast<int>(ty), tile.at(sx, sy),
                            kPlayfieldHeight);
                    }
                }
            }
        }
    }
}

void draw_sprite(Screen& screen, const View& view, const DrawnSprite& sprite) {
    if (sprite.image == nullptr) {
        return;
    }
    const assets::IndexedImage& image = *sprite.image;
    for (std::size_t py = 0; py < image.height; ++py) {
        for (std::size_t px = 0; px < image.width; ++px) {
            const std::size_t sx = sprite.mirrored ? image.width - 1 - px : px;
            const std::size_t sy = sprite.flipped ? image.height - 1 - py : py;
            if (const std::uint8_t colour = image.at(sx, sy); colour != 0) {
                put(screen, sprite.x - view.camera_x + static_cast<int>(px),
                    sprite.y - view.camera_y + static_cast<int>(py), colour, kPlayfieldHeight);
            }
        }
    }
}

void draw_text(Screen& screen, const View& view, const SharedGraphics& shared) {
    for (int row = 0; row < std::min(view.height / kTileSize, kTextRows); ++row) {
        for (int column = 0; column < std::min(view.width / kTileSize, kTextColumns); ++column) {
            const std::uint8_t character =
                view.text[(static_cast<std::size_t>(row) * kTextColumns) + static_cast<std::size_t>(column)];
            if (character < kFirstGlyph || std::size_t{character} - kFirstGlyph >= shared.font.size()) {
                continue;
            }
            const assets::IndexedImage& glyph = shared.font[std::size_t{character} - kFirstGlyph];
            for (std::size_t gy = 0; gy < glyph.height; ++gy) {
                for (std::size_t gx = 0; gx < glyph.width; ++gx) {
                    if (const std::uint8_t colour = glyph.at(gx, gy); colour != 0) {
                        put(screen, view.text_x + (column * kTileSize) + static_cast<int>(gx),
                            view.text_y + (row * kTileSize) + static_cast<int>(gy), colour, kPlayfieldHeight);
                    }
                }
            }
        }
    }
}

void draw_hud(Screen& screen, const HudView& hud, const SharedGraphics& shared) {
    blit(screen, shared.hud_panel, 0, kPlayfieldHeight);
    for (std::size_t who = 0; who < hud.vikings.size(); ++who) {
        const HudView::Viking& viking = hud.vikings[who];
        if (!viking.shown) {
            continue;
        }
        const std::size_t portrait = (static_cast<std::size_t>(viking.portrait) * 3) + who;
        if (portrait < shared.portraits.size()) {
            blit(screen, shared.portraits[portrait], kPortraitAt.at(who)[0], kPlayfieldHeight + kPortraitAt.at(who)[1]);
        }
        if (viking.health >= 0 && std::cmp_less(viking.health, shared.health.size())) {
            blit(screen, shared.health[static_cast<std::size_t>(viking.health)], kHealthAt.at(who)[0],
                 kPlayfieldHeight + kHealthAt.at(who)[1]);
        }
    }
    // The letters of a name, not their cells, in the middle of a portrait's column; grey for one watching.
    const auto name_at = [&](const std::string& name, std::size_t who, int row, bool grey) {
        int x = kPortraitAt.at(who)[0] + kPortraitMiddle - (static_cast<int>(name.size()) * kTileSize / 2);
        for (const char each : name) {
            const auto character = static_cast<std::uint8_t>(each);
            if (character >= kFirstGlyph && std::size_t{character} - kFirstGlyph < shared.font.size()) {
                const assets::IndexedImage& glyph = shared.font[std::size_t{character} - kFirstGlyph];
                for (std::size_t gy = 0; gy < glyph.height; ++gy) {
                    for (std::size_t gx = 0; gx < glyph.width; ++gx) {
                        if (const std::uint8_t colour = glyph.at(gx, gy); colour == 1 || colour == 2) {
                            put(screen, x + static_cast<int>(gx), kPlayfieldHeight + row + static_cast<int>(gy),
                                grey && colour == 2 ? kNameGrey : colour, kScreenHeight);
                        }
                    }
                }
            }
            x += kTileSize;
        }
    };
    for (std::size_t who = 0; who < hud.names.size(); ++who) {
        name_at(hud.names.at(who), who, kNameRow, false);
        for (std::size_t place = 0; place < hud.watching.at(who).size(); ++place) {
            name_at(hud.watching.at(who).at(place), who, kWatchingRow - (static_cast<int>(place) * kNameLine), true);
        }
    }
    for (std::size_t square = 0; square < kSquares; ++square) {
        const int x = kSquareAt.at(square)[0];
        const int y = kPlayfieldHeight + kSquareAt.at(square)[1];
        if (const std::optional<int> icon = hud.icons.at(square);
            icon && *icon >= 0 && std::cmp_less(*icon, shared.items.size())) {
            blit(screen, shared.items[static_cast<std::size_t>(*icon)], x, y);
        }
        if (hud.picker.at(square) && kPickerIcon < shared.items.size()) {
            const assets::IndexedImage& frame = shared.items[kPickerIcon];
            for (std::size_t py = 0; py < frame.height; ++py) {
                for (std::size_t px = 0; px < frame.width; ++px) {
                    if (picker_pixel(px, py)) {
                        put(screen, x + static_cast<int>(px), y + static_cast<int>(py), frame.at(px, py),
                            kScreenHeight);
                    }
                }
            }
        }
    }
}

}  // namespace

std::uint32_t next_graphics_id() noexcept {
    static std::atomic<std::uint32_t> last{0};
    return ++last;
}

std::expected<SharedGraphics, std::string> load_shared_graphics(const assets::Archive& archive) {
    constexpr std::uint16_t kFontEntry = 2;
    constexpr std::uint16_t kFirstPortraitEntry = 4;
    constexpr std::uint16_t kLastPortraitEntry = 12;
    constexpr std::uint16_t kHealthEntry = 13;
    constexpr std::uint16_t kItemsEntry = 14;
    SharedGraphics shared;
    const auto frames =
        [&archive](std::uint16_t entry) -> std::expected<std::vector<assets::IndexedImage>, std::string> {
        auto sprites = assets::load_sprites(archive, entry);
        if (!sprites) {
            return std::unexpected{sprites.error()};
        }
        std::vector<assets::IndexedImage> images;
        for (assets::SpriteFrame& frame : sprites->frames) {
            images.push_back(std::move(frame.image));
        }
        return images;
    };
    auto font = frames(kFontEntry);
    auto health = frames(kHealthEntry);
    auto items = frames(kItemsEntry);
    auto panel = assets::load_image(archive, assets::kImages[0]);
    if (!font || !health || !items || !panel) {
        return std::unexpected{!font ? font.error()
                                     : (!health ? health.error() : (!items ? items.error() : panel.error()))};
    }
    shared.font = std::move(*font);
    shared.health = std::move(*health);
    shared.items = std::move(*items);
    shared.hud_panel = std::move(panel->image);
    for (std::uint16_t entry = kFirstPortraitEntry; entry <= kLastPortraitEntry; ++entry) {
        auto portrait = frames(entry);
        if (!portrait || portrait->empty()) {
            return std::unexpected{portrait ? std::format("entry {} has no frames", entry) : portrait.error()};
        }
        shared.portraits.push_back(std::move(portrait->front()));
    }
    return shared;
}

float moved_since(int by, int phase, float between) noexcept {
    if (phase < 0 || phase > 2) {
        return 0;
    }
    int shifted = 0;  // what the tick's pictures up to this one moved it by
    for (int picture = 0; picture <= phase; ++picture) {
        shifted += share_of_move(by, picture);
    }
    return (static_cast<float>(by) * (static_cast<float>(phase) + between) / 3.0F) - static_cast<float>(shifted);
}

std::array<float, 2> camera_moved(const Motion& motion, float between) noexcept {
    if (motion.phase < 0 || motion.phase > 2 || !motion.scroll_from) {
        return {};
    }
    const float share = (static_cast<float>(motion.phase) + between) / 3.0F;
    const auto axis = [share](int from, int to, int now) {
        return static_cast<float>(from) + (static_cast<float>(to - from) * share) - static_cast<float>(now);
    };
    const auto& [from_x, from_y] = *motion.scroll_from;
    return {axis(from_x, motion.scroll_to[0], motion.scroll[0]), axis(from_y, motion.scroll_to[1], motion.scroll[1])};
}

assets::Palette darkened(const assets::Palette& palette, std::array<std::uint8_t, 3> by, bool all) {
    if (by[0] == 0 && by[1] == 0 && by[2] == 0) {
        return palette;
    }
    // In the VGA's 6 bits a colour, as the original counts; shown as 8 bits.
    const auto less = [](std::uint8_t colour, unsigned amount) {
        const unsigned level = colour >> 2U;
        const unsigned out = level >= amount ? level - amount : 0U;
        return static_cast<std::uint8_t>((out << 2U) | (out >> 4U));
    };
    assets::Palette shown = palette;
    for (std::size_t i = 0; i < shown.size(); ++i) {
        if (!all && i >= 1 && i <= 15) {
            continue;
        }
        shown.at(i) = {
            .r = less(palette.at(i).r, by[0]), .g = less(palette.at(i).g, by[1]), .b = less(palette.at(i).b, by[2])};
    }
    return shown;
}

void draw(Screen& screen, const View& view, const SceneGraphics& scene, const SharedGraphics& shared) {
    screen.palette = darkened(view.palette, view.darken, view.darken_all);
    if (view.colour3) {
        screen.palette[3] = *view.colour3;
    }
    std::ranges::fill(screen.pixels, std::uint8_t{0});
    if (scene.picture) {
        blit(screen, *scene.picture, -view.camera_x, kPictureTop - view.camera_y, kPlayfieldHeight);
    }
    draw_tiles(screen, view, scene, false);
    for (const DrawnSprite& sprite : view.sprites) {
        draw_sprite(screen, view, sprite);
    }
    draw_tiles(screen, view, scene, true);
    draw_text(screen, view, shared);
    if (scene.hud_picture) {
        // Its first 64 rows, in the HUD's place.
        for (std::size_t y = 0; y < std::min<std::size_t>(kScreenHeight - kPlayfieldHeight, scene.hud_picture->height);
             ++y) {
            for (std::size_t x = 0; x < std::min<std::size_t>(kScreenWidth, scene.hud_picture->width); ++x) {
                put(screen, static_cast<int>(x), kPlayfieldHeight + static_cast<int>(y), scene.hud_picture->at(x, y),
                    kScreenHeight);
            }
        }
    }
    if (view.hud) {
        draw_hud(screen, *view.hud, shared);
    }
}

}  // namespace lvr
