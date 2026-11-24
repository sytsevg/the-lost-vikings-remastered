// Tests for src/assets/data_dat.*: unit tests on hand-made input, plus [real-data] tests that check our
// findings (docs/formats/data-dat.md) against the user's own game files.
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

#include "assets/assets.hpp"
#include "assets/data_dat.hpp"

using namespace lvr::assets;

namespace {

// A byte vector from integer literals, bytes({0x01, 0xff}), or from the characters of a string.
std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (const int v : values) {
        out.push_back(static_cast<std::byte>(v));
    }
    return out;
}

std::vector<std::byte> bytes(std::string_view text) {
    const auto chars = std::as_bytes(std::span{text.data(), text.size()});
    return {chars.begin(), chars.end()};
}

// The user's own game files in gamedata/original/, opened the way the game opens them. [real-data] tests skip when
// the folder is missing.
const lvr::assets::GameFiles& game_or_skip() {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    static const auto files = lvr::assets::GameFiles::open(LVR_ORIGINAL_DIR);
    if (!files) {
        FAIL(files.error());
    }
    return *files;
}

}  // namespace

// --- Container ---

TEST_CASE("archive: entries run to the next offset, the last one to the end of the file") {
    // 3 entries: offsets 12, 14, 14 => "AB", "", "CDE"
    const auto file = bytes({12, 0, 0, 0, 14, 0, 0, 0, 14, 0, 0, 0, 'A', 'B', 'C', 'D', 'E'});
    const auto archive = Archive::parse(file);
    REQUIRE(archive.has_value());
    REQUIRE(archive->entry_count() == 3);
    CHECK(std::ranges::equal(*archive->raw_entry(0), bytes("AB")));
    CHECK(archive->raw_entry(1)->empty());
    CHECK(std::ranges::equal(*archive->raw_entry(2), bytes("CDE")));
    CHECK(archive->entry_range(2)->offset == 14);
    CHECK(archive->raw_entry(3).error() == Error::kIndexOutOfRange);
}

TEST_CASE("archive: malformed offset tables are rejected") {
    CHECK(Archive::parse(bytes({4, 0, 0})).error() == Error::kTruncated);
    CHECK(Archive::parse(bytes({0, 0, 0, 0})).error() == Error::kBadOffsetTable);
    CHECK(Archive::parse(bytes({6, 0, 0, 0, 0, 0})).error() == Error::kBadOffsetTable);
    CHECK(Archive::parse(bytes({8, 0, 0, 0})).error() == Error::kOffsetOutOfRange);
    CHECK(Archive::parse(bytes({8, 0, 0, 0, 99, 0, 0, 0})).error() == Error::kOffsetOutOfRange);
    CHECK(Archive::parse(bytes({8, 0, 0, 0, 4, 0, 0, 0})).error() == Error::kOffsetsNotAscending);
}

// --- LZSS ---

TEST_CASE("lzss: literals only") {
    // header 2 => 3 bytes; flags 0b111 => three literals
    const auto input = bytes({0x02, 0x00, 0x07, 'a', 'b', 'c'});
    const auto out = lzss_decompress(input);
    REQUIRE(out.has_value());
    CHECK(out->data == bytes("abc"));
    CHECK(out->consumed == input.size());
}

TEST_CASE("lzss: a reference may overlap the bytes it is producing") {
    // 'a', 'b', then copy 6 bytes from window position 0 => "abababab"
    const auto input = bytes({0x07, 0x00, 0x03, 'a', 'b', 0x00, 0x30});
    const auto out = lzss_decompress(input);
    REQUIRE(out.has_value());
    CHECK(out->data == bytes("abababab"));
    CHECK(out->consumed == input.size());
}

TEST_CASE("lzss: the window starts zero-filled") {
    // reference to position 0x100, length 3, before anything was written
    const auto input = bytes({0x02, 0x00, 0x00, 0x00, 0x01});
    const auto out = lzss_decompress(input);
    REQUIRE(out.has_value());
    CHECK(out->data == std::vector<std::byte>(3, std::byte{0}));
}

TEST_CASE("lzss: output stops at the declared size, even inside a reference") {
    // header 1 => 2 bytes; reference of length 18
    const auto input = bytes({0x01, 0x00, 0x00, 0x00, 0xf0});
    const auto out = lzss_decompress(input);
    REQUIRE(out.has_value());
    CHECK(out->data.size() == 2);
}

