#include "lvr/terrain.hpp"

namespace lvr {

namespace {

// Positions are 16-bit words in the original; the comparisons below are as it makes them.
// Within a 16-pixel cell: how far in a coordinate is, and the cell's first and last pixel.
int in_cell(int value) noexcept {
    return static_cast<int>(static_cast<std::uint16_t>(value) & 0xfU);
}
int cell_start(int value) noexcept {
    return wrapped(static_cast<int>(static_cast<std::uint16_t>(value) & 0xfff0U));
}
int cell_end(int value) noexcept {
    return wrapped(static_cast<int>(static_cast<std::uint16_t>(value) | 0xfU));
}

bool is_slope(int tile_type) noexcept {
    return tile_type >= int{tile::kFirstSlope};
}
bool is_one_of(int tile_type, std::initializer_list<std::uint8_t> types) noexcept {
    return std::ranges::any_of(types, [tile_type](std::uint8_t type) { return tile_type == int{type}; });
}
bool is_free(int tile_type) noexcept {
    return is_one_of(tile_type, {tile::kFree, tile::kBarrier, tile::kLadder});
}
bool is_ground(int tile_type) noexcept {
    return is_one_of(tile_type, {tile::kGround, tile::kPlatform, tile::kGround20, tile::kLadderTop, tile::kGround02});
}

// The engine's view of one entity among the others.
class Surroundings {
  public:
    Surroundings(const World& world, const Entity& self, int slots = kSlots)
        : world_{world}, self_{self}, slots_{slots} {}

    int type_at(int x, int y) const { return world_.map.tile_type_at(wrapped(x), wrapped(y)); }

    // How far into a slope the entity's feet are at column x: below its surface is positive.
    int slope_depth(int slope, int x, int bottom) const {
        const std::size_t index = ((static_cast<unsigned>(slope) & 0xfU) << 4U) + (static_cast<unsigned>(x) & 0xfU);
        return in_cell(bottom) - in_cell(world_.slope_heights.at(index));
    }

    // --- Cells ---

    // From `position` to `last` in steps of a cell, the last step shortened to end on `last`.
    template <typename TypeAt>
    std::optional<Found> scan(const TileTypes& types, int position, int last, const TypeAt& type_at_position) const {
        while (true) {
            const int found = type_at_position(position);
            if (types.test(static_cast<std::size_t>(found))) {
                return Found{.tile_type = static_cast<std::uint8_t>(found)};
            }
            if (position == last) {
                return std::nullopt;
            }
            position = wrapped(position + kCellSize);
            if (position >= last) {
                position = last;
            }
        }
    }

    // A column down the entity's height as it was before this tick's vertical move. An entity whose feet were on a
    // slope finds nothing beside it.
    std::optional<Found> cells_in_column(const Entity& entity, const TileTypes& types, int column) const {
        const int last = entity.box.bottom >= entity.push_y ? wrapped(entity.box.bottom - entity.push_y) : 0;
        if (is_slope(type_at(entity.x, last))) {
            return std::nullopt;
        }
        const int first = entity.box.top > entity.push_y ? wrapped(entity.box.top - entity.push_y) : 0;
        return scan(types, first, last, [&](int row) { return type_at(column, row); });
    }

    std::optional<Found> cells_in_row(const TileTypes& types, int row, int from, int to) const {
        return scan(types, from, to, [&](int column) { return type_at(column, row); });
    }

    // --- Entities as terrain ---

    // The first other entity, in slot order, that acts as one of the types and passes the test.
    template <typename Test>
    std::optional<Found> first_entity(const TileTypes& types, const Test& test) const {
        for (int slot = 0; slot < slots_; ++slot) {
            const Entity* other = world_.entities.at(static_cast<std::size_t>(slot)).get();
            if (other == nullptr || other == &self_) {
                continue;
            }
            if (types.test(std::size_t{other->acts_as} & 0xffU) && test(*other)) {
                return Found{.tile_type = static_cast<std::uint8_t>(other->acts_as & 0xffU), .entity = slot};
            }
        }
        return std::nullopt;
    }

    std::optional<Found> entity_in_column(const Entity& entity, const TileTypes& types, int column) const {
        return first_entity(types, [&](const Entity& other) {
            return column >= other.box.left && wrapped(column - 1) < other.box.right &&
                   wrapped(entity.box.bottom - entity.push_y) >= other.box.top &&
                   wrapped(entity.box.top - entity.push_y) <= other.box.bottom;
        });
    }

