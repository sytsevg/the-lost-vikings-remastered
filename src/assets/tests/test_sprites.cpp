// Tests for src/assets/sprites.*: the four sprite formats, on synthetic data.
#include <algorithm>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

#include "assets/sprites.hpp"

using namespace lvr::assets;

namespace {

// A byte vector from integer literals: bytes({0x01, 0xff}).
std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (const int v : values) {
        out.push_back(static_cast<std::byte>(v));
    }
    return out;
}

}  // namespace

namespace {

// One packed32 frame with every row empty, except the given (plane, row) rows.
struct PackedRow {
    std::size_t plane;
    std::size_t row;
    std::vector<int> bytes;  // mask, then the pixel bytes
};

std::vector<std::byte> packed32_frame(const std::vector<PackedRow>& rows) {
    std::vector<std::byte> out;
    for (std::size_t plane = 0; plane < 4; ++plane) {
        for (std::size_t y = 0; y < 32; ++y) {
            const auto match =
                std::ranges::find_if(rows, [&](const PackedRow& r) { return r.plane == plane && r.row == y; });
            if (match == rows.end()) {
                out.push_back(std::byte{0});
                continue;
            }
            for (const int b : match->bytes) {
                out.push_back(static_cast<std::byte>(b));
            }
        }
    }
    return out;
}

}  // namespace

TEST_CASE("unpacked frame: per plane, a mask byte (MSB first) before every 8 pixels") {
    // 4x8: each plane is one column of 8 pixels, so one group per plane.
    std::vector<std::byte> data = bytes({0xff, 1, 2, 3, 4, 5, 6, 7, 8});  // plane 0: x = 0, rows 0..7, all drawn
    const auto plane1 = bytes({0x80, 9, 0, 0, 0, 0, 0, 0, 0});            // plane 1: only row 0
    const auto plane2 = bytes({0x00, 0, 0, 0, 0, 0, 0, 0, 0});            // plane 2: nothing
    const auto plane3 = bytes({0x01, 0, 0, 0, 0, 0, 0, 0, 10});           // plane 3: only row 7
    data.insert(data.end(), plane1.begin(), plane1.end());
    data.insert(data.end(), plane2.begin(), plane2.end());
    data.insert(data.end(), plane3.begin(), plane3.end());

    const auto image = decode_unpacked_frame(data, 4, 8);
    REQUIRE(image.has_value());
    CHECK(image->at(0, 0) == 1);
    CHECK(image->at(0, 7) == 8);
    CHECK(image->at(1, 0) == 9);
    CHECK(image->at(1, 1) == 0);  // masked out
    CHECK(image->at(2, 3) == 0);
    CHECK(image->at(3, 7) == 10);
}

TEST_CASE("unpacked frame: 16 pixels wide puts 4 pixels of a plane in each row") {
    // 16x2: plane 0 holds x = 0, 4, 8, 12 of row 0, then of row 1: one group of 8.
    std::vector<std::byte> data = bytes({0xff, 1, 2, 3, 4, 5, 6, 7, 8});
    data.resize(4 * 9, std::byte{0});
    const auto image = decode_unpacked_frame(data, 16, 2);
    REQUIRE(image.has_value());
    CHECK(image->at(0, 0) == 1);
    CHECK(image->at(12, 0) == 4);
    CHECK(image->at(0, 1) == 5);
    CHECK(image->at(12, 1) == 8);
}

TEST_CASE("unpacked frame: bad sizes are rejected") {
    const std::vector<std::byte> data(72);
    CHECK(decode_unpacked_frame(data, 6, 8).error() == Error::kBadSize);  // not a multiple of 4 wide
    CHECK(decode_unpacked_frame(data, 4, 4).error() == Error::kBadSize);  // planes of 4 pixels: no whole group
    CHECK(decode_unpacked_frame(data, 0, 8).error() == Error::kBadSize);
    CHECK(decode_unpacked_frame(std::span{data}.first(71), 8, 8).error() == Error::kTruncated);
    CHECK(decode_unpacked_frame(data, 8, 8).has_value());
}

