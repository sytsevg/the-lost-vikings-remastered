#pragma once

// Everything about the DATA.DAT container (docs/formats/data-dat.md): the offset table, LZSS compression, and the
// game knowledge the file itself doesn't record (stored entries, the config entry, entry kinds, known releases).
// The findings are checked against the user's own files by the [real-data] tests in tests/test_data_dat.cpp next to
// this file.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "assets/reader.hpp"

namespace lvr::assets {

// --- Container ---

struct EntryRange {
    std::size_t offset = 0;
    std::size_t size = 0;
};

// A table of u32le entry offsets at the start of the file; entry count = first offset / 4; each entry runs to the
// next offset (the last one to the end of the file). No names, no magic number, no compression flag.
//
// Non-owning: the bytes passed to parse() must outlive the Archive.
class Archive {
  public:
    static std::expected<Archive, Error> parse(std::span<const std::byte> file);

    std::size_t entry_count() const noexcept { return offsets_.size(); }
    std::expected<EntryRange, Error> entry_range(std::size_t index) const noexcept;
    std::expected<std::span<const std::byte>, Error> raw_entry(std::size_t index) const noexcept;

  private:
    Archive(std::span<const std::byte> file, std::vector<std::uint32_t> offsets) noexcept
        : file_{file}, offsets_{std::move(offsets)} {}

    std::span<const std::byte> file_;
    std::vector<std::uint32_t> offsets_;
};

// --- LZSS ---

struct LzssOutput {
    std::vector<std::byte> data;
    std::size_t consumed = 0;  // input bytes used, including the header
};

// The Lost Vikings LZSS variant:
//   - u16le header = decompressed size - 1 (so 1..65536 bytes)
//   - a flag byte before every 8 items, read least-significant bit first: 1 = literal, 0 = reference
//   - literal: one byte
//   - reference: u16le; low 12 bits = window position, high 4 bits = length - 3 (3..18)
//   - 4096-byte ring-buffer window, zero-filled, write position starts at 0
// Output stops as soon as the declared size is reached, even in the middle of a reference.
// The SNES ROM's chunks are the same but for the header, which is the size itself (docs/formats/snes.md).
enum class LzssHeader : std::uint8_t { kSizeMinusOne, kSize };

std::expected<LzssOutput, Error> lzss_decompress(std::span<const std::byte> input,
                                                 LzssHeader header_kind = LzssHeader::kSizeMinusOne);

// --- Game knowledge ---

// Entries stored as-is; every other entry is LZSS-compressed. There is no flag in the file for this.
inline constexpr std::array<std::size_t, 8> kStoredEntries{0, 1, 380, 384, 529, 530, 531, 534};

// Entry 0 holds the sound settings that SETUP.EXE writes, so it differs between installations.
inline constexpr std::size_t kConfigEntry = 0;

// Marks "no entry" in level headers and in our entry tables.
inline constexpr std::uint16_t kNoEntry = 0xffff;

bool is_stored_entry(std::size_t index) noexcept;

// Raw bytes of a stored entry, or the decompressed bytes of a compressed one.
std::expected<std::vector<std::byte>, Error> decode_entry(const Archive& archive, std::size_t index);

enum class EntryKind : std::uint8_t {
    kUnknown,
    kConfig,  // sound configuration (entry 0)
    kXmidi,   // Miles Sound System extended MIDI: IFF "FORM....XDIR"
};

EntryKind detect_kind(std::size_t index, std::span<const std::byte> decoded) noexcept;
std::string_view to_string(EntryKind kind) noexcept;

// --- Release identification ---

// Bytes inside VIKINGS.EXE, by file offset (the 0x200-byte MZ header included). Only valid for the release they
// belong to.
struct ExeRange {
    std::size_t offset = 0;
    std::size_t size = 0;
};

struct Release {
    std::string_view name;
    std::uint64_t exe_hash = 0;  // fnv1a64 of VIKINGS.EXE
    std::uint64_t data_content_hash = 0;
    std::size_t entry_count = 0;
    ExeRange dialogue_texts{};  // segment 07A6 (docs/original/dialogue.md)
    ExeRange viking_starts{};   // ds:8508, the Vikings' start positions (docs/original/objects.md#level-start)
    ExeRange frame_offsets{};   // ds:871C, animation opcode 17 (docs/original/animation.md)
    ExeRange piece_sizes{};     // cs:32D7, animation opcode 15: 4 words, then code the original reads as data
    ExeRange slope_heights{};   // ds:897C, 16 x 16 bytes (docs/original/terrain.md)
    ExeRange type_lists{};      // ds:94CC, the tile-type lists the tile tests take (docs/original/terrain.md)
    ExeRange passwords{};       // ds:85A5, 4 bytes per level, bit 7 set (opcodes D2/D3)
    ExeRange initial_data{};    // ds:0000-2E7F as the game starts: the part of ds the simulation mirrors
    ExeRange slots{};           // ds:940C: the 48 slots' header entries, then their object databases (ds:946C)
    ExeRange trash{};           // ds:8592, by item: non-zero if the trash can takes it (the inventory, 0000:1F93)
    ExeRange header18{};        // ds:853E / 8544: 6 bytes each, by header word 0x18 (0000:1397)
};

// Releases the game and lv-export accept (docs/formats/releases.md).
inline constexpr std::array kKnownReleases{
    Release{.name = "vanilla-dos",
            .exe_hash = 0x5149f850f14a39c3ULL,
            .data_content_hash = 0xf4368f1cf41b4d8bULL,
            .entry_count = 535,
            .dialogue_texts = {.offset = 0x200 + 0x7A60, .size = 0x4070},
            .viking_starts = {.offset = 0x200 + (0x184E * 16) + 0x8508, .size = 0x36},
            .frame_offsets = {.offset = 0x200 + (0x184E * 16) + 0x871C, .size = 0x20},
            .piece_sizes = {.offset = 0x200 + 0x32D7, .size = 0x200},
            .slope_heights = {.offset = 0x200 + (0x184E * 16) + 0x897C, .size = 0x100},
            .type_lists = {.offset = 0x200 + (0x184E * 16) + 0x94CC, .size = 0x94},
            .passwords = {.offset = 0x200 + (0x184E * 16) + 0x85A5, .size = 0x94},
            .initial_data = {.offset = 0x200 + (0x184E * 16), .size = 0x2E80},
            .slots = {.offset = 0x200 + (0x184E * 16) + 0x940C, .size = 0xC0},
            // 19 items, then the passwords, which the original reads for a higher item number all the same
            .trash = {.offset = 0x200 + (0x184E * 16) + 0x8592, .size = 0x40},
            .header18 = {.offset = 0x200 + (0x184E * 16) + 0x853E, .size = 12}},
};

// fnv1a64 of DATA.DAT with the config entry's bytes left out, so the sound settings don't change it.
std::expected<std::uint64_t, Error> data_content_hash(std::span<const std::byte> data_dat);

std::optional<Release> identify_release(std::span<const std::byte> exe, std::span<const std::byte> data_dat);

}  // namespace lvr::assets
