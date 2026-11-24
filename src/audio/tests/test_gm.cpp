// Tests for src/audio/gm.*: driver 463's filter on the sequencer's messages, and [real-data] the General MIDI sound
// through TinySoundFont with the SoundFont in gamedata/ (docs/original/sound-driver.md#driver-463-the-midi-driver).
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "assets/assets.hpp"
#include "audio/gm.hpp"
#include "audio/player.hpp"

using namespace lvr::audio;
namespace assets = lvr::assets;

namespace {

int loudest(const std::vector<std::int16_t>& samples) {
    int peak = 0;
    for (const std::int16_t s : samples) {
        peak = std::max(peak, std::abs(int{s}));
    }
    return peak;
}

}  // namespace

TEST_CASE("audio: driver 463 sends the channel messages as they are, but not its own controllers") {
    for (const int status : {0x80, 0x91, 0xA2, 0xC3, 0xD4, 0xE9}) {
        CHECK(midi_driver_sends(static_cast<std::uint8_t>(status), 0x20));
    }
    for (int c = 0; c < 128; ++c) {
        const bool kept = (c >= 0x20 && c <= 0x2E) || (c >= 0x3A && c <= 0x3F) || (c >= 0x6E && c <= 0x78);
        INFO("controller " << c);
        CHECK(midi_driver_sends(0xB5, static_cast<std::uint8_t>(c)) == !kept);
    }
}

TEST_CASE("audio: a SoundFont that isn't there") {
    auto synth = GmSynth::open("/nonexistent/sound.sf2", 49'715);
    REQUIRE_FALSE(synth);
    CHECK(synth.error().contains("/nonexistent/sound.sf2"));
}

TEST_CASE("audio: the General MIDI synthesizer plays a note, then lets it go", "[real-data]") {
    if (!std::filesystem::exists(LVR_SOUNDFONT)) {
        SKIP("no " LVR_SOUNDFONT);
    }
    auto synth = GmSynth::open(LVR_SOUNDFONT, 49'715);
    REQUIRE(synth);
    GmSynth& gm = **synth;
    CHECK(gm.sample_rate() == 49'715);
    std::vector<std::int16_t> out;
    gm.render(0.01, out);
    CHECK(out.size() == 2 * 497);
    CHECK(loudest(out) == 0);
    gm.send(0xC2, 19, 0);      // church organ
    gm.send(0xB2, 0x6E, 127);  // the sequencer's own: not for the synthesizer
    gm.send(0xB2, 7, 127);
    gm.send(0xE2, 0, 0x40);
    gm.send(0x92, 60, 100);
    gm.send(0x99, 38, 100);  // a snare on the percussion channel
    gm.send(0xA2, 60, 10);   // aftertouch: nothing
    out.clear();
    gm.render(0.2, out);
    CHECK(loudest(out) > 1000);
    gm.send(0x82, 60, 0);
    gm.send(0x99, 38, 0);
    for (int i = 0; i < 40; ++i) {  // the release
        out.clear();
        gm.render(0.1, out);
    }
    CHECK(loudest(out) == 0);
}

TEST_CASE("audio: the Modern sound: the General MIDI tunes and effects through the SoundFont", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR) || !std::filesystem::exists(LVR_SOUNDFONT)) {
        SKIP("no " LVR_ORIGINAL_DIR " or " LVR_SOUNDFONT);
    }
    auto files = assets::GameFiles::open(LVR_ORIGINAL_DIR);
    REQUIRE(files);
    auto data = load_sound_data(files->archive(), Version::kModern);
    REQUIRE(data);
    CHECK(data->start_volume == 90);
    CHECK(data->bank.empty());
    auto classic = load_sound_data(files->archive(), Version::kClassic);
    REQUIRE(classic);
    CHECK(data->effects.size() == classic->effects.size());  // the same effects, numbered the same
    // The effect the Modern sound remakes (number 3): the Classic one's stream, as long and with its notes in the
    // same places, only its instrument, its volume and its notes' height ours. The others are the General MIDI
    // arrangement's own.
    REQUIRE(data->effects.size() > 4);
    const auto& remade = data->effects.at(3).stream;
    const auto& theirs = classic->effects.at(3).stream;
    REQUIRE(remade.size() == theirs.size());
    std::size_t changed = 0;
    for (std::size_t i = 0; i < remade.size(); ++i) {
        changed += remade[i] != theirs[i] ? 1 : 0;
    }
    CHECK(changed >= 1);
    CHECK(changed <= 4);  // the program's number, the volume, and its two notes' keys
    CHECK(data->effects.at(4).stream != classic->effects.at(4).stream);
    // It is heard, alone, and softer than the square-wave buzz it replaces would be at full volume.
    {
        auto alone = GmSynth::open(LVR_SOUNDFONT, 49'715);
        REQUIRE(alone);
        Player effects{*data, **alone, nullptr, false, true};
        effects.play_effect(3);
        std::vector<std::int16_t> heard;
        for (int refresh = 0; refresh < 60; ++refresh) {
            effects.service();
            effects.service();
            (*alone)->render(1.0 / 59.71, heard);
        }
        CHECK(loudest(heard) > 300);
        CHECK(loudest(heard) < 20000);
    }
    for (std::size_t t = 0; t < data->tunes.size(); ++t) {
        CHECK(data->tunes.at(t).size() == classic->tunes.at(t).size());
    }
    auto synth = GmSynth::open(LVR_SOUNDFONT, 49'715);
    REQUIRE(synth);
    Player player{*data, **synth, nullptr, true, true};
    player.music_action(0, 0);  // the Interplay logo: tune 0
    player.play_effect(3);
    std::vector<std::int16_t> out;
    for (int refresh = 0; refresh < 600; ++refresh) {
        player.service();
        player.service();
        (*synth)->render(1.0 / 59.71, out);
    }
    CHECK(loudest(out) > 1000);
    CHECK(loudest(out) < 32767);  // no clipping
}
