// Tests for src/lvr/sprites.*: the animation player on hand-made programs, and the pieces' layer.
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

#include "assets/assets.hpp"
#include "assets/objects.hpp"
#include "lvr/sprites.hpp"
#include "replay/replay.hpp"

using namespace lvr;

namespace {

std::vector<std::byte> program(std::initializer_list<int> bytes) {
    std::vector<std::byte> out;
    for (const int b : bytes) {
        out.push_back(static_cast<std::byte>(b));
    }
    return out;
}

// A sprite of `count` pieces from piece 30 on, playing the program at 0.
struct Stage {
    Pieces pieces{};
    Sprite sprite{.first = 30, .count = 2, .base = 1};
    Body body{.x = 100, .y = 50};
    AnimationTables tables{};
    std::vector<std::byte> bytes;
    std::vector<std::uint16_t> packed_entries;
    std::vector<std::uint8_t> sounds;

    explicit Stage(std::initializer_list<int> code) : bytes{program(code)} {
        for (int i = 0; i < 2; ++i) {
            piece(i).in_use = true;
            piece(i).shape = 4;
            piece(i).size = 16;
        }
        start_animation(sprite, 0);
    }
    Piece& piece(int i) { return pieces.at(static_cast<std::size_t>(30 + i)); }
    bool step() {
        return step_animation(pieces, sprite, body,
                              AnimationContext{.programs = bytes, .tables = &tables, .packed_entries = packed_entries},
                              sounds);
    }
};

}  // namespace

TEST_CASE("animation: frames, waits, the end") {
    // A frame per piece, wait 3, both pieces two frames on, end of step, the end.
    Stage stage{0x01, 2, 5, 0x0F, 3, 0x00, 2, 0x0E, 0x1A};
    REQUIRE(stage.step());
    CHECK(stage.piece(0).frame == 1 + (2 * 72));
    CHECK(stage.piece(1).frame == 1 + (5 * 72));
    CHECK(stage.piece(0).redraw == 2);
    CHECK(stage.piece(0).erase == 2);
    CHECK(stage.sprite.delay == 3);
    CHECK(stage.sprite.program == 5);
    REQUIRE(stage.step());  // 2 to go
    REQUIRE(stage.step());  // 1
    CHECK(stage.piece(0).frame == 1 + (2 * 72));
    REQUIRE(stage.step());  // on
    CHECK(stage.piece(0).frame == 1 + (4 * 72));
    CHECK(stage.piece(1).frame == 1 + (7 * 72));
    CHECK(stage.sprite.delay == 0);
    CHECK(stage.sprite.animating());
    REQUIRE(stage.step());  // a step with no wait runs at once
    CHECK_FALSE(stage.sprite.animating());
    REQUIRE(stage.step());  // nothing more
    CHECK(stage.pieces.at(29) == Piece{});
    CHECK(stage.pieces.at(32) == Piece{});
}

TEST_CASE("animation: jumps, a call and its return, sounds, the instructions that do nothing") {
    //  0: call 14;  3: sound 7;  6, 8, 10: nothing (04 x, 16 x, 0B);  11: go to 18
    // 14: sound 3;  17: return
    // 18: wait 1
    Stage stage{0x05, 14, 0, 0x02, 7, 1, 0x04, 0xAA, 0x16, 0xBB, 0x0B, 0x03, 18, 0, 0x02, 3, 0, 0x06, 0x0F, 1};
    REQUIRE(stage.step());
    CHECK(stage.sounds == std::vector<std::uint8_t>{3, 7});
    CHECK(stage.sprite.return_to == 3);
    CHECK(stage.sprite.delay == 1);
    CHECK(stage.sprite.program == 20);
}

