// Tests for src/lvr/session.*: the engine as a game to play, a picture and its sound for every refresh.
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "lvr/play.hpp"
#include "lvr/render.hpp"
#include "lvr/session.hpp"

using namespace lvr;

TEST_CASE("real data: a session shows a picture a refresh and renders their sound", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    CHECK_FALSE(Session::open("/nowhere", 1));
    CHECK_FALSE(Session::open(LVR_ORIGINAL_DIR, 48));
    auto opened = Session::open(LVR_ORIGINAL_DIR, 1);  // level 2: its password's box waits for a key
    REQUIRE(opened);
    Session& session = **opened;
    CHECK_FALSE(session.has_modern());  // no SoundFont was named
    session.set_modern(true);
    CHECK_FALSE(session.modern());
    CHECK(session.scene() == 1);
    CHECK_FALSE(session.demo());

    // The start: as many refreshes as the original's load and fades take, and their sound (two channels).
    CHECK(session.start_frames.size() == 145);
    const auto samples_a_refresh = static_cast<double>(Session::sample_rate()) / kDisplayHz;
    CHECK(static_cast<double>(session.samples.size()) > 2 * 144 * samples_a_refresh);
    CHECK(static_cast<double>(session.samples.size()) < 2 * 146 * samples_a_refresh);
    session.samples.clear();

    // The first step is a tick and the wait's first pass; after that a pass is three refreshes. The music is heard
    // by now.
    CHECK(session.step(0).size() == 6);
    const auto pass = session.step(0);
    CHECK(pass.size() == 3);
    CHECK(pass.back().pixels.size() == std::size_t{kScreenWidth} * kScreenHeight);
    for (int i = 0; i < 40; ++i) {
        (void)session.step(0);
    }
    CHECK(std::ranges::any_of(session.samples, [](std::int16_t sample) { return sample != 0; }));
    session.samples.clear();

    // The box answered, a tick; then Esc: the pause menu opens without a refresh, and its effect is heard with the
    // next step's.
    (void)session.step(0x8000);
    CHECK(session.step(0).size() == 3);
    CHECK(session.step(0x1000).empty());
    const auto in_menu = session.step(0);
    CHECK(in_menu.size() == 3);
    CHECK(in_menu.front().palette != pass.front().palette);  // the menu darkens the picture

    // The quit keys and "yes": the game is over, without an error.
    (void)session.step(0x1000);  // Esc: "no", the menu closes
    (void)session.step(0, {.yes = false, .no = false, .quit = true});
    session.key_pressed('Y');
    (void)session.step(0, {.yes = true, .no = false, .quit = false});
    CHECK(session.quit());
    CHECK(session.error().empty());
}

TEST_CASE(
    "session: live, a step makes no sound; each picture's requests are heard when given, and the sound is "
    "made as it is asked for",
    "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    auto opened = Session::open(LVR_ORIGINAL_DIR, 1, SessionSound{.modern = false, .soundfont = {}, .live = true});
    REQUIRE(opened);
    Session& session = **opened;
    // The start: a list of requests for each of its pictures, and no sound made yet.
    CHECK(session.samples.empty());
    REQUIRE(session.cues.size() == session.start_frames.size());
    // Asked for before anything was heard: silence, and exactly as much as was asked.
    std::vector<std::int16_t> made;
    session.render_live(1000, made);
    CHECK(made.size() == 2000);
    CHECK(std::ranges::all_of(made, [](std::int16_t sample) { return sample == 0; }));
    // The start's requests heard (the level's music among them), then two seconds asked for in small pieces.
    for (const auto& asked : session.cues) {
        session.hear_now(asked);
    }
    made.clear();
    for (int i = 0; i < 200; ++i) {
        session.render_live(497, made);
    }
    CHECK(made.size() == std::size_t{200} * 497 * 2);
    CHECK(std::ranges::any_of(made, [](std::int16_t sample) { return sample != 0; }));
    // A step: as many lists as pictures, and still nothing in `samples`.
    const auto shown = session.step(0);
    CHECK(session.cues.size() == shown.size());
    CHECK(session.samples.empty());
}

TEST_CASE("real data: a session started at the game's own start goes from the logos to the title", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    auto opened = Session::open(LVR_ORIGINAL_DIR, scene::kLogo);
    REQUIRE(opened);
    Session& session = **opened;
    std::size_t refreshes = 0;
    for (int step = 0; step < 200 && session.scene() != scene::kTitle; ++step) {
        refreshes += session.step(0).size();
    }
    CHECK(session.scene() == scene::kTitle);
    CHECK(refreshes > 2 * 145);  // two changes of scene on the way
    CHECK_FALSE(session.quit());
}
