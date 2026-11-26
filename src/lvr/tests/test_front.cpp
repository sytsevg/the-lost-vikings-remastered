// Tests for src/lvr/front.* (the places, GLOSSARY.md; ADR-0012): machines with the real game on the simulated
// network, each played a refresh at a time as the window's loop plays its place, its menu without a window. A walk
// through every move: hosting and joining into the lobby, a game, back to the lobby, leaving it and joining again,
// another game, one joining it under way, and the host gone.
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "lvr/archive.hpp"
#include "lvr/front.hpp"
#include "lvr/names.hpp"
#include "net/simulated.hpp"

using namespace lvr;

namespace {

// One machine: its title's session (Offline's), its places, the menu of the place it shows.
struct Machine {
    std::unique_ptr<Session> title;
    std::unique_ptr<OfflinePlace> offline;
    Settings settings;
    std::unique_ptr<Places> places;
    std::optional<Menu> kept;  // the menu of the place it shows, when open
    std::unique_ptr<Ui> ui;
    Place* shown = nullptr;                    // the place the menu is for
    int due = 0;                               // refreshes until its next step
    Screen last;                               // the last picture it showed
    std::vector<Screen>* shown_all = nullptr;  // when given: every picture it shows, added to it
    std::uint64_t turns = 0;                   // its refreshes so far (the places time themselves by them)
    bool paused = false;                       // it has no turns (its machine stopped)

    Place& place() { return places->current(); }
    template <typename P>
    P* at() {
        return dynamic_cast<P*>(&place());
    }
    // The menu of the place it shows now: begun as the window's loop begins a place.
    Ui& menu() {
        if (&place() != shown) {
            shown = &place();
            kept.reset();
            ui = std::make_unique<Ui>(headless_ui(kept, place(), settings, [this] { return turns; }));
            place().begin(*ui);
        }
        return *ui;
    }
    // A refresh: what came, and every third (every one while it waits) the next pictures.
    void turn() {
        ++turns;
        Ui& shown_menu = menu();
        Go go = place().refresh(shown_menu);
        if (go.to == Go::To::kStay && --due < 0) {
            const Stepped stepped = place().step(Tick{}, shown_menu);
            due = stepped.frames.size() > 1 ? 2 : 0;
            go = stepped.go;
            if (!stepped.frames.empty()) {
                last = stepped.frames.back();
            }
            if (shown_all != nullptr) {
                shown_all->insert(shown_all->end(), stepped.frames.begin(), stepped.frames.end());
            }
        }
        places->go(go);
    }
    // What the menu is asked, as a key in it asks it: the place's answer, and the menu closed unless kept.
    void ask(Menu::Asked asked) {
        Ui& shown_menu = menu();
        Menu& open = shown_menu.menu() != nullptr ? *shown_menu.menu() : shown_menu.open_menu();
        const Place::Answer answer = place().asked(asked, open);
        if (!answer.handled) {
            kept.reset();
        }
        places->go(answer.go);
    }
    // A key in its menu, as the window's loop gives it: the menu's answer, the place's to it.
    void press(Menu::Key key) {
        Ui& shown_menu = menu();
        Menu& open = shown_menu.menu() != nullptr ? *shown_menu.menu() : shown_menu.open_menu();
        const Menu::Asked asked = open.press(key);
        const Place::Answer answer = place().asked(asked, open);
        if (!answer.handled && asked != Menu::Asked::kNothing) {
            kept.reset();
        }
        places->go(answer.go);
    }
    // Typed into the line its menu waits on.
    void type(std::string_view text) {
        Menu& open = *menu().menu();
        open.take_text(text);
        open.stop_waiting();
    }
    // Its hashes of the game online's state, by step (what the machines compare).
    std::map<std::uint32_t, std::string> hashes() {
        std::map<std::uint32_t, std::string> by_step;
        auto* online = at<OnlinePlace>();
        std::istringstream lines{online != nullptr ? online->log() : std::string{}};
        std::string word;
        while (lines >> word) {
            if (word == "hash") {
                std::uint32_t step = 0;
                std::string hash;
                lines >> step >> hash;
                by_step[step] = hash;
            }
        }
        return by_step;
    }
};

struct Room {
    net::SimulatedNetwork network{
        {.delay_ms = 20, .jitter_ms = 10, .loss_percent = 10, .late_ms = 100, .behind_ms = 0}};
    std::vector<std::unique_ptr<Machine>> machines;

