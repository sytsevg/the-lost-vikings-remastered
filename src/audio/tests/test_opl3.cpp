// Tests for src/audio/opl3.*, the sequencer driving it (sequencer.*) and player.*: hand-made driver tables, timbres and
// XMIDI streams for each rule of docs/original/sound-driver.md, and [real-data] the music's register writes (checked
// against DOSBox's capture).
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <utility>
#include <vector>

#include "assets/assets.hpp"
#include "assets/audio.hpp"
#include "audio/opl3.hpp"
#include "audio/player.hpp"
#include "audio/sequencer.hpp"

using namespace lvr::audio;
namespace assets = lvr::assets;

namespace {

struct Write {
    std::uint16_t reg;
    std::uint8_t value;
    bool operator==(const Write&) const = default;
};

class Recorder : public OplSink {
  public:
    void write(std::uint16_t reg, std::uint8_t value) override { writes.push_back({reg, value}); }
    std::vector<Write> writes;
    // The last value written to `reg`, or -1.
    int last(std::uint16_t reg) const {
        for (auto w = writes.rbegin(); w != writes.rend(); ++w) {
            if (w->reg == reg) {
                return w->value;
            }
        }
        return -1;
    }
    int count(std::uint16_t reg) const {
        int n = 0;
        for (const Write& w : writes) {
            n += w.reg == reg ? 1 : 0;
        }
        return n;
    }
};

// Tables shaped as the driver's: 18 channels, operators 0-35, F-numbers rising by the sixteenth.
assets::OplDriverTables tables() {
    assets::OplDriverTables t;
    for (std::size_t i = 0; i < t.fnumbers.size(); ++i) {
        t.fnumbers.at(i) = static_cast<std::uint16_t>(0x200 + i);
    }
    for (std::size_t i = 0; i < 96; ++i) {
        t.blocks.at(i) = static_cast<std::uint8_t>(i / 12);
        t.note_in_octave.at(i) = static_cast<std::uint8_t>(i % 12);
    }
    t.init_values.at(0x01) = 0x20;
    for (std::size_t i = 0; i < 16; ++i) {
        t.velocity.at(i) = static_cast<std::uint8_t>(82 + (3 * i));
    }
    const std::uint8_t first[] = {0, 1, 2, 6, 7, 8, 12, 13, 14};
    const std::uint8_t offsets[] = {0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 16, 17, 18, 19, 20, 21};
    for (std::size_t ch = 0; ch < 18; ++ch) {
        t.first_operator.at(ch) = static_cast<std::uint8_t>(first[ch % 9] + (ch >= 9 ? 18 : 0));
        t.second_operator.at(ch) = static_cast<std::uint8_t>(t.first_operator.at(ch) + 3);
        t.channel_offset.at(ch) = static_cast<std::uint8_t>(ch % 9);
        t.channel_chip.at(ch) = ch >= 9 ? 1 : 0;
        t.partner.at(ch) = 0xFF;
    }
    for (std::size_t op = 0; op < 36; ++op) {
        t.operator_offset.at(op) = offsets[op % 18];
        t.operator_chip.at(op) = op >= 18 ? 1 : 0;
    }
    for (std::size_t ch : {0U, 1U, 2U, 9U, 10U, 11U}) {
        t.four_op_capable.at(ch) = 1;
        t.partner.at(ch) = static_cast<std::uint8_t>(ch + 3);
        t.partner.at(ch + 3) = static_cast<std::uint8_t>(ch);
        t.four_op_bit.at(ch) = static_cast<std::uint8_t>(1U << (ch % 9 + (ch >= 9 ? 3 : 0)));
        t.four_op_bit.at(ch + 3) = t.four_op_bit.at(ch);
        t.partner_first.at(ch) = t.first_operator.at(ch + 3);
        t.partner_second.at(ch) = t.second_operator.at(ch + 3);
    }
    t.four_op_channels = {0, 1, 2, 9, 10, 11};
    t.scaled_pair1 = {0, 1, 2, 1};
    t.scaled_pair2 = {2, 2, 2, 3};
    t.sequencer.controllers = {7, 1, 10, 11, 64, 114, 110, 111, 112};
    t.sequencer.controller_defaults = {127, 0, 64, 127, 0, 0, 0, 0, 0};
    t.sequencer.default_programs = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF};
    return t;
}

