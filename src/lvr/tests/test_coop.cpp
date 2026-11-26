// Tests for src/lvr/coop.* (co-op online, ADR-0012): the game itself under rollback, a step's input as bytes, and a
// game online on the real game: a host and a guest play level 1 over a network that delays and loses what is sent,
// and one who joins under way comes to the same game.
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include "lvr/coop.hpp"
#include "lvr/names.hpp"
#include "lvr/session.hpp"
#include "net/rollback.hpp"
#include "net/tests/table.hpp"

using namespace lvr;

namespace {

constexpr std::uint16_t kF = 0x8000;
constexpr std::uint16_t kRight = 0x0100;
constexpr std::uint16_t kLeft = 0x0200;
constexpr std::uint16_t kNext = 0x0010;
constexpr std::uint16_t kInventory = 0x2000;

// What each player holds at each step: the host answers the level's opening boxes, then both walk, jump, switch,
// and the second looks into his inventory.
std::uint16_t held(int player, std::uint32_t step) {
    if (player == 0) {
        if (step < 80) {
            return step % 10 == 5 ? kF : 0;  // each box of the opening, answered
        }
        if (step < 140) {
            return static_cast<std::uint16_t>(kRight | (step % 30 == 0 ? kF : 0));
        }
        return step % 20 < 10 ? kLeft : 0;
    }
    if (step >= 90 && step < 120) {
        return kLeft;
    }
    if (step == 125) {
        return kInventory;
    }
    if (step == 128 || step == 131) {
        return kRight;  // the cursor, in his inventory
    }
    if (step == 134) {
        return kInventory;
    }
    return step == 150 ? kNext : 0;
}

// The real game at a game online's table (net/tests/table.hpp): a session opened as the game starts (under way: at
// the state given), with rollback's view of it; each step's input what `held` says for its player.
struct Opened {
    std::unique_ptr<Session> session;
};
struct RealGame : Opened, CoopGame {
    explicit RealGame(std::unique_ptr<Session> opened) : Opened{std::move(opened)}, CoopGame{*session} {}

    static std::unique_ptr<RealGame> made(const net::Started& started) {
        const int slot = started.state.empty() ? started.slot : Session::scene_of(started.state).value_or(-1);
        auto opened = Session::open(LVR_ORIGINAL_DIR, slot, {}, Rules::kRemaster, draw,
                                    Online{.on = true, .players = started.players}, started.player);
        REQUIRE(opened);
        if (!started.state.empty()) {
            REQUIRE((*opened)->take_state(started.state));
        }
        return std::make_unique<RealGame>(std::move(*opened));
    }
    static net::Input input(RealGame& game, int player, std::uint32_t given) {
        return pack(game.session->take_input(held(player, given), {}, {}), player);
    }
};

}  // namespace

TEST_CASE("real data: one who joins level 1 under way comes to the very game the host and the guest play",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    net::test::Table<RealGame> table{
        {.delay_ms = 20, .jitter_ms = 10, .loss_percent = 10, .late_ms = 100, .behind_ms = 0}};
    auto& host = table.host();
    auto& guest = table.join("guest");
    REQUIRE(table.until([&] { return guest.lobby->welcomed(); }));
    host.lobby->start();
    REQUIRE(table.until([&] { return host.match != nullptr && guest.match != nullptr; }));
    REQUIRE(table.run_to(100));

    // A third comes into the free place: Olaf, whom nobody played.
    auto& late = table.join("late");
    REQUIRE(table.until([&] { return late.match != nullptr; }));
    CHECK(late.player() == 2);
    REQUIRE(table.run_to(200));
    table.wait(60);
    CHECK(table.same_game());
    const World& world = late.game->session->world();
    CHECK(world.players[2].here);
    CHECK(world.players[2].viking == 2);
    CHECK(host.game->session->world().players[2].here);
    // What they did happened: the host's Erik walked from where level 1 puts him, and the guest's Baleog.
    auto fresh = Session::open(LVR_ORIGINAL_DIR, 0, {}, Rules::kRemaster);
    REQUIRE(fresh);
    CHECK(host.game->session->world().at(0)->x != (*fresh)->world().at(0)->x);
    CHECK(host.game->session->world().at(1)->x != (*fresh)->world().at(1)->x);
    CHECK(host.connection->names().at(2) == "LATE");
    CHECK(guest.connection->names().at(2) == "LATE");
}

TEST_CASE("coop: a step's input as bytes and back; what is pressed once isn't predicted again") {
    StepInput input;
    input.players[0] = {
        .keys = 0x8100, .pace = Pace{.across = 8, .up_down = 16}, .zoom = {480, 360}, .view = {480, 360}};
    input.players[1] = {.keys = 0x0200, .pace = {}, .zoom = {320, 176}, .view = {}};
    input.typed = 'Y';
    input.menu.yes = true;
    input.menu.next_level = true;
    const std::vector<net::Input> packed{pack(input, 0), pack(input, 1)};
    const StepInput back = unpack(packed);
    CHECK(back.players[0].keys == 0x8100);
    CHECK(back.players[0].pace.across == 8);
    CHECK(back.players[0].zoom == std::array<std::int16_t, 2>{480, 360});
    CHECK(back.players[1].keys == 0x0200);
    CHECK(back.players[1].view == std::array<std::int16_t, 2>{0, 0});
    CHECK(back.typed == 'Y');
    CHECK(back.menu.yes);
    CHECK(back.menu.next_level);
    CHECK_FALSE(back.menu.quit);
    CHECK(pack(input, 1).bytes[12] == 0);  // only the host's has them
}

TEST_CASE("coop: a player gone is so in the step's input") {
    const std::vector<net::Input> inputs{net::Input{}, net::gone_input()};
    const StepInput input = unpack(inputs);
    CHECK_FALSE(input.players[0].gone);
    CHECK(input.players[1].gone);
}

TEST_CASE("real data: online, the intro is the first player's to play, as the recording has it; the others watch",
          "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    auto alone = Session::open(LVR_ORIGINAL_DIR, scene::kIntro, {}, Rules::kRemaster);
    auto guest =
        Session::open(LVR_ORIGINAL_DIR, scene::kIntro, {}, Rules::kRemaster, draw, Online{.on = true, .players = 2}, 1);
    REQUIRE(alone);
    REQUIRE(guest);
    const World& world = (*guest)->world();
    CHECK(world.attract);
    CHECK(world.players[1].viking == kNobody);  // no Viking of his own ...
    for (int step = 0; step < 300; ++step) {
        (*alone)->step(0);
        (*guest)->step(static_cast<std::uint16_t>(kRight | kF));  // ... and what he presses is nothing meanwhile
        REQUIRE(world.players[1].lines.empty());                  // what is said is everyone's
    }
    // The Vikings as with one player; his view where the first player's is, on the Viking the recording plays.
    for (int slot = 0; slot < 3; ++slot) {
        CHECK(world.at(slot)->x == (*alone)->world().at(slot)->x);
        CHECK(world.at(slot)->y == (*alone)->world().at(slot)->y);
    }
    CHECK(world.followed_by(world.players[1]) == world.players[0].viking);
    CHECK(world.players[1].camera.x == world.players[0].camera.x);
    CHECK(world.players[1].camera.y == world.players[0].camera.y);
}