TEST_CASE("animation: pieces placed around the entity, mirrored when it faces left") {
    // x: -8 and +8; y: -4 and 0; end of step.
    Stage right{0x08, 0xF8, 0xFF, 8, 0, 0x0A, 0xFC, 0xFF, 0, 0, 0x0E};
    REQUIRE(right.step());
    CHECK(right.piece(0).x == 92);
    CHECK(right.piece(1).x == 108);
    CHECK(right.piece(0).y == 46);
    CHECK(right.piece(1).y == 50);
    CHECK(right.piece(0).x_placed);
    CHECK(right.piece(0).y_placed);
    CHECK_FALSE(right.piece(0).mirrored);

    Stage left{0x08, 0xF8, 0xFF, 8, 0, 0x0E};
    left.body.facing_left = true;
    REQUIRE(left.step());
    CHECK(left.piece(0).x == 200 - 92 - 16);  // around x = 100, by its width
    CHECK(left.piece(1).x == 200 - 108 - 16);
    CHECK(left.piece(0).mirrored);
}

TEST_CASE("animation: moves: the entity without a mask, the pieces of a group with one") {
    // Move 3 across, 2 down (the entity); groups 1 and 2; mask 2; move -5 across, 1 down (the second piece).
    Stage stage{0x07, 3, 0x09, 2, 0x13, 1, 2, 0x0D, 2, 0x07, 0xFB, 0x09, 1, 0x0E};
    stage.piece(0).x = 10;
    stage.piece(1).x = 20;
    REQUIRE(stage.step());
    CHECK(stage.body.push_x == 3 * 256);
    CHECK(stage.body.push_y == 2 * 256);
    CHECK(stage.piece(0).group == 1);
    CHECK(stage.piece(1).group == 2);
    CHECK(stage.piece(0).x == 10);
    CHECK(stage.piece(1).x == 15);
    CHECK(stage.piece(1).y == 1);

    Stage left{0x07, 3, 0x09, 2, 0x0E};
    left.body.facing_left = true;
    left.body.flipped = true;
    REQUIRE(left.step());
    CHECK(left.body.push_x == -3 * 256);
    CHECK(left.body.push_y == -2 * 256);
}

TEST_CASE("animation: mirror and flip, hide and show, colours") {
    Stage stage{0x10, 0x11, 0x12, 0x18, 0x0C, 2, 14, 0x0E, 0x19, 0x0E};
    stage.piece(1).blinked_off = true;
    REQUIRE(stage.step());
    CHECK_FALSE(stage.piece(0).mirrored);  // mirrored twice
    CHECK_FALSE(stage.piece(0).flipped);
    CHECK(stage.piece(0).hidden);
    CHECK(stage.piece(0).colour == 1);
    CHECK(stage.piece(1).colour == 7);
    REQUIRE(stage.step());
    CHECK_FALSE(stage.piece(0).hidden);
    CHECK_FALSE(stage.piece(1).blinked_off);
    CHECK(stage.piece(0).erase == 0);

    Stage once{0x10, 0x0E, 0x11, 0x0E};
    REQUIRE(once.step());
    CHECK(once.piece(0).mirrored);
    CHECK_FALSE(once.piece(0).flipped);
    REQUIRE(once.step());
    CHECK(once.piece(1).flipped);

    // Under a mask the colours go by the group of the layer's 13th piece: all or none.
    Stage masked{0x0D, 1, 0x0C, 4, 4, 0x0E};
    REQUIRE(masked.step());
    CHECK(masked.piece(0).colour == 0);
    CHECK(masked.sprite.program == 6);  // the operands weren't read: they are run as instructions (04: nothing)
    Stage through{0x0D, 1, 0x0C, 4, 4, 0x0E};
    through.pieces.at(12).group = 1;
    REQUIRE(through.step());
    CHECK(through.piece(1).colour == 2);
}

