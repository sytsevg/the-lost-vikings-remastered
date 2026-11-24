#include "lvr/dialogue.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>

#include "lvr/names.hpp"

namespace lvr {

namespace {

// The frame's characters in the font, the tail's, and a space.
constexpr std::uint8_t kTopLeft = 0x12;
constexpr std::uint8_t kTop = 0x13;
constexpr std::uint8_t kTopRight = 0x14;
constexpr std::uint8_t kLeft = 0x15;
constexpr std::uint8_t kRight = 0x16;
constexpr std::uint8_t kBottomLeft = 0x17;
constexpr std::uint8_t kBottom = 0x18;
constexpr std::uint8_t kBottomRight = 0x19;
constexpr std::uint8_t kTailDown = 0x1A;
constexpr std::uint8_t kTailUp = 0x1B;
constexpr std::uint8_t kSpace = 0x20;
constexpr std::uint8_t kNewLine = 0x0D;

// Where text `number` starts.
std::optional<std::uint16_t> offset_of(Texts texts, int number) {
    const std::size_t entry = static_cast<std::size_t>(number) * 2;
    if (number < 0 || entry + 1 >= texts.size()) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(texts[entry] | (unsigned{texts[entry + 1]} << 8U));
}

struct Size {
    int width = 0;
    int height = 0;
};
std::optional<Size> size_at(Texts texts, std::size_t offset) {
    if (offset + 1 >= texts.size()) {
        return std::nullopt;
    }
    return Size{.width = texts[offset], .height = texts[offset + 1]};
}

// The cell of a box that sits at its position: in the bottom row for a tail below (and for none), the top row for
// one above; in the second column for a tail at the left, the last but one for one at the right, the middle for
// the rest.
Size anchor_cell(int anchor, Size box) {
    Size cell{.width = box.width / 2, .height = 0};
    if (anchor == tail::kBelowLeft || anchor == tail::kAboveLeft) {
        cell.width = 1;
    } else if (anchor == tail::kBelowRight || anchor == tail::kAboveRight) {
        cell.width = box.width - 2;
    }
    if (anchor == tail::kBelowMiddle || anchor == tail::kBelowLeft || anchor == tail::kBelowRight ||
        anchor == tail::kNone) {
        cell.height = box.height - 1;
    }
    return cell;
}

Line box(std::uint16_t offset, int anchor, int column, int row, Size size, int columns = kOriginalTextColumns,
         int rows = kOriginalTextRows) {
    // Kept inside the screen's cells: left of the last column, and above the row after the last.
    const int last_column = columns - 1;
    if (wrapped(column + size.width) >= last_column) {
        column = last_column - 1 - size.width;
    }
    if (wrapped(row + size.height) >= rows) {
        row = rows - 1 - size.height;
    }
    return Line{.kind = Line::Kind::kBox,
                .column = static_cast<std::uint16_t>(column),
                .row = static_cast<std::uint16_t>(row),
                .anchor = static_cast<std::uint16_t>(anchor),
                .text = offset};
}

}  // namespace

std::optional<Line> box_at(Texts texts, int number, int anchor, int column, int row) {
    const auto offset = offset_of(texts, number);
    const auto size = offset ? size_at(texts, *offset) : std::nullopt;
    if (!size) {
        return std::nullopt;
    }
    return box(*offset, anchor, column, row, *size);
}

std::optional<Line> box_at_point(Texts texts, int number, int anchor, int x, int y, int camera_x, int camera_y,
                                 int columns, int rows) {
    const auto offset = offset_of(texts, number);
    const auto size = offset ? size_at(texts, *offset) : std::nullopt;
    if (!size) {
        return std::nullopt;
    }
    const Size cell = anchor_cell(anchor, *size);
    // In cells from the view's corner, less the anchor's cell; never left of column 2 or above row 2.
    const auto cells = [](int position, int camera, int anchor_at) {
        const int on_screen = wrapped(position - camera);
        if (on_screen < 0) {
            return 2;
        }
        const int at = wrapped((static_cast<std::uint16_t>(on_screen) >> 3U) - anchor_at);
        return at < 2 ? 2 : at;
    };
    Line line =
        box(*offset, anchor, cells(x, camera_x, cell.width), cells(y, camera_y, cell.height), *size, columns, rows);
    line.by_point = true;
    return line;
}

std::optional<Line> text_at(Texts texts, int number, int column, int row) {
    const auto offset = offset_of(texts, number);
    if (!offset) {
        return std::nullopt;
    }
    return Line{.kind = Line::Kind::kText,
                .column = static_cast<std::uint16_t>(column),
                .row = static_cast<std::uint16_t>(row),
                .text = *offset};
}

void TextLayer::put(std::size_t column, std::size_t row, std::uint8_t character) {
    if (std::cmp_less(column, columns) && std::cmp_less(row, rows)) {
        cells.at((row * kTextLayerColumns) + column) = character;
    }
}

// Line by line; a new line first fills the rest of the line with spaces, so a shorter text clears a longer one.
void TextLayer::put_text(std::size_t column, std::size_t row, std::size_t width, Texts texts, std::size_t offset) {
    const std::size_t line = width >= 2 ? width - 2 : 0;
    std::size_t left = line;
    std::size_t x = column;
    for (std::size_t at = offset; at < texts.size() && texts[at] != 0; ++at) {
        if (texts[at] == kNewLine) {
            for (; left > 0; --left) {
                put(x++, row, kSpace);
            }
            left = line;
            x = column;
            ++row;
            continue;
        }
        put(x++, row, texts[at]);
        left = left > 0 ? left - 1 : 0;
    }
}

void TextLayer::draw() noexcept {
    if (!pending || !direct || draws > 3) {
        return;
    }
    ++draws;
    shown = cells;
}

void TextLayer::show(const Line& line, Texts texts) {
    put_line(line, texts);
    draw();
}

void TextLayer::resize(int columns_now, int rows_now, int across, int down) {
    columns_now = std::clamp(columns_now, 0, kTextLayerColumns);
    rows_now = std::clamp(rows_now, 0, kTextLayerRows);
    const auto moved = [&](const auto& from) {
        std::remove_cvref_t<decltype(from)> to{};
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                const int to_column = column + across;
                const int to_row = row + down;
                if (to_column >= 0 && to_column < columns_now && to_row >= 0 && to_row < rows_now) {
                    to.at((static_cast<std::size_t>(to_row) * kTextLayerColumns) +
                          static_cast<std::size_t>(to_column)) =
                        from.at((static_cast<std::size_t>(row) * kTextLayerColumns) + static_cast<std::size_t>(column));
                }
            }
        }
        return to;
    };
    cells = moved(cells);
    shown = moved(shown);
    columns = columns_now;
    rows = rows_now;
}

