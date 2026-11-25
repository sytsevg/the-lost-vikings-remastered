// Tests for src/lvr/gpu.*: the GPU renderer makes the picture the Classic one does (render.*), on hand-made views
// with everything a picture can show. That it does so on the replays: lv-replay --gpu.
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "assets/assets.hpp"
#include "assets/graphics.hpp"
#include "lvr/gpu.hpp"
#include "lvr/level.hpp"
#include "lvr/render.hpp"

using namespace lvr;

namespace {

// A picture with a different colour at every pixel that matters: what is mirrored, flipped or cut shows.
assets::IndexedImage patterned(std::size_t width, std::size_t height, std::uint8_t first, bool holes = false) {
    assets::IndexedImage image = assets::make_image(width, height);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const auto colour = static_cast<std::uint8_t>(first + ((x + (3 * y)) % 40));
            image.set(x, y, holes && (x + y) % 3 == 0 ? std::uint8_t{0} : colour);
        }
    }
    return image;
}

SceneGraphics sample_scene() {
    SceneGraphics scene;
    for (std::uint8_t tile = 0; tile < 3; ++tile) {
        scene.tileset.push_back(patterned(8, 8, static_cast<std::uint8_t>(10 + (50 * tile))));
    }
    scene.tile_masks.assign(24, 0);
    scene.tile_masks[8] = 0xa5;  // tile 1: some pixels in front of the sprites
    scene.tile_masks[13] = 0x3c;
    std::ranges::fill_n(scene.tile_masks.begin() + 16, 8, std::uint8_t{0xff});  // tile 2: all of it
    const auto ref = [](std::uint16_t tile, int flags) {
        return assets::TileRef{.tile = tile, .flags = static_cast<std::uint8_t>(flags)};
    };
    scene.metatiles = {
        assets::Metatile{.parts = {ref(0, 0), ref(0, assets::TileRef::kFlipH), ref(0, assets::TileRef::kFlipV),
                                   ref(0, assets::TileRef::kFlipH | assets::TileRef::kFlipV)}},
        assets::Metatile{.parts = {ref(1, assets::TileRef::kForeground),
                                   ref(1, assets::TileRef::kForeground | assets::TileRef::kFlipH),
                                   ref(2, assets::TileRef::kForeground | assets::TileRef::kFlipV), ref(2, 0)}}};
    return scene;
}

SharedGraphics sample_shared() {
    SharedGraphics shared;
    for (std::uint8_t i = 0; i < 80; ++i) {
        shared.font.push_back(patterned(8, 8, i, true));
    }
    shared.hud_panel = patterned(344, 64, 5);
    for (std::uint8_t i = 0; i < 9; ++i) {
        shared.portraits.push_back(patterned(32, 24, static_cast<std::uint8_t>(100 + i)));
    }
    for (std::uint8_t i = 0; i < 7; ++i) {
        shared.health.push_back(patterned(24, 4, static_cast<std::uint8_t>(120 + i)));
    }
    for (std::uint8_t i = 0; i < 24; ++i) {
        shared.items.push_back(patterned(16, 16, static_cast<std::uint8_t>(130 + i)));
    }
    return shared;
}

// Both renderers on the same view: the same pixels and the same palette.
void check_same(GpuRenderer& gpu, const View& view, const SceneGraphics& scene, const SharedGraphics& shared) {
    Screen classic;
    Screen drawn;
    draw(classic, view, scene, shared);
    const auto done = gpu.draw(drawn, view, scene, shared);
    REQUIRE(done);
    CHECK(drawn.palette == classic.palette);
    const auto [ours, theirs] = std::ranges::mismatch(drawn.pixels, classic.pixels);
    INFO("the first pixel that differs: " << (ours - drawn.pixels.begin()) % kScreenWidth << ", "
                                          << (ours - drawn.pixels.begin()) / kScreenWidth);
    CHECK(ours == drawn.pixels.end());
}

}  // namespace

