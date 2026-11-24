// Tests for src/assets/script.*: the instruction decoder on hand-made bytes, the listing of a hand-made object
// database, and [real-data] checks of all six databases (449-454).
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

#include "assets/assets.hpp"
#include "assets/catalog.hpp"
#include "assets/script.hpp"

using namespace lvr::assets;
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

Instruction decode_ok(const std::vector<std::byte>& code, std::uint16_t pc = 0) {
    const auto instruction = decode_instruction(code, pc);
    REQUIRE(instruction.has_value());
    return *instruction;
}

constexpr std::size_t kRecordsEnd = kObjectTypeCount * kObjectRecordSize;  // 0x1347

// A database whose records all point at `script_offset`, with `between` after the records (the byte lists) and
// `code` at script_offset.
std::vector<std::byte> make_database(std::uint16_t script_offset, const std::vector<std::byte>& between,
                                     const std::vector<std::byte>& code) {
    std::vector<std::byte> out(kRecordsEnd);
    for (std::size_t i = 0; i < kObjectTypeCount; ++i) {
        out[(i * kObjectRecordSize) + 0] = static_cast<std::byte>(i);  // something to tell the records apart
        out[(i * kObjectRecordSize) + 3] = static_cast<std::byte>(script_offset & 0xffU);
        out[(i * kObjectRecordSize) + 4] = static_cast<std::byte>(script_offset >> 8U);
    }
    out.insert(out.end(), between.begin(), between.end());
    out.resize(script_offset);
    out.insert(out.end(), code.begin(), code.end());
    return out;
}

}  // namespace

// --- Opcode table ---

TEST_CASE("script: the opcode table has a row for every opcode") {
    for (std::size_t code = 0; code < kOpcodeCount; ++code) {
        CHECK(opcode_info(static_cast<std::uint8_t>(code)).code == code);
    }
    CHECK(opcode_info(0x00).name == "yield");
    CHECK(opcode_info(0x14).name == "spawn");
    CHECK(opcode_info(0x14).format == "G2 G2 b");
    CHECK(opcode_info(0x45).format == "G1 G2");
    CHECK(opcode_info(0xD7).format == "b w");
    CHECK(opcode_info(0xD8).name == "(not an opcode)");
}

TEST_CASE("script: jmp, ret, next_slot and remove end a block") {
    for (std::size_t code = 0; code < kOpcodeCount; ++code) {
        const bool expected = code == 0x03 || code == 0x06 || code == 0x0F || code == 0x10;
        CHECK(ends_block(static_cast<std::uint8_t>(code)) == expected);
    }
}

// --- Decoding ---

TEST_CASE("script: immediate operands") {
    SECTION("no operands") {
        const Instruction yield = decode_ok(bytes({0x00}));
        CHECK(yield.length == 1);
        CHECK(yield.operands().empty());
        CHECK(format_instruction(yield) == "yield");
    }
    SECTION("u16") {
        const Instruction sfx = decode_ok(bytes({0x02, 0x34, 0x12}));
        CHECK(sfx.length == 3);
        REQUIRE(sfx.operands().size() == 1);
        CHECK(sfx.operands()[0].kind == OperandKind::kWord);
        CHECK(sfx.operands()[0].value == 0x1234);
        CHECK(format_instruction(sfx) == "sfx 1234");
    }
    SECTION("signed bytes") {
        const Instruction op = decode_ok(bytes({0x17, 0xfe, 0x05}));
        REQUIRE(op.operands().size() == 2);
        CHECK(op.operands()[0].kind == OperandKind::kSignedByte);
        CHECK(op.operands()[0].value == -2);
        CHECK(op.operands()[1].value == 5);
        CHECK(format_instruction(op) == "op17 -2, 5");
    }
    SECTION("byte and jump") {
        const Instruction op = decode_ok(bytes({0x1E, 0x07, 0xcd, 0xab}));
        CHECK(op.length == 4);
        CHECK(op.operands()[0].kind == OperandKind::kByte);
        CHECK(op.operands()[1].kind == OperandKind::kJump);
        CHECK(op.operands()[1].value == 0xabcd);
        CHECK(format_instruction(op) == "tile1E 07, @ABCD");
    }
    SECTION("call") {
        const Instruction call = decode_ok(bytes({0x05, 0x00, 0x20}));
        CHECK(call.operands()[0].kind == OperandKind::kCall);
        CHECK(format_instruction(call) == "call @2000");
    }
    SECTION("at a pc other than 0") {
        const Instruction op = decode_ok(bytes({0x00, 0x00, 0x51, 0x29, 0x00}), 2);
        CHECK(op.pc == 2);
        CHECK(op.opcode == 0x51);
        CHECK(format_instruction(op) == "A= 0029");
    }
}

