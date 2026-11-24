#include "assets/level.hpp"

namespace lvr::assets {

namespace {

constexpr std::uint16_t kEndOfList = 0xffff;

// Reads a u8 or u16, reporting a short input as kTruncated.
std::expected<std::uint8_t, Error> read_u8(ByteReader& reader) {
    return reader.u8().transform_error([](Error) { return Error::kTruncated; });
}

std::expected<std::uint16_t, Error> read_u16(ByteReader& reader) {
    return reader.u16le().transform_error([](Error) { return Error::kTruncated; });
}

}  // namespace

std::expected<LevelHeader, Error> parse_level_header(std::span<const std::byte> data) {
    constexpr std::size_t kMapInfoOffset = 0x29;
    constexpr std::size_t kObjectsOffset = 0x43;

    constexpr std::size_t kFlagsOffset = 0x1c;

    LevelHeader header;
    ByteReader reader{data};
    if (data.size() < kMapInfoOffset) {
        return std::unexpected{Error::kTruncated};
    }
    // 0x00-0x17: what the level start reads (docs/formats/level.md); the size was checked above.
    header.push = *reader.u16le();
    header.gravity = *reader.u16le();
    header.music_at_start = *reader.u8();
    header.tune = *reader.u8();
    header.music_at_end = *reader.u8();
    header.start_mode = *reader.u8();
    header.start_x = *reader.u16le();
    header.start_y = *reader.u16le();
    header.start_type = *reader.u16le();
    header.start_flags = *reader.u16le();
    header.start_arg = *reader.u16le();
    header.switches = *reader.u16le();
    header.word_14 = *reader.u16le();
    header.next_slot = *reader.u16le();
    header.flags = std::to_integer<std::uint8_t>(data[kFlagsOffset]);
    if (!reader.seek(kMapInfoOffset)) {
        return std::unexpected{Error::kTruncated};
    }

    // 0x29 width, 0x2b height, 0x2d unknown u8, 0x2e map entry, 0x30 tileset entry, 0x32 metatiles entry
    const auto width = read_u16(reader);
    const auto height = read_u16(reader);
    const auto unknown = reader.u8();
    const auto map_entry = read_u16(reader);
    const auto tileset_entry = read_u16(reader);
    const auto metatiles_entry = read_u16(reader);
    if (!width || !height || !unknown || !map_entry || !tileset_entry || !metatiles_entry) {
        return std::unexpected{Error::kTruncated};
    }
    header.width = *width;
    header.height = *height;
    header.map_entry = *map_entry;
    header.tileset_entry = *tileset_entry;
    header.metatiles_entry = *metatiles_entry;

    // Objects: 14 bytes each (x, y, half width, half height, type, flags, arg), ended by x == 0xffff.
    if (!reader.seek(kObjectsOffset)) {
        return std::unexpected{Error::kTruncated};
    }
    while (true) {
        const auto x = read_u16(reader);
        if (!x) {
            return std::unexpected{x.error()};
        }
        if (*x == kEndOfList) {
            break;
        }
        std::array<std::uint16_t, 6> fields{};
        for (std::uint16_t& field : fields) {
            const auto value = read_u16(reader);
            if (!value) {
                return std::unexpected{value.error()};
            }
            field = *value;
        }
        header.objects.push_back(ObjectPlacement{.x = *x,
                                                 .y = fields[0],
                                                 .width = static_cast<std::uint16_t>(fields[1] * 2U),
                                                 .height = static_cast<std::uint16_t>(fields[2] * 2U),
                                                 .type = fields[3],
                                                 .flags = fields[4],
                                                 .arg = fields[5]});
    }

    // Palettes: 3 bytes each (entry, first colour), ended by entry == 0xffff.
    while (true) {
        const auto entry = read_u16(reader);
        if (!entry) {
            return std::unexpected{entry.error()};
        }
        if (*entry == kEndOfList) {
            break;
        }
        const auto first_color = reader.u8();
        if (!first_color) {
            return std::unexpected{Error::kTruncated};
        }
        header.palettes.push_back(PaletteRef{.entry = *entry, .first_color = *first_color});
    }

    // Palette animations: u16 flags, then 3 bytes (delay, from, to) and u16 values ended by 0xffff each, ended by
    // delay == 0.
    const auto animation_flags = read_u16(reader);
    if (!animation_flags) {
        return std::unexpected{animation_flags.error()};
    }
    header.palette_animation_flags = *animation_flags;
    while (true) {
        const auto delay = read_u8(reader);
        if (!delay) {
            return std::unexpected{delay.error()};
        }
        if (*delay == 0) {
            break;
        }
        const auto from_color = read_u8(reader);
        const auto to_color = read_u8(reader);
        if (!from_color || !to_color) {
            return std::unexpected{Error::kTruncated};
        }
        PaletteAnimation animation{.delay = *delay, .from_color = *from_color, .to_color = *to_color, .values = {}};
        while (true) {
            const auto value = read_u16(reader);
            if (!value) {
                return std::unexpected{value.error()};
            }
            if (*value == kEndOfList) {
                break;
            }
            animation.values.push_back(*value);
        }
        header.palette_animations.push_back(std::move(animation));
    }

    // Sprite sets: 6 bytes each (entry, slot, sheet), ended by entry == 0xffff.
    while (true) {
        const auto entry = read_u16(reader);
        if (!entry) {
            return std::unexpected{entry.error()};
        }
        if (*entry == kEndOfList) {
            break;
        }
        const auto slot = read_u16(reader);
        const auto sheet = read_u16(reader);
        if (!slot || !sheet) {
            return std::unexpected{Error::kTruncated};
        }
        header.sprite_sets.push_back(SpriteSetRef{.entry = *entry, .slot = *slot, .sheet = *sheet});
    }

    // Packed sets: 5 bytes each (entry, 3 bytes), ended by entry == 0xffff.
    while (true) {
        const auto entry = read_u16(reader);
        if (!entry) {
            return std::unexpected{entry.error()};
        }
        if (*entry == kEndOfList) {
            break;
        }
        PackedSetRef set{.entry = *entry, .fields = {}};
        for (std::uint8_t& field : set.fields) {
            const auto value = read_u8(reader);
            if (!value) {
                return std::unexpected{value.error()};
            }
            field = *value;
        }
        header.packed_sets.push_back(set);
    }

    header.end = reader.position();
    return header;
}

std::expected<std::array<VikingStartTable, 3>, Error> parse_viking_starts(std::span<const std::byte> data) {
    if (data.size() != 54) {  // 3 tables × 3 Vikings × 3 words
        return std::unexpected{Error::kBadSize};
    }
    ByteReader in{data};
    std::array<VikingStartTable, 3> tables{};
    for (VikingStartTable& table : tables) {
        for (VikingStart& start : table) {
            start = VikingStart{.x = *in.u16le(), .y = *in.u16le(), .flags = *in.u16le()};
        }
    }
    return tables;
}

}  // namespace lvr::assets
