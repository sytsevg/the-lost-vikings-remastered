#include "assets/reader.hpp"

namespace lvr::assets {

std::string_view describe(Error error) noexcept {
    switch (error) {
        case Error::kOutOfBounds:
            return "read past the end of the input";
        case Error::kTruncated:
            return "input is truncated";
        case Error::kBadOffsetTable:
            return "malformed offset table";
        case Error::kOffsetOutOfRange:
            return "an offset points past the end of the data";
        case Error::kOffsetsNotAscending:
            return "entry offsets are not ascending";
        case Error::kIndexOutOfRange:
            return "entry index out of range";
        case Error::kBadSize:
            return "data size does not fit the structure";
        case Error::kBadReference:
            return "reference to a tile, metatile or entry that does not exist";
        case Error::kBadOpcode:
            return "not an opcode";
        case Error::kBadOperandKind:
            return "a mode byte names an operand kind that does not exist";
        case Error::kBadChunk:
            return "an IFF chunk is not the one expected or does not fit";
        case Error::kBadEvent:
            return "not an XMIDI event, or the events have no end";
    }
    return "unknown error";
}

// --- ByteReader ---

std::expected<std::span<const std::byte>, Error> ByteReader::bytes(std::size_t count) noexcept {
    if (count > remaining()) {
        return std::unexpected{Error::kOutOfBounds};
    }
    const auto out = data_.subspan(pos_, count);
    pos_ += count;
    return out;
}

std::expected<std::uint8_t, Error> ByteReader::u8() noexcept {
    return bytes(1).transform([](std::span<const std::byte> b) { return std::to_integer<std::uint8_t>(b[0]); });
}

std::expected<std::uint16_t, Error> ByteReader::u16le() noexcept {
    return bytes(2).transform([](std::span<const std::byte> b) {
        const auto lo = std::to_integer<std::uint32_t>(b[0]);
        const auto hi = std::to_integer<std::uint32_t>(b[1]);
        return static_cast<std::uint16_t>(lo | (hi << 8U));
    });
}

std::expected<std::uint32_t, Error> ByteReader::u32le() noexcept {
    return bytes(4).transform([](std::span<const std::byte> b) {
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < b.size(); ++i) {
            value |= std::to_integer<std::uint32_t>(b[i]) << (8U * i);
        }
        return value;
    });
}

std::expected<void, Error> ByteReader::seek(std::size_t offset) noexcept {
    if (offset > data_.size()) {
        return std::unexpected{Error::kOutOfBounds};
    }
    pos_ = offset;
    return {};
}

}  // namespace lvr::assets
