// Tests for src/assets/snes.*: the SNES ROM's chunk table, tiles, colours and backdrops on a small hand-made image;
// the [real-data] ones read the user's own ROM image and skip when there is none.
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "assets/data_dat.hpp"
#include "assets/hash.hpp"
#include "assets/snes.hpp"

using namespace lvr::assets;

namespace {

void put_u16(std::vector<std::byte>& data, std::size_t offset, std::uint16_t value) {
    if (data.size() < offset + 2) {
        data.resize(offset + 2);
    }
    data[offset] = static_cast<std::byte>(value & 0xffU);
    data[offset + 1] = static_cast<std::byte>(value >> 8U);
}

std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (const int v : values) {
        out.push_back(static_cast<std::byte>(v));
    }
    return out;
}

// `data` as an LZSS chunk of literals only: the size, then a flag byte of ones before every eight bytes.
std::vector<std::byte> packed(const std::vector<std::byte>& data) {
    std::vector<std::byte> out;
    put_u16(out, 0, static_cast<std::uint16_t>(data.size()));
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (i % 8 == 0) {
            out.push_back(std::byte{0xff});
        }
        out.push_back(data[i]);
    }
    return out;
}

// An image with these chunks behind a table at kSnesChunkTable.
std::vector<std::byte> image_of(const std::vector<std::vector<std::byte>>& chunks) {
    std::vector<std::byte> image(kSnesChunkTable + (4 * chunks.size()));
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const std::size_t offset = image.size() - kSnesChunkTable;
        put_u16(image, kSnesChunkTable + (4 * i), static_cast<std::uint16_t>(offset % kSnesBankSize));
        put_u16(image, kSnesChunkTable + (4 * i) + 2, static_cast<std::uint16_t>(offset / kSnesBankSize));
        image.insert(image.end(), chunks[i].begin(), chunks[i].end());
    }
    return image;
}

// A level header with a backdrop of 2 x 1 quads: map chunk 1, the level's tiles (chunk 2) and quads (chunk 3), one
// palette (chunk 15) at colour 16.
std::vector<std::byte> header_chunk(std::uint16_t map_chunk = 1) {
    std::vector<std::byte> data(0x43);
    put_u16(data, 0x29, 1);
    put_u16(data, 0x2b, 1);
    put_u16(data, 0x2e, 0);
    put_u16(data, 0x30, 2);
    put_u16(data, 0x32, 3);
    put_u16(data, 0x34, 2);
    put_u16(data, 0x36, 1);
    data[0x38] = std::byte{2};
    put_u16(data, 0x39, map_chunk);
    put_u16(data, 0x3b, 0xffff);
    put_u16(data, 0x3d, 0xffff);
    put_u16(data, 0x3f, 0x0040);
    put_u16(data, 0x41, 0x0100);
    put_u16(data, 0x43, 0xffff);    // no objects
    put_u16(data, 0x45, 15);        // palette chunk 15 ...
    data.push_back(std::byte{16});  // ... from colour 16
    put_u16(data, 0x48, 0xffff);
    put_u16(data, 0x4a, 0);  // palette animations: flags, then delay 0
    data.push_back(std::byte{0});
    put_u16(data, 0x4d, 0xffff);  // no sprite sets
    put_u16(data, 0x4f, 0xffff);  // no packed sets
    return data;
}

// Two tiles: tile 0 empty, tile 1 with value 5 at its top-left pixel and 15 at the pixel right of it.
std::vector<std::byte> tiles_chunk() {
    std::vector<std::byte> data(64);
    data[32] = std::byte{0xc0};       // bit 0 of the two leftmost pixels of row 0
    data[32 + 1] = std::byte{0x40};   // bit 1 of the second
    data[32 + 16] = std::byte{0xc0};  // bit 2 of both
    data[32 + 17] = std::byte{0x40};  // bit 3 of the second
    return data;
}

// Quad 0: tile 1 in all four corners, palette row 1; top-right mirrored, bottom-left flipped. Quad 1: tile 0.
std::vector<std::byte> quads_chunk() {
    std::vector<std::byte> data;
    for (const unsigned word : {0x0401U, 0x4401U, 0x8401U, 0x0401U, 0U, 0U, 0U, 0U}) {
        put_u16(data, data.size(), static_cast<std::uint16_t>(word));
    }
    return data;
}

