// Tests for src/assets/objects.*: a hand-made object database and its errors.
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "assets/objects.hpp"

using namespace lvr::assets;

namespace {

std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (const int value : values) {
        out.push_back(static_cast<std::byte>(value));
    }
    return out;
}

constexpr std::size_t kRecordsEnd = kObjectTypeCount * kObjectRecordSize;  // 0x1347

// A database whose records all point at `script_offset`, with `between` after the records (the byte lists) and
// `code` at script_offset.
std::vector<std::byte> make_database(std::uint16_t script_offset, const std::vector<std::byte>& between,
                                     const std::vector<std::byte>& code) {
    std::vector<std::byte> out(kRecordsEnd);
    for (std::size_t i = 0; i < kObjectTypeCount; ++i) {
        out[(i * kObjectRecordSize) + 0] = static_cast<std::byte>(i);  // something to tell the records apart
        out[(i * kObjectRecordSize) + 3] = static_cast<std::byte>(script_offset & 0xffU);
        out[(i * kObjectRecordSize) + 4] = static_cast<std::byte>(script_offset >> 8U);
    }
    out.insert(out.end(), between.begin(), between.end());
    out.resize(script_offset);
    out.insert(out.end(), code.begin(), code.end());
    return out;
}

}  // namespace

TEST_CASE("objects: a hand-made object database") {
    constexpr std::uint16_t kScript = 0x134d;
    const auto data = make_database(kScript, bytes({0x00, 0xff, 0x01, 0x02, 0xff, 0xff}), bytes({0x00}));
    const auto database = parse_object_database(data);
    REQUIRE(database.has_value());
    REQUIRE(database->records.size() == kObjectTypeCount);
    CHECK(database->records[7].bytes[0] == 7);
    CHECK(database->records[7].script() == kScript);
    CHECK(database->byte_lists == std::vector<std::vector<std::uint8_t>>{{0x00}, {0x01, 0x02}, {}});
    CHECK(database->bytes.size() == data.size());
}

TEST_CASE("objects: object database errors") {
    CHECK(parse_object_database(std::vector<std::byte>(kRecordsEnd - 1)).error() == Error::kTruncated);
    CHECK(parse_object_database(std::vector<std::byte>(0x10001)).error() == Error::kBadSize);
    // every script offset 0: inside the records
    CHECK(parse_object_database(std::vector<std::byte>(kRecordsEnd + 4)).error() == Error::kOffsetOutOfRange);
    // a script offset past the end
    CHECK(parse_object_database(make_database(0x1350, {}, {})).error() == Error::kOffsetOutOfRange);
    // a byte list without its 0xff
    CHECK(parse_object_database(make_database(0x1349, bytes({0x01, 0x02}), bytes({0x00}))).error() ==
          Error::kTruncated);
}
