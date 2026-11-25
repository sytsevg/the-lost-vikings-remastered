#include "assets/snes.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <optional>
#include <system_error>

#include "assets/data_dat.hpp"
#include "assets/hash.hpp"
#include "assets/level.hpp"

namespace lvr::assets {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kSnesTileBytes = 32;

constexpr std::size_t kSnesQuadBytes = 8;
constexpr std::size_t kSnesQuadSize = 16;
constexpr unsigned kIndexMask = 0x3ffU;
constexpr std::size_t kBackdropFields = 0x34;
constexpr std::size_t kHeaderFixedSize = 0x43;

std::uint16_t word_at(std::span<const std::byte> data, std::size_t at) {
    return static_cast<std::uint16_t>(std::to_integer<unsigned>(data[at]) |
                                      (std::to_integer<unsigned>(data[at + 1]) << 8U));
}

template <typename T>
std::expected<T, std::string> named(std::expected<T, Error> value, std::string_view what, std::size_t chunk) {
    return std::move(value).transform_error(
        [&](Error error) { return std::format("SNES chunk {} ({}): {}", chunk, what, describe(error)); });
}

}  // namespace

// --- The ROM image ---

std::expected<SnesRom, std::string> SnesRom::open(const fs::path& dir) {
    std::optional<fs::path> found;
    std::error_code ec;
    for (const auto& item : fs::directory_iterator{dir, ec}) {
        std::string extension = item.path().extension().string();
        std::ranges::transform(extension, extension.begin(),
                               [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; });
        if (item.is_regular_file(ec) && extension == ".sfc") {
            if (found) {
                return std::unexpected{std::format("more than one SNES ROM image (*.sfc) in {}", dir.string())};
            }
            found = item.path();
        }
    }
    if (!found) {
        return std::unexpected{std::format("no SNES ROM image (*.sfc) in {}", dir.string())};
    }
    std::ifstream in{*found, std::ios::binary};
    std::vector<std::byte> bytes;
    if (in) {
        in.seekg(0, std::ios::end);
        const std::streamoff size = in.tellg();
        in.seekg(0, std::ios::beg);
        bytes.resize(static_cast<std::size_t>(std::max<std::streamoff>(size, 0)));
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!in) {
        return std::unexpected{std::format("cannot read {}", found->string())};
    }
    return from_bytes(std::move(bytes)).transform_error([&](const std::string& error) {
        return std::format("{}: {}", found->string(), error);
    });
}

std::expected<SnesRom, std::string> SnesRom::from_bytes(std::vector<std::byte> image) {
    const std::uint64_t hash = fnv1a64(image);
    const bool known = std::ranges::any_of(
        kKnownSnesRoms, [&](const SnesRelease& r) { return r.size == image.size() && r.hash == hash; });
    if (!known) {
        return std::unexpected{
            std::format("unknown SNES ROM image: {} bytes, fnv1a64={:016x}\n"
                        "Only known images are supported (docs/formats/snes.md).",
                        image.size(), hash)};
    }
    return parse(std::move(image)).transform_error([](Error error) {
        return std::format("the chunk table: {}", describe(error));
    });
}

std::expected<SnesRom, Error> SnesRom::parse(std::vector<std::byte> image) {
    if (image.size() < kSnesChunkTable + 4) {
        return std::unexpected{Error::kTruncated};
    }
    const auto offset_at = [&image](std::size_t index) {
        const std::size_t at = kSnesChunkTable + (4 * index);
        return kSnesChunkTable + (std::size_t{word_at(image, at + 2)} * kSnesBankSize) + word_at(image, at);
    };
    // The first chunk starts where the table ends.
    const std::size_t first = offset_at(0);
    if (first <= kSnesChunkTable || first > image.size() || (first - kSnesChunkTable) % 4 != 0) {
        return std::unexpected{Error::kBadOffsetTable};
    }
    std::vector<std::size_t> offsets;
    for (std::size_t index = 0; index < (first - kSnesChunkTable) / 4; ++index) {
        const std::size_t offset = offset_at(index);
        if (offset >= image.size()) {
            return std::unexpected{Error::kOffsetOutOfRange};
        }
        if (!offsets.empty() && offset <= offsets.back()) {
            return std::unexpected{Error::kOffsetsNotAscending};
        }
        offsets.push_back(offset);
    }
    return SnesRom{std::move(image), std::move(offsets)};
}

