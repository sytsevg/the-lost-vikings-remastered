#pragma once

// Dialogue: the text layer (40 x 22 cells of 8 x 8 pixels over the playfield) and the lines a conversation is made
// of: a framed box with a text, a wait for a key, a clear. The texts are imported from VIKINGS.EXE; who says what
// and when is the kinds' (docs/original/dialogue.md).

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "assets/graphics.hpp"
#include "lvr/names.hpp"

namespace lvr {

// The layer has room for the largest view (Remaster rules' zoom: 640 x 480 pixels); a row of it is always this
// many cells. How much of it is in use is the layer's own `columns` and `rows`: the original's 40 x 22
// unless the view is larger.
inline constexpr int kTextLayerColumns = 80;
inline constexpr int kTextLayerRows = 60;
inline constexpr int kOriginalTextColumns = 40;
inline constexpr int kOriginalTextRows = 22;
inline constexpr int kPasswordText = 0x10d;  // the text that goes before a level's password

// One step of a conversation.
struct Line {
    enum class Kind : std::uint8_t {
        kBox,         // a framed box at a cell, with a tail by its anchor, and the text inside
        kText,        // a text at a cell, without a frame
        kClear,       // the text layer emptied
        kWaitForKey,  // the game holds until an action key is pressed
        kColour,      // the boxes' inside colour: per speaker
        kCharacter,   // one character in a cell (the password's letters)
    };
    Kind kind = Kind::kClear;
    std::uint16_t column = 0;
    std::uint16_t row = 0;
    std::uint16_t anchor = 0;  // kBox: which cell of the box is at (column, row): where its tail points
    std::uint16_t text = 0;    // kBox, kText: where the text is in the texts
    std::uint16_t value = 0;   // kColour: 5 bits each of red, green, blue; kCharacter: the character
    // Its cell was worked out from a point of the level, in the layer as large as it is. Without this, the cell
    // is one of the original's 40 x 22, which a larger layer has in its middle.
    bool by_point = false;

    bool operator==(const Line&) const = default;
};

// The texts as VIKINGS.EXE has them: a table of where each text is, then the texts: the box's width and height in
// cells, the characters with 0x0d between lines, ended by 0.
using Texts = std::span<const std::uint8_t>;

// A box with text `number`, its anchor cell at (column, row), kept inside the screen.
std::optional<Line> box_at(Texts texts, int number, int anchor, int column, int row);
// The same with its anchor cell at a point of the level, as the view at (camera_x, camera_y) shows it: at least
// two cells from the left and the top, and inside a layer of `columns` x `rows`.
std::optional<Line> box_at_point(Texts texts, int number, int anchor, int x, int y, int camera_x, int camera_y,
                                 int columns = kOriginalTextColumns, int rows = kOriginalTextRows);
// Text `number` at a cell, without a frame.
std::optional<Line> text_at(Texts texts, int number, int column, int row);

class TextLayer {
  public:
    // What a line shows (a wait shows nothing).
    void show(const Line& line, Texts texts);
    // A draw of the layer: one after each line, and one with each of a tick's pictures. It matters only in the
    // scenes that draw their text straight onto the picture (`direct`): there the original draws it at most four
    // times after a clear, so text added later isn't seen until the next clear.
    void draw() noexcept;
    // The view changed: the layer is `columns_now` x `rows_now` cells from here on, and what it shows moves by
    // (`across`, `down`) cells, as the view's corner moved the other way. What then lies outside is gone.
    void resize(int columns_now, int rows_now, int across, int down);
    // The cells as they are seen.
    const std::array<std::uint8_t, std::size_t{kTextLayerColumns} * kTextLayerRows>& seen() const noexcept {
        return direct ? shown : cells;
    }

    std::array<std::uint8_t, std::size_t{kTextLayerColumns} * kTextLayerRows> cells{};  // characters; 0: nothing
    int columns = kOriginalTextColumns;                                                 // how much of it is in use
    int rows = kOriginalTextRows;
    std::optional<assets::Rgb> colour3;  // the boxes' inside colour, once a line has set it
    bool direct = false;                 // the scene draws its text onto the picture (the village, the ship, the stars)
    std::array<std::uint8_t, std::size_t{kTextLayerColumns} * kTextLayerRows> shown{};  // then: as last drawn
    bool pending = false;                                                               // text to draw
    int draws = 0;                                                                      // draws since the last clear

    bool operator==(const TextLayer&) const = default;

  private:
    void put_line(const Line& line, Texts texts);
    void put(std::size_t column, std::size_t row, std::uint8_t character);
    void put_text(std::size_t column, std::size_t row, std::size_t width, Texts texts, std::size_t offset);
};

}  // namespace lvr
