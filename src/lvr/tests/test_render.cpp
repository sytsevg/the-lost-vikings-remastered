// Tests for src/lvr/render.*: the Classic renderer on hand-made views. That its pictures are the original's is
// checked on the replays by the pixel-match harness (lv-replay --renderer).
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include "assets/assets.hpp"
#include "assets/graphics.hpp"
#include "lvr/level.hpp"
#include "lvr/render.hpp"
#include "lvr/sprites.hpp"

using namespace lvr;

namespace {

assets::IndexedImage filled(std::size_t width, std::size_t height, std::uint8_t colour) {
    assets::IndexedImage image = assets::make_image(width, height);
    std::ranges::fill(image.pixels, colour);
    return image;
}

std::uint8_t at(const Screen& screen, int x, int y) {
    return screen.pixels.at((static_cast<std::size_t>(y) * kScreenWidth) + static_cast<std::size_t>(x));
}

// Two tiles (colours 10 and 20; the second with its left column 21) and two metatiles: 0 all of tile 0; 1 has tile
// 1 top-left as it is, top-right mirrored, bottom-left flipped, bottom-right in front of the sprites.
SceneGraphics sample_scene() {
    SceneGraphics scene;
    scene.tileset = {filled(8, 8, 10), filled(8, 8, 20)};
    for (std::size_t y = 0; y < 8; ++y) {
        scene.tileset[1].set(0, y, 21);
    }
    scene.tileset[1].set(3, 0, 22);  // a mark in the top row
    scene.tile_masks.assign(16, 0);
    scene.tile_masks[8] = 0x80;  // tile 1: only its first stored pixel (0, 0) is in front
    const auto ref = [](std::uint16_t tile, std::uint8_t flags) {
        return assets::TileRef{.tile = tile, .flags = flags};
    };
    scene.metatiles = {
        assets::Metatile{.parts = {ref(0, 0), ref(0, 0), ref(0, 0), ref(0, 0)}},
        assets::Metatile{.parts = {ref(1, 0), ref(1, assets::TileRef::kFlipH), ref(1, assets::TileRef::kFlipV),
                                   ref(1, assets::TileRef::kForeground)}}};
    return scene;
}

// 30 x 20 cells of metatile 0, with metatile 1 at cell (1, 0).
Map sample_map() {
    Map map{.width = 30, .height = 20, .cells = std::vector<Cell>(600)};
    map.cells[1].metatile = 1;
    return map;
}

SharedGraphics sample_shared() {
    SharedGraphics shared;
    shared.font.assign(80, assets::make_image(8, 8));
    shared.font[0x31].set(1, 2, 2);  // 'A'
    shared.hud_panel = filled(320, 64, 5);
    for (std::uint8_t i = 0; i < 9; ++i) {
        shared.portraits.push_back(filled(32, 24, static_cast<std::uint8_t>(100 + i)));
    }
    for (std::uint8_t i = 0; i < 7; ++i) {
        shared.health.push_back(filled(24, 4, static_cast<std::uint8_t>(120 + i)));
    }
    for (std::uint8_t i = 0; i < 24; ++i) {
        shared.items.push_back(filled(16, 16, static_cast<std::uint8_t>(130 + i)));
    }
    return shared;
}

}  // namespace

TEST_CASE("render: the map at the camera, tiles mirrored and flipped") {
    const SceneGraphics scene = sample_scene();
    const Map map = sample_map();
    Screen screen;
    View view;
    view.map = &map;
    draw(screen, view, scene, SharedGraphics{});
    CHECK(at(screen, 0, 0) == 10);
    CHECK(at(screen, 16, 0) == 21);  // metatile 1's top-left tile: its left column
    CHECK(at(screen, 19, 0) == 22);
    CHECK(at(screen, 17, 0) == 20);
    CHECK(at(screen, 31, 0) == 21);  // top-right, mirrored: the column is at its right
    CHECK(at(screen, 28, 0) == 22);
    CHECK(at(screen, 19, 15) == 22);  // bottom-left, flipped: the top row is at its bottom
    CHECK(at(screen, 19, 8) == 20);
    CHECK(at(screen, 319, 175) == 10);
    CHECK(at(screen, 0, 176) == 0);  // nothing of the map below the playfield

    view.camera_x = 17;
    view.camera_y = 1;
    draw(screen, view, scene, SharedGraphics{});
    CHECK(at(screen, 0, 0) == 20);
    CHECK(at(screen, 2, 0) == 20);
    CHECK(at(screen, 14, 0) == 21);

    // Past the map's edges: nothing.
    view.camera_x = 30 * 16 - 100;
    view.camera_y = -8;
    draw(screen, view, scene, SharedGraphics{});
    CHECK(at(screen, 99, 8) == 10);
    CHECK(at(screen, 100, 8) == 0);
    CHECK(at(screen, 0, 7) == 0);
}