std::expected<std::span<const std::byte>, Error> SnesRom::raw_chunk(std::size_t index) const noexcept {
    if (index >= offsets_.size()) {
        return std::unexpected{Error::kIndexOutOfRange};
    }
    const std::size_t end = index + 1 < offsets_.size() ? offsets_[index + 1] : image_.size();
    return std::span<const std::byte>{image_}.subspan(offsets_[index], end - offsets_[index]);
}

std::expected<std::vector<std::byte>, Error> SnesRom::chunk(std::size_t index) const {
    const auto raw = raw_chunk(index);
    if (!raw) {
        return std::unexpected{raw.error()};
    }
    if (std::ranges::contains(kSnesStoredChunks, index)) {
        return std::vector<std::byte>(raw->begin(), raw->end());
    }
    auto decoded = lzss_decompress(*raw, LzssHeader::kSize);
    if (!decoded) {
        return std::unexpected{decoded.error()};
    }
    return std::move(decoded->data);
}

// --- Backdrops ---

std::expected<BackdropHeader, Error> parse_backdrop_header(std::span<const std::byte> header) {
    if (header.size() < kHeaderFixedSize) {
        return std::unexpected{Error::kTruncated};
    }
    const std::size_t at = kBackdropFields;
    return BackdropHeader{.width = word_at(header, at),
                          .height = word_at(header, at + 2),
                          .map_chunk = word_at(header, at + 5),
                          .tiles_chunk = word_at(header, at + 7),
                          .quads_chunk = word_at(header, at + 9),
                          .scroll_x = word_at(header, at + 11),
                          .scroll_y = word_at(header, at + 13)};
}

std::expected<std::vector<IndexedImage>, Error> decode_snes_tiles(std::span<const std::byte> data) {
    if (data.size() % kSnesTileBytes != 0) {
        return std::unexpected{Error::kBadSize};
    }
    std::vector<IndexedImage> tiles;
    tiles.reserve(data.size() / kSnesTileBytes);
    for (std::size_t at = 0; at < data.size(); at += kSnesTileBytes) {
        IndexedImage tile = make_image(kTileSize, kTileSize);
        for (std::size_t y = 0; y < kTileSize; ++y) {
            // Two bytes hold bits 0 and 1 of the row's pixels, two more, 16 bytes on, bits 2 and 3.
            const std::array<unsigned, 4> planes{
                std::to_integer<unsigned>(data[at + (2 * y)]), std::to_integer<unsigned>(data[at + (2 * y) + 1]),
                std::to_integer<unsigned>(data[at + 16 + (2 * y)]), std::to_integer<unsigned>(data[at + 17 + (2 * y)])};
            for (std::size_t x = 0; x < kTileSize; ++x) {
                unsigned value = 0;
                for (unsigned plane = 0; plane < 4; ++plane) {
                    value |= ((planes[plane] >> (7U - static_cast<unsigned>(x))) & 1U) << plane;
                }
                tile.set(x, y, static_cast<std::uint8_t>(value));
            }
        }
        tiles.push_back(std::move(tile));
    }
    return tiles;
}

std::expected<void, Error> load_snes_palette(Palette& palette, std::span<const std::byte> data, std::size_t first) {
    if (data.size() % 2 != 0) {
        return std::unexpected{Error::kBadSize};
    }
    const auto wide = [](unsigned v) { return static_cast<std::uint8_t>((v << 3U) | (v >> 2U)); };
    for (std::size_t i = 0; i < data.size() / 2 && first + i < palette.size(); ++i) {
        const unsigned word = word_at(data, 2 * i);
        palette[first + i] = Rgb{.r = wide(word & 31U), .g = wide((word >> 5U) & 31U), .b = wide((word >> 10U) & 31U)};
    }
    return {};
}