TEST_CASE("gpu: the picture is the Classic renderer's") {
    auto opened = GpuRenderer::open_headless();
    if (!opened) {
        SKIP("no graphics device: " << opened.error());
    }
    GpuRenderer& gpu = **opened;
    const SharedGraphics shared = sample_shared();
    SceneGraphics scene = sample_scene();
    Map map{.width = 30, .height = 20, .cells = std::vector<Cell>(600)};
    for (std::size_t cell = 0; cell < map.cells.size(); cell += 3) {
        map.cells[cell].metatile = 1;
    }
    map.cells[7].metatile = 0x201;  // past the metatiles: wraps to 1
    map.cells[8].metatile = 0x3ff;  // and one that doesn't exist either way
    const assets::IndexedImage piece = patterned(32, 16, 60, true);
    const assets::IndexedImage small = patterned(8, 8, 200, true);

    View view;
    view.map = &map;
    view.palette[7] = {.r = 200, .g = 100, .b = 50};
    view.sprites = {{.image = &piece, .x = 20, .y = 10},
                    {.image = &piece, .x = 40, .y = 18, .mirrored = true},
                    {.image = &piece, .x = 70, .y = 30, .flipped = true},
                    {.image = &small, .x = 75, .y = 34, .mirrored = true, .flipped = true},
                    {.image = nullptr, .x = 0, .y = 0},
                    {.image = &piece, .x = -10, .y = -5},    // over the picture's corner
                    {.image = &piece, .x = 300, .y = 170}};  // over its right edge and into the HUD's rows
    view.text[3] = 0x31;
    view.text[(21 * kTextColumns) + 39] = 0x20;  // the last cell: cut at the HUD
    view.text[50] = 0x0f;                        // not a character
    view.text[51] = 0xff;                        // nor is this
    SECTION("a level: map, sprites, text and the HUD") {
        HudView hud;
        hud.vikings[0] = {.shown = true, .portrait = HudView::Portrait::kChosen, .health = 3};
        hud.vikings[2] = {.shown = true, .portrait = HudView::Portrait::kDead, .health = 9};
        hud.icons[0] = 4;
        hud.icons[5] = 99;
        hud.icons[12] = 2;
        hud.picker[0] = hud.picker[6] = true;
        view.hud = hud;
        check_same(gpu, view, scene, shared);

        // Scrolled, the text with it, and darkened.
        view.camera_x = 37;
        view.camera_y = 141;
        view.text_x = -5;
        view.text_y = -3;
        view.darken = {4, 8, 12};
        view.colour3 = assets::Rgb{.r = 1, .g = 2, .b = 3};
        check_same(gpu, view, scene, shared);
        view.camera_x = 400;  // past the map's right edge
        view.darken_all = true;
        check_same(gpu, view, scene, shared);
    }
    SECTION("a scene that is a picture, with one in the HUD's place") {
        view.map = nullptr;
        scene.picture = patterned(320, 200, 30);
        scene.hud_picture = patterned(344, 100, 90);
        check_same(gpu, view, scene, shared);
        view.camera_y = 20;
        check_same(gpu, view, scene, shared);
        // Other pictures under another number: the old ones aren't kept.
        scene.id = next_graphics_id();
        check_same(gpu, view, scene, shared);
        scene.picture = patterned(320, 200, 31);
        scene.hud_picture = patterned(100, 30, 91);  // smaller than the HUD
        scene.id = next_graphics_id();
        check_same(gpu, view, scene, shared);
    }
    SECTION("a sprite's picture is kept from the first time it is shown, and found again by what it looks like") {
        scene.id = next_graphics_id();  // (pictures of a scene without a number aren't kept)
        check_same(gpu, view, scene, shared);
        check_same(gpu, view, scene, shared);
        // Another picture in the same place, and the same picture from another place.
        assets::IndexedImage changing = patterned(32, 16, 61, true);
        view.sprites.push_back({.image = &changing, .x = 100, .y = 60});
        check_same(gpu, view, scene, shared);
        changing = patterned(32, 16, 62, true);
        check_same(gpu, view, scene, shared);
        const assets::IndexedImage copy = piece;
        view.sprites.push_back({.image = &copy, .x = 150, .y = 90, .mirrored = true});
        check_same(gpu, view, scene, shared);
        // More sprites than the atlas holds, over time: the ones no longer shown make room.
        for (int round = 0; round < 3; ++round) {
            std::vector<assets::IndexedImage> many;
            for (int i = 0; i < 40; ++i) {
                many.push_back(patterned(320, 176, static_cast<std::uint8_t>((round * 40) + i + 1)));
            }
            view.sprites.clear();
            for (const assets::IndexedImage& image : many) {
                view.sprites.push_back({.image = &image, .x = 0, .y = 0});
            }
            check_same(gpu, view, scene, shared);
        }
    }
    SECTION("nothing at all") {
        check_same(gpu, View{}, SceneGraphics{}, shared);
    }
}

