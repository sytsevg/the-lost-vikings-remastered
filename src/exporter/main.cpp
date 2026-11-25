// lv-export: writes the game data out as files, for inspection and debugging. Not shipped: the game reads DATA.DAT
// directly through lvr::assets (ADR-0014); this tool only adds file formats and sheet layouts on top of it.
//
//   lv-export verify <original-dir>               identify the release, check that every entry decodes
//   lv-export assets <original-dir> <output-dir>  levels, scenes, images and sprites as PNG + JSON in flat folders:
//                                                 levels/NN-* (the level dump: NN-level.json, NN-world.png,
//                                                 NN-collision.png; the cutscenes' by name), scenes/<name>.*,
//                                                 images/<name>.*, sprites/NNN-<name>.*;
//                                                 the object scripts as text (scripts/NNN.txt, scripts/usage.txt) and
//                                                 the dialogue (dialogue/texts.txt)
//                                                 and, with the user's SNES ROM image (*.sfc) in the same
//                                                 folder, each level's SNES backdrop (snes/NN-backdrop.png) and the
//                                                 whole level with it behind the map (snes/NN-level.png); the
//                                                 same for the scenes that have one (snes/<name>-scene.png)
//   lv-export dump   <original-dir> <output-dir>  every decoded entry to <output-dir>/raw/, plus raw/index.json
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <glaze/glaze.hpp>
#include <initializer_list>
#include <map>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "assets/assets.hpp"
#include "assets/catalog.hpp"
#include "assets/data_dat.hpp"
#include "assets/graphics.hpp"
#include "assets/hash.hpp"
#include "assets/level.hpp"
#include "assets/script.hpp"
#include "assets/snes.hpp"
#include "assets/sprites.hpp"
#include "assets/text.hpp"
#include "exporter/level_dump.hpp"
#include "image/png.hpp"
#include "lvr/level.hpp"

