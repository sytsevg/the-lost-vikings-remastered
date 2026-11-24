#include "lvr/movement.hpp"

namespace lvr {

namespace {

std::int16_t within(std::int16_t value, std::int16_t limit) noexcept {
    if (value >= 0) {
        return value < limit ? value : limit;
    }
    const std::int16_t size = wrapped(-value);
    return wrapped(-(size < limit ? size : limit));
}

// One axis of a move: the push within the limit, added to the fraction left over; the whole pixels are the step.
std::int16_t step(std::int16_t push, std::int16_t limit, std::uint8_t& fraction) noexcept {
    const auto bits = static_cast<std::uint16_t>(within(push, limit));
    const unsigned sum = unsigned{fraction} + (bits & 0xffU);
    fraction = static_cast<std::uint8_t>(sum & 0xffU);
    return static_cast<std::int8_t>(static_cast<std::uint8_t>((bits >> 8U) + (sum >> 8U)));
}

}  // namespace

void add_speed(Entity& entity, int gravity, int level_push) noexcept {
    if (entity.moved_by != kMovesItself) {
        return;
    }
    if (entity.is_falling()) {
        entity.speed_y = wrapped(entity.speed_y + gravity);
    }
    if (entity.has_flag(entity_flag::kPushed)) {
        entity.speed_x = wrapped(entity.speed_x + level_push);
    }
    entity.speed_x = within(entity.speed_x, entity.max_step_x);
    entity.speed_y = within(entity.speed_y, entity.max_step_y);
    entity.push_x = wrapped(entity.push_x + (entity.facing_left() ? -entity.speed_x : entity.speed_x));
    entity.push_y = wrapped(entity.push_y + (entity.upside_down() ? -entity.speed_y : entity.speed_y));
}

void shift(Entity& entity, int dx, int dy) noexcept {
    entity.x = wrapped(entity.x + dx);
    entity.box.left = wrapped(entity.box.left + dx);
    entity.box.right = wrapped(entity.box.right + dx);
    entity.y = wrapped(entity.y + dy);
    entity.box.top = wrapped(entity.box.top + dy);
    entity.box.bottom = wrapped(entity.box.bottom + dy);
}

void move(Entity& entity) noexcept {
    entity.previous_x = entity.x;
    entity.previous_y = entity.y;
    if (entity.moved_by != kMovesItself) {
        return;
    }
    entity.push_x = step(entity.push_x, entity.max_step_x, entity.fraction_x);
    entity.push_y = step(entity.push_y, entity.max_step_y, entity.fraction_y);
    shift(entity, entity.push_x, entity.push_y);
}

}  // namespace lvr