    Machine& machine(const std::string& name) {
        auto made = std::make_unique<Machine>();
        auto opened = Session::open(LVR_ORIGINAL_DIR, scene::kTitle, {}, Rules::kRemaster);
        REQUIRE(opened);
        made->title = std::move(*opened);
        made->offline = std::make_unique<OfflinePlace>(*made->title, nullptr, true);
        made->settings.name = name;
        made->settings.join_address = "host:47230";
        Places::Needs needs{
            .online = {.dir = LVR_ORIGINAL_DIR, .sound = {}, .draw = draw, .logs = {}},
            .settings = &made->settings,
            .identity = {.build = "test", .game = 1, .state_version = kStateVersion, .name = {}},
            .host = [this](std::uint16_t) -> std::expected<std::unique_ptr<net::Transport>, std::string> {
                return network.host();
            },
            .join = [this](const std::string&, std::uint16_t,
                           const std::atomic<bool>&) -> std::expected<std::unique_ptr<net::Transport>, std::string> {
                auto joined = network.join();
                if (joined == nullptr) {
                    return std::unexpected{"no host"};
                }
                return joined;
            }};
        made->places = std::make_unique<Places>(*made->offline, std::move(needs));
        machines.push_back(std::move(made));
        return *machines.back();
    }
    // Refreshes pass, each machine having its turn, until `done` (at most a minute of them).
    bool until(const std::function<bool()>& done) {
        constexpr int kMost = 3600;
        for (int refresh = 0; refresh < kMost && !done(); ++refresh) {
            for (auto& one : machines) {
                if (one != nullptr && !one->paused) {
                    one->turn();
                }
            }
            network.advance(17);
        }
        return done();
    }
    // Every machine there in its game online at `steps` steps or more.
    bool online_to(std::uint32_t steps) {
        return until([&] {
            return std::ranges::all_of(machines, [&](const auto& one) {
                return one == nullptr || (one->template at<OnlinePlace>() != nullptr &&
                                          one->template at<OnlinePlace>()->at_step() >= steps);
            });
        });
    }
    // Whether two machines' games are the same, compared at the steps both hashed (at least three).
    static bool same_game(Machine& one, Machine& other) {
        const auto mine = one.hashes();
        const auto theirs = other.hashes();
        int compared = 0;
        for (const auto& [step, hash] : mine) {
            if (const auto found = theirs.find(step); found != theirs.end()) {
                if (found->second != hash) {
                    return false;
                }
                ++compared;
            }
        }
        return compared >= 3;
    }
};

// A machine's picture as its window shows it at the original's size, with `over` (red, green, blue, alpha) laid
// over it, as a PNG in gamedata/previews/online/.
void save_still(const Machine& machine, const std::vector<std::uint8_t>& over, const std::string& name) {
    std::vector<std::uint8_t> rgba(std::size_t{kScreenWidth} * kScreenHeight * 4);
    for (std::size_t i = 0; i < machine.last.pixels.size(); ++i) {
        const assets::Rgb c = machine.last.palette.at(machine.last.pixels[i]);
        const std::array<std::uint8_t, 3> under{c.r, c.g, c.b};
        const unsigned alpha = over.empty() ? 0U : over.at((i * 4) + 3);
        for (std::size_t part = 0; part < 3; ++part) {
            const unsigned top = over.empty() ? 0U : over.at((i * 4) + part);
            rgba.at((i * 4) + part) =
                static_cast<std::uint8_t>(((top * alpha) + (under.at(part) * (255 - alpha))) / 255);
        }
        rgba.at((i * 4) + 3) = 255;
    }
    const std::filesystem::path folder = std::filesystem::path{LVR_ORIGINAL_DIR}.parent_path() / "previews" / "online";
    std::filesystem::create_directories(folder);
    SDL_Surface* surface =
        SDL_CreateSurfaceFrom(kScreenWidth, kScreenHeight, SDL_PIXELFORMAT_RGBA32, rgba.data(), kScreenWidth * 4);
    REQUIRE(surface != nullptr);
    CHECK(SDL_SavePNG(surface, (folder / name).string().c_str()));
    SDL_DestroySurface(surface);
}

}  // namespace

