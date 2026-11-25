#include "lvr/sprites.hpp"

#include <algorithm>
#include <format>
#include <utility>

#include "assets/assets.hpp"
#include "assets/sprites.hpp"

namespace lvr {

namespace {

constexpr std::size_t kFrameBytes = 72;  // an 8 x 8 picture in the sprite memory
constexpr int kFirstBufferPiece = 24;    // the pieces that have a frame buffer: 24-39
constexpr int kColourGroupPiece = 12;    // see Player::colours
constexpr std::size_t kMaxInstructions = 10'000;
constexpr std::uint8_t kChanged = 2;  // a changed piece is drawn again in two pictures

std::uint16_t u16(unsigned value) noexcept {
    return static_cast<std::uint16_t>(value);
}

std::int16_t s16(unsigned value) noexcept {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(value));
}

void changed(Piece& piece) noexcept {
    piece.redraw = kChanged;
    piece.erase = kChanged;
}

// One step of one sprite's program.
class Player {
  public:
    Player(Pieces& pieces, Sprite& sprite, Body& body, const AnimationContext& context,
           std::vector<std::uint8_t>& sounds)
        : pieces_{pieces}, sprite_{sprite}, body_{body}, context_{context}, sounds_{sounds}, at_{sprite.program} {}

    std::uint16_t at() const noexcept { return at_; }

    enum class Next : std::uint8_t { kGoOn, kEndOfStep, kBroken };

    // One instruction (docs/original/animation.md).
    Next instruction() {
        const std::uint8_t op = byte();
        switch (op) {
            case 0x00: {  // every piece's picture: n pictures further
                const unsigned by = byte() * kFrameBytes;
                masked([by](Piece& piece) {
                    piece.frame = u16(piece.frame + by);
                    changed(piece);
                });
                return Next::kGoOn;
            }
            case 0x01:  // a picture of the entity's sprite set for each piece
                masked([this](Piece& piece) {
                    piece.frame = u16((byte() * kFrameBytes) + sprite_.base);
                    changed(piece);
                });
                return Next::kGoOn;
            case 0x02:  // a sound effect
                sounds_.push_back(static_cast<std::uint8_t>(word() & 0xffU));
                return Next::kGoOn;
            case 0x03:  // go to
                at_ = word();
                return Next::kGoOn;
            case 0x04:
            case 0x16:  // nothing, with a byte
                (void)byte();
                return Next::kGoOn;
            case 0x05: {  // call
                const std::uint16_t target = word();
                sprite_.return_to = at_;
                at_ = target;
                return Next::kGoOn;
            }
            case 0x06:  // return
                at_ = sprite_.return_to;
                return Next::kGoOn;
            case 0x07:
            case 0x09:
                move(op == 0x07);
                return Next::kGoOn;
            case 0x08:
            case 0x0A:
                place(op == 0x08);
                return Next::kGoOn;
            case 0x0B:  // nothing
                return Next::kGoOn;
            case 0x0C:
                colours();
                return Next::kGoOn;
            case 0x0D:  // from here to the end of the step: only the pieces of these groups
                mask_ = byte();
                return Next::kGoOn;
            case 0x0E:  // the step is over
                return Next::kEndOfStep;
            case 0x0F:  // wait n ticks
                sprite_.delay = byte();
                return Next::kEndOfStep;
            case 0x10:
            case 0x11:
            case 0x12:  // mirror, flip, or both
                masked([op](Piece& piece) {
                    piece.mirrored = piece.mirrored != (op != 0x11);
                    piece.flipped = piece.flipped != (op != 0x10);
                    changed(piece);
                });
                return Next::kGoOn;
            case 0x13:  // a group for each piece
                masked([this](Piece& piece) { piece.group = byte(); });
                return Next::kGoOn;
            case 0x14: {  // the frame of its packed set the sprite shows
                const std::uint16_t frame = byte();
                if (frame != sprite_.packed_frame) {
                    sprite_.packed_frame = frame;
                    changed(first());
                }
                return Next::kGoOn;
            }
            case 0x15:
                shape();
                return Next::kGoOn;
            case 0x17:
                packed_set();
                return Next::kGoOn;
            case 0x18:  // hide
                masked([](Piece& piece) {
                    piece.hidden = true;
                    piece.erase = kChanged;
                });
                return Next::kGoOn;
            case 0x19:  // show
                masked([](Piece& piece) {
                    piece.hidden = false;
                    piece.blinked_off = false;
                    piece.redraw = kChanged;
                    piece.erase = 0;
                });
                return Next::kGoOn;
            case 0x1A:  // the end
                at_ = kNoAnimation;
                return Next::kEndOfStep;
            default:
                return Next::kBroken;
        }
    }