namespace lvr::exporter {

namespace fs = std::filesystem;

// raw/index.json
struct DumpEntry {
    std::size_t index{};
    bool stored{};
    std::size_t packed_size{};
    std::size_t size{};
    std::string kind{};
    std::string fnv1a64{};
};

struct DumpIndex {
    std::string release{};
    std::vector<DumpEntry> entries{};
};

// levels/NN-level.json and levels/<cutscene>-level.json: the level as the engine has it (lvr::Level), with every
// placement. Every exported file records the DATA.DAT entries it came from
// (ADR-0014).
struct LevelSources {
    std::uint16_t header{};
    std::uint16_t map{};  // 65535 = none, here and below
    std::uint16_t tileset{};
    std::uint16_t tileset_mask{};
    std::uint16_t metatiles{};
    std::uint16_t object_db{};
    std::vector<assets::PaletteRef> palettes{};
};

struct MusicJson {
    std::uint8_t tune{};
    std::uint8_t at_start{};
    std::uint8_t at_end{};
};

// What a level starts with: the Vikings, or a cutscene's one entity.
struct StartJson {
    int x{};
    int y{};
    std::uint16_t kind{};
    std::string kind_hex{};
    std::uint16_t flags{};
    std::uint16_t argument{};
    bool facing_left{};
};

struct PlacementJson {
    std::size_t index{};  // in the level data's list
    int x{};              // the centre
    int y{};
    int width{};  // 0: the kind's own
    int height{};
    std::uint16_t kind{};
    std::string kind_hex{};
    std::uint16_t flags{};
    std::uint16_t argument{};
    bool facing_left{};
    bool always_active{};
    bool created_again_at_once{};
    std::uint16_t sprite_entry{};  // the kind's sprites: a DATA.DAT entry; 65535 none, 65534 the shared pool
};

struct TileTypeJson {
    std::uint8_t tile_type{};
    std::string hex{};
    std::string name{};    // empty: not named yet
    std::string colour{};  // in NN-collision.png
    std::size_t cells{};
};

struct LevelJson {
    std::string release{};
    std::string name{};
    int number{};  // 0: a cutscene
    std::string world{};
    int width{};  // cells of 16 x 16 pixels
    int height{};
    LevelSources sources{};
    std::uint16_t switches{};
    std::uint16_t extra{};
    int gravity{};
    int push{};
    bool hud{};
    MusicJson music{};
    int next_slot{};
    std::vector<StartJson> start{};
    std::vector<PlacementJson> placements{};
    std::vector<TileTypeJson> tile_types{};       // the ones the map uses
    std::vector<std::uint16_t> cell_metatiles{};  // per map cell, row by row
    std::vector<std::uint8_t> cell_tile_types{};
};

// scenes/<name>.json
struct SceneSources {
    std::uint16_t header{};
    std::uint16_t map{};        // tilemap, or the full-screen image for image scenes
    std::uint16_t tileset{};    // 65535 = none (image scene)
    std::uint16_t metatiles{};  // 65535 = none (image scene)
    std::uint16_t hud_image{};  // 65535 = none; our own pairing, not from the header
    std::vector<assets::PaletteRef> palettes{};
};

struct SceneJson {
    std::string release{};
    std::string name{};
    std::string description{};
    std::string kind{};    // "tiled" or "image"
    std::size_t width{};   // pixels
    std::size_t height{};  // pixels
    SceneSources sources{};
    std::vector<assets::ObjectPlacement> objects{};
};

// images/<name>.json, for stand-alone images
struct ImageSources {
    std::uint16_t image{};
    std::uint16_t palette{};
};

struct ImageJson {
    std::string release{};
    std::string name{};
    std::string description{};
    std::size_t width{};
    std::size_t height{};
    ImageSources sources{};
};

// sprites/NNN-name.json
struct SpriteFrameJson {
    std::size_t offset{};  // in the entry; the game's animations address frames by offset
    std::size_t size{};    // bytes
    std::size_t x{};       // where the frame is in the PNG
    std::size_t y{};
    std::size_t width{};
    std::size_t height{};
};

struct SpriteJson {
    std::string release{};
    std::uint16_t entry{};
    std::string name{};
    std::string format{};  // raw, unpacked, packed32 or tiled
    std::string pixels{};  // what the PNG's pixel values mean
    bool colour_0_transparent{};
    // The palette the PNG shows: for raw and unpacked sprites the palettes of a level or scene that loads the entry
    // (preview_header), for packed32 and tiled one 16-colour palette (the game picks the colour base when drawing).
    std::uint16_t preview_header{};
    std::vector<assets::PaletteRef> preview_palettes{};
    std::vector<std::uint16_t> loaded_by{};  // level and scene headers that load this entry
    std::vector<SpriteFrameJson> frames{};
};

namespace {

// --- Writing files ---

std::expected<void, std::string> write_file(const fs::path& path, std::span<const std::byte> bytes) {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    if (!out || !out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        return std::unexpected{std::format("cannot write {}", path.string())};
    }
    return {};
}

std::expected<void, std::string> write_text(const fs::path& path, std::string_view text) {
    return write_file(path, std::as_bytes(std::span{text.data(), text.size()}));
}

std::expected<fs::path, std::string> make_dir(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        return std::unexpected{std::format("cannot create {}: {}", dir.string(), ec.message())};
    }
    return dir;
}

template <typename Json>
std::expected<void, std::string> write_json(const fs::path& path, const Json& value) {
    const auto text = glz::write<glz::opts{.prettify = true}>(value);
    if (!text) {
        return std::unexpected{std::format("cannot serialise {}", path.string())};
    }
    return write_text(path, *text);
}

std::expected<void, std::string> write_png(const fs::path& path, const assets::IndexedImage& image,
                                           const assets::Palette& palette) {
    return image::save_indexed_png(path, image, palette);
}

std::expected<void, std::string> first_error(std::initializer_list<std::expected<void, std::string>> results) {
    for (const auto& result : results) {
        if (!result) {
            return result;
        }
    }
    return {};
}

int fail(std::string_view message) {
    std::println(stderr, "lv-export: {}", message);
    return EXIT_FAILURE;
}

// --- Sheets: many small images side by side in one PNG ---

// All tiles side by side, `columns` per row.
assets::IndexedImage tile_sheet(std::span<const assets::IndexedImage> tiles, std::size_t columns) {
    const std::size_t rows = (tiles.size() + columns - 1) / columns;
    assets::IndexedImage sheet = assets::make_image(columns * assets::kTileSize, rows * assets::kTileSize);
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        for (std::size_t y = 0; y < assets::kTileSize; ++y) {
            for (std::size_t x = 0; x < assets::kTileSize; ++x) {
                sheet.set(((i % columns) * assets::kTileSize) + x, ((i / columns) * assets::kTileSize) + y,
                          tiles[i].at(x, y));
            }
        }
    }
    return sheet;
}