TEST_CASE("lzss: truncated input is an error, never a crash") {
    CHECK(lzss_decompress({}).error() == Error::kTruncated);
    CHECK(lzss_decompress(bytes({0x05})).error() == Error::kTruncated);
    CHECK(lzss_decompress(bytes({0x05, 0x00, 0x01, 'a'})).error() == Error::kTruncated);
    CHECK(lzss_decompress(bytes({0x05, 0x00, 0x00, 0x00})).error() == Error::kTruncated);
}

// --- Real data ---

// --- Entries and release identification, on a hand-made DATA.DAT ---

namespace {

// 3 entries: 0 = "XY" (stored: the config), 1 = "PQ" (stored), 2 = LZSS for "abc" (three literals).
std::vector<std::byte> small_data_dat(char config_byte = 'X', char entry1_byte = 'P') {
    return bytes(
        {12, 0, 0, 0, 14, 0, 0, 0, 16, 0, 0, 0, config_byte, 'Y', entry1_byte, 'Q', 0x02, 0x00, 0x07, 'a', 'b', 'c'});
}

}  // namespace

TEST_CASE("entries: stored ones come back as they are, the others are LZSS-decompressed") {
    const auto file = small_data_dat();
    const auto archive = Archive::parse(file);
    REQUIRE(archive.has_value());
    CHECK(is_stored_entry(0));
    CHECK(is_stored_entry(1));
    CHECK_FALSE(is_stored_entry(2));
    CHECK(*decode_entry(*archive, 0) == bytes("XY"));
    CHECK(*decode_entry(*archive, 1) == bytes("PQ"));
    CHECK(*decode_entry(*archive, 2) == bytes("abc"));
    CHECK(decode_entry(*archive, 3).error() == Error::kIndexOutOfRange);
}

TEST_CASE("entries: kinds are the config entry, XMIDI by its IFF tags, or unknown") {
    const auto xmidi = bytes({'F', 'O', 'R', 'M', 0, 0, 0, 14, 'X', 'D', 'I', 'R'});
    CHECK(detect_kind(kConfigEntry, xmidi) == EntryKind::kConfig);
    CHECK(detect_kind(5, xmidi) == EntryKind::kXmidi);
    CHECK(detect_kind(5, bytes("FORM")) == EntryKind::kUnknown);  // too short for the second tag
    CHECK(detect_kind(5, bytes({'F', 'O', 'R', 'M', 0, 0, 0, 14, 'X', 'M', 'I', 'D'})) == EntryKind::kUnknown);
    CHECK(to_string(EntryKind::kConfig) == "config");
    CHECK(to_string(EntryKind::kXmidi) == "xmidi");
    CHECK(to_string(EntryKind::kUnknown) == "unknown");
}

TEST_CASE("release: the content hash leaves out the config entry, unknown files are not identified") {
    const auto base = data_content_hash(small_data_dat());
    REQUIRE(base.has_value());
    CHECK(data_content_hash(small_data_dat('Z')) == base);       // only the sound settings differ
    CHECK(data_content_hash(small_data_dat('X', 'Z')) != base);  // a real entry differs
    CHECK(data_content_hash(bytes({4, 0, 0})).error() == Error::kTruncated);
    CHECK_FALSE(identify_release(bytes("not the game"), small_data_dat()).has_value());
    CHECK_FALSE(identify_release(bytes("not the game"), bytes({4, 0, 0})).has_value());
}

TEST_CASE("real data: the reference release is identified", "[real-data]") {
    // GameFiles::open identifies the release from VIKINGS.EXE and DATA.DAT (identify_release).
    CHECK(game_or_skip().release().name == "vanilla-dos");
}

TEST_CASE("real data: every compressed entry decodes and uses its input exactly", "[real-data]") {
    const Archive* const archive = &game_or_skip().archive();
    REQUIRE(archive->entry_count() == kKnownReleases[0].entry_count);

    std::size_t xmidi = 0;
    for (std::size_t i = 0; i < archive->entry_count(); ++i) {
        INFO("entry " << i);
        const auto raw = archive->raw_entry(i);
        REQUIRE(raw.has_value());
        if (is_stored_entry(i)) {
            continue;
        }
        const auto out = lzss_decompress(*raw);
        REQUIRE(out.has_value());
        CHECK(out->consumed == raw->size());  // the size header is "size - 1": see docs/formats/data-dat.md
        if (detect_kind(i, out->data) == EntryKind::kXmidi) {
            ++xmidi;
        }
    }
    CHECK(xmidi == 63);
}
