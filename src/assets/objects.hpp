#pragma once

// A world's object database (DATA.DAT entries 449-454, docs/original/script-vm.md#the-object-database-449454): one
// record per object type (its sprite set, size, script offset, the fields it starts with) and the byte lists after
// them. The scripts' bytecode is decoded in assets/script.hpp.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "assets/reader.hpp"

namespace lvr::assets {

// --- Object database ---

inline constexpr std::size_t kObjectTypeCount = 235;  // the same in every database
inline constexpr std::size_t kObjectRecordSize = 0x15;

// One object type. Only the fields below are known; the rest of the record is kept as bytes.
struct ObjectRecord {
    std::array<std::uint8_t, kObjectRecordSize> bytes{};

    std::uint16_t word(std::size_t offset) const noexcept {
        return static_cast<std::uint16_t>(unsigned{bytes[offset]} | (unsigned{bytes[offset + 1]} << 8U));
    }
    // Where the type's script starts in the database. A spawned object starts at script() + 3; the three bytes at
    // script() are a jump that the restart flag (0x0200) goes through.
    std::uint16_t script() const noexcept { return word(3); }
};

struct ObjectDatabase {
    std::vector<ObjectRecord> records;  // kObjectTypeCount, indexed by object type
    // The 0xff-terminated byte lists after the records (offset 0x1347), up to the first script. Identical in all
    // six databases; what reads them is still open (the search opcodes read a similar table at ds:94CC).
    std::vector<std::vector<std::uint8_t>> byte_lists;
    std::vector<std::byte> bytes;  // the whole entry: script and jump offsets index it
};

// Checks the size (at most 64 KiB: offsets are u16) and that every script offset points into the entry.
std::expected<ObjectDatabase, Error> parse_object_database(std::span<const std::byte> data);

}  // namespace lvr::assets