std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (const int v : values) {
        out.push_back(static_cast<std::byte>(v));
    }
    return out;
}

// A 2-operator timbre: transpose 0, modulator 20=0x21 40=0x10 60=0xF1 80=0x22 E0=1, feedback/connection 0x06,
// carrier 20=0x01 40=0x00 60=0xF2 80=0x33 E0=0.
std::vector<std::byte> two_op(int transpose = 0) {
    return bytes({14, 0, transpose, 0x21, 0x10, 0xF1, 0x22, 1, 0x06, 0x01, 0x00, 0xF2, 0x33, 0});
}

std::vector<std::byte> four_op() {
    auto t = bytes({25, 0,    0,    0x21, 0x10, 0xF1, 0x22, 1,    0x86, 0x01, 0x00, 0xF2, 0x33,
                    0,  0x02, 0x05, 0xF3, 0x44, 2,    0,    0x03, 0x06, 0xF4, 0x55, 3});
    return t;
}

int keyons(const Recorder& r) {
    int n = 0;
    for (const Write& w : r.writes) {
        n += (w.reg & 0xF0U) == 0xB0 && (w.reg & 0x0FU) <= 8 && (w.value & 0x20U) != 0 ? 1 : 0;
    }
    return n;
}

}  // namespace

TEST_CASE("audio: the driver starts the chip and the channels' defaults") {
    const auto t = tables();
    Recorder r;
    OplSynth synth{t, r};
    Sequencer driver{t.sequencer, synth, 100};
    REQUIRE(r.writes.size() >= 2 + 0xF5 + 0xF5);
    CHECK(r.writes[0] == Write{0x105, 1});
    CHECK(r.writes[1] == Write{0x104, 0});
    CHECK(r.writes[2] == Write{0x001, 0x20});
    CHECK(r.writes.back().reg == 0x1F5);
    CHECK(driver.status(0) == -1);
}

TEST_CASE("audio: a note: the voice's registers, its frequency, its duration, then the key off") {
    const auto t = tables();
    Recorder r;
    OplSynth synth{t, r};
    Sequencer driver{t.sequencer, synth, 100};
    REQUIRE(synth.install_timbre(0, 5, two_op()));
    CHECK(synth.has_timbre(0, 5));
    CHECK(synth.install_timbre(0, 5, two_op()));  // already there
    // program 5 on channel 2, a note of 3 ticks (key 60 + 0), a delay of 5, the end
    const auto stream = bytes({0xC1, 5, 0x91, 60, 127, 3, 5, 0xFF, 0x2F, 0});
    const int h = driver.register_sequence(stream);
    REQUIRE(h == 0);
    CHECK(driver.status(h) == 0);
    driver.start(h);
    CHECK(driver.status(h) == 1);
    r.writes.clear();
    driver.service();
    // the operators of channel 0 (operator slots 0 and 3): 20 = multiple | flags, 40 = 63 - level', 60, 80, E0
    CHECK(r.last(0x20) == 0x21);
    CHECK(r.last(0x23) == 0x01);
    CHECK(r.last(0x60) == 0xF1);
    CHECK(r.last(0x83) == 0x33);
    CHECK(r.last(0xE0) == 0x01);
    CHECK(r.last(0xC0) == 0x36);  // feedback 6 | both outputs
    CHECK(r.last(0x40) == 0x10);  // the modulator doesn't follow the volume (connection 0)
    CHECK(r.last(0x43) == 0x00);  // the carrier at full volume: 63 - 63
    // key 60 - 12 = 48: block 4 - 1, F-number from note 0, sixteenth 0
    CHECK(r.last(0xA0) == 0x00);
    CHECK(r.last(0xB0) == (0x20 | (3 << 2) | 0x02));
    CHECK(keyons(r) == 1);
    driver.service();
    driver.service();
    CHECK((r.last(0xB0) & 0x20) != 0);
    driver.service();  // 3 ticks later: off
    CHECK((r.last(0xB0) & 0x20) == 0);
    for (int i = 0; i < 6; ++i) {
        driver.service();
    }
    CHECK(driver.status(h) == 2);
    driver.release(h);
    CHECK(driver.status(h) == -1);
}

