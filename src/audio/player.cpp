#include "audio/player.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "assets/assets.hpp"
#include "assets/catalog.hpp"

namespace lvr::audio {

namespace {

// The Modern sound is ours to shape. Where the General MIDI arrangement of an effect grates (it was written for a
// 1993 module, and a SoundFont plays the same instrument harder), the effect is remade: the notes of the OPL3
// arrangement, which the Classic sound plays, on a General MIDI instrument of our choosing.
struct Remade {
    std::size_t effect;
    std::uint8_t program;  // the General MIDI instrument
    std::uint8_t volume;   // its channel volume (controller 7)
    int transpose;         // its notes moved by this many semitones
};
constexpr std::array kRemade{
    // "Nothing to use here": two low notes at once, a semitone apart, on a square wave at nearly full volume.
    // Instead the Classic one's own figure, a short buzz and a longer one an octave below. Its OPL3 instrument is
    // a harsh, held tone (a modulator at half the pitch, driven hard, with feedback): the nearest General MIDI
    // one is the sawtooth lead.
    Remade{.effect = 3, .program = 81, .volume = 110, .transpose = 0},
};

// An effect's stream with every program change set to the row's instrument, every channel volume to its volume,
// and every note moved by its semitones.
std::vector<std::byte> remade(std::vector<std::byte> stream, const Remade& row) {
    const auto at = [&stream](std::size_t i) -> unsigned {
        return i < stream.size() ? static_cast<unsigned>(stream[i]) : 0U;
    };
    const auto past_length = [&at](std::size_t i) {  // past a number of several bytes: its last has the top bit clear
        while ((at(i) & 0x80U) != 0) {
            ++i;
        }
        return i + 1;
    };
    std::size_t i = 0;
    while (i < stream.size()) {
        const unsigned status = at(i);
        const unsigned kind = status & 0xF0U;
        if (status < 0x80) {  // a delay
            ++i;
        } else if (kind == 0x90) {  // a note: key, velocity, and how long it lasts
            if (i + 1 < stream.size()) {
                stream[i + 1] = static_cast<std::byte>(std::clamp(static_cast<int>(at(i + 1)) + row.transpose, 0, 127));
            }
            i = past_length(i + 3);
        } else if (kind == 0xC0) {
            stream[i + 1] = static_cast<std::byte>(row.program);
            i += 2;
        } else if (kind == 0xD0) {
            i += 2;
        } else if (kind == 0xF0) {
            if (status != 0xFF || at(i + 1) == 0x2F) {
                break;  // the end of the track
            }
            const std::size_t length_at = i + 2;
            std::size_t length = 0;
            std::size_t j = length_at;
            for (;; ++j) {
                length = (length << 7U) | (at(j) & 0x7FU);
                if ((at(j) & 0x80U) == 0) {
                    break;
                }
            }
            i = j + 1 + length;
        } else {
            if (kind == 0xB0 && at(i + 1) == 7 && i + 2 < stream.size()) {
                stream[i + 2] = static_cast<std::byte>(row.volume);
            }
            i += 3;
        }
    }
    return stream;
}

}  // namespace

std::expected<SoundData, std::string> load_sound_data(const assets::Archive& archive, Version version) {
    const bool classic = version == Version::kClassic;
    const std::size_t variant = classic ? 2 : 4;
    SoundData data;
    data.version = version;
    auto driver = assets::load_entry(archive, classic ? 460 : 463);
    auto effects = assets::load_entry(archive, 519 + variant);
    if (!driver || !effects) {
        return std::unexpected{!driver ? driver.error() : effects.error()};
    }
    const auto failed = [](assets::Error error) {
        return std::unexpected{std::string{"the sound data: "} + std::string{assets::describe(error)}};
    };
    if (classic) {
        auto bank = assets::load_entry(archive, 526);
        if (!bank) {
            return std::unexpected{bank.error()};
        }
        auto tables = assets::parse_opl_driver(*driver);
        auto timbres = assets::parse_timbre_bank(*bank);
        if (!tables || !timbres) {
            return failed(!tables ? tables.error() : timbres.error());
        }
        data.opl = *tables;
        data.sequencer = tables->sequencer;
        data.bank = std::move(*timbres);
    } else {
        auto tables = assets::parse_midi_driver(*driver);
        if (!tables) {
            return failed(tables.error());
        }
        data.sequencer = *tables;
        data.start_volume = 90;  // 463's 2B67 (460's: 100)
    }
    auto sequences = assets::parse_xmidi(*effects);
    if (!sequences) {
        return failed(sequences.error());
    }
    data.effects = std::move(*sequences);
    if (!classic) {  // the effects the Modern sound remakes from the Classic sound's
        auto classic_entry = assets::load_entry(archive, 519 + 2);
        auto theirs = classic_entry ? assets::parse_xmidi(*classic_entry) : std::unexpected{assets::Error{}};
        if (theirs) {
            for (const Remade& row : kRemade) {
                if (row.effect < data.effects.size() && row.effect < theirs->size()) {
                    assets::XmidiSequence& ours = data.effects.at(row.effect);
                    ours = std::move(theirs->at(row.effect));
                    ours.stream = remade(std::move(ours.stream), row);
                }
            }
        }
    }
    for (std::size_t t = 0; t < assets::kTunes.size(); ++t) {
        auto tune = assets::load_entry(archive, std::size_t{assets::kTunes.at(t)} + variant);
        if (!tune) {
            return std::unexpected{tune.error()};
        }
        auto parsed = assets::parse_xmidi(*tune);
        if (!parsed) {
            return std::unexpected{std::format("tune {}: {}", t, assets::describe(parsed.error()))};
        }
        data.tunes.at(t) = std::move(*parsed);
    }
    return data;
}

Player::Player(const SoundData& data, MidiOut& out, OplSynth* timbres, bool music, bool effects, int effects_at_a_time)
    : data_{&data},
      sequencer_{data.sequencer, out, data.start_volume},
      timbres_{timbres},
      music_on_{music},
      effects_on_{effects},
      effect_slots_{std::clamp(effects_at_a_time, 1, kMostEffects)} {}

void Player::play(int slot, const assets::XmidiSequence& sequence) {  // 76BD
    const auto index = static_cast<std::size_t>(slot);
    handles_.at(index) = sequencer_.register_sequence(sequence.stream);
    sequencer_.set_loudness(handles_.at(index), slot == 0 ? music_loudness_ : effects_loudness_);
    // 7512: each instrument the driver still lacks, from the bank (0CAB, 0CB1). A General MIDI module has its own.
    if (timbres_ != nullptr) {
        install_timbres(sequence);
    }
    // The Modern sound's effects ring out (Sequencer::start); the music and the Classic sound play as written.
    sequencer_.start(handles_.at(index), slot != 0 && data_->version == Version::kModern);
}

void Player::install_timbres(const assets::XmidiSequence& sequence) {
    for (const assets::XmidiTimbre& wanted : sequence.timbres) {
        if (timbres_->has_timbre(wanted.bank, wanted.patch)) {
            continue;
        }
        for (const assets::Timbre& timbre : data_->bank) {
            if (timbre.bank == wanted.bank && timbre.patch == wanted.patch) {
                (void)timbres_->install_timbre(timbre.bank, timbre.patch, timbre.data);
                break;
            }
        }
    }
}

void Player::free_slot(int slot) {
    const auto index = static_cast<std::size_t>(slot);
    sequencer_.stop(handles_.at(index));
    sequencer_.release(handles_.at(index));
    handles_.at(index) = -1;
    playing_.at(index) = -1;
}

void Player::play_effect(int effect) {  // 77B2
    if (!effects_on_ || effect < 0 || static_cast<std::size_t>(effect) >= data_->effects.size()) {
        return;
    }
    for (int slot = effect_slots_; slot >= 1; --slot) {
        const auto index = static_cast<std::size_t>(slot);
        if (handles_.at(index) >= 0) {
            if (sequencer_.status(handles_.at(index)) == 1) {
                continue;  // still playing
            }
            sequencer_.release(handles_.at(index));
        }
        playing_.at(index) = effect;
        play(slot, data_->effects.at(static_cast<std::size_t>(effect)));
        return;
    }
}

void Player::stop_effect(int effect) {  // 782A
    if (!effects_on_) {
        return;
    }
    for (int slot = effect_slots_; slot >= 1; --slot) {
        if (playing_.at(static_cast<std::size_t>(slot)) == effect) {
            free_slot(slot);
        }
    }
}

void Player::music_action(int action, int tune) {
    if (action == 2) {
        fade_music();
        return;
    }
    if (action != 0 && action != 3) {
        return;
    }
    if (tune != loaded_tune_ && tune >= 0 && static_cast<std::size_t>(tune) < data_->tunes.size()) {  // 775D
        loaded_tune_ = tune;
    }
    if (action == 0) {  // 7791
        restart_music();
    }
}

void Player::restart_music() {  // 78D6
    if (!music_on_ || loaded_tune_ < 0) {
        return;
    }
    const auto& tune = data_->tunes.at(static_cast<std::size_t>(loaded_tune_));
    if (!tune.empty()) {
        play(0, tune.front());
    }
}

void Player::fade_music() {  // 78F1
    if (music_on_) {
        sequencer_.set_volume(handles_.front(), 0, 1000);
    }
}

void Player::stop_all(bool keep_music) {  // 7912
    if (!music_on_ && !effects_on_) {
        return;
    }
    for (int slot = keep_music ? 1 : 0; slot <= effect_slots_; ++slot) {
        if (handles_.at(static_cast<std::size_t>(slot)) >= 0) {
            free_slot(slot);
        }
    }
}

void Player::set_music(bool on) {  // 08C8
    music_on_ = on;
    if (on) {
        restart_music();
    } else if (handles_.front() >= 0) {
        sequencer_.stop(handles_.front());
        sequencer_.release(handles_.front());
    }
}

void Player::set_effects(bool on) {
    effects_on_ = on;
    if (!on) {
        for (int slot = 1; slot <= effect_slots_; ++slot) {
            if (handles_.at(static_cast<std::size_t>(slot)) >= 0) {
                free_slot(slot);
            }
        }
    }
}

void Player::set_loudness(int music, int effects) {
    music_loudness_ = music;
    effects_loudness_ = effects;
    for (std::size_t slot = 0; slot < handles_.size(); ++slot) {
        sequencer_.set_loudness(handles_.at(slot), slot == 0 ? music : effects);  // (none there: nothing)
    }
}

}  // namespace lvr::audio