std::expected<assets::IndexedImage, std::string> metatile_sheet(std::span<const assets::Metatile> metatiles,
                                                                std::span<const assets::IndexedImage> tileset,
                                                                std::size_t columns) {
    const std::size_t rows = (metatiles.size() + columns - 1) / columns;
    assets::IndexedImage sheet = assets::make_image(columns * assets::kMetatileSize, rows * assets::kMetatileSize);
    for (std::size_t i = 0; i < metatiles.size(); ++i) {
        const auto drawn = assets::draw_metatile(sheet, (i % columns) * assets::kMetatileSize,
                                                 (i / columns) * assets::kMetatileSize, metatiles[i], tileset);
        if (!drawn) {
            return std::unexpected{std::format("metatile {}: {}", i, assets::describe(drawn.error()))};
        }
    }
    return sheet;
}

// All frames in order, left to right, wrapping into a new row when the next frame would pass kSheetWidth.
struct SpriteSheet {
    assets::IndexedImage image;
    std::vector<SpriteFrameJson> frames;
};

SpriteSheet sprite_sheet(std::span<const assets::SpriteFrame> frames) {
    constexpr std::size_t kSheetWidth = 256;
    SpriteSheet sheet;
    sheet.frames.reserve(frames.size());
    std::size_t x = 0;
    std::size_t y = 0;
    std::size_t row_height = 0;
    std::size_t width = 0;
    for (const assets::SpriteFrame& frame : frames) {
        if (x > 0 && x + frame.image.width > kSheetWidth) {
            x = 0;
            y += row_height;
            row_height = 0;
        }
        sheet.frames.push_back(SpriteFrameJson{.offset = frame.offset,
                                               .size = frame.size,
                                               .x = x,
                                               .y = y,
                                               .width = frame.image.width,
                                               .height = frame.image.height});
        x += frame.image.width;
        width = std::max(width, x);
        row_height = std::max(row_height, frame.image.height);
    }
    sheet.image = assets::make_image(width, y + row_height);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const assets::IndexedImage& image = frames[i].image;
        for (std::size_t fy = 0; fy < image.height; ++fy) {
            for (std::size_t fx = 0; fx < image.width; ++fx) {
                sheet.image.set(sheet.frames[i].x + fx, sheet.frames[i].y + fy, image.at(fx, fy));
            }
        }
    }
    return sheet;
}

// --- Exporting ---

// What the level dump needs besides the level: the EXE's tables and the game's font.
struct DumpContext {
    const assets::GameFiles* files = nullptr;
    assets::EngineTables tables;
    std::array<assets::VikingStartTable, 3> starts{};
    std::vector<assets::IndexedImage> font;
    fs::path dir;  // <output>/levels
};

PlacementJson placement_json(std::size_t index, const Placement& placement, const assets::ObjectDatabase* database) {
    constexpr std::uint16_t kNoSprites = 0xffff;
    return PlacementJson{
        .index = index,
        .x = placement.x,
        .y = placement.y,
        .width = placement.width,
        .height = placement.height,
        .kind = placement.kind,
        .kind_hex = std::format("{:02X}", placement.kind),
        .flags = placement.flags,
        .argument = placement.argument,
        .facing_left = placement.facing_left(),
        .always_active = placement.always_active(),
        .created_again_at_once = placement.created_again_at_once(),
        .sprite_entry = database != nullptr && placement.kind < database->records.size()
                            ? database->records[placement.kind].word(0)
                            : kNoSprites,
    };
}

