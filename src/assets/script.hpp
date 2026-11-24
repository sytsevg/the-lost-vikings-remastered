#pragma once

// Object scripts (docs/original/script-vm.md): the world's object database (DATA.DAT entries 449-454) and its
// bytecode. This file decodes; nothing runs the scripts any more; the engine's kinds are
// written by hand from these listings. The operand formats come from the opcode handlers in VIKINGS.EXE (table
// ds:87CC); the opcode names are ours, and many are still tentative.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "assets/data_dat.hpp"
#include "assets/objects.hpp"
#include "assets/reader.hpp"

namespace lvr::assets {

using assets::Error;
using assets::ObjectDatabase;

// --- Instructions ---

inline constexpr std::size_t kOpcodeCount = 0xD8;  // 00-D7; the dispatch table has no more entries
inline constexpr std::size_t kMaxOperands = 5;     // `14 G2 G2 b`

// One opcode's name and operand format, as tokens separated by spaces:
//   b u8, s s8, w u16, j jump target, c call target (both u16 offsets in the database),
//   G2 a mode byte and two "get" operands (kinds in bits 0-2, then 3-5), G1 a mode byte and one get operand,
//   S2 / S1 the same for "set" operands.
struct OpcodeInfo {
    std::uint8_t code = 0;
    std::string_view name;
    std::string_view format;
};

const OpcodeInfo& opcode_info(std::uint8_t code) noexcept;  // D8-FF: a "(not an opcode)" row

// The kinds of operand. The first five are plain immediates; the rest come from a mode byte.
enum class OperandKind : std::uint8_t {
    kByte,         // b
    kSignedByte,   // s
    kWord,         // w
    kJump,         // j
    kCall,         // c
    kLiteral,      // mode kind 0: a u16 (get only)
    kField,        // mode kind 1: this object's field (an even byte)
    kGlobal,       // mode kind 2: a ds: word, by address
    kTargetField,  // mode kind 3: a field of the target object (field 3C)
    kRandom,       // mode kind 4: a random number, no operand bytes (get only)
};

struct Operand {
    OperandKind kind = OperandKind::kByte;
    std::int32_t value = 0;  // kSignedByte: -128..127; kRandom: 0; otherwise the unsigned operand
};

struct Instruction {
    std::uint16_t pc = 0;
    std::uint8_t opcode = 0;
    std::uint8_t length = 0;  // bytes, the opcode included
    std::uint8_t operand_count = 0;
    std::array<Operand, kMaxOperands> operand_slots{};

    std::span<const Operand> operands() const noexcept { return std::span{operand_slots}.first(operand_count); }
    const OpcodeInfo& info() const noexcept { return opcode_info(opcode); }
};

// Decodes the instruction at `pc`. Errors: kOutOfBounds (pc or an operand past the end), kBadOpcode (D8-FF),
// kBadOperandKind (a mode-byte kind the original's operand tables have no entry for: get 5-7, set 0 and 4-7).
std::expected<Instruction, Error> decode_instruction(std::span<const std::byte> code, std::uint16_t pc);

// The opcodes after which execution never falls through to the next instruction: jmp, ret, next_slot, remove.
bool ends_block(std::uint8_t opcode) noexcept;

// "jae A,f1C, @1A2B": the name, then the operands (b and w in hex, s in decimal, targets as @offset, mode-byte
// operands as #literal, f field, g global, t target field, rnd).
std::string format_instruction(const Instruction& instruction);

// --- Animation programs (docs/original/animation.md) ---

inline constexpr std::size_t kAnimationOpcodeCount = 0x1B;  // 00-1A (table ds:86E6)

// Tables of VIKINGS.EXE the animation opcodes and the tile tests read.
struct EngineTables {
    std::array<std::uint16_t, 16> frame_offsets{};   // ds:871C, by sprite piece (pool 1): animation opcode 17
    std::array<std::uint16_t, 256> piece_sizes{};    // cs:32D7, by operand: animation opcode 15 (4-255: code bytes)
    std::array<std::uint8_t, 256> slope_heights{};   // ds:897C: (slope type & 15) x 16 + (x & 15) -> height
    std::vector<std::uint8_t> type_lists;            // ds:94CC: sorted tile-type lists, each ended by 0xff
    std::vector<std::uint8_t> dialogue;              // segment 07A6: the texts' offset table and texts (41-45)
    std::vector<std::uint8_t> passwords;             // ds:85A5: 4 bytes per level, bit 7 set (D2/D3)
    std::vector<std::uint8_t> trash;                 // ds:8592, by item: non-zero if the trash can takes it
    std::vector<std::uint8_t> header18;              // ds:853E then 8544, 6 bytes each, by header word 0x18 (1397)
    std::vector<std::uint8_t> initial_data;          // ds:0000-2E7F as the game starts (docs/original/game-flow.md)
    std::array<std::uint16_t, 48> slot_headers{};    // ds:940C: each slot's header entry ...
    std::array<std::uint16_t, 48> slot_databases{};  // ... ds:946C: and object database (0xffff: keep the loaded one)
};

// From the bytes of ds:871C (32), cs:32D7 (512), ds:897C (256) and ds:94CC (any size); the dialogue segment and the
// passwords are kept as bytes; load_engine_tables fills them, the initial data and the slot tables.
std::expected<EngineTables, Error> parse_engine_tables(std::span<const std::byte> frame_offsets,
                                                       std::span<const std::byte> piece_sizes,
                                                       std::span<const std::byte> slope_heights,
                                                       std::span<const std::byte> type_lists);

// --- Following the code ---

// Where execution can start: every record's script offset and that offset + 3. Sorted, no duplicates.
std::vector<std::uint16_t> script_entry_points(const ObjectDatabase& database);

struct DecodeFailure {
    std::uint16_t pc = 0;
    Error error = Error::kOutOfBounds;
};

struct CodeMap {
    std::vector<Instruction> instructions;  // every instruction reached, sorted by pc
    std::vector<std::uint16_t> labels;      // the entry points and every jump and call target, sorted
    std::vector<DecodeFailure> failures;    // sorted by pc
};

// Decodes everything reachable from `entry_points`, following jumps and calls (and falling through conditional
// ones). Doesn't reach the animation programs (opcode 19), which are a different bytecode.
CodeMap trace_code(std::span<const std::byte> code, std::span<const std::uint16_t> entry_points);

// A listing of the whole database: each type's script under a "; ---- object type(s)" line, labels, the
// instruction bytes and format_instruction(). lv-export writes it to scripts/NNN.txt.
std::string disassemble(const ObjectDatabase& database);

// The tables the animation programs and tile tests read, from VIKINGS.EXE.
std::expected<EngineTables, std::string> load_engine_tables(std::span<const std::byte> exe,
                                                            const assets::Release& release);

}  // namespace lvr::assets