TEST_CASE("gpu: a kept picture is shown with its colours looked up") {
    auto opened = GpuRenderer::open_headless();
    if (!opened) {
        SKIP("no graphics device: " << opened.error());
    }
    GpuRenderer& gpu = **opened;
    const SharedGraphics shared = sample_shared();
    SceneGraphics scene = sample_scene();
    scene.picture = patterned(320, 200, 30);
    scene.hud_picture = patterned(344, 100, 90);
    View view;
    for (std::size_t i = 0; i < view.palette.size(); ++i) {
        view.palette.at(i) = {.r = static_cast<std::uint8_t>(i),
                              .g = static_cast<std::uint8_t>(255 - i),
                              .b = static_cast<std::uint8_t>(i * 7)};
    }
    Screen classic;
    draw(classic, view, scene, shared);

    // Nothing kept: black.
    const auto nothing = gpu.shown(Screen{});
    REQUIRE(nothing);
    CHECK(nothing->at(0) == 0);
    CHECK(nothing->at(3) == 255);

    // Two pictures kept, the first shown after the second was drawn: each is its own.
    Screen first;
    Screen second;
    REQUIRE(gpu.keep(first, view, scene, shared));
    view.camera_y = 9;
    REQUIRE(gpu.keep(second, view, scene, shared));
    CHECK(first.pixels.empty());
    CHECK(first.kept == 0);
    CHECK(second.kept == 1);
    CHECK(first.palette == classic.palette);
    const auto shown = gpu.shown(first);
    REQUIRE(shown);
    REQUIRE(shown->size() == classic.pixels.size() * 4);
    std::size_t wrong = 0;
    for (std::size_t i = 0; i < classic.pixels.size(); ++i) {
        const assets::Rgb& colour = classic.palette.at(classic.pixels[i]);
        wrong += (shown->at(i * 4) != colour.r || shown->at((i * 4) + 1) != colour.g ||
                  shown->at((i * 4) + 2) != colour.b || shown->at((i * 4) + 3) != 255)
                     ? 1U
                     : 0U;
    }
    CHECK(wrong == 0);
    CHECK_FALSE(gpu.show(first));  // there is no window

    // The minimap: a square of it (64 cells a side: 256 of its pixels) in the bottom left corner, 64 pixels a
    // side, with a frame; the HUD moved 33 pixels right to make room, and the playfield as it was.
    const auto pixel = [](std::size_t x, std::size_t y) { return ((y * kScreenWidth) + x) * 4; };
    const auto colour = [&pixel](const std::vector<std::uint8_t>& from, std::size_t x, std::size_t y) {
        return std::array<int, 3>{from.at(pixel(x, y)), from.at(pixel(x, y) + 1), from.at(pixel(x, y) + 2)};
    };
    // (The minimap lets a little of what is under it through: a colour is near what it was given.)
    const auto near = [](const std::array<int, 3>& is, const std::array<int, 3>& wanted) {
        return std::abs(is.at(0) - wanted.at(0)) <= 20 && std::abs(is.at(1) - wanted.at(1)) <= 20 &&
               std::abs(is.at(2) - wanted.at(2)) <= 20;
    };
    constexpr std::array<int, 3> kGreen{0, 200, 0};
    constexpr std::array<int, 3> kRed{200, 0, 0};
    constexpr std::array<int, 3> kDark{6, 6, 12};
    // A map of 100 x 100 cells: red, with its left half (as far as pixel 200) green; all of it seen but the
    // rows from 300 on.
    Minimap minimap{.width = 400, .height = 400, .pixels = std::vector<std::uint8_t>(400 * 400 * 4, 0)};
    for (std::size_t y = 0; y < 400; ++y) {
        for (std::size_t x = 0; x < 400; ++x) {
            minimap.pixels[(((y * 400) + x) * 4) + 0] = x < 200 ? 0 : 200;
            minimap.pixels[(((y * 400) + x) * 4) + 1] = x < 200 ? 200 : 0;
            minimap.pixels[(((y * 400) + x) * 4) + 3] = y < 300 ? 255 : 0;
        }
    }
    // The view's middle at (800, 800) of the level: (200, 200) of the minimap, where green ends. The square is
    // its pixels 72 to 328, four of them to one of the picture.
    first.level_x = 640;
    first.level_y = 712;
    const auto with = gpu.shown(first, 1, &minimap);
    REQUIRE(with);
    CHECK(near(colour(*with, 10, 200), kGreen));
    CHECK(near(colour(*with, 54, 200), kRed));
    CHECK(colour(*with, 0, 200) == std::array<int, 3>{150, 153, 171});  // the frame
    CHECK(colour(*with, 30, 239) == colour(*with, 0, 200));
    CHECK(colour(*with, 63, 190) == colour(*with, 0, 200));
    CHECK(colour(*with, 40, 176) == colour(*with, 0, 200));
    CHECK(colour(*with, 61, 176) == colour(*with, 0, 200));  // its corners: two pixels cut off, then a step in
    CHECK(colour(*with, 62, 177) == colour(*with, 0, 200));
    CHECK(colour(*with, 63, 176) == colour(*shown, 30, 176));
    CHECK(colour(*with, 62, 176) == colour(*shown, 29, 176));
    CHECK(colour(*with, 63, 177) == colour(*shown, 30, 177));
    CHECK(colour(*with, 64 + 30, 200) == colour(*shown, 31 + 30, 200));  // the HUD, 33 to the right
    CHECK(colour(*with, 319, 200) == colour(*shown, 286, 200));
    CHECK(colour(*with, 100, 100) == colour(*shown, 100, 100));
    // What wasn't seen is dark (rows 300 on: from 233 down here), except where the view shows it now: the view is
    // 80 x 44 of the minimap's pixels around the middle; a view six times as high reaches down there.
    CHECK(near(colour(*with, 10, 236), kDark));
    first.part.height = 6 * 176;
    first.level_y = 800 - (3 * 176);
    const auto shows = gpu.shown(first, 1, &minimap);
    REQUIRE(shows);
    CHECK(near(colour(*shows, 30, 236), kGreen));
    CHECK(near(colour(*shows, 10, 236), kDark));  // left of the view
    first.part.height = 176;
    first.level_y = 712;
    // The Vikings, seen at three times the size (three pixels of the picture to four of the minimap's): Erik
    // where he is, the one being played with white around him; Baleog without; Olaf isn't there.
    const auto wide = [](const std::vector<std::uint8_t>& from, std::size_t x, std::size_t y) {
        const std::size_t at = ((y * kScreenWidth * 3) + x) * 4;
        return std::array<int, 3>{from.at(at), from.at(at + 1), from.at(at + 2)};
    };
    const auto still = gpu.shown(first, 3, &minimap);
    first.vikings.at(0) = {.x = 720, .y = 760, .there = true, .played = true};   // (180, 190) of the minimap
    first.vikings.at(1) = {.x = 880, .y = 760, .there = true, .played = false};  // (220, 190)
    first.vikings.at(2) = {.x = 800, .y = 700, .there = false, .played = false};
    const auto dots = gpu.shown(first, 3, &minimap);
    REQUIRE(still);
    REQUIRE(dots);
    CHECK(near(wide(*dots, 81, 616), {240, 84, 60}));
    CHECK(wide(*dots, 81, 613).at(2) > 150);    // white just above him: blue in it, which he and the map lack
    CHECK(near(wide(*dots, 81, 611), kGreen));  // and no further: the rim is thin
    CHECK(near(wide(*dots, 111, 616), {96, 220, 96}));
    CHECK(near(wide(*dots, 111, 612), kRed));
    CHECK(wide(*dots, 96, 572) == wide(*still, 96, 572));  // where Olaf would be
    first.vikings = {};
    // It moves smoothly: the view a pixel of the level further is another picture.
    first.level_x = 641;
    const auto moved = gpu.shown(first, 3, &minimap);
    first.level_x = 640;
    REQUIRE(moved);
    CHECK(*moved != *still);
    // The view at the level's corner: the square stays inside the map. A map smaller than the square is in its
    // middle, with what wasn't seen's colour around it.
    first.level_x = first.level_y = 0;
    const auto corner = gpu.shown(first, 1, &minimap);
    REQUIRE(corner);
    CHECK(near(colour(*corner, 30, 200), kGreen));
    minimap = Minimap{.width = 96, .height = 96, .pixels = std::vector<std::uint8_t>(96 * 96 * 4, 255)};
    const auto small = gpu.shown(first, 1, &minimap);
    REQUIRE(small);
    CHECK(near(colour(*small, 32, 208), {255, 255, 255}));
    CHECK(near(colour(*small, 8, 208), kDark));
    // The mark of time running backwards: two light triangles pointing left in the top right corner, with dark
    // behind them; nothing of it otherwise.
    first.rewinding = true;
    const auto marked = gpu.shown(first, 1);
    first.rewinding = false;
    REQUIRE(marked);
    CHECK(near(colour(*marked, 320 - 8 - 12, 8 + 5), {255, 255, 255}));                  // in the left triangle
    CHECK(near(colour(*marked, 320 - 8 - 3, 8 + 5), {255, 255, 255}));                   // in the right one
    CHECK(colour(*marked, 320 - 8 - 17, 8 + 1) == colour(*shown, 320 - 8 - 17, 8 + 1));  // above its point
    CHECK(colour(*marked, 100, 100) == colour(*shown, 100, 100));
    // One that isn't whole is left out.
    minimap.pixels.pop_back();
    const auto without = gpu.shown(first, 1, &minimap);
    REQUIRE(without);
    CHECK(*without == *shown);

    // Remaster rules' rewind bar along the minimap's bottom (3 to 60, rows 230 to 236), in the original's pixels
    // and a little see-through: half of it left, the left half of its inside light grey (28 of 56 columns), the
    // rest dark, in a white frame with two pixels cut off each corner; in its middle the rewind's mark (29 to 34,
    // rows 231 to 235), dark on the light grey and light grey on the dark.
    constexpr std::array<int, 3> kWhite{251, 251, 251};
    constexpr std::array<int, 3> kGrey{170, 170, 170};
    minimap = Minimap{.width = 96, .height = 96, .pixels = std::vector<std::uint8_t>(96 * 96 * 4, 255)};
    const auto plain = gpu.shown(first, 1, &minimap);
    REQUIRE(plain);
    // Whether a pixel is a colour of the bar over the picture under it.
    const auto over = [&](const std::vector<std::uint8_t>& from, std::size_t x, std::size_t y,
                          const std::array<int, 3>& bar) {
        const std::array<int, 3> is = colour(from, x, y);
        const std::array<int, 3> under = colour(*plain, x, y);
        bool close = true;
        for (std::size_t i = 0; i < 3; ++i) {
            const double wanted = (0.85 * bar.at(i)) + (0.15 * under.at(i));
            close = close && std::abs(is.at(i) - wanted) <= 2;
        }
        return close;
    };
    first.rewind_left = 0.5F;
    const auto bar = gpu.shown(first, 1, &minimap);
    REQUIRE(bar);
    CHECK(over(*bar, 4, 233, kGrey));
    CHECK(over(*bar, 28, 233, kGrey));
    CHECK(over(*bar, 35, 233, kDark));
    CHECK(over(*bar, 59, 233, kDark));
    CHECK(over(*bar, 29, 233, kDark));  // the mark's points, half way down: on the light grey
    CHECK(over(*bar, 32, 233, kGrey));  // and on the dark
    CHECK(over(*bar, 31, 231, kDark));  // the first triangle's back, at the top
    CHECK(over(*bar, 30, 231, kGrey));  // and beside it
    CHECK(over(*bar, 34, 235, kGrey));
    CHECK(over(*bar, 20, 230, kWhite));  // the frame
    CHECK(over(*bar, 60, 233, kWhite));
    CHECK(over(*bar, 3, 233, kWhite));
    CHECK(over(*bar, 5, 230, kWhite));  // a corner: cut off, then a step in
    CHECK(over(*bar, 4, 231, kWhite));
    CHECK(colour(*bar, 3, 230) == colour(*plain, 3, 230));
    CHECK(colour(*bar, 4, 230) == colour(*plain, 4, 230));
    CHECK(colour(*bar, 3, 231) == colour(*plain, 3, 231));
    CHECK(colour(*bar, 2, 233) == colour(*plain, 2, 233));
    CHECK(colour(*bar, 20, 237) == colour(*plain, 20, 237));
    CHECK(colour(*bar, 150, 233) == colour(*plain, 150, 233));  // nothing over the HUD
    first.rewind_left = 1.0F / 199;                             // any of it: a column
    const auto least = gpu.shown(first, 1, &minimap);
    REQUIRE(least);
    CHECK(over(*least, 4, 233, kGrey));
    CHECK(over(*least, 5, 233, kDark));
    first.rewind_left = 0;
    const auto none = gpu.shown(first, 1, &minimap);
    REQUIRE(none);
    CHECK(over(*none, 4, 233, kDark));
    CHECK(over(*none, 29, 233, kGrey));  // the mark
    // It comes and goes with the minimap: without it, nothing.
    CHECK(colour(*gpu.shown(first, 1), 20, 233) == colour(*shown, 20, 233));
    first.rewind_left.reset();
    CHECK(colour(*gpu.shown(first, 1, &minimap), 20, 233) == colour(*plain, 20, 233));
}

