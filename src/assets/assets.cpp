#include "assets/assets.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <fstream>
#include <ios>
#include <system_error>
#include <utility>

#include "assets/hash.hpp"

namespace lvr::assets {

namespace fs = std::filesystem;

namespace {

bool iequals(std::string_view a, std::string_view b) {
    const auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
    return a.size() == b.size() && std::ranges::equal(a, b, {}, lower, lower);
}

// DOS file names may come in any case depending on how the game was copied.
std::optional<fs::path> find_file(const fs::path& dir, std::string_view name) {
    std::error_code ec;
    for (const auto& item : fs::directory_iterator{dir, ec}) {
        if (item.is_regular_file(ec) && iequals(item.path().filename().string(), name)) {
            return item.path();
        }
    }
    return std::nullopt;
}

std::expected<std::vector<std::byte>, std::string> read_file(const fs::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return std::unexpected{std::format("cannot open {}", path.string())};
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), size)) {
        return std::unexpected{std::format("cannot read {}", path.string())};
    }
    return bytes;
}

template <typename T>
std::expected<T, std::string> check(std::expected<T, Error> value, std::string_view what) {
    return std::move(value).transform_error(
        [what](Error error) { return std::format("{}: {}", what, describe(error)); });
}

// `bottom` below `top`; both the same width.
IndexedImage stack(const IndexedImage& top, const IndexedImage& bottom) {
    IndexedImage out = make_image(top.width, top.height + bottom.height);
    std::ranges::copy(top.pixels, out.pixels.begin());
    std::ranges::copy(bottom.pixels, out.pixels.begin() + static_cast<std::ptrdiff_t>(top.pixels.size()));
    return out;
}

// Both sprite lists of a level or scene header.
struct HeaderSprites {
    std::vector<SpriteSet> sprite_sets;
    std::vector<SpriteSet> packed_sets;
};

std::expected<HeaderSprites, std::string> load_header_sprites(const Archive& archive, const LevelHeader& header) {
    HeaderSprites out;
    for (const SpriteSetRef& ref : header.sprite_sets) {
        auto set = load_sprites(archive, ref.entry);
        if (!set) {
            return std::unexpected{set.error()};
        }
        out.sprite_sets.push_back(std::move(*set));
    }
    for (const PackedSetRef& ref : header.packed_sets) {
        auto set = load_sprites(archive, ref.entry);
        if (!set) {
            return std::unexpected{set.error()};
        }
        out.packed_sets.push_back(std::move(*set));
    }
    return out;
}

}  // namespace

// --- The original files ---

std::expected<GameFiles, std::string> GameFiles::open(const fs::path& dir) {
    const auto exe_path = find_file(dir, kExeName);
    const auto data_path = find_file(dir, kDataName);
    if (!exe_path || !data_path) {
        return std::unexpected{std::format("{} and {} must both be in {}", kExeName, kDataName, dir.string())};
    }
    const auto exe = read_file(*exe_path);
    if (!exe) {
        return std::unexpected{exe.error()};
    }
    auto data = read_file(*data_path);
    if (!data) {
        return std::unexpected{data.error()};
    }
    return from_bytes(*exe, std::move(*data));
}

std::expected<GameFiles, std::string> GameFiles::from_bytes(std::span<const std::byte> exe,
                                                            std::vector<std::byte> data) {
    const auto release = identify_release(exe, data);
    if (!release) {
        const auto content = data_content_hash(data);
        return std::unexpected{
            std::format("unknown release: {} fnv1a64={:016x}, {} content fnv1a64={}\n"
                        "Only known releases are supported (docs/formats/releases.md).",
                        kExeName, fnv1a64(exe), kDataName,
                        content ? std::format("{:016x}", *content) : std::string{describe(content.error())})};
    }
    // A known release always parses; checked anyway rather than assumed.
    auto archive = Archive::parse(data);
    if (!archive) {
        return std::unexpected{std::format("{}: {}", kDataName, describe(archive.error()))};
    }
    return GameFiles{std::vector<std::byte>{exe.begin(), exe.end()}, std::move(data), std::move(*archive), *release};
}