TEST_CASE("audio: controllers: volume, pan, modulation, the sequence's volume ramp, sustain") {
    const auto t = tables();
    Recorder r;
    OplSynth synth{t, r};
    Sequencer driver{t.sequencer, synth, 100};
    REQUIRE(synth.install_timbre(0, 5, two_op()));
    const auto stream = bytes({0xC1, 5,    0xB1, 7,   64, 0xB1, 10,   10, 0xB1, 1,    100,  0xB1, 64,
                               127,  0x91, 60,   127, 2,  3,    0xB1, 64, 0,    0x10, 0xFF, 0x2F, 0});
    const int h = driver.register_sequence(stream);
    driver.start(h);
    driver.service();
    CHECK(r.last(0x43) == 0x20);        // volume 64 (x 2 / 256 + 1 = 64), velocity factor 127: 63 x 64 / 127 = 31
    CHECK((r.last(0xC0) & 0x10) == 0);  // pan 10: the left output off
    CHECK((r.last(0x20) & 0x40) != 0);  // modulation >= 64: vibrato
    driver.service();
    driver.service();
    CHECK((r.last(0xB0) & 0x20) != 0);  // the note's end came with the sustain pedal down
    driver.service();
    driver.service();
    CHECK((r.last(0xB0) & 0x20) == 0);  // pedal up: released
    driver.set_volume(h, 50, 0);
    driver.set_volume(h, 0, 100);  // a ramp: 100 ms
    for (int i = 0; i < 20; ++i) {
        driver.service();
    }
    driver.stop(h);
    CHECK(driver.status(h) != 1);
}

TEST_CASE("audio: FOR / NEXT loops, a stop, a restart") {
    const auto t = tables();
    Recorder r;
    OplSynth synth{t, r};
    Sequencer driver{t.sequencer, synth, 100};
    REQUIRE(synth.install_timbre(0, 5, two_op()));
    // FOR 3 times: a note, then NEXT
    const auto stream = bytes({0xC1, 5, 0xB1, 116, 3, 0x91, 60, 127, 1, 2, 0xB1, 117, 127, 0xFF, 0x2F, 0});
    const int h = driver.register_sequence(stream);
    driver.start(h);
    for (int i = 0; i < 20; ++i) {
        driver.service();
    }
    CHECK(keyons(r) == 3);
    CHECK(driver.status(h) == 2);
    driver.start(h);
    CHECK(driver.status(h) == 1);
    driver.start(h);  // restarting a playing one stops it first
    driver.stop(h);
    driver.stop(h);
    CHECK(driver.status(h) == 0);
}

TEST_CASE("audio: channel locking moves a sequence's channel, and gives it back") {
    const auto t = tables();
    Recorder r;
    OplSynth synth{t, r};
    Sequencer driver{t.sequencer, synth, 100};
    REQUIRE(synth.install_timbre(0, 5, two_op()));
    // channel 12 (0-based 11) locks a channel, plays on it, unlocks
    const auto stream = bytes({0xBB, 110, 127, 0xCB, 5, 0x9B, 60, 127, 1, 2, 0xBB, 110, 0, 0xFF, 0x2F, 0});
    const int h = driver.register_sequence(stream);
    driver.start(h);
    for (int i = 0; i < 5; ++i) {
        driver.service();
    }
    CHECK(keyons(r) == 1);  // channel 12 alone would play nothing (notes only on channels 2-10)
    CHECK(driver.status(h) == 2);
}