  private:
    std::uint8_t byte() {
        const std::uint8_t value =
            at_ < context_.programs.size() ? std::to_integer<std::uint8_t>(context_.programs[at_]) : std::uint8_t{0x1A};
        at_ = u16(at_ + 1U);
        return value;
    }
    std::uint16_t word() {
        const unsigned low = byte();
        return u16(low | (unsigned{byte()} << 8U));
    }

    Piece& piece(int index) { return pieces_.at(static_cast<std::size_t>(index) % pieces_.size()); }
    Piece& first() { return piece(sprite_.first); }

    // The sprite's pieces. A sprite without pieces still has the instruction act on the piece where its pieces
    // would start, as the original's loops test after the first.
    template <typename F>
    void each(const F& act) {
        int index = sprite_.first;
        do {
            act(piece(index));
            ++index;
        } while (index < sprite_.first + sprite_.count);
    }
    // The same, under a mask only the pieces of its groups.
    template <typename F>
    void masked(const F& act) {
        each([&](Piece& one) {
            if (mask_ == 0 || (one.group & mask_) != 0) {
                act(one);
            }
        });
    }

    // 07 / 09: a move by n pixels across / down, the other way for an entity facing left / upside down. Under a
    // mask the pieces move; without one the entity does, this tick.
    void move(bool across) {
        unsigned by = static_cast<std::uint16_t>(static_cast<std::int8_t>(byte()));
        if (across ? body_.facing_left : body_.flipped) {
            by = u16(0U - by);
        }
        if (mask_ != 0) {
            masked([across, by](Piece& one) {
                (across ? one.x : one.y) = s16(static_cast<std::uint16_t>(across ? one.x : one.y) + by);
                changed(one);
            });
            return;
        }
        std::int16_t& push = across ? body_.push_x : body_.push_y;
        push = s16(static_cast<std::uint16_t>(push) + ((by & 0xffU) << 8U));
        changed(first());
    }

    // 08 / 0A: each piece at the entity's position plus its own offset; across, mirrored around the entity if it
    // faces left. (The original's 0A then mirrors down on a test that reads past its tables; it never shows.)
    void place(bool across) {
        const auto origin = static_cast<std::uint16_t>(across ? body_.x : body_.y);
        each([&](Piece& one) {
            const std::int16_t value = s16(unsigned{word()} + origin);
            (across ? one.x : one.y) = value;
            (across ? one.x_placed : one.y_placed) = true;
            changed(one);
        });
        if (across && body_.facing_left && sprite_.count != 0) {
            const unsigned twice = static_cast<std::uint16_t>(body_.x) * 2U;
            each([twice](Piece& one) {
                one.x = s16(twice - static_cast<std::uint16_t>(one.x) - one.size);
                one.mirrored = true;
                changed(one);
            });
        }
    }

    // 0C: which 16 colours each piece is drawn in. Under a mask the original looks at one fixed piece's group (the
    // 13th of the layer), not at each piece's: all of the sprite's pieces, or none.
    void colours() {
        const bool all = mask_ == 0 || (piece(kColourGroupPiece).group & mask_) != 0;
        if (!all) {
            return;
        }
        each([this](Piece& one) { one.colour = static_cast<std::uint8_t>((unsigned{byte()} >> 1U) & 7U); });
    }

    // 15: the pieces' shape by n: 0 is 8 x 8, 2 is 16 x 16, 1 is 32 wide; 3 and up are 32 wide with a height from
    // the table, drawn from the packed set's own bytes.
    void shape() {
        const std::uint8_t n = byte();
        const std::uint16_t size = context_.tables != nullptr ? context_.tables->piece_sizes.at(n) : std::uint16_t{0};
        const bool whole_set = n > 2;
        const auto bits = static_cast<std::uint8_t>(n == 0 ? 1 : (whole_set ? 2 : n * 2));
        masked([&](Piece& one) {
            one.size = size;
            one.shape = bits;
            changed(one);
            if (whole_set) {
                one.frame = 1;
                one.from = Piece::From::kPackedSet;
                sprite_.base = 1;
            }
        });
    }

