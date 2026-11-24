#include "audio/sequencer.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace lvr::audio {
namespace {

template <typename T>
constexpr std::uint8_t u8(T value) noexcept {
    return static_cast<std::uint8_t>(value);
}

constexpr std::uint8_t kFree = 0xFF;

std::uint8_t byte_at(std::span<const std::byte> data, std::size_t at) noexcept {
    return at < data.size() ? std::to_integer<std::uint8_t>(data[at]) : u8(0);
}

}  // namespace

Sequencer::Sequencer(const assets::SequencerTables& tables, MidiOut& out, int start_volume)
    : tables_{&tables}, out_{&out}, start_volume_{start_volume} {
    // 35D8: the driver-wide shadows unknown, then MIDI channels 2-10 get the default controllers, a centred bend and
    // their default program (463 waits 10 refreshes after the controllers and after the programs: 0787).
    shadow_controllers_.fill(kFree);
    shadow_program_.fill(kFree);
    shadow_bend_lsb_.fill(kFree);
    shadow_bend_msb_.fill(kFree);
    for (std::size_t i = 0; i < tables_->controllers.size(); ++i) {
        const std::uint8_t value = tables_->controller_defaults.at(i);
        if (value == kFree) {
            continue;
        }
        for (std::uint8_t ch = 1; ch <= 9; ++ch) {
            shadow_controllers_.at((i * 16) + ch) = value;
            send(u8(0xB0U | ch), tables_->controllers.at(i), value);
        }
    }
    for (std::uint8_t ch = 1; ch <= 9; ++ch) {
        shadow_bend_lsb_.at(ch) = 0;
        shadow_bend_msb_.at(ch) = 0x40;
        send(u8(0xE0U | ch), 0, 0x40);
        const std::uint8_t program = tables_->default_programs.at(ch);
        if (program != kFree) {
            shadow_program_.at(ch) = program;
            send(u8(0xC0U | ch), program, 0);
        }
    }
}

// --- Sequences ---

int Sequencer::register_sequence(std::span<const std::byte> stream) {  // 37F6
    for (int h = 0; h < kSequences; ++h) {
        Sequence& s = sequences_.at(static_cast<std::size_t>(h));
        if (!s.registered) {
            s = Sequence{};
            s.registered = true;
            s.stream = stream;
            return h;
        }
    }
    return -1;
}

int Sequencer::status(int handle) const noexcept {
    if (handle < 0 || handle >= kSequences) {
        return -1;
    }
    const Sequence& s = sequences_[static_cast<std::size_t>(handle)];
    return s.registered ? s.state : -1;
}

void Sequencer::start(int handle, bool ring_out) {  // 3980
    if (status(handle) < 0) {
        return;
    }
    Sequence& s = sequences_.at(static_cast<std::size_t>(handle));
    if (s.state == 1) {
        stop(handle);
    }
    reset(s);
    s.position = 0;
    s.state = 1;
    s.started = true;
    s.ring_out = ring_out;
}

void Sequencer::stop(int handle) {  // 3A15
    if (status(handle) != 1) {
        return;
    }
    Sequence& s = sequences_.at(static_cast<std::size_t>(handle));
    notes_off(s);
    end_sequence(s);
    s.state = 0;
}

void Sequencer::release(int handle) {  // 393C
    if (status(handle) < 0) {
        return;
    }
    Sequence& s = sequences_.at(static_cast<std::size_t>(handle));
    if (s.state == 1) {
        s.release_when_done = true;
    } else {
        s.registered = false;
    }
}

void Sequencer::set_volume(int handle, int target, int milliseconds) {  // 3CF1
    if (status(handle) < 0) {
        return;
    }
    Sequence& s = sequences_.at(static_cast<std::size_t>(handle));
    s.volume_target = target;
    if (milliseconds == 0) {
        s.volume = target;
        apply_volume(s);
        return;
    }
    const int distance = std::abs(s.volume_target - s.volume);
    if (distance == 0) {
        return;
    }
    const std::int32_t period = (milliseconds * 10) / distance;
    s.volume_period = period == 0 ? 1 : period;
    s.volume_time = 0;
}

