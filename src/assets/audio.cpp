#include "assets/audio.hpp"

#include <array>
#include <string_view>

namespace lvr::assets {
namespace {

struct Chunk {
    std::array<char, 4> id{};
    std::span<const std::byte> body;
};

// The chunk at the reader's position (8-byte header, big-endian size); the reader moves past it and its padding.
std::expected<Chunk, Error> next_chunk(ByteReader& reader) {
    auto header = reader.bytes(8);
    if (!header) {
        return std::unexpected{Error::kBadChunk};
    }
    Chunk chunk;
    for (std::size_t i = 0; i < 4; ++i) {
        chunk.id.at(i) = static_cast<char>((*header)[i]);
    }
    std::uint32_t size = 0;
    for (std::size_t i = 4; i < 8; ++i) {
        size = (size << 8U) | std::to_integer<std::uint32_t>((*header)[i]);
    }
    auto body = reader.bytes(size);
    if (!body) {
        return std::unexpected{Error::kBadChunk};
    }
    chunk.body = *body;
    if ((size & 1U) != 0 && reader.remaining() > 0) {
        (void)reader.u8();
    }
    return chunk;
}

bool is(const Chunk& chunk, std::string_view id) {
    return std::string_view{chunk.id.data(), chunk.id.size()} == id;
}

// FORM / CAT: the body's first 4 bytes are its type.
std::expected<ByteReader, Error> group(const Chunk& chunk, std::string_view kind, std::string_view type) {
    if (!is(chunk, kind) || chunk.body.size() < 4) {
        return std::unexpected{Error::kBadChunk};
    }
    const auto* first = reinterpret_cast<const char*>(chunk.body.data());
    if (std::string_view{first, 4} != type) {
        return std::unexpected{Error::kBadChunk};
    }
    return ByteReader{chunk.body.subspan(4)};
}

std::expected<std::uint32_t, Error> variable_length(ByteReader& reader) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        auto b = reader.u8();
        if (!b) {
            return std::unexpected{Error::kBadEvent};
        }
        value = (value << 7U) | (*b & 0x7FU);
        if ((*b & 0x80U) == 0) {
            return value;
        }
    }
    return std::unexpected{Error::kBadEvent};
}

std::expected<XmidiSequence, Error> parse_sequence(ByteReader form) {
    XmidiSequence sequence;
    while (form.remaining() > 0) {
        auto chunk = next_chunk(form);
        if (!chunk) {
            return std::unexpected{chunk.error()};
        }
        if (is(*chunk, "TIMB")) {
            ByteReader timb{chunk->body};
            auto count = timb.u16le();
            if (!count || timb.remaining() < std::size_t{*count} * 2) {
                return std::unexpected{Error::kBadChunk};
            }
            for (std::uint16_t i = 0; i < *count; ++i) {
                const std::uint8_t patch = *timb.u8();
                sequence.timbres.push_back(XmidiTimbre{.patch = patch, .bank = *timb.u8()});
            }
            continue;
        }
        if (!is(*chunk, "EVNT")) {
            continue;
        }
        sequence.stream.assign(chunk->body.begin(), chunk->body.end());
        ByteReader events{chunk->body};
        std::uint32_t tick = 0;
        for (;;) {
            auto status = events.u8();
            if (!status) {
                return std::unexpected{Error::kBadEvent};  // no end of track
            }
            if (*status < 0x80) {
                tick += *status;
                continue;
            }
            XmidiEvent event{.tick = tick, .status = *status};
            const unsigned kind = *status & 0xF0U;
            if (*status == 0xFF) {
                auto type = events.u8();
                auto length = type ? variable_length(events) : std::unexpected{Error::kBadEvent};
                auto payload = length ? events.bytes(*length) : std::unexpected{Error::kBadEvent};
                if (!payload) {
                    return std::unexpected{Error::kBadEvent};
                }
                if (*type == 0x2F) {
                    sequence.end = tick;
                    return sequence;
                }
                event.data1 = *type;
                event.payload.assign(payload->begin(), payload->end());
            } else if (kind == 0xF0) {
                return std::unexpected{Error::kBadEvent};  // SysEx and the rest: never used
            } else {
                auto a = events.u8();
                if (!a) {
                    return std::unexpected{Error::kBadEvent};
                }
                event.data1 = *a;
                if (kind != 0xC0 && kind != 0xD0) {
                    auto b = events.u8();
                    if (!b) {
                        return std::unexpected{Error::kBadEvent};
                    }
                    event.data2 = *b;
                }
                if (kind == 0x90) {
                    auto duration = variable_length(events);
                    if (!duration) {
                        return std::unexpected{duration.error()};
                    }
                    event.duration = *duration;
                }
            }
            sequence.events.push_back(std::move(event));
        }
    }
    return std::unexpected{Error::kBadChunk};  // no EVNT
}

}  // namespace

