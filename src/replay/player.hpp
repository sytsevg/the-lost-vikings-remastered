#pragma once

// The engine (src/lvr/) as a Player of the pixel-match harness.

#include <cstddef>
#include <deque>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "assets/assets.hpp"
#include "lvr/play.hpp"
#include "lvr/render.hpp"
#include "lvr/sprites.hpp"
#include "lvr/world.hpp"
#include "replay/golden.hpp"
#include "replay/replay.hpp"

namespace lvr::replay {

class LvrPlayer : public Player {
  public:
    // Starts `replay`. So far only replays that start in a level (slots 0-36) can be played.
    static std::expected<std::unique_ptr<LvrPlayer>, std::string> open(const std::filesystem::path& original_dir,
                                                                       Replay replay, Draw draw_with = draw);

    // The level's start is shown as its last picture for as long as the original's load takes; then three
    // refreshes a step: a tick's three pictures.
    std::expected<std::optional<Refresh>, std::string> next() override;
    std::string entities() override;

    // Before every step the world is written out and read back into a world made anew for the level
    // (lvr/archive.hpp): what a save holds must be all a replay needs, or the replay differs from its golden. A
    // world that can't be read back ends the run with an error.
    bool saves = false;
    // GGPO's sync test (rollback): before every step the last `rollback` steps are run again from the
    // snapshot before the first of them, without their pictures, and the play goes on from there. They must come
    // to the state the straight run had (its hash), and the replay must still match its golden. A snapshot is a
    // copy of the world (the rewind's and the quicksave's are too): from the steps run again on, the play is a
    // copy's, so what a copy keeps must be all a replay needs as well.
    std::size_t rollback = 0;

  private:
    LvrPlayer(Replay replay, assets::GameFiles files, std::unique_ptr<LoadedLevel> loaded, SharedGraphics shared,
              int slot, Draw draw_with);
    Refresh picture();
    std::vector<Sound> take_sounds();
    void play_step();

    Draw draw_;
    bool save_failed_ = false;  // a world written out didn't read back as it was
    // The sync test: the play before each of the last steps, with its input; where a step run again came out
    // otherwise than the first time.
    struct Kept {
        Play::Snapshot before;  // with what was done before the step (the replay's `place`)
        StepInput input;
        std::optional<std::array<std::int16_t, 2>> place;
    };
    void place_vikings(const std::array<std::int16_t, 2>& at);
    std::deque<Kept> back_;
    std::optional<std::size_t> rolled_otherwise_;
    std::vector<std::uint8_t> written_;  // (the hash's buffer, kept)
    void run_again();
    Replay replay_;
    assets::GameFiles files_;  // the scenes the play goes on to are loaded from them
    SharedGraphics shared_;
    Play play_;
    PiecePainter painter_;
    std::vector<PiecePicture> pictures_;
    Screen screen_;
    std::deque<Refresh> queued_;
    std::optional<Refresh> last_;
    std::vector<Sound> carried_;  // sounds of a step that took no refresh  // the last picture shown: a step that shows
                                  // fewer than its refreshes starts with it
    std::size_t step_ = 0;
};

}  // namespace lvr::replay
