// Tests for src/net/rollback.*: GGPO's way of keeping machines in step, on the toy game, its inputs given
// by hand: predictions put right, the window, the delay, who is there when, out of step. (Over a network, with the
// host in the middle: test_match.cpp.)
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <random>
#include <span>
#include <utility>
#include <vector>

#include "net/rollback.hpp"
#include "net/tests/toy.hpp"

using namespace lvr::net;

using lvr::net::test::held;
using lvr::net::test::Toy;

TEST_CASE("rollback: alone, every step runs once, with nothing to wait for or run again") {
    Toy game;
    Rollback rollback{game, Settings{.players = 1, .local = 0, .delay = 0}};
    for (std::uint32_t step = 0; step < 50; ++step) {
        REQUIRE(rollback.add_local(held(0, step)));
        REQUIRE(rollback.advance());
    }
    CHECK(rollback.step() == 50);
    CHECK(rollback.confirmed() == 50);
    CHECK(rollback.steps_run_again() == 0);
    CHECK_FALSE(rollback.advance());  // no input for the next yet
}

TEST_CASE("rollback: a remote input that comes otherwise than predicted has the steps from it run again") {
    Toy alone;  // the same inputs, all known: what the state must come to
    Toy game;
    Rollback rollback{game, Settings{.players = 2, .local = 0, .delay = 0}};
    Rollback straight{alone, Settings{.players = 2, .local = 0, .delay = 0}};
    for (std::uint32_t step = 0; step < 6; ++step) {
        REQUIRE(rollback.add_local(held(0, step)));
        REQUIRE(rollback.advance());  // the second player's predicted: his first (none yet), then the same
        REQUIRE(straight.add_local(held(0, step)));
        straight.add_remote(1, step, held(1, 10 + step));
        REQUIRE(straight.advance());
    }
    for (std::uint32_t step = 0; step < 6; ++step) {
        rollback.add_remote(1, step, held(1, 10 + step));
    }
    REQUIRE(rollback.add_local(held(0, 6)));
    straight.add_remote(1, 6, held(1, 16));
    REQUIRE(straight.add_local(held(0, 6)));
    REQUIRE(rollback.advance());
    REQUIRE(straight.advance());
    CHECK(rollback.steps_run_again() > 0);
    CHECK(game.state == alone.state);
    CHECK(game.shown.size() == 7);  // each shown once, the ones run again not again
}

TEST_CASE("rollback: no further than the window ahead of another player's inputs: then it waits") {
    Toy game;
    Rollback rollback{game, Settings{.players = 2, .local = 0, .delay = 0, .window = 8}};
    std::uint32_t ran = 0;
    for (std::uint32_t step = 0; step < 20; ++step) {
        (void)rollback.add_local(held(0, step));
        if (rollback.advance()) {
            ++ran;
        }
    }
    CHECK(ran == 8);
    CHECK(rollback.step() == 8);
    rollback.add_remote(1, 0, held(1, 0));
    CHECK(rollback.advance());  // one more now
}

TEST_CASE("rollback: the input delay: the local input given now is for that many steps on") {
    Toy game;
    Rollback rollback{game, Settings{.players = 1, .local = 0, .delay = 2}};
    CHECK(rollback.local_known() == 2);           // the first two steps: nothing
    CHECK(rollback.add_local(held(0, 2)));        // the input given now: for step 2
    CHECK_FALSE(rollback.add_local(held(0, 3)));  // no further ahead than the delay
    CHECK(rollback.advance());
    CHECK(rollback.add_local(held(0, 3)));
}

TEST_CASE("rollback: a player who leaves gives nothing from then on, and nobody waits for him") {
    Toy game;
    Rollback rollback{game, Settings{.players = 2, .local = 0, .delay = 0}};
    for (std::uint32_t step = 0; step < 4; ++step) {
        rollback.add_remote(1, step, held(1, step));
    }
    rollback.leave(1, 4);
    for (std::uint32_t step = 0; step < 30; ++step) {
        REQUIRE(rollback.add_local(held(0, step)));
        REQUIRE(rollback.advance());
    }
    CHECK(rollback.confirmed() == 30);
}

