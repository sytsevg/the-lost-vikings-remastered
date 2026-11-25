#pragma once

// Sprites as the original has them: an entity's sprite is one or more pieces (8 x 8, 16 x 16 or 32 pixels wide),
// taken from a layer of 128. Animations are imported: the original's animation programs, played here on the pieces
// (docs/architecture.md, "Decided 2026-11-18"; the instructions are listed in docs/original/animation.md).

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "assets/data_dat.hpp"
#include "assets/graphics.hpp"
#include "lvr/archive.hpp"

namespace lvr {

inline constexpr int kPieces = 128;
inline constexpr std::uint16_t kNoAnimation = 0xffff;
inline constexpr std::uint16_t kNoPackedFrame = 0xffff;

struct Piece {
    // Where its picture is read from.
    enum class From : std::uint8_t {
        kSpriteMemory,  // the level's sprite sets, loaded back to back
        kUnpacked,      // its own buffer, holding a frame unpacked from a packed set (the Vikings, the enemies)
        kPackedSet,     // a packed set's bytes as they are (the game-over scene's lightning)
    };

    bool in_use = false;
    bool hidden = false;
    bool blinked_off = false;  // hidden by a blink (a Viking that was hit)
    bool mirrored = false;
    bool flipped = false;
    bool x_placed = false;  // an animation has put it somewhere
    bool y_placed = false;
    std::uint8_t shape = 0;  // 1: 8 x 8; 4: 16 x 16; 2: 32 wide and `size` high (the original's bits, kept as they
                             // are: a piece of a packed set also has `packed`)
    bool packed = false;
    std::uint8_t colour = 0;  // 0-7: which 16 colours a packed frame is drawn in
    std::uint8_t group = 0;   // an animation can address the pieces of some groups only
    std::int16_t x = 0;       // the top-left corner, in the level
    std::int16_t y = 0;
    std::uint16_t size = 0;
    From from = From::kSpriteMemory;
    std::uint16_t frame = 0;       // where the picture starts in its memory, + 1; 0: none
    std::uint16_t packed_set = 0;  // which of the level's packed sets its frames are unpacked from
    // Draw counts: set when the piece changes, counted down as it is drawn and erased. A piece that was let go is
    // free again only when both are 0.
    std::uint8_t redraw = 0;
    std::uint8_t erase = 0;

    bool operator==(const Piece&) const = default;
};

// How a layer is laid out: its size, and which pieces each of its three pools has (below).
struct PieceLayout {
    int size = 0;
    int front_end = 0;  // the front pool: [0, front_end)
    int middle_begin = 0;
    int middle_end = 0;
    int back_begin = 0;  // the back pool: [back_begin, size)
    // The middle pool's frame buffers: the original's 16, at the places its table gives (false), or one for every
    // piece of the pool, by its number.
    bool buffer_each = false;

    bool operator==(const PieceLayout&) const = default;
};
// The original's: 128 pieces; the middle and the back pool share four.
inline constexpr PieceLayout kOriginalPieces{
    .size = kPieces, .front_end = 24, .middle_begin = 24, .middle_end = 40, .back_begin = 36, .buffer_each = false};
// Remaster rules': room no level fills, in the same order, so what is in front of what stays as it is.
inline constexpr PieceLayout kRemasterPieces{
    .size = 2048, .front_end = 256, .middle_begin = 256, .middle_end = 512, .back_begin = 512, .buffer_each = true};

// The layer.
class Pieces {
  public:
    Pieces() : Pieces{kOriginalPieces} {}
    explicit Pieces(const PieceLayout& layout) : layout_{layout}, all_(static_cast<std::size_t>(layout.size)) {}

