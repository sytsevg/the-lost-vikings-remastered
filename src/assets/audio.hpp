#pragma once

// The music and sound effects (docs/formats/audio.md): XMIDI files (the Miles Audio Interface Library's extended
// MIDI: an IFF tree of sequences, each with the instruments it uses and its event stream) and the Global Timbre
// Library (the OPL and MT-32 instrument banks).

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "assets/reader.hpp"

namespace lvr::assets {

// One event of a sequence, at its absolute time in XMIDI ticks (120 a second by the XMIDI documentation).
struct XmidiEvent {
    std::uint32_t tick = 0;
    std::uint8_t status = 0;           // 0x80-0xEF a channel event (n = the channel, 0-15), 0xFF a meta event
    std::uint8_t data1 = 0;            // note / controller / program / bend LSB; a meta event's type
    std::uint8_t data2 = 0;            // velocity / value / bend MSB (0 for one-byte events)
    std::uint32_t duration = 0;        // note on: how many ticks it sounds (XMIDI has no note offs)
    std::vector<std::byte> payload{};  // a meta event's data

    bool operator==(const XmidiEvent&) const = default;
};

struct XmidiTimbre {
    std::uint8_t patch = 0;
    std::uint8_t bank = 0;

    bool operator==(const XmidiTimbre&) const = default;
};

struct XmidiSequence {
    std::vector<XmidiTimbre> timbres;  // TIMB: the instruments it uses (empty without the chunk)
    std::vector<std::byte> stream{};   // EVNT as stored (the driver plays the bytes)
    std::vector<XmidiEvent> events;    // EVNT, up to but without the end of track
    std::uint32_t end = 0;             // the end of track's tick
};

// An XMIDI file: FORM XDIR (INFO: the sequence count), CAT XMID of FORM XMID (TIMB, EVNT). Chunk sizes are big-endian;
// a stream stops at its end of track (FF 2F 00), before any padding byte.
std::expected<std::vector<XmidiSequence>, Error> parse_xmidi(std::span<const std::byte> data);

// One instrument of a Global Timbre Library: its bytes, starting with their own u16le size.
struct Timbre {
    std::uint8_t patch = 0;
    std::uint8_t bank = 0;
    std::vector<std::byte> data{};
};

// A Global Timbre Library: records of (patch, bank, u32le offset) up to ff ff, the timbres at the offsets.
std::expected<std::vector<Timbre>, Error> parse_timbre_bank(std::span<const std::byte> data);

// The tables of the Miles OPL3 driver (entry 460, Sound Blaster Pro FM) that its voice layer uses
// (docs/original/sound-driver.md#tables-used-read-from-entry-460-by-the-remake-not-copied), read at their offsets in
// the entry.
// The tables of the Miles drivers' shared XMIDI sequencer (docs/original/sound-driver.md); the same bytes in 460 and
// 463.
struct SequencerTables {
    std::array<std::uint8_t, 9> controllers{};          // 460: 2814, 463: 11D7: the controllers remembered per channel
    std::array<std::uint8_t, 9> controller_defaults{};  // 281D, 11E0
    std::array<std::uint8_t, 10> default_programs{};    // 2825, 11E8: by MIDI channel (0-based), 0xFF none
};

struct OplDriverTables {
    std::array<std::uint16_t, 192> fnumbers{};       // 010D: by note in the octave (12) x sixteenths (16)
    std::array<std::uint8_t, 96> blocks{};           // 028D: by semitone
    std::array<std::uint8_t, 96> note_in_octave{};   // 02ED
    std::array<std::uint8_t, 0x200> init_values{};   // 034D-0441, 0442-0536: registers 001-0F5, 101-1F5 at reset
    std::array<std::uint8_t, 16> velocity{};         // 0537: by velocity / 8
    std::array<std::uint8_t, 18> first_operator{};   // 0547: a channel's operator slots (0-35)
    std::array<std::uint8_t, 18> second_operator{};  // 0559
    std::array<std::uint8_t, 36> operator_offset{};  // 056B: register offset of an operator slot
    std::array<std::uint8_t, 36> operator_chip{};    // 058F: 0 or 1 (registers 1xx)
    std::array<std::uint8_t, 18> channel_offset{};   // 05B3
    std::array<std::uint8_t, 18> channel_chip{};     // 05C5
    std::array<std::uint8_t, 18> four_op_capable{};  // 05D7
    std::array<std::uint8_t, 18> partner{};          // 05E9: the 4-operator partner channel (0xFF none)
    std::array<std::uint8_t, 18> partner_first{};    // 05FB: the partner's operator slots
    std::array<std::uint8_t, 18> partner_second{};   // 060D
    std::array<std::uint8_t, 18> four_op_bit{};      // 061F: the channel's bit in register 104
    std::array<std::uint8_t, 6> four_op_channels{};  // 0631: the first channels of the 6 pairs
    std::array<std::uint8_t, 4> scaled_pair1{};      // 0637: which operators follow the volume, by connection
    std::array<std::uint8_t, 4> scaled_pair2{};      // 063B
    SequencerTables sequencer;
};

// Entry 460 as the vanilla-dos release has it (16,263 bytes, "Sound Blaster Pro(TM) FM" in its description).
std::expected<OplDriverTables, Error> parse_opl_driver(std::span<const std::byte> data);

// Entry 463, the MIDI driver ("Roland MT-32 or compatible with Roland MPU MIDI Interface", 10,625 bytes): its
// sequencer's tables. The rest of it is the MPU-401 output, which the Modern sound's synthesizer takes the place of.
std::expected<SequencerTables, Error> parse_midi_driver(std::span<const std::byte> data);

}  // namespace lvr::assets