void Sequencer::reset(Sequence& s) {  // 2B67
    s.loops.fill(Loop{});
    for (std::uint8_t ch = 0; ch < 16; ++ch) {
        s.map.at(ch) = ch;
    }
    s.program.fill(kFree);
    s.bend_lsb.fill(kFree);
    s.bend_msb.fill(kFree);
    s.remembered.fill(kFree);
    s.notes.fill(NoteSlot{});
    s.delay = 0;
    s.active_notes = 0;
    s.volume = s.volume_target = start_volume_;
    s.rate = s.rate_target = 100;
    s.rate_accumulator = 0;
}

// --- The service (331E) ---

void Sequencer::service() {
    for (Sequence& s : sequences_) {
        if (!s.registered || s.state != 1) {
            continue;
        }
        s.rate_accumulator += s.rate;
        while (s.rate_accumulator >= 100 && s.state == 1) {
            s.rate_accumulator -= 100;
            play_tick(s);
        }
        if (s.state != 1) {
            continue;
        }
        const auto ramp = [](int& value, int target, std::int32_t& time, std::int32_t period) {
            time += 83;
            int steps = 0;
            while (time - period >= 0) {
                time -= period;
                ++steps;
            }
            if (steps == 0) {
                return false;
            }
            value = value < target ? std::min(value + steps, target) : std::max(value - steps, target);
            return true;
        };
        if (s.rate != s.rate_target) {  // 3543
            (void)ramp(s.rate, s.rate_target, s.rate_time, s.rate_period);
        }
        if (s.volume != s.volume_target) {  // 3589
            if (ramp(s.volume, s.volume_target, s.volume_time, s.volume_period)) {
                apply_volume(s);
            }
        }
    }
}

namespace {

constexpr std::int32_t kUntilTheEnd = 0x7fffffff;

// Whether a General MIDI instrument's sound dies away by itself while its note is held: the percussion channel,
// the pianos and chromatic percussion (programs 0-15), the guitars (24-31), pizzicato, harp and timpani (45-47),
// the percussive family (112-119) and the gunshot (127). The others sound for as long as the note is held.
constexpr bool dies_away(std::uint8_t channel, std::uint8_t program) noexcept {
    constexpr std::uint8_t kPercussionChannel = 9;
    return channel == kPercussionChannel || program <= 15 || (program >= 24 && program <= 31) ||
           (program >= 45 && program <= 47) || (program >= 112 && program <= 119) || program == 127;
}

}  // namespace

// Whether the sequence still has a note to start before it ends (or a loop, which may bring one).
bool Sequencer::notes_to_come(const Sequence& s) const {
    constexpr std::uint8_t kLoopStart = 116;
    constexpr std::uint8_t kLoopEnd = 117;
    std::size_t at = s.position;
    while (at < s.stream.size()) {
        const std::uint8_t status = byte_at(s.stream, at);
        if (status < 0x80) {  // a delay
            ++at;
            continue;
        }
        const std::uint8_t kind = status & 0xF0U;
        if (kind == 0x90) {
            return true;
        }
        if (kind == 0xB0) {
            const std::uint8_t controller = byte_at(s.stream, at + 1);
            if (controller == kLoopStart || controller == kLoopEnd) {
                return true;
            }
        }
        if (kind == 0xF0) {
            if (status != 0xFF || byte_at(s.stream, at + 1) == 0x2F) {
                return false;  // the end of the track
            }
            at += 2;
            std::uint32_t length = 0;
            for (;;) {
                const std::uint8_t b = byte_at(s.stream, at++);
                length = (length << 7U) | (b & 0x7FU);
                if ((b & 0x80U) == 0) {
                    break;
                }
            }
            at += length;
            continue;
        }
        at += kind == 0xC0 || kind == 0xD0 ? 2 : 3;
    }
    return false;
}

