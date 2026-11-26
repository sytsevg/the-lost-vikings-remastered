// Tests for src/replay/golden.*: the comparison on hand-made players, the goldens' format, and [real-data] the
// game's session against the player. (That the engine plays each replay as its golden has it is goldens.sh's to say.)
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "lvr/render.hpp"
#include "lvr/session.hpp"
#include "replay/golden.hpp"
#include "replay/player.hpp"
#include "replay/replay.hpp"

using namespace lvr::replay;

namespace {

Refresh picture(std::uint8_t colour) {
    return Refresh{.pixels = std::vector<std::uint8_t>(kWidth * kHeight, colour), .palette = {}, .sounds = {}};
}

// Shows the refreshes it was given, then ends, or stops with an error.
class Scripted : public Player {
  public:
    explicit Scripted(std::vector<Refresh> refreshes, std::string error = {})
        : refreshes_{std::move(refreshes)}, error_{std::move(error)} {}

    std::expected<std::optional<Refresh>, std::string> next() override {
        if (at_ < refreshes_.size()) {
            return refreshes_[at_++];
        }
        if (!error_.empty()) {
            return std::unexpected{error_};
        }
        return std::nullopt;
    }
    std::string entities() override { return "at " + std::to_string(at_); }

  private:
    std::vector<Refresh> refreshes_;
    std::string error_;
    std::size_t at_ = 0;
};

}  // namespace

TEST_CASE("match: two runs that show the same") {
    Scripted first{{picture(1), picture(2), picture(3)}};
    Scripted second{{picture(1), picture(2), picture(3)}};
    const Result result = compare(first, second);
    CHECK(result.matched());
    CHECK(result.refreshes == 3);
}

TEST_CASE("match: the first refresh whose pixels differ") {
    Refresh other = picture(2);
    other.pixels[5] = 9;
    other.pixels[kWidth] = 9;
    Scripted first{{picture(1), picture(2), picture(3)}};
    Scripted second{{picture(1), other, picture(4)}};
    const Result result = compare(first, second);
    REQUIRE_FALSE(result.matched());
    CHECK(result.refreshes == 1);
    const Difference& difference = *result.difference;
    CHECK(difference.refresh == 1);
    CHECK(difference.picture);
    CHECK(difference.pixels == 2);
    CHECK_FALSE(difference.palette);
    CHECK_FALSE(difference.sounds);
    CHECK_FALSE(difference.length);
    CHECK(difference.first_entities == "at 2");
    CHECK(describe(difference) == "refresh 1: 2 pixels differ;");
    REQUIRE(difference.first);
    REQUIRE(difference.second);
    const std::vector<std::uint8_t> mask = difference_mask(*difference.first, *difference.second);
    CHECK(mask[5] == 1);
    CHECK(mask[kWidth] == 1);
    CHECK(mask[6] == 0);
}

TEST_CASE("match: the palette and the sounds are compared too") {
    Refresh recoloured = picture(1);
    recoloured.palette[3] = {.r = 1, .g = 2, .b = 3};
    Scripted first{{picture(1)}};
    Scripted second{{recoloured}};
    const Result palette = compare(first, second);
    REQUIRE_FALSE(palette.matched());
    CHECK(palette.difference->palette);
    CHECK_FALSE(palette.difference->picture);
    CHECK(describe(*palette.difference) == "refresh 0: the palettes differ;");

    Refresh loud = picture(1);
    loud.sounds = {Sound{.kind = Sound::Kind::kEffect, .value = 7},
                   Sound{.kind = Sound::Kind::kMusicAction, .value = 0, .tune = 2}};
    Scripted third{{picture(1)}};
    Scripted fourth{{loud}};
    const Result sounds = compare(third, fourth);
    REQUIRE_FALSE(sounds.matched());
    CHECK(sounds.difference->sounds);
    CHECK(describe(*sounds.difference) == "refresh 0: sounds: first (none), second effect 7, music action 0 tune 2;");
}

TEST_CASE("match: a run that ends early, or stops with an error") {
    Scripted first{{picture(1), picture(2)}};
    Scripted second{{picture(1)}};
    const Result shorter = compare(first, second);
    REQUIRE_FALSE(shorter.matched());
    CHECK(shorter.refreshes == 1);
    CHECK(shorter.difference->length);
    CHECK(shorter.difference->first);
    CHECK_FALSE(shorter.difference->second);
    CHECK(describe(*shorter.difference) == "refresh 1: the second ended, the other goes on");

    Scripted third{{picture(1)}, "it fell over"};
    Scripted fourth{{picture(1), picture(2)}};
    const Result stopped = compare(third, fourth);
    REQUIRE_FALSE(stopped.matched());
    CHECK(stopped.difference->length);
    CHECK(describe(*stopped.difference) == "refresh 1: stopped, first: it fell over");
    CHECK(stopped.difference->second);
}