TEST_CASE("gpu: a kept picture is drawn as it is shown: what moves between ticks, as large as it is shown") {
    auto opened = GpuRenderer::open_headless();
    if (!opened) {
        SKIP("no graphics device: " << opened.error());
    }
    GpuRenderer& gpu = **opened;
    const SharedGraphics shared = sample_shared();
    SceneGraphics scene = sample_scene();
    scene.id = next_graphics_id();
    Map map{.width = 30, .height = 20, .cells = std::vector<Cell>(600)};  // nothing in front of the sprites
    const assets::IndexedImage piece = assets::make_image(8, 8);
    assets::IndexedImage white = piece;
    std::ranges::fill(white.pixels, std::uint8_t{255});
    View view;
    view.map = &map;
    view.camera_x = 40;
    view.palette[255] = {.r = 255, .g = 255, .b = 255};
    view.sprites = {{.image = &white, .x = 100, .y = 60, .moves = {6, 0}}};
    Screen screen;
    REQUIRE(gpu.keep(screen, view, scene, shared));
    const auto pixel = [](std::size_t x, std::size_t y, int scale) {
        return ((y * kScreenWidth * static_cast<std::size_t>(scale)) + x) * 4;
    };
    // The sprite's left edge on row 62, `scale` pixels a pixel: the first white pixel from where it would be
    // 4 pixels further left.
    const auto left_edge = [&](const std::vector<std::uint8_t>& shown, int scale) {
        const auto row = static_cast<std::size_t>(62 * scale);
        for (auto x = static_cast<std::size_t>(56 * scale); x < static_cast<std::size_t>(72 * scale); ++x) {
            const std::size_t at = pixel(x, row, scale);
            if (shown[at] == 255 && shown[at + 1] == 255 && shown[at + 2] == 255) {
                return static_cast<int>(x);
            }
        }
        return -1;
    };
    // Not a tick's picture: as the picture has it, at any moment.
    const auto still = gpu.shown(screen, 3, nullptr, 0.5F);
    REQUIRE(still);
    CHECK(left_edge(*still, 3) == 60 * 3);
    CHECK(gpu.show_times().redrawn);
    // Shown again (the window refreshes faster than pictures come), and in other colours (a fade): not drawn anew.
    REQUIRE(gpu.shown(screen, 3, nullptr, 0.9F));
    CHECK_FALSE(gpu.show_times().redrawn);
    Screen faded = screen;
    faded.palette[255] = {.r = 255, .g = 0, .b = 0};
    const auto red = gpu.shown(faded, 3, nullptr, 0.0F);
    REQUIRE(red);
    CHECK_FALSE(gpu.show_times().redrawn);
    const std::size_t on_sprite = pixel(static_cast<std::size_t>(62 * 3), static_cast<std::size_t>(62 * 3), 3);
    CHECK((*red)[on_sprite] == 255);
    CHECK((*red)[on_sprite + 1] == 0);
    // At another size it is.
    REQUIRE(gpu.shown(screen, 2, nullptr, 0.0F));
    CHECK(gpu.show_times().redrawn);
    REQUIRE(gpu.shown(screen, 3, nullptr, 0.0F));
    CHECK(gpu.show_times().redrawn);
    // The tick's first picture, which has moved it its first share (2 of 6): shown a third of a tick behind, where
    // the tick before left it, and from there a pixel more every half refresh: in thirds of a pixel at 3 times.
    screen.motion.phase = 0;
    screen.motion.sprites = {{6, 0}};
    for (const auto& [between, at] :
         {std::pair{0.0F, 58 * 3}, std::pair{0.5F, (58 * 3) + 3}, std::pair{0.25F, (58 * 3) + 2}}) {
        INFO("between " << between);
        const auto moved = gpu.shown(screen, 3, nullptr, between);
        REQUIRE(moved);
        CHECK(left_edge(*moved, 3) == at);
        CHECK(gpu.show_times().redrawn);  // a tick's picture is drawn anew every time: it moves
    }
    // The camera as well, once it is known where the tick before left it: everything goes the other way.
    screen.motion.scroll = {40, 0};
    screen.motion.scroll_to = {46, 0};
    screen.motion.scroll_from = {{40, 0}};
    const auto both = gpu.shown(screen, 3, nullptr, 0.5F);
    REQUIRE(both);
    // The camera's share at the first picture was 2 of its 6 too (40 is where the step left it: pretend so);
    // half a refresh in it is 1 pixel further, the sprite 1 pixel further as well: no move on the screen.
    CHECK(left_edge(*both, 3) == (58 * 3) + 3 - 3);
}