TEST_CASE("animation: shapes, packed sets and their frames") {
    Stage stage{0x15, 0, 0x0E, 0x15, 2, 0x0E, 0x15, 1, 0x0E, 0x17, 0x2C, 0x01, 0x14, 9, 0x14, 9, 0x0E, 0x15, 5, 0x0E};
    stage.tables.piece_sizes = {8, 32, 16, 0, 0, 176};
    stage.tables.buffer_offsets.at(6) = 0x1201;
    stage.tables.buffer_offsets.at(7) = 0x1601;
    stage.packed_entries = {290, 300};
    REQUIRE(stage.step());
    CHECK(stage.piece(0).shape == 1);
    CHECK(stage.piece(0).size == 8);
    REQUIRE(stage.step());
    CHECK(stage.piece(0).shape == 4);
    CHECK(stage.piece(0).size == 16);
    REQUIRE(stage.step());
    CHECK(stage.piece(1).shape == 2);
    CHECK(stage.piece(1).size == 32);
    REQUIRE(stage.step());  // set 300 is the level's second; frame 9, asked for twice
    CHECK(stage.piece(0).from == Piece::From::kUnpacked);
    CHECK(stage.piece(0).packed_set == 1);
    CHECK(stage.piece(0).packed);
    CHECK(stage.piece(0).frame == 0x1201);
    CHECK(stage.piece(1).frame == 0x1601);
    CHECK(stage.sprite.packed_frame == 9);
    REQUIRE(stage.step());  // a piece that is the packed set's own bytes
    CHECK(stage.piece(0).from == Piece::From::kPackedSet);
    CHECK(stage.piece(0).size == 176);
    CHECK(stage.piece(0).frame == 1);
    CHECK(stage.sprite.base == 1);

    // A set the level doesn't load: its first. Pieces without a buffer get no frame.
    Stage other{0x17, 0x99, 0x09, 0x0E};
    other.packed_entries = {290, 300};
    other.sprite.first = 100;
    other.sprite.packed_frame = 4;
    REQUIRE(other.step());
    CHECK(other.pieces.at(100).packed_set == 0);
    CHECK(other.pieces.at(100).frame == 0);
    CHECK(other.sprite.packed_frame == kNoPackedFrame);
}

TEST_CASE("animation: a sprite without pieces, and broken programs") {
    Stage none{0x01, 3, 0x0E};
    none.sprite.count = 0;
    REQUIRE(none.step());
    CHECK(none.piece(0).frame == 1 + (3 * 72));  // the piece where its pieces would start, as the original

    Stage unknown{0x1B};
    CHECK_FALSE(unknown.step());
    CHECK_FALSE(unknown.sprite.animating());
    Stage endless{0x03, 0, 0};
    CHECK_FALSE(endless.step());
    Stage past{0x0B};  // runs off the end of the programs: the end
    CHECK(past.step());
    CHECK_FALSE(past.sprite.animating());
    Stage idle{0x0E};
    idle.sprite.program = kNoAnimation;
    CHECK(idle.step());
}

// --- The layer ---

TEST_CASE("pieces: the first free run in the pool; let go, a piece is free once erased") {
    Pieces pieces{};
    Sprite door{.count = 4, .base = 0x241};
    REQUIRE(take_pieces(pieces, door, Pool::kBack, PieceStart{.colour = 3, .mirrored = true, .large = true}));
    CHECK(door.first == 36);
    const Piece& piece = pieces.at(36);
    CHECK(piece.in_use);
    CHECK(piece.mirrored);
    CHECK_FALSE(piece.flipped);
    CHECK(piece.colour == 3);
    CHECK(piece.shape == 2);
    CHECK(piece.size == 32);
    CHECK(piece.frame == 0x241);
    CHECK(piece.from == Piece::From::kSpriteMemory);
    CHECK(piece.redraw == 4);
    CHECK(piece.erase == 2);
    CHECK(pieces.at(39).in_use);
    CHECK_FALSE(pieces.at(40).in_use);

    Sprite item{.count = 1};
    REQUIRE(take_pieces(pieces, item, Pool::kBack, PieceStart{}));
    CHECK(item.first == 40);
    CHECK(pieces.at(40).shape == 1);
    CHECK(pieces.at(40).size == 8);

    // The middle pool shares pieces 36-39 with the back pool; the front pool is 0-23.
    Sprite viking{.count = 1};
    REQUIRE(take_pieces(pieces, viking, Pool::kMiddle, PieceStart{}));
    CHECK(viking.first == 24);
    Sprite text{.count = 2};
    REQUIRE(take_pieces(pieces, text, Pool::kFront, PieceStart{}));
    CHECK(text.first == 0);

    // Let go: nobody's at once, free after four more pictures.
    let_go(pieces, door);
    CHECK_FALSE(pieces.at(36).in_use);
    CHECK_FALSE(is_free(pieces.at(36)));
    Sprite next{.count = 2};
    REQUIRE(take_pieces(pieces, next, Pool::kBack, PieceStart{}));
    CHECK(next.first == 41);
    for (int picture = 0; picture < 3; ++picture) {
        count_picture(pieces);
    }
    CHECK_FALSE(is_free(pieces.at(36)));
    count_picture(pieces);
    CHECK(is_free(pieces.at(36)));
    Sprite last{.count = 4};
    REQUIRE(take_pieces(pieces, last, Pool::kBack, PieceStart{}));
    CHECK(last.first == 36);

    // A sprite without pieces takes none.
    Sprite none{.first = 99, .count = 0};
    CHECK(take_pieces(pieces, none, Pool::kFront, PieceStart{}));
    CHECK(none.first == 99);
}

