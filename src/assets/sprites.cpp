#include "assets/sprites.hpp"

namespace lvr::assets {

namespace {

constexpr std::size_t kPlanes = 4;
constexpr std::size_t kGroupPixels = 8;  // pixels behind one mask byte
constexpr std::size_t kPacked32Size = 32;
constexpr std::uint32_t kFirstPixelBit = 0x80;
constexpr std::uint32_t kNibbleShift = 4;
constexpr std::uint32_t kNibbleMask = 0x0f;

std::size_t unpacked_frame_bytes(std::size_t width, std::size_t height) {
    return width * height / kGroupPixels * (kGroupPixels + 1);
}

std::expected<IndexedImage, Error> decode_packed32_frame(ByteReader& reader) {
    IndexedImage image = make_image(kPacked32Size, kPacked32Size);
    for (std::size_t plane = 0; plane < kPlanes; ++plane) {
        for (std::size_t y = 0; y < kPacked32Size; ++y) {
            const auto mask = reader.u8();
            if (!mask) {
                return std::unexpected{Error::kTruncated};
            }
            std::size_t stored = 0;  // pixels stored so far in this row; two per byte
            std::uint32_t pair = 0;
            for (std::size_t bit = 0; bit < kGroupPixels; ++bit) {
                if ((*mask & (kFirstPixelBit >> bit)) == 0) {
                    continue;
                }
                if (stored % 2 == 0) {
                    const auto next = reader.u8();
                    if (!next) {
                        return std::unexpected{Error::kTruncated};
                    }
                    pair = *next;
                }
                const std::uint32_t value = (stored % 2 == 0) ? (pair >> kNibbleShift) : (pair & kNibbleMask);
                image.set(plane + (kPlanes * bit), y, static_cast<std::uint8_t>(value));
                ++stored;
            }
        }
    }
    return image;
}

}  // namespace

std::string_view to_string(SpriteFormat format) noexcept {
    switch (format) {
        case SpriteFormat::kRaw:
            return "raw";
        case SpriteFormat::kUnpacked:
            return "unpacked";
        case SpriteFormat::kPacked32:
            return "packed32";
        case SpriteFormat::kTiled:
            return "tiled";
    }
    return "unknown";
}

std::expected<IndexedImage, Error> decode_unpacked_frame(std::span<const std::byte> data, std::size_t width,
                                                         std::size_t height) {
    if (width == 0 || height == 0 || width % kPlanes != 0 || (width * height) % (kPlanes * kGroupPixels) != 0) {
        return std::unexpected{Error::kBadSize};
    }
    if (data.size() < unpacked_frame_bytes(width, height)) {
        return std::unexpected{Error::kTruncated};
    }
    IndexedImage image = make_image(width, height);
    const std::size_t plane_width = width / kPlanes;
    const std::size_t plane_pixels = plane_width * height;
    std::size_t pos = 0;
    for (std::size_t plane = 0; plane < kPlanes; ++plane) {
        for (std::size_t group = 0; group < plane_pixels; group += kGroupPixels) {
            const auto mask = std::to_integer<std::uint32_t>(data[pos++]);
            for (std::size_t bit = 0; bit < kGroupPixels; ++bit) {
                const auto value = std::to_integer<std::uint8_t>(data[pos++]);
                if ((mask & (kFirstPixelBit >> bit)) != 0) {
                    const std::size_t index = group + bit;
                    image.set(plane + (kPlanes * (index % plane_width)), index / plane_width, value);
                }
            }
        }
    }
    return image;
}

std::expected<std::vector<SpriteFrame>, Error> decode_sprite_frames(std::span<const std::byte> data,
                                                                    SpriteFormat format,
                                                                    std::span<const FrameRun> runs) {
    if (format == SpriteFormat::kPacked32) {
        return decode_packed32(data);
    }
    if (format == SpriteFormat::kTiled) {
        if (runs.size() != 1) {
            return std::unexpected{Error::kBadSize};
        }
        return decode_tiled(data, runs.front());
    }
    std::vector<SpriteFrame> frames;
    std::size_t offset = 0;
    for (const FrameRun& run : runs) {
        const std::size_t size = (format == SpriteFormat::kRaw) ? std::size_t{run.width} * run.height
                                                                : unpacked_frame_bytes(run.width, run.height);
        for (std::size_t i = 0; i < run.count; ++i) {
            if (data.size() - offset < size) {
                return std::unexpected{Error::kBadSize};
            }
            const auto frame_data = data.subspan(offset, size);
            auto image = (format == SpriteFormat::kRaw) ? decode_planar(frame_data, run.width, run.height)
                                                        : decode_unpacked_frame(frame_data, run.width, run.height);
            if (!image) {
                return std::unexpected{image.error()};
            }
            frames.push_back(SpriteFrame{.offset = offset, .size = size, .image = std::move(*image)});
            offset += size;
        }
    }
    if (offset != data.size()) {
        return std::unexpected{Error::kBadSize};
    }
    return frames;
}

std::expected<std::vector<SpriteFrame>, Error> decode_packed32(std::span<const std::byte> data) {
    ByteReader table{data};
    const auto first = table.u16le();
    if (!first) {
        return std::unexpected{Error::kTruncated};
    }
    if (*first == 0 || *first % 2 != 0) {
        return std::unexpected{Error::kBadOffsetTable};
    }
    std::vector<std::size_t> offsets{*first};
    while (offsets.size() < *first / 2U) {
        const auto offset = table.u16le();
        if (!offset) {
            return std::unexpected{Error::kTruncated};
        }
        offsets.push_back(*offset);
    }
    std::vector<SpriteFrame> frames;
    frames.reserve(offsets.size());
    for (const std::size_t offset : offsets) {
        if (offset >= data.size()) {
            return std::unexpected{Error::kOffsetOutOfRange};
        }
        ByteReader reader{data.subspan(offset)};
        auto image = decode_packed32_frame(reader);
        if (!image) {
            return std::unexpected{image.error()};
        }
        frames.push_back(SpriteFrame{.offset = offset, .size = reader.position(), .image = std::move(*image)});
    }
    return frames;
}

std::expected<std::vector<SpriteFrame>, Error> decode_tiled(std::span<const std::byte> data, const FrameRun& run) {
    constexpr std::size_t kMaskBytes = kTileSize;  // one per 8 stored pixel bytes
    constexpr std::size_t kPlaneWidth = kTileSize / kPlanes;
    const std::size_t sheet_width = std::size_t{run.width} * run.count;
    if (run.width == 0 || run.height == 0 || run.count == 0 || run.width % kTileSize != 0 ||
        run.height % kTileSize != 0) {
        return std::unexpected{Error::kBadSize};
    }
    const std::size_t columns = sheet_width / kTileSize;
    const std::size_t rows = run.height / kTileSize;
    if (data.size() != columns * rows * kMaskedTileBytes) {
        return std::unexpected{Error::kBadSize};
    }

    IndexedImage sheet = make_image(sheet_width, run.height);
    for (std::size_t tile = 0; tile < columns * rows; ++tile) {
        const auto bytes = data.subspan(tile * kMaskedTileBytes, kMaskedTileBytes);
        const std::size_t left = (tile % columns) * kTileSize;
        const std::size_t top = (tile / columns) * kTileSize;
        for (std::size_t i = 0; i < kTileBytes; ++i) {
            const auto mask = std::to_integer<std::uint32_t>(bytes[i / kGroupPixels]);
            if ((mask & (kFirstPixelBit >> (i % kGroupPixels))) == 0) {
                continue;
            }
            const std::size_t plane = i / (kTileBytes / kPlanes);
            const std::size_t index = i % (kTileBytes / kPlanes);  // position within the plane
            sheet.set(left + plane + (kPlanes * (index % kPlaneWidth)), top + (index / kPlaneWidth),
                      std::to_integer<std::uint8_t>(bytes[kMaskBytes + i]));
        }
    }

    std::vector<SpriteFrame> frames;
    frames.reserve(run.count);
    const std::size_t frame_columns = run.width / kTileSize;
    for (std::size_t f = 0; f < run.count; ++f) {
        IndexedImage image = make_image(run.width, run.height);
        for (std::size_t y = 0; y < run.height; ++y) {
            for (std::size_t x = 0; x < run.width; ++x) {
                image.set(x, y, sheet.at((f * run.width) + x, y));
            }
        }
        frames.push_back(SpriteFrame{.offset = f * frame_columns * kMaskedTileBytes,
                                     .size = frame_columns * rows * kMaskedTileBytes,
                                     .image = std::move(image)});
    }
    return frames;
}

}  // namespace lvr::assets
