// Tests for src/lvr/profile.*: what the minimap has seen, as text and as a file in the profile's folder.
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "lvr/profile.hpp"

using namespace lvr;

namespace {

// A folder of this test's own, emptied.
std::filesystem::path folder_for(const std::string& name) {
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / ("lvr-test-profile-" + name);
    std::filesystem::remove_all(folder);
    return folder;
}

}  // namespace

TEST_CASE("profile: what the minimap has seen is written as runs and read back the same") {
    SeenLevels levels;
    levels[2] = SeenLevel{.width = 4,
                          .height = 3,
                          .seen = {false, false, true, true, true, false, false, false, true, false, false, true}};
    levels[9] = SeenLevel{.width = 2, .height = 2, .seen = {true, true, true, true}};  // it starts with seen ones
    levels[30] = SeenLevel{.width = 3, .height = 1, .seen = {false, false, false}};
    const std::string text = format_seen(levels);
    CHECK(text == "lvr-minimap 1\nlevel 2 4 3\n2 3 3 1 2 1\nlevel 9 2 2\n0 4\nlevel 30 3 1\n3\n");
    const auto back = parse_seen(text);
    REQUIRE(back);
    CHECK(*back == levels);
    CHECK(parse_seen("lvr-minimap 1\n")->empty());
    CHECK(parse_seen("lvr-minimap 1\n\nlevel 1 1 1\n1\n\n")->size() == 1);  // empty lines are passed over
}

TEST_CASE("profile: a file that isn't what it should be is an error, never half a result") {
    CHECK_FALSE(parse_seen(""));
    CHECK_FALSE(parse_seen("something else\n"));
    CHECK_FALSE(parse_seen("lvr-minimap 1\nlevels\n"));
    CHECK_FALSE(parse_seen("lvr-minimap 1\nlevel 2 4\n"));
    CHECK_FALSE(parse_seen("lvr-minimap 1\nlevel 2 x 3\n12\n"));
    CHECK_FALSE(parse_seen("lvr-minimap 1\nlevel 2 99999 3\n1\n"));
    CHECK_FALSE(parse_seen("lvr-minimap 1\nlevel 2 4 3\n2 3\n"));            // fewer pixels than 4 x 3
    CHECK_FALSE(parse_seen("lvr-minimap 1\nlevel 2 4 3\n2 3 3 1 2 1 5\n"));  // more
    CHECK_FALSE(parse_seen("lvr-minimap 1\nlevel 2 4 3\n99999999999999999999\n"));
}

TEST_CASE("profile: kept in a folder, which is made; nothing kept yet is nothing; a bad file says so") {
    const std::filesystem::path folder = folder_for("seen") / "deeper";
    const auto nothing = load_seen(folder);
    REQUIRE(nothing);
    CHECK(nothing->empty());
    SeenLevels levels;
    levels[5] = SeenLevel{.width = 2, .height = 1, .seen = {true, false}};
    REQUIRE(save_seen(folder, levels));
    CHECK(std::filesystem::exists(folder / "minimap.txt"));
    CHECK_FALSE(std::filesystem::exists(folder / "minimap.txt.new"));
    const auto kept = load_seen(folder);
    REQUIRE(kept);
    CHECK(*kept == levels);
    // Written over.
    levels[5].seen = {true, true};
    REQUIRE(save_seen(folder, levels));
    CHECK(load_seen(folder)->at(5).seen == std::vector<bool>{true, true});

    std::ofstream{folder / "minimap.txt"} << "rubbish";
    const auto bad = load_seen(folder);
    REQUIRE_FALSE(bad);
    CHECK(bad.error().find("minimap.txt") != std::string::npos);
    // A folder that can't be made (a file is in its place): an error.
    CHECK_FALSE(save_seen(folder / "minimap.txt" / "under", levels));
    std::filesystem::remove_all(folder.parent_path());
}

TEST_CASE("profile: the quicksave's file is written and read back; another kind of file is refused") {
    const std::filesystem::path folder = folder_for("quicksave");
    const auto nothing = load_quicksave(folder);
    REQUIRE(nothing);
    CHECK_FALSE(nothing->has_value());
    const SavedPlay play{.remaster = true, .scene = 7, .database_entry = 450, .world = {1, 2, 3, 250}};
    REQUIRE(save_quicksave(folder, play));
    const auto kept = load_quicksave(folder);
    REQUIRE(kept);
    REQUIRE(kept->has_value());
    CHECK(**kept == play);

    // Cut short, with something after it, or not one at all.
    std::string bytes;
    {
        std::ifstream file{folder / "quicksave.bin", std::ios::binary};
        bytes.assign(std::istreambuf_iterator<char>{file}, {});
    }
    for (const std::string& bad : {bytes.substr(0, bytes.size() - 1), bytes + "x", std::string{"rubbish"}}) {
        std::ofstream{folder / "quicksave.bin", std::ios::binary} << bad;
        CHECK_FALSE(load_quicksave(folder));
    }
    std::string damaged = bytes;  // a byte of the world: the sum after it no longer fits
    damaged[bytes.size() - 10] = static_cast<char>(damaged[bytes.size() - 10] ^ 1);
    std::ofstream{folder / "quicksave.bin", std::ios::binary} << damaged;
    CHECK_FALSE(load_quicksave(folder));
    std::string other_kind = bytes;
    other_kind[0] = 'X';
    std::ofstream{folder / "quicksave.bin", std::ios::binary} << other_kind;
    CHECK_FALSE(load_quicksave(folder));
    // Another version of the game's state (the four bytes after the file's kind): refused, and it says so.
    std::string other_version = bytes;
    other_version[4] = static_cast<char>(other_version[4] + 1);
    std::ofstream{folder / "quicksave.bin", std::ios::binary} << other_version;
    const auto stale = load_quicksave(folder);
    REQUIRE_FALSE(stale);
    CHECK(stale.error().find("another version") != std::string::npos);
    std::filesystem::remove_all(folder);
}

TEST_CASE("profile: the screen's look is kept by its name; another kind of file is refused") {
    const std::filesystem::path folder = folder_for("screen");
    const auto nothing = load_screen_look(folder);
    REQUIRE(nothing);
    CHECK_FALSE(*nothing);  // nothing kept yet
    REQUIRE(save_screen_look(folder, "tv"));
    const auto kept = load_screen_look(folder);
    REQUIRE(kept);
    REQUIRE(*kept);
    CHECK(**kept == "tv");
    REQUIRE(save_screen_look(folder, "none"));  // written again over it
    CHECK(load_screen_look(folder)->value() == "none");
    for (const std::string bad : {"lvr-screen 2\nlook tv\n", "lvr-screen 1\nshade tv\n", "", "lvr-screen 1"}) {
        std::ofstream{folder / "screen.txt", std::ios::binary | std::ios::trunc} << bad;
        CHECK_FALSE(load_screen_look(folder));
    }
    std::filesystem::remove_all(folder);
}