    const PieceLayout& layout() const noexcept { return layout_; }
    std::size_t size() const noexcept { return all_.size(); }
    Piece& at(std::size_t index) { return all_.at(index); }
    const Piece& at(std::size_t index) const { return all_.at(index); }
    Piece& operator[](std::size_t index) { return all_[index]; }
    const Piece& operator[](std::size_t index) const { return all_[index]; }
    auto begin() noexcept { return all_.begin(); }
    auto end() noexcept { return all_.end(); }
    auto begin() const noexcept { return all_.begin(); }
    auto end() const noexcept { return all_.end(); }

    bool operator==(const Pieces&) const = default;

    // Written out and read back (lvr/archive.hpp): the layout, how many pieces, then by turns how many in a row are
    // as they were made (never used: most of Remaster rules' 2048) and one that isn't. (A piece that was let go
    // keeps its place for the next one to have it, so it is written whole.)
    void save(Writer& to) const;
    void load(Reader& from);

  private:
    PieceLayout layout_;
    std::vector<Piece> all_;
};

// A pace, in 16ths of the original's: how fast an animation goes on (Sprite::pace), or a Viking walks (World::pace).
// Remaster rules' only (a gamepad's stick pushed part way); the original's is always all of it.
inline constexpr std::uint8_t kFullPace = 16;
// Remaster rules: how far a gamepad's stick has the Vikings walk (`across`: left or right, and steer in the air) and
// climb (`up_down`), in 16ths, 1 to kFullPace (all of it, as anything but a stick does).
struct Pace {
    std::uint8_t across = kFullPace;
    std::uint8_t up_down = kFullPace;

    bool operator==(const Pace&) const = default;
};

// An entity's pieces and the animation it is playing.
struct Sprite {
    int first = 0;  // its pieces in the layer: [first, first + count)
    int count = 0;
    std::uint16_t base = 0;                       // where its sprite set starts in the sprite memory, + 1
    std::uint16_t packed_frame = kNoPackedFrame;  // the frame of its packed set its buffer holds
    std::uint16_t program = kNoAnimation;         // where its animation is in the world's programs
    std::uint16_t delay = 0;                      // ticks until the animation goes on
    std::uint16_t return_to = 0;                  // an animation calls one level deep
    // Where the animation it was last started on begins (kNoAnimation: none yet). The game never reads it: it is
    // what the Modern visuals' effects answer to (effects.hpp).
    std::uint16_t started = kNoAnimation;
    // Remaster rules: how fast the animation goes on, a step every tick at kFullPace; slower, `paced` adds up
    // the pace each tick and it goes on when that reaches a whole step. Starting an animation sets it back to full.
    std::uint8_t pace = kFullPace;
    std::uint8_t paced = 0;

    bool animating() const noexcept { return program != kNoAnimation; }
    bool operator==(const Sprite&) const = default;
};

// What of its entity an animation reads and moves.
struct Body {
    std::int16_t x = 0;
    std::int16_t y = 0;
    bool facing_left = false;
    std::int16_t push_x = 0;  // this tick's move so far, in 256ths of a pixel
    std::int16_t push_y = 0;
    bool flipped = false;  // upside down

