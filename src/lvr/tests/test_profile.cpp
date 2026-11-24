// Tests for src/lvr/profile.*: what the minimap has seen, as text and as a file in the profile's folder.
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
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
