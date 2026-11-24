#pragma once

// The Classic renderer (ADR-0005, ADR-0006): the 320 x 240 picture the original shows, from what the engine
// has to show (a View): the map at the camera, the sprites, the foreground tiles over them, the text, and the HUD.
// It draws; it decides nothing. What it draws is held by the replays' goldens (replays/README.md).

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "assets/assets.hpp"
#include "assets/graphics.hpp"
#include "lvr/level.hpp"

namespace lvr {

inline constexpr int kScreenWidth = 320;
inline constexpr int kScreenHeight = 240;
inline constexpr int kPlayfieldHeight = 176;  // above the HUD
inline constexpr int kTextColumns = 40;       // the text layer: cells of 8 x 8 pixels
inline constexpr int kTextRows = 22;
inline constexpr int kSquares = 13;  // the inventory's squares: four per Viking, then the trash can

struct Screen {
    std::vector<std::uint8_t> pixels = std::vector<std::uint8_t>(std::size_t{kScreenWidth} * kScreenHeight);
    assets::Palette palette{};
};

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
    std::vector<assets::IndexedImage> tileset;        // 8 x 8 tiles
    std::vector<std::uint8_t> tile_masks;             // 8 bytes a tile: the pixels drawn in front of the sprites
    std::vector<assets::Metatile> metatiles;          // a cell's picture: 2 x 2 tiles
    std::optional<assets::IndexedImage> picture;      // a scene without a map: its screen
    std::optional<assets::IndexedImage> hud_picture;  // in the HUD's place (the title, the starfield, the ship)
};

// One piece of a sprite, as drawn.
struct DrawnSprite {
    const assets::IndexedImage* image = nullptr;  // colour 0 is see-through
    int x = 0;                                    // its top-left corner in the level
    int y = 0;
    bool mirrored = false;
    bool flipped = false;
};

// What the HUD shows.
struct HudView {
    enum class Portrait : std::uint8_t { kChosen, kWaiting, kDead };
    struct Viking {
        bool shown = false;
        Portrait portrait = Portrait::kWaiting;
        int health = 0;  // which of the health pictures
    };
    std::array<Viking, 3> vikings;                   // Erik, Baleog, Olaf
    std::array<std::optional<int>, kSquares> icons;  // the item's icon in each square; none: left as the panel has it
    std::array<bool, kSquares> picker{};             // the picker's corners around the square
};

// Everything one picture shows.
struct View {
    int camera_x = 0;  // the view's top-left corner in the level, the shake included
    int camera_y = 0;
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

}  // namespace lvr