// The dump of one level or cutscene: <prefix>level.json, <prefix>world.png, <prefix>collision.png.
std::expected<void, std::string> export_level_dump(const DumpContext& context, const Level& level,
                                                   const assets::LevelHeader& header, LevelSources sources,
                                                   const assets::IndexedImage& map_image, std::string_view prefix) {
    const auto slot = std::ranges::find(context.tables.slot_headers, sources.header);
    if (slot == context.tables.slot_headers.end()) {
        return std::unexpected{std::format("no slot has header entry {}", sources.header)};
    }
    const auto slot_number = static_cast<std::uint16_t>(slot - context.tables.slot_headers.begin());
    sources.object_db = context.tables.slot_databases.at(slot_number);
    sources.palettes = header.palettes;
    std::optional<assets::ObjectDatabase> database;
    if (sources.object_db != assets::kNoEntry) {
        auto loaded = assets::load_object_database(context.files->archive(), sources.object_db);
        if (!loaded) {
            return std::unexpected{loaded.error()};
        }
        database = std::move(*loaded);
    }
    const assets::IndexedImage world = world_picture(level, map_image, context.font);
    const CollisionMap collision = collision_map(level.map, {}, context.font);

    LevelJson json{
        .release = std::string{context.files->release().name},
        .name = level.name,
        .number = level.number,
        .world = level.world,
        .width = level.map.width,
        .height = level.map.height,
        .sources = std::move(sources),
        .switches = level.switches,
        .extra = level.extra,
        .gravity = level.gravity,
        .push = level.push,
        .hud = level.hud,
        .music = {.tune = level.music.tune, .at_start = level.music.at_start, .at_end = level.music.at_end},
        .next_slot = level.next_slot,
    };
    const assets::ObjectDatabase* records = database ? &*database : nullptr;
    for (const Placement& start : level.start) {
        json.start.push_back(StartJson{.x = start.x,
                                       .y = start.y,
                                       .kind = start.kind,
                                       .kind_hex = std::format("{:02X}", start.kind),
                                       .flags = start.flags,
                                       .argument = start.argument,
                                       .facing_left = start.facing_left()});
    }
    for (std::size_t i = 0; i < level.placements.size(); ++i) {
        PlacementJson placement = placement_json(i, level.placements[i], records);
        json.placements.push_back(std::move(placement));
    }
    for (const TileTypeUse& use : collision.legend) {
        json.tile_types.push_back(
            TileTypeJson{.tile_type = use.tile_type,
                         .hex = std::format("{:02X}", use.tile_type),
                         .name = use.name,
                         .colour = std::format("#{:02x}{:02x}{:02x}", use.colour.r, use.colour.g, use.colour.b),
                         .cells = use.cells});
    }
    for (const Cell& cell : level.map.cells) {
        json.cell_metatiles.push_back(cell.metatile);
        json.cell_tile_types.push_back(cell.tile_type);
    }

    std::vector<std::expected<void, std::string>> written{
        write_png(context.dir / std::format("{}world.png", prefix), world, level.palette),
        write_json(context.dir / std::format("{}level.json", prefix), json)};
    if (!level.map.cells.empty()) {
        written.push_back(
            write_png(context.dir / std::format("{}collision.png", prefix), collision.image, collision.palette));
    }
    for (auto& result : written) {
        if (!result) {
            return result;
        }
    }
    return {};
}

std::expected<void, std::string> export_level(const DumpContext& context, const assets::LevelData& level) {
    constexpr std::size_t kSheetColumns = 16;
    const auto metatile_image = metatile_sheet(level.map.metatiles, level.map.tileset, kSheetColumns);
    if (!metatile_image) {
        return std::unexpected{metatile_image.error()};
    }
    const assets::LevelHeader& header = level.header;
    const std::string prefix = std::format("{:02}-", level.info.number);  // the game's level number, not an entry
    return first_error(
        {export_level_dump(context, make_level(level, context.starts), header,
                           {.header = level.info.header_entry,
                            .map = header.map_entry,
                            .tileset = header.tileset_entry,
                            .tileset_mask = static_cast<std::uint16_t>(header.tileset_entry + 1U),
                            .metatiles = header.metatiles_entry},
                           level.map.image, prefix),
         write_png(context.dir / (prefix + "tileset.png"), tile_sheet(level.map.tileset, kSheetColumns), level.palette),
         write_png(context.dir / (prefix + "metatiles.png"), *metatile_image, level.palette)});
}

// A cutscene's dump, next to the levels' (its pictures stay in scenes/ too).
std::expected<void, std::string> export_cutscene(const DumpContext& context, const assets::SceneData& scene) {
    const assets::LevelHeader& header = scene.header;
    const bool tiled = header.is_tiled();
    return export_level_dump(
        context, make_cutscene(scene, context.starts), header,
        {.header = scene.info.header_entry,
         .map = header.map_entry,
         .tileset = header.tileset_entry,
         .tileset_mask = tiled ? static_cast<std::uint16_t>(header.tileset_entry + 1U) : assets::kNoEntry,
         .metatiles = header.metatiles_entry},
        scene.image, std::format("{}-", scene.info.name));
}

std::expected<void, std::string> export_scene(const assets::SceneData& scene, std::string_view release,
                                              const fs::path& output_dir) {
    const assets::LevelHeader& header = scene.header;
    const SceneJson json{.release = std::string{release},
                         .name = std::string{scene.info.name},
                         .description = std::string{scene.info.description},
                         .kind = header.is_tiled() ? "tiled" : "image",
                         .width = scene.image.width,
                         .height = scene.image.height,
                         .sources = {.header = scene.info.header_entry,
                                     .map = header.map_entry,
                                     .tileset = header.tileset_entry,
                                     .metatiles = header.metatiles_entry,
                                     .hud_image = scene.info.hud_entry,
                                     .palettes = header.palettes},
                         .objects = header.objects};
    const auto dir = make_dir(output_dir / "scenes");
    if (!dir) {
        return std::unexpected{dir.error()};
    }
    const std::string stem{scene.info.name};
    return first_error(
        {write_png(*dir / (stem + ".png"), scene.image, scene.palette), write_json(*dir / (stem + ".json"), json)});
}

