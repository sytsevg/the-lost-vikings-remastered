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
    CHECK(colour(*with, 63, 176) == colour(*with, 0, 200));
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