std::vector<std::vector<std::byte>> sample_chunks() {
    std::vector<std::vector<std::byte>> chunks{packed(header_chunk()),       // 0
                                               packed(bytes({0, 0, 1, 0})),  // 1: the map, quads 0 and 1
                                               packed(tiles_chunk()),        // 2
                                               packed(quads_chunk()),        // 3
                                               bytes({1, 2, 3})};            // 4: stored, as 4-14 are
    chunks.resize(15, bytes({0}));
    chunks.push_back(packed(bytes({0x1f, 0, 0xff, 0x7f})));  // 15: red, white
    return chunks;
}

const SnesRom& rom_or_skip() {
    static const auto rom = SnesRom::open(std::filesystem::exists(LVR_ORIGINAL_DIR) ? LVR_ORIGINAL_DIR : ".");
    if (!rom) {
        SKIP("no SNES ROM image: " << rom.error());
    }
    return *rom;
}

}  // namespace

TEST_CASE("snes: the chunk table gives each chunk's bytes; compressed ones are decoded, stored ones as they are") {
    const auto rom = SnesRom::parse(image_of(sample_chunks()));
    REQUIRE(rom.has_value());
    CHECK(rom->chunk_count() == 16);
    CHECK(rom->chunk(1) == bytes({0, 0, 1, 0}));
    CHECK(rom->chunk(4) == bytes({1, 2, 3}));  // chunk 4 is in kSnesStoredChunks
    CHECK(rom->raw_chunk(4)->size() == 3);
    CHECK(rom->raw_chunk(15)->size() == 7);  // the last one runs to the image's end
    CHECK(rom->chunk(16).error() == Error::kIndexOutOfRange);
    CHECK(rom->raw_chunk(16).error() == Error::kIndexOutOfRange);
}

TEST_CASE("snes: a broken chunk table is an error") {
    CHECK(SnesRom::parse(std::vector<std::byte>(100)).error() == Error::kTruncated);
    // The first offset is 0: no table.
    CHECK(SnesRom::parse(std::vector<std::byte>(kSnesChunkTable + 16)).error() == Error::kBadOffsetTable);
    auto image = image_of(sample_chunks());
    put_u16(image, kSnesChunkTable, 6);  // not a multiple of 4
    CHECK(SnesRom::parse(image).error() == Error::kBadOffsetTable);
    image = image_of(sample_chunks());
    put_u16(image, kSnesChunkTable + 4 + 2, 0x100);  // chunk 1 far past the end
    CHECK(SnesRom::parse(image).error() == Error::kOffsetOutOfRange);
    image = image_of(sample_chunks());
    put_u16(image, kSnesChunkTable + 8, 24);  // chunk 2 at chunk 0's place
    CHECK(SnesRom::parse(image).error() == Error::kOffsetsNotAscending);
    // A chunk that ends in the middle of its data.
    auto chunks = sample_chunks();
    chunks[15].resize(3);
    const auto cut = SnesRom::parse(image_of(chunks));
    REQUIRE(cut.has_value());
    CHECK(cut->chunk(15).error() == Error::kTruncated);
}

TEST_CASE("snes: only known images are accepted") {
    const auto unknown = SnesRom::from_bytes(image_of(sample_chunks()));
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().contains("unknown SNES ROM image"));
}

TEST_CASE("snes: open finds the one .sfc file of a folder") {
    const auto dir = std::filesystem::temp_directory_path() / "lvr-test-snes-open";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    CHECK(SnesRom::open(dir).error().contains("no SNES ROM image"));
    const auto write = [&](const char* name) {
        const auto image = image_of(sample_chunks());
        std::ofstream out{dir / name, std::ios::binary};
        out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    };
    write("Game.SFC");
    const auto one = SnesRom::open(dir);
    REQUIRE_FALSE(one.has_value());  // read, then refused: not a known image
    CHECK(one.error().contains("Game.SFC"));
    CHECK(one.error().contains("unknown SNES ROM image"));
    write("other.sfc");
    CHECK(SnesRom::open(dir).error().contains("more than one"));
    std::filesystem::remove_all(dir);
}