TEST_CASE("sprite frames: runs of different sizes, back to back, using up the entry") {
    const std::vector<FrameRun> runs{{.width = 8, .height = 8, .count = 2}, {.width = 16, .height = 16, .count = 1}};
    const std::vector<std::byte> data((2 * 72) + 288);
    const auto frames = decode_sprite_frames(data, SpriteFormat::kUnpacked, runs);
    REQUIRE(frames.has_value());
    REQUIRE(frames->size() == 3);
    CHECK((*frames)[1].offset == 72);
    CHECK((*frames)[2].offset == 144);
    CHECK((*frames)[2].size == 288);
    CHECK((*frames)[2].image.width == 16);

    const std::vector<std::byte> longer(data.size() + 1);
    CHECK(decode_sprite_frames(longer, SpriteFormat::kUnpacked, runs).error() == Error::kBadSize);
    CHECK(decode_sprite_frames(std::span{data}.first(200), SpriteFormat::kUnpacked, runs).error() == Error::kBadSize);
}

TEST_CASE("sprite frames: raw frames are plain planar images") {
    const std::vector<FrameRun> runs{{.width = 4, .height = 1, .count = 2}};
    const auto frames = decode_sprite_frames(bytes({1, 2, 3, 4, 5, 6, 7, 8}), SpriteFormat::kRaw, runs);
    REQUIRE(frames.has_value());
    REQUIRE(frames->size() == 2);
    CHECK((*frames)[1].offset == 4);
    CHECK((*frames)[1].image.pixels == std::vector<std::uint8_t>{5, 6, 7, 8});
}

TEST_CASE("tiled: 8 mask bytes, then 64 planar pixels per tile; tiles fill the sheet row by row") {
    // A sheet of two 8x8 frames side by side, one tile row: tile 0 = frame 0, tile 1 = frame 1.
    std::vector<std::byte> data(2 * kMaskedTileBytes);
    data[0] = std::byte{0x80};  // mask byte 0, bit 7: pixel byte 0 = plane 0, row 0, x = 0
    data[8] = std::byte{3};
    data[1] = std::byte{0x40};  // mask byte 1, bit 6: pixel byte 9 = plane 0, row 4, x = 4
    data[8 + 9] = std::byte{4};
    data[8 + 10] = std::byte{9};                      // pixel byte 10 is masked out: ignored
    data[kMaskedTileBytes + 7] = std::byte{0x01};     // tile 1, mask byte 7, bit 0: pixel byte 63
    data[kMaskedTileBytes + 8 + 63] = std::byte{15};  // = plane 3, row 7, x = 7
    const std::vector<FrameRun> runs{{.width = 8, .height = 8, .count = 2}};
    const auto frames = decode_sprite_frames(data, SpriteFormat::kTiled, runs);
    REQUIRE(frames.has_value());
    REQUIRE(frames->size() == 2);
    const IndexedImage& first = (*frames)[0].image;
    CHECK(first.at(0, 0) == 3);
    CHECK(first.at(4, 4) == 4);
    CHECK(std::ranges::count(first.pixels, 0) == 62);
    CHECK((*frames)[1].offset == kMaskedTileBytes);
    CHECK((*frames)[1].image.at(7, 7) == 15);
}

TEST_CASE("tiled: a frame is cut from the sheet, so its tiles are not contiguous") {
    // Two 16x8 frames: the sheet row holds tiles 0 1 | 2 3. Mark each tile's first pixel with its number.
    std::vector<std::byte> data(4 * kMaskedTileBytes);
    for (std::size_t tile = 0; tile < 4; ++tile) {
        data[tile * kMaskedTileBytes] = std::byte{0x80};
        data[(tile * kMaskedTileBytes) + 8] = static_cast<std::byte>(tile + 1);
    }
    const auto frames = decode_tiled(data, {.width = 16, .height = 8, .count = 2});
    REQUIRE(frames.has_value());
    REQUIRE(frames->size() == 2);
    CHECK((*frames)[1].image.at(0, 0) == 3);
    CHECK((*frames)[1].image.at(8, 0) == 4);
    CHECK((*frames)[1].offset == 2 * kMaskedTileBytes);
    CHECK((*frames)[1].size == 2 * kMaskedTileBytes);

    // 16x16 frames: tile rows run across the whole sheet (tiles 0 1 2 3 / 4 5 6 7).
    std::vector<std::byte> tall(8 * kMaskedTileBytes);
    tall[4 * kMaskedTileBytes] = std::byte{0x80};
    tall[(4 * kMaskedTileBytes) + 8] = std::byte{7};
    const auto tall_frames = decode_tiled(tall, {.width = 16, .height = 16, .count = 2});
    REQUIRE(tall_frames.has_value());
    CHECK((*tall_frames)[0].image.at(0, 8) == 7);
}

