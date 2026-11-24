#include "lvr/world.hpp"

#include <algorithm>
#include <utility>

#include "lvr/movement.hpp"
#include "lvr/terrain.hpp"

namespace lvr {

namespace {

// A kind whose behaviour isn't written yet.
class Unwritten : public Entity {
  public:
    void update(World& /*world*/) override {}
};

constexpr int kMargin = 16;  // the active area: the view and this much around it

}  // namespace

std::array<KindRecord, kKinds> kind_records(const assets::ObjectDatabase& database) {
    std::array<KindRecord, kKinds> kinds{};
    for (std::size_t kind = 0; kind < kinds.size() && kind < database.records.size(); ++kind) {
        const assets::ObjectRecord& record = database.records[kind];
        const std::uint8_t pieces = record.bytes[2];
        const std::uint16_t sprites = record.word(0);
        // Two bits choose the pool: packed sprites the middle one; the count's top bit, with or without that, the
        // front one.
        const Pool pool = (pieces & 0x80U) != 0 ? Pool::kFront
                                                : (sprites == KindRecord::kPackedSprites ? Pool::kMiddle : Pool::kBack);
        kinds.at(kind) = KindRecord{.sprite_entry = sprites,
                                    .pieces = static_cast<int>(pieces & 0x7fU),
                                    .pool = pool,
                                    .health = record.word(7),
                                    .width = record.bytes[9],
                                    .height = record.bytes[10],
                                    .kind_value = record.word(0x0B),
                                    .hit_class = record.word(0x0D),
                                    .hit_mask = record.word(0x0F),
                                    .max_step_x = wrapped(record.word(0x11)),
                                    .max_step_y = wrapped(record.word(0x13))};
    }
    return kinds;
}

Entity* World::create(const Creation& creation) {
    if (creation.placement >= 0 && std::cmp_less(creation.placement, gone.size()) &&
        gone.test(static_cast<std::size_t>(creation.placement))) {
        return nullptr;
    }
    int slot = 0;
    while (entities.at(static_cast<std::size_t>(slot))) {
        if (++slot == slots()) {
            return nullptr;
        }
    }
    const KindRecord& record = kinds.at(creation.kind % kKinds);
    std::uint16_t base = 0;
    if (record.sprite_entry != KindRecord::kNoSprites && record.sprite_entry != KindRecord::kPackedSprites) {
        const auto loaded = std::ranges::find(sprite_sets, record.sprite_entry, &LoadedSpriteSet::entry);
        if (loaded == sprite_sets.end()) {
            return nullptr;  // the level doesn't load this kind's sprites
        }
        base = loaded->base;
    }
    std::unique_ptr<Entity> made = make ? make(creation.kind) : nullptr;
    if (!made) {
        made = make_entity<Unwritten>();
    }
    Entity& entity = *made;
    entity.slot = slot;
    entity.kind = creation.kind;
    entity.placement = creation.placement;
    entity.flags = creation.flags;
    entity.argument = creation.argument;
    entity.health = record.health;
    entity.kind_value = record.kind_value;
    entity.hit_class = record.hit_class;
    entity.hit_mask = record.hit_mask;
    entity.max_step_x = record.max_step_x;
    entity.max_step_y = record.max_step_y;
    entity.creator = creation.creator;
    entity.x = entity.previous_x = wrapped(creation.x);
    entity.y = entity.previous_y = wrapped(creation.y);
    entity.width = record.width;
    entity.height = record.height;
    entity.box.left = wrapped(entity.x - (record.width / 2));
    entity.box.right = wrapped(entity.box.left + record.width - 1);
    entity.box.top = wrapped(entity.y - (record.height / 2));
    entity.box.bottom = wrapped(entity.box.top + record.height - 1);
    const bool own_size = creation.half_width < 0;
    entity.half_width = wrapped(own_size ? record.width / 2 : creation.half_width);
    entity.half_height = wrapped(own_size ? record.height / 2 : creation.half_height);
    entity.sprite = Sprite{.count = record.pieces, .base = base};
    if (record.pieces != 0 && !take_pieces(pieces, entity.sprite, record.pool,
                                           PieceStart{.colour = static_cast<std::uint8_t>(
                                                          (unsigned{creation.flags} & entity_flag::kColour) >> 1U),
                                                      .mirrored = entity.facing_left(),
                                                      .flipped = entity.upside_down(),
                                                      .large = (creation.flags & entity_flag::kLargePieces) != 0})) {
        return nullptr;
    }
    entities.at(static_cast<std::size_t>(slot)) = std::move(made);
    return &entity;
}

void World::remove(int slot) {
    if (!entities.at(static_cast<std::size_t>(slot))) {
        return;
    }
    removed_.push_back(std::move(entities.at(static_cast<std::size_t>(slot))));
    const Entity* entity = removed_.back().get();
    if (entity->sprite.count != 0) {
        let_go(pieces, entity->sprite);
    }
    if (Entity* creator = at(entity->creator)) {
        creator->last_created = kNobody;
    }
    if (Entity* created = at(entity->last_created)) {
        created->creator = kNobody;
    }
    if (!ending && entity->placement >= 0 && entity->has_flag(entity_flag::kCreatedAgainAtOnce)) {
        const Entity* again =
            create_if_in(entity->placement, View{.left = wrapped(camera.x - kMargin),
                                                 .right = wrapped(camera.x + camera.view_width + kMargin),
                                                 .top = wrapped(camera.y - kMargin),
                                                 .bottom = wrapped(camera.y + camera.view_height + kMargin)});
        // Created in the slot of the one whose turn it is (it removed itself), or below it: the tick's turns have
        // passed it by, and it has its first one after the tick's moves (where the others look at what they ran
        // into): it is there and shows, but doesn't move yet.
        if (again != nullptr && updating != kNobody && again->slot <= updating) {
            created_late.push_back(again->slot);
        }
    }
}

Entity* World::create_placed(int index) {
    const Placement& placement = placements.at(static_cast<std::size_t>(index));
    return create(Creation{.kind = placement.kind,
                           .x = placement.x,
                           .y = placement.y,
                           .flags = placement.flags,
                           .argument = placement.argument,
                           .placement = index,
                           .half_width = placement.width / 2,
                           .half_height = placement.height / 2});
}

void World::create_always_active() {
    for (std::size_t index = 0; index < placements.size(); ++index) {
        if (placements[index].always_active()) {
            (void)create_placed(static_cast<int>(index));
        }
    }
}

Entity* World::create_if_in(int index, const View& view) {
    if (index < 0 || std::cmp_greater_equal(index, placements.size())) {
        return nullptr;
    }
    const Placement& placement = placements[static_cast<std::size_t>(index)];
    const int half_width = placement.width / 2;
    const int half_height = placement.height / 2;
    if (wrapped(placement.x + half_width) < view.left || wrapped(placement.x - half_width) >= view.right ||
        wrapped(placement.y + half_height) < view.top || wrapped(placement.y - half_height) >= view.bottom) {
        return nullptr;
    }
    for (int slot = first_placed_slot; slot < slots(); ++slot) {
        const Entity* entity = at(slot);
        if (entity != nullptr && entity->placement == index) {
            return nullptr;  // it is there already
        }
    }
    return create_placed(index);
}

void World::create_in_view(ViewPart part) {
    View view{.left = wrapped(camera.x - kMargin),
              .right = wrapped(camera.x + camera.view_width + kMargin),
              .top = wrapped(camera.y - kMargin),
              .bottom = wrapped(camera.y + camera.view_height + kMargin)};
    // A strip: the 32 pixels at that edge of the active area.
    switch (part) {
        case ViewPart::kWhole:
            break;
        case ViewPart::kLeft:
            view.right = wrapped(camera.x + kMargin);
            break;
        case ViewPart::kRight:
            view.left = wrapped(camera.x + camera.view_width - kMargin);
            break;
        case ViewPart::kBottom:
            view.top = wrapped(camera.y + camera.view_height - kMargin);
            break;
        case ViewPart::kTop:
            view.bottom = wrapped(camera.y + kMargin);
            break;
    }
    for (int* edge : {&view.left, &view.right, &view.top, &view.bottom}) {
        *edge = std::max(*edge, 0);
    }
    for (std::size_t index = 0; index < placements.size(); ++index) {
        (void)create_if_in(static_cast<int>(index), view);
    }
}

void World::save(Writer& to) const {
    to.measured([this](Writer& part) { part(static_cast<const WorldState&>(*this)); });
    to(static_cast<std::uint32_t>(entities.size()));
    for (const OwnedEntity& entity : entities) {
        to(static_cast<bool>(entity));
        if (entity) {
            to(entity->kind);
            to.measured([&entity](Writer& part) { entity->save(part); });
        }
    }
}

bool World::load(Reader& from) {
    from.measured([this](Reader& part) { part(static_cast<WorldState&>(*this)); });
    std::uint32_t count = 0;
    from(count);
    if (!from.ok() || count > kRemasterSlots) {
        return false;
    }
    entities.clear();
    entities.resize(count);
    removed_.clear();
    for (OwnedEntity& slot : entities) {
        bool there = false;
        from(there);
        if (!there) {
            continue;
        }
        std::uint16_t kind = 0;
        from(kind);
        std::unique_ptr<Entity> made = make ? make(kind) : nullptr;
        if (!made) {
            made = make_entity<Unwritten>();
        }
        from.measured([&made](Reader& part) { made->load(part); });
        slot = std::move(made);
        if (!from.ok()) {
            return false;
        }
    }
    return from.ok();
}

Camera World::camera_for_view(int width, int height) const noexcept {
    Camera placed = camera;
    const bool larger = width > camera.view_width || height > camera.view_height;
    placed.view_width = width;
    placed.view_height = height;
    placed.covered = height > kViewHeight ? kHudRows : 0;  // a larger view has the HUD over its bottom
    set_camera_limits(placed, map.width, map.height);
    const Entity* viking = current_viking != kNobody ? at(current_viking) : nullptr;
    if (larger || viking == nullptr) {
        start_camera(placed, camera.x + (camera.view_width / 2),
                     camera.y + ((camera.view_height - camera.covered) / 2));
    } else {
        start_camera(placed, viking->x, viking->y);
    }
    return placed;
}

void World::set_view(int width, int height) {
    constexpr int kTextCell = 8;
    const Camera before = camera;
    camera = camera_for_view(width, height);
    // The text stays where it is in the level, as nearly as its cells allow.
    text.resize(width / kTextCell, height / kTextCell, (before.x / kTextCell) - (camera.x / kTextCell),
                (before.y / kTextCell) - (camera.y / kTextCell));
    strip_x = cell_of(camera.x);
    strip_y = cell_of(camera.y);
    create_in_view(ViewPart::kWhole);
}

void World::create_scrolled_in() {
    const int across = cell_of(camera.x);
    if (across != strip_x) {
        const bool left = wrapped(across) < wrapped(strip_x);
        strip_x = across;
        create_in_view(left ? ViewPart::kLeft : ViewPart::kRight);
    }
    const int down = cell_of(camera.y);
    if (down != strip_y) {
        const bool up = wrapped(down) < wrapped(strip_y);
        strip_y = down;
        create_in_view(up ? ViewPart::kTop : ViewPart::kBottom);
    }
}

// --- The tick ---

void Entity::outside_active_area(World& world) {
    world.remove(slot);
}

Entity* World::create_by(Entity& creator, std::uint16_t kind, int x, int y, std::uint16_t argument,
                         std::uint16_t flags) {
    constexpr std::uint16_t kOwnBits = 0x00fe;  // facing, colours ... as its creator
    constexpr std::uint16_t kAskedBits = entity_flag::kAlwaysActive | entity_flag::kLargePieces;
    Entity* created = create(Creation{
        .kind = kind,
        .x = x,
        .y = y,
        .flags = static_cast<std::uint16_t>((unsigned{creator.flags} & kOwnBits) | (unsigned{flags} & kAskedBits)),
        .argument = argument,
        .creator = creator.slot});
    if (created != nullptr) {
        creator.last_created = created->slot;
        if (created->slot < creator.slot) {
            created_below.push_back(created->slot);
        }
    }
    return created;
}

void World::play(Entity& entity, Animation animation) const {
    std::uint16_t program = animation.place;
    const int home = animation.home;
    const AnimationPlaces* paired = places;
    if (home != database::kSpaceship) {
        const auto index = static_cast<std::size_t>(home - database::kSpaceship);
        paired = home == database || places_from == nullptr || index >= kWorlds ? nullptr : &places_from->at(index);
    }
    if (paired != nullptr) {
        if (const auto here = paired->find(program); here != paired->end()) {
            program = here->second;
        }
    }
    start_animation(entity.sprite, program);
}

void World::animate(Entity& entity) {
    Body body{.x = entity.x,
              .y = entity.y,
              .facing_left = entity.facing_left(),
              .push_x = entity.push_x,
              .push_y = entity.push_y,
              .flipped = entity.upside_down()};
    std::vector<std::uint8_t> effects;
    std::vector<std::uint16_t> unloaded;
    const std::uint16_t program = entity.sprite.program;
    if (!step_animation(pieces, entity.sprite, body,
                        AnimationContext{.programs = programs,
                                         .tables = &animation_tables,
                                         .packed_entries = packed_entries,
                                         .unloaded = &unloaded},
                        effects)) {
        note_missing({.kind = entity.kind, .what = program, .program = true});
    }
    for (const std::uint16_t entry : unloaded) {
        note_missing({.kind = entity.kind, .what = entry, .program = false});
    }
    for (const std::uint8_t effect : effects) {
        cues.push_back(SoundCue{.kind = SoundCue::Kind::kEffect, .value = effect, .tune = 0, .entity = entity.slot});
    }
    entity.push_x = body.push_x;
    entity.push_y = body.push_y;
    add_speed(entity, gravity, level_push);
}

void World::note_missing(const Missing& what) {
    if (std::ranges::find(missing, what) == missing.end()) {  // each once: a long game doesn't fill it
        missing.push_back(what);
    }
}

void World::hide_pieces(const Entity& entity) {
    for (int index = entity.sprite.first; index < entity.sprite.first + entity.sprite.count; ++index) {
        Piece& piece = piece_at(index);
        piece.hidden = true;
        piece.erase |= 2U;
    }
}

void World::show_pieces(const Entity& entity) {
    for (int index = entity.sprite.first; index < entity.sprite.first + entity.sprite.count; ++index) {
        Piece& piece = piece_at(index);
        piece.hidden = false;
        piece.blinked_off = false;
        piece.redraw = 2;
        piece.erase = 0;
    }
}

void World::read_keys(std::uint16_t keys) noexcept {
    pressed = static_cast<std::uint16_t>(keys & ~unsigned{keys_before});
    held = keys_before = keys;
}

bool World::well_in_view(const Entity& entity) const noexcept {
    constexpr int kInset = 31;  // this far inside the view on every side (258 across and 114 down in the original's)
    const int left = wrapped(camera.x + kInset);
    const int top = wrapped(camera.y + kInset);
    return left < entity.x && wrapped(left + camera.view_width - (2 * kInset)) >= entity.x && top < entity.y &&
           wrapped(top + camera.view_height - (2 * kInset)) >= entity.y;
}

bool World::can_be_spoken_to(const Entity& entity) const noexcept {
    constexpr int kAtRest = 4;
    return entity.slot == current_viking && static_cast<std::uint16_t>(camera.moved_x) < kAtRest &&
           static_cast<std::uint16_t>(camera.moved_y) < kAtRest && well_in_view(entity);
}

void World::say_at_point(int number, int anchor, int x, int y) {
    if (const auto line = box_at_point(texts, number, anchor, x, y, camera.x, camera.y, text.columns, text.rows)) {
        lines.push_back(*line);
    }
}

void World::say_at_cell(int number, int anchor, int column, int row) {
    if (const auto line = box_at(texts, number, anchor, column, row)) {
        lines.push_back(*line);
    }
}

void World::speak_at_point(std::uint16_t colour, int number, int anchor, int x, int y) {
    colour_boxes(colour);
    say_at_point(number, anchor, x, y);
    wait_for_key();
    clear_text();
}

void World::speak_at_cell(std::uint16_t colour, int number, int anchor, int column, int row) {
    colour_boxes(colour);
    say_at_cell(number, anchor, column, row);
    wait_for_key();
    clear_text();
}

bool World::show_lines(std::uint16_t keys) {
    while (next_line < lines.size()) {
        // The Viking being played is shown, whatever its blink was at.
        if (current_viking != kNobody) {
            if (const Entity* viking = at(current_viking)) {
                Piece& piece = piece_at(viking->sprite.first);
                piece.blinked_off = false;
                piece.redraw = 2;
            }
        }
        const Line line = lines[next_line++];
        text.show(line, texts);
        if (line.kind == Line::Kind::kClear) {  // the playfield is drawn again, twice
            count_picture(pieces);
            count_picture(pieces);
        }
        read_keys(keys);
        if (line.kind == Line::Kind::kWaitForKey) {
            // The wait's first pass, at once: no key can be new in it.
            read_keys(keys);
            turn_colours();
            waiting_for_key = true;
            return true;
        }
    }
    lines.clear();
    next_line = 0;
    return false;
}

void World::step(int part, std::uint16_t keys) {
    if (part == 0) {
        read_keys(keys);
    }
    tick_part(part);
}

void World::tick_part(int part) {
    if (part == 0) {
        switch_viking();
        check_viking_in_view();
        create_scrolled_in();
    }
    run(part);
}

void World::run(int part, bool starting) {
    const auto turn = [this](int slot) {
        Entity* entity = at(slot);
        if (entity == nullptr) {
            return;
        }
        if (entity->has_flag(entity_flag::kCountingDown) && entity->countdown != 0) {
            --entity->countdown;
        }
        updating = slot;
        if (entity->has_flag(entity_flag::kLeftTheActiveArea)) {
            if (!entity->dead) {
                entity->outside_active_area(*this);
            }
        } else {
            entity->update(*this);
        }
        forget_removed();
    };
    if (part == 0) {
        for (auto& entity : entities) {  // a tick's push starts from nothing
            if (entity) {
                entity->push_x = entity->push_y = 0;
            }
        }
        created_below.clear();
        created_late.clear();
        for (int slot = 0; slot < slots(); ++slot) {
            turn(slot);
            // The list may grow as they take their turns.
            // NOLINTNEXTLINE(modernize-loop-convert)
            for (std::size_t i = 0; i < created_below.size(); ++i) {
                turn(created_below[i]);
            }
            created_below.clear();
        }
        updating = kNobody;
        for (auto& entity : entities) {
            if (entity) {
                move(*entity);
            }
        }
        for (auto& entity : entities) {
            if (entity && entity->has_flag(entity_flag::kPutOnGround)) {
                put_on_ground(*this, *entity);
            }
        }
        for (int slot = 0; slot < slots(); ++slot) {
            if (std::ranges::find(created_late, slot) != created_late.end()) {
                turn(slot);
                continue;
            }
            if (Entity* entity = at(slot); entity != nullptr && entity->moved_by == kMovesItself) {
                entity->after_move(*this);
            }
        }
        updating = kNobody;
        created_late.clear();
        move_attached();
    } else if (part == 1) {
        if (has_vikings && !starting) {
            fall_back_viking();
        }
        for (int slot = 0; slot < slots(); ++slot) {
            if (Entity* entity = at(slot)) {
                entity->check_hits(*this);
            }
        }
    }
    if (part == 2 && !starting) {  // after the tick's second picture
        count_down_shake();
    }
    // (In a scene without Vikings the view follows what is in the first slot the same way: the night the
    // Vikings are taken moves its director to scroll.)
    // The tick's first picture measures how far the Viking is out of the box; the other two only move their share
    // of that, also when he died in between (the last Viking's death leaves nobody to follow).
    if (const Entity* viking = current_viking != kNobody ? at(current_viking) : nullptr) {
        follow(camera, viking->x, viking->y, part);
    } else if (part != 0) {
        follow(camera, 0, 0, part);
    }
    if (part == 2) {
        mark_outside_active_area();
    }
    shift_sprites(part);
    if (part == 2) {
        update_hud();
    }
    if (part == 2 && !starting) {
        turn_colours();
    }
    count_picture(pieces);
}

std::uint16_t World::random() noexcept {
    if (attract) {
        const auto swapped = static_cast<std::uint16_t>((unsigned{demo_seed} >> 8U) | (unsigned{demo_seed} << 8U));
        const auto rotated = static_cast<std::uint16_t>(
            (unsigned{swapped} << 3U) | (((unsigned{swapped} >> 15U) & 1U) << 1U) | ((unsigned{swapped} >> 14U) & 1U));
        demo_seed = static_cast<std::uint16_t>(swapped ^ rotated);
        return rotated;
    }
    constexpr std::uint32_t kMultiplier = 0x015a4e35;
    seed = (seed * kMultiplier) + 1U;
    return static_cast<std::uint16_t>(seed >> 16U);
}

void World::update_hud() {
    for (std::size_t square = 0; square < shown_items.size(); ++square) {
        const std::uint16_t item = items.at(square / kSquares).at(square % kSquares);
        if (item != shown_items.at(square)) {
            shown_items.at(square) = item;
            square = static_cast<std::size_t>(chosen_square[kVikingSlots - 1]) + (std::size_t{kSquares} * 2);
        }
    }
}

void World::load_colours(std::size_t offset) {
    constexpr std::size_t kBytes = std::size_t{16} * 3;
    if (offset + kBytes > programs.size()) {
        return;
    }
    const auto colours = programs.subspan(offset, kBytes);
    (void)assets::load_palette(palette, colours, 192);
    (void)assets::load_palette(palette, colours, 224);
}

void World::turn_colours() {
    for (std::size_t i = cycles.size(); i-- > 0;) {
        ColourCycle& cycle = cycles[i];
        if (!cycle.running || cycle.left == 0) {
            continue;
        }
        if (--cycle.left != 0) {
            continue;
        }
        const assets::Rgb last = palette.at(cycle.to);
        if (cycle.to >= cycle.from) {
            for (std::size_t colour = cycle.to; colour > cycle.from; --colour) {
                palette.at(colour) = palette.at(colour - 1);
            }
        } else {
            for (std::size_t colour = cycle.to; colour < cycle.from; ++colour) {
                palette.at(colour) = palette.at(colour + 1);
            }
        }
        palette.at(cycle.from) = last;
    }
    for (ColourCycle& cycle : cycles) {
        if (cycle.running && cycle.left == 0) {
            cycle.left = cycle.delay;
        }
    }
}

// --- A level's rules ---

namespace {

constexpr int kBlinkTicks = 21;
constexpr int kJustSwitched = 5;  // what the camera's last move is set to when the Viking changes

// The first piece of a Viking's sprite shown again after a blink.
void show(Pieces& pieces, const Entity& viking, bool erased_too) {
    Piece& piece = pieces.at(static_cast<std::size_t>(viking.sprite.first) % pieces.size());
    piece.blinked_off = false;
    piece.redraw = 2;
    if (erased_too) {
        piece.erase = 0;
    }
}

}  // namespace

int World::nearest_viking(const Entity& entity) {
    int best = 65536;
    for (int slot = 0; slot < kVikingSlots; ++slot) {
        const Entity* viking = at(slot);
        if (viking == nullptr || viking->health == 0) {
            continue;
        }
        const int across = wrapped(entity.x - viking->x);
        const int down = wrapped(entity.y - viking->y);
        const int distance = static_cast<std::uint16_t>((across < 0 ? -across : across) + (down < 0 ? -down : down));
        if (distance < best) {
            best = distance;
            last_nearest_viking = slot;
        }
    }
    return last_nearest_viking;
}

World::Offset World::offset_to(const Entity& entity, int slot) const {
    const Entity* other = at(slot);
    if (other == nullptr) {
        return {};
    }
    return {.ahead = entity.facing_left() ? wrapped(entity.x - other->x) : wrapped(other->x - entity.x),
            .below = entity.upside_down() ? wrapped(entity.y - other->y) : wrapped(other->y - entity.y)};
}

void World::switch_viking() {
    const bool previous = (pressed & key::kPrevious) != 0;
    if (!has_vikings || current_viking == kNobody || (!previous && (pressed & key::kNext) == 0)) {
        return;
    }
    int viking = current_viking;
    while (true) {
        viking =
            previous ? (viking == 0 ? kVikingSlots - 1 : viking - 1) : (viking == kVikingSlots - 1 ? 0 : viking + 1);
        if (viking == current_viking) {
            return;  // no other has health left
        }
        const Entity* other = at(viking);
        if (other != nullptr && other->health != 0) {
            break;
        }
    }
    Piece& piece = pieces.at(static_cast<std::size_t>(entities.at(static_cast<std::size_t>(viking))->sprite.first) %
                             pieces.size());
    piece.blinked_off = false;
    piece.redraw = 2;
    current_viking = viking;
    camera.moved_x = camera.moved_y = kJustSwitched;
}

void World::check_viking_in_view() {
    constexpr int kSlack = 12;
    if (!has_vikings || current_viking == kNobody) {
        return;
    }
    const Entity* viking = at(current_viking);
    if (viking == nullptr) {
        return;
    }
    // 12 pixels of slack on every side; while the camera is at the top, nothing is too high or too low.
    const bool in_view = wrapped(viking->x - camera.x + kSlack) >= 0 &&
                         wrapped(camera.x + camera.view_width + kSlack - viking->x) >= 0 &&
                         (camera.y == 0 || (wrapped(viking->y - camera.y + kSlack) >= 0 &&
                                            wrapped(camera.y + camera.view_height - viking->y) >= 0));
    if (!in_view) {
        held = pressed = 0;
        return;
    }
    if (shown_viking != current_viking) {
        if (shown_viking != kNobody) {
            const Entity* before = at(shown_viking);
            if (before != nullptr && !before->dead) {
                show(pieces, *before, true);
            }
        }
        shown_viking = current_viking;
        blink = kBlinkTicks;
    }
    if (blink == 0) {
        return;
    }
    --blink;
    if ((static_cast<unsigned>(blink) & 2U) != 0) {  // hidden two ticks out of four
        --blink;
        Piece& piece = piece_at(viking->sprite.first);
        piece.blinked_off = true;
        piece.redraw = 0;
        piece.erase = 2;
        return;
    }
    show(pieces, *viking, true);
}

void World::fall_back_viking() {
    const auto alive = [this](int slot) {
        const Entity* viking = at(slot);
        return viking != nullptr && !viking->dead;
    };
    if (current_viking == kNobody || alive(current_viking)) {
        return;
    }
    camera.moved_x = camera.moved_y = kJustSwitched;
    int viking = current_viking;
    for (int i = 0; i < kVikingSlots - 1; ++i) {
        viking = viking == kVikingSlots - 1 ? 0 : viking + 1;
        if (alive(viking)) {
            current_viking = viking;
            return;
        }
    }
    current_viking = kNobody;
    game_over = true;
}

void World::count_down_shake() noexcept {
    for (Shake* shake : {&shake_x, &shake_y}) {
        if (shake->ticks != 0) {
            --shake->ticks;
            if (shake->amount != 0) {
                shake->offset ^= shake->amount;
                continue;
            }
        }
        shake->offset = 0;
    }
}

void World::flip_across(Entity& entity) {
    entity.flags ^= entity_flag::kFacingLeft;
    const int twice = entity.x * 2;
    const std::int16_t left = wrapped(twice - entity.box.right - 1);
    entity.box.right = wrapped(twice - entity.box.left - 1);
    entity.box.left = left;
    if (entity.sprite.count == 0) {
        return;
    }
    for (int index = entity.sprite.first; index < entity.sprite.first + entity.sprite.count; ++index) {
        Piece& piece = piece_at(index);
        piece.x = wrapped(twice - piece.x - piece.size);
        piece.mirrored = !piece.mirrored;
        piece.redraw = piece.erase = 2;
    }
}

void World::flip_down(Entity& entity) {
    entity.flags ^= entity_flag::kUpsideDown;
    const int twice = entity.y * 2;
    const std::int16_t top = wrapped(twice - entity.box.bottom - 1);
    entity.box.bottom = wrapped(twice - entity.box.top - 1);
    entity.box.top = top;
    if (entity.sprite.count == 0) {
        return;
    }
    for (int index = entity.sprite.first; index < entity.sprite.first + entity.sprite.count; ++index) {
        Piece& piece = piece_at(index);
        piece.y = wrapped(twice - piece.y - piece.size);
        piece.flipped = !piece.flipped;
        piece.redraw = piece.erase = 2;
    }
}

void World::move_attached() {
    for (auto& owned : entities) {
        if (!owned || owned->moved_by == kMovesItself) {
            continue;
        }
        Entity& entity = *owned;
        if (entity.moved_by >= kMovedFreely) {
            shift(entity, entity.push_x, entity.push_y);
        } else if (const Entity* carrier =
                       entities.at(static_cast<std::size_t>(entity.moved_by) % entities.size()).get()) {
            if (carrier->facing_left() != entity.facing_left()) {
                flip_across(entity);
            }
            if (carrier->upside_down() != entity.upside_down()) {
                flip_down(entity);
            }
            // To the carrier's place plus its own push, which counts the other way for one facing left.
            const int across = wrapped(carrier->x - entity.x);
            entity.push_x = wrapped(entity.facing_left() ? across - entity.push_x : across + entity.push_x);
            const int down = wrapped(carrier->y - entity.y);
            entity.push_y = wrapped(entity.upside_down() ? down - entity.push_y : down + entity.push_y);
            shift(entity, entity.push_x, entity.push_y);
        }
        entity.moved_by = kMovesItself;
    }
}

void World::shift_sprites(int picture) {
    for (auto& owned : entities) {
        if (!owned || owned->sprite.count == 0) {
            continue;
        }
        const int dx = share_of_move(wrapped(owned->x - owned->previous_x), picture);
        const int dy = share_of_move(wrapped(owned->y - owned->previous_y), picture);
        if (dx != 0 || dy != 0) {
            shift_pieces(pieces, owned->sprite, dx, dy);
        }
    }
}

void World::mark_outside_active_area() {
    // The area's left and top edges stop at the level's; its right edge is measured from where the left edge
    // would be without that, its bottom edge from the top edge as stopped.
    const int unclamped_left = wrapped(camera.x - kMargin);
    const View area{
        .left = std::max(unclamped_left, 0),
        .right = wrapped(unclamped_left + camera.view_width + (2 * kMargin)),
        .top = std::max<int>(wrapped(camera.y - kMargin), 0),
        .bottom = wrapped(std::max<int>(wrapped(camera.y - kMargin), 0) + camera.view_height + (2 * kMargin))};
    for (int slot = kVikingSlots; slot < slots(); ++slot) {
        Entity* entity = at(slot);
        if (entity == nullptr || entity->stays_active()) {
            continue;
        }
        if (wrapped(entity->x + entity->half_width) < area.left ||
            wrapped(entity->x - entity->half_width) >= area.right ||
            wrapped(entity->y + entity->half_height) < area.top ||
            wrapped(entity->y - entity->half_height) >= area.bottom) {
            entity->set_flag(entity_flag::kLeftTheActiveArea);
        }
    }
}

}  // namespace lvr
