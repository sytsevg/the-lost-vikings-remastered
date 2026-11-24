// Tests for src/assets/text.*: a hand-made text segment, and [real-data] checks of the 390 texts in VIKINGS.EXE.
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

#include "assets/assets.hpp"
#include "assets/text.hpp"

using namespace lvr::assets;

namespace {

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

namespace {

std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (const int value : values) {
        out.push_back(static_cast<std::byte>(value));
    }
    return out;
}

}  // namespace

TEST_CASE("text: a hand-made segment") {
    // Three texts; the last two share an offset. "HI" / "YOU" in a 6x4 box, and an empty one in a 3x2 box.
    const auto segment = bytes({0x06, 0x00, 0x0f, 0x00, 0x0f, 0x00,               // offsets
                                0x06, 0x04, 'H', 'I', 0x0d, 'Y', 'O', 'U', 0x00,  // 06: HI / YOU
                                0x03, 0x02, 0x00});                               // 0F: empty
    const auto texts = parse_dialogue_texts(segment);
    REQUIRE(texts.has_value());
    REQUIRE(texts->size() == 3);
    CHECK((*texts)[0].offset == 6);
    CHECK((*texts)[0].width == 6);
    CHECK((*texts)[0].height == 4);
    CHECK((*texts)[0].lines == std::vector<std::string>{"HI", "YOU"});
    CHECK((*texts)[1].offset == 0x0f);
    CHECK((*texts)[1].width == 3);
    CHECK((*texts)[1].lines == std::vector<std::string>{""});
    CHECK((*texts)[2].offset == 0x0f);
}

TEST_CASE("text: errors") {
    CHECK(parse_dialogue_texts(bytes({})).error() == Error::kTruncated);
    CHECK(parse_dialogue_texts(bytes({0x00, 0x00})).error() == Error::kBadOffsetTable);        // no texts
    CHECK(parse_dialogue_texts(bytes({0x03, 0x00, 0x00})).error() == Error::kBadOffsetTable);  // odd size
    CHECK(parse_dialogue_texts(bytes({0x04, 0x00})).error() == Error::kBadOffsetTable);        // table past the end
    CHECK(parse_dialogue_texts(bytes({0x04, 0x00, 0x00, 0x00})).error() == Error::kOffsetOutOfRange);  // in table
    CHECK(parse_dialogue_texts(bytes({0x02, 0x00, 0x01})).error() == Error::kOffsetOutOfRange);        // no size bytes
    CHECK(parse_dialogue_texts(bytes({0x02, 0x00, 0x05, 0x03, 'A'})).error() == Error::kTruncated);    // no 0x00
}

TEST_CASE("text: the 390 dialogue texts in VIKINGS.EXE", "[real-data]") {
    const GameFiles& files = game_or_skip();
    const auto texts = load_dialogue_texts(files.exe(), files.release());
    REQUIRE(texts.has_value());
    REQUIRE(texts->size() == 390);
    REQUIRE((*texts)[0].lines.size() == 1);
    CHECK((*texts)[0].lines[0].size() == 13);
    CHECK((*texts)[0].width == 15);
    CHECK((*texts)[0].height == 3);
    REQUIRE((*texts)[20].lines.size() == 2);
    CHECK((*texts)[20].lines[0].size() == 14);
    CHECK((*texts)[20].lines[1].size() == 16);
    CHECK((*texts)[20].width == 18);
    CHECK((*texts)[20].height == 4);
    CHECK((*texts)[16].offset == (*texts)[17].offset);
    for (const DialogueText& text : *texts) {
        INFO("text at " << text.offset);
        // Every line fits inside the box's frame, and uses only characters of the upper-case font.
        CHECK(text.lines.size() + 2 <= text.height);
        for (const std::string& line : text.lines) {
            CHECK(line.size() + 2 <= text.width);
            for (const char c : line) {
                CHECK((c >= ' ' && c <= 'Z'));
            }
        }
    }
}

TEST_CASE("text: the dialogue range must lie inside VIKINGS.EXE") {
    const std::vector<std::byte> exe(16);
    const Release release{.name = "test", .dialogue_texts = {.offset = 8, .size = 9}};
    CHECK_FALSE(load_dialogue_texts(exe, release).has_value());
    const Release past_end{.name = "test", .dialogue_texts = {.offset = 17, .size = 0}};
    CHECK_FALSE(load_dialogue_texts(exe, past_end).has_value());
}