// --- Building blocks ---

std::expected<std::vector<std::byte>, std::string> load_entry(const Archive& archive, std::size_t index) {
    return decode_entry(archive, index).transform_error([index](Error error) {
        return std::format("entry {}: {}", index, describe(error));
    });
}

std::expected<LevelHeader, std::string> load_header(const Archive& archive, std::uint16_t header_entry) {
    const auto data = load_entry(archive, header_entry);
    if (!data) {
        return std::unexpected{data.error()};
    }
    return check(parse_level_header(*data), std::format("header {}", header_entry));
}

std::expected<Palette, std::string> load_palette(const Archive& archive, std::span<const PaletteRef> palettes) {
    Palette palette{};
    for (const PaletteRef& ref : palettes) {
        const auto data = load_entry(archive, ref.entry);
        if (!data) {
            return std::unexpected{data.error()};
        }
        if (const auto loaded =
                check(load_palette(palette, *data, ref.first_color), std::format("palette {}", ref.entry));
            !loaded) {
            return std::unexpected{loaded.error()};
        }
    }
    return palette;
}

std::expected<IndexedImage, std::string> load_screen_image(const Archive& archive, std::uint16_t image_entry) {
    const auto data = load_entry(archive, image_entry);
    if (!data) {
        return std::unexpected{data.error()};
    }
    return check(decode_screen_image(*data), std::format("image {}", image_entry));
}

std::expected<TiledMap, std::string> load_tiled_map(const Archive& archive, const LevelHeader& header) {
    const auto map_data = load_entry(archive, header.map_entry);
    const auto tileset_data = load_entry(archive, header.tileset_entry);
    const auto metatile_data = load_entry(archive, header.metatiles_entry);
    if (!map_data || !tileset_data || !metatile_data) {
        return std::unexpected{!map_data ? map_data.error()
                                         : (!tileset_data ? tileset_data.error() : metatile_data.error())};
    }
    auto map =
        check(decode_tilemap(*map_data, header.width, header.height), std::format("tilemap {}", header.map_entry));
    auto tileset = check(decode_tileset(*tileset_data), std::format("tileset {}", header.tileset_entry));
    auto metatiles = check(decode_metatiles(*metatile_data), std::format("metatiles {}", header.metatiles_entry));
    if (!map || !tileset || !metatiles) {
        return std::unexpected{!map ? map.error() : (!tileset ? tileset.error() : metatiles.error())};
    }
    auto image =
        check(render_tilemap(*map, *metatiles, *tileset), std::format("rendering tilemap {}", header.map_entry));
    if (!image) {
        return std::unexpected{image.error()};
    }
    return TiledMap{.map = std::move(*map),
                    .tileset = std::move(*tileset),
                    .metatiles = std::move(*metatiles),
                    .image = std::move(*image)};
}

std::expected<SpriteSet, std::string> load_sprites(const Archive& archive, const SpriteInfo& info) {
    const auto data = load_entry(archive, info.entry);
    if (!data) {
        return std::unexpected{data.error()};
    }
    auto frames = check(decode_sprite(*data, info), std::format("sprite entry {}", info.entry));
    if (!frames) {
        return std::unexpected{frames.error()};
    }
    return SpriteSet{.info = info, .frames = std::move(*frames)};
}

std::expected<SpriteSet, std::string> load_sprites(const Archive& archive, std::uint16_t entry) {
    const auto found = std::ranges::find(kSpriteCatalog, entry, &SpriteInfo::entry);
    if (found == kSpriteCatalog.end()) {
        return std::unexpected{std::format("sprite entry {} is not in the sprite catalogue", entry)};
    }
    return load_sprites(archive, *found);
}

std::expected<ObjectDatabase, std::string> load_object_database(const Archive& archive, std::uint16_t entry) {
    const auto data = load_entry(archive, entry);
    if (!data) {
        return std::unexpected{data.error()};
    }
    return check(parse_object_database(*data), std::format("object database {}", entry));
}

