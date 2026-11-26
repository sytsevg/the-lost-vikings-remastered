// Tests for src/lvr/session.*: the engine as a game to play, a picture and its sound for every refresh.
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lvr/archive.hpp"
#include "lvr/names.hpp"
#include "lvr/play.hpp"
#include "lvr/render.hpp"
#include "lvr/session.hpp"
#include "lvr/showcase.hpp"

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
    CHECK_FALSE((*vanilla)->zoom(2));
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
    CHECK(session.zoom(5));  // as far out as it goes: twice the original's view
    // The level's password box waits for a key: a step is one picture for its three refreshes, but the view goes on
    // changing over all three.
    const std::vector<Screen> waiting = session.step(0);
    REQUIRE(waiting.size() == 3);
    CHECK(waiting[0].part.width < waiting[1].part.width);
    CHECK(waiting[1].part.width < waiting[2].part.width);
    const Screen::Part opening = part_after(session, 1);
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

TEST_CASE("real data: held by the menu, the play shows itself a little darker, moves on not at all, and zooms",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    auto played = Session::open(LVR_ORIGINAL_DIR, 1, {}, Rules::kRemaster);
    auto held = Session::open(LVR_ORIGINAL_DIR, 1, {}, Rules::kRemaster);
    REQUIRE(played);
    REQUIRE(held);
    constexpr std::uint16_t kF = 0x8000;  // past the level's password box
    for (Session* session : {played->get(), held->get()}) {
        (void)session->step(0);
        (void)session->step(kF);
        (void)session->step(0);
    }
    const Screen last = (*held)->step(0x0100).back();  // walking right
    (void)(*played)->step(0x0100);
    const std::vector<Screen> menu = (*held)->held();
    REQUIRE(menu.size() == 3);
    CHECK(menu.back().pixels == last.pixels);  // as it is
    CHECK(menu.back().palette != last.palette);
    // Darker by the original's menus' tint, but for the HUD's and the text's colours (1-15).
    CHECK(menu.back().palette == darkened(last.palette, kMenuTint, false));
    CHECK(menu.back().palette.at(1) == last.palette.at(1));
    for (int step = 0; step < 5; ++step) {
        (void)(*held)->held();
    }
    // Nothing moved meanwhile: the play goes on as if it never held.
    CHECK((*held)->step(0x0100).back().pixels == (*played)->step(0x0100).back().pixels);
    // A zoom asked for while it holds is shown at once.
    (*held)->set_zoom_level(2);
    Screen::Part part;
    for (int step = 0; step < 10; ++step) {
        part = (*held)->held().back().part;
    }
    CHECK(part == Screen::Part{.x = 0, .y = 0, .width = 640, .height = 480});
    CHECK((*held)->zoom_level() == 2);
}

