// Tests for src/lvr/dialogue.*: by hand.
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "assets/assets.hpp"
#include "assets/objects.hpp"
#include "lvr/dialogue.hpp"

using namespace lvr;

namespace {

// Texts as VIKINGS.EXE has them: the table of offsets, then each text: width, height, characters, 0.
std::vector<std::uint8_t> make_texts(const std::vector<std::string>& texts,
                                     const std::vector<std::array<int, 2>>& sizes) {
    std::vector<std::uint8_t> bytes(texts.size() * 2);
    for (std::size_t i = 0; i < texts.size(); ++i) {
        bytes[2 * i] = static_cast<std::uint8_t>(bytes.size() & 0xffU);
        bytes[(2 * i) + 1] = static_cast<std::uint8_t>(bytes.size() >> 8U);
        bytes.push_back(static_cast<std::uint8_t>(sizes[i][0]));
        bytes.push_back(static_cast<std::uint8_t>(sizes[i][1]));
        for (const char c : texts[i]) {
            bytes.push_back(c == '\n' ? std::uint8_t{0x0D} : static_cast<std::uint8_t>(c));
        }
        bytes.push_back(0);
    }
    return bytes;
}

std::vector<std::uint8_t> sample_texts() {
    return make_texts({"ONE LINE!", "IT IS TWO LINES LONG\nAND THIS ONE.", "HI", "A\nBB\nCCC", ""},
                      {{11, 3}, {22, 4}, {4, 3}, {5, 5}, {1, 1}});
}

std::uint8_t cell(const TextLayer& layer, int column, int row) {
    return layer.cells.at((static_cast<std::size_t>(row) * kTextLayerColumns) + static_cast<std::size_t>(column));
}

}  // namespace

TEST_CASE("dialogue: a framed box with its text and tail; a shorter line clears to the frame") {
    const std::vector<std::uint8_t> texts = sample_texts();
    const auto line = box_at(texts, 0, 0, 5, 4);  // the first text: 11 x 3, the tail in the bottom row's middle
    REQUIRE(line);
    CHECK(line->kind == Line::Kind::kBox);
    TextLayer layer;
    layer.show(*line, texts);
    CHECK(cell(layer, 5, 4) == 0x12);
    CHECK(cell(layer, 6, 4) == 0x13);
    CHECK(cell(layer, 15, 4) == 0x14);
    CHECK(cell(layer, 5, 5) == 0x15);
    CHECK(cell(layer, 6, 5) == 'O');
    CHECK(cell(layer, 14, 5) == '!');
    CHECK(cell(layer, 15, 5) == 0x16);
    CHECK(cell(layer, 5, 6) == 0x17);
    CHECK(cell(layer, 10, 6) == 0x1A);  // the tail, pointing down
    CHECK(cell(layer, 15, 6) == 0x19);
    CHECK(cell(layer, 16, 4) == 0);

    // The second line of a two-line text; the first one's rest is spaces.
    layer.show(*box_at(texts, 1, 4, 2, 10), texts);
    CHECK(cell(layer, 3, 10) == 0x1B);  // anchor 4: the tail up, in the second column
    CHECK(cell(layer, 3, 11) == 'I');
    CHECK(cell(layer, 3, 12) == 'A');
    CHECK(cell(layer, 22, 11) == 'G');

    layer.show(Line{.kind = Line::Kind::kClear}, texts);
    CHECK(layer == TextLayer{});
}

TEST_CASE("dialogue: a box is kept on the screen; one at an entity is placed by its anchor") {
    const std::vector<std::uint8_t> texts = sample_texts();
    CHECK(
        box_at(texts, 1, 0, 30, 20) ==
        Line{.kind = Line::Kind::kBox, .column = 16, .row = 17, .anchor = 0, .text = box_at(texts, 1, 0, 0, 0)->text});
    CHECK_FALSE(box_at(texts, 99, 0, 0, 0));
    CHECK_FALSE(box_at(texts, -1, 0, 0, 0));
    // Erik at (132, 418 - 28) with the view at (0, 304): cell (16, 10); anchor 1 puts the box's second column
    // and bottom row there.
    const auto line = box_at_point(texts, 0, 1, 132, 390, 0, 304);
    REQUIRE(line);
    CHECK(line->column == 16 - 1);
    CHECK(line->row == 10 - 2);
    // Left of or above the view: at cell 2.
    const auto off = box_at_point(texts, 2, 3, -40, 300, 0, 304);
    REQUIRE(off);
    CHECK(off->column == 2);
    CHECK(off->row == 2);
    CHECK_FALSE(box_at_point(texts, 99, 0, 0, 0, 0, 0));
}

TEST_CASE("dialogue: text without a frame, a character, the boxes' colour") {
    const std::vector<std::uint8_t> texts = sample_texts();
    TextLayer layer;
    const auto text = text_at(texts, 2, 3, 2);
    REQUIRE(text);
    layer.show(*text, texts);
    CHECK(cell(layer, 3, 2) == 4);  // read from where the text starts: its size bytes first, as the original
    CHECK(cell(layer, 5, 2) == 'H');
    layer.show(Line{.kind = Line::Kind::kCharacter, .column = 39, .row = 21, .value = 'T'}, texts);
    CHECK(cell(layer, 39, 21) == 'T');
    layer.show(Line{.kind = Line::Kind::kCharacter, .column = 40, .row = 0, .value = 'T'}, texts);  // off the layer
    layer.show(Line{.kind = Line::Kind::kColour, .value = 0x12}, texts);
    REQUIRE(layer.colour3);
    CHECK(*layer.colour3 == assets::Rgb{.r = 146, .g = 0, .b = 0});  // 18 x 2 of 63
    layer.show(Line{.kind = Line::Kind::kWaitForKey}, texts);
    CHECK_FALSE(text_at(texts, 77, 0, 0));
    // A box too small to be one isn't drawn.
    const TextLayer before = layer;
    layer.show(*box_at(texts, 4, 0, 1, 1), texts);
    CHECK(layer == before);
}