void Sequencer::play_tick(Sequence& s) {
    // 33EE: the note durations, the expired ones off.
    if (s.active_notes != 0) {
        for (NoteSlot& n : s.notes) {
            if (n.channel == kFree) {
                continue;
            }
            if (--n.ticks_left >= 0) {
                continue;
            }
            if (s.ring_out && dies_away(n.channel, s.program.at(n.channel)) && !notes_to_come(s)) {
                n.ticks_left = kUntilTheEnd;  // let go when the sequence ends (meta_event)
                continue;
            }
            const std::uint8_t channel = std::exchange(n.channel, kFree);
            const std::uint8_t mapped = s.map.at(channel);
            --channel_notes_.at(mapped);
            send(u8(0x80U | mapped), n.key, 0);
            if (--s.active_notes == 0) {
                break;
            }
        }
    }
    if (--s.delay <= 0) {
        read_events(s);
    }
}

void Sequencer::read_events(Sequence& s) {  // 3479
    const auto at = [&s](std::size_t i) { return byte_at(s.stream, s.position + i); };
    while (s.state == 1) {
        if (s.position >= s.stream.size()) {
            end_sequence(s);  // a stream without an end of track (never in the game's data)
            s.state = 2;
            return;
        }
        const std::uint8_t status = at(0);
        if (status < 0x80) {
            s.position += 1;
            s.delay = status;
            return;
        }
        const std::uint8_t kind = status & 0xF0U;
        const std::uint8_t channel = status & 0x0FU;
        const std::uint8_t d1 = at(1);
        const std::uint8_t d2 = at(2);
        std::size_t length = 3;
        if (kind == 0xF0) {
            if (status != 0xFF) {
                end_sequence(s);  // SysEx: never in the game's data
                s.state = 2;
                return;
            }
            length = meta_event(s);
        } else if (kind == 0xE0) {
            s.bend_lsb.at(channel) = d1;
            s.bend_msb.at(channel) = d2;
            shadow_bend_lsb_.at(channel) = d1;
            shadow_bend_msb_.at(channel) = d2;
        } else if (kind == 0xC0 || kind == 0xD0) {
            length = 2;
            if (kind == 0xC0) {
                s.program.at(channel) = d1;
                shadow_program_.at(channel) = d1;
            }
        } else if (kind == 0xB0) {
            controller_event(s, channel, d1, d2);
            s.position += 3;
            continue;
        } else if (kind != 0xA0) {
            length = note_on_event(s, status);
            s.position += length;
            continue;
        }
        if (kind != 0xF0 && (channel_lock_.at(channel) & 0x80U) == 0) {  // 351F
            send(u8(kind | s.map.at(channel)), d1, d2);
        }
        s.position += length;
    }
}

std::size_t Sequencer::note_on_event(Sequence& s, std::uint8_t status) {  // 30CD
    const std::uint8_t channel = status & 0x0FU;
    const std::uint8_t key = byte_at(s.stream, s.position + 1);
    const std::uint8_t velocity = byte_at(s.stream, s.position + 2);
    std::size_t at = s.position + 3;
    std::uint32_t duration = 0;
    for (;;) {
        const std::uint8_t b = byte_at(s.stream, at++);
        duration = (duration << 7U) | (b & 0x7FU);
        if ((b & 0x80U) == 0) {
            break;
        }
    }
    const std::size_t length = at - s.position;
    if ((channel_lock_.at(channel) & 0x80U) != 0) {
        return length;
    }
    // The first free note slot; with none, slot 0 is taken over (its note never gets its note off).
    std::size_t slot = 0;
    for (std::size_t i = 0; i < s.notes.size(); ++i) {
        if (s.notes.at(i).channel == kFree) {
            slot = i;
            ++s.active_notes;
            break;
        }
    }
    s.notes.at(slot) = NoteSlot{.channel = channel, .key = key, .ticks_left = static_cast<std::int32_t>(duration) - 1};
    const std::uint8_t mapped = s.map.at(channel);
    ++channel_notes_.at(mapped);
    send(u8(0x90U | mapped), key, velocity);
    return length;
}

std::size_t Sequencer::meta_event(Sequence& s) {  // 3185
    const std::uint8_t type = byte_at(s.stream, s.position + 1);
    std::size_t at = s.position + 2;
    std::uint32_t length = 0;
    for (;;) {
        const std::uint8_t b = byte_at(s.stream, at++);
        length = (length << 7U) | (b & 0x7FU);
        if ((b & 0x80U) == 0) {
            break;
        }
    }
    if (type == 0x2F) {  // end of track
        if (s.ring_out) {
            notes_off(s);  // the ones left ringing
        }
        end_sequence(s);
        s.state = 2;
        if (s.release_when_done) {
            s.registered = false;
        }
    }
    // 0x58 (time signature) and 0x51 (tempo) only feed the beat counter.
    return (at - s.position) + length;
}

