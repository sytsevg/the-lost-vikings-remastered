#pragma once

// Dialogue texts (docs/original/dialogue.md): the 390 speech-box texts stored in VIKINGS.EXE segment 07A6. Scripts
// show them by number (opcodes 41, 44, 45). Where the segment is in the executable is release knowledge
// (Release::dialogue_texts); this file only decodes the segment's bytes.

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "assets/reader.hpp"

namespace lvr::assets {

struct DialogueText {
    std::uint16_t offset = 0;  // in the segment; some texts share one
    std::uint8_t width = 0;    // the box, in 8x8 cells, frame included
    std::uint8_t height = 0;
    std::vector<std::string> lines;  // broken by hand: the game does no word wrap
};

// The segment starts with a table of u16 offsets, one per text; the first offset is the table's size. Each text is
// a width byte, a height byte, then characters with 0x0d between lines, ended by 0x00.
std::expected<std::vector<DialogueText>, Error> parse_dialogue_texts(std::span<const std::byte> segment);

}  // namespace lvr::assets
