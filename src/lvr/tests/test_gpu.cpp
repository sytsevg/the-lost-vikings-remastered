// Tests for src/lvr/gpu.*: the GPU renderer makes the picture the Classic one does (render.*), on hand-made views
// with everything a picture can show. That it does so on the replays: lv-replay --gpu.
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
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
}