TEST_CASE("gpu: pictures kept on pages: a scene's pictures stay for the pictures kept of it") {
    auto opened = GpuRenderer::open_headless();
    if (!opened) {
        SKIP("no graphics device: " << opened.error());
    }
    GpuRenderer& gpu = **opened;
    const SharedGraphics shared = sample_shared();
    SceneGraphics one;
    one.id = next_graphics_id();
    one.picture = patterned(320, 200, 30);
    SceneGraphics two;
    two.id = next_graphics_id();
    two.picture = patterned(320, 200, 140);
    View view;
    for (std::size_t i = 0; i < view.palette.size(); ++i) {
        view.palette.at(i) = {.r = static_cast<std::uint8_t>(i), .g = 0, .b = 0};
    }
    // Kept of the one scene, then the same picture again (a fade: kept once), then of the other scene: shown
    // after all were kept, each is its own scene's.
    Screen first;
    Screen again;
    Screen second;
    REQUIRE(gpu.keep(first, view, one, shared));
    REQUIRE(gpu.keep(again, view, one, shared));
    REQUIRE(gpu.keep(second, view, two, shared));
    const auto shows = [&gpu](const Screen& screen, std::uint8_t colour) {
        const auto shown = gpu.shown(screen);
        REQUIRE(shown);
        CHECK(shown->at(std::size_t{kPictureTop} * kScreenWidth * 4) == colour);  // the picture's first pixel
    };
    shows(first, 30);
    shows(again, 30);
    shows(second, 140);
    // Many scenes, each kept once and shown at once: the pages are used again.
    for (int i = 0; i < 12; ++i) {
        SceneGraphics each;
        each.id = next_graphics_id();
        each.picture = patterned(320, 200, static_cast<std::uint8_t>(10 * i));
        Screen screen;
        REQUIRE(gpu.keep(screen, view, each, shared));
        shows(screen, static_cast<std::uint8_t>(10 * i));
    }
}

