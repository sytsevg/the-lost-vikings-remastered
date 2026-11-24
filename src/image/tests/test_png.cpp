// Tests for src/image/png.*: indexed PNGs must round-trip exactly (pixel indices and palette).
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "assets/assets.hpp"
#include "assets/catalog.hpp"
#include "assets/data_dat.hpp"
#include "assets/graphics.hpp"
#include "assets/level.hpp"
#include "image/png.hpp"

using namespace lvr::assets;
using lvr::image::load_indexed_png;
using lvr::image::save_indexed_png;

namespace {

// The user's own game files in gamedata/original/, opened the way the game opens them. [real-data] tests skip when
// the folder is missing.
const lvr::assets::GameFiles& game_or_skip() {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    static const auto files = lvr::assets::GameFiles::open(LVR_ORIGINAL_DIR);
    if (!files) {
        FAIL(files.error());
    }
    return *files;
}

}  // namespace

namespace {

std::filesystem::path temp_png(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("lvr_test_" + name + ".png");
}

void check_round_trip(const IndexedImage& image, const Palette& palette, const std::string& name,
                      std::optional<std::uint8_t> transparent = {}) {
    const auto path = temp_png(name);
    REQUIRE(save_indexed_png(path, image, palette, transparent).has_value());
    const auto loaded = load_indexed_png(path);
    std::filesystem::remove(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->image.width == image.width);
    CHECK(loaded->image.height == image.height);
    CHECK(loaded->image.pixels == image.pixels);
    CHECK(loaded->palette == palette);
    CHECK(loaded->transparent == transparent);
}

}  // namespace

TEST_CASE("png: every palette index and colour survives a save/load round trip") {
    // 256 x 3 image using every index, with an odd width so rows can't be accidentally aligned
    IndexedImage image = make_image(257, 3);
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        image.pixels[i] = static_cast<std::uint8_t>((i * 7) % 256);
    }
    Palette palette{};
    for (std::size_t i = 0; i < palette.size(); ++i) {
        palette[i] = Rgb{.r = static_cast<std::uint8_t>(i),
                         .g = static_cast<std::uint8_t>(255 - i),
                         .b = static_cast<std::uint8_t>(i * 3)};
    }
    check_round_trip(image, palette, "synthetic");
    check_round_trip(image, palette, "synthetic_transparent", 0);
    check_round_trip(image, palette, "synthetic_transparent_200", 200);
}

TEST_CASE("png: a non-indexed PNG is rejected") {
    CHECK_FALSE(load_indexed_png(temp_png("does_not_exist")).has_value());
}

TEST_CASE("real data: level 1 round-trips through PNG unchanged", "[real-data]") {
    const Archive* const archive = &game_or_skip().archive();
    const auto header = parse_level_header(*decode_entry(*archive, kLevels[0].header_entry));
    REQUIRE(header.has_value());
    Palette palette{};
    for (const PaletteRef& ref : header->palettes) {
        REQUIRE(load_palette(palette, *decode_entry(*archive, ref.entry), ref.first_color).has_value());
    }
    const auto map = decode_tilemap(*decode_entry(*archive, header->map_entry), header->width, header->height);
    const auto tileset = decode_tileset(*decode_entry(*archive, header->tileset_entry));
    const auto metatiles = decode_metatiles(*decode_entry(*archive, header->metatiles_entry));
    const auto image = render_tilemap(*map, *metatiles, *tileset);
    REQUIRE(image.has_value());
    check_round_trip(*image, palette, "level01");
}
