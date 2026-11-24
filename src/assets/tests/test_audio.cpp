// Tests for src/assets/audio.*: hand-made XMIDI files and timbre banks for each rule, and [real-data] checks of
// all 63 XMIDI entries and the instrument banks against the counts in docs/formats/audio.md.
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <string_view>
#include <vector>

#include "assets/assets.hpp"
#include "assets/audio.hpp"

using namespace lvr::assets;

namespace {

const lvr::assets::GameFiles& game_or_skip() {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    static const auto files = lvr::assets::GameFiles::open(LVR_ORIGINAL_DIR);
    if (!files) {
        FAIL(files.error());
    }
    return *files;
}

std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (const int v : values) {
        out.push_back(static_cast<std::byte>(v));
    }
    return out;
}

void append(std::vector<std::byte>& out, const std::vector<std::byte>& more) {
    out.insert(out.end(), more.begin(), more.end());
}

// A chunk: id, big-endian size, body, a padding byte when odd.
std::vector<std::byte> chunk(std::string_view id, const std::vector<std::byte>& body) {
    std::vector<std::byte> out;
    for (const char c : id) {
        out.push_back(static_cast<std::byte>(c));
    }
    const auto size = static_cast<std::uint32_t>(body.size());
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::byte>((size >> static_cast<unsigned>(shift)) & 0xFFU));
    }
    append(out, body);
    if ((body.size() & 1U) != 0) {
        out.push_back(std::byte{0});
    }
    return out;
}

std::vector<std::byte> group(std::string_view kind, std::string_view type, const std::vector<std::byte>& body) {
    std::vector<std::byte> inner;
    for (const char c : type) {
        inner.push_back(static_cast<std::byte>(c));
    }
    append(inner, body);
    return chunk(kind, inner);
}

std::vector<std::byte> xmidi(const std::vector<std::vector<std::byte>>& sequences) {
    std::vector<std::byte> forms;
    for (const auto& sequence : sequences) {
        append(forms, group("FORM", "XMID", sequence));
    }
    std::vector<std::byte> out = group("FORM", "XDIR", chunk("INFO", bytes({static_cast<int>(sequences.size()), 0})));
    append(out, group("CAT ", "XMID", forms));
    return out;
}

}  // namespace

TEST_CASE("audio: an XMIDI sequence's instruments and events, delays added up, notes with durations") {
    std::vector<std::byte> sequence = chunk("TIMB", bytes({2, 0, 5, 0, 36, 127}));
    append(sequence, chunk("EVNT", bytes({
                                       0xC1, 5,                       // program change, channel 1
                                       0x7F, 0x01,                    // 127 + 1 ticks
                                       0x91, 60,   100,  0x81, 0x00,  // note on, 128 ticks long
                                       0xB9, 110,  127,               // controller
                                       0x05,                          //
                                       0xFF, 0x01, 2,    'h',  'i',   // a text
                                       0xE2, 0x00, 0x40,              // pitch bend
                                       0x10, 0xFF, 0x2F, 0x00,        // end of track at 149
                                   })));
    std::vector<std::byte> second = chunk("EVNT", bytes({0xFF, 0x2F, 0x00, 0x00}));  // a padding byte after the end
    const auto parsed = parse_xmidi(xmidi({sequence, second}));
    REQUIRE(parsed);
    REQUIRE(parsed->size() == 2);
    const XmidiSequence& s = parsed->front();
    CHECK(s.timbres == std::vector<XmidiTimbre>{{.patch = 5, .bank = 0}, {.patch = 36, .bank = 127}});
    REQUIRE(s.events.size() == 5);
    CHECK(s.events[0] == XmidiEvent{.tick = 0, .status = 0xC1, .data1 = 5});
    CHECK(s.events[1] == XmidiEvent{.tick = 128, .status = 0x91, .data1 = 60, .data2 = 100, .duration = 128});
    CHECK(s.events[2] == XmidiEvent{.tick = 128, .status = 0xB9, .data1 = 110, .data2 = 127});
    CHECK(s.events[3].tick == 133);
    CHECK(s.events[3].data1 == 0x01);
    CHECK(s.events[3].payload == bytes({'h', 'i'}));
    CHECK(s.events[4] == XmidiEvent{.tick = 133, .status = 0xE2, .data1 = 0x00, .data2 = 0x40});
    CHECK(s.end == 149);
    CHECK(parsed->back().events.empty());
    CHECK(parsed->back().timbres.empty());
}