TEST_CASE("pieces: a pool without room; a run that would pass its end") {
    Pieces pieces{};
    Sprite big{.count = 16};
    REQUIRE(take_pieces(pieces, big, Pool::kMiddle, PieceStart{}));  // 24-39: all of it
    Sprite one{.count = 1};
    CHECK_FALSE(take_pieces(pieces, one, Pool::kMiddle, PieceStart{}));
    Sprite front{.count = 20};
    REQUIRE(take_pieces(pieces, front, Pool::kFront, PieceStart{}));
    Sprite five{.count = 5};
    CHECK_FALSE(take_pieces(pieces, five, Pool::kFront, PieceStart{}));  // 20-23 are free: one short
    Sprite four{.count = 4};
    REQUIRE(take_pieces(pieces, four, Pool::kFront, PieceStart{}));
    CHECK(four.first == 20);
}

TEST_CASE("pieces: the counts of a picture; a tick's move over its three pictures") {
    Pieces pieces{};
    pieces.at(5) = Piece{.in_use = true, .redraw = 2, .erase = 1};
    pieces.at(6) = Piece{.in_use = true, .hidden = true, .redraw = 2, .erase = 2};
    pieces.at(7) = Piece{.in_use = true, .blinked_off = true, .redraw = 1};
    count_picture(pieces);
    CHECK(pieces.at(5).redraw == 1);
    CHECK(pieces.at(5).erase == 0);
    CHECK(pieces.at(6).redraw == 2);  // a hidden piece isn't drawn
    CHECK(pieces.at(6).erase == 1);
    CHECK(pieces.at(7).redraw == 1);
    count_picture(pieces);
    count_picture(pieces);
    CHECK(pieces.at(5).redraw == 0);

    for (const int distance : {0, 1, 2, 3, 7, 16, -1, -7, -16}) {
        CHECK(share_of_move(distance, 0) + share_of_move(distance, 1) + share_of_move(distance, 2) == distance);
    }
    CHECK(share_of_move(7, 0) == 3);
    CHECK(share_of_move(7, 1) == 2);
    CHECK(share_of_move(7, 2) == 2);
    CHECK(share_of_move(-1, 0) == -1);
    CHECK(share_of_move(-1, 1) == 0);

    Pieces moved{};
    const Sprite sprite{.first = 10, .count = 2};
    shift_pieces(moved, sprite, 3, -2);
    CHECK(moved.at(10).x == 3);
    CHECK(moved.at(11).y == -2);
    CHECK(moved.at(11).redraw == 2);
    CHECK(moved.at(12) == Piece{});
    shift_pieces(moved, Sprite{.first = 50, .count = 0}, 1, 1);  // the piece its pieces would start at
    CHECK(moved.at(50).x == 1);
}

// --- Pictures ---

