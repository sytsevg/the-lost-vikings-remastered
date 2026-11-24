#include "audio/opl3.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>

#include "ymfm_opl.h"

namespace lvr::audio {
namespace {

template <typename T>
constexpr std::uint8_t u8(T value) noexcept {
    return static_cast<std::uint8_t>(value);
}

constexpr std::uint8_t kFree = 0xFF;
constexpr int kVoices = 20;
constexpr int kOplChannels = 18;
constexpr std::uint8_t kPercussion = 9;  // MIDI channel 10

// 0E49: what a voice update writes.
constexpr unsigned kWriteMultiple = 0x80U;
constexpr unsigned kWriteLevel = 0x40U;
constexpr unsigned kWriteEnvelope = 0x20U;
constexpr unsigned kWriteWaveform = 0x10U;
constexpr unsigned kWriteConnection = 0x08U;
constexpr unsigned kWriteFrequency = 0x01U;
constexpr unsigned kWriteAll = 0xF9U;

// The driver's arithmetic shift right (sar) of a signed value.
constexpr int asr(int value, int shift) noexcept {
    return value >> shift;  // NOLINT(bugprone-signed-bitwise): an arithmetic shift is what the driver does
}

// (a * b * 2) >> 8, then + 1 unless 0 (1B7C: "mul, shl, cmp 1, sbb 0xff").
std::uint8_t scale(std::uint8_t a, std::uint8_t b) noexcept {
    const auto v = u8(((unsigned{a} * b) << 1U) >> 8U);
    return v == 0 ? u8(0) : u8(v + 1U);
}

std::uint8_t byte_at(std::span<const std::byte> data, std::size_t at) noexcept {
    return at < data.size() ? std::to_integer<std::uint8_t>(data[at]) : u8(0);
}

}  // namespace

OplSynth::OplSynth(const assets::OplDriverTables& tables, OplSink& sink) : tables_{&tables}, sink_{&sink} {
    // 1582: OPL3 mode, 2-operator channels, then every register's start value.
    sink_->write(0x105, 1);
    sink_->write(0x104, 0);
    for (std::uint16_t reg = 0x001; reg <= 0x0F5; ++reg) {
        sink_->write(reg, tables_->init_values.at(reg));
    }
    for (std::uint16_t reg = 0x101; reg <= 0x1F5; ++reg) {
        sink_->write(reg, tables_->init_values.at(reg));
    }
    // 19BE
    channel_timbre_.fill(-1);
    drum_timbre_.fill(-1);
    opl_owner_.fill(kFree);
}

// --- Timbres ---

int OplSynth::find_timbre(std::uint8_t bank, std::uint8_t patch) const noexcept {  // 15E9
    for (std::size_t i = 0; i < cache_.size(); ++i) {
        if (cache_[i].bank == bank && cache_[i].patch == patch) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool OplSynth::has_timbre(std::uint8_t bank, std::uint8_t patch) const noexcept {
    return find_timbre(bank, patch) >= 0;
}

bool OplSynth::install_timbre(std::uint8_t bank, std::uint8_t patch, std::span<const std::byte> data) {  // 18DB
    if (has_timbre(bank, patch)) {
        return true;
    }
    if (cache_bytes_ + data.size() > kCacheBytes) {
        return false;
    }
    cache_bytes_ += data.size();
    cache_.push_back(CachedTimbre{.bank = bank, .patch = patch, .data = {data.begin(), data.end()}});
    return true;
}

// --- The voice layer (2629) ---

void OplSynth::send(std::uint8_t status, std::uint8_t d1, std::uint8_t d2) {
    const std::uint8_t ch = status & 0x0FU;
    switch (status & 0xF0U) {
        case 0x90:
            if (ch < 1 || ch > 9) {
                return;
            }
            if (d2 == 0) {
                note_off(ch, d1);
            } else {
                note_on(ch, d1, d2);
            }
            return;
        case 0x80:
            note_off(ch, d1);
            return;
        case 0xE0:
            bend_lsb_.at(ch) = d1;
            bend_msb_.at(ch) = d2;
            update_channel(ch, u8(kWriteFrequency));
            return;
        case 0xC0:
            program_.at(ch) = d1;
            channel_timbre_.at(ch) = find_timbre(bank_.at(ch), d1);
            return;
        case 0xB0:
            break;
        default:
            return;
    }
    switch (d1) {
        case 1:
            modulation_.at(ch) = d2;
            update_channel(ch, u8(kWriteMultiple));
            return;
        case 7:
            volume_.at(ch) = d2;
            update_channel(ch, u8(kWriteLevel));
            return;
        case 10:
            pan_.at(ch) = d2;
            update_channel(ch, u8(kWriteConnection));
            return;
        case 11:
            expression_.at(ch) = d2;
            update_channel(ch, u8(kWriteLevel));
            return;
        case 64:
            sustain_.at(ch) = d2;
            if (d2 < 64) {
                sustain_off(ch);
            }
            return;
        case 112:
            priority_lock_.at(ch) = d2;
            return;
        case 114:
            bank_.at(ch) = d2;
            return;
        case 121:  // reset the controllers
            sustain_.at(ch) = 0;
            sustain_off(ch);
            modulation_.at(ch) = 0;
            expression_.at(ch) = 127;
            bend_lsb_.at(ch) = 0;
            bend_msb_.at(ch) = 0x40;
            update_channel(ch, u8(kWriteMultiple | kWriteLevel | kWriteFrequency));
            return;
        case 123:  // all notes off
            for (const Voice& v : voices_) {
                if (v.state == 1 && v.midi_channel == ch) {
                    note_off(ch, v.note);
                }
            }
            return;
        default:
            return;  // 113 (timbre protection) only matters to the cache's eviction
    }
}

void OplSynth::update_channel(std::uint8_t channel, std::uint8_t flag) {  // 272D
    for (int slot = 0; slot < kVoices; ++slot) {
        Voice& v = voices_.at(static_cast<std::size_t>(slot));
        if (v.state != 0 && v.midi_channel == channel) {
            v.pending = u8(unsigned{v.pending} | flag);
            update(slot);
        }
    }
}

void OplSynth::note_on(std::uint8_t channel, std::uint8_t key, std::uint8_t velocity) {  // 24E4
    int timbre = channel_timbre_.at(channel);
    if (channel == kPercussion) {
        timbre = drum_timbre_.at(key);
        if (timbre < 0) {
            timbre = find_timbre(127, key);
            drum_timbre_.at(key) = timbre;
        }
    }
    if (timbre < 0) {
        return;
    }
    int slot = -1;
    for (int i = 0; i < kVoices; ++i) {
        if (voices_.at(static_cast<std::size_t>(i)).state == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return;
    }
    const std::span<const std::byte> t = cache_.at(static_cast<std::size_t>(timbre)).data;
    Voice& v = voices_.at(static_cast<std::size_t>(slot));
    v.midi_channel = channel;
    v.key = key;
    const std::uint8_t byte2 = byte_at(t, 2);
    if (channel == kPercussion) {
        v.note = byte2;
        v.transpose = 0;
    } else {
        v.note = key;
        v.transpose = static_cast<std::int8_t>(byte2);
    }
    v.velocity = tables_->velocity.at(velocity >> 3U);
    v.timbre = timbre;
    v.state = 1;
    v.sustained = false;
    const unsigned size = byte_at(t, 0) | (unsigned{byte_at(t, 1)} << 8U);
    if (size == 25) {
        setup_four_op(v, t);
    } else if (size == 14) {
        setup_two_op(v, t);
    } else {
        return;
    }
    v.opl_channel = kFree;
    assign_channel(slot);
}

void OplSynth::note_off(std::uint8_t channel, std::uint8_t key) {  // 2477
    for (int slot = 0; slot < kVoices; ++slot) {
        Voice& v = voices_.at(static_cast<std::size_t>(slot));
        if (v.state != 1 || v.key != key || v.midi_channel != channel) {
            continue;
        }
        if (sustain_.at(channel) >= 64) {
            v.sustained = true;
            continue;
        }
        release_voice(slot);
        v.state = 0;
    }
}

void OplSynth::sustain_off(std::uint8_t channel) {  // 25EE
    for (const Voice& v : voices_) {
        if (v.state != 0 && v.midi_channel == channel && v.sustained) {
            note_off(channel, v.note);
        }
    }
}

void OplSynth::setup_two_op(Voice& v, std::span<const std::byte> t) {  // 2384
    const auto b = [t](std::size_t i) { return byte_at(t, i); };
    v.key_on = 0x20;
    v.kind = 0;
    v.priority = 0x7FFF;
    v.connection = b(8) & 0x01U;
    v.feedback = static_cast<std::uint16_t>((b(8) & 0x0FU) << 12U);
    const auto setup = [&v](std::size_t op, std::uint8_t character, std::uint8_t scaling, std::uint8_t ad,
                            std::uint8_t sr) {
        v.flags.at(op) = character & 0xF0U;
        v.multiple.at(op) = static_cast<std::uint16_t>((character & 0x0FU) << 12U);
        v.ksl.at(op) = scaling & 0xC0U;
        v.level.at(op) = static_cast<std::uint16_t>((63U - (scaling & 0x3FU)) << 10U);
        v.attack_decay.at(op) = ad;
        v.sustain_release.at(op) = sr;
    };
    setup(0, b(3), b(4), b(5), b(6));
    setup(1, b(9), b(10), b(11), b(12));
    v.waveform.at(0) = b(7);
    v.waveform.at(1) = b(13);
    v.scaled1 = u8(v.connection | 2U);
    v.pending = u8(kWriteAll);
}

void OplSynth::setup_four_op(Voice& v, std::span<const std::byte> t) {  // 2299
    setup_two_op(v, t);
    const auto b = [t](std::size_t i) { return byte_at(t, i); };
    v.kind = 3;
    v.connection = u8(v.connection | ((b(8) & 0x80U) >> 6U));
    v.scaled1 = tables_->scaled_pair1.at(v.connection);
    v.scaled2 = tables_->scaled_pair2.at(v.connection);
    const auto setup = [&v](std::size_t op, std::uint8_t character, std::uint8_t scaling, std::uint8_t ad,
                            std::uint8_t sr) {
        v.flags.at(op) = character & 0xF0U;
        v.multiple.at(op) = static_cast<std::uint16_t>((character & 0x0FU) << 12U);
        v.ksl.at(op) = scaling & 0xC0U;
        v.level.at(op) = static_cast<std::uint16_t>((63U - (scaling & 0x3FU)) << 10U);
        v.attack_decay.at(op) = ad;
        v.sustain_release.at(op) = sr;
    };
    setup(2, b(14), b(15), b(16), b(17));
    setup(3, b(20), b(21), b(22), b(23));
    v.waveform.at(2) = b(18);
    v.waveform.at(3) = b(24);
}

void OplSynth::assign_channel(int slot) {  // 1A5A
    Voice& v = voices_.at(static_cast<std::size_t>(slot));
    if (v.kind != 3) {
        for (int tries = 0; tries < kOplChannels; ++tries) {
            last_channel_ = (last_channel_ + 1) % kOplChannels;
            if (opl_owner_.at(static_cast<std::size_t>(last_channel_)) == kFree) {
                v.opl_channel = u8(last_channel_);
                ++voices_on_.at(v.midi_channel);
                opl_owner_.at(static_cast<std::size_t>(last_channel_)) = v.midi_channel;
                v.pending = u8(kWriteAll);
                update(slot);
                return;
            }
        }
    } else {
        for (int tries = 0; tries < 6; ++tries) {
            last_pair_ = (last_pair_ + 1) % 6;
            const std::uint8_t ch = tables_->four_op_channels.at(static_cast<std::size_t>(last_pair_));
            if (opl_owner_.at(ch) == kFree && opl_owner_.at(ch + 3U) == kFree) {
                v.opl_channel = ch;
                ++voices_on_.at(v.midi_channel);
                opl_owner_.at(ch) = v.midi_channel;
                opl_owner_.at(ch + 3U) = v.midi_channel;
                v.pending = u8(kWriteAll);
                update(slot);
                return;
            }
        }
    }
    steal(slot);
}

void OplSynth::steal(int /*waiting*/) {  // 2155
    int active = 0;
    for (Voice& v : voices_) {
        if (v.state == 0) {
            continue;
        }
        ++active;
        unsigned p = priority_lock_.at(v.midi_channel) >= 64 ? 0xFFFFU : v.priority;
        p = p >= voices_on_.at(v.midi_channel) ? p - voices_on_.at(v.midi_channel) : 0U;
        v.steal_priority = static_cast<std::uint16_t>(p);
    }
    while (active > 0) {
        unsigned highest_waiting = 0;
        unsigned lowest = 0xFFFF;
        unsigned lowest_four = 0xFFFF;
        int waiting = -1;
        int victim = -1;
        int victim_four = 0;
        for (int i = 0; i < kVoices; ++i) {
            const Voice& v = voices_.at(static_cast<std::size_t>(i));
            if (v.state == 0) {
                continue;
            }
            const unsigned p = v.steal_priority;
            if (v.opl_channel == kFree) {
                if (p >= highest_waiting) {
                    highest_waiting = p;
                    waiting = i;
                }
                continue;
            }
            if (tables_->four_op_capable.at(v.opl_channel) != 0 && p <= lowest_four) {
                lowest_four = p;
                victim_four = i;
            }
            if (p <= lowest) {
                lowest = p;
                victim = i;
            }
        }
        if (highest_waiting < lowest || highest_waiting == 0 || waiting < 0 || victim < 0) {
            return;
        }
        Voice& w = voices_.at(static_cast<std::size_t>(waiting));
        if (w.kind == 3) {
            victim = victim_four;
            if (voices_.at(static_cast<std::size_t>(victim)).kind != 3) {
                // The partner channel's voice goes too. The driver looks the partner up by the slot's number, not
                // its channel's (05E9 + slot; slots 18 and 19 read the next table, 05FB).
                const auto at = static_cast<std::size_t>(victim);
                const std::uint8_t partner = at < 18 ? tables_->partner.at(at) : tables_->partner_first.at(at - 18);
                for (int i = 0; i < kVoices; ++i) {
                    if (voices_.at(static_cast<std::size_t>(i)).state != 0 &&
                        voices_.at(static_cast<std::size_t>(i)).opl_channel == partner) {
                        release_voice(i);
                        break;
                    }
                }
            }
        }
        // The partner looked up by the slot's number can be the victim's own channel: then the victim was just
        // released, and the driver goes on with its channel as it is now, 0xFF. The waiting slot gets no channel
        // and stays waiting (1B7C returns for it), yet its MIDI channel's voice count goes up, and stays one too
        // high. The driver's two owner bytes for "channel 0xFF" land in its own detection code (cs:1448, 144B),
        // which ran at the start: nothing more comes of them.
        const std::uint8_t channel = voices_.at(static_cast<std::size_t>(victim)).opl_channel;
        release_voice(victim);
        w.opl_channel = channel;
        ++voices_on_.at(w.midi_channel);
        if (channel != kFree) {
            opl_owner_.at(channel) = w.midi_channel;
            if (w.kind == 3) {
                opl_owner_.at(channel + 3U) = w.midi_channel;
            }
        }
        w.pending = u8(kWriteAll);
        update(waiting);
        --active;
    }
}

void OplSynth::release_voice(int slot) {  // 1B11
    Voice& v = voices_.at(static_cast<std::size_t>(slot));
    if (v.opl_channel == kFree) {
        return;
    }
    v.key_on = u8(v.key_on & 0xDFU);
    v.pending = u8(unsigned{v.pending} | kWriteFrequency);
    update(slot);
    --voices_on_.at(v.midi_channel);
    if (v.kind == 3) {
        opl_owner_.at(v.opl_channel + 3U) = kFree;
    }
    opl_owner_.at(v.opl_channel) = kFree;
    v.opl_channel = kFree;
    if (v.kind == 0 || v.kind == 3) {
        v.state = 0;
    }
}

void OplSynth::update(int slot) {  // 1B7C
    Voice& v = voices_.at(static_cast<std::size_t>(slot));
    if (v.opl_channel == kFree) {
        return;
    }
    std::uint8_t volume = 0;
    if ((unsigned{v.pending} & kWriteLevel) != 0) {
        volume = scale(scale(volume_.at(v.midi_channel), expression_.at(v.midi_channel)), v.velocity);
    }
    const std::uint8_t ch = v.opl_channel;
    const std::uint8_t bit = tables_->four_op_bit.at(ch);
    if (v.kind == 3) {
        const auto reg = u8(unsigned{four_op_register_} | bit);
        if (reg != four_op_register_) {
            four_op_register_ = reg;
            sink_->write(0x104, reg);
        }
        const std::uint8_t pending = v.pending;
        write_pair(v, 2, u8(ch + 3U), volume);
        v.pending = pending;
    } else {
        const auto reg = u8(unsigned{four_op_register_} & ~unsigned{bit});
        if (reg != four_op_register_) {
            four_op_register_ = reg;
            sink_->write(0x104, reg);
            write_operator(tables_->partner_first.at(ch), 0x80, 0x0F);
            write_operator(tables_->partner_second.at(ch), 0x80, 0x0F);
            write_channel(tables_->partner.at(ch), 0xB0, 0);
        }
    }
    write_pair(v, 0, ch, volume);
}

// One operator pair (0: the first, 2: the second of a 4-operator voice) in the order of 1D34: 20, 40, 60/80, E0,
// C0, then (first pair only) the frequency.
void OplSynth::write_pair(Voice& v, int pair, std::uint8_t ch, std::uint8_t volume) {
    const auto first = static_cast<std::size_t>(pair);
    const std::uint8_t op_a = tables_->first_operator.at(ch);
    const std::uint8_t op_b = tables_->second_operator.at(ch);
    if ((unsigned{v.pending} & kWriteMultiple) != 0) {
        const unsigned vibrato = modulation_.at(v.midi_channel) >= 64 ? 0x40U : 0U;
        write_operator(op_a, 0x20, u8((unsigned{v.multiple.at(first)} >> 12U) | vibrato | v.flags.at(first)));
        write_operator(op_b, 0x20, u8((unsigned{v.multiple.at(first + 1)} >> 12U) | vibrato | v.flags.at(first + 1)));
        v.pending = u8(unsigned{v.pending} & ~kWriteMultiple);
    }
    if ((unsigned{v.pending} & kWriteLevel) != 0) {
        const std::uint8_t scaled = pair == 0 ? v.scaled1 : v.scaled2;
        const auto level = [&](std::size_t op, unsigned mask) {
            auto l = u8(v.level.at(op) >> 10U);
            if ((scaled & mask) != 0) {
                l = u8((unsigned{l} * volume) / 127U);
            }
            return u8((~unsigned{l} & 0x3FU) | v.ksl.at(op));
        };
        write_operator(op_a, 0x40, level(first, 1));
        write_operator(op_b, 0x40, level(first + 1, 2));
        v.pending = u8(unsigned{v.pending} & ~kWriteLevel);
    }
    if ((unsigned{v.pending} & kWriteEnvelope) != 0) {
        write_operator(op_a, 0x60, v.attack_decay.at(first));
        write_operator(op_b, 0x60, v.attack_decay.at(first + 1));
        write_operator(op_a, 0x80, v.sustain_release.at(first));
        write_operator(op_b, 0x80, v.sustain_release.at(first + 1));
        v.pending = u8(unsigned{v.pending} & ~kWriteEnvelope);
    }
    if ((unsigned{v.pending} & kWriteWaveform) != 0) {
        write_operator(op_b, 0xE0, v.waveform.at(first + 1));
        write_operator(op_a, 0xE0, v.waveform.at(first));
        v.pending = u8(unsigned{v.pending} & ~kWriteWaveform);
    }
    if ((unsigned{v.pending} & kWriteConnection) != 0) {
        const std::uint8_t feedback = pair == 0 ? u8((unsigned{v.feedback} >> 12U) & 0x0EU) : u8(0);
        const std::uint8_t connection =
            pair == 0 ? u8(unsigned{v.connection} & 1U) : u8((unsigned{v.connection} >> 1U) & 1U);
        auto value = u8(unsigned{connection} | feedback | 0x30U);
        const std::uint8_t pan = pan_.at(v.midi_channel);
        if (pan <= 27) {
            value = u8(value & 0xEFU);
        } else if (pan >= 100) {
            value = u8(value & 0xDFU);
        }
        write_channel(ch, 0xC0, value);
        v.pending = u8(unsigned{v.pending} & ~kWriteConnection);
    }
    if ((unsigned{v.pending} & kWriteFrequency) != 0) {
        if (pair == 0) {
            write_frequency(v, ch);
        }
        v.pending = u8(unsigned{v.pending} & ~kWriteFrequency);
    }
}

void OplSynth::write_frequency(Voice& v, std::uint8_t ch) {  // 2011
    if ((v.key_on & 0x20U) == 0) {
        write_channel(ch, 0xB0, u8(v.last_b0 & 0xDFU));
        return;
    }
    const auto c = v.midi_channel;
    const int bend = static_cast<std::int16_t>(((unsigned{bend_msb_.at(c)} << 7U) | bend_lsb_.at(c)) - 0x2000U);
    auto pitch = static_cast<std::int16_t>(asr(bend, 5) * 12);
    int note = v.note + v.transpose - 24;
    do {
        note += 12;
    } while (note < 0);
    note += 12;
    do {
        note -= 12;
    } while (note > 95);
    pitch = static_cast<std::int16_t>(pitch + (note * 256));
    int sixteenths = asr(pitch + 8, 4);
    sixteenths -= 192;
    do {
        sixteenths += 192;
    } while (sixteenths < 0);
    sixteenths += 192;
    do {
        sixteenths -= 192;
    } while (sixteenths > 0x5FF);
    const auto semitone = static_cast<std::size_t>(sixteenths / 16);  // 0-0x5FF here
    const std::size_t index =
        (std::size_t{tables_->note_in_octave.at(semitone)} * 16U) + static_cast<std::size_t>(sixteenths % 16);
    auto fnumber = static_cast<std::int16_t>(tables_->fnumbers.at(index));
    int block = tables_->blocks.at(semitone) - 1;
    if (fnumber < 0) {
        ++block;
    }
    if (block < 0) {
        block = 0;
        fnumber = static_cast<std::int16_t>(asr(fnumber, 1));
    }
    const unsigned bits = static_cast<std::uint16_t>(fnumber);
    write_channel(ch, 0xA0, u8(bits & 0xFFU));
    v.last_b0 = u8(((bits >> 8U) & 0x03U) | (static_cast<unsigned>(block) * 4U) | v.key_on);
    write_channel(ch, 0xB0, v.last_b0);
}

void OplSynth::write_operator(std::uint8_t op, std::uint8_t base, std::uint8_t value) {  // 13A8
    sink_->write(static_cast<std::uint16_t>((tables_->operator_chip.at(op) * 0x100U) +
                                            u8(tables_->operator_offset.at(op) + base)),
                 value);
}

void OplSynth::write_channel(std::uint8_t channel, std::uint8_t base, std::uint8_t value) {  // 13C4
    sink_->write(static_cast<std::uint16_t>((tables_->channel_chip.at(channel) * 0x100U) +
                                            u8(tables_->channel_offset.at(channel) + base)),
                 value);
}

// --- The chip ---

struct Opl3::Chip {
    ymfm::ymfm_interface interface;
    ymfm::ymf262 chip{interface};
};

Opl3::Opl3() : chip_{std::make_unique<Chip>()}, rate_{chip_->chip.sample_rate(kClock)} {
    chip_->chip.reset();
}

Opl3::~Opl3() = default;

void Opl3::write(std::uint16_t reg, std::uint8_t value) {
    if ((reg & 0x100U) != 0) {
        chip_->chip.write_address_hi(static_cast<std::uint8_t>(reg & 0xFFU));
    } else {
        chip_->chip.write_address(static_cast<std::uint8_t>(reg & 0xFFU));
    }
    chip_->chip.write_data(value);
}

void Opl3::render(double seconds, std::vector<std::int16_t>& out) {
    pending_ += seconds * rate_;
    const auto samples = static_cast<std::uint32_t>(pending_);
    pending_ -= samples;
    ymfm::ymf262::output_data frame{};
    for (std::uint32_t i = 0; i < samples; ++i) {
        chip_->chip.generate(&frame, 1);
        for (const int channel : {0, 1}) {  // outputs A (left) and B (right); C and D aren't wired on the SB Pro 2
            out.push_back(static_cast<std::int16_t>(std::clamp(frame.data[channel], -32768, 32767)));
        }
    }
}

}  // namespace lvr::audio