TEST_CASE("script: mode-byte operands") {
    SECTION("all five get kinds, through two G2") {
        // spawn: mode 0x08 = literal, field; mode 0x1a = global, target field... then the type byte
        const Instruction spawn = decode_ok(bytes({0x14, 0x08, 0x10, 0x00, 0x1e, 0x1a, 0x06, 0x02, 0x20, 0x05}));
        CHECK(spawn.length == 10);
        REQUIRE(spawn.operands().size() == 5);
        CHECK(spawn.operands()[0].kind == OperandKind::kLiteral);
        CHECK(spawn.operands()[0].value == 0x10);
        CHECK(spawn.operands()[1].kind == OperandKind::kField);
        CHECK(spawn.operands()[2].kind == OperandKind::kGlobal);
        CHECK(spawn.operands()[2].value == 0x0206);
        CHECK(spawn.operands()[3].kind == OperandKind::kTargetField);
        CHECK(spawn.operands()[4].kind == OperandKind::kByte);
        CHECK(format_instruction(spawn) == "spawn #10, f1E, g206, t20, 05");

        const Instruction random = decode_ok(bytes({0x48, 0x24}));  // kinds 4 and 4: no operand bytes
        CHECK(random.length == 2);
        CHECK(random.operands()[0].kind == OperandKind::kRandom);
        CHECK(format_instruction(random) == "op48 rnd, rnd");
    }
    SECTION("G1 reads one operand, bits 3-5 ignored") {
        const Instruction text = decode_ok(bytes({0x45, 0x39, 0x1e, 0x00, 0x26, 0x00, 0x01, 0x00}));
        CHECK(text.length == 8);
        REQUIRE(text.operands().size() == 3);
        CHECK(text.operands()[0].kind == OperandKind::kField);
        CHECK(text.operands()[1].kind == OperandKind::kLiteral);
        CHECK(text.operands()[2].value == 1);
    }
    SECTION("set operands") {
        const Instruction rel = decode_ok(bytes({0x15, 0x19, 0x12, 0x14}));  // field 12, target field 14
        CHECK(rel.operands()[0].kind == OperandKind::kField);
        CHECK(rel.operands()[1].kind == OperandKind::kTargetField);
        CHECK(format_instruction(rel) == "rel_viking f12, t14");
    }
    SECTION("kinds the original has no handler for") {
        CHECK(decode_instruction(bytes({0x48, 0x05}), 0).error() == Error::kBadOperandKind);  // get kind 5
        CHECK(decode_instruction(bytes({0x48, 0x38, 0x00, 0x00}), 0).error() == Error::kBadOperandKind);
        CHECK(decode_instruction(bytes({0x15, 0x08, 0x00, 0x00}), 0).error() == Error::kBadOperandKind);  // set 0
        CHECK(decode_instruction(bytes({0x15, 0x0c, 0x00}), 0).error() == Error::kBadOperandKind);        // set 4
    }
}

TEST_CASE("script: decode errors") {
    CHECK(decode_instruction(bytes({}), 0).error() == Error::kOutOfBounds);
    CHECK(decode_instruction(bytes({0x00}), 1).error() == Error::kOutOfBounds);
    CHECK(decode_instruction(bytes({0xD8}), 0).error() == Error::kBadOpcode);
    CHECK(decode_instruction(bytes({0xff}), 0).error() == Error::kBadOpcode);
    CHECK(decode_instruction(bytes({0x02, 0x34}), 0).error() == Error::kOutOfBounds);
    CHECK(decode_instruction(bytes({0x14}), 0).error() == Error::kOutOfBounds);              // no mode byte
    CHECK(decode_instruction(bytes({0x14, 0x00, 0x01}), 0).error() == Error::kOutOfBounds);  // half a literal
}

// --- Following the code ---

TEST_CASE("script: trace_code follows jumps and calls and stops at the end of a block") {
    // 0: call @8 / 3: je A,# 1, @C / 8: ret (ends the block) / 9: unreached / C: jmp @0
    const auto code = bytes({0x05, 0x08, 0x00,              // 0
                             0x72, 0x01, 0x00, 0x0c, 0x00,  // 3
                             0x06,                          // 8
                             0x00, 0x00, 0x00,              // 9, unreached
                             0x03, 0x00, 0x00});            // C
    const std::array<std::uint16_t, 1> entries{0};
    const CodeMap map = trace_code(code, entries);
    std::vector<std::uint16_t> pcs;
    for (const Instruction& instruction : map.instructions) {
        pcs.push_back(instruction.pc);
    }
    CHECK(pcs == std::vector<std::uint16_t>{0, 3, 8, 0x0c});
    CHECK(map.labels == std::vector<std::uint16_t>{0, 8, 0x0c});
    CHECK(map.failures.empty());
}