void TextLayer::put_line(const Line& given, Texts texts) {
    // A cell of the original's 40 x 22 is in the middle of a larger layer.
    Line line = given;
    if (!line.by_point) {
        line.column = static_cast<std::uint16_t>(line.column + ((columns - kOriginalTextColumns) / 2));
        line.row = static_cast<std::uint16_t>(line.row + ((rows - kOriginalTextRows) / 2));
    }
    if (line.kind == Line::Kind::kBox || line.kind == Line::Kind::kText || line.kind == Line::Kind::kCharacter) {
        pending = true;
    }
    switch (line.kind) {
        case Line::Kind::kBox: {
            const auto size = size_at(texts, line.text);
            if (!size || size->width < 2 || size->height < 2) {
                return;
            }
            const auto width = static_cast<std::size_t>(size->width);
            const auto height = static_cast<std::size_t>(size->height);
            for (std::size_t y = 0; y < height; ++y) {
                for (std::size_t x = 0; x < width; ++x) {
                    const bool left = x == 0;
                    const bool right = x == width - 1;
                    std::uint8_t character = kSpace;
                    if (y == 0) {
                        character = left ? kTopLeft : (right ? kTopRight : kTop);
                    } else if (y == height - 1) {
                        character = left ? kBottomLeft : (right ? kBottomRight : kBottom);
                    } else if (left || right) {
                        character = left ? kLeft : kRight;
                    }
                    put(line.column + x, line.row + y, character);
                }
            }
            if (line.anchor != tail::kNone) {  // the tail, in the frame at the anchor's cell
                const Size cell =
                    anchor_cell(line.anchor <= tail::kAboveRight ? line.anchor : tail::kAboveMiddle, *size);
                put(line.column + static_cast<std::size_t>(cell.width),
                    line.row + static_cast<std::size_t>(cell.height), cell.height != 0 ? kTailDown : kTailUp);
            }
            put_text(std::size_t{line.column} + 1, std::size_t{line.row} + 1, width, texts, std::size_t{line.text} + 2);
            return;
        }
        case Line::Kind::kText:
            put_text(line.column, line.row, 2, texts, line.text);
            return;
        case Line::Kind::kCharacter:
            put(line.column, line.row, static_cast<std::uint8_t>(line.value & 0xffU));
            return;
        case Line::Kind::kClear:
            cells.fill(0);
            shown.fill(0);
            pending = false;
            draws = 0;
            return;
        case Line::Kind::kColour: {
            // 5 bits a colour, doubled into the VGA's 6, shown as 8.
            const auto level = [](unsigned bits) {
                const unsigned vga = (bits & 31U) * 2U;
                return static_cast<std::uint8_t>((vga << 2U) | (vga >> 4U));
            };
            colour3 = assets::Rgb{.r = level(line.value), .g = level(line.value >> 5U), .b = level(line.value >> 10U)};
            return;
        }
        case Line::Kind::kWaitForKey:
            return;
    }
}

}  // namespace lvr