std::expected<std::vector<XmidiSequence>, Error> parse_xmidi(std::span<const std::byte> data) {
    ByteReader top{data};
    auto xdir = next_chunk(top);
    if (!xdir) {
        return std::unexpected{xdir.error()};
    }
    auto dir = group(*xdir, "FORM", "XDIR");
    if (!dir) {
        return std::unexpected{dir.error()};
    }
    auto info = next_chunk(*dir);
    if (!info || !is(*info, "INFO") || info->body.size() < 2) {
        return std::unexpected{Error::kBadChunk};
    }
    const auto count = static_cast<std::size_t>(std::to_integer<unsigned>(info->body[0]) |
                                                (std::to_integer<unsigned>(info->body[1]) << 8U));
    auto cat_chunk = next_chunk(top);
    if (!cat_chunk) {
        return std::unexpected{cat_chunk.error()};
    }
    auto cat = group(*cat_chunk, "CAT ", "XMID");
    if (!cat) {
        return std::unexpected{cat.error()};
    }
    std::vector<XmidiSequence> sequences;
    while (sequences.size() < count) {
        auto form = next_chunk(*cat);
        if (!form) {
            return std::unexpected{form.error()};
        }
        auto body = group(*form, "FORM", "XMID");
        if (!body) {
            return std::unexpected{body.error()};
        }
        auto sequence = parse_sequence(*body);
        if (!sequence) {
            return std::unexpected{sequence.error()};
        }
        sequences.push_back(std::move(*sequence));
    }
    return sequences;
}

std::expected<std::vector<Timbre>, Error> parse_timbre_bank(std::span<const std::byte> data) {
    ByteReader reader{data};
    std::vector<Timbre> bank;
    for (;;) {
        auto patch = reader.u8();
        auto bank_number = reader.u8();
        if (!patch || !bank_number) {
            return std::unexpected{Error::kTruncated};
        }
        if (*patch == 0xFF && *bank_number == 0xFF) {
            return bank;
        }
        auto offset = reader.u32le();
        if (!offset) {
            return std::unexpected{Error::kTruncated};
        }
        ByteReader at{data};
        if (!at.seek(*offset)) {
            return std::unexpected{Error::kOffsetOutOfRange};
        }
        auto size = at.u16le();
        if (!size || *size < 2) {
            return std::unexpected{Error::kBadSize};
        }
        (void)at.seek(*offset);
        auto bytes = at.bytes(*size);
        if (!bytes) {
            return std::unexpected{Error::kOffsetOutOfRange};
        }
        bank.push_back(Timbre{.patch = *patch, .bank = *bank_number, .data = {bytes->begin(), bytes->end()}});
    }
}

namespace {

template <std::size_t N>
void copy_bytes(std::array<std::uint8_t, N>& out, std::span<const std::byte> data, std::size_t at) {
    for (std::size_t i = 0; i < N; ++i) {
        out.at(i) = std::to_integer<std::uint8_t>(data[at + i]);
    }
}

SequencerTables sequencer_tables(std::span<const std::byte> data, std::size_t at) {
    SequencerTables t;
    copy_bytes(t.controllers, data, at);
    copy_bytes(t.controller_defaults, data, at + 9);
    copy_bytes(t.default_programs, data, at + 17);
    return t;
}

}  // namespace

std::expected<OplDriverTables, Error> parse_opl_driver(std::span<const std::byte> data) {
    constexpr std::size_t kSize = 16'263;
    constexpr std::string_view kName = "Sound Blaster Pro(TM) FM";
    if (data.size() != kSize) {
        return std::unexpected{Error::kBadSize};
    }
    const std::string_view text{reinterpret_cast<const char*>(data.data()), data.size()};
    if (!text.contains(kName)) {
        return std::unexpected{Error::kBadReference};
    }
    OplDriverTables t;
    for (std::size_t i = 0; i < t.fnumbers.size(); ++i) {
        t.fnumbers.at(i) = static_cast<std::uint16_t>(std::to_integer<unsigned>(data[0x10D + (2 * i)]) |
                                                      (std::to_integer<unsigned>(data[0x10E + (2 * i)]) << 8U));
    }
    copy_bytes(t.blocks, data, 0x28D);
    copy_bytes(t.note_in_octave, data, 0x2ED);
    for (std::size_t reg = 0x001; reg <= 0x0F5; ++reg) {
        t.init_values.at(reg) = std::to_integer<std::uint8_t>(data[0x34C + reg]);
    }
    for (std::size_t reg = 0x101; reg <= 0x1F5; ++reg) {
        t.init_values.at(reg) = std::to_integer<std::uint8_t>(data[0x341 + reg]);
    }
    copy_bytes(t.velocity, data, 0x537);
    copy_bytes(t.first_operator, data, 0x547);
    copy_bytes(t.second_operator, data, 0x559);
    copy_bytes(t.operator_offset, data, 0x56B);
    copy_bytes(t.operator_chip, data, 0x58F);
    copy_bytes(t.channel_offset, data, 0x5B3);
    copy_bytes(t.channel_chip, data, 0x5C5);
    copy_bytes(t.four_op_capable, data, 0x5D7);
    copy_bytes(t.partner, data, 0x5E9);
    copy_bytes(t.partner_first, data, 0x5FB);
    copy_bytes(t.partner_second, data, 0x60D);
    copy_bytes(t.four_op_bit, data, 0x61F);
    copy_bytes(t.four_op_channels, data, 0x631);
    copy_bytes(t.scaled_pair1, data, 0x637);
    copy_bytes(t.scaled_pair2, data, 0x63B);
    t.sequencer = sequencer_tables(data, 0x2814);
    return t;
}

std::expected<SequencerTables, Error> parse_midi_driver(std::span<const std::byte> data) {
    constexpr std::size_t kSize = 10'625;
    constexpr std::string_view kName = "Roland MT-32 or compatible with Roland MPU MIDI Interface";
    if (data.size() != kSize) {
        return std::unexpected{Error::kBadSize};
    }
    const std::string_view text{reinterpret_cast<const char*>(data.data()), data.size()};
    if (!text.contains(kName)) {
        return std::unexpected{Error::kBadReference};
    }
    return sequencer_tables(data, 0x11D7);
}

}  // namespace lvr::assets