TEST_CASE("audio: more notes than OPL channels: the oldest kind of voice gives way") {
    const auto t = tables();
    Recorder r;
    OplSynth synth{t, r};
    Sequencer driver{t.sequencer, synth, 100};
    REQUIRE(synth.install_timbre(0, 5, two_op()));
    std::vector<std::byte> stream = bytes({0xC1, 5});
    for (int key = 30; key < 30 + 22; ++key) {  // 22 long notes at once: 20 voice slots, 18 channels
        for (const int b : {0x91, key, 127, 0x40}) {
            stream.push_back(static_cast<std::byte>(b));
        }
    }
    for (const int b : {0x7F, 0xFF, 0x2F, 0}) {
        stream.push_back(static_cast<std::byte>(b));
    }
    const int h = driver.register_sequence(stream);
    driver.start(h);
    driver.service();
    CHECK(keyons(r) >= 18);
    driver.stop(h);
}

TEST_CASE("audio: stealing for a 4-operator voice whose victim is its own partner leaves the voice waiting") {
    // The driver looks the victim's partner up by the slot's number (2220): when that is the victim's own channel,
    // the victim is released twice and the waiting voice gets "channel 0xFF", which is no channel. Reached in
    // the game by the password screen's beeps over the title music. Many
    // 2- and 4-operator notes of mixed lengths reach it here; nothing may fault, and notes go on sounding.
    const auto t = tables();
    Recorder r;
    OplSynth synth{t, r};
    Sequencer driver{t.sequencer, synth, 100};
    REQUIRE(synth.install_timbre(0, 5, two_op()));
    REQUIRE(synth.install_timbre(0, 7, four_op()));
    std::vector<std::byte> stream = bytes({0xC1, 5, 0xC2, 7, 0xC3, 5, 0xC4, 7});
    unsigned seed = 1;
    for (int i = 0; i < 600; ++i) {
        seed = (seed * 1103515245U) + 12345U;
        const int channel = 1 + static_cast<int>((seed >> 16U) % 4U);
        const int key = 30 + static_cast<int>((seed >> 18U) % 40U);
        const int length = 1 + static_cast<int>((seed >> 24U) % 30U);
        for (const int b : {0x90 | channel, key, 100, length}) {
            stream.push_back(static_cast<std::byte>(b));
        }
        if (i % 3 == 0) {
            stream.push_back(std::byte{1});  // a tick's delay
        }
    }
    for (const int b : {0x7F, 0xFF, 0x2F, 0}) {
        stream.push_back(static_cast<std::byte>(b));
    }
    const int h = driver.register_sequence(stream);
    driver.start(h);
    for (int i = 0; i < 260; ++i) {
        driver.service();
    }
    CHECK(keyons(r) > 200);
    driver.stop(h);
}

TEST_CASE("audio: a 4-operator voice uses a channel pair and register 104") {
    const auto t = tables();
    Recorder r;
    OplSynth synth{t, r};
    Sequencer driver{t.sequencer, synth, 100};
    REQUIRE(synth.install_timbre(0, 7, four_op()));
    const auto stream = bytes({0xC1, 7, 0x91, 60, 127, 2, 3, 0xFF, 0x2F, 0});
    const int h = driver.register_sequence(stream);
    driver.start(h);
    driver.service();
    CHECK(r.last(0x104) == 0x01);
    CHECK(r.last(0x28) == 0x02);  // the second pair on channel 3's operators (slots 6 and 9: offsets 8, 11)
    CHECK(r.last(0x2B) == 0x03);
    for (int i = 0; i < 6; ++i) {
        driver.service();
    }
    CHECK(driver.status(h) == 2);
}

TEST_CASE("audio: the cache's size, percussion by key, and only eight sequences") {
    const auto t = tables();
    Recorder r;
    OplSynth synth{t, r};
    Sequencer driver{t.sequencer, synth, 100};
    std::vector<std::byte> big(OplSynth::kCacheBytes);
    CHECK_FALSE(synth.install_timbre(1, 1, std::vector<std::byte>(OplSynth::kCacheBytes + 1)));
    REQUIRE(synth.install_timbre(127, 36, two_op(50)));  // a drum: plays its own note, 50
    const auto stream = bytes({0x99, 36, 100, 2, 3, 0xFF, 0x2F, 0});
    std::vector<int> handles;
    for (int i = 0; i < Sequencer::kSequences; ++i) {
        handles.push_back(driver.register_sequence(stream));
    }
    CHECK(driver.register_sequence(stream) == -1);
    driver.start(handles[0]);
    driver.service();
    CHECK(keyons(r) == 1);
}