TEST_CASE("tiled: bad sizes are rejected") {
    const std::vector<std::byte> data(2 * kMaskedTileBytes);
    CHECK(decode_tiled(data, {.width = 8, .height = 8, .count = 2}).has_value());
    CHECK(decode_tiled(data, {.width = 8, .height = 8, .count = 1}).error() == Error::kBadSize);  // left over
    CHECK(decode_tiled(data, {.width = 12, .height = 8, .count = 1}).error() == Error::kBadSize);
    CHECK(decode_tiled(data, {.width = 8, .height = 8, .count = 0}).error() == Error::kBadSize);
    CHECK(decode_tiled(std::span{data}.first(100), {.width = 8, .height = 8, .count = 2}).error() == Error::kBadSize);
    const std::vector<FrameRun> two_runs{{.width = 8, .height = 8, .count = 1}, {.width = 8, .height = 8, .count = 1}};
    CHECK(decode_sprite_frames(data, SpriteFormat::kTiled, two_runs).error() == Error::kBadSize);
}

TEST_CASE("packed32: offset table, then per plane row a mask byte and 4-bit pixels") {
    // plane 0 row 0: x = 0 and x = 8 (mask bits 7 and 5), pixels 3 and 12 in one byte
    // plane 1 row 5: x = 29 (mask bit 0), pixel 5 in the high nibble, low nibble is padding
    const auto frame =
        packed32_frame({{.plane = 0, .row = 0, .bytes = {0xa0, 0x3c}}, {.plane = 1, .row = 5, .bytes = {0x01, 0x50}}});
    std::vector<std::byte> data = bytes({2, 0});
    data.insert(data.end(), frame.begin(), frame.end());
    data.push_back(std::byte{0xff});  // trailing bytes are allowed

    const auto frames = decode_packed32(data);
    REQUIRE(frames.has_value());
    REQUIRE(frames->size() == 1);
    const SpriteFrame& f = (*frames)[0];
    CHECK(f.offset == 2);
    CHECK(f.size == 128 + 2);  // 128 mask bytes + 2 pixel bytes
    CHECK(f.image.width == 32);
    CHECK(f.image.at(0, 0) == 3);
    CHECK(f.image.at(8, 0) == 12);
    CHECK(f.image.at(4, 0) == 0);
    CHECK(f.image.at(29, 5) == 5);
}

TEST_CASE("packed32: malformed tables and frames are rejected") {
    const auto frame = packed32_frame({});
    std::vector<std::byte> data = bytes({2, 0});
    data.insert(data.end(), frame.begin(), frame.end());
    CHECK(decode_packed32(data).has_value());

    CHECK(decode_packed32(bytes({1})).error() == Error::kTruncated);
    CHECK(decode_packed32(bytes({0, 0})).error() == Error::kBadOffsetTable);
    CHECK(decode_packed32(bytes({3, 0, 0})).error() == Error::kBadOffsetTable);
    CHECK(decode_packed32(bytes({4, 0})).error() == Error::kTruncated);  // second offset missing
    CHECK(decode_packed32(bytes({4, 0, 9, 0})).error() == Error::kOffsetOutOfRange);
    CHECK(decode_packed32(std::span{data}.first(100)).error() == Error::kTruncated);  // frame cut short
}