TEST_CASE("snes: tiles are 8 x 8 pixels of 4 bits in two pairs of bit rows") {
    const auto tiles = decode_snes_tiles(tiles_chunk());
    REQUIRE(tiles.has_value());
    REQUIRE(tiles->size() == 2);
    CHECK(std::ranges::all_of((*tiles)[0].pixels, [](std::uint8_t p) { return p == 0; }));
    CHECK((*tiles)[1].at(0, 0) == 5);
    CHECK((*tiles)[1].at(1, 0) == 15);
    CHECK((*tiles)[1].at(2, 0) == 0);
    CHECK((*tiles)[1].at(0, 1) == 0);
    CHECK(decode_snes_tiles(std::vector<std::byte>(33)).error() == Error::kBadSize);
}

TEST_CASE("snes: colours are words of 5 bits each, red lowest") {
    Palette palette{};
    REQUIRE(load_snes_palette(palette, bytes({0x1f, 0, 0xe0, 0x03, 0x00, 0x7c, 0xff, 0x7f}), 254));
    CHECK(palette[254] == Rgb{.r = 255, .g = 0, .b = 0});
    CHECK(palette[255] == Rgb{.r = 0, .g = 255, .b = 0});  // the two after it fall past 255
    CHECK(load_snes_palette(palette, bytes({1, 2, 3}), 0).error() == Error::kBadSize);
}

TEST_CASE("snes: a map of quads is drawn with mirrored and flipped tiles and the quad's palette row") {
    const auto tiles = decode_snes_tiles(tiles_chunk());
    REQUIRE(tiles.has_value());
    const auto quads = quads_chunk();
    const auto image = draw_snes_map(bytes({0, 0xfc, 1, 0}), 2, 1, quads, *tiles);  // the upper bits aren't the quad
    REQUIRE(image.has_value());
    CHECK(image->width == 32);
    CHECK(image->height == 16);
    CHECK(image->at(0, 0) == 16 + 5);  // top-left
    CHECK(image->at(1, 0) == 16 + 15);
    CHECK(image->at(15, 0) == 16 + 5);  // top-right, mirrored
    CHECK(image->at(14, 0) == 16 + 15);
    CHECK(image->at(0, 15) == 16 + 5);  // bottom-left, flipped
    CHECK(image->at(8, 8) == 16 + 5);   // bottom-right
    CHECK(image->at(2, 0) == 0);        // what is behind
    CHECK(image->at(16, 0) == 0);       // quad 1: empty
    CHECK(draw_snes_map(bytes({0, 0}), 2, 1, quads, *tiles).error() == Error::kBadSize);
    CHECK(draw_snes_map(bytes({0, 0}), 0, 1, quads, *tiles).error() == Error::kBadSize);
    CHECK(draw_snes_map(bytes({2, 0}), 1, 1, quads, *tiles).error() == Error::kBadReference);  // no quad 2
    CHECK(draw_snes_map(bytes({0, 0}), 1, 1, quads, std::span{*tiles}.first(1)).error() == Error::kBadReference);
}

TEST_CASE("snes: a level's backdrop: its header fields, its picture from the level's tiles and quads, its colours") {
    const auto rom = SnesRom::parse(image_of(sample_chunks()));
    REQUIRE(rom.has_value());
    const auto backdrop = load_backdrop(*rom, 0);
    REQUIRE(backdrop.has_value());
    CHECK(backdrop->header.width == 2);
    CHECK(backdrop->header.height == 1);
    CHECK(backdrop->header.map_chunk == 1);
    CHECK(backdrop->header.tiles_chunk == 0xffff);
    CHECK(backdrop->header.scroll_x == 0x40);
    CHECK(backdrop->header.scroll_y == 0x100);
    CHECK(backdrop->image.width == 32);
    CHECK(backdrop->image.at(0, 0) == 16 + 5);
    CHECK(backdrop->palette[16] == Rgb{.r = 255, .g = 0, .b = 0});
    CHECK(backdrop->palette[17] == Rgb{.r = 255, .g = 255, .b = 255});
    CHECK(parse_backdrop_header(std::vector<std::byte>(0x42)).error() == Error::kTruncated);
    CHECK(load_level_backdrop(*rom, 0).error() == "no level 0");
    CHECK(load_level_backdrop(*rom, 38).error() == "no level 38");
    CHECK(load_level_backdrop(*rom, 1).error().contains("SNES chunk 111"));  // not in this small image
}

