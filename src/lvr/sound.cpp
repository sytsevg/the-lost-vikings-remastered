#include "lvr/sound.hpp"

#include <algorithm>

namespace lvr {

namespace {

// (Who hears it too: online one player's inventory click isn't another's, though both are nobody's entity's.)
bool same(const SoundCue& one, const SoundCue& other) noexcept {
    return one.kind == other.kind && one.value == other.value && one.tune == other.tune && one.entity == other.entity &&
           one.heard_by == other.heard_by;
}

}  // namespace

CueLedger::Heard CueLedger::heard(std::uint32_t step, const std::vector<SoundCue>& asked) {
    auto kept = std::ranges::find(played_, step, &Played::step);
    if (kept == played_.end()) {
        // A step not seen yet: after the others (steps are run in order, and again from an earlier one on).
        const auto later = std::ranges::find_if(played_, [step](const Played& one) { return one.step > step; });
        kept = played_.insert(later, Played{.step = step, .cues = {}});
    }
    Heard heard;
    std::vector<bool> matched(kept->cues.size(), false);
    for (const SoundCue& cue : asked) {
        bool before = false;
        for (std::size_t i = 0; i < kept->cues.size() && !before; ++i) {
            if (!matched[i] && same(kept->cues[i], cue)) {
                matched[i] = true;
                before = true;
            }
        }
        if (!before) {
            heard.play.push_back(cue);
        }
    }
    for (std::size_t i = 0; i < kept->cues.size(); ++i) {
        if (!matched[i]) {
            heard.undone.push_back(kept->cues[i]);
        }
    }
    kept->cues = asked;
    return heard;
}

void CueLedger::confirm(std::uint32_t step) {
    while (!played_.empty() && played_.front().step < step) {
        played_.pop_front();
    }
}

}  // namespace lvr