TEST_CASE("match: the difference mask of pictures of different sizes") {
    Refresh small;
    small.pixels = {1, 2};
    const std::vector<std::uint8_t> mask = difference_mask(small, picture(1));
    CHECK(mask[0] == 0);
    CHECK(mask[1] == 1);
    CHECK(mask[2] == 1);
    Scripted first{{small}};
    Scripted second{{picture(1)}};
    const Result result = compare(first, second);
    REQUIRE_FALSE(result.matched());
    CHECK(result.difference->pixels == (kWidth * kHeight) - 1);
}

TEST_CASE("match: a refresh's hash covers pixels, palette and sounds; sounds in words") {
    const Refresh plain = picture(1);
    Refresh pixel = plain;
    pixel.pixels[0] = 2;
    Refresh colour = plain;
    colour.palette[255].b = 1;
    Refresh sound = plain;
    sound.sounds.push_back(Sound{.kind = Sound::Kind::kStopEffect, .value = 0x102});
    CHECK(hash(plain) == hash(picture(1)));
    CHECK(hash(plain) != hash(pixel));
    CHECK(hash(plain) != hash(colour));
    CHECK(hash(plain) != hash(sound));

    CHECK(describe(Sound{.kind = Sound::Kind::kStopEffect, .value = 4}) == "stop effect 4");
    CHECK(describe(Sound{.kind = Sound::Kind::kRestartMusic}) == "restart music");
    CHECK(describe(Sound{.kind = Sound::Kind::kFadeMusic}) == "fade music");
    CHECK(describe(Sound{.kind = Sound::Kind::kStopAll, .value = 1}) == "stop all (1)");
    CHECK(describe(Sound{.kind = Sound::Kind::kEffectsOn, .value = 0}) == "effects off");
    CHECK(describe(Sound{.kind = Sound::Kind::kMusicOn, .value = 1}) == "music on");
}

TEST_CASE("golden: a run as a hash per refresh, written and read back") {
    Scripted run{{picture(1), picture(2), picture(2)}};
    const auto hashes = record(run);
    REQUIRE(hashes);
    REQUIRE(hashes->size() == 3);
    CHECK(hashes->at(0) == hash(picture(1)));
    CHECK(hashes->at(1) == hashes->at(2));
    const std::string text = format_golden(*hashes);
    CHECK(text == std::format("{:08x}\n{:08x} 2\n", hashes->at(0), hashes->at(1)));
    CHECK(parse_golden(text) == *hashes);
    CHECK(format_golden({}).empty());
    CHECK(parse_golden("000000ff\r\n\n10 3\n7") == std::vector<std::uint32_t>{0xff, 0x10, 0x10, 0x10, 7});
    const auto broken = parse_golden("1\nxyz\n");
    REQUIRE_FALSE(broken);
    CHECK(broken.error() == "line 2: not a hash");
    CHECK(parse_golden("1x\n").error() == "line 1: not a hash");
    CHECK(parse_golden("100000000\n").error() == "line 1: not a hash");  // more than 32 bits
    CHECK(parse_golden("1\n2 0\n").error() == "line 2: not a run's length");
    CHECK(parse_golden("1 2x\n").error() == "line 1: not a run's length");
    CHECK(parse_golden("1 99999999999\n").error() == "line 1: not a run's length");

    Scripted stops{{picture(1)}, "it fell over"};
    const auto stopped = record(stops);
    REQUIRE_FALSE(stopped);
    CHECK(stopped.error() == "refresh 1: it fell over");

    CHECK_FALSE(first_difference({1, 2, 3}, {1, 2, 3}));
    CHECK(first_difference({1, 2, 3}, {1, 5, 3}) == 1);
    CHECK(first_difference({1, 2, 3}, {1, 2}) == 2);
    CHECK(first_difference({}, {7}) == 0);
}

TEST_CASE("the game's session shows what the pixel-matched player shows, refresh by refresh", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    // Level 2's replay: the password's box, walking, the inventory, a Viking switch.
    const auto replay = load_replay(std::filesystem::path{LVR_REPLAYS_DIR} / "level-02.replay");
    REQUIRE(replay);
    auto player = LvrPlayer::open(LVR_ORIGINAL_DIR, *replay);
    auto session = lvr::Session::open(LVR_ORIGINAL_DIR, replay->slot);
    REQUIRE(player);
    REQUIRE(session);
    std::vector<lvr::Screen> shown = std::move((*session)->start_frames);
    for (const Step& step : replay->steps) {
        std::vector<lvr::Screen> frames = (*session)->step(step.keyboard);
        shown.insert(shown.end(), std::make_move_iterator(frames.begin()), std::make_move_iterator(frames.end()));
    }
    std::size_t same = 0;
    for (const lvr::Screen& screen : shown) {
        const auto refresh = (*player)->next();
        REQUIRE(refresh);
        REQUIRE(*refresh);
        if ((*refresh)->pixels != screen.pixels || (*refresh)->palette != screen.palette) {
            break;
        }
        ++same;
    }
    CHECK(same == shown.size());
    CHECK(same > 1500);
    const auto after = (*player)->next();
    REQUIRE(after);
    CHECK_FALSE(*after);  // and the player has no more
}
