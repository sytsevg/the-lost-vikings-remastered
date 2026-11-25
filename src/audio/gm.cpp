#include "audio/gm.hpp"

#include <format>
#include <utility>

#include "tsf.h"

namespace lvr::audio {
namespace {

constexpr int kChannels = 16;
constexpr int kPercussion = 9;  // MIDI channel 10
// Voices at once (set up once, so none is made while the sound card waits). A note of GeneralUser GS takes one
// or two, and a note let go keeps one while it dies away: the tunes want up to 106 (level 6), and at the 64 there
// were the synthesizer cut notes short to make room (the user heard a bass or a guitar stop early).
constexpr int kMaxVoices = 256;
// The output's gain: -7 dB brings GeneralUser GS to the Classic sound's loudness (RMS over the first 3,000 steps:
// 3,687 against 1,658 at 0 dB), so switching keeps the level, and its loudest moments don't clip.
constexpr float kGainDb = -7.0F;

}  // namespace

bool midi_driver_sends(std::uint8_t status, std::uint8_t data1) noexcept {
    if ((status & 0xF0U) != 0xB0) {
        return true;
    }
    return !((data1 >= 0x20 && data1 <= 0x2E) || (data1 >= 0x3A && data1 <= 0x3F) || (data1 >= 0x6E && data1 <= 0x78));
}

struct GmSynth::Synth {
    tsf* f = nullptr;
    Synth() = default;
    Synth(const Synth&) = delete;
    Synth& operator=(const Synth&) = delete;
    Synth(Synth&&) = delete;
    Synth& operator=(Synth&&) = delete;
    ~Synth() {
        if (f != nullptr) {
            tsf_close(f);
        }
    }
};

namespace {

void set_up(tsf* f, std::uint32_t rate) {
    tsf_set_output(f, TSF_STEREO_INTERLEAVED, static_cast<int>(rate), kGainDb);
    (void)tsf_set_max_voices(f, kMaxVoices);
    for (int ch = 0; ch < kChannels; ++ch) {  // every channel exists, with the first preset, until told otherwise
        (void)tsf_channel_set_presetnumber(f, ch, 0, ch == kPercussion ? 1 : 0);
    }
}

}  // namespace

std::expected<std::unique_ptr<GmSynth>, std::string> GmSynth::open(const std::filesystem::path& soundfont,
                                                                   std::uint32_t rate) {
    auto synth = std::make_unique<Synth>();
    synth->f = tsf_load_filename(soundfont.string().c_str());
    if (synth->f == nullptr) {
        return std::unexpected{std::format("can't load the SoundFont {}", soundfont.string())};
    }
    set_up(synth->f, rate);
    return std::unique_ptr<GmSynth>{new GmSynth{std::move(synth), rate}};
}

GmSynth::GmSynth(std::unique_ptr<Synth> synth, std::uint32_t rate) : synth_{std::move(synth)}, rate_{rate} {}

GmSynth::~GmSynth() = default;

std::unique_ptr<GmSynth> GmSynth::copy() const {
    auto synth = std::make_unique<Synth>();
    synth->f = tsf_copy(synth_->f);
    if (synth->f == nullptr) {
        return nullptr;
    }
    set_up(synth->f, rate_);
    return std::unique_ptr<GmSynth>{new GmSynth{std::move(synth), rate_}};
}

void GmSynth::send(std::uint8_t status, std::uint8_t d1, std::uint8_t d2) {
    if (!midi_driver_sends(status, d1)) {
        return;
    }
    tsf* f = synth_->f;
    const auto ch = static_cast<int>(status & 0x0FU);
    switch (status & 0xF0U) {
        case 0x90:
            (void)tsf_channel_note_on(f, ch, d1, static_cast<float>(d2) / 127.0F);  // velocity 0: a note off
            return;
        case 0x80:
            tsf_channel_note_off(f, ch, d1);
            return;
        case 0xB0:
            (void)tsf_channel_midi_control(f, ch, d1, d2);
            return;
        case 0xC0:
            (void)tsf_channel_set_presetnumber(f, ch, d1, ch == kPercussion ? 1 : 0);
            return;
        case 0xE0:
            (void)tsf_channel_set_pitchwheel(f, ch, static_cast<int>(d1 | (unsigned{d2} << 7U)));
            return;
        default:
            return;  // aftertouch: the synthesizer has none
    }
}

void GmSynth::render(double seconds, std::vector<std::int16_t>& out) {
    pending_ += seconds * rate_;
    const auto samples = static_cast<int>(pending_);
    pending_ -= samples;
    buffer_.resize(static_cast<std::size_t>(samples) * 2);
    tsf_render_short(synth_->f, buffer_.data(), samples, 0);
    out.insert(out.end(), buffer_.begin(), buffer_.end());
}

}  // namespace lvr::audio
