// Tests for src/lvr/session.*: the engine as a game to play, a picture and its sound for every refresh.
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

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
    CHECK(session.rules() == Rules::kVanilla);
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
    // Under Remaster rules, which a session keeps from scene to scene.
    auto opened = Session::open(LVR_ORIGINAL_DIR, scene::kLogo, {}, Rules::kRemaster);
    REQUIRE(opened);
    Session& session = **opened;
    CHECK(session.rules() == Rules::kRemaster);
    std::size_t refreshes = 0;
    for (int step = 0; step < 200 && session.scene() != scene::kTitle; ++step) {
        refreshes += session.step(0).size();
    }
    CHECK(session.scene() == scene::kTitle);
    CHECK(refreshes > 2 * 145);  // two changes of scene on the way
    CHECK(session.rules() == Rules::kRemaster);
    CHECK_FALSE(session.quit());
}

TEST_CASE("real data: Remaster rules' zoom widens the view over a moment and closes it again; Vanilla has none",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    const Screen::Part original{};
    const auto part_after = [](Session& session, int steps) {
        Screen::Part part;
        for (int step = 0; step < steps; ++step) {
            for (const Screen& screen : session.step(0)) {
                part = screen.part;
            }
        }
        return part;
    };
    auto vanilla = Session::open(LVR_ORIGINAL_DIR, 1);
    REQUIRE(vanilla);
    (*vanilla)->zoom(2);
    CHECK(part_after(**vanilla, 10) == original);
    CHECK((*vanilla)->minimap() == nullptr);  // nor a minimap

    auto opened = Session::open(LVR_ORIGINAL_DIR, 1, {}, Rules::kRemaster);
    REQUIRE(opened);
    Session& session = **opened;
    CHECK(part_after(session, 2) == original);
    // The minimap: level 2 is 64 x 51 cells; the start's view was seen, the far corner not. It can be put away.
    REQUIRE(session.minimap() != nullptr);
    CHECK(session.minimap()->width == 64 * kMinimapCell);
    CHECK(session.minimap()->height == 51 * kMinimapCell);
    const std::vector<std::uint8_t> before = session.minimap()->pixels;
    session.toggle_minimap();
    CHECK(session.minimap() == nullptr);
    session.toggle_minimap();
    session.zoom(5);  // as far out as it goes: twice the original's view
    const Screen::Part opening = part_after(session, 2);
    CHECK(opening.width > original.width);
    CHECK(opening.width < 640);
    CHECK(part_after(session, 30) == Screen::Part{.x = 0, .y = 0, .width = 640, .height = 480});
    CHECK(session.minimap()->pixels != before);  // the larger view has shown more
    session.zoom(-1);
    const Screen::Part closing = part_after(session, 2);
    CHECK(closing.width < 640);
    CHECK(closing.width > 480);
    CHECK(part_after(session, 30) == Screen::Part{.x = 0, .y = 0, .width = 480, .height = 360});
    session.zoom(-5);
    CHECK(part_after(session, 30) == original);
}

TEST_CASE("real data: Remaster rules' rewind goes back at twice the speed, and the play goes on from there as before",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    // Level 2: its password's box is answered (a key pressed anew), then Erik walks right. Every step's pictures:
    using Pictures = std::vector<std::vector<std::uint8_t>>;
    const auto keys_of = [](int step) {
        return static_cast<std::uint16_t>(step < 6 ? (step % 2 != 0 ? key::kF : 0) : key::kRight);
    };
    const auto play = [&](Session& session, int from, int to, std::vector<Pictures>& steps) {
        for (int step = from; step < to; ++step) {
            Pictures pictures;
            for (const Screen& screen : session.step(keys_of(step))) {
                pictures.push_back(screen.pixels);
            }
            REQUIRE_FALSE(pictures.empty());
            steps.push_back(std::move(pictures));
        }
    };
    auto opened = Session::open(LVR_ORIGINAL_DIR, 1, {}, Rules::kRemaster);
    REQUIRE(opened);
    Session& session = **opened;
    session.toggle_minimap();
    std::vector<Pictures> steps;
    play(session, 0, 60, steps);
    CHECK(steps.at(8).back() != steps.at(59).back());  // he has moved
    CHECK_FALSE(session.rewinding());

    // Back: every step undoes two, and shows every second of their pictures, the last first: the first picture of
    // the later step, the second of the earlier one, and the play as it was before that (the last picture of the
    // step before).
    session.rewind(true);
    for (std::size_t back = 0; back < 5; ++back) {
        const std::vector<Screen> shown = session.step(0);
        REQUIRE(shown.size() == 3);
        CHECK(session.rewinding());
        const std::size_t later = 59 - (2 * back);
        REQUIRE(steps.at(later).size() == 3);
        REQUIRE(steps.at(later - 1).size() == 3);
        CHECK(shown.at(0).rewinding);  // marked as going back
        CHECK(shown.at(2).rewinding);
        CHECK(shown.at(0).pixels == steps.at(later).at(0));
        CHECK(shown.at(1).pixels == steps.at(later - 1).at(1));
        CHECK(shown.at(2).pixels == steps.at(later - 2).back());
    }
    // Let go, ten steps back: the same keys from there give the same play again.
    session.rewind(false);
    std::vector<Pictures> again;
    play(session, 50, 60, again);
    CHECK_FALSE(session.rewinding());
    CHECK(again.front() == steps.at(50));
    CHECK(again.back() == steps.at(59));

    // Held for longer than there is to go back to: at the oldest step that was kept the rewind is over, and the
    // play goes on, unmarked, until the key is let go and held again.
    session.rewind(true);
    int went_back = 0;
    std::vector<Screen> shown;
    for (int back = 0; back < 50; ++back) {
        shown = session.step(key::kRight);
        went_back += session.rewinding() ? 1 : 0;
    }
    CHECK(went_back > 20);
    CHECK(went_back < 50);
    CHECK_FALSE(session.rewinding());
    CHECK_FALSE(shown.back().rewinding);
    session.rewind(false);
    session.step(key::kRight);
    session.rewind(true);
    CHECK(session.step(key::kRight).back().rewinding);
    CHECK(session.rewinding());
    session.rewind(false);

    // Vanilla rules have no rewind: the play goes on.
    auto vanilla = Session::open(LVR_ORIGINAL_DIR, 1);
    REQUIRE(vanilla);
    std::vector<Pictures> plain;
    play(**vanilla, 0, 30, plain);
    (*vanilla)->rewind(true);
    play(**vanilla, 30, 40, plain);
    CHECK_FALSE((*vanilla)->rewinding());
    CHECK(plain.at(39) == steps.at(39));
}