    // 17: the sprite's frames come from packed set `entry` (the level's first, if it doesn't load that one), each
    // piece's unpacked into its own buffer.
    void packed_set() {
        const std::uint16_t entry = word();
        std::uint16_t set = 0;
        bool loaded = false;
        for (std::size_t i = 0; i < context_.packed_entries.size(); ++i) {
            if (context_.packed_entries[i] == entry) {
                set = u16(static_cast<unsigned>(i));
                loaded = true;
                break;
            }
        }
        if (!loaded && context_.unloaded != nullptr) {
            context_.unloaded->push_back(entry);
        }
        int index = sprite_.first;
        each([&](Piece& one) {
            const PieceLayout& layout = pieces_.layout();
            const int place = index++ % layout.size;
            const int buffer = place - kFirstBufferPiece;
            one.packed_set = set;
            one.from = Piece::From::kUnpacked;
            if (layout.buffer_each) {  // its own buffer, from the start of it (PiecePainter finds it by the piece)
                one.frame =
                    place >= layout.middle_begin && place < layout.middle_end ? std::uint16_t{1} : std::uint16_t{0};
            } else {
                one.frame = context_.tables != nullptr && buffer >= 0 && buffer < 16
                                ? context_.tables->buffer_offsets.at(static_cast<std::size_t>(buffer))
                                : std::uint16_t{0};
            }
            one.shape |= 2U;
            one.packed = true;
            one.redraw = kChanged;
        });
        sprite_.packed_frame = kNoPackedFrame;
    }

    Pieces& pieces_;
    Sprite& sprite_;
    Body& body_;
    const AnimationContext& context_;
    std::vector<std::uint8_t>& sounds_;
    std::uint16_t at_;
    std::uint8_t mask_ = 0;
};

}  // namespace

// --- The layer ---

bool is_free(const Piece& piece) noexcept {
    return !piece.in_use && !piece.hidden && !piece.blinked_off && !piece.mirrored && !piece.flipped &&
           !piece.x_placed && !piece.y_placed && !piece.packed && piece.shape == 0 && piece.colour == 0 &&
           piece.redraw == 0 && piece.erase == 0;
}

bool take_pieces(Pieces& pieces, Sprite& sprite, Pool pool, const PieceStart& start) {
    constexpr std::uint8_t kFirstDraws = 4;
    if (sprite.count <= 0) {
        return true;
    }
    const PieceLayout& layout = pieces.layout();
    int at = pool == Pool::kBack ? layout.back_begin : (pool == Pool::kMiddle ? layout.middle_begin : 0);
    const int pool_end =
        pool == Pool::kBack ? layout.size : (pool == Pool::kMiddle ? layout.middle_end : layout.front_end);
    const auto free_at = [&pieces](int index) { return is_free(pieces.at(static_cast<std::size_t>(index))); };
    // The first free piece, then whether the ones after it are free too; if one isn't, on from that one.
    int first = -1;
    while (first < 0) {
        while (!free_at(at)) {
            if (++at >= pool_end) {
                return false;
            }
        }
        first = at;
        while (true) {
            ++at;
            if (at == first + sprite.count) {
                break;
            }
            if (at == pool_end) {
                return false;
            }
            if (!free_at(at)) {
                first = -1;
                break;
            }
        }
    }
    sprite.first = first;
    for (int index = first; index < first + sprite.count; ++index) {
        Piece& piece = pieces.at(static_cast<std::size_t>(index));
        // Its place, group aside, is left as the last owner had it until an animation puts it somewhere.
        piece.in_use = true;
        piece.hidden = false;
        piece.blinked_off = false;
        piece.mirrored = start.mirrored;
        piece.flipped = start.flipped;
        piece.x_placed = false;
        piece.y_placed = false;
        piece.shape = start.large ? 2 : 1;
        piece.packed = false;
        piece.colour = start.colour;
        piece.group = 0;
        piece.size = start.large ? 32 : 8;
        piece.from = Piece::From::kSpriteMemory;
        piece.frame = sprite.base;
        piece.redraw = kFirstDraws;
        piece.erase = kChanged;
    }
    return true;
}

