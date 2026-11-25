// lv-replay: the pixel-match harness (ADR-0017). Plays replays (replays/*.replay) in the engine and compares what
// it shows, refresh by refresh, with each replay's golden (NAME.golden: a hash per refresh of the Classic picture,
// its palette and what the game asks of the sound). The goldens in the repository were recorded from the
// reference engine before it was retired (replays/README.md).
//
//   lv-replay [--check] [options] REPLAY...  the engine against each replay's golden
//   lv-replay --record [options] REPLAY...   write each replay's golden next to it, from the engine as it is: for
//                                           a new replay only (it pins what the engine does now)
//   lv-replay --shot FILE.png REPLAY       write what the engine shows at the replay's last refresh (the picture
//                                           tools/lockstep.py sets beside the original's in DOSBox)
//       --gpu          the pictures drawn by the GPU renderer (lvr/gpu.hpp) instead of the Classic one: they are
//                      to be the same
//       --copies       the world replaced by a copy of itself before every step: a copy (the rewind's snapshot)
//                      must keep everything a replay needs
//       --saves        the world written out and read back (lvr/archive.hpp) before every step: what is written
//                      must be all of it
//   lv-replay --coop LOG                   a log of a game online that went out of step (NetGame::log) played again:
//                                           the first step whose state isn't the log's, or that all are
//       --rollback N   GGPO's sync test (ADR-0012): before every step the last N run again from the snapshot
//                      before them, without pictures; they must come to the same state, and the play goes on from
//                      there
//       --data DIR     the game's files (default gamedata/original)
//       --out DIR      where a difference is written (default gamedata/previews/match): per replay
//                      NAME-shown.png (the picture at the first refresh that differs) and NAME.txt (which
//                      refresh, and the entities then)
//
// Exit status 0 when every replay matched.
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <print>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "assets/graphics.hpp"
#include "image/png.hpp"
#include "lvr/coop.hpp"
#include "lvr/gpu.hpp"
#include "replay/golden.hpp"
#include "replay/player.hpp"
#include "replay/replay.hpp"