    std::optional<Found> entity_in_row(const Entity& entity, const TileTypes& types, int row) const {
        return first_entity(types, [&](const Entity& other) {
            return row >= other.box.top && wrapped(row - 1) < other.box.bottom &&
                   wrapped(entity.box.right - other.box.left) >= 0 && wrapped(other.box.right - entity.box.left) >= 0;
        });
    }

    std::optional<Found> entity_at(const TileTypes& types, int x, int y) const {
        return first_entity(types, [&](const Entity& other) {
            return x >= other.box.left && wrapped(x - 1) < other.box.right && y >= other.box.top &&
                   wrapped(y - 1) < other.box.bottom;
        });
    }

  private:
    const World& world_;
    const Entity& self_;
    int slots_;
};

std::optional<Found> or_else(std::optional<Found> first, const auto& second) {
    return first ? first : second();
}

// --- Pushed back ---

void clear_x(Entity& entity, int dx) noexcept {
    entity.x = wrapped(entity.x + dx);
    entity.box.left = wrapped(entity.box.left + dx);
    entity.box.right = wrapped(entity.box.right + dx);
    entity.fraction_x = 0;
}

void clear_y(Entity& entity, int dy) noexcept {
    entity.y = wrapped(entity.y + dy);
    entity.box.top = wrapped(entity.box.top + dy);
    entity.box.bottom = wrapped(entity.box.bottom + dy);
    entity.fraction_y = 0;
}

// Out of the cell it moved into, to the cell's edge.
void out_of_wall(Entity& entity, bool moved_left) noexcept {
    if (moved_left) {
        clear_x(entity, wrapped(cell_end(entity.box.left) + 1) - entity.box.left);
    } else {
        clear_x(entity, wrapped(cell_start(entity.box.right) - 1) - entity.box.right);
    }
}
void onto_cell(Entity& entity) noexcept {
    clear_y(entity, wrapped(cell_start(entity.box.bottom) - 1) - entity.box.bottom);
}
void under_ceiling(Entity& entity) noexcept {
    clear_y(entity, wrapped(cell_end(entity.box.top) + 1) - entity.box.top);
}

// The edges overlap in height as they were before both entities' vertical moves.
bool beside(const Entity& entity, const Entity& other, int edge) noexcept {
    if (edge < other.box.left || wrapped(edge - 1) >= other.box.right) {
        return false;
    }
    if (wrapped(entity.box.bottom - entity.push_y) < wrapped(other.box.top - other.push_y)) {
        return false;
    }
    return wrapped(other.box.bottom - other.push_y) >= wrapped(entity.box.top - entity.push_y);
}

// Overlapping across, and the sweep of its bottom edge this tick meets the sweep of the other's top edge.
bool comes_down_on(const Entity& entity, const Entity& other) noexcept {
    if (entity.box.right < other.box.left || other.box.right < entity.box.left) {
        return false;
    }
    const int now = entity.box.bottom >= 0 ? entity.box.bottom : 0;
    const int before = wrapped(entity.box.bottom - entity.y + entity.previous_y);
    const int was = before > 0 ? before : 0;
    const int low = was >= now ? now : was;
    const int high = was >= now ? was : now;
    const int other_before = wrapped(other.box.top - other.y + other.previous_y);
    if (other_before >= other.box.top) {
        return high >= other.box.top && other_before >= low;
    }
    return high >= other_before && other.box.top >= low;
}

// How a landing lifts the entity: onto the top of the cell row its feet are in, or up by some pixels (a slope).
struct Landing {
    bool onto_cell = false;
    int lift = 0;
};

// It crossed into a new cell row, and a cell of the types is under its bottom edge.
std::optional<Landing> land_on_cells(const Surroundings& around, const Entity& entity, const TileTypes& types) {
    const int previous_row = cell_start(wrapped(entity.box.bottom - entity.y + entity.previous_y));
    if (cell_start(entity.box.bottom) == previous_row) {
        return std::nullopt;
    }
    int column = entity.box.left;
    while (true) {
        const int found = around.type_at(column, entity.box.bottom);
        if (!is_slope(found) && types.test(static_cast<std::size_t>(found))) {
            return Landing{.onto_cell = true, .lift = 0};
        }
        if (column == entity.box.right) {
            return std::nullopt;
        }
        column = wrapped(column + kCellSize);
        if (column >= entity.box.right) {
            column = entity.box.right;
        }
    }
}

std::optional<Landing> land_on_slope(const Surroundings& around, const Entity& entity, const TileTypes& types) {
    const int column = entity.x;
    const int previous_bottom = wrapped(entity.previous_y - entity.y + entity.box.bottom);
    const int was_on = around.type_at(column, previous_bottom);
    if (is_slope(was_on) && !is_slope(around.type_at(column, entity.box.bottom))) {
        // It left the slope's cell downwards: measured from the bottom row of that cell.
        const int depth = around.slope_depth(was_on, column, 15);
        if (depth >= 0) {
            return Landing{.onto_cell = false, .lift = in_cell(entity.box.bottom) + depth + 1};
        }
    }
    const int on = around.type_at(column, entity.box.bottom);
    if (!is_slope(on)) {
        return land_on_cells(around, entity, types);
    }
    const int depth = around.slope_depth(on, column, entity.box.bottom);
    if (depth >= 0) {
        return Landing{.onto_cell = false, .lift = depth};
    }
    return std::nullopt;
}

bool has_slopes(const TileTypes& types) {
    for (std::size_t type = tile::kFirstSlope; type < 0xff; ++type) {
        if (types.test(type)) {
            return true;
        }
    }
    return false;
}

std::optional<Found> beside_among(const World& world, const Entity& entity, Side side, const TileTypes& types,
                                  bool vikings_only) {
    const Surroundings around{world, entity, vikings_only ? kVikingSlots : kSlots};
    if (side == Side::kAbove || side == Side::kBelow) {
        const int row = side == Side::kAbove ? wrapped(entity.box.top - 1) : wrapped(entity.box.bottom + 1);
        const auto entities = [&] { return around.entity_in_row(entity, types, row); };
        return vikings_only ? entities()
                            : or_else(around.cells_in_row(types, row, entity.box.left, entity.box.right), entities);
    }
    const bool right_side = (side == Side::kAhead) != entity.facing_left();
    const int column = right_side ? wrapped(entity.box.right + 1) : wrapped(entity.box.left - 1);
    const auto entities = [&] { return around.entity_in_column(entity, types, column); };
    return vikings_only ? entities() : or_else(around.cells_in_column(entity, types, column), entities);
}

}  // namespace

TileTypes tile_types(std::initializer_list<int> types) {
    TileTypes set;
    for (const int type : types) {
        set.set(static_cast<std::size_t>(type) & std::size_t{0xff});
    }
    return set;
}

const TileTypes& slopes() {
    static const TileTypes types = tile_types({0x30, 0x31, 0x32, 0x33, 0x34, 0x35});
    return types;
}

std::optional<Found> find_beside(const World& world, const Entity& entity, Side side, const TileTypes& types) {
    return beside_among(world, entity, side, types, false);
}

std::optional<Found> find_viking_beside(const World& world, const Entity& entity, Side side, const TileTypes& types) {
    return beside_among(world, entity, side, types, true);
}

std::optional<Found> find_ahead_below(const World& world, const Entity& entity, const TileTypes& types) {
    const int x = entity.facing_left() ? wrapped(entity.box.left - 1) : wrapped(entity.box.right + 1);
    return find_at(world, entity, x, wrapped(entity.box.bottom + 1), types);
}

std::optional<Found> find_at(const World& world, const Entity& entity, int x, int y, const TileTypes& types) {
    const Surroundings around{world, entity};
    return or_else(around.cells_in_row(types, y, x, x), [&] { return around.entity_at(types, x, y); });
}

bool can_walk_on(const World& world, const Entity& entity) {
    const Surroundings around{world, entity};
    const int column = wrapped(entity.x + (entity.facing_left() ? -kCellSize : kCellSize));
    const int feet = entity.box.bottom;
    const int at_feet = around.type_at(column, feet);
    if (is_slope(at_feet)) {
        return true;
    }
    if (at_feet == int{tile::kGround}) {
        const int above = around.type_at(column, feet - kCellSize);
        if (is_slope(above) || is_free(above)) {
            return true;
        }
    }
    if (is_free(at_feet)) {
        const int under = around.type_at(column, feet + kCellSize);
        return is_slope(under) || is_ground(under);
    }
    return false;
}

bool stop_at_wall(const World& world, Entity& entity, const TileTypes& types) {
    const Surroundings around{world, entity};
    if (entity.x != entity.previous_x) {  // cells only stop an entity that moved across
        const bool moved_left = entity.x <= entity.previous_x;
        if (around.cells_in_column(entity, types, moved_left ? entity.box.left : entity.box.right)) {
            out_of_wall(entity, moved_left);
            return true;
        }
    }
    // An entity stops it by how the two moved against each other, whether it moved itself or not.
    const auto into = around.first_entity(types, [&](const Entity& other) {
        if (entity.push_x == other.push_x) {
            return false;
        }
        const bool left_side = entity.push_x <= other.push_x;
        return beside(entity, other, left_side ? entity.box.left : entity.box.right);
    });
    if (!into) {
        return false;
    }
    const Entity& other = *world.entities.at(static_cast<std::size_t>(into->entity));
    if (entity.push_x <= other.push_x) {
        clear_x(entity, wrapped(other.box.right - entity.box.left + 1));
    } else {
        clear_x(entity, wrapped(-(entity.box.right - other.box.left + 1)));
    }
    return true;
}

bool stop_at_floor_or_ceiling(const World& world, Entity& entity, const TileTypes& types) {
    const Surroundings around{world, entity};
    if (entity.y != entity.previous_y) {
        const bool moved_up = entity.y <= entity.previous_y;
        if (around.cells_in_row(types, moved_up ? entity.box.top : entity.box.bottom, entity.box.left,
                                entity.box.right)) {
            if (moved_up) {
                under_ceiling(entity);
            } else {
                onto_cell(entity);
            }
            return true;
        }
    }
    const auto against = [&entity](const Entity& other) { return wrapped(entity.push_y - other.push_y); };
    const auto into = around.first_entity(types, [&](const Entity& other) {
        const int relative = against(other);
        if (relative == 0) {
            return false;
        }
        const int edge = relative < 0 ? entity.box.top : entity.box.bottom;
        return edge >= other.box.top && wrapped(edge - 1) < other.box.bottom && entity.box.right >= other.box.left &&
               other.box.right >= entity.box.left;
    });
    if (!into) {
        return false;
    }
    const Entity& other = *world.entities.at(static_cast<std::size_t>(into->entity));
    if (against(other) < 0) {
        clear_y(entity, wrapped(other.box.bottom - entity.box.top + 1));
    } else {
        clear_y(entity, wrapped(-(entity.box.bottom - other.box.top + 1)));
    }
    return true;
}

bool land(const World& world, Entity& entity, const TileTypes& types) {
    const Surroundings around{world, entity};
    if (entity.y > entity.previous_y) {  // cells only catch an entity that moved down
        const auto landing =
            has_slopes(types) ? land_on_slope(around, entity, types) : land_on_cells(around, entity, types);
        if (landing) {
            if (landing->onto_cell) {
                onto_cell(entity);
            } else {
                clear_y(entity, -landing->lift);
            }
            return true;
        }
    }
    const auto onto = around.first_entity(
        types, [&](const Entity& other) { return entity.push_y > other.push_y && comes_down_on(entity, other); });
    if (!onto) {
        return false;
    }
    const Entity& other = *world.entities.at(static_cast<std::size_t>(onto->entity));
    clear_y(entity, wrapped(-(entity.box.bottom - other.box.top + 1)));
    return true;
}

void put_on_ground(const World& world, Entity& entity) {
    entity.clear_flag(entity_flag::kPutOnGround);
    if (entity.speed_y < 0) {
        return;
    }
    const Surroundings around{world, entity};
    const int column = entity.x;
    const int feet = entity.box.bottom;
    const int at_feet = around.type_at(column, feet);
    std::optional<int> lift;
    if (at_feet == int{tile::kLadderTop}) {
        lift = in_cell(feet) + 1;
    } else if (at_feet == int{tile::kGround}) {  // in the ground: up a step, if there is room or a slope above
        const int above = around.type_at(column, feet - kCellSize);
        if (is_slope(above)) {
            lift = around.slope_depth(above, column, feet) + kCellSize;
        } else if (is_free(above)) {
            lift = in_cell(feet) + 1;
        }
    }
    if (!lift) {
        const TileTypes vikings_stand_on = tile_types({tile::kGround, tile::kPlatform, tile::kShieldOverhead});
        if (entity.slot < kVikingSlots && around.entity_in_row(entity, vikings_stand_on, wrapped(feet + 1))) {
            return;  // a Viking standing on an entity
        }
        if (is_slope(at_feet)) {
            lift = around.slope_depth(at_feet, column, feet);
        } else if (is_free(at_feet)) {
            const int under = around.type_at(column, feet + kCellSize);
            if (is_slope(under)) {
                lift = around.slope_depth(under, column, feet) - kCellSize;
            } else if (is_ground(under)) {
                lift = in_cell(feet) - 15;
            }
        }
    }
    if (lift) {
        entity.y = wrapped(entity.y - *lift);
        entity.box.top = wrapped(entity.box.top - *lift);
        entity.box.bottom = wrapped(entity.box.bottom - *lift);
        entity.push_y = 0;
    }
}

}  // namespace lvr