TEST_CASE("snes: a backdrop's errors name the chunk") {
    const auto broken = [](std::size_t index, std::vector<std::byte> chunk) {
        auto chunks = sample_chunks();
        chunks[index] = std::move(chunk);
        const auto rom = SnesRom::parse(image_of(chunks));
        REQUIRE(rom.has_value());
        const auto backdrop = load_backdrop(*rom, 0);
        REQUIRE_FALSE(backdrop.has_value());
        return backdrop.error();
    };
    CHECK(broken(0, packed(header_chunk(0xffff))).contains("no backdrop"));
    CHECK(broken(0, packed(std::vector<std::byte>(0x20))).contains("SNES chunk 0 (level header)"));
    CHECK(broken(0, packed(header_chunk(9))).contains("SNES chunk 9 (backdrop map)"));
    CHECK(broken(1, packed(bytes({0, 0}))).contains("SNES chunk 1 (backdrop map)"));
    CHECK(broken(2, packed(std::vector<std::byte>(33))).contains("SNES chunk 2 (tiles)"));
    CHECK(broken(2, bytes({9})).contains("SNES chunk 2 (tiles)"));
    CHECK(broken(3, bytes({9})).contains("SNES chunk 3 (quads)"));
    CHECK(broken(15, packed(bytes({1, 2, 3}))).contains("SNES chunk 15 (palette)"));
    CHECK(broken(15, bytes({9})).contains("SNES chunk 15 (palette)"));
}

// --- The user's own ROM image ---

TEST_CASE("real data: the SNES image's chunks all decode and use up their bytes", "[real-data]") {
    const SnesRom& rom = rom_or_skip();
    CHECK(rom.chunk_count() == 361);
    for (std::size_t index = 0; index < rom.chunk_count(); ++index) {
        INFO("chunk " << index);
        const auto raw = rom.raw_chunk(index);
        REQUIRE(raw.has_value());
        if (std::ranges::contains(kSnesStoredChunks, index)) {
            continue;
        }
        const auto decoded = lzss_decompress(*raw, LzssHeader::kSize);
        REQUIRE(decoded.has_value());
        if (index + 1 < rom.chunk_count()) {
            CHECK(decoded->consumed == raw->size());
        } else {  // the last chunk: zeros up to the image's end
            CHECK(std::ranges::all_of(raw->subspan(decoded->consumed), [](std::byte b) { return b == std::byte{0}; }));
        }
    }
}

TEST_CASE("real data: every level has a backdrop on the SNES", "[real-data]") {
    const SnesRom& rom = rom_or_skip();
    for (std::uint8_t number = 1; number <= 37; ++number) {
        INFO("level " << int{number});
        const auto backdrop = load_level_backdrop(rom, number);
        REQUIRE(backdrop.has_value());
        CHECK(backdrop->image.width == backdrop->header.width * 16U);
        CHECK(backdrop->image.height == backdrop->header.height * 16U);
        CHECK(backdrop->header.tiles_chunk == 0xffff);
        if (number == 1) {  // the stars, moving across by themselves (bit 15) and down with the camera
            CHECK(backdrop->image.width == 256);
            CHECK(backdrop->image.height == 128);
            CHECK(backdrop->header.scroll_x == 0x8500);
            CHECK(backdrop->header.scroll_y == 0x0100);
            CHECK(fnv1a64(std::as_bytes(std::span{backdrop->image.pixels})) == 0xb5e5cb15323c0702ULL);
        }
        if (number == 5) {  // the mountains at dusk, a quarter of the camera's speed
            CHECK(backdrop->image.height == 512);
            CHECK(backdrop->header.scroll_x == 0x40);
        }
    }
}