TEST_CASE("rollback: the one ahead of the other on average is told to wait a refresh") {
    Toy game;
    Rollback rollback{game, Settings{.players = 2, .local = 0, .delay = 0}};
    for (std::uint32_t step = 0; step < 10; ++step) {
        (void)rollback.add_local(held(0, step));
        rollback.add_remote(1, step, held(1, step));
        REQUIRE(rollback.advance());
    }
    for (int i = 0; i < 16; ++i) {
        rollback.report(1, 6, -4);  // he is at step 6 and sees this one 4 ahead of him
    }
    CHECK(rollback.advantage(1) == 4);
    CHECK(rollback.ahead());
    for (int i = 0; i < 16; ++i) {
        rollback.report(1, 10, 0);
    }
    CHECK_FALSE(rollback.ahead());
}

TEST_CASE("rollback: a step the game says is never to be run again waits for every player's input") {
    class Waits : public Toy {
      public:
        bool waits = false;
        bool waits_for_inputs() override { return waits; }
    };
    Waits game;
    Rollback rollback{game, Settings{.players = 2, .local = 0, .delay = 0}};
    REQUIRE(rollback.add_local(held(0, 0)));
    REQUIRE(rollback.advance());  // on a prediction
    game.waits = true;
    REQUIRE(rollback.add_local(held(0, 1)));
    CHECK_FALSE(rollback.advance());  // not on a prediction
    rollback.add_remote(1, 0, held(1, 0));
    rollback.add_remote(1, 1, held(1, 1));
    CHECK(rollback.advance());
}

TEST_CASE("rollback: one who arrives after being away: the steps from then are run again, and wait for his inputs") {
    Toy game;
    Rollback rollback{game, Settings{.players = 2, .local = 0, .delay = 0}};
    rollback.leave(1, 0);  // nobody in the second place yet
    for (std::uint32_t step = 0; step < 8; ++step) {
        REQUIRE(rollback.add_local(held(0, step)));
        REQUIRE(rollback.advance());
    }
    CHECK(rollback.confirmed() == 8);
    CHECK(rollback.steps_run_again() == 0);
    rollback.arrive(1, 5);  // he comes at step 5 (the host says): the steps from 5 were run without him
    CHECK(rollback.there(1, 5));
    CHECK_FALSE(rollback.there(1, 4));
    CHECK(rollback.confirmed() == 5);
    REQUIRE(rollback.add_local(held(0, 8)));
    REQUIRE(rollback.advance());
    CHECK(rollback.steps_run_again() == 3);  // 5, 6, 7
    for (std::uint32_t step = 5; step < 9; ++step) {
        rollback.add_remote(1, step, held(1, step));
    }
    CHECK(rollback.confirmed() == 9);
    // And away again from 12: known from there without his inputs.
    rollback.leave(1, 12);
    for (std::uint32_t step = 9; step < 12; ++step) {
        rollback.add_remote(1, step, held(1, step));
    }
    CHECK(rollback.known(1) > 1000);
}

TEST_CASE("rollback: one joining under way starts at the step of the state he was given") {
    Toy game;
    Rollback rollback{game, Settings{.players = 2, .local = 1, .delay = 1, .first_step = 100}};
    CHECK(rollback.step() == 100);
    CHECK(rollback.local_known() == 101);  // the delay's step: nothing
    rollback.add_remote(0, 100, held(0, 100));
    REQUIRE(rollback.add_local(held(1, 101)));
    CHECK(rollback.advance());
    CHECK(rollback.step() == 101);
    CHECK(rollback.local_from(100).size() == 2);
}

TEST_CASE("rollback: one that doesn't decide who is there counts a player away only as far as that is decided") {
    Toy game;
    Rollback rollback{game, Settings{.players = 3, .local = 1, .delay = 0, .decides_presence = false}};
    rollback.leave(2, 0);                             // the third place nobody's (as far as is known)
    for (std::uint32_t step = 0; step < 8; ++step) {  // (the window's: with nothing confirmed, as far as it runs)
        rollback.add_remote(0, step, held(0, step));
        REQUIRE(rollback.add_local(held(1, step)));
        (void)rollback.advance();
    }
    CHECK(rollback.confirmed() == 0);  // nothing said yet of who is there
    rollback.decided_until(6);
    CHECK(rollback.confirmed() == 6);
    // An arrival at 8, told after the inputs that would have let 8 be confirmed without him: it isn't.
    rollback.arrive(2, 8);
    rollback.decided_until(20);
    CHECK(rollback.confirmed() == 8);  // his inputs from 8 on are to come
    rollback.decided_until(4);         // (never back)
    CHECK(rollback.confirmed() == 8);
}