// Run by hand when the online parts' look changes (lvr_tests "[scratch]"): stills of the lobby's page, the names under
// the Vikings' portraits, and the box saying whom the game waits for.
TEST_CASE("online stills: the lobby, the names under the portraits, the waiting box", "[.scratch]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    Room room;
    Machine& host = room.machine("host");
    Machine& guest = room.machine("guest");
    REQUIRE(room.until([&] { return host.title->world().scene == scene::kTitle; }));
    host.ask(Menu::Asked::kHost);
    guest.ask(Menu::Asked::kJoin);
    (void)guest.place().refresh(guest.menu());  // (connecting: JOIN GAME's page, the mark turning beside JOIN)
    save_still(guest, menu_picture(*guest.menu().menu(), guest.place().session().font()), "joining.png");
    REQUIRE(room.until([&] { return guest.at<LobbyPlace>() != nullptr && !guest.last.pixels.empty(); }));
    // The host types level 1's password (as the game's files have it) and starts it.
    const auto& password = host.title->world().passwords.at(0);
    host.press(Menu::Key::kChoose);
    host.type(std::string(password.begin(), password.end()));
    REQUIRE(room.until([&] { return host.menu().menu()->start().starts_with("LEVEL 1"); }));
    save_still(host, menu_picture(*host.menu().menu(), host.place().session().font()), "lobby-host.png");
    save_still(guest, menu_picture(*guest.menu().menu(), guest.place().session().font()), "lobby-guest.png");
    host.press(Menu::Key::kDown);
    host.press(Menu::Key::kChoose);  // START GAME
    REQUIRE(room.online_to(60));
    save_still(guest, {}, "names.png");
    // The host's machine stops: the guest waits for him, and says so.
    host.paused = true;
    REQUIRE(room.until([&] { return !guest.place().overlay().waiting.empty(); }));
    save_still(guest, coop_picture(guest.place().overlay(), guest.place().session().font()), "waiting.png");
}

TEST_CASE(
    "real data: a game online rises from black as the original's level does, and goes dark to the lobby, the minimap "
    "with it",
    "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    Room room;
    Machine& host = room.machine("host");
    Machine& guest = room.machine("guest");
    REQUIRE(room.until([&] { return host.title->world().scene == scene::kTitle; }));
    host.ask(Menu::Asked::kHost);
    guest.ask(Menu::Asked::kJoin);
    REQUIRE(room.until([&] { return guest.at<LobbyPlace>() != nullptr && !guest.last.pixels.empty(); }));
    const auto& password = host.title->world().passwords.at(0);
    host.press(Menu::Key::kChoose);
    host.type(std::string(password.begin(), password.end()));
    REQUIRE(room.until([&] { return host.menu().menu()->start().starts_with("LEVEL 1"); }));
    host.press(Menu::Key::kDown);
    host.press(Menu::Key::kChoose);  // START GAME
    // The level's start, from the lobby gone dark (the user, 2026-11-26): the original's start's tick and fade in, the
    // start's picture rising from black, all of it (the minimap goes by Screen::bright) in step; not two seconds of
    // black with the minimap on it, nor a rise of its own after them.
    REQUIRE(room.until([&] { return host.at<OnlinePlace>() != nullptr; }));
    const std::vector<Screen>& start = host.place().session().start_frames;
    REQUIRE(start.size() == 75);
    CHECK(start.front().bright == 0);
    CHECK(start.front().palette == assets::Palette{});
    CHECK(start.back().bright == 1);
    CHECK(start.front().minimap != nullptr);
    CHECK(std::ranges::is_sorted(start, {}, &Screen::bright));
    REQUIRE(room.online_to(60));
    std::vector<Screen> shown;
    host.shown_all = &shown;
    CHECK(room.until([&] { return shown.size() >= 3; }));
    CHECK(std::ranges::all_of(shown, [](const Screen& picture) { return picture.bright == 1; }));
    // Back to the lobby: the game goes dark, the minimap with it; all dark, it has none, as the lobby that comes.
    shown.clear();
    host.ask(Menu::Asked::kLobby);
    REQUIRE(room.until([&] { return host.at<LobbyPlace>() != nullptr; }));
    host.shown_all = nullptr;
    const auto dark = std::ranges::find_if(shown, [](const Screen& picture) { return picture.bright == 0; });
    REQUIRE(dark != shown.end());
    CHECK(dark->minimap == nullptr);
    CHECK(std::ranges::is_sorted(shown.begin(), dark, std::ranges::greater{}, &Screen::bright));
    CHECK(dark - shown.begin() >= 20);  // (a moment)
}

