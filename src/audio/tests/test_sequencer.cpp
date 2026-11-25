// Tests for src/audio/sequencer.*: what the shared XMIDI sequencer sends to a driver's output, as MIDI messages
// (docs/original/sound-driver.md). Its timing, loops and channel locking are checked through the OPL3 voice layer in
// test_opl3.cpp.
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <vector>

#include "assets/assets.hpp"
#include "assets/audio.hpp"
#include "audio/sequencer.hpp"

using namespace lvr::audio;
namespace assets = lvr::assets;

namespace {

struct Message {
    std::uint8_t status;
    std::uint8_t data1;
    std::uint8_t data2;
    bool operator==(const Message&) const = default;
};

class Recorder : public MidiOut {
  public:
    void send(std::uint8_t status, std::uint8_t data1, std::uint8_t data2) override {
        messages.push_back({status, data1, data2});
    }
    std::vector<Message> messages;
};

assets::SequencerTables tables() {
    assets::SequencerTables t;
    t.controllers = {7, 1, 10, 11, 64, 114, 110, 111, 112};
    t.controller_defaults = {127, 0xFF, 64, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    t.default_programs = {0, 3, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return t;
}

std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (const int v : values) {
        out.push_back(static_cast<std::byte>(v));
    }
    return out;
}

}  // namespace

TEST_CASE("audio: the sequencer's start: channels 2-10 get the default controllers, a centred bend, the programs") {
    const auto t = tables();
    Recorder r;
    const Sequencer sequencer{t, r, 100};
    std::vector<Message> expected;
    for (std::uint8_t ch = 1; ch <= 9; ++ch) {
        expected.push_back({static_cast<std::uint8_t>(0xB0U | ch), 7, 127});
    }
    for (std::uint8_t ch = 1; ch <= 9; ++ch) {
        expected.push_back({static_cast<std::uint8_t>(0xB0U | ch), 10, 64});
    }
    expected.push_back({0xE1, 0, 0x40});
    expected.push_back({0xC1, 3, 0});
    for (std::uint8_t ch = 2; ch <= 9; ++ch) {
        expected.push_back({static_cast<std::uint8_t>(0xE0U | ch), 0, 0x40});
    }
    CHECK(r.messages == expected);
    CHECK(sequencer.status(0) == -1);
}

TEST_CASE("audio: the start volume scales a sequence's volume controller (463 starts at 90%, 460 at 100%)") {
    const auto t = tables();
    const auto stream = bytes({0xB2, 7, 100, 0xB2, 10, 30, 0x01, 0xFF, 0x2F, 0});
    for (const auto [start, sent] : {std::pair{100, 100}, std::pair{90, 90}}) {
        Recorder r;
        Sequencer sequencer{t, r, start};
        r.messages.clear();
        const int h = sequencer.register_sequence(stream);
        sequencer.start(h);
        sequencer.service();
        REQUIRE(r.messages.size() >= 2);
        CHECK(r.messages[0] == Message{0xB2, 7, static_cast<std::uint8_t>(sent)});
        CHECK(r.messages[1] == Message{0xB2, 10, 30});  // others as they are
        sequencer.service();
        CHECK(sequencer.status(h) == 2);
    }
}

TEST_CASE("audio: the loudness scales what the game's volume gives, and at 0 no note is played") {
    const auto t = tables();
    // The volume controller, a note, then the volume controller again a tick later.
    const auto stream = bytes({0xB2, 7, 100, 0x92, 60, 90, 0x01, 0x01, 0xB2, 7, 120, 0x01, 0xFF, 0x2F, 0});
    Recorder r;
    Sequencer sequencer{t, r, 90};
    r.messages.clear();
    const int h = sequencer.register_sequence(stream);
    sequencer.set_loudness(h, 50);
    CHECK(r.messages.empty());  // nothing to send before it plays
    sequencer.start(h);
    sequencer.service();
    REQUIRE(r.messages.size() >= 2);
    CHECK(r.messages[0] == Message{0xB2, 7, 45});  // 100 at 90% at half the loudness
    CHECK(r.messages[1] == Message{0x92, 60, 90});
    r.messages.clear();
    sequencer.set_loudness(h, 100);  // sent again at once, from what the sequence last asked for
    CHECK(r.messages == std::vector<Message>{{0xB2, 7, 90}});
    r.messages.clear();
    sequencer.set_loudness(h, 0);
    CHECK(r.messages == std::vector<Message>{{0xB2, 7, 0}});

    Recorder silent;
    Sequencer quiet{t, silent, 100};
    silent.messages.clear();
    const int q = quiet.register_sequence(stream);
    quiet.set_loudness(q, 0);
    quiet.start(q);
    quiet.service();
    for (const Message& m : silent.messages) {
        CHECK((m.status & 0xF0U) != 0x90U);  // no note on
    }
    // A sequence registered again in that handle is heard as the driver has it.
    quiet.stop(q);
    quiet.release(q);
    const int again = quiet.register_sequence(stream);
    CHECK(again == q);
    silent.messages.clear();
    quiet.start(again);
    quiet.service();
    REQUIRE_FALSE(silent.messages.empty());
    CHECK(silent.messages[0] == Message{0xB2, 7, 100});
}

TEST_CASE("audio: a placed sequence pans its channels from where it pans itself, before each note") {
    const auto t = tables();
    // A note on channel 3 with no pan of its own; a pan to the right, a note; the end.
    const auto stream = bytes({0x92, 60, 90, 0x01, 0x01, 0xB2, 10, 100, 0x92, 62, 90, 0x01, 0x01, 0xFF, 0x2F, 0});
    Recorder r;
    Sequencer sequencer{t, r, 100};
    r.messages.clear();
    const int h = sequencer.register_sequence(stream);
    sequencer.set_pan(h, -40);  // to the left
    sequencer.start(h);
    sequencer.service();
    REQUIRE(r.messages.size() >= 2);
    CHECK(r.messages[0] == Message{0xB2, 10, 24});  // the middle, 40 to the left
    CHECK(r.messages[1] == Message{0x92, 60, 90});
    r.messages.clear();
    sequencer.service();
    sequencer.service();
    // Its own pan moved by as much, and again before the note.
    CHECK(std::ranges::count(r.messages, Message{0xB2, 10, 60}) == 2);
    // One not placed sends no pan of ours.
    Recorder plain;
    Sequencer unplaced{t, plain, 100};
    plain.messages.clear();
    const int u = unplaced.register_sequence(stream);
    unplaced.start(u);
    unplaced.service();
    REQUIRE_FALSE(plain.messages.empty());
    CHECK(plain.messages[0] == Message{0x92, 60, 90});
}

TEST_CASE("audio: with ring-out, a dying-away instrument's last note is let go only when the sequence ends") {
    const auto t = tables();
    // Channel 3: a program, a note of 2 ticks, then 10 ticks of nothing, the end.
    const auto drum = bytes({0xC2, 118, 0x92, 60, 100, 2, 10, 0xFF, 0x2F, 0});
    const auto flute = bytes({0xC2, 73, 0x92, 60, 100, 2, 10, 0xFF, 0x2F, 0});
    const auto two_notes = bytes({0xC2, 118, 0x92, 60, 100, 2, 6, 0x92, 62, 100, 2, 10, 0xFF, 0x2F, 0});
    // The service calls after which the note-off of `key` has been sent.
    const auto off_after = [&t](const std::vector<std::byte>& stream, bool ring_out, std::uint8_t key) {
        Recorder r;
        Sequencer sequencer{t, r, 100};
        const int h = sequencer.register_sequence(stream);
        sequencer.start(h, ring_out);
        r.messages.clear();
        for (int services = 1; services <= 40; ++services) {
            sequencer.service();
            for (const Message& m : r.messages) {
                if (m.status == 0x82 && m.data1 == key) {
                    return services;
                }
            }
        }
        return -1;
    };
    const int written = off_after(drum, false, 60);
    const int rung_out = off_after(drum, true, 60);
    CHECK(written > 0);
    CHECK(rung_out > written + 5);                 // at the sequence's end, well after its written length
    CHECK(off_after(flute, true, 60) == written);  // an instrument that sounds while held: as written
    // Only the last note rings: the first is let go as written, since another is still to come.
    CHECK(off_after(two_notes, true, 60) == written);
    CHECK(off_after(two_notes, true, 62) > off_after(two_notes, false, 62) + 5);
}

TEST_CASE("audio: one effect ending cuts another copy of itself short, as the driver; not with ring-out") {
    const auto t = tables();
    // Channel 12, locked: a note of 30 ticks; the lock given back and the end at tick 10.
    const auto effect = bytes({0xBB, 110, 127, 0xCB, 80, 0x9B, 40, 100, 30, 10, 0xBB, 110, 0, 0xFF, 0x2F, 0});
    // The service calls, counted from the second copy's start, until both copies' notes have been let go. The
    // first copy started 6 calls before.
    const auto both_let_go_after = [&t, &effect](bool ours) {
        Recorder r;
        Sequencer sequencer{t, r, 100};
        const int first = sequencer.register_sequence(effect);
        const int second = sequencer.register_sequence(effect);
        sequencer.start(first, ours);
        for (int i = 0; i < 6; ++i) {
            sequencer.service();
        }
        sequencer.start(second, ours);
        int offs = 0;
        for (int services = 1; services <= 60; ++services) {
            r.messages.clear();
            sequencer.service();
            for (const Message& m : r.messages) {
                offs += (m.status & 0xF0U) == 0x80 && m.data1 == 40 ? 1 : 0;
            }
            if (offs == 2) {
                return services;
            }
        }
        return -1;
    };
    const int cut = both_let_go_after(false);
    const int whole = both_let_go_after(true);
    CHECK(cut > 0);
    CHECK(whole > cut);  // the second copy plays as long as the first did
}

TEST_CASE("audio: 460's and 463's sequencer tables are the same bytes", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR);
    }
    auto files = assets::GameFiles::open(LVR_ORIGINAL_DIR);
    REQUIRE(files);
    auto opl = assets::load_entry(files->archive(), 460);
    auto midi = assets::load_entry(files->archive(), 463);
    REQUIRE(opl);
    REQUIRE(midi);
    auto a = assets::parse_opl_driver(*opl);
    auto b = assets::parse_midi_driver(*midi);
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->sequencer.controllers == b->controllers);
    CHECK(a->sequencer.controller_defaults == b->controller_defaults);
    CHECK(a->sequencer.default_programs == b->default_programs);
    CHECK(b->controllers == std::array<std::uint8_t, 9>{7, 1, 10, 11, 64, 114, 110, 111, 112});
    CHECK(assets::parse_midi_driver(*opl).error() == assets::Error::kBadSize);
    auto wrong = *midi;
    wrong[0xDF] = std::byte{'X'};  // the description
    CHECK(assets::parse_midi_driver(wrong).error() == assets::Error::kBadReference);
}