TEST_CASE("gpu: a kept picture is shown with the scene's backdrop where the map leaves a palette row's colour 0") {
    auto opened = GpuRenderer::open_headless();
    if (!opened) {
        SKIP("no graphics device: " << opened.error());
    }
    GpuRenderer& gpu = **opened;
    const SharedGraphics shared = sample_shared();
    // A map of 30 x 20 cells drawn all in colour 0x20 (row 2's colour 0) but one cell, and a backdrop of 4 x 2
    // pixels: 0x31-0x34 over 0x35-0x38.
    SceneGraphics scene;
    scene.id = next_graphics_id();
    assets::IndexedImage open = assets::make_image(8, 8);
    std::ranges::fill(open.pixels, std::uint8_t{0x20});
    scene.tileset = {open, patterned(8, 8, 0x41)};
    scene.tile_masks.assign(16, 0);
    const auto all = [](std::uint16_t tile) {
        const assets::TileRef ref{.tile = tile, .flags = 0};
        return assets::Metatile{.parts = {ref, ref, ref, ref}};
    };
    scene.metatiles = {all(0), all(1)};
    Map map{.width = 30, .height = 20, .cells = std::vector<Cell>(600)};
    map.cells[3].metatile = 1;  // pixels 48-63 across, 0-15 down
    assets::IndexedImage backdrop = assets::make_image(4, 2);
    for (std::size_t i = 0; i < backdrop.pixels.size(); ++i) {
        backdrop.pixels[i] = static_cast<std::uint8_t>(0x31 + i);
    }
    scene.backdrop = Backdrop{.image = backdrop, .scroll_x = 0x40, .scroll_y = 0x100};
    View view;
    view.map = &map;
    view.hud = HudView{};
    for (std::size_t i = 0; i < view.palette.size(); ++i) {
        view.palette.at(i) = {.r = static_cast<std::uint8_t>(i),
                              .g = static_cast<std::uint8_t>(255 - i),
                              .b = static_cast<std::uint8_t>(i * 7)};
    }
    const auto index_at = [](const std::vector<std::uint8_t>& shown, std::size_t x, std::size_t y, int scale = 1) {
        return shown.at(((y * kScreenWidth * static_cast<std::size_t>(scale)) + x) * 4);  // red is the index
    };
    // The camera at 40 across: a quarter of that is 10, 2 into the backdrop; down it is with the camera, at 0.
    view.camera_x = 40;
    Screen screen;
    REQUIRE(gpu.keep(screen, view, scene, shared));
    screen.motion.scroll = {40, 0};
    const auto shown = gpu.shown(screen);
    REQUIRE(shown);
    CHECK(index_at(*shown, 0, 0) == 0x33);
    CHECK(index_at(*shown, 1, 0) == 0x34);
    CHECK(index_at(*shown, 2, 0) == 0x31);  // repeated across ...
    CHECK(index_at(*shown, 0, 1) == 0x37);
    CHECK(index_at(*shown, 0, 2) == 0x33);            // ... and down
    CHECK(index_at(*shown, 10, 5) == 0x41 + 2 + 15);  // the map's own pixels stay (its cell at 48-63 is at 8-23)
    // Not in the HUD's place, where the panel has colour 0x10 (at its 11th pixel of its first row).
    CHECK(index_at(*shown, 11, 176) == 0x10);

    // The screen shake moves the map, not the backdrop: it goes by the camera without it.
    Screen shaken;
    view.camera_x = 42;
    REQUIRE(gpu.keep(shaken, view, scene, shared));
    shaken.motion.scroll = {40, 0};
    const auto shook = gpu.shown(shaken);
    REQUIRE(shook);
    CHECK(index_at(*shook, 0, 0) == 0x33);
    view.camera_x = 40;

    // Past the map's edge there is nothing, not the backdrop: the camera at 400 shows the map up to 79.
    Screen past;
    view.camera_x = 400;
    REQUIRE(gpu.keep(past, view, scene, shared));
    past.motion.scroll = {400, 0};
    const auto beyond = gpu.shown(past);
    REQUIRE(beyond);
    CHECK(index_at(*beyond, 79, 10) == 0x31 + ((100 + 79) % 4));
    CHECK(index_at(*beyond, 80, 10) == 0);
    view.camera_x = 40;

    // Between ticks the camera moves on smoothly, and the backdrop at its speed: shown at 4 times, the camera half
    // a refresh into the tick's first picture is a pixel further (a sixth of 6), the backdrop a quarter pixel.
    screen.motion.phase = 0;
    screen.motion.scroll_from = {{40, 0}};
    screen.motion.scroll_to = {46, 0};
    const auto still = gpu.shown(screen, 4, nullptr, 0.0F);
    const auto moving = gpu.shown(screen, 4, nullptr, 0.5F);
    REQUIRE(still);
    REQUIRE(moving);
    CHECK(index_at(*still, 3, 0, 4) == 0x33);   // 10 + 3/4 of a pixel: still the backdrop's pixel 2
    CHECK(index_at(*moving, 3, 0, 4) == 0x34);  // 10.25 + 3/4: its pixel 3

    // One that moves by itself (the spaceship's stars): 5 pixels a refresh across, whatever the camera does.
    scene.backdrop->scroll_x = 0x8500;
    scene.id = next_graphics_id();
    Screen stars;
    REQUIRE(gpu.keep(stars, view, scene, shared));
    stars.motion.scroll = {40, 0};
    stars.refresh = 3;  // 15 pixels: 3 into the backdrop
    const auto flown = gpu.shown(stars);
    REQUIRE(flown);
    CHECK(index_at(*flown, 0, 0) == 0x34);
    const auto on = gpu.shown(stars, 1, nullptr, 0.5F);  // half a refresh on: 2.5 pixels more, 17.5 (+ half a pixel)
    REQUIRE(on);
    CHECK(index_at(*on, 0, 0) == 0x33);

    // Without a backdrop: the map's colour.
    scene.backdrop.reset();
    scene.id = next_graphics_id();
    Screen plain;
    REQUIRE(gpu.keep(plain, view, scene, shared));
    const auto none = gpu.shown(plain);
    REQUIRE(none);
    CHECK(index_at(*none, 0, 0) == 0x20);
}

