#include "assets/text.hpp"

#include <cstddef>

namespace lvr::assets {

std::expected<std::vector<DialogueText>, Error> parse_dialogue_texts(std::span<const std::byte> segment) {
    ByteReader table{segment};
    const auto table_size = table.u16le();
    if (!table_size) {
        return std::unexpected{Error::kTruncated};
    }
    if (*table_size == 0 || *table_size % 2 != 0 || *table_size > segment.size()) {
        return std::unexpected{Error::kBadOffsetTable};
    }
    std::vector<DialogueText> texts(*table_size / 2U);
    if (const auto rewound = table.seek(0); !rewound) {
        return std::unexpected{rewound.error()};
    }
    for (DialogueText& text : texts) {
        const auto offset = table.u16le();
        if (!offset) {
            return std::unexpected{offset.error()};
        }
        if (*offset < *table_size || std::size_t{*offset} + 2 > segment.size()) {
            return std::unexpected{Error::kOffsetOutOfRange};
        }
        text.offset = *offset;
        text.width = std::to_integer<std::uint8_t>(segment[*offset]);
        text.height = std::to_integer<std::uint8_t>(segment[*offset + 1U]);
        std::string line;
        std::size_t pos = *offset + 2U;
        for (;; ++pos) {
            if (pos >= segment.size()) {
                return std::unexpected{Error::kTruncated};  // no 0x00 at the end
            }
            const char c = std::to_integer<char>(segment[pos]);
            if (c == '\0') {
                break;
            }
            if (c == '\r') {
                text.lines.push_back(std::move(line));
                line.clear();
            } else {
                line += c;
            }
        }
        text.lines.push_back(std::move(line));
    }
    return texts;
}

}  // namespace lvr::assets
