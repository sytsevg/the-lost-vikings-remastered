#include "lvr/level.hpp"

#include <format>

namespace lvr {

namespace {

// Positions are 16-bit words in the level data; the levels that drop the Vikings in from above have y = -176.
int signed_word(std::uint16_t value) noexcept {
    return static_cast<std::int16_t>(value);
}

Placement placed(const assets::ObjectPlacement& object) {
    return Placement{.x = object.x,
                     .y = object.y,
                     .width = object.width,
                     .height = object.height,
                     .kind = object.type,
                     .flags = object.flags,
                     .argument = object.arg};
}

void fill_from_header(Level& level, const assets::LevelHeader& header,
                      const std::array<assets::VikingStartTable, 3>& starts) {
    constexpr std::uint8_t kHudFlag = 0x01;
    level.start = make_start(header, starts);
    level.placements.reserve(header.objects.size());
    for (const assets::ObjectPlacement& object : header.objects) {
        level.placements.push_back(placed(object));
    }
    level.switches = header.switches;
    level.extra = header.word_14;
    level.gravity = header.gravity;
    level.push = header.push;
    level.hud = (header.flags & kHudFlag) != 0;
    level.quits_at_once = (header.flags & 0x08U) != 0;
    level.text_direct = (header.flags & 0xe0U) != 0;
    level.width = header.width;
    level.height = header.height;
    level.music = Music{.tune = header.tune, .at_start = header.music_at_start, .at_end = header.music_at_end};
    level.next_slot = header.next_slot;
    level.palette_animations = header.palette_animations;
    level.palette_animations_running = header.palette_animation_flags;
}

void fill_from_map(Level& level, const assets::TiledMap& map) {
    level.map = make_map(map.map);
    level.tileset = map.tileset;
    level.metatiles = map.metatiles;
}

}  // namespace

Terrain terrain(std::uint8_t tile_type) noexcept {
    if (tile_type >= tile::kFirstSlope && tile_type <= tile::kLastSlope) {
        return Terrain::kSlope;
    }
    switch (tile_type) {
        case 0x00:
        case 0x03:
        case 0x0c:
            return Terrain::kFree;
        case 0x01:
        case 0x02:
        case 0x04:
        case 0x05:
        case 0x20:
            return Terrain::kGround;
        default:
            return Terrain::kUnknown;
    }
}

std::string tile_type_name(std::uint8_t tile_type) {
    switch (tile_type) {
        case tile::kLadder:
            return "ladder";
        case tile::kLadderTop:
            return "ladder top";
        case tile::kLava:
            return "lava";
        case tile::kPlatformStop:
            return "platform stop";
        case tile::kHiddenPlatform:
            return "hidden platform";
        case tile::kWater:
            return "water";
        case tile::kQuicksand:
            return "quicksand";
        case tile::kSpikes:
            return "spikes";
        case tile::kElectricity:
            return "electricity";
        case tile::kSpring:
            return "spring";
        case tile::kRotatingSpikes:
            return "rotating spikes";
        default:
            break;
    }
    switch (terrain(tile_type)) {
        case Terrain::kFree:
            return "free";
        case Terrain::kGround:
            return "ground";
        case Terrain::kSlope:
            return std::format("slope {:x}", tile_type - tile::kFirstSlope);
        case Terrain::kUnknown:
            break;
    }
    return {};
}

std::uint8_t Map::tile_type_at(int x, int y) const noexcept {
    if (x < 0 || x >= pixel_width()) {
        return tile::kGround;
    }
    if (y < 0 || y >= pixel_height()) {
        return tile::kFree;
    }
    return cells[(static_cast<std::size_t>(y / kCellSize) * static_cast<std::size_t>(width)) +
                 static_cast<std::size_t>(x / kCellSize)]
        .tile_type;
}

Map make_map(const assets::Tilemap& tilemap) {
    Map map{.width = static_cast<int>(tilemap.width), .height = static_cast<int>(tilemap.height), .cells = {}};
    map.cells.reserve(tilemap.cells.size());
    for (const assets::MapCell& cell : tilemap.cells) {
        // The decoder splits the word at bit 9; the game at bit 10.
        map.cells.push_back(Cell{.metatile = static_cast<std::uint16_t>(cell.metatile | ((cell.attributes & 1U) << 9U)),
                                 .tile_type = static_cast<std::uint8_t>(cell.attributes >> 1U)});
    }
    return map;
}

std::vector<Placement> make_start(const assets::LevelHeader& header,
                                  const std::array<assets::VikingStartTable, 3>& starts) {
    constexpr std::array<std::uint16_t, 3> kVikings{1, 0, 2};  // the order they are created in
    constexpr int kApart = 32;
    const std::uint8_t mode = header.start_mode;
    if (mode == assets::start_mode::kScene) {
        return {Placement{.x = signed_word(header.start_x),
                          .y = signed_word(header.start_y),
                          .kind = header.start_type,
                          .flags = header.start_flags,
                          .argument = header.start_arg}};
    }
    std::vector<Placement> start;
    if (mode == 2 || mode == 4 || mode == 5) {
        const assets::VikingStartTable& table = starts.at(mode == 2 ? 0 : (mode == 4 ? 1 : 2));
        for (std::size_t i = 0; i < kVikings.size(); ++i) {
            start.push_back(Placement{.x = signed_word(table.at(i).x),
                                      .y = signed_word(table.at(i).y),
                                      .kind = kVikings.at(i),
                                      .flags = static_cast<std::uint16_t>(table.at(i).flags | header.start_flags)});
        }
        return start;
    }
    // In a row behind the first; a level that drops them in has the second and third 16 and 8 pixels higher.
    const bool drop_in = mode == assets::start_mode::kDropIn;
    const std::array<int, 3> raised{0, drop_in ? 16 : 0, drop_in ? 8 : 0};
    const int direction = (header.start_flags & placement_flag::kFacingLeft) != 0 ? 1 : -1;
    for (std::size_t i = 0; i < kVikings.size(); ++i) {
        start.push_back(Placement{.x = signed_word(header.start_x) + (direction * kApart * static_cast<int>(i)),
                                  .y = signed_word(header.start_y) - raised.at(i),
                                  .kind = kVikings.at(i),
                                  .flags = header.start_flags,
                                  .argument = header.start_arg});
    }
    return start;
}

Level make_level(const assets::LevelData& data, const std::array<assets::VikingStartTable, 3>& starts) {
    Level level;
    level.name = std::format("level-{:02}", data.info.number);
    level.number = data.info.number;
    level.world = std::string{data.info.world};
    level.palette = data.palette;
    fill_from_header(level, data.header, starts);
    fill_from_map(level, data.map);
    return level;
}

Level make_cutscene(const assets::SceneData& data, const std::array<assets::VikingStartTable, 3>& starts) {
    Level level;
    level.name = std::string{data.info.name};
    level.palette = data.palette;
    fill_from_header(level, data.header, starts);
    if (data.map) {
        fill_from_map(level, *data.map);
    } else {
        level.picture = data.image;
    }
    return level;
}

}  // namespace lvr
