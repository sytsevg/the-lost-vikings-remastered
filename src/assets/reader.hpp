#pragma once

// Low-level building blocks shared by every format parser: the error type and a bounds-checked reader.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace lvr::assets {

// One error type for all format parsers, returned through std::expected.
enum class Error : std::uint8_t {
    kOutOfBounds,          // read or seek past the end of the input
    kTruncated,            // input ends before the structure is complete
    kBadOffsetTable,       // an offset table (archive, packed sprites) is malformed
    kOffsetOutOfRange,     // an offset points past the end of the data
    kOffsetsNotAscending,  // archive entry offsets go backwards
    kIndexOutOfRange,      // requested entry does not exist
    kBadSize,              // data size does not fit the structure (e.g. not a whole number of records)
    kBadReference,         // an index points at a tile, metatile or entry that does not exist
    kBadOpcode,            // a script byte that is not an opcode
    kBadOperandKind,       // a script mode byte names an operand kind that does not exist
    kBadChunk,             // an IFF chunk (XMIDI) isn't the one expected, or doesn't fit its parent
    kBadEvent,             // an XMIDI event stream byte that starts no event, or a stream without an end
};

std::string_view describe(Error error) noexcept;

// Bounds-checked little-endian reader over untrusted bytes. Every read either succeeds and advances,
// or fails with Error::kOutOfBounds and leaves the position unchanged.
class ByteReader {
  public:
    explicit ByteReader(std::span<const std::byte> data) noexcept : data_{data} {}

    std::expected<std::uint8_t, Error> u8() noexcept;
    std::expected<std::uint16_t, Error> u16le() noexcept;
    std::expected<std::uint32_t, Error> u32le() noexcept;
    std::expected<std::span<const std::byte>, Error> bytes(std::size_t count) noexcept;
    std::expected<void, Error> seek(std::size_t offset) noexcept;

    std::size_t position() const noexcept { return pos_; }
    std::size_t size() const noexcept { return data_.size(); }
    std::size_t remaining() const noexcept { return data_.size() - pos_; }

  private:
    std::span<const std::byte> data_;
    std::size_t pos_ = 0;
};

}  // namespace lvr::assets
