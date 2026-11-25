#include "lvr/play.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <string_view>
#include <utility>

#include "assets/catalog.hpp"
#include "assets/hash.hpp"
#include "lvr/archive.hpp"
#include "lvr/kinds.hpp"

namespace lvr {

namespace {

// The view's corner on one axis: the camera plus the shake's offset, or minus it where that would pass the
// camera's limit.
int shaken(int camera, const World::Shake& shake, int limit) noexcept {
    const auto plus = static_cast<std::uint16_t>(static_cast<std::uint16_t>(camera) + shake.offset);
    return plus <= static_cast<std::uint16_t>(limit) ? wrapped(plus) : wrapped(camera - shake.offset);
}

// Online (ADR-0012): every layer of text, into a player's view of it. What is at a place of the level (Line::
// in_level) is there in every view that has that place; what is at a place of the screen is in the middle of each
// view as the first player's has it (the shared layer), or only in its own player's view (said to him alone). The
// boxes' colour is the viewer's own text's, else the shared layer's, else another player's.
void compose_text(View& view, const World& world, int viewer, const Camera& camera) {
    constexpr int kCell = 8;
    const int left = static_cast<std::uint16_t>(camera.x) / kCell;  // the view's first text cell, in the level
    const int top = static_cast<std::uint16_t>(camera.y) / kCell;
    const int columns = std::min(view.width / kCell, kTextColumns);
    const int rows = std::min(view.height / kCell, kTextRows);
    view.text.fill(0);
    // A layer's cells, its first at (across, down) of the view's; true if any was put.
    const auto put = [&](const TextLayer& layer, int across, int down) {
        bool any = false;
        const auto& cells = layer.seen();
        for (int row = 0; row < layer.rows; ++row) {
            for (int column = 0; column < layer.columns; ++column) {
                const std::uint8_t character =
                    cells.at((static_cast<std::size_t>(row) * kTextLayerColumns) + static_cast<std::size_t>(column));
                const int to_column = across + column;
                const int to_row = down + row;
                if (character == 0 || to_column < 0 || to_column >= columns || to_row < 0 || to_row >= rows) {
                    continue;
                }
                view.text.at((static_cast<std::size_t>(to_row) * kTextColumns) + static_cast<std::size_t>(to_column)) =
                    character;
                any = true;
            }
        }
        return any;
    };
    const TextLayer& shared = world.text;
    const bool shared_shown = shared.in_level ? put(shared, shared.origin_x - left, shared.origin_y - top)
                                              : put(shared, (columns - shared.columns) / 2, (rows - shared.rows) / 2);
    std::optional<assets::Rgb> own;
    std::optional<assets::Rgb> other;
    for (int who = 0; who < world.player_count; ++who) {
        const Player& player = world.players.at(static_cast<std::size_t>(who));
        TextLayer colours;  // what his kColour lines leave
        bool any = false;
        for (const Line& line : player.said) {
            if (line.kind == Line::Kind::kColour) {
                colours.show(line, world.texts);
                continue;
            }
            if (!line.in_level && who != viewer) {
                continue;
            }
            TextLayer one;
            one.columns = kTextLayerColumns;
            one.rows = kTextLayerRows;
            one.show(line, world.texts);
            any = put(one, line.in_level ? line.origin_x - left : 0, line.in_level ? line.origin_y - top : 0) || any;
        }
        if (any && colours.colour3) {
            if (who == viewer) {
                own = colours.colour3;
            } else if (!other) {
                other = colours.colour3;
            }
        }
    }
    view.colour3 = own ? own : (shared_shown || !other ? shared.colour3 : other);
}

}  // namespace

namespace {

// What a level and a cutscene load alike: the pictures' masks, the sprites, the world's object database with the
// animation places of the other worlds' kinds, and what comes from VIKINGS.EXE. `number`: the level, 0 for a
// cutscene.
std::expected<LoadedLevel, std::string> load_with(const assets::GameFiles& files, const assets::LevelHeader& header,
                                                  Level level, std::uint16_t database_entry, int number) {
    const assets::Archive& archive = files.archive();
    auto database = assets::load_object_database(archive, database_entry);
    auto tables = load_animation_tables(files.exe(), files.release());
    if (!database || !tables) {
        return std::unexpected{!database ? database.error() : tables.error()};
    }
    LoadedLevel loaded;
    loaded.level = std::move(level);
    // The original blacks out the first colour of each row of 16 as it loads a palette.
    for (std::size_t colour = 16; colour < loaded.level.palette.size(); colour += 16) {
        loaded.level.palette.at(colour) = assets::Rgb{};
    }
    loaded.graphics.id = next_graphics_id();
    loaded.graphics.tileset = loaded.level.tileset;
    loaded.graphics.metatiles = loaded.level.metatiles;
    if (header.is_tiled()) {
        const auto masks = assets::load_entry(archive, std::size_t{header.tileset_entry} + 1);
        if (!masks) {
            return std::unexpected{masks.error()};
        }
        for (const std::byte b : *masks) {
            loaded.graphics.tile_masks.push_back(std::to_integer<std::uint8_t>(b));
        }
    } else if (auto image = assets::load_screen_image(archive, header.map_entry)) {
        loaded.graphics.picture = std::move(*image);  // a scene that is one picture
    }
    // What some scenes show where a level has its HUD.
    if ((header.flags & 0x02U) != 0 || (header.flags & 0x40U) != 0) {
        auto lower = assets::load_screen_image(archive, (header.flags & 0x02U) != 0 ? 384 : 531);
        if (!lower) {
            return std::unexpected{lower.error()};
        }
        loaded.graphics.hud_picture = std::move(*lower);
    } else if ((header.flags & 0x20U) != 0) {
        auto lower = assets::load_image(archive, assets::kImages[1]);
        if (!lower) {
            return std::unexpected{lower.error()};
        }
        loaded.graphics.hud_picture = std::move(lower->image);
    }
    for (const assets::SpriteSetRef& set : header.sprite_sets) {
        auto entry = assets::load_entry(archive, set.entry);
        if (!entry) {
            return std::unexpected{entry.error()};
        }
        loaded.sprite_sets.push_back(
            {.entry = set.entry, .base = static_cast<std::uint16_t>(loaded.sprites.sets.size() + 1)});
        loaded.sprites.sets.insert(loaded.sprites.sets.end(), entry->begin(), entry->end());
    }
    for (const assets::PackedSetRef& set : header.packed_sets) {
        auto entry = assets::load_entry(archive, set.entry);
        if (!entry) {
            return std::unexpected{entry.error()};
        }
        loaded.packed_entries.push_back(set.entry);
        loaded.sprites.packed_sets.push_back(std::move(*entry));
    }
    loaded.database = std::move(*database);
    // Each world's empty and solid cell (their pictures), by its database: 449 the spaceship, then the
    // prehistoric world, Egypt, the factory, the wacky world; 454 isn't a level's.
    constexpr std::array<std::uint16_t, 6> kSolidPictures{11, 6, 6, 1, 8, 6};
    const std::size_t world_index =
        database_entry >= database::kSpaceship ? std::size_t{database_entry} - database::kSpaceship : 0U;
    loaded.cleared_cell =
        Cell{.metatile = static_cast<std::uint16_t>(world_index == 0 ? 1 : 0), .tile_type = tile::kFree};
    loaded.filled_cell =
        Cell{.metatile = kSolidPictures.at(world_index % kSolidPictures.size()), .tile_type = tile::kGround};
    if (loaded.level.number >= 19 && loaded.level.number <= 21) {  // three factory levels have another set of pictures
        loaded.filled_cell.metatile = 2;
    }
    constexpr std::array<std::uint16_t, 6> kBrokenPictures{12, 14, 0, 11, 7, 0};
    loaded.broken_picture = kBrokenPictures.at(world_index % kBrokenPictures.size());
    loaded.database_entry = database_entry;
    for (std::size_t home = 0; home < kWorlds; ++home) {
        const auto entry = static_cast<std::uint16_t>(database::kSpaceship + home);
        if (entry == database_entry) {
            continue;
        }
        const auto other = assets::load_object_database(archive, entry);
        if (!other) {
            return std::unexpected{other.error()};
        }
        loaded.places_from.at(home) = pair_animation_places(other->bytes, loaded.database.bytes);
    }
    loaded.places = loaded.places_from.front();
    loaded.animation_tables = *tables;
    const assets::ExeRange slopes = files.release().slope_heights;
    if (slopes.size < loaded.slope_heights.size() || slopes.offset + slopes.size > files.exe().size()) {
        return std::unexpected{std::format("{}: the slope table is past the end of the file", assets::kExeName)};
    }
    const assets::ExeRange text_range = files.release().dialogue_texts;
    const assets::ExeRange passwords = files.release().passwords;
    const std::size_t password_at = passwords.offset + (static_cast<std::size_t>(number > 0 ? number - 1 : 0) * 4);
    if (text_range.offset + text_range.size > files.exe().size() || password_at + 4 > files.exe().size()) {
        return std::unexpected{std::format("{}: the texts are past the end of the file", assets::kExeName)};
    }
    for (std::size_t i = 0; i < text_range.size; ++i) {
        loaded.texts.push_back(std::to_integer<std::uint8_t>(files.exe()[text_range.offset + i]));
    }
    for (std::size_t at = passwords.offset; at + 4 <= passwords.offset + passwords.size && at + 4 <= files.exe().size();
         at += 4) {
        std::array<std::uint8_t, 4> letters{};
        for (std::size_t i = 0; i < letters.size(); ++i) {
            letters.at(i) = std::to_integer<std::uint8_t>(files.exe()[at + i]) & 0x7fU;
        }
        loaded.passwords.push_back(letters);
    }
    for (std::size_t i = 0; i < loaded.password.size(); ++i) {
        loaded.password.at(i) = std::to_integer<std::uint8_t>(files.exe()[password_at + i]) & 0x7fU;
    }
    for (std::size_t i = 0; i < loaded.slope_heights.size(); ++i) {
        loaded.slope_heights.at(i) = std::to_integer<std::uint8_t>(files.exe()[slopes.offset + i]);
    }
    return loaded;
}

}  // namespace

std::expected<LoadedLevel, std::string> load_level(const assets::GameFiles& files, int number) {
    if (number < 1 || std::cmp_greater(number, assets::kLevels.size())) {
        return std::unexpected{std::format("no level {}", number)};
    }
    const assets::LevelInfo& info = assets::kLevels.at(static_cast<std::size_t>(number - 1));
    auto data = assets::load_level(files.archive(), info);
    auto starts = assets::load_viking_starts(files.exe(), files.release());
    if (!data || !starts) {
        return std::unexpected{!data ? data.error() : starts.error()};
    }
    return load_with(files, data->header, make_level(*data, *starts), info.object_db_entry, number);
}

std::expected<LoadedLevel, std::string> load_game_over(const assets::GameFiles& files, int database_entry) {
    const auto* info = std::ranges::find(assets::kScenes, std::string_view{"game-over"}, &assets::SceneInfo::name);
    if (info == assets::kScenes.end()) {
        return std::unexpected{std::string{"no game-over scene"}};
    }
    auto data = assets::load_scene(files.archive(), *info);
    auto starts = assets::load_viking_starts(files.exe(), files.release());
    if (!data || !starts) {
        return std::unexpected{!data ? data.error() : starts.error()};
    }
    auto loaded =
        load_with(files, data->header, make_cutscene(*data, *starts), static_cast<std::uint16_t>(database_entry), 0);
    if (loaded) {
        loaded->slot = scene::kGameOver;
    }
    return loaded;
}

namespace {

// The scenes from the title on, by their name in catalog.hpp's kScenes; "" for a level.
std::string_view scene_name(int slot) {
    constexpr std::array<std::string_view, 10> kNames{"title",       "interplay",  "silicon-synapse", "galaxy",
                                                      "title",       "home-intro", "home-demo",       "starfield",
                                                      "home-ending", "ship"};
    if (slot == scene::kGameOver) {
        return "game-over";
    }
    return slot >= scene::kTitle && slot < kSceneSlots ? kNames.at(static_cast<std::size_t>(slot - scene::kTitle))
                                                       : std::string_view{};
}

// load_slot without the backdrop.
std::expected<LoadedLevel, std::string> load_slot_of(const assets::GameFiles& files, int slot, int database_entry) {
    if (slot >= 0 && slot < scene::kGameOver) {
        return load_level(files, slot + 1);
    }
    if (slot == scene::kGameOver) {
        return load_game_over(files, database_entry);
    }
    const auto* info = std::ranges::find(assets::kScenes, scene_name(slot), &assets::SceneInfo::name);
    if (info == assets::kScenes.end()) {
        return std::unexpected{std::format("no scene {}", slot)};
    }
    auto data = assets::load_scene(files.archive(), *info);
    auto starts = assets::load_viking_starts(files.exe(), files.release());
    if (!data || !starts) {
        return std::unexpected{!data ? data.error() : starts.error()};
    }
    auto loaded = load_with(files, data->header, make_cutscene(*data, *starts), database::kScenes, 0);
    if (loaded) {
        loaded->slot = slot;
    }
    return loaded;
}

}  // namespace

std::expected<LoadedLevel, std::string> load_slot(const assets::GameFiles& files, int slot, int database_entry,
                                                  const assets::SnesRom* snes) {
    auto loaded = load_slot_of(files, slot, database_entry);
    if (!loaded || snes == nullptr) {
        return loaded;
    }
    const std::string_view name = scene_name(slot);
    std::optional<std::expected<assets::Backdrop, std::string>> backdrop;
    if (slot >= 0 && slot < scene::kGameOver) {
        backdrop = assets::load_level_backdrop(*snes, static_cast<std::uint8_t>(slot + 1));
    } else if (std::ranges::contains(assets::kSnesSceneHeaders, name, &assets::SnesScene::name)) {
        backdrop = assets::load_scene_backdrop(*snes, name);
    }
    if (backdrop && !*backdrop) {
        return std::unexpected{(*backdrop).error()};
    }
    if (backdrop) {
        loaded->graphics.backdrop = Backdrop{.image = std::move((*backdrop)->image),
                                             .scroll_x = (*backdrop)->header.scroll_x,
                                             .scroll_y = (*backdrop)->header.scroll_y};
    }
    return loaded;
}

World prepare_world(const LoadedLevel& loaded, const Map* map_before, Rules rules, const Views& views, int players) {
    const Level& level = loaded.level;
    World world;
    world.set_rules(rules);
    world.player_count = std::clamp(players, 1, kMostPlayers);
    // A scene that is one picture has no map of its own: the one before it stays (and with it the camera's limits).
    world.map = level.map.cells.empty() && map_before != nullptr ? *map_before : level.map;
    world.placements = level.placements;
    world.kinds = kind_records(loaded.database);
    world.sprite_sets = loaded.sprite_sets;
    world.programs = loaded.database.bytes;
    world.places = loaded.places.empty() ? nullptr : &loaded.places;
    world.places_from = &loaded.places_from;
    world.database = loaded.database_entry;
    world.animation_tables = loaded.animation_tables;
    world.packed_entries = loaded.packed_entries;
    world.slope_heights = loaded.slope_heights;
    world.gravity = level.gravity;
    world.level_push = level.push;
    world.level = level.number;
    world.scene = level.number > 0 ? level.number - 1 : loaded.slot;
    if (rules == Rules::kRemaster && world.scene == scene::kGameOver) {
        // The letters that drop in are drawn over everything, the Vikings they land on too. (The original takes
        // their pieces from the middle pool, as the Vikings' figures, and they end up under them.)
        world.kinds.at(game_over_kind::kLetters).pool = Pool::kFront;
    }
    world.has_hud = level.hud;
    world.cleared_cell = loaded.cleared_cell;
    world.filled_cell = loaded.filled_cell;
    world.broken_picture = loaded.broken_picture;
    world.shot_picture = world.database == database::kWacky
                             ? 6
                             : (world.database == database::kSpaceship ? 12 : 0);  // each world's start
    world.switches = level.switches;
    world.more_switches = level.extra;  // the header's next word: the second set, as the level starts
    world.password = loaded.password;
    world.passwords = loaded.passwords;
    world.make = [database = loaded.database_entry](std::uint16_t kind) { return make_kind(kind, database); };
    world.texts = loaded.texts;
    world.text.direct = level.text_direct;
    world.palette = level.palette;
    for (std::size_t i = 0; i < level.palette_animations.size() && i < 8; ++i) {
        const assets::PaletteAnimation& animation = level.palette_animations[i];
        world.cycles.push_back({.delay = animation.delay,
                                .left = animation.delay,
                                .from = animation.from_color,
                                .to = animation.to_color,
                                .running = (level.palette_animations_running & (1U << i)) != 0});
    }
    world.has_vikings = level.start.size() == kVikingSlots;
    world.first_placed_slot = world.has_vikings ? kVikingSlots : 1;
    for (const Placement& start : level.start) {
        Entity* entity = world.create(
            {.kind = start.kind, .x = start.x, .y = start.y, .flags = start.flags, .argument = start.argument});
        if (entity != nullptr && world.has_vikings) {
            entity->health = kFullHealth;
        }
    }
    // What plays stops, and the level's music starts (or goes on, by its music action).
    world.cues.push_back(SoundCue{.kind = SoundCue::Kind::kStopAll});
    world.cues.push_back(
        SoundCue{.kind = SoundCue::Kind::kMusicAction, .value = level.music.at_start, .tune = level.music.tune});
    world.choose_vikings();
    for (Player& player : world.playing()) {  // each view on its player's Viking
        set_camera_limits(player.camera, level.width, level.height);
        const Entity* viking = world.at(player.viking);
        if (const Entity* first = viking != nullptr ? viking : world.entities[0].get()) {
            start_camera(player.camera, first->x, first->y);
        }
        player.strip_x = cell_of(player.camera.x);
        player.strip_y = cell_of(player.camera.y);
    }
    world.create_always_active();
    world.create_in_views();
    for (int who = 0; who < world.player_count; ++who) {  // (Remaster rules: as zoomed out as each player has it)
        const Camera& camera = world.players.at(static_cast<std::size_t>(who)).camera;
        if (const auto [width, height] = world.view_for(who, views.at(static_cast<std::size_t>(who)));
            width != camera.view_width || height != camera.view_height) {
            world.set_view(who, width, height);
        }
    }
    return world;
}

World start_world(const LoadedLevel& loaded, Rules rules) {
    World world = prepare_world(loaded, nullptr, rules);
    for (int part = 0; part < 3; ++part) {
        world.run(part, true);
    }
    count_picture(world.pieces);  // the start draws a fourth time
    return world;
}

namespace {

constexpr int kGiveUpText = 2;  // the pause menu's question, and two empty lines
constexpr int kQuitText = 3;    // the quit box's question, and two empty lines
constexpr int kNoText = 4;
constexpr int kYesText = 5;
constexpr int kBlankText = 6;  // three spaces
constexpr int kMenuRow = 15;
constexpr int kYesColumn = 16;
constexpr int kNoColumn = 22;
constexpr int kNoBlankColumn = 21;

}  // namespace

bool Play::restore(const Snapshot& from) {
    const bool another = from.loaded != loaded_;
    world = from.world;
    state_ = from.state;
    loaded_ = from.loaded;
    owned_ = from.owner;
    cleared_.reset();
    fade.reset();
    fading_out_.reset();
    return another;
}

std::uint64_t Play::state_hash(std::vector<std::uint8_t>& buffer) const {
    buffer.clear();
    Writer to{buffer};
    world.save(to);
    to(state_);
    return assets::fnv1a64(std::as_bytes(std::span{buffer}));
}

void Play::take(const StepInput& input) {
    menu_keys = input.menu;
    if (input.typed != 0) {
        world.typed = input.typed;
    }
    for (int who = 0; who < world.player_count; ++who) {
        const PlayerInput& given = input.players.at(static_cast<std::size_t>(who));
        if (given.zoom[0] != 0 && given.zoom[1] != 0) {
            state_.views.at(static_cast<std::size_t>(who)) = {given.zoom[0], given.zoom[1]};
        }
        if (given.view[0] == 0 || given.view[1] == 0) {
            continue;
        }
        const Camera& camera = world.players.at(static_cast<std::size_t>(who)).camera;
        if (const auto [width, height] = world.view_for(who, {given.view[0], given.view[1]});
            width != camera.view_width || height != camera.view_height) {
            world.set_view(who, width, height);
        }
    }
}

void Play::zoom_back(int player, std::array<int, 2> zoom) {
    state_.views.at(static_cast<std::size_t>(player)) = zoom;
    const Camera& camera = world.players.at(static_cast<std::size_t>(player)).camera;
    if (const auto [width, height] = world.view_for(player, zoom);
        width != camera.view_width || height != camera.view_height) {
        world.set_view(player, width, height);
    }
}

void Play::scene_sound() {
    const Level& level = loaded_->level;
    world.cues.push_back(SoundCue{.kind = SoundCue::Kind::kStopAll});
    world.cues.push_back(
        SoundCue{.kind = SoundCue::Kind::kMusicAction, .value = level.music.at_start, .tune = level.music.tune});
    scene_loaded = true;
}

bool Play::pause_wanted() const noexcept {
    return world.has_vikings && !leaving() && (world.host().pressed & key::kEnter) != 0 && world.lines.empty() &&
           !world.host().inventory.open;
}

void Play::menu_text(int number, int column) {
    if (const auto line = text_at(world.texts, number, column, kMenuRow)) {
        world.text.show(*line, world.texts);
    }
}

void Play::open_menu(bool quit_box) {
    // The picture a little darker, unless something darkens it already; then the text is black, else white.
    state_.menu_tinted = world.tint == std::array<std::uint8_t, 3>{};
    if (state_.menu_tinted) {
        world.tint = kMenuTint;
    }
    world.text.colour3 = state_.menu_tinted ? assets::Rgb{} : assets::Rgb{.r = 255, .g = 255, .b = 255};
    world.sound(effect::kMenuOpens);
    if (const auto box = box_at(world.texts, quit_box ? kQuitText : kGiveUpText, tail::kNone, quit_box ? 13 : 15, 12)) {
        world.text.show(*box, world.texts);
    }
    menu_text(kYesText, kYesColumn);
    if (quit_box) {  // the level's password where "no" would be
        for (std::size_t i = 0; i < world.password.size(); ++i) {
            world.text.show({.kind = Line::Kind::kCharacter,
                             .column = static_cast<std::uint16_t>(18 + i),
                             .row = 17,
                             .value = world.password.at(i)},
                            world.texts);
        }
    }
    state_.menu_blink = kBlinkStart;
    state_.menu_choice = 1;
    state_.menu = quit_box ? Menu::kQuitBox : Menu::kPause;
}

bool Play::menu_pass(const StepInput& input, const std::function<void()>& picture, int& refreshes) {
    refreshes += 3;
    world.read_keys(with_demo(input));
    const Player& host = world.host();
    --state_.menu_blink;
    if ((state_.menu_blink & 0x0fU) == 0) {  // the chosen word goes, or is back
        const bool no = state_.menu_choice != 0;
        if ((state_.menu_blink & 0x10U) != 0) {
            menu_text(kBlankText, no ? kNoBlankColumn : kYesColumn);
        } else {
            menu_text(no ? kNoText : kYesText, no ? kNoColumn : kYesColumn);
        }
        picture();
    }
    bool moved = false;  // the word left behind is shown again
    if ((host.pressed & key::kLeft) != 0 && state_.menu_choice != 0) {
        state_.menu_choice = 0;
        state_.menu_blink = kBlinkStart;
        menu_text(kNoText, kNoColumn);
        moved = true;
    }
    if ((host.pressed & key::kRight) != 0 && state_.menu_choice == 0) {
        state_.menu_choice = 1;
        state_.menu_blink = kBlinkStart;
        menu_text(kYesText, kYesColumn);
        moved = true;
    }
    std::optional<std::uint16_t> choice;
    if ((host.pressed & key::kF) != 0) {
        choice = state_.menu_choice;
    } else if ((host.pressed & key::kEnter) != 0 || (!menu_keys.yes && menu_keys.no)) {
        choice = 1;
    } else if (menu_keys.yes) {
        choice = 0;
    }
    if (!choice) {
        if (moved) {
            picture();
        }
        return false;
    }
    world.read_keys(with_demo(input));
    for (int i = 0; i < 2; ++i) {  // the playfield drawn again, twice
        count_picture(world.pieces);
        picture();
        ++refreshes;
    }
    const bool quit_box = state_.menu == Menu::kQuitBox;
    if (*choice == 0) {
        world.game_over = true;
    }
    world.text.show({.kind = Line::Kind::kClear}, world.texts);
    state_.menu = Menu::kNone;
    if (state_.menu_tinted && !(quit_box && world.game_over)) {  // quitting, the picture stays dark
        world.tint = {};
    }
    if (quit_box) {
        picture();
    }
    return true;
}

int Play::change_scene(const std::function<void()>& picture) {
    constexpr std::uint8_t kFadeSteps = 70;
    scene_loaded = true;
    const Level& leaving_level = loaded_->level;
    // What is left of the scene: its music's end, every entity's turn as when it leaves the active area, and
    // the picture going dark, a refresh a step.
    // The health each Viking leaves with: the game-over scene shows who still stands.
    std::array<std::uint16_t, kVikingSlots> health_left{};
    for (int viking = 0; viking < kVikingSlots; ++viking) {
        const Entity* entity = world.at(viking);
        health_left.at(static_cast<std::size_t>(viking)) =
            entity == nullptr || entity->dead ? std::uint16_t{0} : entity->health;
    }
    world.cues.push_back(SoundCue{
        .kind = SoundCue::Kind::kMusicAction, .value = leaving_level.music.at_end, .tune = leaving_level.music.tune});
    // The fade shows the scene as it was before the pass below emptied it: the original's fade (0FA0) only darkens
    // the palette and flips its two pages, which still hold the last pictures drawn (seen in DOSBox: everyone stays
    // there while it goes dark).
    fading_out_ = world;
    world.ending = true;
    for (int slot = 0; slot < world.slots(); ++slot) {
        if (Entity* entity = world.at(slot)) {
            world.updating = slot;
            entity->outside_active_area(world);
            world.forget_removed();
        }
    }
    world.updating = kNobody;
    world.ending = false;
    for (std::uint8_t step = 0; step < kFadeSteps; ++step) {
        fade = step;
        picture();
    }
    fade = kFadeSteps;
    fading_out_.reset();

    // The next scene: the game-over scene, the level again or the logos after it, or what the scene itself says.
    // Asked for both in the same tick, the scene's own choice wins: the last Viking dying at the exit starts the
    // level again, as an exit reached with one gone does.
    const bool to_game_over = world.game_over && !world.leave;
    int slot = to_game_over ? scene::kGameOver : world.next_scene;
    if (world.game_over_scene.try_again) {
        slot = world.level - 1;
    } else if (world.game_over_scene.give_up) {
        slot = scene::kLogo;
    }
    state_.demo_wanted = state_.demo_wanted || world.title.demo_wanted;
    std::expected<LoadedLevel, std::string> next =
        std::unexpected{std::string{"the scene after this one can't be loaded without the game's files"}};
    if (files_ != nullptr) {
        start_demo(slot);
        next = load_slot(*files_, slot, loaded_->database_entry, snes);
    }
    if (!next) {
        ended = true;
        error = next.error();
        fade.reset();
        return kFadeSteps;
    }
    auto loaded = std::make_unique<LoadedLevel>(std::move(*next));
    World started = prepare_world(*loaded, &world.map, world.rules, state_.views, world.player_count);
    started.cues.front().value = leaving_level.music.at_end == 1 ? 1 : 0;  // what plays stops, or the music stays
    // What goes on from scene to scene.
    started.seed = world.seed;
    started.demo_seed = world.demo_seed;
    started.typed = world.typed;
    started.retries = world.retries;
    started.scene = slot;
    started.previous_scene = world.scene;
    started.next_scene = loaded->level.next_slot;
    started.attract = state_.playback;
    if (state_.playback) {
        started.demo_seed = 30;
    }
    started.title.cheat = world.title.cheat;
    started.online = world.online;
    started.text.draws = world.text.draws;                    // the count of text draws isn't a scene's
    started.last_nearest_viking = world.last_nearest_viking;  // nor is who was last found nearest (ds:03CA)
    for (std::size_t i = 0; i < started.players.size(); ++i) {
        // A key held over the change isn't newly pressed after it (ds:03BA).
        started.players.at(i).keys_before = world.players.at(i).keys_before;
    }
    // A quirk of the original: a scene sets up only its own colour cycles; those the scene before had more of
    // are still there, and run if the new scene's bits say so.
    for (std::size_t i = started.cycles.size(); i < world.cycles.size(); ++i) {
        auto cycle = world.cycles[i];
        cycle.running = (loaded->level.palette_animations_running & (1U << i)) != 0;
        started.cycles.push_back(cycle);
    }
    if (slot >= scene::kGameOver) {  // outside the levels the last password stays
        started.password = world.password;
    }
    if (slot == scene::kGameOver) {  // by the game being over, or by a level's end without all three
        started.level = world.level;
        started.game_over_scene.kept_health = health_left;
    }
    started.after_game_over = world.scene == scene::kGameOver;
    started.cues.insert(started.cues.begin(), world.cues.begin(), world.cues.end());
    world = std::move(started);
    ++scenes_started;
    owned_ = std::move(loaded);
    loaded_ = owned_.get();
    cleared_.reset();

    // Its first tick, in the dark, with the fourth picture a start draws; then it fades in.
    for (int part = 0; part < 3; ++part) {
        world.run(part, true);
        world.text.draw();
        picture();
    }
    count_picture(world.pieces);
    picture();
    for (int step = kFadeSteps; step >= 0; --step) {
        fade = static_cast<std::uint8_t>(step);
        picture();
    }
    fade.reset();
    return kFadeSteps + 4 + kFadeSteps + 1;
}

View view_of(const World& world, const LoadedLevel& loaded, PiecePainter& painter, std::vector<PiecePicture>& pictures,
             int player) {
    const Player& whose = world.players.at(static_cast<std::size_t>(player));
    const Camera& camera = whose.camera;
    View view;
    view.camera_x = shaken(camera.x, world.shake_x, camera.max_x);
    view.camera_y = shaken(camera.y, world.shake_y, camera.max_y);
    view.width = camera.view_width;
    view.height = camera.view_height;
    // The text layer scrolls with the camera's last three bits, and shakes with the view.
    view.text_x = -static_cast<int>(static_cast<std::uint16_t>(camera.x) & 7U) - (view.camera_x - camera.x);
    view.text_y = -static_cast<int>(static_cast<std::uint16_t>(camera.y) & 7U) - (view.camera_y - camera.y);
    view.map = loaded.level.map.cells.empty() ? nullptr : &world.map;
    for (const auto& entity : world.entities) {
        if (entity) {
            painter.unpack(world.pieces, entity->sprite, loaded.sprites);
        }
    }
    pictures = painter.pictures(world.pieces, loaded.sprites);
    // How far each piece's entity moves in the tick: what shift_sprites moves its pieces by, a share a picture.
    std::vector<std::array<int, 2>> moves(world.pieces.size());
    for (const auto& entity : world.entities) {
        if (!entity || entity->sprite.count == 0 || moves.empty()) {
            continue;
        }
        for (int piece = entity->sprite.first; piece < entity->sprite.first + entity->sprite.count; ++piece) {
            moves.at(static_cast<std::size_t>(piece) % moves.size()) = {wrapped(entity->x - entity->previous_x),
                                                                        wrapped(entity->y - entity->previous_y)};
        }
    }
    for (const PiecePicture& picture : pictures) {
        view.sprites.push_back(
            DrawnSprite{.image = &picture.image,
                        .x = picture.x,
                        .y = picture.y,
                        .mirrored = picture.mirrored,
                        .flipped = picture.flipped,
                        .moves = picture.piece < moves.size() ? moves[picture.piece] : std::array<int, 2>{}});
    }
    if (world.online) {
        compose_text(view, world, player, camera);
    } else {
        view.text = world.text.seen();
        view.colour3 = world.text.colour3;
    }
    view.palette = world.palette;
    for (std::size_t i = 0; i < view.darken.size(); ++i) {
        view.darken.at(i) = world.tint.at(i) | world.tint2.at(i);
    }
    if (loaded.level.hud) {
        HudView hud;
        for (int who = 0; who < kVikingSlots; ++who) {
            const Entity* viking = world.at(who);
            // The picker is over each Viking's chosen square, whether he is there or not.
            hud.picker.at((static_cast<std::size_t>(who) * World::kSquares) +
                          static_cast<std::size_t>(world.chosen_square.at(static_cast<std::size_t>(who)))) = true;
            if (viking == nullptr) {
                continue;
            }
            hud.vikings.at(static_cast<std::size_t>(who)) = HudView::Viking{
                .shown = true,
                .portrait = viking->dead
                                ? HudView::Portrait::kDead
                                : (who == whose.viking ? HudView::Portrait::kChosen : HudView::Portrait::kWaiting),
                // With the shield item's point the second set of health pictures is shown.
                .health = (viking->health + (world.armour.at(static_cast<std::size_t>(who)) != 0 ? 4 : 0)) / 2};
        }
        for (std::size_t square = 0; square < 12; ++square) {
            hud.icons.at(square) = world.shown_items.at(square);
        }
        if (const Inventory& inventory = whose.inventory; inventory.open) {
            // The cursor's square, as the blinking last left it. Choosing: its item, with or without the picker.
            // Carrying: the carried item or nothing; the pickers stay on the Vikings' chosen squares.
            constexpr int kTrashCanIcon = 23;
            const auto cursor = static_cast<std::size_t>(inventory.cursor);
            if (inventory.choosing) {
                hud.icons.at(cursor) = inventory.item == 0 && inventory.cursor == Inventory::kTrashCan
                                           ? kTrashCanIcon
                                           : int{inventory.item};
                hud.picker.at(cursor) = inventory.lit;
            } else {
                hud.icons.at(cursor) = inventory.lit ? int{inventory.item} : 0;
                bool chosen = false;
                for (int who = 0; who < kVikingSlots; ++who) {
                    chosen =
                        chosen || (who * World::kSquares) + world.chosen_square.at(static_cast<std::size_t>(who)) ==
                                      inventory.cursor;
                }
                hud.picker.at(cursor) = chosen;
            }
        }
        view.hud = hud;
    }
    return view;
}

const SceneGraphics& Play::graphics() {
    constexpr std::size_t kWidth = 320;
    constexpr std::size_t kHeight = 176;
    constexpr std::size_t kRow = 30;
    if (!world.title.screen_cleared) {
        return loaded_->graphics;
    }
    if (!cleared_) {
        cleared_ = loaded_->graphics;
        assets::IndexedImage screen{
            .width = kWidth, .height = kHeight, .pixels = std::vector<std::uint8_t>(kWidth * kHeight, 0)};
        if (const auto& lower = loaded_->graphics.hud_picture) {
            for (std::size_t y = 0; y < lower->height && kRow + y < kHeight; ++y) {
                for (std::size_t x = 0; x < std::min(kWidth, lower->width); ++x) {
                    screen.set(x, kRow + y, lower->at(x, y));
                }
            }
        }
        cleared_->id = next_graphics_id();
        cleared_->picture = std::move(screen);
        cleared_->hud_picture.reset();
    }
    return *cleared_;
}

void Play::start_demo(int& slot) {
    // The levels the title shows when it is left alone, in turn, and their recordings.
    constexpr std::array<int, 6> kDemoLevels{2, 9, 8, 15, 23, 25};
    constexpr std::array<std::uint16_t, 6> kDemoRecordings{418, 419, 420, 422, 421, 448};
    constexpr std::uint16_t kIntroRecording = 443;
    constexpr std::uint16_t kEmptyRecording = 444;
    state_.demo_count = state_.demo_word = state_.demo_keys = 0;
    const bool wanted = state_.demo_wanted;
    state_.demo_wanted = false;
    state_.playback = slot == scene::kStarfield || slot == scene::kEnding || slot == scene::kShip || wanted;
    if (!state_.playback || files_ == nullptr) {
        state_.playback = false;
        return;
    }
    std::uint16_t entry = kEmptyRecording;
    if (slot == scene::kIntro) {
        entry = kIntroRecording;
    } else if (slot != scene::kVillageDemo && slot != scene::kStarfield && slot != scene::kEnding &&
               slot != scene::kShip) {
        slot = kDemoLevels.at(static_cast<std::size_t>(state_.demo_index));
        entry = kDemoRecordings.at(static_cast<std::size_t>(state_.demo_index));
    }
    state_.recording.clear();
    state_.demo_at = 0;
    if (const auto bytes = assets::load_entry(files_->archive(), entry)) {
        for (std::size_t i = 0; i + 1 < bytes->size(); i += 2) {
            state_.recording.push_back(static_cast<std::uint16_t>(std::to_integer<unsigned>((*bytes)[i]) |
                                                                  (std::to_integer<unsigned>((*bytes)[i + 1]) << 8U)));
        }
    }
}

void Play::play_demo() {
    constexpr std::uint16_t kEnds = key::kEnter;  // the pause key, and a recording's last word
    if (!state_.playback) {
        return;
    }
    const bool ended_now = (world.host().held & kEnds) != 0;
    if (ended_now) {
        state_.demo_count = state_.demo_word = 0xffff;
    }
    if (state_.demo_count != 0) {
        --state_.demo_count;
        state_.demo_keys |= state_.demo_word;
    } else {
        const std::uint16_t word =
            state_.demo_at < state_.recording.size() ? state_.recording.at(state_.demo_at) : kEnds;
        const std::uint16_t ticks = state_.demo_at + 1 < state_.recording.size()
                                        ? state_.recording.at(state_.demo_at + 1)
                                        : std::uint16_t{0xffff};
        state_.demo_keys = state_.demo_word = word;
        state_.demo_count = static_cast<std::uint16_t>(ticks - 1U);
        state_.demo_at += 2;
    }
    if (!ended_now) {
        return;
    }
    // What follows the scene that was played.
    switch (world.scene) {
        case scene::kIntro:  // its recording's end: the village's demo; a key: level 1
            if (world.host().held != 0xffff) {
                state_.demo_wanted = true;
            } else {
                world.next_scene = 0;
                world.host().viking = 0;
            }
            break;
        case scene::kVillageDemo:
            world.host().viking = 0;
            break;
        case scene::kStarfield:
            state_.demo_wanted = true;
            break;
        case scene::kEnding:
            world.next_scene = scene::kLogo;
            break;
        default:  // one of the title's demos: the next one the next time, and the logos again
            state_.demo_index = (state_.demo_index + 1) % 6;
            world.next_scene = scene::kLogo;
            break;
    }
    state_.playback = false;
    world.leave = true;
}

}  // namespace lvr