TEST_CASE("real data: the quicksave keeps the play and brings it back, with the cheat on only", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    const auto keys_of = [](int step) {
        return static_cast<std::uint16_t>(step < 6 ? (step % 2 != 0 ? key::kF : 0) : key::kRight);
    };
    auto opened = Session::open(LVR_ORIGINAL_DIR, 1, {}, Rules::kRemaster);
    REQUIRE(opened);
    Session& session = **opened;
    std::vector<std::vector<std::uint8_t>> ends;
    for (int step = 0; step < 20; ++step) {
        ends.push_back(session.step(keys_of(step)).back().pixels);
    }
    // Without the cheat nothing is kept.
    session.quicksave();
    ends.push_back(session.step(keys_of(20)).back().pixels);
    CHECK_FALSE(session.has_quicksave());
    // With it: kept before step 21, and brought back 20 steps later; the play goes on from there as it did.
    session.cheat();
    session.quicksave();
    for (int step = 21; step < 41; ++step) {
        ends.push_back(session.step(keys_of(step)).back().pixels);
    }
    CHECK(session.has_quicksave());
    session.quickload();
    CHECK(session.step(keys_of(21)).back().pixels == ends.at(21));
    CHECK(session.step(keys_of(22)).back().pixels == ends.at(22));
    // What led up to the load isn't there to go back to.
    session.rewind(true);
    CHECK(session.step(0).back().pixels == ends.at(20));  // (the two steps since the load at once)
    CHECK(session.rewinding());
    session.step(0);
    CHECK_FALSE(session.rewinding());  // and then nothing
    session.rewind(false);

    // Vanilla rules have no quicksave, cheat or not.
    auto vanilla = Session::open(LVR_ORIGINAL_DIR, 1);
    REQUIRE(vanilla);
    (*vanilla)->cheat();
    (*vanilla)->quicksave();
    (*vanilla)->step(0);
    CHECK_FALSE((*vanilla)->has_quicksave());
}

TEST_CASE("real data: live, the sound of the rewound seconds is heard backwards", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    auto opened = Session::open(LVR_ORIGINAL_DIR, 1, SessionSound{.modern = false, .soundfont = {}, .live = true},
                                Rules::kRemaster);
    REQUIRE(opened);
    Session& session = **opened;
    for (const std::vector<SoundCue>& asked : session.cues) {  // the start's: the level's music
        session.hear_now(asked);
    }
    session.step(0);
    std::vector<std::int16_t> forwards;
    session.render_live(150000, forwards);  // three seconds
    REQUIRE(forwards.size() == 300000);
    CHECK(std::ranges::any_of(forwards, [](std::int16_t sample) { return sample != 0; }));
    // Going back: every second of the same frames, the last first, left before right as ever.
    session.rewind(true);
    session.step(0);
    REQUIRE(session.rewinding());
    std::vector<std::int16_t> backwards;
    session.render_live(1000, backwards);
    REQUIRE(backwards.size() == 2000);
    bool same = true;
    for (std::size_t frame = 0; frame < 1000; ++frame) {
        same = same && backwards[frame * 2] == forwards[(149999 - (2 * frame)) * 2] &&
               backwards[(frame * 2) + 1] == forwards[((149999 - (2 * frame)) * 2) + 1];
    }
    CHECK(same);
    // Past what was heard there is silence.
    std::vector<std::int16_t> rest;
    session.render_live(160000, rest);
    CHECK(rest.at(319998) == 0);
    CHECK(rest.at(319999) == 0);
}