std::expected<void, std::string> export_image(const assets::Image& image, std::string_view release,
                                              const fs::path& output_dir) {
    const ImageJson json{.release = std::string{release},
                         .name = std::string{image.info.name},
                         .description = std::string{image.info.description},
                         .width = image.image.width,
                         .height = image.image.height,
                         .sources = {.image = image.info.image_entry, .palette = image.info.palette_entry}};
    const auto dir = make_dir(output_dir / "images");
    if (!dir) {
        return std::unexpected{dir.error()};
    }
    const std::string stem{image.info.name};
    return first_error(
        {write_png(*dir / (stem + ".png"), image.image, image.palette), write_json(*dir / (stem + ".json"), json)});
}

// Which level and scene headers load each sprite entry, in kLevels then kScenes order.
using SpriteLoaders = std::map<std::uint16_t, std::vector<std::uint16_t>>;

std::expected<SpriteLoaders, std::string> sprite_loaders(const assets::Archive& archive) {
    std::vector<std::uint16_t> headers;
    headers.reserve(assets::kLevels.size() + assets::kScenes.size());
    for (const assets::LevelInfo& info : assets::kLevels) {
        headers.push_back(info.header_entry);
    }
    for (const assets::SceneInfo& info : assets::kScenes) {
        headers.push_back(info.header_entry);
    }
    SpriteLoaders loaders;
    for (const std::uint16_t header_entry : headers) {
        const auto header = assets::load_header(archive, header_entry);
        if (!header) {
            return std::unexpected{header.error()};
        }
        const auto add = [&](std::uint16_t entry) {
            auto& list = loaders[entry];
            if (std::ranges::find(list, header_entry) == list.end()) {
                list.push_back(header_entry);
            }
        };
        for (const assets::SpriteSetRef& set : header->sprite_sets) {
            add(set.entry);
        }
        for (const assets::PackedSetRef& set : header->packed_sets) {
            add(set.entry);
        }
    }
    return loaders;
}

std::expected<void, std::string> export_sprite(const assets::Archive& archive, const assets::SpriteInfo& info,
                                               const SpriteLoaders& loaders, std::string_view release,
                                               const fs::path& output_dir) {
    // Raw and unpacked sets that no header loads (the HUD graphics, three unused sets) are shown with level 1's
    // palette.
    const auto loaded = loaders.find(info.entry);
    const std::vector<std::uint16_t> loaded_by =
        loaded != loaders.end() ? loaded->second : std::vector<std::uint16_t>{};

    SpriteJson json{.release = std::string{release},
                    .entry = info.entry,
                    .name = std::string{info.name},
                    .format = std::string{assets::to_string(info.format)},
                    .colour_0_transparent = info.format != assets::SpriteFormat::kRaw,
                    .loaded_by = loaded_by};
    if (info.format == assets::SpriteFormat::kPacked32 || info.format == assets::SpriteFormat::kTiled) {
        json.pixels = "1-15, added to a colour base the game picks when drawing";
        json.preview_header = assets::kNoEntry;
        json.preview_palettes = {assets::PaletteRef{.entry = info.preview_palette, .first_color = 0}};
    } else {
        json.pixels = "palette indices";
        json.preview_header = loaded_by.empty() ? assets::kLevels[0].header_entry : loaded_by.front();
        const auto header = assets::load_header(archive, json.preview_header);
        if (!header) {
            return std::unexpected{header.error()};
        }
        json.preview_palettes = header->palettes;
    }
    const auto palette = assets::load_palette(archive, json.preview_palettes);
    const auto sprites = assets::load_sprites(archive, info);
    if (!palette || !sprites) {
        return std::unexpected{!palette ? palette.error() : sprites.error()};
    }
    SpriteSheet sheet = sprite_sheet(sprites->frames);
    json.frames = std::move(sheet.frames);

    const auto dir = make_dir(output_dir / "sprites");
    if (!dir) {
        return std::unexpected{dir.error()};
    }
    const std::string stem = std::format("{:03}-{}", info.entry, info.name);
    const auto transparent = json.colour_0_transparent ? std::optional<std::uint8_t>{0} : std::optional<std::uint8_t>{};
    return first_error({image::save_indexed_png(*dir / (stem + ".png"), sheet.image, *palette, transparent),
                        write_json(*dir / (stem + ".json"), json)});
}

