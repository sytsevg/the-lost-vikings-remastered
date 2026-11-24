#include "lvr/hits.hpp"

namespace lvr {

namespace {

unsigned word(int value) noexcept {
    return static_cast<std::uint16_t>(value);
}

bool overlap(const Box& a, const Box& b) noexcept {
    return a.right >= b.left && b.right >= a.left && a.bottom >= b.top && b.bottom >= a.top;
}

template <typename Matches>
int first_touching(const World& world, const Entity& entity, bool vikings_only, const Matches& matches) {
    const int slots = vikings_only ? kVikingSlots : world.slots();
    for (int slot = 0; slot < slots; ++slot) {
        const Entity* other = world.at(slot);
        if (other != nullptr && other != &entity && matches(*other) && overlap(entity.box, other->box)) {
            return slot;
        }
    }
    return kNobody;
}

}  // namespace

int find_touching(const World& world, const Entity& entity, std::uint16_t hit_class, bool vikings_only) {
    return first_touching(world, entity, vikings_only,
                          [hit_class](const Entity& other) { return other.hit_class == hit_class; });
}

int find_touching_any(const World& world, const Entity& entity, std::uint16_t mask, bool vikings_only) {
    return first_touching(world, entity, vikings_only,
                          [mask](const Entity& other) { return (other.hit_mask & mask) != 0; });
}

int find_in_row(const World& world, const Entity& entity, Row row, const TileTypes& types, bool vikings_only,
                int from) {
    const int at = row == Row::kAbove ? wrapped(entity.box.top - 1) : wrapped(entity.box.bottom + 1);
    const int slots = vikings_only ? kVikingSlots : world.slots();
    for (int slot = from < 0 ? 0 : from; slot < slots; ++slot) {
        const Entity* other = world.at(slot);
        if (other == nullptr || other == &entity || !types.test(std::size_t{other->acts_as} & 0xffU)) {
            continue;
        }
        const Box& box = other->box;
        // The original compares the Vikings' boxes as unsigned numbers, the others' as signed ones.
        const bool there = vikings_only ? word(at) >= word(box.top) && word(at - 1) < word(box.bottom) &&
                                              word(entity.box.right) >= word(box.left) &&
                                              word(box.right) >= word(entity.box.left)
                                        : at >= box.top && wrapped(at - 1) >= box.bottom &&
                                              entity.box.right >= box.left && box.right >= entity.box.left;
        if (there) {
            return slot;
        }
    }
    return kNobody;
}

}  // namespace lvr
