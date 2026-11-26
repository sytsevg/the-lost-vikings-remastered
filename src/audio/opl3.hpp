#pragma once

// The Classic sound (ADR-0015): driver 460's voice layer, which turns the sequencer's MIDI messages into OPL3
// register writes (docs/original/sound-driver.md), with its timbre cache and the driver's tables read from DATA.DAT
// (assets::OplDriverTables); and the chip, ymfm's YMF262 (OPL3) as the Sound Blaster Pro 2 has it, taking the
// register writes and rendering 16-bit stereo at its own rate (14,318,180 Hz / 288 = 49,716 Hz).
//
// Not reproduced: the timbre cache's eviction (the game's instruments fit its 3,584 bytes: install_timbre says
// when they wouldn't).

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "assets/audio.hpp"
#include "audio/sequencer.hpp"

namespace lvr::audio {

// Where the register writes go: the chip emulation, a recording. Registers 0x000-0x1FF (0x1xx: the second set).
class OplSink {
  public:
    OplSink() = default;
    OplSink(const OplSink&) = default;
    OplSink& operator=(const OplSink&) = default;
    OplSink(OplSink&&) = default;
    OplSink& operator=(OplSink&&) = default;
    virtual ~OplSink() = default;
    virtual void write(std::uint16_t reg, std::uint8_t value) = 0;
};

// Driver 460's voice layer (2629).
class OplSynth : public MidiOut {
  public:
    static constexpr std::size_t kCacheBytes = 0xE00;

    // Initialises the chip (fn 102: 1582, 19BE).
    OplSynth(const assets::OplDriverTables& tables, OplSink& sink);

    void send(std::uint8_t status, std::uint8_t data1, std::uint8_t data2) override;

    // --- Timbres (fns 155, 156) ---
    // Whether the cache has the timbre (bank, patch).
    bool has_timbre(std::uint8_t bank, std::uint8_t patch) const noexcept;
    // Installs it (its bytes, starting with their size). False if the cache would be over its size (the original
    // would evict the least recently used).
    bool install_timbre(std::uint8_t bank, std::uint8_t patch, std::span<const std::byte> data);

  private:
    // The voice layer's slot (20 of them).
    struct Voice {
        std::uint8_t state = 0;            // 0D95: 0 free, 1 on
        std::uint8_t kind = 0;             // 0DA9: 0 two operators, 3 four
        std::uint8_t opl_channel = 0xFF;   // 0DBD
        std::uint8_t midi_channel = 0;     // 0DD1
        std::uint8_t note = 0;             // 0DE5: what it plays
        std::uint8_t key = 0;              // 0DF9: the note on's key
        std::int8_t transpose = 0;         // 0E0D
        std::uint8_t velocity = 0;         // 0E21: the factor
        bool sustained = false;            // 0E35: released while the sustain pedal was down
        std::uint8_t pending = 0;          // 0E49: registers to write
        std::uint8_t last_b0 = 0;          // 0E5D
        std::uint8_t key_on = 0;           // 0E71: 0x20 while on
        std::uint8_t connection = 0;       // 0E85: bit 0 pair 1, bit 1 pair 2
        std::uint16_t priority = 0x7FFF;   // 0FED
        std::uint16_t steal_priority = 0;  // 135B
        int timbre = -1;                   // the cache index
        // The operators: 0 the modulator, 1 the carrier, 2 and 3 the second pair. Levels as (63 - TL) << 10, the
        // multiple as << 12, the flags (AM/VIB/EG/KSR) and KSL as the register's bits.
        std::array<std::uint16_t, 4> level{};
        std::array<std::uint8_t, 4> ksl{};
        std::array<std::uint16_t, 4> multiple{};
        std::array<std::uint8_t, 4> flags{};
        std::array<std::uint8_t, 4> attack_decay{};
        std::array<std::uint8_t, 4> sustain_release{};
        std::array<std::uint8_t, 4> waveform{};
        std::uint16_t feedback = 0;  // 0FC5: byte 8 << 12
        std::uint8_t scaled1 = 0;    // 0F39: which operators of pair 1 follow the volume (bits 0, 1)
        std::uint8_t scaled2 = 0;    // 11F5
    };
    struct CachedTimbre {
        std::uint8_t bank = 0;
        std::uint8_t patch = 0;
        std::vector<std::byte> data;
    };

    // --- The voice layer ---
    void note_on(std::uint8_t channel, std::uint8_t key, std::uint8_t velocity);
    void note_off(std::uint8_t channel, std::uint8_t key);
    void sustain_off(std::uint8_t channel);
    void update_channel(std::uint8_t channel, std::uint8_t flag);
    int find_timbre(std::uint8_t bank, std::uint8_t patch) const noexcept;
    void setup_two_op(Voice& v, std::span<const std::byte> t);
    void setup_four_op(Voice& v, std::span<const std::byte> t);
    void assign_channel(int slot);
    void steal(int slot);
    void release_voice(int slot);
    void update(int slot);
    void write_pair(Voice& v, int pair, std::uint8_t opl_channel, std::uint8_t volume);
    void write_frequency(Voice& v, std::uint8_t opl_channel);
    void write_operator(std::uint8_t op, std::uint8_t base, std::uint8_t value);
    void write_channel(std::uint8_t channel, std::uint8_t base, std::uint8_t value);

    const assets::OplDriverTables* tables_;
    OplSink* sink_;
    // The voice layer's per MIDI channel (cs:1209-12A9, 1339).
    std::array<std::uint8_t, 16> volume_{}, pan_{}, bend_lsb_{}, bend_msb_{}, expression_{}, modulation_{}, sustain_{},
        priority_lock_{}, bank_{}, program_{}, voices_on_{};
    std::array<int, 16> channel_timbre_{};
    std::array<int, 128> drum_timbre_{};  // 12B9: channel 10's timbre by key, looked up once
    std::array<Voice, 20> voices_{};
    std::array<std::uint8_t, 18> opl_owner_{};  // 1349: the MIDI channel using an OPL channel, 0xFF free
    int last_channel_ = -1;                     // 0D18
    int last_pair_ = -1;                        // 0D1A
    std::uint8_t four_op_register_ = 0;         // 0D1C
    std::vector<CachedTimbre> cache_;
    std::size_t cache_bytes_ = 0;
};

class Opl3 : public OplSink {
  public:
    static constexpr std::uint32_t kClock = 14'318'180;
    static constexpr std::uint32_t kSampleRate = kClock / 288;  // what ymfm's YMF262 gives for the clock
    Opl3();
    Opl3(const Opl3&) = delete;
    Opl3& operator=(const Opl3&) = delete;
    Opl3(Opl3&&) = delete;
    Opl3& operator=(Opl3&&) = delete;
    ~Opl3() override;

    void write(std::uint16_t reg, std::uint8_t value) override;
    // Told every register write from here on (none: nobody): to compare them with the original's (a DOSBox-X
    // capture, docs/original/sound-driver.md).
    void tap(std::function<void(std::uint16_t, std::uint8_t)> told);
    // Renders `seconds` of sound (with the fraction of a sample carried over) onto `out`, left and right in turn.
    void render(double seconds, std::vector<std::int16_t>& out);
    std::uint32_t sample_rate() const noexcept { return rate_; }

  private:
    struct Chip;
    std::unique_ptr<Chip> chip_;
    std::uint32_t rate_;
    double pending_ = 0;
};

}  // namespace lvr::audio