TEST_CASE("render: a metatile past the scene's list wraps, or is left out") {
    SceneGraphics scene = sample_scene();
    Map map = sample_map();
    map.cells[0].metatile = 0x201;  // 0x201 & 0x1ff = 1
    map.cells[2].metatile = 0x1f0;
    Screen screen;
    View view;
    view.map = &map;
    draw(screen, view, scene, SharedGraphics{});
    CHECK(at(screen, 0, 0) == 21);
    CHECK(at(screen, 32, 0) == 0);
    scene.metatiles[0].parts[0].tile = 99;  // a tile the scene doesn't have
    draw(screen, view, scene, SharedGraphics{});
    CHECK(at(screen, 48, 0) == 0);
    CHECK(at(screen, 56, 0) == 10);
}

TEST_CASE("render: sprites over the map, the foreground's pixels over the sprites") {
    const SceneGraphics scene = sample_scene();
    const Map map = sample_map();
    assets::IndexedImage picture = filled(16, 16, 50);
    picture.set(0, 0, 51);
    picture.set(5, 5, 0);  // see-through
    Screen screen;
    View view;
    view.map = &map;
    view.sprites = {DrawnSprite{.image = &picture, .x = 16, .y = 0}};
    draw(screen, view, scene, SharedGraphics{});
    CHECK(at(screen, 16, 0) == 51);
    CHECK(at(screen, 17, 0) == 50);
    CHECK(at(screen, 21, 5) == 20);  // the map shows through colour 0
    CHECK(at(screen, 24, 8) == 21);  // the foreground tile's masked pixel (its top-left) is over the sprite
    CHECK(at(screen, 25, 8) == 50);

    // Mirrored and flipped, a later sprite over an earlier one, clipped at the screen's edge and the HUD's.
    assets::IndexedImage other = filled(8, 8, 60);
    view.sprites = {DrawnSprite{.image = &picture, .x = 100, .y = 100, .mirrored = true, .flipped = true},
                    DrawnSprite{.image = &other, .x = 104, .y = 104}, DrawnSprite{.image = &picture, .x = -8, .y = 170},
                    DrawnSprite{}};
    draw(screen, view, scene, SharedGraphics{});
    CHECK(at(screen, 115, 115) == 51);
    CHECK(at(screen, 100, 100) == 50);
    CHECK(at(screen, 104, 104) == 60);
    CHECK(at(screen, 0, 170) == 50);
    CHECK(at(screen, 0, 175) == 50);
    CHECK(at(screen, 0, 176) == 0);
    view.camera_x = 90;
    view.camera_y = 90;
    draw(screen, view, scene, SharedGraphics{});
    CHECK(at(screen, 14, 14) == 60);
}

TEST_CASE("render: the text layer at its offset") {
    const SharedGraphics shared = sample_shared();
    Screen screen;
    View view;
    view.text[0] = 'A';
    view.text[(std::size_t{2} * kTextColumns) + 3] = 'A';
    view.text[1] = 0x05;  // no glyph
    view.text[2] = 0xff;
    view.text_x = -3;
    view.text_y = -1;
    draw(screen, view, SceneGraphics{}, shared);
    CHECK(at(screen, 1 + 24 - 3, 2 + 16 - 1) == 2);
    std::size_t drawn = 0;
    for (const std::uint8_t pixel : screen.pixels) {
        drawn += pixel == 2 ? 1 : 0;
    }
    CHECK(drawn == 1);  // the first 'A' has its pixel left of the screen
}

TEST_CASE("render: a scene's picture, and a picture in the HUD's place") {
    SceneGraphics scene;
    scene.picture = filled(320, 200, 7);
    scene.hud_picture = filled(320, 100, 8);
    Screen screen;
    View view;
    view.camera_y = 32;
    draw(screen, view, scene, SharedGraphics{});
    CHECK(at(screen, 0, 0) == 7);
    CHECK(at(screen, 319, 175) == 7);
    CHECK(at(screen, 0, 176) == 8);
    CHECK(at(screen, 319, 239) == 8);
    view.camera_y = 24;  // 8 rows lower
    draw(screen, view, scene, SharedGraphics{});
    CHECK(at(screen, 0, 7) == 0);
    CHECK(at(screen, 0, 8) == 7);
}

TEST_CASE("render: the HUD: panel, portraits, health, items and the picker's corners") {
    const SharedGraphics shared = sample_shared();
    Screen screen;
    View view;
    view.hud = HudView{};
    view.hud->vikings[0] = {.shown = true, .portrait = HudView::Portrait::kChosen, .health = 3};
    view.hud->vikings[2] = {.shown = true, .portrait = HudView::Portrait::kDead, .health = 99};
    view.hud->icons[0] = 4;
    view.hud->icons[5] = 99;  // no such icon
    view.hud->icons[12] = 23;
    view.hud->picker[0] = true;
    draw(screen, view, SceneGraphics{}, shared);
    CHECK(at(screen, 0, 176) == 5);
    CHECK(at(screen, 319, 239) == 5);
    CHECK(at(screen, 45, 176 + 19) == 100);       // Erik, chosen
    CHECK(at(screen, 117, 176 + 19) == 5);        // Baleog isn't shown
    CHECK(at(screen, 189, 176 + 19) == 100 + 8);  // Olaf, dead
    CHECK(at(screen, 47, 176 + 43) == 123);
    CHECK(at(screen, 191, 176 + 43) == 5);  // no such health picture
    CHECK(at(screen, 79 + 8, 176 + 18 + 8) == 134);
    CHECK(at(screen, 79, 176 + 18) == 130 + 19);  // the picker's corner over the item
    CHECK(at(screen, 79 + 4, 176 + 18) == 130 + 19);
    CHECK(at(screen, 79 + 5, 176 + 18) == 134);
    CHECK(at(screen, 79, 176 + 18 + 4) == 130 + 19);
    CHECK(at(screen, 79 + 2, 176 + 18 + 4) == 134);
    CHECK(at(screen, 79 + 15, 176 + 18 + 15) == 130 + 19);
    CHECK(at(screen, 151 + 16, 176 + 18) == 5);
    CHECK(at(screen, 255, 176 + 18) == 153);
}

