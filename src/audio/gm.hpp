#pragma once

// The Modern sound (ADR-0015): the General MIDI data played as driver 463 (the MPU-401 MIDI driver) sends it, into
// TinySoundFont with a SoundFont from a file (GeneralUser GS by default, not part of the game's files), rendering
// 16-bit stereo at the OPL3's rate so both versions share one audio stream.

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "audio/sequencer.hpp"

namespace lvr::audio {

// Whether driver 463's message routine (08CE) sends a message from the sequencer to the MIDI port as it is. It
// keeps controllers 0x20-0x2E and 0x3A-0x3F (its MT-32 patch and timbre edits, sent as SysEx or not at all) and
// 0x6E-0x78 (the sequencer's own: channel lock, loops, bank select for the timbre cache, ...); everything else goes
// out unchanged.
bool midi_driver_sends(std::uint8_t status, std::uint8_t data1) noexcept;

class GmSynth : public MidiOut {
  public:
    // The SoundFont `soundfont`, rendering at `rate`.
    static std::expected<std::unique_ptr<GmSynth>, std::string> open(const std::filesystem::path& soundfont,
                                                                     std::uint32_t rate);
    GmSynth(const GmSynth&) = delete;
    GmSynth& operator=(const GmSynth&) = delete;
    GmSynth(GmSynth&&) = delete;
    GmSynth& operator=(GmSynth&&) = delete;
    ~GmSynth() override;

    // A second synthesizer on the same SoundFont (its samples shared), with channels and voices of its own.
    std::unique_ptr<GmSynth> copy() const;

    void send(std::uint8_t status, std::uint8_t data1, std::uint8_t data2) override;
    // Renders `seconds` of sound (with the fraction of a sample carried over) onto `out`, left and right in turn.
    void render(double seconds, std::vector<std::int16_t>& out);
    std::uint32_t sample_rate() const noexcept { return rate_; }

  private:
    struct Synth;
    GmSynth(std::unique_ptr<Synth> synth, std::uint32_t rate);
    std::unique_ptr<Synth> synth_;
    std::uint32_t rate_;
    double pending_ = 0;
    std::vector<std::int16_t> buffer_;
};

}  // namespace lvr::audio
