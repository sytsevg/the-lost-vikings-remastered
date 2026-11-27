// Tests for src/log/log.*: lines into a file of the log's own by its thread, where each was said, the files kept, and
// the throttle.
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "log/log.hpp"

using namespace lvr;

namespace {

// A folder of this test's own, emptied.
std::filesystem::path folder_for(const std::string& name) {
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / ("lvr-test-log-" + name);
    std::filesystem::remove_all(folder);
    return folder;
}

// The log's files in `folder` (named NAME-...log), in the order of their names.
std::vector<std::filesystem::path> files_of(const std::filesystem::path& folder, const std::string& name) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator{folder}) {
        const std::string file = entry.path().filename().string();
        if (file.starts_with(name + "-") && file.ends_with(".log")) {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);
    return files;
}

std::vector<std::string> lines_of(const std::filesystem::path& file) {
    std::vector<std::string> lines;
    std::ifstream in{file};
    for (std::string line; std::getline(in, line);) {
        lines.push_back(line);
    }
    return lines;
}

// Lines said in a class's function, in a lambda in it, and in a function of no class: where each was said.
struct Probe {
    void speak() {
        log::info("from a member");
        [] { log::info("from a lambda"); }();
    }
};

void free_speech() {
    log::warn("from a function of no class");
}

// Quiet (nothing on stderr), the files as small as a test wants.
log::Keeping quiet(std::uintmax_t file_bytes = 1'000'000, int files = 10) {
    return log::Keeping{.name = "test", .file_bytes = file_bytes, .files = files, .echo = false};
}

}  // namespace

TEST_CASE("log: not started, only warnings and errors are wanted") {
    CHECK_FALSE(log::wanted(log::Level::kInfo));
    CHECK(log::wanted(log::Level::kWarn));
    CHECK(log::wanted(log::Level::kError));
}

TEST_CASE("log: started, every line is in its file in the order said, with the time and the level") {
    const std::filesystem::path folder = folder_for("lines") / "logs";  // (made)
    {
        auto started = log::start(folder, quiet());
        REQUIRE(started);
        CHECK(log::wanted(log::Level::kInfo));
        CHECK_FALSE(log::start(folder, quiet()));  // (once)
        log::info("scene {} began", 5);
        log::warn("held up {:.1f} s", 1.25);
        log::error("the host is gone");
    }
    CHECK_FALSE(log::wanted(log::Level::kInfo));
    log::info("after it stopped");  // (nowhere)
    const std::vector<std::filesystem::path> files = files_of(folder, "test");
    REQUIRE(files.size() == 1);
    CHECK(std::regex_match(files[0].filename().string(), std::regex{R"(test-\d{4}-\d\d-\d\d-\d{6}\.log)"}));
    const std::vector<std::string> lines = lines_of(files[0]);
    REQUIRE(lines.size() == 3);
    const std::regex shape{R"(\[\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}\] \[\w+\] +\[\w+\]: .*)"};
    for (const std::string& line : lines) {
        CHECK(std::regex_match(line, shape));
    }
    // The levels as wide as each other: where each line was said begins in one column.
    CHECK(lines[0].substr(26, 9) == "[info]  [");
    CHECK(lines[1].substr(26, 9) == "[warn]  [");
    CHECK(lines[2].substr(26, 9) == "[error] [");
    CHECK(lines[0].ends_with("]: scene 5 began"));
    CHECK(lines[1].ends_with("]: held up 1.2 s"));
    CHECK(lines[2].ends_with("]: the host is gone"));
}

TEST_CASE("log: each line names the function that said it, after its class") {
    const std::filesystem::path folder = folder_for("origins");
    {
        auto started = log::start(folder, quiet());
        REQUIRE(started);
        Probe{}.speak();
        free_speech();
    }
    const std::vector<std::filesystem::path> files = files_of(folder, "test");
    REQUIRE(files.size() == 1);
    const std::vector<std::string> lines = lines_of(files[0]);
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].ends_with("] [info]  [Probe::speak]: from a member"));
    CHECK(lines[1].ends_with("] [info]  [Probe::speak]: from a lambda"));  // (the function it is in)
    CHECK(lines[2].ends_with("] [warn]  [free_speech]: from a function of no class"));
}

TEST_CASE("log: a function's name as the compiler gives it, as the log names it") {
    CHECK(log::origin_of("void lvr::Places::go(const Go &)") == "Places::go");
    CHECK(log::origin_of("std::expected<int, std::string> lvr::net::Match::poll()") == "Match::poll");
    CHECK(log::origin_of("static int lvr::net::Match::count()") == "Match::count");
    CHECK(log::origin_of("lvr::net::Match::Match(int)") == "Match::Match");
    CHECK(log::origin_of("lvr::net::Match::~Match()") == "Match::~Match");
    CHECK(log::origin_of("void lvr::net::Match::take(const T &) [T = int]") == "Match::take");
    CHECK(log::origin_of("auto lvr::Places::go(const Go &)::(lambda)::operator()() const") == "Places::go");
    CHECK(log::origin_of("void lvr::(anonymous namespace)::helper(std::vector<int> &)") == "helper");
    CHECK(log::origin_of("void lvr::net::SdlTransport::Inner::pump()") == "SdlTransport::Inner::pump");
    CHECK(log::origin_of("bool lvr::Cue::operator()(int) const") == "Cue::operator()");
    CHECK(log::origin_of("void lvr::Table<std::map<int, int>>::fill()") == "Table<std::map<int, int>>::fill");
    CHECK(log::origin_of("int main()") == "main");
    CHECK(log::origin_of("") == "?");
}