TEST_CASE("gpu: the screen's look shows the picture as Hyllian's CRT shader would; without one it is as it was") {
    auto opened = GpuRenderer::open_headless();
    if (!opened) {
        SKIP("no graphics device: " << opened.error());
    }
    GpuRenderer& gpu = **opened;
    using Look = GpuRenderer::Look;
    // Their names, each once.
    for (const Look look : GpuRenderer::kLooks) {
        CHECK(GpuRenderer::look_named(GpuRenderer::name_of(look)) == look);
    }
    CHECK_FALSE(GpuRenderer::look_named("vga"));          // tried and dropped
    CHECK_FALSE(GpuRenderer::look_named("tv"));           // so was our own television
    CHECK_FALSE(GpuRenderer::look_named("xbr-noblend"));  // tried and dropped
    // A screen all of one grey.
    const SharedGraphics shared = sample_shared();
    SceneGraphics scene = sample_scene();
    assets::IndexedImage grey = assets::make_image(kScreenWidth, kScreenHeight);
    std::ranges::fill(grey.pixels, std::uint8_t{1});
    scene.picture = grey;
    scene.hud_picture = grey;
    View view;
    view.camera_y = kPictureTop;
    view.palette[1] = {.r = 128, .g = 128, .b = 128};
    Screen screen;
    REQUIRE(gpu.keep(screen, view, scene, shared));
    constexpr int kScale = 2;
    constexpr std::size_t kWide = std::size_t{kScreenWidth} * kScale;
    constexpr std::size_t kHigh = std::size_t{kScreenHeight} * kScale;
    CHECK(gpu.look() == Look::kNone);
    const auto plain = gpu.shown(screen, kScale);
    gpu.set_look(Look::kCrt);
    const auto crt = gpu.shown(screen, kScale);
    CHECK(gpu.look() == Look::kCrt);
    REQUIRE(plain);
    REQUIRE(crt);
    const auto red = [](const std::vector<std::uint8_t>& pixels, std::size_t x, std::size_t y) {
        return int{pixels.at(((y * kWide) + x) * 4)};
    };
    // The mean of an 8 x 8 block from (x, y); the lowest and highest in it.
    const auto block = [&](const std::vector<std::uint8_t>& pixels, std::size_t x, std::size_t y) {
        int sum = 0;
        int lowest = 255;
        int highest = 0;
        for (std::size_t down = y; down < y + 8; ++down) {
            for (std::size_t across = x; across < x + 8; ++across) {
                const int value = red(pixels, across, down);
                sum += value;
                lowest = std::min(lowest, value);
                highest = std::max(highest, value);
            }
        }
        return std::array<int, 3>{sum / 64, lowest, highest};
    };
    // Without a look: the grey everywhere.
    std::size_t not_grey = 0;
    for (std::size_t at = 0; at < plain->size(); at += 4) {
        not_grey += plain->at(at) != 128 || plain->at(at + 1) != 128 || plain->at(at + 2) != 128 ? 1 : 0;
    }
    CHECK(not_grey == 0);
    // In scanlines, a row of the picture each, and the grille's columns, but the grey's own colour when averaged
    // (no tint), a little darker.
    const auto [crt_middle, crt_lowest, crt_highest] = block(*crt, kWide / 2, kHigh / 2);
    CHECK(crt_middle > 0.6 * 128);
    CHECK(crt_middle < 1.1 * 128);
    CHECK(crt_highest - crt_lowest > 10);
    std::array<int, 3> sums{};
    for (std::size_t y = kHigh / 2; y < (kHigh / 2) + 8; ++y) {
        for (std::size_t x = kWide / 2; x < (kWide / 2) + 8; ++x) {
            for (std::size_t c = 0; c < 3; ++c) {
                sums.at(c) += crt->at((((y * kWide) + x) * 4) + c);
            }
        }
    }
    CHECK(std::abs(sums[0] - sums[1]) < 64 * 4);  // within a few steps a pixel
    CHECK(std::abs(sums[0] - sums[2]) < 64 * 4);
    // Three of the window's rows a row of the picture: the beam's middle brighter than its edges, which are alike
    // (at two they are both as far from the middle, and alike too: no lines to see).
    constexpr int kThree = 3;
    constexpr std::size_t kWide3 = std::size_t{kScreenWidth} * kThree;
    const auto crt3 = gpu.shown(screen, kThree);
    REQUIRE(crt3);
    const auto row_sum = [&](std::size_t y) {  // its red across 8 pixels in the middle
        int sum = 0;
        for (std::size_t x = kWide3 / 2; x < (kWide3 / 2) + 8; ++x) {
            sum += int{crt3->at(((y * kWide3) + x) * 4)};
        }
        return sum;
    };
    const std::size_t row = std::size_t{kScreenHeight} / 2 * kThree;  // a row's first: its middle is the next
    CHECK(row_sum(row + 1) > row_sum(row));
    CHECK(row_sum(row) == row_sum(row + 2));
    // At two the beam is taken over each of the window's rows whole: as bright on the whole as at three (zoomed
    // out, as at the original's view), and the same row after row.
    const auto mean = [](const std::vector<std::uint8_t>& pixels, std::size_t wide, std::size_t y, std::size_t rows) {
        int sum = 0;
        for (std::size_t down = y; down < y + rows; ++down) {
            for (std::size_t x = wide / 2; x < (wide / 2) + 8; ++x) {
                sum += int{pixels.at(((down * wide) + x) * 4)};
            }
        }
        return static_cast<double>(sum) / static_cast<double>(rows * 8);
    };
    const double at_two = mean(*crt, kWide, kHigh / 2, 12);
    const double at_three = mean(*crt3, kWide3, row, 12);
    CHECK(at_two > 0.9 * at_three);
    CHECK(at_two < 1.1 * at_three);
    CHECK(row_sum(row + 1) == row_sum(row + 1 + kThree));
    // A picture of the whole view (a development tool's) never has it.
    const auto whole = gpu.shown_whole(screen, 1);
    gpu.set_look(Look::kNone);
    const auto whole_plain = gpu.shown_whole(screen, 1);
    REQUIRE(whole);
    REQUIRE(whole_plain);
    CHECK(whole->pixels == whole_plain->pixels);
}

