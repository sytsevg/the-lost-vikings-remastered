#pragma once

// The Classic renderer (ADR-0005, ADR-0006): the 320 x 240 picture the original shows, from what the engine
// has to show (a View): the map at the camera, the sprites, the foreground tiles over them, the text, and the HUD.
// It draws; it decides nothing. What it draws is held by the replays' goldens (replays/README.md).

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "assets/assets.hpp"
#include "assets/graphics.hpp"
#include "lvr/backdrop.hpp"
#include "lvr/level.hpp"

namespace lvr {

struct Minimap;  // (minimap.hpp)

// The display the original runs on: Mode X, 527 lines (docs/original/timing.md). A tick is three refreshes.
inline constexpr double kDisplayHz = 25'175'000.0 / (800.0 * 527.0);

inline constexpr int kScreenWidth = 320;
inline constexpr int kScreenHeight = 240;
inline constexpr int kPlayfieldHeight = 176;  // above the HUD
// The text layer: cells of 8 x 8 pixels over the playfield, as many as the view has (the original's: 40 x 22).
// A view's `text` has room for the largest view; a row of it is always kTextColumns cells.
inline constexpr int kTextColumns = 80;
inline constexpr int kTextRows = 60;
inline constexpr int kSquares = 13;  // the inventory's squares: four per Viking, then the trash can

// Where things are on the picture, for whatever draws it.
inline constexpr int kTileSize = 8;
inline constexpr int kPictureTop = 32;  // a scene's picture starts 32 rows above the level's first row
inline constexpr std::size_t kPickerIcon = 19;
// The HUD's parts, from the HUD's top-left corner (docs/formats/level.md).
inline constexpr std::array<std::array<int, 2>, 3> kPortraitAt{{{45, 19}, {117, 19}, {189, 19}}};
inline constexpr std::array<std::array<int, 2>, 3> kHealthAt{{{47, 43}, {119, 43}, {191, 43}}};
// The game's font begins at this character (VIKINGS.EXE's text): below it nothing is drawn.
inline constexpr std::uint8_t kFirstGlyph = 0x10;
// Where a portrait's middle is across, the top of a name under it (in the HUD's bottom border), and of the first
// above it (in its top border; the next one a line higher).
inline constexpr int kPortraitMiddle = 16;
inline constexpr int kNameRow = 54;
inline constexpr int kWatchingRow = 6;
inline constexpr int kNameLine = 9;
inline constexpr std::uint8_t kNameGrey = 12;  // the fixed colour a watching player's name is in (146, 146, 146)
inline constexpr std::array<std::array<int, 2>, kSquares> kSquareAt{{{79, 18},
                                                                     {95, 18},
                                                                     {79, 34},
                                                                     {95, 34},
                                                                     {151, 18},
                                                                     {167, 18},
                                                                     {151, 34},
                                                                     {167, 34},
                                                                     {223, 18},
                                                                     {239, 18},
                                                                     {223, 34},
                                                                     {239, 34},
                                                                     {255, 18}}};
// The picker is drawn as its icon's corners only: 5 pixels along each edge, 2 deep, and 3 more down each side.
constexpr bool picker_pixel(std::size_t x, std::size_t y) noexcept {
    const bool edge_x = x < 2 || x >= 14;
    const bool edge_y = y < 2 || y >= 14;
    return (edge_y && (x < 5 || x >= 11)) || (edge_x && (y < 5 || y >= 11));
}
// Whether the tileset's masks put pixel (x, y) of `tile` in front of the sprites. A mask bit per pixel, in the
// order the tile's bytes are stored: by plane (x % 4), then row, then x / 4; the most significant bit first.
inline bool in_front(const std::vector<std::uint8_t>& masks, std::size_t tile, std::size_t x, std::size_t y) {
    const std::size_t stored = ((x % 4) * 16) + (y * 2) + (x / 4);
    const std::size_t at = (tile * 8) + (stored / 8);
    return at < masks.size() && (masks[at] & (0x80U >> (stored % 8))) != 0;
}

// Smooth movement (Remaster rules, ADR-0005): what moves in a tick, so that a picture can be shown with it where
// an even movement over the tick puts it at the moment it is shown, between the screen's refreshes too. The
// original moves a tick's distance in three whole-pixel shares (7 is 3, 2, 2: share_of_move); shown smoothly, a
// thing is a third of a tick behind where the picture has it, and goes on evenly from there.
struct Motion {
    int phase = -1;  // which of a tick's three pictures this is; -1: not one of a tick's, nothing is moved
    // The camera as the step left it (not shaken), and where it was at the end of the tick before and is at the
    // end of this one; without those (a tick after a scene's start, a change of view) it isn't moved.
    std::array<int, 2> scroll{};
    std::optional<std::array<int, 2>> scroll_from;
    std::array<int, 2> scroll_to{};
    std::array<int, 2> view{};  // the view's size: the camera is only moved between ticks of the same view
    // Each sprite's (of the view's, in order) movement in the tick: its entity's, which its pieces are moved by.
    std::vector<std::array<int, 2>> sprites;
};

// How far a thing moving `by` pixels in a tick is to be drawn from where picture `phase` of the tick has it,
// `between` (0 to 1) into the refresh that picture is shown for.
float moved_since(int by, int phase, float between) noexcept;
// The same for the camera of `motion`'s picture: x and y.
std::array<float, 2> camera_moved(const Motion& motion, float between) noexcept;

// The Modern visuals' light on a picture (effects.hpp): what gives it off (a box: its middle and half its size, in the
// picture's pixels), how far from that it reaches, and its colour there (0 to 1 each).
struct Light {
    float x = 0;
    float y = 0;
    float half_width = 0;
    float half_height = 0;
    float reach = 0;
    std::array<float, 3> colour{};
    // What gives it off, so that what is in front of that hides its halo (the user, 2026-11-26: a laser behind a
    // column, an enemy behind a Viking): the view's sprite (its place in View::sprites), the map (kFromMap: behind
    // every sprite), or nothing in particular (kAnywhere: laid over all).
    static constexpr std::int32_t kAnywhere = -1;
    static constexpr std::int32_t kFromMap = -2;
    std::int32_t from = kAnywhere;

