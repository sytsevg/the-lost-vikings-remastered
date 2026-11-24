#include "assets/script.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <set>
#include <utility>

#include "assets/assets.hpp"

namespace lvr::assets {

using assets::ObjectRecord;

namespace {

// --- Opcode table ---

// Built from the opcode handlers (docs/original/script-vm.md#instruction-set); one row per opcode, in order.
constexpr std::array<OpcodeInfo, kOpcodeCount> kOpcodes{{
    {0x00, "yield", ""},
    {0x01, "end", ""},
    {0x02, "sfx", "w"},
    {0x03, "jmp", "j"},
    {0x04, "sfx_stop", "b"},
    {0x05, "call", "c"},
    {0x06, "ret", ""},
    {0x07, "face_left", ""},
    {0x08, "face_right", ""},
    {0x09, "face_flipv_set", ""},
    {0x0A, "face_flipv_clear", ""},
    {0x0B, "flip_h", ""},
    {0x0C, "flip_v", ""},
    {0x0D, "gflag_clear", ""},
    {0x0E, "gflag_set", ""},
    {0x0F, "next_slot", ""},
    {0x10, "remove", ""},
    {0x11, "op11", ""},
    {0x12, "op12", ""},
    {0x13, "system", "b w"},
    {0x14, "spawn", "G2 G2 b"},
    {0x15, "rel_viking", "S2"},
    {0x16, "rel_target", "S2"},
    {0x17, "op17", "s s"},
    {0x18, "op18", "s s"},
    {0x19, "anim", "w"},
    {0x1A, "hit_class", "b c"},
    {0x1B, "op1B", "s s"},
    {0x1C, "if_anim_running", "j"},
    {0x1D, "hit_mask", "w c"},
    {0x1E, "tile1E", "b j"},
    {0x1F, "tile1F", "b j"},
    {0x20, "tile20", "b j"},
    {0x21, "tile21", "b j"},
    {0x22, "tile22", "b j"},
    {0x23, "tile23", "b j"},
    {0x24, "tile24", "b j"},
    {0x25, "tile25", "b j"},
    {0x26, "op26", "G2 S2"},
    {0x27, "op27", "G2 S1"},
    {0x28, "op28", "G2 S2"},
    {0x29, "map29", "G2 G1"},
    {0x2A, "map2A", "G2 G1"},
    {0x2B, "map2B", "G2 G1"},
    {0x2C, "for_kind_viking", "b c"},
    {0x2D, "next_match", ""},
    {0x2E, "shake_x", "w"},
    {0x2F, "anim_step", ""},
    {0x30, "op30", "b j"},
    {0x31, "op31", "b j"},
    {0x32, "hit32", "b c"},
    {0x33, "hit33", "b c"},
    {0x34, "rel_nearest_viking", "S2"},
    {0x35, "for_kind", "b c"},
    {0x36, "next_match_n", ""},
    {0x37, "hit_class_all", "b c"},
    {0x38, "hit_mask_all", "w c"},
    {0x39, "nop3", "b w"},
    {0x3A, "op12_alias", ""},
    {0x3B, "shake_y", "w"},
    {0x3C, "hit3C", "b c"},
    {0x3D, "tint", "b b b"},
    {0x3E, "tint_clear", ""},
    {0x3F, "op3F", ""},
    {0x40, "op40", ""},
    {0x41, "text_at_object", "G2 G2"},
    {0x42, "text_cmd2", ""},
    {0x43, "text_cmd4", ""},
    {0x44, "text_at", "G2 G2"},
    {0x45, "text45", "G1 G2"},
    {0x46, "text_cmd6", "w"},
    {0x47, "nop", ""},
    {0x48, "op48", "G2"},
    {0x49, "op49", "G2 b j"},
    {0x4A, "op4A", "G2 b j"},
    {0x4B, "set_flag_2000", ""},
    {0x4C, "tint2", "b b b"},
    {0x4D, "tint2_clear", ""},
    {0x4E, "op4E", "j"},
    {0x4F, "op4F", "j"},
    {0x50, "text_cmd8", "G2 G1"},
    {0x51, "A=", "w"},
    {0x52, "A=f", "b"},
    {0x53, "A=g", "w"},
    {0x54, "A=t", "b"},
    {0x55, "A=rnd", ""},
    {0x56, "mov f,A", "b"},
    {0x57, "mov g,A", "w"},
    {0x58, "mov t,A", "b"},
    {0x59, "add f,A", "b"},
    {0x5A, "add g,A", "w"},
    {0x5B, "add t,A", "b"},
    {0x5C, "sub f,A", "b"},
    {0x5D, "sub g,A", "w"},
    {0x5E, "sub t,A", "b"},
    {0x5F, "and f,A", "b"},
    {0x60, "and g,A", "w"},
    {0x61, "and t,A", "b"},
    {0x62, "or f,A", "b"},
    {0x63, "or g,A", "w"},
    {0x64, "or t,A", "b"},
    {0x65, "xor f,A", "b"},
    {0x66, "xor g,A", "w"},
    {0x67, "xor t,A", "b"},
    {0x68, "jae A,#", "w j"},
    {0x69, "jae A,f", "b j"},
    {0x6A, "jae A,g", "w j"},
    {0x6B, "jae A,t", "b j"},
    {0x6C, "jae A,rnd", "j"},
    {0x6D, "jb A,#", "w j"},
    {0x6E, "jb A,f", "b j"},
    {0x6F, "jb A,g", "w j"},
    {0x70, "jb A,t", "b j"},
    {0x71, "jb A,rnd", "j"},
    {0x72, "je A,#", "w j"},
    {0x73, "je A,f", "b j"},
    {0x74, "je A,g", "w j"},
    {0x75, "je A,t", "b j"},
    {0x76, "je A,rnd", "j"},
    {0x77, "jne A,#", "w j"},
    {0x78, "jne A,f", "b j"},
    {0x79, "jne A,g", "w j"},
    {0x7A, "jne A,t", "b j"},
    {0x7B, "jne A,rnd", "j"},
    {0x7C, "jge A,#", "w j"},
    {0x7D, "jge A,f", "b j"},
    {0x7E, "jge A,g", "w j"},
    {0x7F, "jge A,t", "b j"},
    {0x80, "jge A,rnd", "j"},
    {0x81, "jl A,#", "w j"},
    {0x82, "jl A,f", "b j"},
    {0x83, "jl A,g", "w j"},
    {0x84, "jl A,t", "b j"},
    {0x85, "jl A,rnd", "j"},
    {0x86, "calle A,#", "w c"},
    {0x87, "calle A,f", "b c"},
    {0x88, "calle A,g", "w c"},
    {0x89, "calle A,t", "b c"},
    {0x8A, "calle A,rnd", "c"},
    {0x8B, "callne A,#", "w c"},
    {0x8C, "callne A,f", "b c"},
    {0x8D, "callne A,g", "w c"},
    {0x8E, "callne A,t", "b c"},
    {0x8F, "callne A,rnd", "c"},
    {0x90, "add_facing f,A", "b"},
    {0x91, "add_facing g,A", "w"},
    {0x92, "add_facing t,A", "b"},
    {0x93, "sub_facing f,A", "b"},
    {0x94, "sub_facing g,A", "w"},
    {0x95, "sub_facing t,A", "b"},
    {0x96, "target=A", ""},
    {0x97, "A=bit #", "b w"},
    {0x98, "A=bit f", "b b"},
    {0x99, "A=bit g", "b w"},
    {0x9A, "A=bit t", "b b"},
    {0x9B, "A=rnd&1", ""},
    {0x9C, "setbit f", "b b"},
    {0x9D, "setbit g", "b w"},
    {0x9E, "setbit t", "b b"},
    {0x9F, "andbit f", "b b"},
    {0xA0, "andbit g", "b w"},
    {0xA1, "andbit t", "b b"},
    {0xA2, "orbit f", "b b"},
    {0xA3, "orbit g", "b w"},
    {0xA4, "orbit t", "b b"},
    {0xA5, "xorbit f", "b b"},
    {0xA6, "xorbit g", "b w"},
    {0xA7, "xorbit t", "b b"},
    {0xA8, "je A,bit #", "b w j"},
    {0xA9, "je A,bit f", "b b j"},
    {0xAA, "je A,bit g", "b w j"},
    {0xAB, "je A,bit t", "b b j"},
    {0xAC, "je A,bit rnd", "j"},
    {0xAD, "jne A,bit #", "b w j"},
    {0xAE, "jne A,bit f", "b b j"},
    {0xAF, "jne A,bit g", "b w j"},
    {0xB0, "jne A,bit t", "b b j"},
    {0xB1, "jne A,bit rnd", "j"},
    {0xB2, "calle A,bit #", "b w c"},
    {0xB3, "calle A,bit f", "b b c"},
    {0xB4, "calle A,bit g", "b w c"},
    {0xB5, "calle A,bit t", "b b c"},
    {0xB6, "calle A,bit rnd", "c"},
    {0xB7, "callne A,bit #", "b w c"},
    {0xB8, "callne A,bit f", "b b c"},
    {0xB9, "callne A,bit g", "b w c"},
    {0xBA, "callne A,bit t", "b b c"},
    {0xBB, "callne A,bit rnd", "c"},
    {0xBC, "mov f,A<<8", "b"},
    {0xBD, "mov g,A<<8", "w"},
    {0xBE, "mov t,A<<8", "b"},
    {0xBF, "tileBF", "b j"},
    {0xC0, "tileC0", "b j"},
    {0xC1, "tileC1", "b j"},
    {0xC2, "tileC2", "b j"},
    {0xC3, "tileC3", "b j"},
    {0xC4, "tileC4", "b j"},
    {0xC5, "tileC5", "b j"},
    {0xC6, "tileC6", "b j"},
    {0xC7, "hdr_list0=A", ""},
    {0xC8, "hdr_list2=A", ""},
    {0xC9, "hdr_listA=A", ""},
    {0xCA, "hdr_listC=A", ""},
    {0xCB, "text_cmd4_alias", ""},
    {0xCC, "onscreenCC", "j"},
    {0xCD, "onscreenCD", "j"},
    {0xCE, "onscreenCE", "j"},
    {0xCF, "onscreenCF", "j"},
    {0xD0, "for_kind_below", "b c"},
    {0xD1, "for_kind_viking_below", "b c"},
    {0xD2, "password_load", ""},
    {0xD3, "password_check", ""},
    {0xD4, "move_towards", "G2 b"},
    {0xD5, "music_restart", "b"},
    {0xD6, "music_fade", ""},
    {0xD7, "sfx_stop3", "b w"},
}};

constexpr bool is_valid_token(std::string_view token) {
    return token == "b" || token == "s" || token == "w" || token == "j" || token == "c" || token == "G2" ||
           token == "G1" || token == "S2" || token == "S1";
}

// Every row in its place, every format well-formed and within kMaxOperands.
constexpr bool table_is_valid() {
    for (std::size_t i = 0; i < kOpcodes.size(); ++i) {
        const OpcodeInfo& op = kOpcodes[i];
        if (op.code != i || op.name.empty()) {
            return false;
        }
        std::size_t operands = 0;
        std::string_view rest = op.format;
        while (!rest.empty()) {
            const std::size_t space = rest.find(' ');
            const std::string_view token = rest.substr(0, space);
            if (!is_valid_token(token)) {
                return false;
            }
            operands += token.ends_with('2') ? 2 : 1;
            rest = space == std::string_view::npos ? std::string_view{} : rest.substr(space + 1);
        }
        if (operands > kMaxOperands) {
            return false;
        }
    }
    return true;
}
static_assert(table_is_valid());

constexpr std::array<OperandKind, 5> kModeKinds{OperandKind::kLiteral, OperandKind::kField, OperandKind::kGlobal,
                                                OperandKind::kTargetField, OperandKind::kRandom};

// Operand bytes per mode kind (0 for random). The set table in the original has only kinds 1-3.
constexpr std::size_t mode_operand_size(OperandKind kind) {
    switch (kind) {
        case OperandKind::kLiteral:
        case OperandKind::kGlobal:
            return 2;
        case OperandKind::kRandom:
            return 0;
        default:
            return 1;
    }
}

class Decoder {
  public:
    Decoder(std::span<const std::byte> code, std::size_t pos) noexcept : code_{code}, pos_{pos} {}