void let_go(Pieces& pieces, const Sprite& sprite) {
    constexpr std::uint8_t kErases = 4;
    for (int index = sprite.first; index < sprite.first + sprite.count; ++index) {
        Piece& piece = pieces.at(static_cast<std::size_t>(index));
        piece.in_use = false;
        piece.hidden = false;
        piece.blinked_off = false;
        piece.mirrored = false;
        piece.flipped = false;
        piece.x_placed = false;
        piece.y_placed = false;
        piece.packed = false;
        piece.shape = 0;
        piece.colour = 0;
        piece.redraw = 0;
        piece.erase = kErases;
    }
}

void count_picture(Pieces& pieces) noexcept {
    for (Piece& piece : pieces) {
        if (piece.erase != 0) {
            --piece.erase;
        }
    }
    for (Piece& piece : pieces) {
        if (piece.in_use && !piece.hidden && !piece.blinked_off && piece.redraw != 0) {
            --piece.redraw;
        }
    }
}

int share_of_move(int distance, int phase) noexcept {
    const int share = ((distance < 0 ? -distance : distance) + 2 - phase) / 3;
    return distance < 0 ? -share : share;
}

void shift_pieces(Pieces& pieces, const Sprite& sprite, int dx, int dy) {
    int index = sprite.first;
    do {  // at least the piece its pieces start at, as the original
        Piece& piece = pieces.at(static_cast<std::size_t>(index) % pieces.size());
        piece.x = s16(static_cast<std::uint16_t>(piece.x) + static_cast<unsigned>(dx));
        piece.y = s16(static_cast<std::uint16_t>(piece.y) + static_cast<unsigned>(dy));
        changed(piece);
        ++index;
    } while (index < sprite.first + sprite.count);
}

// --- Pictures ---

std::optional<std::size_t> PiecePainter::buffer_of(const Pieces& pieces, std::size_t index) {
    const Piece& piece = pieces.at(index);
    if (piece.frame == 0) {
        return std::nullopt;
    }
    const PieceLayout& layout = pieces.layout();
    if (!layout.buffer_each) {
        return std::size_t{piece.frame} - 1;
    }
    if (std::cmp_less(index, layout.middle_begin) || std::cmp_greater_equal(index, layout.middle_end)) {
        return std::nullopt;
    }
    return ((index - static_cast<std::size_t>(layout.middle_begin)) * kBufferBytes) + piece.frame - 1;
}

void PiecePainter::unpack(const Pieces& pieces, const Sprite& sprite, const SpriteMemory& memory) {
    constexpr int kGroups = 4 * 32;  // 4 planes of 32 rows: a mask byte and up to 8 pixels each
    if (sprite.count <= 0 || sprite.packed_frame == kNoPackedFrame || sprite.first < 0 ||
        std::cmp_greater_equal(sprite.first, pieces.size())) {
        return;
    }
    const Piece& piece = pieces.at(static_cast<std::size_t>(sprite.first));
    const auto buffer = buffer_of(pieces, static_cast<std::size_t>(sprite.first));
    if (piece.from != Piece::From::kUnpacked || !buffer || piece.packed_set >= memory.packed_sets.size()) {
        return;
    }
    if (const PieceLayout& layout = pieces.layout(); layout.buffer_each) {
        buffers_.resize(std::max(buffers_.size(),
                                 static_cast<std::size_t>(layout.middle_end - layout.middle_begin) * kBufferBytes));
    }
    // The set starts with a table of where each frame is. A frame is, per plane and row, a byte that says which
    // of 8 pixels are there, then those pixels, two to a byte (the first in the high half).
    const std::span<const std::byte> set = memory.packed_sets[piece.packed_set];
    const std::size_t entry = std::size_t{sprite.packed_frame} * 2;
    if (entry + 1 >= set.size()) {
        return;
    }
    std::size_t in = std::to_integer<std::size_t>(set[entry]) | (std::to_integer<std::size_t>(set[entry + 1]) << 8U);
    std::size_t out = *buffer;
    const unsigned colours = 0x80U | (unsigned{piece.colour} << 4U);
    const auto next = [&set, &in]() { return in < set.size() ? std::to_integer<unsigned>(set[in++]) : 0U; };
    const auto store = [this, &out](unsigned value) {
        if (out < buffers_.size()) {
            buffers_[out] = static_cast<std::byte>(value);
        }
        ++out;
    };
    for (int group = 0; group < kGroups; ++group) {
        const unsigned present = next();
        store(present);
        bool half = false;  // the low half of `pair` is still to come
        unsigned pair = 0;
        for (unsigned bit = 0x80; bit != 0; bit >>= 1U) {
            if ((present & bit) == 0) {
                store(0);
            } else if (half) {
                store((pair & 0x0fU) | colours);
                half = false;
            } else {
                pair = next();
                store((pair >> 4U) | colours);
                half = true;
            }
        }
    }
}

