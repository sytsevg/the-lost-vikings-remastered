#include "lvr/showcase.hpp"

#include <format>
#include <print>

#include "lvr/names.hpp"

namespace lvr {

std::string Showcase::title() const {
    return std::format("Showcase {}/{}: {} (PgDn or ] next, PgUp or [ back)", index_ + 1, kScenarios.size(),
                       current().name);
}

void Showcase::next(int by) {
    const auto count = static_cast<int>(kScenarios.size());
    index_ = static_cast<std::size_t>((((static_cast<int>(index_) + by) % count) + count) % count);
    held_ = false;
    asked_ = false;
}

std::uint16_t Showcase::before_step(Session& session, std::uint16_t keys) {
    const Scenario& scenario = current();
    ++steps_;
    // What waits for a key gets F, pressed anew every second step.
    const std::uint16_t answer = steps_ % 2 != 0 ? key::kF : 0;
    if (session.scene() != scenario.slot) {
        held_ = false;
        if (session.waiting_for_key()) {
            return answer;
        }
        if (!asked_) {
            asked_ = session.go_to(scenario.slot);
        }
        return 0;
    }
    asked_ = false;
    if (session.waiting_for_key()) {
        return answer;
    }
    if (!held_) {
        held_ = session.hold_view(scenario.x, scenario.y, kShowcaseWidth, kShowcaseHeight);
        std::println(stderr, "showcase {}: {}: level {}, view {} at {}, {}", index_ + 1, scenario.name,
                     scenario.slot + 1, held_ ? "held" : "not held (yet)", scenario.x, scenario.y);
    }
    return keys;
}

}  // namespace lvr
