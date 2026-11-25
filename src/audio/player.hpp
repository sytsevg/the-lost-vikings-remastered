#pragma once

// The game's side of the sound (docs/original/sound.md): what VIKINGS.EXE asks of the driver. The music in player slot
// 0, the sound effects in slots 8, 6, 4, 2; each play registers the sequence, installs the instruments it lacks
// from the bank (the OPL3 driver only), and starts it (0000:76BD). The front end calls these from the simulation's
// events, on both versions of the sound (ADR-0015): they run side by side and one is heard.

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "assets/audio.hpp"
#include "assets/data_dat.hpp"
#include "audio/opl3.hpp"
#include "audio/sequencer.hpp"

namespace lvr::audio {

enum class Version : std::uint8_t {
    kClassic,  // OPL3: device variant 2, driver 460
    kModern,   // General MIDI: device variant 4, driver 463
};

struct SoundData {
    Version version = Version::kClassic;
    assets::OplDriverTables opl;  // Classic only
    assets::SequencerTables sequencer;
    int start_volume = 100;                                    // a sequence's volume when it starts (2B67)
    std::vector<assets::Timbre> bank;                          // Classic: entry 526; Modern: none
    std::vector<assets::XmidiSequence> effects;                // 521, 523
    std::array<std::vector<assets::XmidiSequence>, 11> tunes;  // kTunes[t] + 2, + 4
};

// Classic: driver 460, bank 526, effects 521, the tunes' third entries. Modern: driver 463, effects 523, the tunes'
// fifth entries.
std::expected<SoundData, std::string> load_sound_data(const assets::Archive& archive, Version version);

// Where an effect is heard (ours: the Modern sound places its effects by where they happen): `pan` from the
// middle, -63 hard left to 63 hard right, and how loud, in percent of what it would be. None: as the driver plays it.
struct Placement {
    int pan = 0;
    int loudness = 100;
};

class Player {
  public:
    // `out`: the driver's output, initialised; `timbres`: the OPL3 voice layer's cache (Classic), or none.
    // `music` and `effects`: the game's flags (ds:0302 / 0304 bit 0 clear: on).
    // `effects_at_a_time`: the original plays four; more is the Modern sound's, on a synthesizer of the effects'
    // own (at most kMostEffects, what the sequencer holds beside the music).
    Player(const SoundData& data, MidiOut& out, OplSynth* timbres, bool music, bool effects,
           int effects_at_a_time = kOriginalEffects);
    static constexpr int kOriginalEffects = 4;
    static constexpr int kMostEffects = Sequencer::kSequences - 1;

    // Two per display refresh (the driver's service rate in the game).
    void service() { sequencer_.service(); }

    void play_effect(int effect, const std::optional<Placement>& placed = std::nullopt);  // 77B2
    void stop_effect(int effect);                                                         // 782A, 787F
    // A level header's music action (7749 / 774F): 0 load `tune` and play it from the start, 2 fade out, 3 load
    // only; 1 and 4 nothing.
    void music_action(int action, int tune);
    void restart_music();            // 78D6
    void fade_music();               // 78F1
    void stop_all(bool keep_music);  // 7912
    void set_music(bool on);         // 08C8, Alt+M
    void set_effects(bool on);       // 08C8, Alt+S
    // Ours (Remaster rules' volume): how loud the music and the effects are heard, in percent of what the game asks
    // for (Sequencer::set_loudness), from now on.
    void set_loudness(int music, int effects);
    // Ours too: how loud the effect `effect` is heard from now on, in percent of what it would be.
    void set_effect_loudness(int effect, int percent);

  private:
    void play(int slot, const assets::XmidiSequence& sequence, const std::optional<Placement>& placed = std::nullopt,
              int percent = 100);  // 76BD
    void install_timbres(const assets::XmidiSequence& sequence);
    void free_slot(int slot);

    const SoundData* data_;
    Sequencer sequencer_;
    OplSynth* timbres_;
    bool music_on_;
    bool effects_on_;
    int loaded_tune_ = -1;  // ds:25AF
    int effect_slots_ = kOriginalEffects;
    int music_loudness_ = 100;
    int effects_loudness_ = 100;
    std::vector<int> effect_loudness_;                    // by effect: set_effect_loudness()'s, 100 as it is
    std::array<int, kMostEffects + 1> handles_ = none();  // ds:990C, by slot / 2: the music's, then the effects'
    std::array<int, kMostEffects + 1> playing_ = none();  // ds:9916: the effect in each slot
    static constexpr std::array<int, kMostEffects + 1> none() {
        std::array<int, kMostEffects + 1> all{};
        all.fill(-1);
        return all;
    }
};

}  // namespace lvr::audio