TEST_CASE("rollback: a step to put right further back than the states kept: out of step, not a wrong state loaded") {
    Toy game;
    Rollback rollback{game, Settings{.players = 2, .local = 0, .delay = 0}};
    rollback.leave(1, 0);
    for (std::uint32_t step = 0; step < 30; ++step) {
        REQUIRE(rollback.add_local(held(0, step)));
        REQUIRE(rollback.advance());
    }
    rollback.arrive(1, 5);  // (no machine may: the host picks a step not confirmed yet)
    rollback.catch_up();
    CHECK(rollback.out_of_step() == std::optional<std::uint32_t>{5});
}

TEST_CASE("rollback: an input said to be for a step far beyond any machine's is not kept") {
    Toy game;
    Rollback rollback{game, Settings{.players = 2, .local = 0, .delay = 0}};
    rollback.add_remote(1, 0xFFFFFF00U, held(1, 0));
    rollback.add_remote(1, 0, held(1, 0));
    CHECK(rollback.known(1) == 1);
}

TEST_CASE(
    "rollback: a state given to go on from (the host's): the steps since run again unseen, none before it ever again; "
    "one not that far yet goes on from it") {
    // What every input known comes to: run to step 20, its state there given, then on to 30.
    Toy right;
    Rollback straight{right, Settings{.players = 2, .local = 0, .delay = 0}};
    const auto run_straight = [&](std::uint32_t to) {
        for (std::uint32_t step = straight.step(); step < to; ++step) {
            REQUIRE(straight.add_local(held(0, step)));
            straight.add_remote(1, step, held(1, step));
            REQUIRE(straight.advance());
        }
    };
    run_straight(20);
    const std::vector<std::uint8_t> given = straight.state_before(20);
    run_straight(30);

    // Player 1's machine, gone wrong at step 5, without player 0's inputs for steps 10 to 19 yet (predicted).
    Toy game;
    game.spoil_at = 5;
    Rollback rollback{game, Settings{.players = 2, .local = 1, .delay = 0, .window = 32}};
    for (std::uint32_t step = 0; step < 30; ++step) {
        REQUIRE(rollback.add_local(held(1, step)));
        if (step < 10 || step >= 20) {
            rollback.add_remote(0, step, held(0, step));
        }
        REQUIRE(rollback.advance());
    }
    REQUIRE(game.state != right.state);
    const std::uint32_t before = rollback.steps_run_again();
    REQUIRE(rollback.take_state(20, given));
    CHECK(rollback.step() == 30);
    CHECK(rollback.steps_run_again() == before + 10);
    CHECK(game.state == right.state);
    CHECK(game.shown.size() == 30);  // (the steps run again unseen)
    // Player 0's inputs before the state come, otherwise than predicted: nothing before step 20 is run again.
    for (std::uint32_t step = 10; step < 20; ++step) {
        rollback.add_remote(0, step, held(0, step));
    }
    rollback.catch_up();
    CHECK(game.state == right.state);
    CHECK_FALSE(rollback.out_of_step());

    // One not that far yet (his input delay ahead of him: two steps): on from the state given, at its step.
    Toy behind_game;
    Rollback behind{behind_game, Settings{.players = 2, .local = 1, .delay = 2, .window = 32}};
    for (std::uint32_t step = 0; step < 18; ++step) {
        REQUIRE(behind.add_local(held(1, step + 2)));  // (for step + 2)
        behind.add_remote(0, step, held(0, step));
        REQUIRE(behind.advance());
    }
    REQUIRE(behind.take_state(20, given));
    CHECK(behind.step() == 20);
    REQUIRE(behind.add_local(held(1, 20)));
    behind.add_remote(0, 20, held(0, 20));
    REQUIRE(behind.advance());
    Toy expected;  // the state given, and step 20 run on it
    REQUIRE(expected.take_state(given));
    const std::vector<Input> inputs{held(0, 20), held(1, 20)};
    expected.advance(inputs, 20, true);
    CHECK(behind_game.state == expected.state);
}