// The object databases as listings (scripts/NNN.txt) and how often each opcode occurs (scripts/usage.txt). The first
// line of each file names its source.
std::expected<void, std::string> export_scripts(const assets::Archive& archive, std::string_view release,
                                                const fs::path& output_dir) {
    std::map<std::uint16_t, std::string> users;  // database -> the worlds that use it
    for (const assets::LevelInfo& info : assets::kLevels) {
        std::string& list = users[info.object_db_entry];
        if (!list.contains(info.world)) {
            list += std::format("{}{}", list.empty() ? "" : ", ", info.world);
        }
    }
    users[assets::kSceneObjectDbEntry] = "scenes";
    const auto dir = make_dir(output_dir / "scripts");
    if (!dir) {
        return std::unexpected{dir.error()};
    }
    std::array<std::size_t, lvr::assets::kOpcodeCount> usage{};
    for (const auto& [entry, worlds] : users) {
        const auto database = assets::load_object_database(archive, entry);
        if (!database) {
            return std::unexpected{database.error()};
        }
        for (const lvr::assets::Instruction& instruction :
             lvr::assets::trace_code(database->bytes, lvr::assets::script_entry_points(*database)).instructions) {
            ++usage.at(instruction.opcode);
        }
        const std::string text = std::format("; {}, {} entry {}: object database of {}\n{}", release, assets::kDataName,
                                             entry, worlds, lvr::assets::disassemble(*database));
        if (const auto written = write_text(*dir / std::format("{}.txt", entry), text); !written) {
            return written;
        }
    }
    std::string text = std::format("; {}, {} entries {}-{}: instructions per opcode, all object databases\n", release,
                                   assets::kDataName, users.begin()->first, users.rbegin()->first);
    for (std::size_t code = 0; code < usage.size(); ++code) {
        text += std::format("{:02X} {:6}  {}\n", code, usage.at(code),
                            lvr::assets::opcode_info(static_cast<std::uint8_t>(code)).name);
    }
    return write_text(*dir / "usage.txt", text);
}

// dialogue/texts.txt: one text per line, its lines joined by " / ".
std::expected<void, std::string> export_dialogue(const assets::GameFiles& files, const fs::path& output_dir) {
    const auto texts = assets::load_dialogue_texts(files.exe(), files.release());
    if (!texts) {
        return std::unexpected{texts.error()};
    }
    const assets::ExeRange range = files.release().dialogue_texts;
    std::string text = std::format("; {}, {} bytes {:#x}-{:#x} (segment 07A6): number, offset, box size, text\n",
                                   files.release().name, assets::kExeName, range.offset, range.offset + range.size);
    for (std::size_t i = 0; i < texts->size(); ++i) {
        const assets::DialogueText& entry = (*texts)[i];
        std::string joined;
        for (std::size_t line = 0; line < entry.lines.size(); ++line) {
            joined += std::format("{}{}", line == 0 ? "" : " / ", entry.lines[line]);
        }
        text += std::format("{:3} {:04X} {:2}x{:<2} {}\n", i, entry.offset, entry.width, entry.height, joined);
    }
    const auto dir = make_dir(output_dir / "dialogue");
    if (!dir) {
        return std::unexpected{dir.error()};
    }
    return write_text(*dir / "texts.txt", text);
}

// --- Commands ---

int cmd_verify(const fs::path& original_dir) {
    const auto files = assets::GameFiles::open(original_dir);
    if (!files) {
        return fail(files.error());
    }
    const assets::Archive& archive = files->archive();
    std::size_t xmidi = 0;
    for (std::size_t i = 0; i < archive.entry_count(); ++i) {
        const auto decoded = assets::load_entry(archive, i);
        if (!decoded) {
            return fail(decoded.error());
        }
        if (assets::detect_kind(i, *decoded) == assets::EntryKind::kXmidi) {
            ++xmidi;
        }
    }
    std::println("release: {}", files->release().name);
    std::println("entries: {} ({} stored, {} compressed), all decode", archive.entry_count(),
                 assets::kStoredEntries.size(), archive.entry_count() - assets::kStoredEntries.size());
    std::println("xmidi:   {}", xmidi);
    return EXIT_SUCCESS;
}

