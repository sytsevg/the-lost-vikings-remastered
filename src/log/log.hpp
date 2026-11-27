#pragma once

// The log: what the game says it did, kept to find out afterwards what went wrong (ADR-0023). A line at a time, from
// any thread, into a file of its own for each run in a folder beside the program (`logs/`), and on stderr:
//
//   [2026-11-27 21:03:15.042] [info]  [Places::host]: hosting on port 47230
//   [2026-11-27 21:03:15.044] [warn]  [Match::poll]: held up 1.2 s for player 2
//   [2026-11-27 21:03:15.051] [error] [OnlinePlace::refresh]: the host is gone
//
// The time, the level, and the function that said it (its class's name before it, the namespaces left out). The
// line is made where it is said and handed over: a thread of the log's own writes it, so saying it never waits for
// the disk or a terminal. Until the log is started (a dump, a tool's run, the tests) a warning or an error goes to
// stderr at once, without the time, and the rest nowhere. Plain C++: everything may use it, `net` too.

#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace lvr::log {

enum class Level : std::uint8_t { kInfo, kWarn, kError };

// How the log's files are kept: a file is followed by a new one once it has this many bytes, and only the newest so
// many of the log's files stay in the folder (what else is there, such as a co-op game's log, is left alone).
struct Keeping {
    std::string name = "vikings-remaster";  // the files' names: NAME-2026-11-27-210315.log
    std::uintmax_t file_bytes = std::uintmax_t{10} * 1024 * 1024;
    int files = 10;
    bool echo = true;  // each line on stderr as well
};

// The log written into its folder from start() until this is gone; then what is still waiting is written, and the
// log stops.
class Started {
  public:
    Started(const Started&) = delete;
    Started& operator=(const Started&) = delete;
    Started(Started&& other) noexcept : owns_{std::exchange(other.owns_, false)} {}
    Started& operator=(Started&& other) noexcept;
    ~Started();

  private:
    friend std::expected<Started, std::string> start(const std::filesystem::path& folder, const Keeping& keeping);
    Started() = default;
    bool owns_ = true;
};

// The log written into `folder` (made if need be), in a new file. An error says why it can't be (the folder or the
// file can't be made, or the log is started already); the log then goes on as if it never was.
std::expected<Started, std::string> start(const std::filesystem::path& folder, const Keeping& keeping = {});

// What a line is never to say as it is, so that a log can be shown to anyone: the player's home folder, the game's
// own folder, the address a guest joins. Every line, on stderr too, says `shown` in its place (the longest first).
// Hidden again, it is kept once (said as `shown` now). Forgotten when the log is started again.
void hide(std::string seen, std::string shown);

// Whether a line of this level goes anywhere now: every level once started, else warnings and errors only.
bool wanted(Level level);

// A line said in the function `where` is: handed to the log's thread (started), or a warning or an error straight to
// stderr (not).
void say(Level level, std::string text, const std::source_location& where = std::source_location::current());

// What info(), warn() and error() take: the line's format, checked as std::format checks it, and where it is said.
template <class... Args>
struct Format {
    template <class Text>
        requires std::convertible_to<const Text&, std::string_view>
    consteval Format(const Text& text, std::source_location at = std::source_location::current())  // (implicit)
        : format{text}, where{at} {}
    std::format_string<Args...> format;
    std::source_location where;
};

template <class... Args>
void info(Format<std::type_identity_t<Args>...> format, Args&&... args) {
    if (wanted(Level::kInfo)) {
        say(Level::kInfo, std::format(format.format, std::forward<Args>(args)...), format.where);
    }
}

template <class... Args>
void warn(Format<std::type_identity_t<Args>...> format, Args&&... args) {
    say(Level::kWarn, std::format(format.format, std::forward<Args>(args)...), format.where);
}

template <class... Args>
void error(Format<std::type_identity_t<Args>...> format, Args&&... args) {
    say(Level::kError, std::format(format.format, std::forward<Args>(args)...), format.where);
}

// Where a line was said, as the log names it: the function, after its class (and the classes that one is in), without
// the namespaces, the return type or the parameters; in a lambda, the function it is in.
// "std::expected<int, std::string> lvr::net::Match::poll()": "Match::poll".
std::string origin_of(std::string_view function);

// For what may happen again and again: one line at most every `every_ms`, on the caller's own clock (the
// connection's, the window's), and how many were held back meanwhile told with the next one let through.
class Throttle {
  public:
    explicit Throttle(std::uint32_t every_ms) : every_ms_{every_ms} {}
    // None: held back. Else how many were held back since the last one let through.
    std::optional<std::uint32_t> pass(std::uint32_t now_ms);

  private:
    std::uint32_t every_ms_;
    std::optional<std::uint32_t> last_;
    std::uint32_t held_ = 0;
};

}  // namespace lvr::log