TEST_CASE("real data: Remaster rules' pictures say what moves between ticks; Vanilla's don't", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    // Level 2: the password's box waits for a key; then Erik runs right.
    const auto run = [](Session& session) {
        std::vector<Screen> shown;
        for (int step = 0; step < 28; ++step) {
            shown = session.step(step == 16 ? key::kF : step > 16 ? key::kRight : std::uint16_t{0});
        }
        return shown;
    };
    auto vanilla = Session::open(LVR_ORIGINAL_DIR, 1);
    REQUIRE(vanilla);
    for (const Screen& screen : run(**vanilla)) {
        CHECK(screen.motion.phase == -1);
    }
    auto opened = Session::open(LVR_ORIGINAL_DIR, 1, {}, Rules::kRemaster);
    REQUIRE(opened);
    Session& session = **opened;
    // Waiting for the key: no tick, nothing moves.
    for (int step = 0; step < 4; ++step) {
        for (const Screen& screen : session.step(0)) {
            CHECK(screen.motion.phase == -1);
        }
    }
    const std::vector<Screen> tick = run(session);
    REQUIRE(tick.size() == 3);
    for (int phase = 0; phase < 3; ++phase) {
        const Motion& motion = tick.at(static_cast<std::size_t>(phase)).motion;
        CHECK(motion.phase == phase);
        CHECK(motion.scroll_from.has_value());  // the tick before was one too
        CHECK(motion.scroll_to == tick[2].motion.scroll);
        CHECK(motion.view == std::array<int, 2>{kScreenWidth, kPlayfieldHeight});
        // Erik runs right: his pieces move with him.
        CHECK(std::ranges::any_of(motion.sprites, [](const auto& moves) { return moves[0] > 0; }));
    }
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

TEST_CASE("real data: Remaster rules' rewind: ten seconds a level, given again when a level starts, shown in a bar",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    // Level 2: its password's box answered, and then the Vikings stand.
    const auto keys_of = [](int step) { return static_cast<std::uint16_t>(step < 6 && step % 2 != 0 ? key::kF : 0); };
    auto opened = Session::open(LVR_ORIGINAL_DIR, 1, {}, Rules::kRemaster);
    REQUIRE(opened);
    Session& session = **opened;
    // What the bar over the HUD shows of the rewind: how much is left (none: no bar).
    const auto bar = [](const Screen& screen) { return screen.rewind_left.value_or(-1.0F); };
    const auto back_for = [&session](int steps) {
        session.rewind(true);
        int went_back = 0;
        for (int step = 0; step < steps; ++step) {
            (void)session.step(0);
            went_back += session.rewinding() ? 1 : 0;
        }
        session.rewind(false);
        return went_back;
    };
    std::vector<Screen> shown;
    for (int step = 0; step < 100; ++step) {
        shown = session.step(keys_of(step));
    }
    CHECK(session.rewind_left() == Session::kRewindSteps);
    CHECK(bar(shown.back()) == 1);

    // Back no further than the level's start: its 100 steps, two a step.
    CHECK(back_for(60) == 50);
    CHECK(session.rewind_left() == Session::kRewindSteps - 100);
    CHECK(bar(session.step(0).back()) == static_cast<float>(Session::kRewindSteps - 100) / Session::kRewindSteps);
    // Played on for longer than is left (the password's box answered again): that much is gone back over, and then
    // there is none.
    for (int step = 0; step < 150; ++step) {
        (void)session.step(step == 1 ? key::kF : std::uint16_t{0});
    }
    CHECK(back_for(60) == 50);  // 99 steps
    CHECK(session.rewind_left() == 0);
    CHECK(bar(session.step(0).back()) == 0);
    CHECK(back_for(10) == 0);

    // A level started (here by the cheat's F5: the one before) has all of it, and nothing before its start.
    session.cheat();
    (void)session.step(0, Play::MenuKeys{.previous_level = true});
    shown = session.step(0);
    CHECK(session.scene() == 0);
    CHECK(session.rewind_left() == Session::kRewindSteps);
    REQUIRE(shown.size() > 145);
    REQUIRE(shown.at(10).rewind_left);  // (over the fades too, with the minimap: as dark as the picture)
    CHECK(shown.at(10).bright < 1.0F);
    CHECK(bar(shown.back()) == 1);
    CHECK(back_for(1) == 0);

    // Only in a level being played: not from the game-over scene back into the level it ends (given up in the pause
    // menu here).
    for (int step = 0; step < 20; ++step) {
        (void)session.step(keys_of(step));
    }
    (void)session.step(key::kEnter);
    (void)session.step(key::kLeft);  // "yes"
    (void)session.step(0);
    (void)session.step(key::kF);
    REQUIRE(session.scene() == scene::kGameOver);
    CHECK(back_for(5) == 0);
    CHECK_FALSE(session.step(0).back().rewind_left);  // and no bar
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
    CHECK_FALSE(session.quicksave());
    ends.push_back(session.step(keys_of(20)).back().pixels);
    CHECK_FALSE(session.has_quicksave());
    // With it: kept before step 21, and brought back 20 steps later; the play goes on from there as it did.
    session.cheat();
    CHECK(session.quicksave());
    for (int step = 21; step < 41; ++step) {
        ends.push_back(session.step(keys_of(step)).back().pixels);
    }
    CHECK(session.has_quicksave());
    CHECK(session.quickload());
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
    CHECK_FALSE((*vanilla)->quicksave());
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

TEST_CASE("real data: the cheat's next level key skips one level, even while it is held over the change",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    auto opened = Session::open(LVR_ORIGINAL_DIR, 1);
    REQUIRE(opened);
    Session& session = **opened;
    session.cheat();
    for (int step = 0; step < 24; ++step) {
        (void)session.step(step == 16 ? key::kF : std::uint16_t{0});
    }
    const Play::MenuKeys next{.next_level = true};
    (void)session.step(0, next);  // the level is over
    (void)session.step(0, next);  // the next one is loaded, the key still held: as the original, let go there
    CHECK(session.scene() == 2);
    (void)session.step(0);
    (void)session.step(0);
    CHECK(session.scene() == 2);
}

TEST_CASE("real data: Remaster rules' zoom is the player's: a level starts with it, the rewind keeps it",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    const std::array<int, 2> farthest{640, 480};
    auto opened = Session::open(LVR_ORIGINAL_DIR, 1, {}, Rules::kRemaster);
    REQUIRE(opened);
    Session& session = **opened;
    session.cheat();
    for (int step = 0; step < 24; ++step) {
        (void)session.step(step == 16 ? key::kF : std::uint16_t{0});
    }
    session.zoom(2);
    std::vector<Screen> shown;
    for (int step = 0; step < 30; ++step) {
        shown = session.step(key::kRight);
    }
    REQUIRE_FALSE(shown.empty());
    CHECK(shown.back().motion.view == farthest);
    // Back over the steps from before the zoom: still zoomed out.
    session.rewind(true);
    for (int step = 0; step < 20; ++step) {
        for (const Screen& screen : session.step(0)) {
            CHECK(screen.motion.view == farthest);
            CHECK(screen.part == Screen::Part{.x = 0, .y = 0, .width = 640, .height = 480});
        }
    }
    session.rewind(false);
    // (Back as far as the password's box: it waits for a key again.)
    (void)session.step(0);
    (void)session.step(key::kF);
    (void)session.step(0);
    // The next level comes in zoomed out. The minimap goes dark with the picture, the level's own as it goes out,
    // and the next level's comes in with it.
    (void)session.step(0, Play::MenuKeys{.next_level = true});
    REQUIRE(session.minimap() != nullptr);
    const Minimap going = *session.minimap();
    const std::vector<Screen> loading = session.step(0);
    REQUIRE(session.scene_loaded());
    REQUIRE(loading.size() > 140);
    CHECK(std::ranges::count_if(loading, [](const Screen& screen) { return screen.fading; }) > 140);
    CHECK_FALSE(loading.back().fading);
    const auto dark = std::ranges::find_if(loading, [](const Screen& screen) { return screen.bright <= 0.0F; });
    REQUIRE(dark != loading.end());
    CHECK(std::all_of(loading.begin(), dark,
                      [&going](const Screen& screen) { return screen.minimap && *screen.minimap == going; }));
    CHECK(loading.front().bright > 0.9F);
    CHECK(loading.front().bright > std::prev(dark)->bright);
    REQUIRE(loading.back().minimap);
    CHECK(*loading.back().minimap == *session.minimap());
    CHECK(*loading.back().minimap != going);
    CHECK(loading.back().bright == 1.0F);
    CHECK(session.minimap(loading.front()) == loading.front().minimap.get());
    session.toggle_minimap();  // (switched off: none over any picture)
    CHECK(session.minimap(loading.front()) == nullptr);
    session.toggle_minimap();
    CHECK(loading.back().motion.view == farthest);
    CHECK(loading.back().part == Screen::Part{.x = 0, .y = 0, .width = 640, .height = 480});
}

TEST_CASE("real data: the village has no minimap, zoom or rewind: they are the levels'", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    auto opened = Session::open(LVR_ORIGINAL_DIR, scene::kIntro, {}, Rules::kRemaster);
    REQUIRE(opened);
    Session& session = **opened;
    session.zoom(2);
    std::vector<Screen> shown;
    for (int step = 0; step < 40; ++step) {
        shown = session.step(0);
    }
    CHECK(session.minimap() == nullptr);
    REQUIRE_FALSE(shown.empty());
    CHECK(shown.back().motion.view == std::array<int, 2>{kScreenWidth, kPlayfieldHeight});
    session.rewind(true);
    (void)session.step(0);
    CHECK_FALSE(session.rewinding());
}

TEST_CASE("real data: Remaster rules' own keys act only in a level being played; elsewhere nothing is kept of them",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    // The village: no zoom, minimap or quicksave; the keys are the game's.
    auto village = Session::open(LVR_ORIGINAL_DIR, scene::kIntro, {}, Rules::kRemaster);
    REQUIRE(village);
    (*village)->cheat();
    CHECK_FALSE((*village)->takes_play_keys());
    CHECK_FALSE((*village)->zoom(2));
    CHECK_FALSE((*village)->toggle_minimap());
    CHECK_FALSE((*village)->quicksave());
    CHECK_FALSE((*village)->quickload());  // (nothing kept to bring back)

    // Level 2, its password's box answered: they act. In the pause menu they don't, and once the play goes on
    // the view and the minimap are as they were.
    auto opened = Session::open(LVR_ORIGINAL_DIR, 1, {}, Rules::kRemaster);
    REQUIRE(opened);
    Session& session = **opened;
    for (int step = 0; step < 4; ++step) {
        (void)session.step(step == 2 ? key::kF : std::uint16_t{0});
    }
    CHECK(session.takes_play_keys());
    (void)session.step(key::kEnter);
    CHECK_FALSE(session.takes_play_keys());
    CHECK_FALSE(session.zoom(2));
    CHECK_FALSE(session.toggle_minimap());
    (void)session.step(0);
    (void)session.step(key::kF);  // "no"
    std::vector<Screen> shown;
    for (int step = 0; step < 30; ++step) {
        shown = session.step(0);
    }
    CHECK(session.takes_play_keys());
    REQUIRE_FALSE(shown.empty());
    CHECK(shown.back().motion.view == std::array<int, 2>{kScreenWidth, kPlayfieldHeight});
    CHECK(session.minimap() != nullptr);

    // Vanilla rules have none of them.
    auto vanilla = Session::open(LVR_ORIGINAL_DIR, 1);
    REQUIRE(vanilla);
    CHECK_FALSE((*vanilla)->takes_play_keys());
}

TEST_CASE("real data: a scene fades out as it was last shown, with everyone in it", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    for (const Rules rules : {Rules::kVanilla, Rules::kRemaster}) {
        auto opened = Session::open(LVR_ORIGINAL_DIR, 1, {}, rules);
        REQUIRE(opened);
        Session& session = **opened;
        session.cheat();
        for (int step = 0; step < 4; ++step) {  // the password's box, answered
            (void)session.step(step == 2 ? key::kF : std::uint16_t{0});
        }
        for (int step = 0; step < 10; ++step) {
            (void)session.step(key::kRight);
        }
        // The level ends (the cheat's next level key, acted on after the tick); the next step fades it out.
        const std::vector<Screen> last = session.step(0, Play::MenuKeys{.next_level = true});
        REQUIRE_FALSE(last.empty());
        const std::vector<Screen> loading = session.step(0);
        REQUIRE(session.scene_loaded());
        // Its first picture (after the last one shown again for the refreshes the step has more than pictures)
        // isn't darkened yet: it is the last one shown, the Vikings and everything else still there, and stays so
        // as it darkens (the original's fade only darkens what its pages hold).
        const auto first = std::ranges::find_if(loading, [](const Screen& screen) { return screen.fading; });
        REQUIRE(first != loading.end());
        REQUIRE(std::distance(first, loading.end()) > 30);
        CHECK(first->pixels == last.back().pixels);
        CHECK(first->vikings.at(0).there);
        CHECK(first[30].vikings.at(0).there);
        CHECK(first[30].palette != last.back().palette);  // darker by then
    }
}

TEST_CASE("real data: development tools: to another level, and the view held on a spot there", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    auto opened = Session::open(LVR_ORIGINAL_DIR, 1, {}, Rules::kRemaster);
    REQUIRE(opened);
    Session& session = **opened;
    (void)session.step(0);
    CHECK(session.waiting_for_key());  // the password's box
    // Answered; then to level 5, which is started (and waits for its password) a step later.
    for (int step = 0; step < 6; ++step) {
        (void)session.step(step % 2 != 0 ? key::kF : std::uint16_t{0});
    }
    REQUIRE_FALSE(session.waiting_for_key());
    CHECK_FALSE(session.go_to(37));  // no such level
    CHECK(session.go_to(4));
    (void)session.step(0);
    (void)session.step(0);
    CHECK(session.scene() == 4);
    for (int step = 0; step < 6 && session.waiting_for_key(); ++step) {
        (void)session.step(step % 2 != 0 ? key::kF : std::uint16_t{0});
    }
    REQUIRE_FALSE(session.waiting_for_key());
    // The view held: as large as asked (with the HUD's rows under it), its corner where asked, and there it stays
    // while the Vikings walk off.
    REQUIRE(session.hold_view(200, 100, 384, 224));
    std::vector<Screen> shown;
    for (int step = 0; step < 30; ++step) {
        shown = session.step(key::kRight);
    }
    REQUIRE_FALSE(shown.empty());
    CHECK(shown.back().motion.view == std::array<int, 2>{384, 224 + 64});
    CHECK(shown.back().level_x == 200);
    CHECK(shown.back().level_y == 100);
}

TEST_CASE("real data: Remaster rules' effects: lights, glowing colours, what stops light, and particles",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    // Level 4, the view held on an electric barrier (it hums: a light along it, and sparks now and then); the same
    // play with the effects and without.
    auto with = Session::open(LVR_ORIGINAL_DIR, 3, {}, Rules::kRemaster);
    auto without = Session::open(LVR_ORIGINAL_DIR, 3, {}, Rules::kRemaster);
    REQUIRE(with);
    REQUIRE(without);
    (*with)->set_effects(true);
    std::vector<Screen> lit;
    std::vector<Screen> plain;
    bool sparks = false;
    for (int step = 0; step < 60; ++step) {
        const auto keys = static_cast<std::uint16_t>(step < 6 && step % 2 != 0 ? key::kF : 0);
        if (step == 8) {
            REQUIRE((*with)->hold_view(730, 328, 384, 224));
            REQUIRE((*without)->hold_view(730, 328, 384, 224));
        }
        lit = (*with)->step(keys);
        plain = (*without)->step(keys);
        REQUIRE(lit.size() == plain.size());
        sparks = sparks || lit.back().motion.sprites.size() > plain.back().motion.sprites.size();
    }
    const Screen& shown = lit.back();
    CHECK_FALSE(shown.lights.empty());
    CHECK(std::ranges::any_of(shown.glow, [](std::uint8_t glow) { return glow != 0; }));  // the screens' colours
    REQUIRE(shown.ground != nullptr);
    CHECK(std::ranges::any_of(shown.ground->solid, [](std::uint8_t solid) { return solid != 0; }));
    CHECK(sparks);
    // Without them: none of it.
    CHECK(plain.back().lights.empty());
    CHECK(std::ranges::none_of(plain.back().glow, [](std::uint8_t glow) { return glow != 0; }));
    CHECK(plain.back().ground == nullptr);
    // Vanilla rules have none, asked for or not.
    auto vanilla = Session::open(LVR_ORIGINAL_DIR, 3);
    REQUIRE(vanilla);
    (*vanilla)->set_effects(true);
    for (int step = 0; step < 12; ++step) {
        CHECK((*vanilla)
                  ->step(static_cast<std::uint16_t>(step < 6 && step % 2 != 0 ? key::kF : 0))
                  .back()
                  .lights.empty());
    }
}

TEST_CASE("real data: Remaster rules' effects: a Viking electrocuted or burning up gives off light while he dies",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    // The showcase's deaths: the same play twice, the Viking put there in the one. While he dies there is a light
    // at him that the other play doesn't have (where the lights are doesn't hang on their flicker): blue for
    // electricity, orange for fire. When he is dead it is gone.
    int deaths = 0;
    for (const Scenario& scenario : kScenarios) {
        if (!scenario.death) {
            continue;
        }
        ++deaths;
        const Death& death = *scenario.death;
        INFO(scenario.name);
        auto dies = Session::open(LVR_ORIGINAL_DIR, scenario.slot, {}, Rules::kRemaster);
        auto lives = Session::open(LVR_ORIGINAL_DIR, scenario.slot, {}, Rules::kRemaster);
        REQUIRE(dies);
        REQUIRE(lives);
        (*dies)->set_effects(true);
        (*lives)->set_effects(true);
        // A light of `shown` at the Viking (in the level), at a place where `other` has none.
        const auto his_light = [](const Screen& shown, const Screen& other, const Screen::Viking& viking) {
            const auto at = [](const Light& light, float x, float y) {
                return std::abs(light.x - x) < 1 && std::abs(light.y - y) < 1;
            };
            const auto found = std::ranges::find_if(shown.lights, [&](const Light& light) {
                return std::abs(light.x - static_cast<float>(viking.x - shown.level_x)) < 24 &&
                       std::abs(light.y - static_cast<float>(viking.y - shown.level_y)) < 24 &&
                       std::ranges::none_of(
                           other.lights, [&](const Light& their) { return at(their, light.x, light.y); });
            });
            return found == shown.lights.end() ? std::optional<Light>{} : std::optional<Light>{*found};
        };
        std::optional<Light> seen;
        bool gone = false;
        for (int step = 0; step < 90; ++step) {
            const auto keys = static_cast<std::uint16_t>(step < 6 && step % 2 != 0 ? key::kF : 0);
            if (step == 8) {
                REQUIRE((*dies)->hold_view(scenario.x, scenario.y, kShowcaseWidth, kShowcaseHeight));
                REQUIRE((*lives)->hold_view(scenario.x, scenario.y, kShowcaseWidth, kShowcaseHeight));
            }
            if (step == 10) {
                REQUIRE((*dies)->put_viking(death.viking, death.x, death.y, 1));
                CHECK_FALSE((*dies)->put_viking(3, death.x, death.y, 1));
            }
            const std::vector<Screen> shown = (*dies)->step(keys);
            const std::vector<Screen> other = (*lives)->step(keys);
            REQUIRE(shown.size() == other.size());
            const Screen::Viking& viking = shown.back().vikings.at(static_cast<std::size_t>(death.viking));
            const auto light = his_light(shown.back(), other.back(), viking);
            if (step < 10) {
                CHECK_FALSE(light);
            } else if (!seen) {
                seen = light;
            } else if (step > 80) {
                gone = !light;
            }
        }
        REQUIRE(seen);
        const bool fire = scenario.name.find("burning") != std::string_view::npos;
        CHECK((fire ? seen->colour[0] > seen->colour[2] : seen->colour[2] > seen->colour[0]));
        CHECK(gone);
    }
    CHECK(deaths == 5);
}

TEST_CASE("real data: the game's state as bytes for one joining online: short, taken anywhere, the same play on",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    const auto keys_of = [](int step) {
        return static_cast<std::uint16_t>(step < 6 ? (step % 2 != 0 ? key::kF : 0) : key::kRight);
    };
    auto opened = Session::open(LVR_ORIGINAL_DIR, 1, {}, Rules::kRemaster);
    REQUIRE(opened);
    Session& session = **opened;
    for (int step = 0; step < 30; ++step) {
        session.step(keys_of(step));
    }
    const Play::Snapshot snapshot = session.snapshot();
    const std::vector<std::uint8_t> bytes = Session::state_bytes(snapshot);
    std::vector<std::uint8_t> fixed;
    Writer to{fixed};
    snapshot.world.save(to);
    CHECK(bytes.size() < fixed.size());  // (mostly small numbers: about 26 KB, against 31 in fixed widths)
    CHECK(Session::scene_of(bytes) == std::optional<int>{session.world().scene});

    // Taken by a session in another scene: the same state, and the same play from there.
    auto other = Session::open(LVR_ORIGINAL_DIR, 5, {}, Rules::kRemaster);
    REQUIRE(other);
    REQUIRE((*other)->take_state(bytes));
    // (It hears the level's music from its first step on, as a quickload does: cues the step takes out.)
    REQUIRE((*other)->world().cues.size() == 2);
    CHECK((*other)->world().cues[1].kind == SoundCue::Kind::kMusicAction);
    for (int step = 30; step < 50; ++step) {
        CHECK((*other)->step(keys_of(step)).back().pixels == session.step(keys_of(step)).back().pixels);
    }
    CHECK((*other)->state_hash() == session.state_hash());

    // Cut short, or of another version: not taken, the play as it was.
    const std::uint64_t before = (*other)->state_hash();
    const std::vector<std::uint8_t> cut(bytes.begin(), bytes.end() - 10);
    CHECK_FALSE((*other)->take_state(cut));
    std::vector<std::uint8_t> another = bytes;
    another.front() = static_cast<std::uint8_t>(kStateVersion + 1);
    CHECK_FALSE((*other)->take_state(another));
    CHECK_FALSE(Session::scene_of(another).has_value());
    CHECK((*other)->state_hash() == before);
}

TEST_CASE("real data: Remaster rules' effects hold still under the original's pause box, as the world does",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    // Level 4, the view held on an electric barrier: its light flickers and it sparks, a picture at a time.
    auto opened = Session::open(LVR_ORIGINAL_DIR, 3, {}, Rules::kRemaster);
    REQUIRE(opened);
    Session& session = **opened;
    session.set_effects(true);
    for (int step = 0; step < 30; ++step) {
        if (step == 8) {
            REQUIRE(session.hold_view(730, 328, 384, 224));
        }
        session.step(static_cast<std::uint16_t>(step < 6 && step % 2 != 0 ? key::kF : 0));
    }
    // Whether the lights or the sparks change from one picture to the next, over 40 steps (a step that shows no
    // picture of its own, as the one the pause box opens in, leaves the one before).
    Screen last = session.step(0).back();
    const auto lights_change = [&] {
        bool changed = false;
        for (int step = 0; step < 40; ++step) {  // (the box's word blinks every 16th step: a picture anew)
            for (Screen& now : session.step(0)) {
                changed =
                    changed || now.lights != last.lights || now.motion.sprites.size() != last.motion.sprites.size();
                last = std::move(now);
            }
        }
        return changed;
    };
    CHECK(lights_change());
    // The pause box (Enter; the tick it opens in still moves the world): from its first picture on nothing moves,
    // though the box's word blinks (were it not open, they would).
    CHECK(session.step(key::kEnter).empty());
    last = session.step(0).back();
    CHECK_FALSE(lights_change());
}

TEST_CASE("real data: Remaster rules' title, offline, has HOST GAME and JOIN GAME before QUIT TO DOS", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    // To the title's menu (Enter, the screen dims and comes back with it), then the keys given.
    const auto at_menu = [](Rules rules) {
        auto opened = Session::open(LVR_ORIGINAL_DIR, scene::kTitle, {}, rules);
        REQUIRE(opened);
        for (int step = 0; step < 50; ++step) {
            (*opened)->step(step == 20 ? key::kEnter : 0);
        }
        return std::move(*opened);
    };
    const auto press = [](Session& session, std::uint16_t keys) {
        session.step(keys);
        session.step(0);
    };
    auto remaster = at_menu(Rules::kRemaster);
    press(*remaster, key::kDown);
    press(*remaster, key::kDown);
    remaster->step(key::kEnter);
    CHECK(remaster->world().title.online == 1);  // HOST GAME: for the front end, one tick
    remaster->step(0);
    CHECK(remaster->world().title.online == 0);
    press(*remaster, key::kDown);
    remaster->step(key::kEnter);
    CHECK(remaster->world().title.online == 2);  // JOIN GAME
    press(*remaster, key::kDown);
    remaster->step(key::kEnter);  // QUIT TO DOS, the last
    remaster->step(0);            // (the game ends as the next step starts)
    CHECK(remaster->quit());
    // Up from the first goes to the last.
    auto around = at_menu(Rules::kRemaster);
    press(*around, key::kUp);
    around->step(key::kEnter);
    around->step(0);  // (the game ends as the next step starts)
    CHECK(around->quit());

    // Vanilla rules: the original's three.
    auto vanilla = at_menu(Rules::kVanilla);
    press(*vanilla, key::kDown);
    press(*vanilla, key::kDown);
    vanilla->step(key::kEnter);
    CHECK(vanilla->world().title.online == 0);
    vanilla->step(0);  // (the game ends as the next step starts)
    CHECK(vanilla->quit());
}

TEST_CASE("real data: Remaster rules: the menu's QUIT GAME isn't held up by a text waiting for its key",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    auto opened = Session::open(LVR_ORIGINAL_DIR, 0, {}, Rules::kRemaster);
    REQUIRE(opened);
    Session& session = **opened;
    for (int step = 0; step < 200 && !session.waiting_for_key(); ++step) {
        (void)session.step(0);
    }
    REQUIRE(session.waiting_for_key());  // (the level's opening lines)
    Play::MenuKeys quit;
    quit.quit = true;
    (void)session.step(0, quit);  // the quit box, the text put away
    Play::MenuKeys yes;
    yes.yes = true;
    for (int step = 0; step < 20 && !session.quit(); ++step) {
        (void)session.step(0, yes);
    }
    CHECK(session.quit());
}

TEST_CASE("real data: Remaster rules: the GAME OVER letters are seen as they fall, as under Vanilla rules",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    std::vector<std::uint8_t> vanilla;
    for (const Rules rules : {Rules::kVanilla, Rules::kRemaster}) {
        auto opened = Session::open(LVR_ORIGINAL_DIR, scene::kGameOver, {}, rules);
        REQUIRE(opened);
        Session& session = **opened;
        for (int step = 0; step < 300; ++step) {  // (until "try again?" is asked)
            (void)session.step(0);
        }
        session.key_pressed('N');
        std::vector<Screen> pictures;
        for (int step = 0; step < 80; ++step) {
            pictures = session.step(0);
        }
        REQUIRE_FALSE(pictures.empty());
        if (rules == Rules::kVanilla) {
            vanilla = pictures.back().pixels;
        } else {  // (the letters, packed, from the front pool: they had no frame buffer and weren't drawn)
            CHECK(pictures.back().pixels == vanilla);
        }
    }
}