TEST_CASE("log: every run has a file of its own") {
    const std::filesystem::path folder = folder_for("runs");
    for (int run = 0; run < 3; ++run) {
        auto started = log::start(folder, quiet());
        REQUIRE(started);
        log::info("run {}", run);
    }
    const std::vector<std::filesystem::path> files = files_of(folder, "test");
    REQUIRE(files.size() == 3);  // (three in a second: -2, -3)
    for (const std::filesystem::path& file : files) {
        CHECK(lines_of(file).size() == 1);
    }
}

TEST_CASE("log: lines said on many threads at once all arrive whole, each thread's in its order") {
    const std::filesystem::path folder = folder_for("threads");
    constexpr int kThreads = 8;
    constexpr int kLines = 500;
    {
        auto started = log::start(folder, quiet());
        REQUIRE(started);
        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([t] {
                for (int n = 0; n < kLines; ++n) {
                    log::info("thread {} line {}", t, n);
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
    }
    const std::vector<std::filesystem::path> files = files_of(folder, "test");
    REQUIRE(files.size() == 1);
    const std::vector<std::string> lines = lines_of(files[0]);
    REQUIRE(lines.size() == std::size_t{kThreads} * kLines);
    std::vector<int> next(kThreads, 0);
    const std::regex said{R"(.* \[info\]  \[\w+\]: thread (\d) line (\d+))"};
    for (const std::string& line : lines) {
        std::smatch match;
        REQUIRE(std::regex_match(line, match, said));
        const auto thread = static_cast<std::size_t>(std::stoi(match[1].str()));
        CHECK(std::stoi(match[2].str()) == next.at(thread));
        ++next.at(thread);
    }
}

TEST_CASE("log: a full file is followed by a new one, and only the newest of the log's files are kept") {
    const std::filesystem::path folder = folder_for("keeping");
    std::filesystem::create_directories(folder);
    for (const char* other : {"coop-1234-player1.log", "notes.txt"}) {  // (not the log's: left alone)
        std::ofstream{folder / other} << "kept\n";
    }
    constexpr std::uintmax_t kBytes = 300;
    {
        auto started = log::start(folder, quiet(kBytes, 3));
        REQUIRE(started);
        for (int n = 0; n < 40; ++n) {
            log::info("line {:02} of a log that fills its files", n);
        }
    }
    const std::vector<std::filesystem::path> files = files_of(folder, "test");
    CHECK(files.size() == 3);
    CHECK(std::filesystem::exists(folder / "coop-1234-player1.log"));
    CHECK(std::filesystem::exists(folder / "notes.txt"));
    bool last_found = false;
    for (const std::filesystem::path& file : files) {
        CHECK(std::filesystem::file_size(file) < kBytes + 100);  // (past the size by one line at most)
        const std::vector<std::string> lines = lines_of(file);
        REQUIRE_FALSE(lines.empty());
        // (Every one kept followed another; the log's own line, from where the log said it.)
        CHECK(lines.front().contains("] [info]  [Writer::begin_file]: continued from test-"));
        last_found = last_found || lines.back().ends_with("line 39 of a log that fills its files");
    }
    CHECK(last_found);
}

TEST_CASE("log: with a folder that can't be made, it isn't started") {
    const std::filesystem::path folder = folder_for("unmade");
    std::filesystem::create_directories(folder);
    std::ofstream{folder / "a-file"} << "in the way\n";
    const auto started = log::start(folder / "a-file" / "logs", quiet());
    REQUIRE_FALSE(started);
    CHECK(started.error().starts_with("can't make "));
    CHECK_FALSE(log::wanted(log::Level::kInfo));
}

TEST_CASE("log: a throttle lets one through every so often, and tells how many it held back") {
    log::Throttle throttle{1000};
    CHECK(throttle.pass(5000) == 0U);
    CHECK_FALSE(throttle.pass(5500));
    CHECK_FALSE(throttle.pass(5999));
    CHECK(throttle.pass(6000) == 2U);
    CHECK(throttle.pass(9000) == 0U);
    log::Throttle wrapping{1000};  // (a clock of 32 bits wraps round)
    CHECK(wrapping.pass(0xFFFF'FF00U) == 0U);
    CHECK_FALSE(wrapping.pass(0x100U));
    CHECK(wrapping.pass(0x400U) == 1U);
}