namespace lvr::replay {
namespace {

namespace fs = std::filesystem;

enum class Mode : std::uint8_t { kCheck, kRecord, kShot };

struct Options {
    Mode mode = Mode::kCheck;
    fs::path data = "gamedata/original";
    fs::path out = "gamedata/previews/match";
    fs::path shot;
    bool gpu = false;
    bool copies = false;
    bool saves = false;
    std::size_t rollback = 0;
    std::vector<fs::path> replays;
};

using PlayerPtr = std::expected<std::unique_ptr<Player>, std::string>;

// The GPU renderer, opened when first asked for; the process ends if there is none or it fails.
void draw_on_gpu(Screen& screen, const View& view, const SceneGraphics& scene, const SharedGraphics& shared) {
    static const std::unique_ptr<GpuRenderer> renderer = [] {
        auto opened = GpuRenderer::open_headless();
        if (!opened) {
            std::println(stderr, "the GPU renderer: {}", opened.error());
            std::exit(EXIT_FAILURE);
        }
        return std::move(*opened);
    }();
    if (const auto drawn = renderer->draw(screen, view, scene, shared); !drawn) {
        std::println(stderr, "the GPU renderer: {}", drawn.error());
        std::exit(EXIT_FAILURE);
    }
}

PlayerPtr open_engine(const Options& options, const Replay& replay) {
    return LvrPlayer::open(options.data, replay, options.gpu ? Draw{draw_on_gpu} : Draw{draw})
        .transform([&options](auto player) {
            player->saves = options.saves;
            player->copies = options.copies;
            player->rollback = options.rollback;
            return player;
        })
        .transform([](auto player) -> std::unique_ptr<Player> { return player; });
}

bool write_picture(const fs::path& path, const std::vector<std::uint8_t>& pixels, const assets::Palette& palette) {
    const assets::IndexedImage image{.width = kWidth, .height = kHeight, .pixels = pixels};
    if (image.pixels.size() != kWidth * kHeight) {
        return true;  // nothing to show
    }
    if (const auto saved = image::save_indexed_png(path, image, palette); !saved) {
        std::println(stderr, "{}", saved.error());
        return false;
    }
    return true;
}

// What the engine shows at the refresh that differs from the golden, and its entities then, into the output
// folder: the golden keeps only a hash, so there is no second picture to set beside it.
bool write_difference(const Options& options, const Replay& replay, const std::string& name, std::size_t at) {
    std::error_code error;
    fs::create_directories(options.out, error);
    if (error) {
        std::println(stderr, "can't create {}: {}", options.out.string(), error.message());
        return false;
    }
    auto player = open_engine(options, replay);
    if (!player) {
        return false;
    }
    std::optional<Refresh> shown;
    for (std::size_t refresh = 0; refresh <= at; ++refresh) {
        auto next = (*player)->next();
        if (!next || !*next) {
            break;
        }
        shown = std::move(**next);
    }
    bool written = true;
    if (shown) {
        written = write_picture(options.out / (name + "-shown.png"), shown->pixels, shown->palette);
    }
    std::ofstream report{options.out / (name + ".txt")};
    report << "differs from its golden at refresh " << at << "\n\n" << (*player)->entities();
    return written && static_cast<bool>(report);
}

// One replay; true if it matched (or was recorded).
bool run(const Options& options, const fs::path& path) {
    const std::string name = path.stem().string();
    const auto replay = load_replay(path);
    if (!replay) {
        std::println(stderr, "{}", replay.error());
        return false;
    }
    fs::path golden_path = path;
    golden_path.replace_extension(".golden");

    auto player = open_engine(options, *replay);
    if (!player) {
        std::println(stderr, "{}: {}", name, player.error());
        return false;
    }
    if (options.mode == Mode::kShot) {
        std::optional<Refresh> last;
        std::size_t refreshes = 0;
        for (;;) {
            auto next = (*player)->next();
            if (!next) {
                std::println(stderr, "{}: {}", name, next.error());
                return false;
            }
            if (!*next) {
                break;
            }
            last = std::move(**next);
            ++refreshes;
        }
        if (!last || !write_picture(options.shot, last->pixels, last->palette)) {
            return false;
        }
        std::println("{}: {} refreshes, the last in {}", name, refreshes, options.shot.string());
        return true;
    }
    const auto hashes = record(**player);
    if (options.mode == Mode::kRecord) {
        if (!hashes) {
            std::println(stderr, "{}: {}", name, hashes.error());
            return false;
        }
        std::ofstream out{golden_path, std::ios::binary};
        out << format_golden(*hashes);
        std::println("{}: {} refreshes recorded", name, hashes->size());
        return static_cast<bool>(out);
    }
    std::ifstream in{golden_path, std::ios::binary};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    const auto golden = in ? parse_golden(text) : std::unexpected{std::string{"can't read it"}};
    if (!golden) {
        std::println(stderr, "{}: {}", golden_path.string(), golden.error());
        return false;
    }
    if (!hashes) {
        std::println("{}: DIFFERS: stopped at {}", name, hashes.error());
        return false;
    }
    if (const auto at = first_difference(*golden, *hashes); at) {
        std::println("{}: DIFFERS from its golden at refresh {} (of {})", name, *at, golden->size());
        (void)write_difference(options, *replay, name, *at);
        return false;
    }
    std::println("{}: matches its golden, {} refreshes", name, hashes->size());
    return true;
}

void print_usage() {
    std::println(stderr,
                 "usage: lv-replay [--check | --record | --shot FILE.png] [--gpu] [--copies] [--saves] [--rollback N] "
                 "[--data DIR] [--out DIR] "
                 "REPLAY...");
}

int run_all(int argc, char** argv) {
    Options options;
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.size() >= 2 && args[0] == "--coop") {  // a game online's log
        std::ifstream file{std::string{args[1]}};
        std::stringstream text;
        text << file.rdbuf();
        const fs::path data = args.size() >= 4 && args[2] == "--data" ? fs::path{args[3]} : options.data;
        const auto replayed = replay_log(data, text.str());
        if (!file || !replayed) {
            std::println(stderr, "{}: {}", args[1], replayed ? "can't be read" : replayed.error());
            return EXIT_FAILURE;
        }
        if (*replayed) {
            std::println("{}: the state after step {} isn't the log's", args[1], **replayed);
            return EXIT_FAILURE;
        }
        std::println("{}: every step's state is the log's", args[1]);
        return EXIT_SUCCESS;
    }
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "--record") {
            options.mode = Mode::kRecord;
        } else if (arg == "--check") {
            options.mode = Mode::kCheck;
        } else if (arg == "--gpu") {
            options.gpu = true;
        } else if (arg == "--copies") {
            options.copies = true;
        } else if (arg == "--saves") {
            options.saves = true;
        } else if (arg == "--rollback" && i + 1 < args.size()) {
            options.rollback = static_cast<std::size_t>(std::stoul(std::string{args[++i]}));
        } else if (arg == "--shot" && i + 1 < args.size()) {
            options.mode = Mode::kShot;
            options.shot = args[++i];
        } else if ((arg == "--data" || arg == "--out") && i + 1 < args.size()) {
            (arg == "--data" ? options.data : options.out) = args[++i];
        } else if (arg.starts_with("--")) {
            print_usage();
            return EXIT_FAILURE;
        } else {
            options.replays.emplace_back(arg);
        }
    }
    if (options.replays.empty()) {
        print_usage();
        return EXIT_FAILURE;
    }
    std::size_t failed = 0;
    for (const fs::path& path : options.replays) {
        failed += run(options, path) ? 0 : 1;
    }
    if (options.replays.size() > 1) {
        std::println("{} of {} replays {}", options.replays.size() - failed, options.replays.size(),
                     options.mode == Mode::kRecord ? "recorded" : "match");
    }
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace
}  // namespace lvr::replay

int main(int argc, char** argv) try { return lvr::replay::run_all(argc, argv); } catch (const std::exception& e) {
    std::fputs("lv-replay: fatal: ", stderr);
    std::fputs(e.what(), stderr);
    std::fputs("\n", stderr);
    return EXIT_FAILURE;
} catch (...) {
    std::fputs("lv-replay: fatal: unhandled exception\n", stderr);
    return EXIT_FAILURE;
}
