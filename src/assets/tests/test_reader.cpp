// Tests for src/assets/reader.*
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <set>
#include <span>
#include <string_view>

#include "assets/reader.hpp"

using lvr::assets::ByteReader;
using lvr::assets::Error;

namespace {

constexpr std::array kData{std::byte{0x01}, std::byte{0x34}, std::byte{0x12}, std::byte{0x78},
                           std::byte{0x56}, std::byte{0x34}, std::byte{0x12}};

}  // namespace

TEST_CASE("ByteReader reads little-endian integers in order") {
    ByteReader reader{kData};
    CHECK(reader.u8() == 0x01);
    CHECK(reader.u16le() == 0x1234);
    CHECK(reader.u32le() == 0x12345678U);
    CHECK(reader.remaining() == 0);
}

TEST_CASE("ByteReader fails without advancing when reading past the end") {
    ByteReader reader{std::span{kData}.first(1)};
    const auto result = reader.u16le();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == Error::kOutOfBounds);
    CHECK(reader.position() == 0);
    CHECK(reader.u8() == 0x01);
}

TEST_CASE("ByteReader hands out byte ranges, bounds-checked") {
    ByteReader reader{kData};
    const auto first = reader.bytes(3);
    REQUIRE(first.has_value());
    CHECK(first->size() == 3);
    CHECK(reader.position() == 3);
    CHECK(reader.bytes(5).error() == Error::kOutOfBounds);
    CHECK(reader.bytes(4).has_value());
    CHECK(reader.bytes(0).has_value());  // an empty range at the end is fine
}

TEST_CASE("every error has its own description") {
    const std::array errors{
        Error::kOutOfBounds,         Error::kTruncated,       Error::kBadOffsetTable, Error::kOffsetOutOfRange,
        Error::kOffsetsNotAscending, Error::kIndexOutOfRange, Error::kBadSize,        Error::kBadReference,
        Error::kBadOpcode,           Error::kBadOperandKind,  Error::kBadChunk,       Error::kBadEvent};
    std::set<std::string_view> seen;
    for (const Error error : errors) {
        CHECK_FALSE(describe(error).empty());
        seen.insert(describe(error));
    }
    CHECK(seen.size() == errors.size());
}

TEST_CASE("ByteReader seek is bounds-checked") {
    ByteReader reader{kData};
    CHECK(reader.seek(kData.size()).has_value());
    CHECK_FALSE(reader.seek(kData.size() + 1).has_value());
    REQUIRE(reader.seek(3).has_value());
    CHECK(reader.u32le() == 0x12345678U);
}
