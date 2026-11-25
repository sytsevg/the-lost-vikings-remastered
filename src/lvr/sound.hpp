#pragma once

// Sound cues: what the game reports to the sound side. The engine only says what happened; the sound
// side decides what plays. So far the cues are the original's own requests, with its numbers: Classic sound plays
// exactly these.

#include <cstdint>

namespace lvr {

struct SoundCue {
    enum class Kind : std::uint8_t {
        kEffect,       // value: the original's effect number
        kStopEffect,   // value: the effect
        kMusicAction,  // value: a level's music action (0 play its tune, 2 fade out, 3 load only); tune: the tune
        kRestartMusic,
        kFadeMusic,
        kStopAll,    // every effect stopped, and the music unless value is 1
        kEffectsOn,  // Alt+S: value 1 the effects on, 0 off
        kMusicOn,    // Alt+M: value 1 the music on, 0 off
    };
    Kind kind = Kind::kEffect;
    std::uint16_t value = 0;
    std::uint8_t tune = 0;
    int entity = -1;  // the slot of the entity it came from; -1: the scene
    // Where it happened, an entity's: its middle, in the level's pixels (the Modern sound places its effects by it).
    std::int16_t x = 0;
    std::int16_t y = 0;

    bool operator==(const SoundCue&) const = default;
};

}  // namespace lvr
