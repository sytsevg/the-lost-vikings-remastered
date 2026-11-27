#include "log/log.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <source_location>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace lvr::log {
namespace {

using Clock = std::chrono::system_clock;

// Lines handed over and not written yet: past this many (the disk stuck), the rest are dropped, and counted.
constexpr std::size_t kMostWaiting = 10'000;

// Each level as a line has it, as wide as the others: what comes after it lines up.
constexpr std::array<std::string_view, 3> kLevelWords{"[info] ", "[warn] ", "[error]"};

std::string_view word(Level level) {
    return kLevelWords.at(static_cast<std::size_t>(level));
}

struct Said {
    Clock::time_point when;
    Level level = Level::kInfo;
    std::source_location where;  // (its names are the program's own: they outlive the line)
    std::string text;
};

// The local time, and its milliseconds.
std::pair<std::tm, int> local(Clock::time_point when) {
    const std::time_t seconds = Clock::to_time_t(when);
    std::tm local{};
#ifdef _WIN32
    (void)localtime_s(&local, &seconds);
#else
    (void)localtime_r(&seconds, &local);
#endif
    const auto since = std::chrono::duration_cast<std::chrono::milliseconds>(when.time_since_epoch()).count();
    return {local, static_cast<int>(since % 1000)};
}

// A line as the file has it: [2026-11-27 21:03:15.042] [warn]  [Match::poll]: TEXT.
std::string line_of(const Said& said) {
    const auto [at, ms] = local(said.when);
    return std::format("[{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}] {} [{}]: {}\n", at.tm_year + 1900, at.tm_mon + 1,
                       at.tm_mday, at.tm_hour, at.tm_min, at.tm_sec, ms, word(said.level),
                       origin_of(said.where.function_name()), said.text);
}

// The log's lines, handed over by whoever says them and written by its own thread.
class Writer {
  public:
    Writer() = default;
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    Writer(Writer&&) = delete;
    Writer& operator=(Writer&&) = delete;
    ~Writer() { stop(); }

    std::expected<void, std::string> start(const std::filesystem::path& folder, const Keeping& keeping);
    void stop() noexcept;
    bool started() const noexcept { return started_.load(std::memory_order_relaxed); }
    // The line handed to the thread (`text` taken), or false: the log isn't started (`text` left as it was).
    bool hand_over(Level level, std::string& text, const std::source_location& where);
    void hide(std::string seen, std::string shown);
    // `text` with what is hidden in it replaced.
    std::string hidden(std::string text);

  private:
    void run();
    void write(const Said& said);
    // A new file to write in, its first line `first` (if any); the oldest of the log's files beyond what is kept
    // removed.
    std::expected<void, std::string> begin_file(const std::string& first);
    void prune();