TEST_CASE("audio: malformed XMIDI is an error") {
    const auto with_events = [](std::initializer_list<int> events) {
        return parse_xmidi(xmidi({chunk("EVNT", bytes(events))}));
    };
    CHECK(with_events({0x90, 60}).error() == Error::kBadEvent);                             // cut short
    CHECK(with_events({0x90, 60, 1, 0x80}).error() == Error::kBadEvent);                    // duration cut short
    CHECK(with_events({0x90, 60, 1, 0xFF, 0xFF, 0xFF, 0xFF}).error() == Error::kBadEvent);  // duration too long
    CHECK(with_events({0xF0, 1}).error() == Error::kBadEvent);                              // SysEx
    CHECK(with_events({0x10}).error() == Error::kBadEvent);                                 // no end of track
    CHECK(with_events({0xFF, 0x01, 5, 'a'}).error() == Error::kBadEvent);                   // meta cut short
    CHECK(with_events({0xB0, 7}).error() == Error::kBadEvent);
    CHECK(with_events({0xC0}).error() == Error::kBadEvent);
    CHECK(parse_xmidi(xmidi({chunk("TIMB", bytes({2, 0, 1}))})).error() == Error::kBadChunk);
    CHECK(parse_xmidi(xmidi({chunk("TEXT", bytes({1, 2}))})).error() == Error::kBadChunk);  // no EVNT
    CHECK(parse_xmidi(bytes({'F', 'O', 'R', 'M', 0, 0, 0, 4, 'X', 'Y', 'Z', 'W'})).error() == Error::kBadChunk);
    CHECK(parse_xmidi(bytes({'F', 'O', 'R'})).error() == Error::kBadChunk);
    std::vector<std::byte> no_cat = group("FORM", "XDIR", chunk("INFO", bytes({1, 0})));
    CHECK(parse_xmidi(no_cat).error() == Error::kBadChunk);
    append(no_cat, group("CAT ", "XMID", {}));  // INFO says 1, CAT holds none
    CHECK(parse_xmidi(no_cat).error() == Error::kBadChunk);
    std::vector<std::byte> no_info = group("FORM", "XDIR", chunk("NOPE", bytes({1, 0})));
    CHECK(parse_xmidi(no_info).error() == Error::kBadChunk);
    std::vector<std::byte> wrong_form = group("FORM", "XDIR", chunk("INFO", bytes({1, 0})));
    append(wrong_form, group("CAT ", "XMID", group("FORM", "XXXX", {})));
    CHECK(parse_xmidi(wrong_form).error() == Error::kBadChunk);
}

TEST_CASE("audio: a timbre bank's records and timbres") {
    // two records, the terminator, two timbres (sizes counting their own size field)
    const std::vector<std::byte> bank = bytes({1,    0,    20,   0,    0, 0,        // patch 1, bank 0 at 20
                                               2,    127,  24,   0,    0, 0,        // patch 2, bank 127 at 24
                                               0xFF, 0xFF, 0,    0,    0, 0, 0, 0,  // the terminator, then filler
                                               4,    0,    0xAA, 0xBB,              // at 20
                                               3,    0,    0xCC});                  // at 24
    const auto parsed = parse_timbre_bank(bank);
    REQUIRE(parsed);
    REQUIRE(parsed->size() == 2);
    CHECK(parsed->at(0).patch == 1);
    CHECK(parsed->at(0).data == bytes({4, 0, 0xAA, 0xBB}));
    CHECK(parsed->at(1).bank == 127);
    CHECK(parsed->at(1).data == bytes({3, 0, 0xCC}));
    CHECK(parse_timbre_bank(bytes({1, 0, 20})).error() == Error::kTruncated);
    CHECK(parse_timbre_bank(bytes({1})).error() == Error::kTruncated);
    CHECK(parse_timbre_bank(bytes({1, 0, 99, 0, 0, 0})).error() == Error::kOffsetOutOfRange);
    CHECK(parse_timbre_bank(bytes({1, 0, 6, 0, 0, 0, 1, 0})).error() == Error::kBadSize);
    CHECK(parse_timbre_bank(bytes({1, 0, 6, 0, 0, 0, 9, 0})).error() == Error::kOffsetOutOfRange);
}

TEST_CASE("audio: every XMIDI entry parses, with the counts found in the format survey", "[real-data]") {
    const GameFiles& files = game_or_skip();
    std::vector<std::size_t> entries;
    for (std::size_t entry = 464; entry <= 528; ++entry) {
        if (entry < 524 || entry == 524 || entry == 528) {
            entries.push_back(entry);
        }
    }
    entries.push_back(533);
    REQUIRE(entries.size() == 63);
    std::size_t sequences = 0;
    std::map<unsigned, std::size_t> kinds;
    std::size_t tempos = 0;
    for (const std::size_t entry : entries) {
        INFO("entry " << entry);
        const auto data = load_entry(files.archive(), entry);
        REQUIRE(data);
        const auto parsed = parse_xmidi(*data);
        REQUIRE(parsed);
        sequences += parsed->size();
        for (const XmidiSequence& sequence : *parsed) {
            for (const XmidiEvent& event : sequence.events) {
                ++kinds[event.status == 0xFF ? 0xFFU : event.status & 0xF0U];
                tempos += event.status == 0xFF && event.data1 == 0x51 ? 1 : 0;
            }
        }
    }
    CHECK(sequences == 544);
    CHECK(kinds[0x90] == 54'949);
    CHECK(kinds[0xE0] == 12'456);
    CHECK(kinds[0xB0] == 9'827);
    CHECK(kinds[0xC0] == 1'002);
    CHECK(kinds[0xA0] == 8);
    CHECK(kinds[0x80] == 0);
    CHECK(tempos == 539);
    const auto tune2 = parse_xmidi(*load_entry(files.archive(), 466));  // tune 2, OPL3: 6,866-6,895 ticks long
    REQUIRE(tune2);
    CHECK(tune2->front().end >= 6'866);
    CHECK(tune2->front().end <= 6'895);
}

TEST_CASE("audio: the instrument banks", "[real-data]") {
    const GameFiles& files = game_or_skip();
    const auto opl = parse_timbre_bank(*load_entry(files.archive(), 526));
    REQUIRE(opl);
    CHECK(opl->size() == 276);
    std::map<std::size_t, std::size_t> sizes;
    for (const Timbre& timbre : *opl) {
        ++sizes[timbre.data.size()];
    }
    CHECK(sizes[25] == 44);
    CHECK(sizes[14] == 276 - 44);
    CHECK(*load_entry(files.archive(), 525) == *load_entry(files.archive(), 526));
    const auto mt32 = parse_timbre_bank(*load_entry(files.archive(), 527));
    REQUIRE(mt32);
    CHECK(mt32->size() == 64);
    CHECK(mt32->front().data.size() == 248);
}