TEST_CASE("script: trace_code records where decoding fails") {
    const auto code = bytes({0x03, 0x05, 0x00, 0x00, 0x00, 0xee, 0x02});  // jmp @5; at 5 a bad opcode
    const std::array<std::uint16_t, 2> entries{0, 6};
    const CodeMap map = trace_code(code, entries);
    REQUIRE(map.failures.size() == 2);
    CHECK(map.failures[0].pc == 5);
    CHECK(map.failures[0].error == Error::kBadOpcode);
    CHECK(map.failures[1].pc == 6);
    CHECK(map.failures[1].error == Error::kOutOfBounds);  // sfx without its operand
}

// --- Object database ---

TEST_CASE("script: a hand-made object database") {
    constexpr std::uint16_t kScript = 0x134d;
    // byte lists {00}, {01 02}, {}; then a script: jmp @1350 / 1350: yield / jmp @1350
    const auto data = make_database(kScript, bytes({0x00, 0xff, 0x01, 0x02, 0xff, 0xff}),
                                    bytes({0x03, 0x50, 0x13, 0x00, 0x03, 0x50, 0x13}));
    const auto database = parse_object_database(data);
    REQUIRE(database.has_value());
    REQUIRE(database->records.size() == kObjectTypeCount);
    CHECK(database->records[7].bytes[0] == 7);
    CHECK(database->records[7].script() == kScript);
    CHECK(database->byte_lists == std::vector<std::vector<std::uint8_t>>{{0x00}, {0x01, 0x02}, {}});
    CHECK(database->bytes.size() == data.size());
    CHECK(script_entry_points(*database) == std::vector<std::uint16_t>{kScript, kScript + 3});

    const std::string listing = disassemble(*database);
    CHECK(listing.starts_with("\n; ---- object type(s) 00, 01, 02,"));
    CHECK(listing.contains(", EA: header jump at 134D, start at 1350\n"));
    CHECK(listing.contains("134D:\n    134D  03 50 13                 jmp @1350\n"));
    CHECK(listing.contains("1350:\n    1350  00                       yield\n"));
    CHECK(listing.contains("    1351  03 50 13                 jmp @1350\n"));
}

TEST_CASE("script: disassemble lists decode failures at the end") {
    const auto data = make_database(0x1347, {}, bytes({0x03, 0x4a, 0x13, 0xee}));
    const auto database = parse_object_database(data);
    REQUIRE(database.has_value());
    CHECK(disassemble(*database).ends_with("; error at 134A: not an opcode\n"));
}

// --- The real databases ---

TEST_CASE("script: every script in the six databases decodes", "[real-data]") {
    const GameFiles& files = game_or_skip();
    // Instruction counts from the first full decode (scripts/lvscript.py, 2026-11-17).
    const std::map<std::uint16_t, std::size_t> expected_instructions{{449, 12038}, {450, 8935},  {451, 10077},
                                                                     {452, 10955}, {453, 10440}, {454, 9630}};
    std::vector<std::vector<std::uint8_t>> first_lists;
    for (const auto& [entry, instruction_count] : expected_instructions) {
        INFO("object database " << entry);
        const auto database = load_object_database(files.archive(), entry);
        REQUIRE(database.has_value());
        CHECK(database->records.size() == kObjectTypeCount);
        CHECK(database->byte_lists.size() == 35);
        if (first_lists.empty()) {
            first_lists = database->byte_lists;
        }
        CHECK(database->byte_lists == first_lists);  // the same in every database

        const CodeMap map = trace_code(database->bytes, script_entry_points(*database));
        CHECK(map.failures.empty());
        CHECK(map.instructions.size() == instruction_count);
        // No byte belongs to two instructions.
        std::vector<int> owners(database->bytes.size());
        for (const Instruction& instruction : map.instructions) {
            for (std::size_t i = 0; i < instruction.length; ++i) {
                ++owners.at(instruction.pc + i);
            }
        }
        CHECK(std::ranges::count_if(owners, [](int n) { return n > 1; }) == 0);
    }
}

TEST_CASE("script: known instructions in the spaceship database", "[real-data]") {
    const GameFiles& files = game_or_skip();
    const auto database = load_object_database(files.archive(), kLevels[0].object_db_entry);
    REQUIRE(database.has_value());
    // Type 3C (the janitor alien's conversation partner): header jump, then A = 0x29.
    const ObjectRecord& record = database->records.at(0x3C);
    CHECK(record.script() == 0x8365);
    CHECK(format_instruction(decode_ok(database->bytes, record.script())) == "jmp @5C87");
    CHECK(format_instruction(decode_ok(database->bytes, record.script() + 3)) == "A= 0029");
    // The script most types share starts with a text box at an absolute position.
    CHECK(format_instruction(decode_ok(database->bytes, 0x13DE)) == "text_at #10D, #6, #A, #C");
}