int cmd_dump(const fs::path& original_dir, const fs::path& output_dir) {
    const auto files = assets::GameFiles::open(original_dir);
    if (!files) {
        return fail(files.error());
    }
    const auto raw_dir = make_dir(output_dir / "raw");
    if (!raw_dir) {
        return fail(raw_dir.error());
    }
    const assets::Archive& archive = files->archive();
    DumpIndex index{.release = std::string{files->release().name}};
    for (std::size_t i = 0; i < archive.entry_count(); ++i) {
        const auto raw = archive.raw_entry(i);
        const auto decoded = assets::load_entry(archive, i);
        if (!raw || !decoded) {
            return fail(std::format("entry {} does not decode", i));
        }
        if (const auto written = write_file(*raw_dir / std::format("{:03}.bin", i), *decoded); !written) {
            return fail(written.error());
        }
        index.entries.push_back(DumpEntry{
            .index = i,
            .stored = assets::is_stored_entry(i),
            .packed_size = raw->size(),
            .size = decoded->size(),
            .kind = std::string{assets::to_string(assets::detect_kind(i, *decoded))},
            .fnv1a64 = std::format("{:016x}", assets::fnv1a64(*decoded)),
        });
    }
    if (const auto written = write_json(*raw_dir / "index.json", index); !written) {
        return fail(written.error());
    }
    std::println("wrote {} entries to {}", index.entries.size(), raw_dir->string());
    return EXIT_SUCCESS;
}