TEST_CASE("pictures: a piece's frame from the sprite memory, by its shape; the last piece first") {
    SpriteMemory memory;
    // Two 8 x 8 frames of 72 bytes: all of colour 5, all of colour 6; 4 planes of 2 groups: a mask byte and 8 pixels.
    for (const int colour : {5, 6}) {
        for (int group = 0; group < 8; ++group) {
            memory.sets.push_back(std::byte{0xff});
            memory.sets.insert(memory.sets.end(), 8, static_cast<std::byte>(colour));
        }
    }
    Pieces pieces{};
    pieces.at(3) = Piece{.in_use = true, .mirrored = true, .shape = 1, .x = 10, .y = 20, .size = 8, .frame = 1};
    pieces.at(9) = Piece{.in_use = true, .flipped = true, .shape = 1, .x = -4, .y = 7, .size = 8, .frame = 73};
    pieces.at(10) = Piece{.in_use = true, .hidden = true, .shape = 1, .frame = 1};
    pieces.at(11) = Piece{.in_use = true, .blinked_off = true, .shape = 1, .frame = 1};
    pieces.at(12) = Piece{.shape = 1, .frame = 1};                  // nobody's
    pieces.at(13) = Piece{.in_use = true, .shape = 1, .frame = 0};  // no picture
    pieces.at(14) = Piece{.in_use = true, .shape = 3, .frame = 1};  // no such shape
    pieces.at(15) = Piece{.in_use = true, .shape = 4, .frame = 1};  // 16 x 16: past the memory's end
    pieces.at(16) = Piece{.in_use = true, .shape = 2, .size = 0, .frame = 1};
    pieces.at(17) = Piece{.in_use = true, .shape = 2, .size = 200, .frame = 1};
    pieces.at(18) = Piece{.in_use = true, .shape = 1, .from = Piece::From::kPackedSet, .frame = 1, .packed_set = 4};
    const PiecePainter painter;
    const std::vector<PiecePicture> pictures = painter.pictures(pieces, memory);
    REQUIRE(pictures.size() == 2);
    CHECK(pictures[0].x == -4);  // piece 9 first, piece 3 over it
    CHECK(pictures[0].y == 7);
    CHECK(pictures[0].flipped);
    CHECK(pictures[0].image.width == 8);
    CHECK(pictures[0].image.at(0, 0) == 6);
    CHECK(pictures[1].x == 10);
    CHECK(pictures[1].mirrored);
    CHECK(pictures[1].image.at(7, 7) == 5);
}

TEST_CASE("pictures: a packed frame unpacked into its piece's buffer, in the piece's colours") {
    // A packed set with one frame: its table (frame 0 at offset 2), then 128 groups: the first has pixels 0 and 7
    // (nibbles 1 and 2), the others none.
    SpriteMemory memory;
    std::vector<std::byte> set{std::byte{2}, std::byte{0}, std::byte{0x81}, std::byte{0x12}};
    set.insert(set.end(), 127, std::byte{0});
    memory.packed_sets = {std::vector<std::byte>{}, set};
    Pieces pieces{};
    pieces.at(24) = Piece{.in_use = true,
                          .shape = 2,
                          .packed = true,
                          .colour = 3,
                          .x = 50,
                          .size = 32,
                          .from = Piece::From::kUnpacked,
                          .frame = 0x401,
                          .packed_set = 1};
    Sprite sprite{.first = 24, .count = 1, .packed_frame = 0};
    PiecePainter painter;
    CHECK(painter.pictures(pieces, memory).at(0).image.at(0, 0) == 0);  // nothing unpacked yet
    painter.unpack(pieces, sprite, memory);
    const std::vector<PiecePicture> pictures = painter.pictures(pieces, memory);
    REQUIRE(pictures.size() == 1);
    CHECK(pictures[0].image.width == 32);
    CHECK(pictures[0].image.height == 32);
    CHECK(pictures[0].image.at(0, 0) == (0x80 | 0x30 | 1));  // plane 0's first group: x = 0, 4, ... 28
    CHECK(pictures[0].image.at(28, 0) == (0x80 | 0x30 | 2));
    CHECK(pictures[0].image.at(4, 0) == 0);
    CHECK(pictures[0].image.at(1, 0) == 0);

    // Nothing is unpacked for a sprite without a packed frame, without pieces, from a set the level doesn't have,
    // or for a frame past the set's table.
    PiecePainter untouched;
    for (Sprite other :
         {Sprite{.first = 24, .count = 1}, Sprite{.first = 24, .count = 0, .packed_frame = 0},
          Sprite{.first = 24, .count = 1, .packed_frame = 900}, Sprite{.first = 500, .count = 1, .packed_frame = 0}}) {
        untouched.unpack(pieces, other, memory);
    }
    pieces.at(24).packed_set = 7;
    untouched.unpack(pieces, sprite, memory);
    pieces.at(24).packed_set = 1;
    pieces.at(24).from = Piece::From::kSpriteMemory;
    untouched.unpack(pieces, sprite, memory);
    pieces.at(24).from = Piece::From::kUnpacked;
    CHECK(untouched.pictures(pieces, memory).at(0).image.at(0, 0) == 0);
    // The set's own bytes as a picture (too short here: no picture).
    pieces.at(24).from = Piece::From::kPackedSet;
    CHECK(untouched.pictures(pieces, memory).empty());
}

