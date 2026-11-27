# The sound drivers: 460 (Sound Blaster Pro FM, OPL3) and 463 (MIDI)

**Status:** observed (static disassembly of `DATA.DAT` entry 460: `objdump -D -b binary -m i8086`;
offsets below are within the entry) and **checked in DOSBox** (below). Implemented in `src/audio/sequencer.*` (the
sequencer, which driver 463 shares: [below](#driver-463-the-midi-driver-modern)), `src/audio/opl3.*` (the voice layer and
the chip, ymfm's YMF262; the driver's tables read from the entry by `assets::parse_opl_driver`),
and `src/audio/player.*` (the game's side, [`sound.md`](sound.md)).

## Layout
- Offset 0: a word, the offset of the **function table** (`0x2D`): pairs of words (AIL function number, offset),
  up to a 0 or `0xFFFF` number. 100 (`0x64`) describe the driver → `27C8`; 101 detect → `140E`; 102 initialise →
  `35D8`; **103, the timer service → `331E`**; 104 shut down → `3709`; 150–159, 170–194 the sequence and timbre
  calls (register `0xAA` → `3980`, …).
- The driver keeps its state in its own code segment (`cs:`), per sequence in a state table (pointer in `ds:si`).

## The sequencer (timer service, `331E`)
Run at every timer interrupt: twice per display refresh in the game (119.43 Hz, [`timing.md`](timing.md#audio)).
For each registered sequence that is playing (state word `1A` = 1):
1. **Rate:** `30` += `32` (the rate in percent, 100 normal); while `30` ≥ 100: `30` −= 100 and one **XMIDI tick**
   is played. So at the normal rate, one tick per service call.
2. Per tick: a beat counter (`48`/`4A` += `44`/`46`, wrapping at `4C`/`4E` into `3E`/`40`, the beat and bar).
3. Per tick, the **note durations**: each of the 32 note slots (`148` + *k*: the channel, `0xFF` free; `168` the
   key; `188`/`1C8` the 32-bit ticks left) counts down; at 0 a **note off** (status `0x80` | the channel's mapped
   channel, key, velocity 0) goes to the synth (`2629`), the slot is freed and the channel's note count
   (`cs:2A1B`) drops. `20` counts the busy slots.
4. Per tick, the **delay** `1E` counts down; at 0 the events are read (`3479`) until a delay byte (< `0x80`), which
   becomes the new delay. Each channel event's channel is mapped through the sequence's table `68` (channel
   locking) and skipped if that channel is locked by another (`cs:2A2B` bit 7):
   - note on (`30CD`): the duration (variable-length) − 1 into a free note slot (a new slot when none,
     `20` + 1), then the note on to the synth;
   - controller (`2EE9`, then to the synth), program (kept in `78` + ch and `cs:29EB` + ch, then the synth),
     pitch bend (kept in `88`/`98` + ch and `cs:29FB`/`2A0B`), aftertouch (`A0`, `D0`: to the synth as they are);
   - meta (`3185`): **end of track** ends the sequence (`2CF4`, below; state `1A` = 2, "done"), restarted only
     when a loop count is set (`1C`); **time signature** (`58`) and **tempo** (`51`) only set the beat counter's
     step (`44`/`46`) and bar length (`4C`/`4E`): they never change the playback speed (XMIDI's fixed 120 Hz, here
     the timer's rate); SysEx (`3286`).
   - the sequencer's own **controllers** (`2EE9`): 110 (≥ 64) **locks** a channel: `3E20` picks, among the
     physical channels 8 down to 1 (0-based) not locked or protected (`cs:2A2B` & `0xC0`), the one with the fewest
     notes (`cs:2A1B`; ties: the first, from 8 down), protected ones only if none else; it sends it sustain off,
     releases its notes, marks it locked (`0x80`), and the sequence's channel is mapped there (`68` + ch); none
     free: the channel stays itself. Below 64 the lock is undone (`2C1E` releases the notes, `3EA3` unlocks: sustain
     off, all notes off, …) and the map restored. 111 protects (≥ 64) or unprotects the channel (`0x40`). 116
     **FOR**: a free loop level of 4 (`60`–`66` = `0xFFFF`) gets the count (0 = forever) and the position after the
     event (`50`/`52`). 117 **NEXT** (only ≥ 64): the innermost active level: count 0 → jump back; else count − 1,
     and at 0 the level ends without a jump. 118 resets the beat counter, 119 calls the application's callback,
     115 makes the channel's next controller take its value from a table (unused here). 7 (volume) is scaled by
     the sequence's volume (`24`, percent) / 100, at most 127; its value and some others are remembered per
     channel (`B8` + 16 × index, `cs:295B`; index by controller from `cs:282F`). Every other controller goes to
     the synth on the mapped channel.
5. **Ramps:** if the rate `32` isn't its target `34`, or the volume `24` isn't its target `26`: a time accumulator
   (`36` or `28`) += 83 (8.3 ms in tenths of a millisecond) and the value moves 1 towards the target for every
   period (`3A` or `2C`) that passed; a volume change is applied to the channels (`2E8A`).

## The synth layer (`2629`): MIDI messages to OPL voices
Per MIDI channel (0–15) in `cs:`: `1209` volume (controller 7), `1219` pan (10), `1229`/`1239` pitch bend LSB/MSB,
`1249` expression (11), `1259` modulation (1), `1269` sustain (64), `1279` controller 112, `1289` the channel's
timbre (an index into the timbre cache), `1299` patch bank (controller 114), `12A9` program; `12B9` + key: the
percussion timbres (bank 127) by key.
- **Note on** (`24E4`), only on MIDI channels 1–9 (0-based; 2–10 one-based), velocity 0 = note off: the channel's
  timbre (channel 9: the key's percussion timbre, looked up once, `15E9` with bank 127); none → ignored. The
  timbre's LRU stamp (`cs:0647`, a 32-bit counter, into `064B`/`07CB` + timbre) is renewed. The **first free voice
  slot** of 20 (`cs:0D95` = 0) takes it; none free → the note is **dropped**. The slot keeps the channel (`0DD1`),
  the key (`0DF9`), the note it plays (`0DE5`: the key, but for channel 9 the timbre's byte 2, and `0E0D` the key
  then), the velocity through the table `cs:0537` (by velocity / 8) in `0E21`, the timbre's address. By the timbre's
  size: 14 bytes → a 2-operator voice (`2384`), 25 bytes → a 4-operator voice (`2299`); then key on (`1A5A`).
- **Note off** (`2477`): every slot playing that channel and key: with sustain on (≥ 64) it is only marked
  (`0E35` = 1) for later; else released (`1B11`) and freed, or (4-op pairs not yet …) marked in `0D6D`.
- **Controllers:** 1, 7, 10, 11 store and mark every slot of the channel for an update (`0E49` |= `0x80`, `0x40`,
  `0x08`, `0x40`), and the slots are updated at once (`1B7C`); 64 stores, and below 64 releases the sustained notes
  (`25EE`); 112, 114 store; 113 sets/clears bit 6 of the channel's timbre's flags (`cs:0C4B`); 121 resets the
  channel's controllers (sustain off and released, modulation 0, expression 127, bend centred) and updates;
  123 all notes off on the channel.
- **Program change:** kept, and the channel's timbre looked up (`15E9`, bank `1299`, program).
- **Pitch bend:** kept, slots updated (`0x01`).

### Timbres (the bank's bytes, [`../formats/audio.md`](../formats/audio.md#instrument-banks-global-timbre-library))
14 bytes (2 operators): `u16` size, then 2: transpose or (percussion) the note to play; 3–7 the modulator's
registers `20`, `40`, `60`, `80`, `E0` (AM/VIB/EG/KSR/multiple, KSL/level, attack/decay, sustain/release,
waveform); 8 feedback/connection (`C0`); 9–13 the carrier's `20`, `40`, `60`, `80`, `E0`. 25 bytes (4 operators,
OPL3): the same, then 14–17 the third operator's `20`, `40`, `60`, `80`, 18 its `E0`; 20–23 the fourth's, 24 its
`E0`; bit 7 of byte 8 the second connection bit. Levels are kept inverted (63 − level, × 4) and scaled at the
update.

## The sequence calls
- **Reset** (`2B67`, on start): loops free, channel map identity, programs/bends/remembered controllers unknown
  (`0xFF`), note slots free, delay 0, volume and its target 100, rate and its target 100, the beat counter reset.
- **Start** (fn 170, `3980`): a playing sequence is stopped first; reset, the read position at the first event,
  state 1 (playing).
- **Stop** (fn 171, `3A15`): every note of the sequence off (`2CA0`), the end handling (`2CF4`), state 0.
- **End handling** (`2CF4`): per channel, sustain off if it was on, a locked channel released and unlocked (map
  restored), protection cleared, controller 112 sent as 0 if it was on.
- **Resume** (fn 173): locked channels locked again, the remembered controllers, bends and programs sent again.
- **Status** (fn 174): `1A`: 0 stopped, 1 playing, 2 done (ended by itself).
- **Release** (fn 152, `393C`): a playing sequence is released when it ends (`1C` = 1), else at once.
- **Volume / rate ramps** (fns 177 / 178, `3CF1` / `3C7D`): the target, and over *t* ms: period = *t* × 10 /
  |target − current| (at least 1), in tenths of a millisecond; each service call adds 83 (8.3 ms) and steps the
  value by one per whole period, towards the target. *t* = 0: at once. The game's fade (`0CED`, 1,000 ms to 0).

## The voices (`2384`, `2299`, `1A5A`, `1B11`, `1B7C`, `2155`)
- **2 operators** (`2384`): the timbre's bytes into the slot (`0DA9` = 0): connection bit (byte 8 bit 0) into
  `0E85`, the operators' level `(63 − (byte & 0x3F)) × 4` and KSL (`& 0xC0`), multiple (low nibble) and flags
  (high nibble), AD, SR, waveforms, feedback; everything marked for writing (`0E49` = `0xF9`), key on bit `0x20`
  (`0E71`), priority `0x7FFF` (`0FED`). Which levels follow the volume: the carrier always, the modulator too when
  the connection is additive (`0F39` = connection | 2).
- **4 operators** (`2299`): the 2-operator part, then `0DA9` = 3, the second connection bit (byte 8 bit 7) into
  `0E85` bit 1, which operators follow the volume from the tables `cs:0637`/`063B` by the two connection bits,
  and the third and fourth operators' values (bytes 14–18, 20–24).
- **Key on** (`1A5A`): an OPL channel for the slot: 2 operators: the next free of 18 after the last one used
  (`cs:0D18`, round robin; free = `cs:1349` + ch is `0xFF`); 4 operators: the next free pair of 6 (`cs:0D1A`, first
  channels from `cs:0631`), both channels free. The channel's MIDI channel is recorded and its voice count
  (`cs:1339`) + 1; then every register is written (`1B7C` with `0xF9`). None free: **steal** (`2155`).
- **Steal** (`2155`): each active slot's priority = `0xFFFF` if its MIDI channel's controller 112 ≥ 64, else its
  own (`0FED`, `0x7FFF`), minus the voices its MIDI channel has (at least 0). The waiting slot (no OPL channel) with
  the highest priority takes the channel of the playing one with the lowest, if its priority is higher (a 4-op
  slot takes a 2-op channel only with its partner, `cs:05E9`/`05D7`); repeated while it helps.
  **A flaw of the driver's, kept:** the partner is looked up by the victim's *slot* number, not its channel
  (`2220`). When that happens to be the victim's own channel, the victim is released as "the partner", the second
  release does nothing, and the waiting slot is given the victim's channel as it now is: `0xFF`, none. It stays
  waiting (`1B7C` returns for a slot without a channel), but its MIDI channel's voice count has gone up and stays
  one too high. The two owner bytes the driver then writes "for channel `0xFF`" land in its own detection routine
  (`cs:1448`, `144B` inside `140E`), which only runs at the start. Reached in the game by the password screen's
  beeps over the title music; found by a recorded playthrough, where our
  code faulted instead. Not compared register for register with the original on that path.
- **Release** (`1B11`): key on bit off, `B0` rewritten (key off), the OPL channel (and a 4-op partner) freed, the
  MIDI channel's voice count − 1, the slot freed.
- **Writing the registers** (`1B7C`, by the flags in `0E49`, in this order): first the OPL3 4-operator register
  `0x104` (kept in `cs:0D1C`) when the slot's channel changes between 2- and 4-operator use (when it becomes
  2-operator again, also the partner's levels and its `B0`). Then for the first operator pair (and for a 4-op slot
  after it, the second pair the same way):
  - `0x80`: `20` of both operators: multiple | flags | `0x40` (vibrato) while the channel's modulation ≥ 64;
  - `0x40`: `40`: KSL | (63 − level'), level' = level × v / 127 for the operators that follow the volume, where
    v = (channel volume × expression × 2 / 256, + 1 unless 0) × the velocity factor (`cs:0537` + velocity / 8) the
    same way;
  - `0x20`: `60` and `80` of both;
  - `0x10`: `E0`: the carrier's, then the modulator's;
  - `0x08`: `C0` + channel: feedback | connection | `0x30` (both outputs), less `0x10` when the channel's pan ≤ 27,
    less `0x20` when ≥ 100;
  - `0x01`: the frequency: note = key (`0DE5`) + transpose (`0E0D`, signed) − 24, folded into 0–95 by octaves;
    (−24, then up an octave at least once and down an octave at least once, so in effect key + transpose − 12)
    pitch = note × 256 + ((bend − 8192) >> 5) × 12, then (pitch + 8) >> 4 in 1/16 semitones, folded into
    0–`0x5FF` by octaves; the F-number from `cs:010D` (32 bytes per note of the octave `cs:02ED`[semitone], 2 per
    sixteenth) and the block `cs:028D`[semitone] − 1 (+ 1 when the F-number's sign bit is set; a negative block: 0
    and the F-number halved). `A0` + channel = its low byte, `B0` + channel = its high 2 bits | block << 2 |
    key-on (`0x20`, kept in `0E5D`). A slot whose key-on bit is off writes only `B0` without it.
- Register writes go through `13A8` (an operator's: `cs:056B` offset, `058F` chip), `13C4` (a channel's: `05B3`,
  `05C5`) and `13DF` (port = base + 2 × chip, the register, then the value, with status reads between).

## Initialisation (fn 102, `35D8`)
Sequences and locks cleared; the remembered-controller indices from the controller list `cs:2814` (9 controllers,
16 apart); the synth reset (`1383`, `1582`, `19BE`, `37D4`); then for the MIDI channels 2–10 (1–9 0-based) each
controller of `cs:2814` with its default from `cs:281D` (unless `0xFF`), the bend centred (`0x40` MSB), and the
default program from `cs:2825`.

## Tables used (read from entry 460 by the game, not copied)
`010D` F-numbers, `028D` blocks, `02ED` note in octave, `0537` velocity factors, `0547`/`0559` the operator slots
of a channel's first/second operator (with `0547`/`0559` + 3 for the 4-op second pair), `056B`/`058F` operator
register offset / chip, `05B3`/`05C5` channel register offset / chip, `05D7`, `05E9` 4-op partners, `05FB`/`060D`
the partner channel's operators, `061F` the `0x104` bit of a channel, `0631` the 4-op pairs' first channels,
`0637`/`063B` which operators follow the volume by connection, `2814`/`281D`/`2825` the default controllers,
their values and the default programs.

## Not reproduced
The timbre cache's eviction (3,584 bytes; the game's instruments fit, `install_timbre` refuses what wouldn't), the
beat and bar counter with its callbacks (controller 119, meta `58`/`51`), indirect controllers (115), `RBRN` branch
chunks, SysEx: none of them change what the game sounds like.

## Driver 463: the MIDI driver (Modern)
Entry 463 (by its own description the Roland MT-32 driver for an MPU MIDI interface; 10,625 bytes), the driver `SETUP.EXE` gives
both the MT-32 and General MIDI (variant 4) choices ([`../formats/audio.md`](../formats/audio.md#sound-devices)).
Read statically: both drivers disassembled, aligned by instruction (Python's `difflib` over the
mnemonics) and their operands compared with 460's addresses mapped to 463's.
- **Its sequencer is 460's code**: 460 `2814`–`3656` and `36F6`–`3F86` (fns 150–194, the timer service, the
  sequencer's routines and tables) line up instruction for instruction with 463 `11D7`–`2026` and `20F0`–`2980`
  (2,556 instructions; the shift is −0x163D up to `32C6`, −0x1630 to fn 102, −0x1606 after it, because 463's
  hardware code and initialisation have other lengths). The tables at `2814`/`281D`/`2825` are the same bytes at
  463's `11D7`/`11E0`/`11E8`. The differences:
  - a sequence's **start volume** (`2B67`: state `24`/`26`) is **90** in 463, 100 in 460, so every volume controller
    of the MIDI version is scaled to 90 % (`2EE9`);
  - every MIDI message goes to `08CE` (below) instead of 460's voice layer `2629`;
  - fn 102 calls the MPU's set-up (`050D` reset, command `0xFF`; `0543` UART mode, `0x3F`; `0564` two MT-32 SysEx
    blocks through fn 187, addresses 7F 00 00 (reset) and 10 00 0D) and waits 10 refreshes (`0787`, port `3DA`)
    after the default controllers and after the default programs.
- **The output (`08CE`)** sends each message to the MPU byte by byte (`0499`) as it is, except for controllers
  `0x20`–`0x2E` and `0x3A`–`0x3F` (MT-32 patch and timbre edits: SysEx or nothing) and `0x6E`–`0x78` (the
  sequencer's own; `0x71`/`0x72` also set the MT-32 timbre cache's state), which never go out as controllers.
  Program changes and note-ons also keep the MT-32 timbre cache (`cs:040F`–`042F`, `0C38`, `0C06`), which a
  General MIDI module has no use for.
- **The General MIDI data** (tunes `kTunes[t] + 4`, effects 523) uses only General MIDI's channel messages and
  controllers 0 (bank select: 0, once 8), 1, 7, 10, 21, 91, 93, 121, the sequencer's 110, 111, 114, 116, 117,
  and 60 three times (kept by 463).
- Implemented: `audio::Sequencer` (`src/audio/sequencer.*`, shared with Classic, start volume 90),
  `audio::midi_driver_sends` and `audio::GmSynth` (`src/audio/gm.*`): 463's filter, then TinySoundFont with a
  SoundFont (ADR-0015). Not checked in DOSBox: there is no reference for what a 1993 module sounded like.

## Checked in DOSBox
With a script since removed (it needed the reference engine's register dump): the original's OPL3
writes captured in DOSBox-X from before its sound start-up, ours from the same sound code on the same input; every key-on and
key-off compared with its channel's registers (frequency, timbre, levels, connection, `0x104`).
- **The music**: the logo's tune 0 for 20 s, **all 886 key-ons and key-offs the same**, register for register, times
  within 11 ms (ours are refresh-granular). Also the `[real-data]` test's hash of the register writes.
- **The effects** (music off by Alt+M, the route to level 1 with walking and jumps): every effect note the same, in
  content and order.
- **Together**: the same until the game's timing meets the music's. The original's music runs on while the emulated
  386 loads a slot (149–160 refreshes against our 148, [`timing.md`](timing.md#slot-loads)) and its game
  code falls somewhere between the driver's two interrupts of a refresh, so an effect or a fade meets the music
  a few refreshes earlier or later than in the original: notes in another order, on other OPL channels, a fade a few
  steps further. Nothing a player could tell apart; not a rule of the game.
- The music's rate: 2 XMIDI ticks per refresh, [`timing.md`](timing.md#checked-in-dosbox).
- **The title's demos** (the Classic music seemed to cut notes): from the game's start, ten
  minutes with no key pressed (the logos, the title, its six demo levels, their tunes and effects together), the
  original's capture (`tools/spotcheck.py opl --slot 39 --frames 36000`, run in parts for the control link) and
  Vanilla rules' writes (`lvr_tests "[.opl]"`), paired by `tools/oplcompare.py`: 15,226 of the original's 16,228
  notes have one of ours of the same pitch starting within 20 ms, and only 99 of them last more than 30 ms longer
  or shorter. The ones without a partner are in the windows a scene change falls in (the offset moves there,
  `timing.md`); within a scene every note pairs. The cut notes were the Remaster's volume, then turned down in the
  driver ([`../decisions.md`](../decisions.md): the volumes are gains on the sound made).
- **Notes the chip never started** (in the first 25 s, the logos' tune, a bass riff cut out,
  silent, and back at random, though the notes above were the same): the same writes as the original's, but a
  service call's reach the chip at one moment, and ymfm does two things only at its next sample. A `0x104` write
  gives a pair of channels its 4-operator use there (`assign_operators`), while a key-on goes at once to the
  operators the channel has then: a 4-operator voice set up and keyed on in one call keyed on two of its four,
  and the logos' bass, whose output is its last pair, stayed silent. And a key-on bit is looked at once a sample
  (`fm_operator::prepare`): off and on again between two, a note's envelope didn't start again. The original's
  writes take time (the driver reads the status between them; DOSBox and Nuked OPL3's buffered writes put
  them a couple of samples apart). `Opl3::write` makes a sample first after a `0x104` write
  and before a key change the chip hasn't seen is undone. Found by the sound itself: the original's first minute
  recorded in DOSBox-X (`tools/spotcheck.py wave --slot 39 --frames 3600`, Nuked's core), ours (`--dump 900 F
  --wav W`), lined up 5 s at a time and compared below ~250 Hz. Our writes played through ymfm at once: 17 stretches
  of 100 ms with under 35% of the original's bass in the logos' tune; spaced 2 samples apart, or through Nuked at
  once or spaced, or ymfm with the two samples made: 4, all at the title's scene change, where the two line up
  badly. The register writes are unchanged: comparing them, as above, could never show it.