void Sequencer::controller_event(Sequence& s, std::uint8_t channel, std::uint8_t controller, std::uint8_t value) {
    for (std::size_t i = 0; i < tables_->controllers.size(); ++i) {  // 282F: the remembered ones
        if (tables_->controllers.at(i) == controller) {
            shadow_controllers_.at((i * 16) + channel) = value;
            s.remembered.at((i * 16) + channel) = value;
        }
    }
    switch (controller) {
        case 7:
            if (s.volume != 100) {
                value = u8(std::min((unsigned{value} * static_cast<unsigned>(s.volume)) / 100U, 127U));
                shadow_controllers_.at(channel) = value;
            }
            break;
        case 116: {  // FOR
            for (Loop& loop : s.loops) {
                if (loop.count == -1) {
                    loop.count = value;
                    loop.position = s.position;
                    break;
                }
            }
            return;
        }
        case 117: {  // NEXT
            if (value < 64) {
                return;
            }
            for (auto loop = s.loops.rbegin(); loop != s.loops.rend(); ++loop) {
                if (loop->count == -1) {
                    continue;
                }
                if (loop->count != 0) {
                    --loop->count;
                    if (loop->count == 0) {
                        loop->count = -1;
                        return;
                    }
                }
                s.position = loop->position;
                return;
            }
            return;
        }
        case 111:  // lock protection
            channel_lock_.at(channel) =
                u8(value >= 64 ? channel_lock_.at(channel) | 0x40U : channel_lock_.at(channel) & ~0x40U);
            return;
        case 110:  // channel lock
            if (value >= 64) {
                const int locked = lock_channel();
                s.map.at(channel) = locked == 0 ? channel : u8(locked - 1);
            } else {
                release_channel_notes(channel, s.ring_out ? &s : nullptr);
                unlock_channel(s.map.at(channel), s.ring_out);
                s.map.at(channel) = channel;
            }
            return;
        case 115:
        case 118:
        case 119:
            return;
        default:
            break;
    }
    if ((channel_lock_.at(channel) & 0x80U) == 0) {
        send(u8(0xB0U | s.map.at(channel)), controller, value);
    }
}

// 3E20: the channel among 2-9 (0-based 1-8) with the fewest notes that no one has locked or protected (protected
// ones only if none else), silenced and locked. Its number + 1, or 0.
int Sequencer::lock_channel() {
    for (const std::uint8_t mask : {std::uint8_t{0xC0}, std::uint8_t{0x80}}) {
        int chosen = -1;
        unsigned fewest = 0xFF;
        for (int ch = 8; ch >= 1; --ch) {
            const auto c = static_cast<std::size_t>(ch);
            if ((channel_lock_.at(c) & mask) != 0 || channel_notes_.at(c) >= fewest) {
                continue;
            }
            fewest = channel_notes_.at(c);
            chosen = ch;
        }
        if (chosen < 0) {
            continue;
        }
        const auto c = static_cast<std::uint8_t>(chosen);
        if (set_back_later_.at(c)) {  // what its last holder left undone
            set_back(c);
        }
        send(u8(0xB0U | c), 64, 0);
        release_channel_notes(c);
        channel_notes_.at(c) = 0;
        channel_lock_.at(c) = u8(channel_lock_.at(c) | 0x80U);
        return chosen + 1;
    }
    return 0;
}

// 3EA3: the lock undone, the channel silenced and given back what was last sent on it.
void Sequencer::unlock_channel(std::uint8_t channel, bool later) {
    if ((channel_lock_.at(channel) & 0x80U) == 0) {
        return;
    }
    channel_lock_.at(channel) = u8(channel_lock_.at(channel) & 0x7FU);
    channel_notes_.at(channel) = 0;
    if (later) {
        set_back_later_.at(channel) = true;
        return;
    }
    set_back(channel);
}

