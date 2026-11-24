#include "assets/graphics.hpp"

namespace lvr::assets {

// --- Palettes ---

std::expected<void, Error> load_palette(Palette& palette, std::span<const std::byte> data, std::size_t first) {
    constexpr std::size_t kBytesPerColour = 3;
    constexpr std::uint32_t kMaxDacValue = 63;
    if (data.size() % kBytesPerColour != 0) {
        return std::unexpected{Error::kBadSize};
    }
    const auto expand = [](std::byte value) {
        const std::uint32_t v = std::to_integer<std::uint32_t>(value) & kMaxDacValue;
        return static_cast<std::uint8_t>((v << 2U) | (v >> 4U));
    };
    for (std::size_t i = 0; i < data.size() / kBytesPerColour && first + i < palette.size(); ++i) {
        palette[first + i] = Rgb{.r = expand(data[(i * kBytesPerColour) + 0]),
                                 .g = expand(data[(i * kBytesPerColour) + 1]),
                                 .b = expand(data[(i * kBytesPerColour) + 2])};
    }
    return {};
}

// --- Indexed images ---

IndexedImage make_image(std::size_t width, std::size_t height) {
    return IndexedImage{.width = width, .height = height, .pixels = std::vector<std::uint8_t>(width * height, 0)};
}

std::expected<IndexedImage, Error> decode_planar(std::span<const std::byte> data, std::size_t width,
                                                 std::size_t height) {
    constexpr std::size_t kPlanes = 4;
    const std::size_t count = width * height;
    if (count % kPlanes != 0) {
        return std::unexpected{Error::kBadSize};
    }
    if (data.size() < count) {
        return std::unexpected{Error::kTruncated};
    }
    const std::size_t plane_size = count / kPlanes;
    IndexedImage image = make_image(width, height);
    for (std::size_t offset = 0; offset < count; ++offset) {
        const std::size_t plane = offset % kPlanes;
        const std::size_t index_in_plane = offset / kPlanes;
        image.pixels[offset] = std::to_integer<std::uint8_t>(data[(plane * plane_size) + index_in_plane]);
    }
    return image;
}

// --- Full-screen images ---

std::expected<IndexedImage, Error> decode_screen_image(std::span<const std::byte> data) {
    constexpr std::size_t kPlanes = 4;
    ByteReader reader{data};
    const auto plane_size = reader.u16le();
    if (!plane_size) {
        return std::unexpected{Error::kTruncated};
    }
    const std::size_t pixel_count = std::size_t{*plane_size} * kPlanes;
    if (reader.remaining() != pixel_count || pixel_count % kScreenWidth != 0) {
        return std::unexpected{Error::kBadSize};
    }
    return decode_planar(data.subspan(reader.position()), kScreenWidth, pixel_count / kScreenWidth);
}

// --- Tiles and metatiles ---

std::expected<std::vector<IndexedImage>, Error> decode_tileset(std::span<const std::byte> data) {
    if (data.size() % kTileBytes != 0) {
        return std::unexpected{Error::kBadSize};
    }
    std::vector<IndexedImage> tiles;
    tiles.reserve(data.size() / kTileBytes);
    for (std::size_t offset = 0; offset < data.size(); offset += kTileBytes) {
        auto tile = decode_planar(data.subspan(offset, kTileBytes), kTileSize, kTileSize);
        if (!tile) {
            return std::unexpected{tile.error()};
        }
        tiles.push_back(std::move(*tile));
    }
    return tiles;
}

std::expected<std::vector<Metatile>, Error> decode_metatiles(std::span<const std::byte> data) {
    constexpr std::uint32_t kTileShift = 6;
    constexpr std::uint32_t kFlagsMask = 0x3f;
    if (data.size() % kMetatileBytes != 0) {
        return std::unexpected{Error::kBadSize};
    }
    ByteReader reader{data};
    std::vector<Metatile> metatiles(data.size() / kMetatileBytes);
    for (Metatile& metatile : metatiles) {
        for (TileRef& part : metatile.parts) {
            const std::uint32_t value = *reader.u16le();  // size checked above
            part.tile = static_cast<std::uint16_t>(value >> kTileShift);
            part.flags = static_cast<std::uint8_t>(value & kFlagsMask);
        }
    }
    return metatiles;
}

std::expected<void, Error> draw_metatile(IndexedImage& target, std::size_t x, std::size_t y, const Metatile& metatile,
                                         std::span<const IndexedImage> tileset) {
    constexpr std::size_t kLast = kTileSize - 1;
    if (x + kMetatileSize > target.width || y + kMetatileSize > target.height) {
        return std::unexpected{Error::kOutOfBounds};
    }
    for (std::size_t part = 0; part < metatile.parts.size(); ++part) {
        const TileRef& ref = metatile.parts[part];
        if (ref.tile >= tileset.size()) {
            return std::unexpected{Error::kBadReference};
        }
        const IndexedImage& tile = tileset[ref.tile];
        const std::size_t origin_x = x + ((part % 2) * kTileSize);
        const std::size_t origin_y = y + ((part / 2) * kTileSize);
        for (std::size_t ty = 0; ty < kTileSize; ++ty) {
            for (std::size_t tx = 0; tx < kTileSize; ++tx) {
                const std::size_t sx = ref.flip_h() ? kLast - tx : tx;
                const std::size_t sy = ref.flip_v() ? kLast - ty : ty;
                target.set(origin_x + tx, origin_y + ty, tile.at(sx, sy));
            }
        }
    }
    return {};
}

// --- Tilemaps ---

std::expected<Tilemap, Error> decode_tilemap(std::span<const std::byte> data, std::size_t width, std::size_t height) {
    constexpr std::uint32_t kMetatileMask = 0x1ff;
    constexpr std::uint32_t kAttributeShift = 9;
    if (data.size() > width * height * 2 || data.size() % 2 != 0) {
        return std::unexpected{Error::kBadSize};
    }
    ByteReader reader{data};
    Tilemap map{.width = width,
                .height = height,
                .cells = std::vector<MapCell>(width * height),
                .stored_cells = data.size() / 2};
    for (MapCell& cell : std::span{map.cells}.first(map.stored_cells)) {
        const std::uint32_t value = *reader.u16le();  // size checked above
        cell.metatile = static_cast<std::uint16_t>(value & kMetatileMask);
        cell.attributes = static_cast<std::uint8_t>(value >> kAttributeShift);
    }
    return map;
}

std::expected<IndexedImage, Error> render_tilemap(const Tilemap& map, std::span<const Metatile> metatiles,
                                                  std::span<const IndexedImage> tileset) {
    IndexedImage image = make_image(map.width * kMetatileSize, map.height * kMetatileSize);
    for (std::size_t my = 0; my < map.height; ++my) {
        for (std::size_t mx = 0; mx < map.width; ++mx) {
            if ((my * map.width) + mx >= map.stored_cells) {
                return image;  // missing cells at the end stay colour 0
            }
            const MapCell& cell = map.at(mx, my);
            if (cell.metatile >= metatiles.size()) {
                return std::unexpected{Error::kBadReference};
            }
            const auto drawn =
                draw_metatile(image, mx * kMetatileSize, my * kMetatileSize, metatiles[cell.metatile], tileset);
            if (!drawn) {
                return std::unexpected{drawn.error()};
            }
        }
    }
    return image;
}

}  // namespace lvr::assets