TEST_CASE(
    "real data: the places, every move: host and join, a game, back to the lobby, leave and join again, "
    "another game joined under way, the host gone",
    "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    Room room;
    Machine& host = room.machine("host");
    Machine& guest = room.machine("guest");
    REQUIRE(room.until([&] { return host.title->world().scene == scene::kTitle; }));

    // HOST GAME: the lobby at once. JOIN GAME: connecting, then the lobby.
    host.ask(Menu::Asked::kHost);
    CHECK(host.at<LobbyPlace>() != nullptr);
    guest.ask(Menu::Asked::kJoin);
    CHECK(guest.at<ConnectingPlace>() != nullptr);
    REQUIRE(room.until([&] { return guest.at<LobbyPlace>() != nullptr; }));
    CHECK(guest.menu().menu() != nullptr);
    CHECK(guest.menu().menu()->in_lobby());

    // START: after the countdown, everyone in the game (a new one: the intro).
    host.ask(Menu::Asked::kStart);
    CHECK(host.at<LobbyPlace>() != nullptr);  // (the countdown first)
    REQUIRE(room.online_to(60));
    CHECK(Room::same_game(host, guest));
    CHECK(guest.at<OnlinePlace>()->session().scene() == scene::kIntro);

    // RETURN TO LOBBY: everyone there again. The guest leaves it (the title, nothing said), and joins again.
    host.ask(Menu::Asked::kLobby);
    CHECK(host.at<OnlinePlace>() != nullptr);  // (the game fades out first)
    REQUIRE(room.until([&] { return host.at<LobbyPlace>() != nullptr && guest.at<LobbyPlace>() != nullptr; }));
    guest.ask(Menu::Asked::kLeave);
    CHECK(guest.at<OfflinePlace>() != nullptr);
    CHECK(guest.menu().menu() == nullptr);
    guest.ask(Menu::Asked::kJoin);
    REQUIRE(room.until([&] { return guest.at<LobbyPlace>() != nullptr; }));

    // Another game; a third joins it under way, straight into it.
    host.ask(Menu::Asked::kStart);
    REQUIRE(room.online_to(40));
    Machine& late = room.machine("late");
    late.ask(Menu::Asked::kJoin);
    REQUIRE(room.until([&] { return late.at<OnlinePlace>() != nullptr; }));
    REQUIRE(room.online_to(160));
    CHECK(Room::same_game(host, guest));
    CHECK(Room::same_game(host, late));

    // The host gone: the others are back at their title, the JOIN GAME page saying why.
    room.machines.front().reset();
    REQUIRE(room.until([&] { return guest.at<OfflinePlace>() != nullptr && late.at<OfflinePlace>() != nullptr; }));
    for (Machine* one : {&guest, &late}) {
        const Menu* page = one->menu().menu();
        REQUIRE(page != nullptr);
        CHECK(page->of_title());
    }
}

TEST_CASE("real data: a join that fails: the mark turns two seconds, why beside JOIN four, then JOIN GAME's page again",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    Room room;
    Machine& guest = room.machine("guest");  // (nobody hosts)
    guest.ask(Menu::Asked::kJoin);
    const auto join_line = [&] {
        const Menu* page = guest.menu().menu();
        REQUIRE(page != nullptr);
        const auto lines = page->lines();
        const auto join = std::ranges::find(lines, std::string{"JOIN"}, &Menu::Line::label);
        REQUIRE(join != lines.end());
        return *join;
    };
    const std::uint32_t began = room.network.now();
    const auto after = [&](std::uint32_t ms) { return room.until([&] { return room.network.now() >= began + ms; }); };
    REQUIRE(after(1000));  // a second: still turning
    CHECK(guest.at<ConnectingPlace>() != nullptr);
    CHECK(join_line().turning.has_value());
    CHECK(join_line().dim);  // (greyed while it joins)
    REQUIRE(after(3000));    // three seconds: why
    CHECK(join_line().value == "NO GAME THERE");
    REQUIRE(room.until([&] { return guest.at<OfflinePlace>() != nullptr; }));
    CHECK(room.network.now() >= began + 6000);
    CHECK(join_line().value.empty());  // JOIN GAME's page again, to try once more
}