TEST_CASE("render: the palette darkened, colours 1-15 kept unless everything fades; colour 3") {
    assets::Palette palette{};
    palette[0] = {.r = 255, .g = 130, .b = 8};
    palette[5] = {.r = 255, .g = 255, .b = 255};
    palette[200] = {.r = 40, .g = 80, .b = 255};
    CHECK(darkened(palette, {0, 0, 0}, false) == palette);
    const assets::Palette tinted = darkened(palette, {10, 0, 63}, false);
    CHECK(tinted[0] == assets::Rgb{.r = 215, .g = 130, .b = 0});  // 63 - 10 = 53 -> 215
    CHECK(tinted[5] == palette[5]);
    CHECK(tinted[200] == assets::Rgb{.r = 0, .g = 81, .b = 0});  // 80 is level 20, shown as 81
    const assets::Palette faded = darkened(palette, {63, 63, 63}, true);
    CHECK(faded[5] == assets::Rgb{});

    Screen screen;
    View view;
    view.palette = palette;
    view.darken = {10, 0, 63};
    view.colour3 = assets::Rgb{.r = 1, .g = 2, .b = 3};
    draw(screen, view, SceneGraphics{}, SharedGraphics{});
    CHECK(screen.palette[0] == tinted[0]);
    CHECK(screen.palette[3] == assets::Rgb{.r = 1, .g = 2, .b = 3});
    view.darken_all = true;
    draw(screen, view, SceneGraphics{}, SharedGraphics{});
    CHECK(screen.palette[5] == assets::Rgb{.r = 215, .g = 255, .b = 0});
}

TEST_CASE("render: smooth movement: a tick's movement shown evenly, a third of a tick behind") {
    // Where a thing is shown, from where the tick began: where picture `phase` has it (the original's shares)
    // plus the correction.
    const auto shown_at = [](int by, int phase, float between) {
        int shifted = 0;
        for (int picture = 0; picture <= phase; ++picture) {
            shifted += share_of_move(by, picture);
        }
        return static_cast<float>(shifted) + moved_since(by, phase, between);
    };
    for (int by = -40; by <= 40; ++by) {
        INFO("by " << by);
        CHECK(shown_at(by, 0, 0) == 0);  // the tick's first picture: where the tick before ended
        for (int phase = 0; phase < 3; ++phase) {
            // Evenly: a third of the tick a refresh, without a jump from one picture to the next.
            CHECK(std::abs(shown_at(by, phase, 0.5F) -
                           (static_cast<float>(by) * (static_cast<float>(phase) + 0.5F) / 3.0F)) < 1e-4F);
            const float next = phase < 2 ? shown_at(by, phase + 1, 0) : static_cast<float>(by);
            CHECK(std::abs(shown_at(by, phase, 1) - next) < 1e-4F);
        }
    }
    CHECK(moved_since(7, -1, 0.5F) == 0);  // not a tick's picture: as it is
    CHECK(moved_since(7, 3, 0.5F) == 0);

    Motion motion;
    motion.scroll = {103, 50};
    motion.scroll_to = {106, 44};
    motion.phase = 1;
    CHECK(camera_moved(motion, 0.5F) == std::array<float, 2>{});  // where the tick before ended isn't known
    motion.scroll_from = {{100, 50}};
    // A third of the way at the second picture: 102 and 48, where the step left it at 103 and 50.
    CHECK(camera_moved(motion, 0) == std::array<float, 2>{102 - 103, 48 - 50});
    CHECK(camera_moved(motion, 0.5F) == std::array<float, 2>{103 - 103, 47 - 50});
    motion.phase = -1;
    CHECK(camera_moved(motion, 0.5F) == std::array<float, 2>{});
}

TEST_CASE("real data: the shared pictures", "[real-data]") {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    const auto files = assets::GameFiles::open(LVR_ORIGINAL_DIR);
    REQUIRE(files);
    const auto shared = load_shared_graphics(files->archive());
    REQUIRE(shared);
    CHECK(shared->font.size() == 80);
    CHECK(shared->portraits.size() == 9);
    CHECK(shared->health.size() == 7);
    CHECK(shared->items.size() == 24);
    CHECK(shared->hud_panel.width == 344);  // a Mode X page's width; the screen shows its first 320
    CHECK(shared->hud_panel.height == 64);
}
