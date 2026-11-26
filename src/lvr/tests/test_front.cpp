// Tests for src/lvr/front.* (the places, GLOSSARY.md; ADR-0012): machines with the real game on the simulated
// network, each played a refresh at a time as the window's loop plays its place, its menu without a window. A walk
// through every move: hosting and joining into the lobby, a game, back to the lobby, leaving it and joining again,
// another game, one joining it under way, and the host gone.
#include <algorithm>
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
    std::unique_ptr<HeadlessUi> ui;
    Place* shown = nullptr;  // the place the menu is for
    int due = 0;             // refreshes until its next step

    Place& place() { return places->current(); }
    template <typename P>
    P* at() {
        return dynamic_cast<P*>(&place());
    }
    // The menu of the place it shows now: begun as the window's loop begins a place.
    HeadlessUi& menu() {
        if (&place() != shown) {
            shown = &place();
            ui = std::make_unique<HeadlessUi>(place(), settings);
            place().begin(*ui);
        }
        return *ui;
    }
    // A refresh: what came, and every third (every one while it waits) the next pictures.
    void turn() {
        HeadlessUi& shown_menu = menu();
        Go go = place().refresh(shown_menu);
        if (go.to == Go::To::kStay && --due < 0) {
            const Stepped stepped = place().step(Tick{}, shown_menu);
            due = stepped.frames.size() > 1 ? 2 : 0;
            go = stepped.go;
        }
        places->go(go);
    }
    // What the menu is asked, as a key in it asks it: the place's answer, and the menu closed unless kept.
    void ask(Menu::Asked asked) {
        HeadlessUi& shown_menu = menu();
        Menu& open = shown_menu.menu() != nullptr ? *shown_menu.menu() : shown_menu.open_menu();
        const Place::Answer answer = place().asked(asked, open);
        if (!answer.handled) {
            shown_menu.close();
        }
        places->go(answer.go);
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
            .join = [this](const std::string&,
                           std::uint16_t) -> std::expected<std::unique_ptr<net::Transport>, std::string> {
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
                if (one != nullptr) {
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

}  // namespace

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
    CHECK(host.at<LobbyPlace>() != nullptr);
    REQUIRE(room.until([&] { return guest.at<LobbyPlace>() != nullptr; }));
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