    std::atomic<bool> started_{false};  // (read without the lock: whether a line is worth making)
    std::mutex lock_;
    std::condition_variable wake_;
    // Under the lock: the lines handed over, how many were dropped, whether lines are taken, and the word to stop.
    std::vector<Said> waiting_;
    std::vector<std::pair<std::string, std::string>> hidden_;  // (the longest first)
    std::uint64_t dropped_ = 0;
    bool open_ = false;
    bool stopping_ = false;
    std::thread thread_;
    // The thread's own once it runs (start's before).
    // (Nothing made here can throw: the writer is made before main.)
    std::filesystem::path folder_;
    std::string name_;
    std::uintmax_t file_bytes_ = 0;
    int files_ = 0;
    bool echo_ = false;
    std::optional<std::ofstream> file_;
    std::filesystem::path path_;
    std::uintmax_t bytes_ = 0;
};

Writer the_writer;  // (made before main: never on a thread's first line)

Writer& writer() {
    return the_writer;
}

std::expected<void, std::string> Writer::start(const std::filesystem::path& folder, const Keeping& keeping) {
    const std::scoped_lock held{lock_};
    if (open_) {
        return std::unexpected{std::string{"the log is started already"}};
    }
    folder_ = folder;
    name_ = keeping.name;
    file_bytes_ = keeping.file_bytes;
    files_ = keeping.files;
    echo_ = keeping.echo;
    std::error_code error;
    std::filesystem::create_directories(folder_, error);
    if (error) {
        return std::unexpected{std::format("can't make {}: {}", folder_.string(), error.message())};
    }
    if (auto begun = begin_file({}); !begun) {
        return begun;
    }
    waiting_.clear();
    hidden_.clear();
    dropped_ = 0;
    stopping_ = false;
    try {
        thread_ = std::thread{[this] { run(); }};
    } catch (const std::system_error& failed) {
        file_.reset();
        return std::unexpected{std::format("the log can't start its thread: {}", failed.what())};
    }
    open_ = true;
    started_ = true;
    return {};
}

void Writer::stop() noexcept {
    try {
        {
            const std::scoped_lock held{lock_};
            if (!open_) {
                return;
            }
            open_ = false;  // (no line is taken after this: the thread writes what there is, and ends)
            stopping_ = true;
            started_ = false;
        }
        wake_.notify_one();
        thread_.join();
        file_.reset();
    } catch (const std::system_error& failed) {  // (a lock or a join that fails: what is left unwritten is lost)
        (void)std::fputs(std::format("error: the log couldn't stop: {}\n", failed.what()).c_str(), stderr);
    }
}

bool Writer::hand_over(Level level, std::string& text, const std::source_location& where) {
    const Clock::time_point now = Clock::now();
    {
        const std::scoped_lock held{lock_};
        if (!open_) {
            return false;
        }
        if (waiting_.size() >= kMostWaiting) {
            ++dropped_;
            return true;
        }
        waiting_.push_back(Said{.when = now, .level = level, .where = where, .text = std::move(text)});
    }
    wake_.notify_one();
    return true;
}

void Writer::hide(std::string seen, std::string shown) {
    if (seen.empty()) {
        return;
    }
    const std::scoped_lock held{lock_};
    if (const auto known = std::ranges::find(hidden_, seen, &std::pair<std::string, std::string>::first);
        known != hidden_.end()) {
        known->second = std::move(shown);  // (hidden already: once, as it is to be shown now)
        return;
    }
    hidden_.emplace_back(std::move(seen), std::move(shown));
    std::ranges::stable_sort(hidden_, std::greater{}, [](const auto& one) { return one.first.size(); });
}

std::string Writer::hidden(std::string text) {
    const std::scoped_lock held{lock_};
    for (const auto& [seen, shown] : hidden_) {
        for (std::size_t at = text.find(seen); at != std::string::npos; at = text.find(seen, at + shown.size())) {
            text.replace(at, seen.size(), shown);
        }
    }
    return text;
}

void Writer::run() {
    std::vector<Said> lines;
    for (;;) {
        std::uint64_t dropped = 0;
        bool last = false;
        {
            std::unique_lock held{lock_};
            wake_.wait(held, [this] { return stopping_ || !waiting_.empty(); });
            lines.swap(waiting_);
            dropped = std::exchange(dropped_, 0);
            last = stopping_;  // (nothing more is handed over once it is)
        }
        for (const Said& said : lines) {
            write(said);
        }
        lines.clear();
        if (dropped != 0) {
            write(Said{.when = Clock::now(),
                       .level = Level::kWarn,
                       .where = std::source_location::current(),
                       .text = std::format("{} line(s) dropped: the log fell behind", dropped)});
        }
        if (file_) {
            file_->flush();
        }
        if (last) {
            return;
        }
    }
}

void Writer::write(const Said& said) {
    const std::string line = hidden(line_of(said));
    if (echo_) {
        (void)std::fputs(line.c_str(), stderr);
    }
    if (!file_) {
        return;
    }
    *file_ << line;
    bytes_ += line.size();
    // On in a new file (none: the lines go on to stderr only); never after a file's first line, however long.
    if (bytes_ >= file_bytes_ && bytes_ > line.size()) {
        const std::string was = path_.filename().string();
        file_.reset();
        (void)begin_file(std::format("continued from {}", was));
    }
}

std::expected<void, std::string> Writer::begin_file(const std::string& first) {
    const auto [at, ms] = local(Clock::now());
    const std::string stamp = std::format("{:04}-{:02}-{:02}-{:02}{:02}{:02}", at.tm_year + 1900, at.tm_mon + 1,
                                          at.tm_mday, at.tm_hour, at.tm_min, at.tm_sec);
    std::filesystem::path path = folder_ / std::format("{}-{}.log", name_, stamp);
    std::error_code error;
    for (int another = 2; std::filesystem::exists(path, error); ++another) {  // (two in one second)
        path = folder_ / std::format("{}-{}-{}.log", name_, stamp, another);
    }
    file_.emplace(path, std::ios::binary | std::ios::app);
    if (!*file_) {
        file_.reset();
        return std::unexpected{std::format("can't write {}", path.string())};
    }
    path_ = std::move(path);
    bytes_ = 0;
    prune();
    if (!first.empty()) {
        write(
            Said{.when = Clock::now(), .level = Level::kInfo, .where = std::source_location::current(), .text = first});
    }
    return {};
}

void Writer::prune() {
    struct Found {
        std::filesystem::file_time_type when;
        std::filesystem::path path;
    };
    std::vector<Found> found;
    const std::string begins = name_ + "-";
    std::error_code error;
    for (std::filesystem::directory_iterator at{folder_, error}, end; !error && at != end; at.increment(error)) {
        const std::filesystem::path& path = at->path();
        const std::string name = path.filename().string();
        std::error_code unread;
        if (path == path_ || !name.starts_with(begins) || !name.ends_with(".log") || !at->is_regular_file(unread)) {
            continue;
        }
        found.push_back(Found{.when = at->last_write_time(unread), .path = path});
    }
    std::ranges::sort(found, std::greater{}, &Found::when);                 // the newest first
    const auto others = static_cast<std::size_t>(std::max(files_ - 1, 0));  // (and the one being written)
    for (std::size_t i = others; i < found.size(); ++i) {
        std::error_code ignored;
        (void)std::filesystem::remove(found[i].path, ignored);
    }
}

}  // namespace

