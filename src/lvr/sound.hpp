#pragma once

// Sound cues: what the game reports to the sound side. The engine only says what happened; the sound
// side decides what plays. So far the cues are the original's own requests, with its numbers: Classic sound plays
// exactly these.

#include <cstdint>
#include <deque>
#include <vector>

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
    // Online (ADR-0012): heard by that player alone (his inventory's clicks); -1: by everyone.
    int heard_by = -1;

    bool operator==(const SoundCue&) const = default;
};

// Online, rollback runs steps again (ADR-0012): what each asks of the sound is heard once. The ledger keeps what was
// played for every step that may still run again. A step's cues as it comes out of it, the first time or again:
// those it hadn't asked for before are to be played now; those played for it before that it doesn't ask for any
// more were undone (an effect of them still sounding is to be stopped). A cue is the same as one played before by
// what it is, its number, its tune and its entity, and the how-manieth such it is in the step (not where it was:
// a step run again may have its entity a pixel elsewhere).
class CueLedger {
  public:
    struct Heard {
        std::vector<SoundCue> play;
        std::vector<SoundCue> undone;
    };
    Heard heard(std::uint32_t step, const std::vector<SoundCue>& asked);
    // The steps before `step` are confirmed: none of them runs again, and they are forgotten.
    void confirm(std::uint32_t step);
    std::size_t steps_kept() const noexcept { return played_.size(); }

  private:
    struct Played {
        std::uint32_t step;
        std::vector<SoundCue> cues;
    };
    std::deque<Played> played_;  // by step, the oldest first
};

}  // namespace lvr