TEST_CASE("gpu: the xbr look rounds off stair-steps, blending their edges; flat stays flat") {
    auto opened = GpuRenderer::open_headless();
    if (!opened) {
        SKIP("no graphics device: " << opened.error());
    }
    GpuRenderer& gpu = **opened;
    using Look = GpuRenderer::Look;
    const SharedGraphics shared = sample_shared();
    SceneGraphics scene = sample_scene();
    // Grey above a line going down a pixel to the right a pixel, white below; the HUD's place grey.
    assets::IndexedImage split = assets::make_image(kScreenWidth, kScreenHeight);
    for (std::size_t y = 0; y < kScreenHeight; ++y) {
        for (std::size_t x = 0; x < kScreenWidth; ++x) {
            split.set(x, y, x < y && y < kPlayfieldHeight ? std::uint8_t{2} : std::uint8_t{1});
        }
    }
    scene.picture = split;
    assets::IndexedImage grey = assets::make_image(kScreenWidth, kScreenHeight);
    std::ranges::fill(grey.pixels, std::uint8_t{1});
    scene.hud_picture = grey;
    View view;
    view.camera_y = kPictureTop;
    view.palette[1] = {.r = 128, .g = 128, .b = 128};
    view.palette[2] = {.r = 255, .g = 255, .b = 255};
    Screen screen;
    REQUIRE(gpu.keep(screen, view, scene, shared));
    constexpr int kScale = 4;
    constexpr std::size_t kWide = std::size_t{kScreenWidth} * kScale;
    gpu.set_look(Look::kNone);
    const auto plain = gpu.shown(screen, kScale);
    gpu.set_look(Look::kXbr);
    const auto rounded = gpu.shown(screen, kScale);
    REQUIRE(plain);
    REQUIRE(rounded);
    // Along the line (pixels 40 to 80 of the picture, all of each's 4 x 4): not the steps, and colours between the
    // two where it blends.
    std::size_t differ = 0;
    std::size_t between_colours = 0;
    for (std::size_t y = 40 * kScale; y < 80 * kScale; ++y) {
        for (std::size_t x = y - (2 * kScale); x < y + (2 * kScale); ++x) {
            const std::size_t at = ((y * kWide) + x) * 4;
            differ += rounded->at(at) != plain->at(at) ? 1 : 0;
            between_colours += rounded->at(at) != 128 && rounded->at(at) != 255 ? 1 : 0;
        }
    }
    CHECK(differ > 0);
    CHECK(between_colours > 0);
    // The HUD's place, all one grey, as it was.
    std::size_t not_grey = 0;
    for (std::size_t y = 200 * kScale; y < 230 * kScale; ++y) {
        for (std::size_t x = 20 * kScale; x < 300 * kScale; ++x) {
            not_grey += rounded->at(((y * kWide) + x) * 4) != 128 ? 1 : 0;
        }
    }
    CHECK(not_grey == 0);
}
