#pragma once

// The GPU renderer (ADR-0006): the picture draw() makes (render.hpp), drawn by the graphics card. Every picture a
// scene is made of sits in an atlas of palette indices (a sprite's from the first time it is shown); a picture on
// screen is a rectangle of it, drawn a pixel for a pixel into a target of indices. Read back, that target is the
// Classic picture exactly. For a window a picture is kept as those rectangles and drawn when it is shown: as large
// as it is shown, and with what moves between ticks where it is at that moment (Motion, render.hpp).

#include <cstddef>
#include <cstdint>
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

    // For a window: the picture is kept, to be drawn when it is shown, and `screen` only says which one it is
    // (`kept`, with its palette; no pixels). The last 512 are kept; showing one lets go of those before it.
    std::expected<void, std::string> keep(Screen& screen, const View& view, const SceneGraphics& scene,
                                          const SharedGraphics& shared);
    // A kept picture in the window, its colours looked up: as large as whole pixels allow, in the middle, and
    // `between` (0 to 1) into the refresh it is shown for (what moves between ticks is moved by that much).
    // With `minimap`: a square of it around the view in the bottom left corner, the HUD moved right to make room.
    std::expected<void, std::string> show(const Screen& screen, const Minimap* minimap = nullptr, float between = 0);
    // The same without a window, `scale` times the original's 320 x 240, read back: red, green, blue and alpha a
    // pixel. For a test or a dump.
    std::expected<std::vector<std::uint8_t>, std::string> shown(const Screen& screen, int scale = 1,
                                                                const Minimap* minimap = nullptr, float between = 0);
    // A kept picture's view alone, as large as it is (`scale` pixels a pixel of it), without the rows the HUD lies
    // over or the HUD: with a view as large as a level, a picture of all of it (a development tool). Read back as
    // shown() reads it.
    struct Whole {
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> pixels;
    };
    std::expected<Whole, std::string> shown_whole(const Screen& screen, int scale = 1);

    // How many times a page of the atlas was set up (a scene's pictures put in anew): for finding what is slow.
    std::size_t pages_set_up() const noexcept;
    // How the last show() went, for finding what is slow: milliseconds waiting for the window's picture to draw
    // on (long when the card is behind), recording the drawing, and handing it over; and what was drawn.
    struct ShowTimes {
        double waiting = 0;
        double recording = 0;
        double handing_over = 0;
        int drawn_scale = 0;  // pixels a pixel of the picture, as it was drawn
        int width = 0;        // the picture drawn: its size, and the window's
        int height = 0;
        int window_width = 0;
        int window_height = 0;
        int samples = 0;       // the colours mixed for a pixel shown, along each side
        bool remade = false;   // the texture it is drawn into was made anew
        bool redrawn = false;  // the picture was drawn into it anew (else: as it was the show before)
        int kept = 0;          // pictures kept since the show before
        int uploads = 0;       // of those, how many sent new pictures to the atlas, and how much
        std::size_t uploaded_bytes = 0;
    };
    const ShowTimes& show_times() const noexcept;
    // The graphics card used, and its driver, as the driver names them.
    std::string device_name() const;

  private:
    struct State;
    explicit GpuRenderer(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

}  // namespace lvr