    std::size_t position() const noexcept { return pos_; }

    std::expected<std::uint16_t, Error> read(std::size_t size) noexcept {
        if (size > code_.size() - pos_) {
            return std::unexpected{Error::kOutOfBounds};
        }
        std::uint16_t value = 0;
        for (std::size_t i = 0; i < size; ++i) {
            value |= static_cast<std::uint16_t>(std::to_integer<unsigned>(code_[pos_ + i]) << (8U * i));
        }
        pos_ += size;
        return value;
    }

  private:
    std::span<const std::byte> code_;
    std::size_t pos_;
};

std::expected<void, Error> decode_token(Decoder& in, std::string_view token, Instruction& out) {
    const auto add = [&out](OperandKind kind, std::int32_t value) {
        out.operand_slots.at(out.operand_count++) = Operand{.kind = kind, .value = value};
    };
    if (token.starts_with('G') || token.starts_with('S')) {
        const auto mode = in.read(1);
        if (!mode) {
            return std::unexpected{mode.error()};
        }
        const bool is_set = token.starts_with('S');
        const std::size_t count = token.ends_with('2') ? 2 : 1;
        for (std::size_t i = 0; i < count; ++i) {
            const unsigned kind_number = (unsigned{*mode} >> (3U * i)) & 7U;
            if (kind_number >= kModeKinds.size() || (is_set && (kind_number == 0 || kind_number == 4))) {
                return std::unexpected{Error::kBadOperandKind};
            }
            const OperandKind kind = kModeKinds.at(kind_number);
            const auto value = in.read(mode_operand_size(kind));
            if (!value) {
                return std::unexpected{value.error()};
            }
            add(kind, *value);
        }
        return {};
    }
    const char type = token.front();
    const auto value = in.read(type == 'b' || type == 's' ? 1 : 2);
    if (!value) {
        return std::unexpected{value.error()};
    }
    switch (type) {
        case 'b':
            add(OperandKind::kByte, *value);
            break;
        case 's':
            add(OperandKind::kSignedByte, static_cast<std::int8_t>(*value));
            break;
        case 'w':
            add(OperandKind::kWord, *value);
            break;
        case 'j':
            add(OperandKind::kJump, *value);
            break;
        default:  // 'c'
            add(OperandKind::kCall, *value);
            break;
    }
    return {};
}

std::string format_operand(const Operand& operand) {
    switch (operand.kind) {
        case OperandKind::kByte:
            return std::format("{:02X}", operand.value);
        case OperandKind::kSignedByte:
            return std::format("{}", operand.value);
        case OperandKind::kWord:
            return std::format("{:04X}", operand.value);
        case OperandKind::kJump:
        case OperandKind::kCall:
            return std::format("@{:04X}", operand.value);
        case OperandKind::kLiteral:
            return std::format("#{:X}", operand.value);
        case OperandKind::kField:
            return std::format("f{:X}", operand.value);
        case OperandKind::kGlobal:
            return std::format("g{:X}", operand.value);
        case OperandKind::kTargetField:
            return std::format("t{:X}", operand.value);
        case OperandKind::kRandom:
            return "rnd";
    }
    return "?";
}

}  // namespace

// --- Instructions ---

const OpcodeInfo& opcode_info(std::uint8_t code) noexcept {
    static constexpr OpcodeInfo kNotAnOpcode{.code = 0xff, .name = "(not an opcode)", .format = ""};
    return code < kOpcodes.size() ? kOpcodes[code] : kNotAnOpcode;
}

std::expected<Instruction, Error> decode_instruction(std::span<const std::byte> code, std::uint16_t pc) {
    if (pc >= code.size()) {
        return std::unexpected{Error::kOutOfBounds};
    }
    const auto opcode = std::to_integer<std::uint8_t>(code[pc]);
    if (opcode >= kOpcodeCount) {
        return std::unexpected{Error::kBadOpcode};
    }
    Instruction out{.pc = pc, .opcode = opcode};
    Decoder in{code, std::size_t{pc} + 1};
    std::string_view rest = kOpcodes.at(opcode).format;
    while (!rest.empty()) {
        const std::size_t space = rest.find(' ');
        if (const auto decoded = decode_token(in, rest.substr(0, space), out); !decoded) {
            return std::unexpected{decoded.error()};
        }
        rest = space == std::string_view::npos ? std::string_view{} : rest.substr(space + 1);
    }
    out.length = static_cast<std::uint8_t>(in.position() - pc);
    return out;
}

bool ends_block(std::uint8_t opcode) noexcept {
    return opcode == 0x03 || opcode == 0x06 || opcode == 0x0F || opcode == 0x10;
}

std::string format_instruction(const Instruction& instruction) {
    std::string out{instruction.info().name};
    const char* separator = " ";
    for (const Operand& operand : instruction.operands()) {
        out += separator;
        out += format_operand(operand);
        separator = ", ";
    }
    return out;
}

// --- Following the code ---

std::vector<std::uint16_t> script_entry_points(const ObjectDatabase& database) {
    std::set<std::uint16_t> entries;
    for (const ObjectRecord& record : database.records) {
        entries.insert(record.script());
        entries.insert(static_cast<std::uint16_t>(record.script() + 3U));
    }
    return {entries.begin(), entries.end()};
}

CodeMap trace_code(std::span<const std::byte> code, std::span<const std::uint16_t> entry_points) {
    std::map<std::uint16_t, Instruction> seen;
    std::set<std::uint16_t> labels{entry_points.begin(), entry_points.end()};
    std::map<std::uint16_t, Error> failures;
    std::vector<std::uint16_t> todo{entry_points.begin(), entry_points.end()};
    while (!todo.empty()) {
        std::uint16_t pc = todo.back();
        todo.pop_back();
        while (!seen.contains(pc) && !failures.contains(pc)) {
            const auto instruction = decode_instruction(code, pc);
            if (!instruction) {
                failures.emplace(pc, instruction.error());
                break;
            }
            seen.emplace(pc, *instruction);
            for (const Operand& operand : instruction->operands()) {
                if (operand.kind == OperandKind::kJump || operand.kind == OperandKind::kCall) {
                    const auto target = static_cast<std::uint16_t>(operand.value);
                    labels.insert(target);
                    todo.push_back(target);
                }
            }
            if (ends_block(instruction->opcode) || pc + instruction->length > 0xffff) {
                break;
            }
            pc = static_cast<std::uint16_t>(pc + instruction->length);
        }
    }
    CodeMap out;
    out.instructions.reserve(seen.size());
    for (const auto& [pc, instruction] : seen) {
        out.instructions.push_back(instruction);
    }
    out.labels.assign(labels.begin(), labels.end());
    for (const auto& [pc, error] : failures) {
        out.failures.push_back(DecodeFailure{.pc = pc, .error = error});
    }
    return out;
}

std::string disassemble(const ObjectDatabase& database) {
    const CodeMap map = trace_code(database.bytes, script_entry_points(database));
    std::map<std::uint16_t, std::vector<std::size_t>> starts;  // script offset -> the types that start there
    for (std::size_t type = 0; type < database.records.size(); ++type) {
        starts[database.records[type].script()].push_back(type);
    }
    std::string out;
    std::size_t label = 0;
    for (const Instruction& instruction : map.instructions) {
        const std::uint16_t pc = instruction.pc;
        if (const auto types = starts.find(pc); types != starts.end()) {
            out += "\n; ---- object type(s) ";
            for (std::size_t i = 0; i < types->second.size(); ++i) {
                out += std::format("{}{:02X}", i == 0 ? "" : ", ", types->second[i]);
            }
            out += std::format(": header jump at {:04X}, start at {:04X}\n", pc, pc + 3);
        }
        while (label < map.labels.size() && map.labels[label] < pc) {
            ++label;
        }
        if (label < map.labels.size() && map.labels[label] == pc) {
            out += std::format("{:04X}:\n", pc);
        }
        std::string hex;
        for (std::size_t i = 0; i < instruction.length; ++i) {
            hex += std::format("{}{:02x}", i == 0 ? "" : " ", std::to_integer<unsigned>(database.bytes[pc + i]));
        }
        out += std::format("    {:04X}  {:<24} {}\n", pc, hex, format_instruction(instruction));
    }
    for (const DecodeFailure& failure : map.failures) {
        out += std::format("; error at {:04X}: {}\n", failure.pc, describe(failure.error));
    }
    return out;
}

// --- Animation programs ---

std::expected<EngineTables, Error> parse_engine_tables(std::span<const std::byte> frame_offsets,
                                                       std::span<const std::byte> piece_sizes,
                                                       std::span<const std::byte> slope_heights,
                                                       std::span<const std::byte> type_lists) {
    EngineTables tables;
    if (frame_offsets.size() != 2 * tables.frame_offsets.size() ||
        piece_sizes.size() != 2 * tables.piece_sizes.size() || slope_heights.size() != tables.slope_heights.size()) {
        return std::unexpected{Error::kBadSize};
    }
    for (std::size_t i = 0; i < slope_heights.size(); ++i) {
        tables.slope_heights.at(i) = std::to_integer<std::uint8_t>(slope_heights[i]);
    }
    for (const std::byte b : type_lists) {
        tables.type_lists.push_back(std::to_integer<std::uint8_t>(b));
    }
    const auto word = [](std::span<const std::byte> bytes, std::size_t i) {
        return static_cast<std::uint16_t>(std::to_integer<unsigned>(bytes[2 * i]) |
                                          (std::to_integer<unsigned>(bytes[(2 * i) + 1]) << 8U));
    };
    for (std::size_t i = 0; i < tables.frame_offsets.size(); ++i) {
        tables.frame_offsets.at(i) = word(frame_offsets, i);
    }
    for (std::size_t i = 0; i < tables.piece_sizes.size(); ++i) {
        tables.piece_sizes.at(i) = word(piece_sizes, i);
    }
    return tables;
}

std::expected<EngineTables, std::string> load_engine_tables(std::span<const std::byte> exe,
                                                            const assets::Release& release) {
    for (const assets::ExeRange range :
         {release.frame_offsets, release.piece_sizes, release.slope_heights, release.type_lists, release.dialogue_texts,
          release.passwords, release.initial_data, release.slots, release.trash, release.header18}) {
        if (range.offset > exe.size() || range.size > exe.size() - range.offset) {
            return std::unexpected{std::format("{}: the engine tables are past the end of the file", assets::kExeName)};
        }
    }
    const auto part = [exe](assets::ExeRange range) { return exe.subspan(range.offset, range.size); };
    auto tables = parse_engine_tables(part(release.frame_offsets), part(release.piece_sizes),
                                      part(release.slope_heights), part(release.type_lists))
                      .transform_error([](assets::Error error) {
                          return std::format("{} engine tables: {}", assets::kExeName, assets::describe(error));
                      });
    if (tables) {
        for (const std::byte b : part(release.dialogue_texts)) {
            tables->dialogue.push_back(std::to_integer<std::uint8_t>(b));
        }
        for (const std::byte b : part(release.passwords)) {
            tables->passwords.push_back(std::to_integer<std::uint8_t>(b));
        }
        for (const std::byte b : part(release.trash)) {
            tables->trash.push_back(std::to_integer<std::uint8_t>(b));
        }
        for (const std::byte b : part(release.header18)) {
            tables->header18.push_back(std::to_integer<std::uint8_t>(b));
        }
        for (const std::byte b : part(release.initial_data)) {
            tables->initial_data.push_back(std::to_integer<std::uint8_t>(b));
        }
        const auto slots = part(release.slots);
        const auto word = [slots](std::size_t i) {
            return static_cast<std::uint16_t>(std::to_integer<unsigned>(slots[2 * i]) |
                                              (std::to_integer<unsigned>(slots[(2 * i) + 1]) << 8U));
        };
        for (std::size_t i = 0; i < tables->slot_headers.size() && (2 * (i + 48)) + 1 < slots.size(); ++i) {
            tables->slot_headers.at(i) = word(i);
            tables->slot_databases.at(i) = word(i + 48);
        }
    }
    return tables;
}

}  // namespace lvr::assets