std::vector<PiecePicture> PiecePainter::pictures(const Pieces& pieces, const SpriteMemory& memory) const {
    constexpr std::size_t kTallest = 176;
    std::vector<PiecePicture> drawn;
    for (std::size_t index = pieces.size(); index-- > 0;) {
        const Piece& piece = pieces[index];
        if (!piece.in_use || piece.hidden || piece.blinked_off) {
            continue;
        }
        std::size_t width = 0;
        std::size_t height = 0;
        if (piece.shape == 1) {
            width = height = 8;
        } else if (piece.shape == 4) {
            width = height = 16;
        } else if (piece.shape == 2) {
            width = 32;
            height = piece.size;
        } else {
            continue;
        }
        std::span<const std::byte> bytes = memory.sets;
        std::size_t start = piece.frame == 0 ? 0 : std::size_t{piece.frame} - 1;
        if (piece.from == Piece::From::kUnpacked) {
            bytes = buffers_;
            const auto buffer = buffer_of(pieces, index);
            if (!buffer) {
                continue;
            }
            start = *buffer;
        } else if (piece.from == Piece::From::kPackedSet) {
            bytes = piece.packed_set < memory.packed_sets.size()
                        ? std::span<const std::byte>{memory.packed_sets[piece.packed_set]}
                        : std::span<const std::byte>{};
        }
        const std::size_t size = width * height * 9 / 8;  // a mask byte to every 8 pixels
        if (piece.frame == 0 || height == 0 || height > kTallest || start + size > bytes.size()) {
            continue;
        }
        auto image = assets::decode_unpacked_frame(bytes.subspan(start, size), width, height);
        if (!image) {
            continue;
        }
        drawn.push_back(PiecePicture{.image = std::move(*image),
                                     .x = piece.x,
                                     .y = piece.y,
                                     .mirrored = piece.mirrored,
                                     .flipped = piece.flipped,
                                     .piece = index});
    }
    return drawn;
}

std::expected<AnimationTables, std::string> load_animation_tables(std::span<const std::byte> exe,
                                                                  const assets::Release& release) {
    AnimationTables tables;
    const auto read = [&exe](assets::ExeRange range, std::span<std::uint16_t> into) {
        if (range.offset > exe.size() || range.size > exe.size() - range.offset || range.size < into.size() * 2) {
            return false;
        }
        for (std::size_t i = 0; i < into.size(); ++i) {
            into[i] = u16(std::to_integer<unsigned>(exe[range.offset + (2 * i)]) |
                          (std::to_integer<unsigned>(exe[range.offset + (2 * i) + 1]) << 8U));
        }
        return true;
    };
    if (!read(release.frame_offsets, tables.buffer_offsets) || !read(release.piece_sizes, tables.piece_sizes)) {
        return std::unexpected{std::format("{}: the animation tables are past the end of the file", assets::kExeName)};
    }
    return tables;
}

void start_animation(Sprite& sprite, std::uint16_t program) noexcept {
    sprite.program = program;
    sprite.started = program;
    sprite.delay = 1;
}

bool step_animation(Pieces& pieces, Sprite& sprite, Body& body, const AnimationContext& context,
                    std::vector<std::uint8_t>& sounds) {
    if (!sprite.animating()) {
        return true;
    }
    if (sprite.delay != 0) {
        --sprite.delay;
        if (sprite.delay != 0) {
            return true;  // still waiting
        }
    }
    Player player{pieces, sprite, body, context, sounds};
    for (std::size_t n = 0; n < kMaxInstructions; ++n) {
        switch (player.instruction()) {
            case Player::Next::kGoOn:
                continue;
            case Player::Next::kEndOfStep:
                sprite.program = player.at();
                return true;
            case Player::Next::kBroken:
                sprite.program = kNoAnimation;
                return false;
        }
    }
    sprite.program = kNoAnimation;
    return false;
}

}  // namespace lvr