std::expected<std::vector<DialogueText>, std::string> load_dialogue_texts(std::span<const std::byte> exe,
                                                                          const Release& release) {
    const ExeRange range = release.dialogue_texts;
    if (range.offset > exe.size() || range.size > exe.size() - range.offset) {
        return std::unexpected{std::format("{}: the dialogue texts are past the end of the file", kExeName)};
    }
    return check(parse_dialogue_texts(exe.subspan(range.offset, range.size)), std::format("{} dialogue", kExeName));
}

std::expected<std::array<VikingStartTable, 3>, std::string> load_viking_starts(std::span<const std::byte> exe,
                                                                               const Release& release) {
    const ExeRange range = release.viking_starts;
    if (range.offset > exe.size() || range.size > exe.size() - range.offset) {
        return std::unexpected{std::format("{}: the Viking start tables are past the end of the file", kExeName)};
    }
    return check(parse_viking_starts(exe.subspan(range.offset, range.size)), std::format("{} Viking starts", kExeName));
}

// --- Levels, scenes and images ---

std::expected<LevelData, std::string> load_level(const Archive& archive, const LevelInfo& info) {
    auto header = load_header(archive, info.header_entry);
    if (!header) {
        return std::unexpected{std::format("level {}: {}", info.number, header.error())};
    }
    auto palette = load_palette(archive, header->palettes);
    auto map = load_tiled_map(archive, *header);
    auto sprites = load_header_sprites(archive, *header);
    if (!palette || !map || !sprites) {
        return std::unexpected{std::format("level {}: {}", info.number,
                                           !palette ? palette.error() : (!map ? map.error() : sprites.error()))};
    }
    return LevelData{.info = info,
                     .header = std::move(*header),
                     .palette = *palette,
                     .map = std::move(*map),
                     .sprite_sets = std::move(sprites->sprite_sets),
                     .packed_sets = std::move(sprites->packed_sets)};
}

std::expected<SceneData, std::string> load_scene(const Archive& archive, const SceneInfo& info) {
    const auto fail = [&info](const std::string& error) {
        return std::unexpected{std::format("scene {}: {}", info.name, error)};
    };
    auto header = load_header(archive, info.header_entry);
    if (!header) {
        return fail(header.error());
    }
    auto palette = load_palette(archive, header->palettes);
    auto sprites = load_header_sprites(archive, *header);
    if (!palette || !sprites) {
        return fail(!palette ? palette.error() : sprites.error());
    }
    SceneData scene{.info = info,
                    .header = std::move(*header),
                    .palette = *palette,
                    .map = std::nullopt,
                    .image = {},
                    .sprite_sets = std::move(sprites->sprite_sets),
                    .packed_sets = std::move(sprites->packed_sets)};
    if (scene.header.is_tiled()) {
        auto map = load_tiled_map(archive, scene.header);
        if (!map) {
            return fail(map.error());
        }
        scene.image = map->image;
        scene.map = std::move(*map);
        return scene;
    }
    // Image scene: the header's map entry is a full-screen image; the HUD-area part below it is our own pairing.
    auto top = load_screen_image(archive, scene.header.map_entry);
    if (!top) {
        return fail(top.error());
    }
    scene.image = std::move(*top);
    if (info.hud_entry != kNoEntry) {
        const auto bottom = load_screen_image(archive, info.hud_entry);
        if (!bottom) {
            return fail(bottom.error());
        }
        if (bottom->width != scene.image.width) {
            return fail(std::format("image {} is not as wide as image {}", info.hud_entry, scene.header.map_entry));
        }
        scene.image = stack(scene.image, *bottom);
    }
    return scene;
}

std::expected<Image, std::string> load_image(const Archive& archive, const ImageInfo& info) {
    auto image = load_screen_image(archive, info.image_entry);
    const std::array palettes{PaletteRef{.entry = info.palette_entry, .first_color = 0}};
    auto palette = load_palette(archive, palettes);
    if (!image || !palette) {
        return std::unexpected{std::format("image {}: {}", info.name, !image ? image.error() : palette.error())};
    }
    return Image{.info = info, .image = std::move(*image), .palette = *palette};
}

}  // namespace lvr::assets