std::expected<IndexedImage, Error> draw_snes_map(std::span<const std::byte> map, std::size_t width, std::size_t height,
                                                 std::span<const std::byte> quads,
                                                 std::span<const IndexedImage> tiles) {
    if (width == 0 || height == 0 || map.size() != width * height * 2 || quads.size() % kSnesQuadBytes != 0) {
        return std::unexpected{Error::kBadSize};
    }
    IndexedImage image = make_image(width * kSnesQuadSize, height * kSnesQuadSize);
    for (std::size_t cell = 0; cell < width * height; ++cell) {
        const std::size_t quad = word_at(map, 2 * cell) & kIndexMask;
        if ((quad + 1) * kSnesQuadBytes > quads.size()) {
            return std::unexpected{Error::kBadReference};
        }
        for (std::size_t part = 0; part < 4; ++part) {
            const unsigned word = word_at(quads, (quad * kSnesQuadBytes) + (2 * part));
            const std::size_t index = word & kIndexMask;
            if (index >= tiles.size()) {
                return std::unexpected{Error::kBadReference};
            }
            const IndexedImage& tile = tiles[index];
            const unsigned row = (word >> 10U) & 7U;
            const bool mirrored = (word & 0x4000U) != 0;
            const bool flipped = (word & 0x8000U) != 0;
            const std::size_t left = ((cell % width) * kSnesQuadSize) + ((part % 2) * kTileSize);
            const std::size_t top = ((cell / width) * kSnesQuadSize) + ((part / 2) * kTileSize);
            for (std::size_t y = 0; y < kTileSize; ++y) {
                for (std::size_t x = 0; x < kTileSize; ++x) {
                    const unsigned value = tile.at(mirrored ? kTileSize - 1 - x : x, flipped ? kTileSize - 1 - y : y);
                    image.set(left + x, top + y,
                              value == 0 ? std::uint8_t{0} : static_cast<std::uint8_t>((row * 16U) + value));
                }
            }
        }
    }
    return image;
}

std::expected<Backdrop, std::string> load_backdrop(const SnesRom& rom, std::uint16_t header_chunk) {
    const auto header_bytes = named(rom.chunk(header_chunk), "level header", header_chunk);
    if (!header_bytes) {
        return std::unexpected{header_bytes.error()};
    }
    // The header is the DOS one (docs/formats/level.md) with chunk numbers for its entries.
    const auto level = named(parse_level_header(*header_bytes), "level header", header_chunk);
    const auto fields = named(parse_backdrop_header(*header_bytes), "level header", header_chunk);
    if (!level || !fields) {
        return std::unexpected{level ? fields.error() : level.error()};
    }
    if (fields->map_chunk == kNoEntry) {
        return std::unexpected{std::format("SNES chunk {} (level header): no backdrop", header_chunk)};
    }
    Backdrop backdrop{.header = *fields, .image = {}, .palette = {}};
    for (const PaletteRef& ref : level->palettes) {
        const auto colours = named(rom.chunk(ref.entry), "palette", ref.entry);
        if (!colours) {
            return std::unexpected{colours.error()};
        }
        if (auto loaded = named(load_snes_palette(backdrop.palette, *colours, ref.first_color), "palette", ref.entry);
            !loaded) {
            return std::unexpected{loaded.error()};
        }
    }
    const std::uint16_t tiles_chunk = fields->tiles_chunk == kNoEntry ? level->tileset_entry : fields->tiles_chunk;
    const std::uint16_t quads_chunk = fields->quads_chunk == kNoEntry ? level->metatiles_entry : fields->quads_chunk;
    const auto tile_bytes = named(rom.chunk(tiles_chunk), "tiles", tiles_chunk);
    const auto quads = named(rom.chunk(quads_chunk), "quads", quads_chunk);
    const auto map = named(rom.chunk(fields->map_chunk), "backdrop map", fields->map_chunk);
    if (!tile_bytes || !quads || !map) {
        return std::unexpected{!tile_bytes ? tile_bytes.error() : (!quads ? quads.error() : map.error())};
    }
    const auto tiles = named(decode_snes_tiles(*tile_bytes), "tiles", tiles_chunk);
    if (!tiles) {
        return std::unexpected{tiles.error()};
    }
    auto image =
        named(draw_snes_map(*map, fields->width, fields->height, *quads, *tiles), "backdrop map", fields->map_chunk);
    if (!image) {
        return std::unexpected{image.error()};
    }
    backdrop.image = std::move(*image);
    return backdrop;
}

std::expected<Backdrop, std::string> load_level_backdrop(const SnesRom& rom, std::uint8_t number) {
    if (number < 1 || number > kSnesLevelHeaders.size()) {
        return std::unexpected{std::format("no level {}", int{number})};
    }
    return load_backdrop(rom, kSnesLevelHeaders[number - 1U]);
}

std::expected<Backdrop, std::string> load_scene_backdrop(const SnesRom& rom, std::string_view name) {
    const auto* scene = std::ranges::find(kSnesSceneHeaders, name, &SnesScene::name);
    if (scene == kSnesSceneHeaders.end()) {
        return std::unexpected{std::format("no SNES backdrop for the scene \"{}\"", name)};
    }
    return load_backdrop(rom, scene->header_chunk);
}

}  // namespace lvr::assets