TEST_CASE("real data: the animation tables", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    const auto files = assets::GameFiles::open(LVR_ORIGINAL_DIR);
    REQUIRE(files);
    const auto tables = load_animation_tables(files->exe(), files->release());
    REQUIRE(tables);
    CHECK(tables->piece_sizes[0] == 8);
    CHECK(tables->piece_sizes[1] == 32);
    CHECK(tables->piece_sizes[2] == 16);
    CHECK(tables->buffer_offsets[1] > tables->buffer_offsets[0]);
    CHECK_FALSE(load_animation_tables(files->exe().first(100), files->release()));
}

TEST_CASE("pieces: Remaster rules' layer has the same three pools, larger, and a frame buffer to each middle piece") {
    Pieces pieces{kRemasterPieces};
    CHECK(pieces.size() == 2048);
    CHECK(pieces.layout() == kRemasterPieces);
    CHECK(Pieces{}.size() == kPieces);
    // The pools in the original's order (the front one has the lowest pieces: drawn over the others), none shared.
    Sprite front{.count = 2};
    Sprite middle{.count = 1};
    Sprite back{.count = 3};
    REQUIRE(take_pieces(pieces, front, Pool::kFront, PieceStart{}));
    REQUIRE(take_pieces(pieces, middle, Pool::kMiddle, PieceStart{}));
    REQUIRE(take_pieces(pieces, back, Pool::kBack, PieceStart{}));
    CHECK(front.first == 0);
    CHECK(middle.first == 256);
    CHECK(back.first == 512);
    // Far more of the Vikings' and enemies' pool than the original's 16.
    for (int taken = 1; taken < 256; ++taken) {
        Sprite more{.count = 1};
        REQUIRE(take_pieces(pieces, more, Pool::kMiddle, PieceStart{}));
        CHECK(more.first == 256 + taken);
    }
    Sprite one_too_many{.count = 1};
    CHECK_FALSE(take_pieces(pieces, one_too_many, Pool::kMiddle, PieceStart{}));

    // Two pieces of the middle pool, each showing what was unpacked for it: a frame with one pixel, in its colours.
    SpriteMemory memory;
    std::vector<std::byte> set{std::byte{2}, std::byte{0}, std::byte{0x80}, std::byte{0x10}};
    set.insert(set.end(), 127, std::byte{0});
    memory.packed_sets = {set};
    for (const int index : {300, 511}) {
        pieces.at(static_cast<std::size_t>(index)) = Piece{.in_use = true,
                                                           .shape = 2,
                                                           .packed = true,
                                                           .colour = static_cast<std::uint8_t>(index == 300 ? 3 : 5),
                                                           .size = 32,
                                                           .from = Piece::From::kUnpacked,
                                                           .frame = 1,
                                                           .packed_set = 0};
    }
    PiecePainter painter;
    painter.unpack(pieces, Sprite{.first = 300, .count = 1, .packed_frame = 0}, memory);
    painter.unpack(pieces, Sprite{.first = 511, .count = 1, .packed_frame = 0}, memory);
    // A piece outside the middle pool has no buffer: nothing is unpacked for it, and it shows nothing.
    pieces.at(600) = pieces.at(300);
    painter.unpack(pieces, Sprite{.first = 600, .count = 1, .packed_frame = 0}, memory);
    std::vector<int> seen;
    for (const PiecePicture& picture : painter.pictures(pieces, memory)) {
        if (picture.image.width == 32) {
            seen.push_back(picture.image.at(0, 0));
        }
    }
    CHECK(seen == std::vector<int>{0x80 | 0x50 | 1, 0x80 | 0x30 | 1});  // the last piece first
}