    bool operator==(const Body&) const = default;
};

// --- The layer: who has which pieces ---

// The layer is three pools; a kind draws its pieces from one of them. A lower piece is drawn over a higher one, so
// the pools are also what is in front of what.
enum class Pool : std::uint8_t {
    kBack,    // kinds with a sprite set of the level's own (doors, items, machines ...): the original's pieces 36-127
    kMiddle,  // kinds drawn from packed sets (the Vikings, the enemies); each has a frame buffer: 24-39
    kFront,   // 0-23
};

// How a new entity's pieces start out.
struct PieceStart {
    std::uint8_t colour = 0;
    bool mirrored = false;
    bool flipped = false;
    bool large = false;  // 32 pixels wide and high; otherwise 8 x 8
};

// A piece nobody has and whose last picture has been erased.
bool is_free(const Piece& piece) noexcept;

// Takes `sprite.count` pieces next to each other for the sprite: the first free run in the pool, as the original
// looks for it. False if the pool has no room (then the entity can't be created). The pieces show the sprite's set
// (`sprite.base`) and are due to be drawn.
bool take_pieces(Pieces& pieces, Sprite& sprite, Pool pool, const PieceStart& start);

// Lets the sprite's pieces go: nobody's from now on, and free once the next pictures have erased them.
void let_go(Pieces& pieces, const Sprite& sprite);

// What a picture being drawn does to the counts: every piece's erase count and every shown piece's draw count go
// down by one.
void count_picture(Pieces& pieces) noexcept;

// A tick's move shown over its three pictures: how much of `distance` picture `phase` (0, 1, 2) takes. The three
// add up to the distance: 7 is 3, 2, 2.
int share_of_move(int distance, int phase) noexcept;

// Moves the sprite's pieces; they are due to be drawn again.
void shift_pieces(Pieces& pieces, const Sprite& sprite, int dx, int dy);

// --- Pictures ---

// The sprite data a level or cutscene loads.
struct SpriteMemory {
    std::vector<std::byte> sets;                      // its sprite sets, back to back: frames are offsets into it
    std::vector<std::vector<std::byte>> packed_sets;  // its packed sets, each with a table of its frames
};

// A piece as the renderer draws it.
struct PiecePicture {
    assets::IndexedImage image;  // colour 0 is see-through
    int x = 0;                   // its top-left corner in the level
    int y = 0;
    bool mirrored = false;
    bool flipped = false;
    std::size_t piece = 0;  // which of the layer's pieces it is
};

// Makes the pictures of the pieces. It keeps the buffers that packed frames are unpacked into, which a piece
// shows until something else is unpacked there.
class PiecePainter {
  public:
    // The sprite's packed frame into its first piece's buffer, in that piece's colours. For every sprite, before
    // pictures().
    void unpack(const Pieces& pieces, const Sprite& sprite, const SpriteMemory& memory);
    // The pieces in use and shown, in the order they are drawn: the last piece first, so a lower piece is on top.
    std::vector<PiecePicture> pictures(const Pieces& pieces, const SpriteMemory& memory) const;

    // A frame buffer's room where every piece of the middle pool has its own: more than the largest frame needs.
    static constexpr std::size_t kBufferBytes = 2048;

  private:
    // Where a piece's unpacked frame is in the buffers; none: nowhere.
    static std::optional<std::size_t> buffer_of(const Pieces& pieces, std::size_t index);
    std::vector<std::byte> buffers_ = std::vector<std::byte>(0x10000);
};

// Tables of VIKINGS.EXE two instructions read.
struct AnimationTables {
    std::array<std::uint16_t, 16> buffer_offsets{};  // where each piece's unpacked frame is in the frame buffer
    std::array<std::uint16_t, 256> piece_sizes{};    // a 32-wide piece's height, by the instruction's operand
};

std::expected<AnimationTables, std::string> load_animation_tables(std::span<const std::byte> exe,
                                                                  const assets::Release& release);

// What an animation step works with besides the sprite itself.
struct AnimationContext {
    std::span<const std::byte> programs;  // the world's object database: programs are offsets into it
    const AnimationTables* tables = nullptr;
    std::span<const std::uint16_t> packed_entries;   // the level's packed sets, as DATA.DAT entries
    std::vector<std::uint16_t>* unloaded = nullptr;  // if given: the packed sets asked for that the level doesn't load
};

// Starts the program at `program`; it runs at the next step.
void start_animation(Sprite& sprite, std::uint16_t program) noexcept;

// One tick of the sprite's animation: a running wait counts down; when it is over the program goes on until it
// waits again or ends. Sound effects it asks for are added to `sounds`. False if the program is broken (an unknown
// instruction, or one that never waits): the animation is stopped.
bool step_animation(Pieces& pieces, Sprite& sprite, Body& body, const AnimationContext& context,
                    std::vector<std::uint8_t>& sounds);

}  // namespace lvr
