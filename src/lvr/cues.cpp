// Running a scene's cues (lvr/cues.hpp).
#include "lvr/cues.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

#include "lvr/dialogue.hpp"
#include "lvr/hits.hpp"
#include "lvr/kinds.hpp"
#include "lvr/world.hpp"

namespace lvr {

bool run_cues(World& world, Entity& director, std::span<const Cue> cues, std::size_t& at) {
    while (at < cues.size()) {
        const Cue& cue = cues[at++];
        switch (cue.kind) {
            case Cue::Kind::kEffect:
                world.sound(director, static_cast<std::uint8_t>(cue.value));
                break;
            case Cue::Kind::kStopEffect:
                world.stop_sound(director, cue.value);
                break;
            case Cue::Kind::kRestartMusic:
                world.cues.push_back({.kind = SoundCue::Kind::kRestartMusic,
                                      .value = 0,
                                      .tune = 0,
                                      .entity = director.slot,
                                      .x = 0,
                                      .y = 0});
                break;
            case Cue::Kind::kColour:
                world.colour_boxes(cue.value);
                break;
            case Cue::Kind::kSay:
                world.say_at_point(cue.value, cue.anchor, director.x, director.y + cue.below);
                break;
            case Cue::Kind::kClear:
                world.clear_text();
                break;
            case Cue::Kind::kWait:
                director.count_down();
                director.countdown = cue.value;
                if (cue.value != 0) {  // a wait animates (and so moves) the director from its first tick on
                    world.animate(director);
                }
                return true;
            case Cue::Kind::kSpeedAcross:
                if (Entity* entity = world.at(cue.slot)) {
                    entity->speed_x = static_cast<std::int16_t>(cue.value);
                }
                break;
            case Cue::Kind::kSpeedDown:
                if (Entity* entity = world.at(cue.slot)) {
                    entity->speed_y = static_cast<std::int16_t>(cue.value);
                }
                break;
            case Cue::Kind::kOwnSpeed:
                director.speed_x = static_cast<std::int16_t>(cue.value);
                break;
            case Cue::Kind::kSwitches:
                world.switches = cue.value;
                break;
            case Cue::Kind::kShake:
                world.shake_y = {.amount = static_cast<std::uint8_t>(cue.value),
                                 .offset = 0,
                                 .ticks = static_cast<std::uint16_t>(cue.slot)};
                break;
            case Cue::Kind::kNoHealth:
                if (Entity* entity = world.at(cue.slot)) {
                    entity->health = 0;
                }
                break;
            case Cue::Kind::kSayAt:
                if (const Entity* entity = world.at(cue.slot)) {
                    world.say_at_point(cue.value, cue.anchor, entity->x, entity->y + cue.below);
                }
                break;
            case Cue::Kind::kSayAtEntity:
                world.say_at_point(cue.value, cue.anchor, director.x + cue.slot, director.y + cue.below);
                break;
            case Cue::Kind::kSayAtPoint:
                world.say_at_point(cue.value, cue.anchor, cue.slot, cue.below);
                break;
            case Cue::Kind::kSayAtCell:
                world.say_at_cell(cue.value, cue.anchor, cue.slot, cue.below);
                break;
            case Cue::Kind::kTell:
            case Cue::Kind::kHealth:
                for (int viking = 0; viking < kVikingSlots; ++viking) {
                    if (Entity* entity = world.at(viking)) {
                        if (cue.kind == Cue::Kind::kTell) {
                            entity->message = cue.value;
                        } else {
                            entity->health = cue.value;
                        }
                    }
                }
                break;
            case Cue::Kind::kLeave:
                world.leave = true;
                break;
        }
    }
    return false;
}

}  // namespace lvr
