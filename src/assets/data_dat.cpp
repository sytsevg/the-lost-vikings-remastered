#include "assets/data_dat.hpp"

#include <algorithm>

#include "assets/hash.hpp"

namespace lvr::assets {

// --- Container ---

std::expected<Archive, Error> Archive::parse(std::span<const std::byte> file) {
    constexpr std::uint32_t kOffsetSize = 4;

    ByteReader reader{file};
    const auto first = reader.u32le();
    if (!first) {
        return std::unexpected{Error::kTruncated};
    }
    if (*first == 0 || *first % kOffsetSize != 0) {
        return std::unexpected{Error::kBadOffsetTable};
    }
    if (*first > file.size()) {
        return std::unexpected{Error::kOffsetOutOfRange};
    }

    const std::size_t count = *first / kOffsetSize;
    std::vector<std::uint32_t> offsets;
    offsets.reserve(count);
    offsets.push_back(*first);
    for (std::size_t i = 1; i < count; ++i) {
        const auto offset = reader.u32le();
        if (!offset) {
            return std::unexpected{Error::kTruncated};
        }
        if (*offset > file.size()) {
            return std::unexpected{Error::kOffsetOutOfRange};
        }
        if (*offset < offsets.back()) {
            return std::unexpected{Error::kOffsetsNotAscending};
        }
        offsets.push_back(*offset);
    }
    return Archive{file, std::move(offsets)};
}

std::expected<EntryRange, Error> Archive::entry_range(std::size_t index) const noexcept {
    if (index >= offsets_.size()) {
        return std::unexpected{Error::kIndexOutOfRange};
    }
    const std::size_t begin = offsets_[index];
    const std::size_t end = index + 1 < offsets_.size() ? offsets_[index + 1] : file_.size();
    return EntryRange{.offset = begin, .size = end - begin};
}

std::expected<std::span<const std::byte>, Error> Archive::raw_entry(std::size_t index) const noexcept {
    return entry_range(index).transform([this](EntryRange range) { return file_.subspan(range.offset, range.size); });
}

// --- LZSS ---

std::expected<LzssOutput, Error> lzss_decompress(std::span<const std::byte> input, LzssHeader header_kind) {
    constexpr std::size_t kWindowSize = 0x1000;
    constexpr std::size_t kWindowMask = kWindowSize - 1;
    constexpr std::uint32_t kPositionMask = 0x0fffU;
    constexpr std::uint32_t kLengthShift = 12U;
    constexpr std::size_t kMinLength = 3;

    ByteReader reader{input};
    const auto header = reader.u16le();
    if (!header) {
        return std::unexpected{Error::kTruncated};
    }
    const std::size_t size = std::size_t{*header} + (header_kind == LzssHeader::kSizeMinusOne ? 1U : 0U);

    std::array<std::byte, kWindowSize> window{};
    std::size_t write_pos = 0;
    LzssOutput out;
    out.data.reserve(size);

    const auto emit = [&](std::byte b) {
        out.data.push_back(b);
        window[write_pos] = b;
        write_pos = (write_pos + 1) & kWindowMask;
    };

    while (out.data.size() < size) {
        const auto flags = reader.u8();
        if (!flags) {
            return std::unexpected{Error::kTruncated};
        }
        const std::uint32_t flag_bits = *flags;
        for (std::uint32_t bit = 0; bit < 8U && out.data.size() < size; ++bit) {
            if (((flag_bits >> bit) & 1U) != 0U) {
                const auto literal = reader.u8();
                if (!literal) {
                    return std::unexpected{Error::kTruncated};
                }
                emit(std::byte{*literal});
            } else {
                const auto reference = reader.u16le();
                if (!reference) {
                    return std::unexpected{Error::kTruncated};
                }
                const std::uint32_t info = *reference;
                const std::size_t position = info & kPositionMask;
                const std::size_t length = (info >> kLengthShift) + kMinLength;
                for (std::size_t k = 0; k < length && out.data.size() < size; ++k) {
                    emit(window[(position + k) & kWindowMask]);
                }
            }
        }
    }

    out.consumed = reader.position();
    return out;
}

// --- Game knowledge ---

namespace {

bool has_tag(std::span<const std::byte> data, std::size_t offset, std::string_view tag) noexcept {
    if (offset > data.size() || data.size() - offset < tag.size()) {
        return false;
    }
    for (std::size_t i = 0; i < tag.size(); ++i) {
        if (std::to_integer<unsigned char>(data[offset + i]) != static_cast<unsigned char>(tag[i])) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool is_stored_entry(std::size_t index) noexcept {
    return std::ranges::contains(kStoredEntries, index);
}

std::expected<std::vector<std::byte>, Error> decode_entry(const Archive& archive, std::size_t index) {
    const auto raw = archive.raw_entry(index);
    if (!raw) {
        return std::unexpected{raw.error()};
    }
    if (is_stored_entry(index)) {
        return std::vector<std::byte>(raw->begin(), raw->end());
    }
    auto decoded = lzss_decompress(*raw);
    if (!decoded) {
        return std::unexpected{decoded.error()};
    }
    return std::move(decoded->data);
}

EntryKind detect_kind(std::size_t index, std::span<const std::byte> decoded) noexcept {
    if (index == kConfigEntry) {
        return EntryKind::kConfig;
    }
    if (has_tag(decoded, 0, "FORM") && has_tag(decoded, 8, "XDIR")) {
        return EntryKind::kXmidi;
    }
    return EntryKind::kUnknown;
}

std::string_view to_string(EntryKind kind) noexcept {
    switch (kind) {
        case EntryKind::kUnknown:
            return "unknown";
        case EntryKind::kConfig:
            return "config";
        case EntryKind::kXmidi:
            return "xmidi";
    }
    return "unknown";
}

// --- Release identification ---

std::expected<std::uint64_t, Error> data_content_hash(std::span<const std::byte> data_dat) {
    const auto archive = Archive::parse(data_dat);
    if (!archive) {
        return std::unexpected{archive.error()};
    }
    const auto config = archive->entry_range(kConfigEntry);
    if (!config) {
        return std::unexpected{config.error()};
    }
    Fnv1a64 hasher;
    hasher.update(data_dat.first(config->offset));
    hasher.update(data_dat.subspan(config->offset + config->size));
    return hasher.value();
}

std::optional<Release> identify_release(std::span<const std::byte> exe, std::span<const std::byte> data_dat) {
    const auto content_hash = data_content_hash(data_dat);
    if (!content_hash) {
        return std::nullopt;
    }
    const std::uint64_t exe_hash = fnv1a64(exe);
    for (const Release& release : kKnownReleases) {
        if (release.exe_hash == exe_hash && release.data_content_hash == *content_hash) {
            return release;
        }
    }
    return std::nullopt;
}

}  // namespace lvr::assets
