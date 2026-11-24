// Tests for src/replay/replay.*: the replay text format.
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "replay/replay.hpp"

using namespace lvr::replay;

TEST_CASE("replay: slot, steps, keys over ranges, held keys and typed characters") {
    const auto replay = parse_replay(
        "# level 3, a short walk\n"
        "slot 2\n"
        "steps 10\r\n"
        "\n"
        "key 1-3 0x0010   # right\n"
        "key 3 0x0001\n"
        "held 4 0x15\n"
        "held 4 0x38\n"
        "typed 9 0x54\n"
        "place 6 1200 400\n");
    REQUIRE(replay);
    CHECK(replay->steps[6].place == std::array<std::int16_t, 2>{1200, 400});
    CHECK_FALSE(replay->steps[5].place);
    CHECK(format_replay(*replay).ends_with("place 6 1200 400\n"));
    CHECK(replay->slot == 2);
    REQUIRE(replay->steps.size() == 10);
    CHECK(replay->steps[0] == Step{});
    CHECK(replay->steps[1].keyboard == 0x0010);
    CHECK(replay->steps[3].keyboard == 0x0011);  // lines add up
    CHECK(replay->steps[4].keyboard == 0);
    CHECK(replay->steps[4].held == std::vector<std::uint8_t>{0x15, 0x38});
    CHECK_FALSE(replay->steps[8].typed);
    CHECK(replay->steps[9].typed == 0x54);
}

TEST_CASE("replay: errors name the line") {
    const auto error = [](const char* text) {
        const auto replay = parse_replay(text);
        REQUIRE_FALSE(replay);
        return replay.error();
    };
    CHECK(error("slot 0\n") == "no `steps` line");
    CHECK(error("steps 5\njump 1 2\n").starts_with("line 2:"));
    CHECK(error("key 1 2\nsteps 5\n").starts_with("line 1: `steps` must come first"));
    CHECK(error("steps 5\nkey 5 1\n").starts_with("line 2: a step past"));
    CHECK(error("steps 5\nkey 3-2 1\n").starts_with("line 2: expected"));
    CHECK(error("steps 5\nkey 1 0x10000\n").starts_with("line 2: expected"));
    CHECK(error("steps 5\nheld 1 0x100\n").starts_with("line 2: expected"));
    CHECK(error("steps 5\nkey 1\n").starts_with("line 2: expected"));
    CHECK(error("steps 5\nplace 1 2\n").starts_with("line 2: expected"));
    CHECK(error("steps 5\nplace 7 2 3\n").starts_with("line 2: a step past"));
    CHECK(error("steps 5\nkey x 1\n").starts_with("line 2: expected"));
    CHECK(error("steps 5\nkey 1 0x\n").starts_with("line 2: expected"));
    CHECK(error("steps many\n").starts_with("line 1: a number"));
    CHECK(error("slot 70000\nsteps 1\n").starts_with("line 1: a number"));
    CHECK(error("steps 99999999\n").starts_with("line 1: a number"));
}

TEST_CASE("replay: the text of a replay parses back to it") {
    Replay replay{.slot = 7, .steps = std::vector<Step>(12)};
    for (std::size_t step = 2; step <= 5; ++step) {
        replay.steps[step].keyboard = 0x0030;
    }
    replay.steps[6].keyboard = 0x0001;
    replay.steps[11].keyboard = 0x8000;
    replay.steps[3].held = {0x1f};
    replay.steps[8].typed = 0x81;
    const std::string text = format_replay(replay);
    CHECK(text == "slot 7\nsteps 12\nkey 2-5 0x0030\nkey 6 0x0001\nkey 11 0x8000\nheld 3 0x1f\ntyped 8 0x81\n");
    CHECK(parse_replay(text) == replay);
    CHECK(format_replay(Replay{}) == "slot 0\nsteps 0\n");
}

TEST_CASE("replay: read from a file") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lvr-test.replay";
    {
        std::ofstream out{path, std::ios::binary};
        out << "slot 1\nsteps 3\nkey 0 2\n";
    }
    const auto replay = load_replay(path);
    REQUIRE(replay);
    CHECK(replay->steps[0].keyboard == 2);
    {
        std::ofstream out{path, std::ios::binary};
        out << "steps\n";
    }
    const auto broken = load_replay(path);
    REQUIRE_FALSE(broken);
    CHECK(broken.error().find("lvr-test.replay: line 1") != std::string::npos);
    std::filesystem::remove(path);
    CHECK_FALSE(load_replay(path));
}
