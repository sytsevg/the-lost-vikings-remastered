# Sound: what the game asks of the sound driver

**Status:** observed (static disassembly of `VIKINGS.EXE`, `0000:7561`–`7912` and `08C8`, 2026-11-17). The
simulation's part (the flags, the loaded tune) is in `src/lvr/play.*` and was compared with the original in lockstep
(Alt+S / Alt+M, the music actions); the player itself is `src/audio/` (the sequencer, the OPL3 and General MIDI
outputs, ADR-0015). Formats in [`../formats/audio.md`](../formats/audio.md); the rate in
[`timing.md`](timing.md#audio).

The game drives the Miles AIL (segment `0BAD`) through a handful of calls. Everything else (timing, voices, the
instruments) is the driver's.

## Start-up (`7561`)
Register and initialise the driver (`0BAD:0A67`, `0B1F`, `0B45` with the port, IRQ, DMA from entry 0, `0B4B`), give it
a timbre cache (`0C9F` → size, memory from `0D96`, `0CA5`). `ds:A39A` = 1 once done. If the driver can't be
registered or initialised: a message that the sound card could not be set up (`ds:2E05`), a 2-second pause, and **no sound**: `0302` =
`0304` = `0x8000`. With the MT-32 (driver index 8, variant 3) it prints a line that asks the player to wait and
plays entry 533 once (the instrument upload). Without sound (`0302 & 0304` bit 15) the game's own 125 Hz timer is
installed instead (`7693`: `0BAD:07C3`, `09D6` with 125).

## Flags
| Word | Bit 15 | Bit 0 |
|---|---|---|
| `ds:0302` | no sound device (from entry 0) | music off (Alt+M) |
| `ds:0304` | the same | effects off (Alt+S) |

## Playing a sequence (`76BD`)
`76BD` (sequence number `ax`, file segment `bx`, player slot `si`): register the sequence (`0C93`) into the slot's
handle (`ds:990C + si`), then ask the driver for each instrument it still needs (`0CAB` returns bank × 256 + patch,
`0xFFFF` when done), look it up in the loaded bank (`7512`) and hand it over (`0CB1`); then start it (`0CC9`).

**Slots:** `si` = 0 the music (sequence 0 of the tune's file, segment `ds:2E6B`); 2, 4, 6, 8 the four **sound
effects** (sequence *n* of the effects' file, segment `ds:2E6D`; the effect number kept in `ds:9916 + si`).

## Sound effects
- **Play effect *n*** (script/animation opcode `02`, `77B2`), only with effects on (`0304` = 0): try slots 8, 6, 4,
  2 in that order. A free slot (handle `0xFFFF`) takes it. A slot whose sequence has finished (status `0CDB` ≠ 1,
  "playing") is released (`0C99`) and takes it. If all four are playing, the effect is **dropped**.
- **Stop effect *n*** (`04`, `782A`; `D7`, `787F`, the same with a word the original skips): every slot playing
  *n* is stopped (`0CCF`), released and freed.

## Music
- **The header's music actions** (byte 4 at the level start, `7749`, byte 6 at its end, `774F`; table `ds:A37A`):
  | Action | Routine | What |
  |---|---|---|
  | 0 | `7791` | load the tune (below), and with music on play it **from the start** (even if it was already playing) |
  | 1, 4 | `77B1` | nothing |
  | 2 | `78F1` | with music on, **fade out**: the music's volume to 0 over 1,000 ms (`0CED`) |
  | 3 | `775D` | load the tune only |
- **Load the tune** (`775D`), with a sound device: the header's tune byte (`25B8`) unless it is the one loaded
  (`ds:25AF`, which it then records): entry `kTunes[t]` (`ds:A384`) + the device variant (`ds:86B8`) into the
  tune's segment.
- **Restart the music** (opcode `D5`, `78D6`): with music on, sequence 0 again from the start.
- **Fade it out** (opcode `D6`): `78F1`.

## Alt+S / Alt+M (`08C8`, after the tick)
With a sound device and Alt held (key-state `ds:91A4`):
- S (`918B`): the byte cleared, `0304` bit 0 toggled. Turning effects **off** stops and frees every effect slot
  (2–8).
- M (`919E`): the byte cleared, `0302` bit 0 toggled. Music **on**: the tune starts again from the beginning
  (`76BD`, slot 0); **off**: it is stopped and released.

The key-state bytes are set by each make code of the key (a press, and every typematic repeat) and cleared by its
release or by `08C8` here, so holding Alt+S toggles again at every repeat. Checked by lockstep (2026-11-17): Alt+S
held over six steps (toggled at each, as the lockstep writes the byte at every step), Alt+M, Alt+S: `0302`/`0304` as
the original's.