int cmd_assets(const fs::path& original_dir, const fs::path& output_dir) {
    const auto files = assets::GameFiles::open(original_dir);
    if (!files) {
        return fail(files.error());
    }
    const assets::Archive& archive = files->archive();
    const std::string_view release = files->release().name;
    auto tables = assets::load_engine_tables(files->exe(), files->release());
    auto starts = assets::load_viking_starts(files->exe(), files->release());
    auto font = assets::load_sprites(archive, 2);  // the game's font
    const auto levels_dir = make_dir(output_dir / "levels");
    if (!tables || !starts || !font || !levels_dir) {
        return fail(!tables ? tables.error()
                            : (!starts ? starts.error() : (!font ? font.error() : levels_dir.error())));
    }
    std::vector<assets::IndexedImage> glyphs;
    for (assets::SpriteFrame& frame : font->frames) {
        glyphs.push_back(std::move(frame.image));
    }
    const DumpContext context{.files = &*files,
                              .tables = std::move(*tables),
                              .starts = *starts,
                              .font = std::move(glyphs),
                              .dir = *levels_dir};
    for (const assets::LevelInfo& info : assets::kLevels) {
        const auto level = assets::load_level(archive, info);
        if (!level) {
            return fail(level.error());
        }
        if (const auto exported = export_level(context, *level); !exported) {
            return fail(std::format("level {}: {}", info.number, exported.error()));
        }
    }
    for (const assets::SceneInfo& info : assets::kScenes) {
        const auto scene = assets::load_scene(archive, info);
        if (!scene) {
            return fail(scene.error());
        }
        if (const auto exported = export_scene(*scene, release, output_dir); !exported) {
            return fail(std::format("scene {}: {}", info.name, exported.error()));
        }
        if (std::ranges::find(kCutscenes, info.name) != kCutscenes.end()) {
            if (const auto exported = export_cutscene(context, *scene); !exported) {
                return fail(std::format("cutscene {}: {}", info.name, exported.error()));
            }
        }
    }
    for (const assets::ImageInfo& info : assets::kImages) {
        const auto image = assets::load_image(archive, info);
        if (!image) {
            return fail(image.error());
        }
        if (const auto exported = export_image(*image, release, output_dir); !exported) {
            return fail(std::format("image {}: {}", info.name, exported.error()));
        }
    }
    const auto loaders = sprite_loaders(archive);
    if (!loaders) {
        return fail(loaders.error());
    }
    for (const assets::SpriteInfo& info : assets::kSpriteCatalog) {
        if (const auto exported = export_sprite(archive, info, *loaders, release, output_dir); !exported) {
            return fail(std::format("sprite entry {}: {}", info.entry, exported.error()));
        }
    }
    if (const auto exported =
            first_error({export_scripts(archive, release, output_dir), export_dialogue(*files, output_dir)});
        !exported) {
        return fail(exported.error());
    }
    // The SNES version's backdrops, when the folder also holds the user's ROM image (docs/formats/snes.md).
    if (const auto rom = assets::SnesRom::open(original_dir); rom) {
        const auto dir = make_dir(output_dir / "snes");
        if (!dir) {
            return fail(dir.error());
        }
        for (const assets::LevelInfo& info : assets::kLevels) {
            const auto backdrop = assets::load_level_backdrop(*rom, info.number);
            const auto level = assets::load_level(archive, info);
            if (!backdrop || !level) {
                return fail(!backdrop ? backdrop.error() : level.error());
            }
            // The whole level with its backdrop behind it, as Remaster rules' parallax lays it in.
            const BackdropPreview preview =
                backdrop_preview(make_level(*level, context.starts).map, level->map.image, backdrop->image,
                                 backdrop->header.scroll_x, backdrop->header.scroll_y);
            if (const auto written = first_error(
                    {write_png(*dir / std::format("{:02}-backdrop.png", info.number), backdrop->image,
                               backdrop->palette),
                     write_png(*dir / std::format("{:02}-level.png", info.number), preview.image, level->palette)});
                !written) {
                return fail(written.error());
            }
            std::println("level {:2}: the backdrop shows in {:4.1f}% of the map", info.number,
                         100.0 * static_cast<double>(preview.shown) /
                             static_cast<double>(std::max<std::size_t>(preview.image.pixels.size(), 1)));
        }
        for (const assets::SnesScene& snes_scene : assets::kSnesSceneHeaders) {
            const auto* info = std::ranges::find(assets::kScenes, snes_scene.name, &assets::SceneInfo::name);
            const auto backdrop = assets::load_scene_backdrop(*rom, snes_scene.name);
            auto scene = info != assets::kScenes.end()
                             ? assets::load_scene(archive, *info)
                             : std::unexpected{std::format("no scene \"{}\"", snes_scene.name)};
            if (!backdrop || !scene) {
                return fail(!backdrop ? backdrop.error() : scene.error());
            }
            const BackdropPreview preview =
                backdrop_preview(make_cutscene(*scene, context.starts).map, scene->image, backdrop->image,
                                 backdrop->header.scroll_x, backdrop->header.scroll_y);
            if (const auto written = first_error(
                    {write_png(*dir / std::format("{}-backdrop.png", snes_scene.name), backdrop->image,
                               backdrop->palette),
                     write_png(*dir / std::format("{}-scene.png", snes_scene.name), preview.image, scene->palette)});
                !written) {
                return fail(written.error());
            }
            std::println("{}: the backdrop shows in {:4.1f}% of the scene", snes_scene.name,
                         100.0 * static_cast<double>(preview.shown) /
                             static_cast<double>(std::max<std::size_t>(preview.image.pixels.size(), 1)));
        }
        std::println("exported {} SNES backdrops, and each level and scene with its backdrop, to {}",
                     assets::kLevels.size() + assets::kSnesSceneHeaders.size(), dir->string());
    } else {
        std::println("no SNES backdrops: {}", rom.error());
    }
    std::println(
        "exported {} levels, {} scenes, {} images, {} sprite entries, the object scripts and the dialogue to {}",
        assets::kLevels.size(), assets::kScenes.size(), assets::kImages.size(), assets::kSpriteCatalog.size(),
        output_dir.string());
    return EXIT_SUCCESS;
}

void print_usage(std::string_view program) {
    std::println(stderr,
                 "usage:\n"
                 "  {0} verify <original-dir>\n"
                 "  {0} assets <original-dir> <output-dir>\n"
                 "  {0} dump   <original-dir> <output-dir>",
                 program);
}

}  // namespace

}  // namespace lvr::exporter

int main(int argc, char** argv) try {
    const std::span args{argv, static_cast<std::size_t>(argc)};
    const std::string_view program = args.empty() ? "lv-export" : args[0];
    if (args.size() == 3 && std::string_view{args[1]} == "verify") {
        return lvr::exporter::cmd_verify(args[2]);
    }
    if (args.size() == 4 && std::string_view{args[1]} == "assets") {
        return lvr::exporter::cmd_assets(args[2], args[3]);
    }
    if (args.size() == 4 && std::string_view{args[1]} == "dump") {
        return lvr::exporter::cmd_dump(args[2], args[3]);
    }
    lvr::exporter::print_usage(program);
    return 2;
} catch (const std::exception& e) {
    std::fputs("lv-export: fatal: ", stderr);
    std::fputs(e.what(), stderr);
    std::fputs("\n", stderr);
    return EXIT_FAILURE;
} catch (...) {
    std::fputs("lv-export: fatal: unhandled exception\n", stderr);
    return EXIT_FAILURE;
}