Started& Started::operator=(Started&& other) noexcept {
    if (this != &other) {
        if (owns_) {
            writer().stop();
        }
        owns_ = std::exchange(other.owns_, false);
    }
    return *this;
}

Started::~Started() {
    if (owns_) {
        writer().stop();
    }
}

std::expected<Started, std::string> start(const std::filesystem::path& folder, const Keeping& keeping) {
    if (auto started = writer().start(folder, keeping); !started) {
        return std::unexpected{started.error()};
    }
    return Started{};
}

bool wanted(Level level) {
    return level != Level::kInfo || writer().started();
}

void say(Level level, std::string text, const std::source_location& where) {
    if (!writer().hand_over(level, text, where) && level != Level::kInfo) {
        (void)std::fputs(
            writer().hidden(std::format("{} [{}]: {}\n", word(level), origin_of(where.function_name()), text)).c_str(),
            stderr);
    }
}

void hide(std::string seen, std::string shown) {
    writer().hide(std::move(seen), std::move(shown));
}

std::string origin_of(std::string_view function) {
    std::string name{function};
    constexpr std::string_view kAnonymous = "(anonymous namespace)::";
    for (std::size_t at = name.find(kAnonymous); at != std::string::npos; at = name.find(kAnonymous)) {
        name.erase(at, kAnonymous.size());
    }
    // The qualified name ends where its parameters begin: the first bracket outside a template's (an operator()'s
    // own pair passed over); the return type before it ends at the last space outside one.
    int depth = 0;
    std::size_t begin = 0;
    std::size_t end = name.size();
    for (std::size_t at = 0; at < name.size(); ++at) {
        const char c = name[at];
        if (c == '<') {
            ++depth;
        } else if (c == '>' && depth > 0) {
            --depth;
        } else if (c == ' ' && depth == 0) {
            begin = at + 1;
        } else if (c == '(' && depth == 0) {
            if (std::string_view{name}.substr(0, at).ends_with("operator") && name.compare(at, 2, "()") == 0) {
                ++at;
                continue;
            }
            end = at;
            break;
        }
    }
    // Its parts, outside templates; the function, and the classes it is in before it (a class's name begins with a
    // capital, a namespace's never).
    std::vector<std::string_view> parts;
    const std::string_view qualified = std::string_view{name}.substr(begin, end - begin);
    depth = 0;
    std::size_t part = 0;
    for (std::size_t at = 0; at < qualified.size(); ++at) {
        if (qualified[at] == '<') {
            ++depth;
        } else if (qualified[at] == '>' && depth > 0) {
            --depth;
        } else if (depth == 0 && qualified.substr(at).starts_with("::")) {
            parts.push_back(qualified.substr(part, at - part));
            part = at + 2;
            ++at;
        }
    }
    parts.push_back(qualified.substr(part));
    std::size_t first = parts.size() - 1;
    while (first > 0 && !parts[first - 1].empty() && parts[first - 1].front() >= 'A' &&
           parts[first - 1].front() <= 'Z') {
        --first;
    }
    std::string origin;
    for (std::size_t at = first; at < parts.size(); ++at) {
        origin += at == first ? "" : "::";
        origin += parts[at];
    }
    return origin.empty() ? std::string{"?"} : origin;
}

std::optional<std::uint32_t> Throttle::pass(std::uint32_t now_ms) {
    if (last_ && now_ms - *last_ < every_ms_) {  // (the clock may wrap round: the difference is still right)
        ++held_;
        return std::nullopt;
    }
    last_ = now_ms;
    return std::exchange(held_, 0);
}

}  // namespace lvr::log