    bool operator==(const Light&) const = default;
};
inline constexpr std::size_t kMostLights = 64;  // a picture's most (gpu_show.frag takes as many)

// What stops the Modern visuals' light (effects.hpp): a pixel of the level for a pixel, non-zero where the map is
// solid to it (its ground and walls, and slopes under their surface). One is made anew only when the map changes
// (`id` says which).
struct Ground {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> solid;
    std::uint64_t id = 0;
};

struct Screen {
    std::vector<std::uint8_t> pixels = std::vector<std::uint8_t>(std::size_t{kScreenWidth} * kScreenHeight);
    assets::Palette palette{};
    std::int64_t kept = -1;  // drawn by a renderer that keeps its pictures itself (lvr/gpu.hpp): which one; no
                             // pixels here
    Motion motion;
    // The part of the playfield that is shown, scaled to the playfield's place on screen: all of the original's
    // view, or (Remaster rules' zoom) of a larger one, or a part of it while the zoom changes.
    struct Part {
        float x = 0;
        float y = 0;
        float width = kScreenWidth;
        float height = kPlayfieldHeight;
        bool operator==(const Part&) const = default;
    } part;
    // Where the picture's top left corner is in the level (for what is laid over it by place: the minimap).
    int level_x = 0;
    int level_y = 0;
    // Where the Vikings are in the level as the picture was drawn (Erik, Baleog, Olaf), for the minimap.
    struct Viking {
        int x = 0;
        int y = 0;
        bool there = false;
        bool played = false;
    };
    std::array<Viking, 3> vikings{};
    // Which refresh of the display this picture is shown for, counted from the session's start (a picture shown
    // again has the refresh it is shown again for), and back with a rewind: for a backdrop that moves by itself (the
    // spaceship's stars), which goes back with the play (the user, 2026-11-26) and on while it holds.
    std::uint32_t refresh = 0;
    bool rewinding = false;  // the picture is one of time running backwards (Remaster rules): it is marked so
    bool fading = false;     // a scene's going dark or coming in
    // How bright the picture is for a scene's fade (or a game online rising from black): 1 not darkened, 0 black.
    // What is laid over it (the minimap) goes as dark.
    float bright = 1;
    // Remaster rules, in a level: its minimap as it was when the picture was drawn (a scene fading out keeps its
    // own while the next is already there).
    std::shared_ptr<const Minimap> minimap;
    // Remaster rules, in a level being played: how much of the level's rewind is left (0 to 1), shown in a bar along
    // the minimap's bottom.
    std::optional<float> rewind_left;
    // The Modern visuals' effects (effects.hpp), for what shows the picture: the lights on it, and how much light
    // each colour of the palette gives off around it (0: none, 255: as bright as it is).
    std::vector<Light> lights;
    std::array<std::uint8_t, 256> glow{};
    std::shared_ptr<const Ground> ground;  // what stops the lights; none: nothing does
};

// What draws a view: draw() below, or something that is to make the same picture.
struct View;
struct SceneGraphics;
struct SharedGraphics;
using Draw = std::function<void(Screen&, const View&, const SceneGraphics&, const SharedGraphics&)>;

// The pictures every scene shares, loaded once.
struct SharedGraphics {
    std::vector<assets::IndexedImage> font;       // by character - 0x10
    assets::IndexedImage hud_panel;               // the HUD's stone frame
    std::vector<assets::IndexedImage> portraits;  // chosen, waiting, dead: Erik, Baleog, Olaf each
    std::vector<assets::IndexedImage> health;     // the health dots: 7 pictures
    std::vector<assets::IndexedImage> items;      // the inventory's icons; 0 is the empty square
};

std::expected<SharedGraphics, std::string> load_shared_graphics(const assets::Archive& archive);

// What a level or cutscene is drawn from.
struct SceneGraphics {
    // Which pictures these are: another number whenever they are others (next_graphics_id), so that what is kept
    // of them elsewhere (the GPU renderer's atlas) knows. 0: not said; nothing is kept.
    std::uint32_t id = 0;
    std::vector<assets::IndexedImage> tileset;        // 8 x 8 tiles
    std::vector<std::uint8_t> tile_masks;             // 8 bytes a tile: the pixels drawn in front of the sprites
    std::vector<assets::Metatile> metatiles;          // a cell's picture: 2 x 2 tiles
    std::optional<assets::IndexedImage> picture;      // a scene without a map: its screen
    std::optional<assets::IndexedImage> hud_picture;  // in the HUD's place (the title, the starfield, the ship)
    // Remaster rules' parallax: the SNES version's backdrop behind the map (backdrop.hpp). Only the GPU renderer
    // draws it, when a picture is shown; the Classic picture never has it.
    std::optional<Backdrop> backdrop;
};

std::uint32_t next_graphics_id() noexcept;

// One piece of a sprite, as drawn.
struct DrawnSprite {
    const assets::IndexedImage* image = nullptr;  // colour 0 is see-through
    int x = 0;                                    // its top-left corner in the level
    int y = 0;
    bool mirrored = false;
    bool flipped = false;
    std::array<int, 2> moves{};  // how far its entity moves in this tick (x, y)
    int piece = -1;              // the piece it is (the effects find an entity's sprites by them)
};

// What the HUD shows.
struct HudView {
    enum class Portrait : std::uint8_t { kChosen, kWaiting, kDead };
    struct Viking {
        bool shown = false;
        Portrait portrait = Portrait::kWaiting;
        int health = 0;  // which of the health pictures
    };
    std::array<Viking, 3> vikings;  // Erik, Baleog, Olaf
    // Co-op online (ADR-0012): under each portrait the name of the player who plays him, in the game's font, its
    // letters only; above it, grey, those watching him, in their line's order, as one name (A/B). Empty: none.
    std::array<std::string, 3> names;
    std::array<std::vector<std::string>, 3> watching;
    std::array<std::optional<int>, kSquares> icons;  // the item's icon in each square; none: left as the panel has it
    std::array<bool, kSquares> picker{};             // the picker's corners around the square
};

// Everything one picture shows.
struct View {
    int camera_x = 0;  // the view's top-left corner in the level, the shake included
    int camera_y = 0;
    // How much of the level the playfield shows. Only the GPU renderer draws another size than the original's.
    int width = kScreenWidth;
    int height = kPlayfieldHeight;
    // Rows under the playfield its sprites and text are drawn on too, where the HUD's place has nothing: Remaster
    // rules' title menu, its pointer on the last of six choices reaching under the text layer's last row (the user,
    // 2026-11-26). Elsewhere they end at the playfield's bottom.
    int drawn_below = 0;
    int text_x = 0;  // where the text layer's first cell is on screen (it moves with the camera's last 3 bits)
    int text_y = 0;
    const Map* map = nullptr;          // none: a scene that is one picture
    std::vector<DrawnSprite> sprites;  // in the order they are drawn: later ones over earlier ones
    std::array<std::uint8_t, std::size_t{kTextColumns} * kTextRows> text{};  // characters; below 0x10: nothing
    std::optional<HudView> hud;
    assets::Palette palette{};
    // Darkening: each colour's red, green and blue less these (in the VGA's 64 levels). Colours 1-15, the HUD's
    // and the text's, keep theirs unless `darken_all` (a fade between scenes).
    std::array<std::uint8_t, 3> darken{};
    bool darken_all = false;
    std::optional<assets::Rgb> colour3;  // the text's background colour, when a text or menu set it
};

// The palette as shown: `palette` darkened as above.
assets::Palette darkened(const assets::Palette& palette, std::array<std::uint8_t, 3> by, bool all);

void draw(Screen& screen, const View& view, const SceneGraphics& scene, const SharedGraphics& shared);
// What draws nothing: for a session whose pictures nobody looks at (a test of the play, not of what it shows).
inline void draw_nothing(Screen& /*screen*/, const View& /*view*/, const SceneGraphics& /*scene*/,
                         const SharedGraphics& /*shared*/) {}

// A picture `share` (0 to 1) as bright, and everything that goes dark with it: its colours, the original's levels off
// them as a scene's fade takes them (unless they were darkened so as it was drawn: View::darken), the lights on it,
// and Screen::bright, by which what is laid over it (the minimap, the rewind's bar) loses as many. Every fade goes
// through here, the scenes' and the places' (the user, 2026-11-26: online the minimap stayed lit on a black screen).
void darken(Screen& picture, float share, bool colours_drawn_dark = false);

}  // namespace lvr