// A channel given back: the pedal up, its notes off, and the controllers, program and bend it had before.
void Sequencer::set_back(std::uint8_t channel) {
    set_back_later_.at(channel) = false;
    send(u8(0xB0U | channel), 64, 0);
    send(u8(0xB0U | channel), 123, 0);
    for (std::size_t i = 0; i < tables_->controllers.size(); ++i) {
        const std::uint8_t value = shadow_controllers_.at((i * 16) + channel);
        if (value != kFree) {
            send(u8(0xB0U | channel), tables_->controllers.at(i), value);
        }
    }
    if (shadow_program_.at(channel) != kFree) {
        send(u8(0xC0U | channel), shadow_program_.at(channel), 0);
    }
    if (shadow_bend_lsb_.at(channel) != kFree && shadow_bend_msb_.at(channel) != kFree) {
        send(u8(0xE0U | channel), shadow_bend_lsb_.at(channel), shadow_bend_msb_.at(channel));
    }
}

// 2C1E: in every sequence, the notes whose (own) channel is `channel` off.
void Sequencer::release_channel_notes(std::uint8_t channel, const Sequence* only) {
    for (Sequence& s : sequences_) {
        if (!s.registered || s.active_notes == 0 || (only != nullptr && &s != only)) {
            continue;
        }
        for (NoteSlot& n : s.notes) {
            if (n.channel != channel) {
                continue;
            }
            n.channel = kFree;
            const std::uint8_t mapped = s.map.at(channel);
            --channel_notes_.at(mapped);
            send(u8(0x80U | mapped), n.key, 0);
            --s.active_notes;
        }
    }
}

void Sequencer::notes_off(Sequence& s) {  // 2CA0
    for (NoteSlot& n : s.notes) {
        if (n.channel == kFree) {
            continue;
        }
        const std::uint8_t channel = std::exchange(n.channel, kFree);
        const std::uint8_t mapped = s.map.at(channel);
        --channel_notes_.at(mapped);
        send(u8(0x80U | mapped), n.key, 0);
    }
    s.active_notes = 0;
}

void Sequencer::end_sequence(Sequence& s) {  // 2CF4
    // The remembered controllers' indices (tables.controllers: 7, 1, 10, 11, 64, 114, 110, 111, 112).
    const auto remembered = [&s, this](std::uint8_t controller, std::uint8_t ch) -> std::uint8_t {
        for (std::size_t i = 0; i < tables_->controllers.size(); ++i) {
            if (tables_->controllers.at(i) == controller) {
                return s.remembered.at((i * 16) + ch);
            }
        }
        return kFree;
    };
    for (std::uint8_t ch = 0; ch < 16; ++ch) {
        const std::uint8_t sustain = remembered(64, ch);
        if (sustain != kFree && sustain >= 64) {
            shadow_controllers_.at((4 * 16) + ch) = 0;
            send(u8(0xB0U | ch), 64, 0);
        }
        const std::uint8_t lock = remembered(110, ch);
        if (lock != kFree && lock >= 64) {
            release_channel_notes(ch, s.ring_out ? &s : nullptr);
            unlock_channel(s.map.at(ch), s.ring_out);
            s.map.at(ch) = ch;
        }
        const std::uint8_t protect = remembered(111, ch);
        if (protect != kFree && protect >= 64) {
            channel_lock_.at(ch) = u8(channel_lock_.at(ch) & 0xBFU);
        }
        const std::uint8_t c112 = remembered(112, ch);
        if (c112 != kFree && c112 >= 64) {
            send(u8(0xB0U | ch), 112, 0);
        }
    }
}

void Sequencer::apply_volume(Sequence& s) {  // 2E8A
    for (std::uint8_t ch = 0; ch < 16; ++ch) {
        const std::uint8_t value = s.remembered.at(ch);  // controller 7, index 0
        if (value == kFree) {
            continue;
        }
        const auto scaled = u8(std::min((unsigned{value} * static_cast<unsigned>(s.volume)) / 100U, 127U));
        shadow_controllers_.at(ch) = scaled;
        if ((channel_lock_.at(ch) & 0x80U) == 0) {
            send(u8(0xB0U | s.map.at(ch)), 7, scaled);
        }
    }
}

}  // namespace lvr::audio
