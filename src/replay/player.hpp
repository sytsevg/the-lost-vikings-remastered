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

    // Before every step the world is replaced by a copy of itself: what a snapshot keeps (a rewind, a quicksave)
    // is then all a replay needs, or the replay differs from its golden.
    bool copies = false;

  private:
    LvrPlayer(Replay replay, assets::GameFiles files, std::unique_ptr<LoadedLevel> loaded, SharedGraphics shared,
              int slot, Draw draw_with);
    Refresh picture();
    std::vector<Sound> take_sounds();
    void play_step();

    Draw draw_;
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