TEST_CASE("real data: what the minimap has seen is kept in the profile from one run to the next", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "lvr-test-session-profile";
    std::filesystem::remove_all(folder);
    std::vector<std::uint8_t> zoomed_out;
    {
        auto opened = Session::open(LVR_ORIGINAL_DIR, 1, {}, Rules::kRemaster);
        REQUIRE(opened);
        Session& session = **opened;
        REQUIRE(session.use_profile(folder));
        session.zoom(2);  // the larger view shows more
        for (int step = 0; step < 30; ++step) {
            session.step(0);
        }
        REQUIRE(session.minimap() != nullptr);
        zoomed_out = session.minimap()->pixels;
        REQUIRE(session.save_profile());
    }
    CHECK(std::filesystem::exists(folder / "minimap.txt"));
    {
        // Another run: what the last one saw is there at once, though this one's view is the original's.
        auto opened = Session::open(LVR_ORIGINAL_DIR, 1, {}, Rules::kRemaster);
        REQUIRE(opened);
        Session& session = **opened;
        session.step(0);
        REQUIRE(session.minimap() != nullptr);
        CHECK(session.minimap()->pixels != zoomed_out);
        REQUIRE(session.use_profile(folder));
        REQUIRE(session.minimap() != nullptr);
        CHECK(session.minimap()->pixels == zoomed_out);
    }
    {
        // A file that can't be read: said, and the game goes on without it. Vanilla rules keep no profile.
        std::ofstream{folder / "minimap.txt"} << "rubbish";
        auto opened = Session::open(LVR_ORIGINAL_DIR, 1, {}, Rules::kRemaster);
        REQUIRE(opened);
        CHECK_FALSE((*opened)->use_profile(folder));
        (*opened)->step(0);
        CHECK((*opened)->minimap() != nullptr);
        REQUIRE((*opened)->save_profile());  // written over
        auto vanilla = Session::open(LVR_ORIGINAL_DIR, 1);
        REQUIRE(vanilla);
        std::filesystem::remove_all(folder);
        REQUIRE((*vanilla)->use_profile(folder));
        REQUIRE((*vanilla)->save_profile());
        CHECK_FALSE(std::filesystem::exists(folder));
    }
    std::filesystem::remove_all(folder);
}

TEST_CASE("real data: the quicksave is kept in the profile and brought back in another run", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "lvr-test-session-quicksave";
    std::filesystem::remove_all(folder);
    const auto keys_of = [](int step) {
        return static_cast<std::uint16_t>(step < 6 ? (step % 2 != 0 ? key::kF : 0) : key::kRight);
    };
    std::vector<std::vector<std::uint8_t>> ends;
    {
        auto opened = Session::open(LVR_ORIGINAL_DIR, 1, {}, Rules::kRemaster);
        REQUIRE(opened);
        Session& session = **opened;
        REQUIRE(session.use_profile(folder));
        session.cheat();
        for (int step = 0; step < 40; ++step) {
            if (step == 25) {
                session.quicksave();  // the play before step 25
            }
            ends.push_back(session.step(keys_of(step)).back().pixels);
        }
        CHECK(session.has_quicksave());
    }
    CHECK(std::filesystem::exists(folder / "quicksave.bin"));
    {
        // Another run, in another level: the quicksave is there, and brings back the level and the play.
        auto opened = Session::open(LVR_ORIGINAL_DIR, 4, {}, Rules::kRemaster);
        REQUIRE(opened);
        Session& session = **opened;
        CHECK_FALSE(session.has_quicksave());
        REQUIRE(session.use_profile(folder));
        REQUIRE(session.has_quicksave());
        session.step(0);
        session.quickload();  // without the cheat: nothing
        session.step(0);
        CHECK(session.scene() == 4);
        session.cheat();
        session.quickload();
        CHECK(session.step(keys_of(25)).back().pixels == ends.at(25));
        CHECK(session.scene() == 1);
        for (int step = 26; step < 40; ++step) {
            CHECK(session.step(keys_of(step)).back().pixels == ends.at(static_cast<std::size_t>(step)));
        }
    }
    {
        // A file that was damaged: said, and there is no quicksave.
        std::string bytes;
        {
            std::ifstream file{folder / "quicksave.bin", std::ios::binary};
            bytes.assign(std::istreambuf_iterator<char>{file}, {});
        }
        REQUIRE(bytes.size() > 2000);
        bytes[bytes.size() / 2] = static_cast<char>(bytes[bytes.size() / 2] ^ 0x55);
        std::ofstream{folder / "quicksave.bin", std::ios::binary} << bytes;
        auto opened = Session::open(LVR_ORIGINAL_DIR, 1, {}, Rules::kRemaster);
        REQUIRE(opened);
        CHECK_FALSE((*opened)->use_profile(folder));
        CHECK_FALSE((*opened)->has_quicksave());
    }
    std::filesystem::remove_all(folder);
}
