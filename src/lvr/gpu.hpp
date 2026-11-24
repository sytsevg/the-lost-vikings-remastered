#pragma once

// The GPU renderer (ADR-0006): the picture draw() makes (render.hpp), drawn by the graphics card. Every picture a
// scene is made of sits in one atlas of palette indices (a sprite's from the first time it is shown); a picture on
// screen is a rectangle of it, drawn a pixel for a pixel into a target of indices. Read back, that target is the
// Classic picture exactly.

#include <expected>
#include <memory>
#include <string>
#include <vector>

#include "lvr/minimap.hpp"
#include "lvr/render.hpp"

struct SDL_Window;

namespace lvr {

class GpuRenderer {
  public:
    // Without a window: for a tool or a test, on whatever Vulkan driver there is (a software one will do).
    static std::expected<std::unique_ptr<GpuRenderer>, std::string> open_headless();
    // On `window`, which must outlive it; SDL's video is the caller's to start.
    static std::expected<std::unique_ptr<GpuRenderer>, std::string> open_for(SDL_Window* window);
    ~GpuRenderer();
    GpuRenderer(const GpuRenderer&) = delete;
    GpuRenderer& operator=(const GpuRenderer&) = delete;

    // As draw() of render.hpp: the same `screen` from the same view.
    std::expected<void, std::string> draw(Screen& screen, const View& view, const SceneGraphics& scene,
                                          const SharedGraphics& shared);

    // For a window: the picture is drawn and kept on the card, and `screen` only says which one it is (`kept`,
    // with its palette; no pixels). The last 512 are kept.
    std::expected<void, std::string> keep(Screen& screen, const View& view, const SceneGraphics& scene,
                                          const SharedGraphics& shared);
    // A kept picture in the window, its colours looked up: as large as whole pixels allow, in the middle.
    // With `minimap`: a square of it around the view in the bottom left corner, the HUD moved right to make room.
    std::expected<void, std::string> show(const Screen& screen, const Minimap* minimap = nullptr);
    // The same without a window, `scale` times the original's 320 x 240, read back: red, green, blue and alpha a
    // pixel. For a test or a dump.
    std::expected<std::vector<std::uint8_t>, std::string> shown(const Screen& screen, int scale = 1,
                                                                const Minimap* minimap = nullptr);

  private:
    struct State;
    explicit GpuRenderer(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

}  // namespace lvr