TEST_CASE("audio: the player: effect slots, music actions, the switches") {
    SoundData data;
    data.opl = tables();
    data.sequencer = data.opl.sequencer;
    data.bank.push_back(assets::Timbre{.patch = 5, .bank = 0, .data = two_op()});
    assets::XmidiSequence effect;
    effect.timbres = {{.patch = 5, .bank = 0}};
    effect.stream = bytes({0xC1, 5, 0x91, 60, 127, 0x40, 0x40, 0xFF, 0x2F, 0});
    data.effects = {effect, effect};
    data.tunes.at(2) = {effect};
    Recorder r;
    OplSynth synth{data.opl, r};
    Player player{data, synth, &synth, true, true};
    for (int i = 0; i < 5; ++i) {
        player.play_effect(1);  // four slots: the fifth is dropped
    }
    player.service();
    CHECK(keyons(r) == 4);
    player.stop_effect(1);
    player.play_effect(7);  // no such effect
    player.music_action(0, 2);
    player.service();
    player.fade_music();
    player.music_action(2, 2);
    player.music_action(3, 2);
    player.music_action(1, 2);
    player.restart_music();
    player.set_effects(false);
    player.play_effect(0);
    player.stop_effect(0);
    player.set_effects(true);
    player.set_music(false);
    player.set_music(true);
    player.stop_all(false);
    player.stop_all(true);
    player.service();
    CHECK(keyons(r) >= 5);
}

TEST_CASE("audio: the OPL3 chip renders the writes into sound") {
    Opl3 chip;
    CHECK(chip.sample_rate() == 49'715);  // 14,318,180 / 288
    std::vector<std::int16_t> out;
    chip.render(0.01, out);
    CHECK(out.size() == 2 * 497);  // 0.01 s
    for (const std::int16_t s : out) {
        CHECK(s == 0);
    }
    // a plain sine on channel 0, keyed on
    for (const auto [reg, value] : std::initializer_list<std::pair<std::uint16_t, std::uint8_t>>{{0x105, 1},
                                                                                                 {0x23, 0x01},
                                                                                                 {0x43, 0x00},
                                                                                                 {0x63, 0xF0},
                                                                                                 {0x83, 0x00},
                                                                                                 {0xC0, 0x31},
                                                                                                 {0xA0, 0x80},
                                                                                                 {0xB0, 0x32}}) {
        chip.write(reg, value);
    }
    out.clear();
    chip.render(0.05, out);
    int loudest = 0;
    for (const std::int16_t s : out) {
        loudest = std::max(loudest, std::abs(int{s}));
    }
    CHECK(loudest > 1000);
}

TEST_CASE("audio: the logo's music, register for register as the original's driver plays it", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR);
    }
    auto files = assets::GameFiles::open(LVR_ORIGINAL_DIR);
    REQUIRE(files);
    auto data = load_sound_data(files->archive(), Version::kClassic);
    REQUIRE(data);
    Recorder r;
    OplSynth synth{data->opl, r};
    Player player{*data, synth, &synth, true, true};
    player.music_action(0, 0);  // the Interplay logo: tune 0
    for (int refresh = 0; refresh < 1000; ++refresh) {
        player.service();
        player.service();
    }
    // FNV-1a over the writes. Their key-ons and key-offs (886 in 20 s) were found the same as the original's in a
    // DOSBox-X capture (docs/original/sound-driver.md#checked-in-dosbox); the hash locks that in.
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const Write& w : r.writes) {
        for (const unsigned b : {w.reg & 0xFFU, unsigned{w.reg} >> 8U, unsigned{w.value}}) {
            hash = (hash ^ b) * 0x100000001b3ULL;
        }
    }
    CHECK(keyons(r) > 300);
    INFO("hash " << std::hex << hash);
    CHECK(hash == 0xa7bd833950e69003ULL);
}
