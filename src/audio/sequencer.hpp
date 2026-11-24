#pragma once

// The XMIDI sequencer the Miles drivers share (docs/original/sound-driver.md): driver 460's and 463's are the same
// code. It plays up to 8 sequences one XMIDI tick per service call and sends MIDI messages to the driver's output:
// the OPL3 voice layer (Classic, opl3.hpp) or a General MIDI synthesizer (Modern, gm.hpp).
//
// Not reproduced: the beat/bar counter and callbacks, indirect controllers (115), RBRN branches.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "assets/audio.hpp"

namespace lvr::audio {

// Where the sequencer's MIDI messages go (460: 2629, 463: 08CE). Program changes and channel pressure have one data
// byte; `data2` is then 0.
class MidiOut {
  public:
    MidiOut() = default;
    MidiOut(const MidiOut&) = default;
    MidiOut& operator=(const MidiOut&) = default;
    MidiOut(MidiOut&&) = default;
    MidiOut& operator=(MidiOut&&) = default;
    virtual ~MidiOut() = default;
    virtual void send(std::uint8_t status, std::uint8_t data1, std::uint8_t data2) = 0;
};

class Sequencer {
  public:
    static constexpr int kSequences = 8;

    // fn 102 (35D8): the channels' default controllers, a centred bend and the default programs to `out`, which has
    // been initialised. `start_volume`: a sequence's volume (percent) when it starts, 100 in 460, 90 in 463 (2B67).
    Sequencer(const assets::SequencerTables& tables, MidiOut& out, int start_volume);

    // The timer service (fn 103): one call per driver interrupt, twice per display refresh in the game.
    void service();

    // fn 151: the first free handle, or -1. `stream` must outlive the registration.
    int register_sequence(std::span<const std::byte> stream);
    // fn 170. `ring_out` is ours, not the driver's (the Modern sound's effects): a note of an instrument that dies
    // away by itself, still sounding when nothing more is to come, is let ring until the sequence ends instead of
    // being cut at its written length. The sequences are written with that room after the last note.
    void start(int handle, bool ring_out = false);
    void stop(int handle);                  // fn 171
    void release(int handle);               // fn 152: at once, or when it ends if it is playing
    int status(int handle) const noexcept;  // fn 174: 0 stopped, 1 playing, 2 done; -1 no such handle
    // fn 177: the volume (percent) towards `target` over `milliseconds` (0: at once).
    void set_volume(int handle, int target, int milliseconds);

  private:
    struct NoteSlot {
        std::uint8_t channel = 0xFF;  // the sequence's own channel; 0xFF free
        std::uint8_t key = 0;
        std::int32_t ticks_left = 0;
    };
    struct Loop {
        std::int32_t count = -1;  // -1 free, 0 forever
        std::size_t position = 0;
    };
    struct Sequence {
        bool registered = false;
        bool started = false;
        bool release_when_done = false;
        bool ring_out = false;
        int state = 0;  // 0 stopped, 1 playing, 2 done
        std::span<const std::byte> stream;
        std::size_t position = 0;
        int delay = 0;
        int rate = 100;
        int rate_target = 100;
        int rate_accumulator = 0;
        std::int32_t rate_time = 0;
        std::int32_t rate_period = 1;
        int volume = 100;
        int volume_target = 100;
        std::int32_t volume_time = 0;
        std::int32_t volume_period = 1;
        std::array<Loop, 4> loops{};
        std::array<std::uint8_t, 16> map{};
        std::array<std::uint8_t, 16> program{};
        std::array<std::uint8_t, 16> bend_lsb{};
        std::array<std::uint8_t, 16> bend_msb{};
        std::array<std::uint8_t, std::size_t{9} * 16> remembered{};  // B8: tables.controllers' values, 16 apart
        std::array<NoteSlot, 32> notes{};
        int active_notes = 0;
    };

    void play_tick(Sequence& s);
    bool notes_to_come(const Sequence& s) const;
    void read_events(Sequence& s);
    std::size_t note_on_event(Sequence& s, std::uint8_t status);
    std::size_t meta_event(Sequence& s);
    void controller_event(Sequence& s, std::uint8_t channel, std::uint8_t controller, std::uint8_t value);
    void reset(Sequence& s);
    void notes_off(Sequence& s);
    // `only`: that sequence's notes alone. The driver lets go of every sequence's notes on a channel of that
    // number, so one effect ending cuts another copy of itself short; the Modern sound's effects don't.
    void release_channel_notes(std::uint8_t channel, const Sequence* only = nullptr);
    void end_sequence(Sequence& s);
    void apply_volume(Sequence& s);
    int lock_channel();
    // `later`: what the channel is set back to is sent when it is next taken, not now. The driver sends it at
    // once, the volume among it, while the effect's last notes are still dying away: they end on a louder
    // note. The Modern sound's effects wait.
    void unlock_channel(std::uint8_t channel, bool later = false);
    void set_back(std::uint8_t channel);
    void send(std::uint8_t status, std::uint8_t data1, std::uint8_t data2) { out_->send(status, data1, data2); }

    const assets::SequencerTables* tables_;
    MidiOut* out_;
    int start_volume_;
    std::array<Sequence, kSequences> sequences_{};
    // Driver-wide per MIDI channel (cs:2A1B, 2A2B): notes the sequencer has on it; 0x80 locked, 0x40 protected.
    std::array<std::uint8_t, 16> channel_notes_{};
    std::array<std::uint8_t, 16> channel_lock_{};
    std::array<bool, 16> set_back_later_{};
    // What was last sent on each channel number by any sequence (cs:295B, 29EB, 29FB, 2A0B): an unlocked channel
    // gets them back.
    std::array<std::uint8_t, std::size_t{9} * 16> shadow_controllers_{};
    std::array<std::uint8_t, 16> shadow_program_{}, shadow_bend_lsb_{}, shadow_bend_msb_{};
};

}  // namespace lvr::audio
