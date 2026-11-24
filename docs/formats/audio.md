# Audio

> Status: container and event structure **verified** (2026-11-16) for all 63 XMIDI entries (544 sequences) and the
> three instrument banks, with a Python prototype over the reference release. How the drivers play them (timing,
> voices, the OPL timbres) is in [`../original/sound-driver.md`](../original/sound-driver.md).
> Code: `src/assets/audio.*` (XMIDI sequences, `TIMB`, `EVNT`, the timbre banks), `[real-data]` over all 63 XMIDI
> entries; played by `src/audio/` ([ADR-0015](../decisions.md): OPL3 and General MIDI).

The DOS release has **no digitised sound**. Music and sound effects are both **XMIDI** (the Miles Audio Interface
Library's extended MIDI), played by a Miles sound driver that `SETUP.EXE` chooses; the FM and MT-32 instruments are
in two **Global Timbre Library** banks. Everything is in `DATA.DAT`:

| Entries | What |
|---|---|
| 0 | sound configuration written by `SETUP.EXE`: driver, device variant, port, IRQ, DMA ([`data-dat.md`](data-dat.md#entry-0-configuration)) |
| 455–463 | Miles sound drivers (x86 code), one per driver index |
| 464–518 | music: 11 tunes × 5 device variants ([below](#music)) |
| 519–523 | sound effects: one collection per device variant ([below](#sound-effects)) |
| 524–528 | instrument bank per device variant: 524 and 528 are empty XMIDI placeholders, 525 = 526 the OPL bank, 527 the Roland MT-32 bank |
| 533 | the MT-32 instrument upload |

## Sound devices
Read from `SETUP.EXE` (the tables behind its menu) and `VIKINGS.EXE` (the loader, `0000:2B0E`, and the music player,
`0000:7765`). The configuration holds a **driver index** `d` and a **device variant** `v` (0–4). The game loads:

| What | Entry |
|---|---|
| driver | 455 + `d` |
| sound effects | 519 + `v` |
| instrument bank | 524 + `v` |
| tune `t` | `kTunes[t]` + `v` (table below) |

| `SETUP.EXE` choice | `d` → driver entry | `v` | Music, effects, bank |
|---|---|---|---|
| No Sound | 0 → 455 | 0 | none (config words 1 and 2 = `0x8000`) |
| PC Speaker | 0 → 455 | 0 | v0: one-voice versions, 519, no bank (524 is empty) |
| Adlib | 1 → 456 | 1 | v1: OPL2, 520, 525 |
| Adlib Gold | 2 → 457 | 2 | v2: OPL3, 521, 526 |
| Sound Blaster | 3 → 458 | 1 | v1 |
| Sound Blaster Pro | 4 → 459 | 2 | v2 |
| Pro Audio Spectrum | 6 → 461 | 1 | v1 |
| Pro Audio Spectrum 16 | 7 → 462 | 2 | v2 |
| Roland MT-32 | 8 → 463 | 3 | v3: MT-32, 522, 527, plus the upload 533 |
| Roland SCC-1/General MIDI | 8 → 463 | 4 | v4: General MIDI, 523, no bank (528 is empty) |

- Driver 5 (**460**, Sound Blaster Pro FM) is never chosen: `SETUP.EXE` maps Sound Blaster Pro to driver 4 (459), and
  shows an existing configuration with driver 5 as "Sound Blaster Pro" too. 459 is byte-identical to 458
  (Sound Blaster FM), and 461 to 456 (AdLib).
- 462 (Pro Audio Spectrum 16) needs Media Vision's MVSOUND.SYS: at offset `1429` it asks for it through `int 2Fh`
  (`AX=BC00h`, `BX=3F3Fh`, expects `BX^CX^DX = 'MV'`, then `BC03h` for version ≥ 10 and a function table it calls),
  and fails without it. Seen in DOSBox-X with its PAS16 emulation (2026-11-17).
- General MIDI uses the MT-32 driver (an MPU-401 driver) with its own music versions.
- All nine drivers declare a 120 Hz service rate ([`../original/timing.md`](../original/timing.md#audio)).

## XMIDI container
An IFF tree. Chunk ids are 4 ASCII bytes and chunk sizes are **big-endian** `u32`, not counting the 8-byte chunk
header. All chunk sizes are even (see `EVNT` below). Every one of the 63 entries has exactly this shape:

```
FORM XDIR
  INFO            u16le: number of sequences (2 bytes)
CAT  XMID
  FORM XMID       one per sequence, `INFO` of them
    TIMB          optional: the instruments the sequence uses
    EVNT          the events
```

- **`TIMB`**: `u16le` count, then count × (`patch` `u8`, `bank` `u8`).
- **`EVNT`**: the event stream (below). In 321 of the 544 sequences the stream has an odd length and is followed by
  one `0x00` byte that is counted *inside* the chunk size, so that the size is even. Readers must stop at end of
  track, not at the chunk end.

## Event stream (`EVNT`)
Differences from a standard MIDI track, all checked on every sequence (every stream parses with exactly these rules
and ends with end of track):
- **Delays:** any byte below `0x80` is a delay in ticks; consecutive delay bytes add up. There are no variable-length
  delta times.
- **Note on** (`9n`): note, velocity, then the note's **duration** as a variable-length number. There are **no note
  off events** (`8n` never occurs).
- **No running status:** every event has its status byte.
- Meta events (`FF type length data`) as in standard MIDI. The stream ends with `FF 2F 00`.

Counts over all 544 sequences: 54,949 notes, 12,456 pitch bends (`En`), 9,827 controller changes (`Bn`), 1,002
program changes (`Cn`), 8 polyphonic aftertouch (`An`); no channel pressure and no SysEx. Meta events: text (`01`,
2,192), tempo (`51`, 539), time signature (`58`, 538), key signature (`59`, 277), SMPTE offset (`54`, 259), MIDI
port (`21`, 175), marker (`06`, 69).

Controllers used: 0, 1, 7, 10, 91, 93 and 121 (standard MIDI); 16, 17, 20, 21, 32–37 and 58–63 (meaning unknown);
and the XMIDI controllers **110** (channel lock, 944×), **111** (channel lock protect), **114** (patch bank select,
601×), **116** / **117** (for-loop / next-loop, 68 / 64×). The names of the XMIDI controllers come from the public
XMIDI documentation; their behaviour as driver 460 runs it is in
[`../original/sound-driver.md`](../original/sound-driver.md). Driver 460 doesn't act on the controllers of unknown
meaning (the OPL capture matches register for register); 463 passes them to the MIDI module.

Timing: XMIDI ticks run at a fixed **120 per second** per the XMIDI documentation. All nine drivers declare a 120 Hz
service rate; in the game the timer is locked to the display, so the driver actually runs 2× per refresh (119.43 Hz, measured),
exactly **6 XMIDI ticks per game tick** ([`../original/timing.md`](../original/timing.md#audio)). Tempo
events are present with many different values (most often 500,000 and 600,000 µs per quarter note); the driver
only sets its beat counter from them and never changes the playback speed
([`../original/sound-driver.md`](../original/sound-driver.md)).

## Music
The game plays music by **tune number** `t` (header byte `0x05`, [`level.md`](level.md#level-header)) from a table
in `VIKINGS.EXE` (`ds:A384`), plus the device variant `v`: entry = `kTunes[t] + v`. So every tune is stored in
**five versions**, one per device variant ([above](#sound-devices)):

| Tune | Entries (v0–v4) | Plays in | Length (ticks), v1–v4 |
|---|---|---|---|
| 0 | 494–498 | Interplay logo, carried on through the Silicon & Synapse logo and the title | 3,236–3,591 |
| 1 | 489–493 | the village scenes; **identical copies of tune 5** | 5,246–5,318 |
| 2 | 464–468 | spaceship world (levels 1–4 and 34–37) | 6,866–6,895 |
| 3 | 469–473 | world 2 (levels 5–11) | 5,733 (v1, v2), 6,364, 7,798 |
| 4 | 474–478 | world 3 (levels 12–17) | 8,006–8,095 |
| 5 | 479–483 | world 4 (levels 18–25) | 5,246–5,318 |
| 6 | 499–503 | galaxy | 2,123, 2,123, 1,013, 1,063 |
| 7 | 484–488 | world 5 (levels 26–33) | 11,363–11,391 |
| 8 | 504–508 | ship | 6,839 |
| 9 | 509–513 | starfield | 2,824–2,826 |
| 10 | 514–518 | game-over scene (slot 37): all five empty, so **silence** | |

- **v0 (PC speaker)** is the 102-byte empty placeholder for every tune except 0 and 6: 494 is a one-channel piece
  (a text event names its one voice), 499 uses bank 0 only. So with a PC speaker only the logos/title and galaxy music play.
- The four other versions' instrument use matches their device (all tunes):

| Variant | Banks in `TIMB` | Notes |
|---|---|---|
| v1, OPL2 | 0, 1, 127 | uses patch bank select (controller 114) |
| v2, OPL3 | 0, 1, **2**, 127 | the only version that uses bank 2 (the bank with the 25-byte, presumably 4-operator, timbres) |
| v3, MT-32 | mostly **1** and 127 | 497 asks for bank-1 patches 50 and 59, which only the MT-32 bank has |
| v4, General MIDI | 0 and 127 only | no patch bank select (except once, in 483) |

Header bytes `0x04` (when the level starts, `0000:117A`) and `0x06` (when it ends, `0000:0157`) choose what happens
to the music (`ds:A37A` jump table):

| Value | Action | Seen |
|---|---|---|
| 0 | load tune `0x05` (unless it is already loaded) and play it | `0x04` in 43 headers |
| 1, 4 | nothing: the current music carries on | `0x04` in the Silicon & Synapse logo and the title (2 headers); `0x06` in the Interplay and Silicon & Synapse logos |
| 2 | fade out over 1,000 ms | `0x06` in 45 headers |
| 3 | load tune `0x05` without starting it | `0x04` in the village demo and the starfield |

Other music entries:
- **533** (420 bytes): 64 program changes on MIDI channel 11 and no notes; its `TIMB` asks for bank 1, patches 0–63,
  exactly the 64 instruments of the MT-32 bank. The game plays it once at start-up when the device is an MT-32
  (variant 3), after printing a line that asks the player to wait: it makes the driver upload all
  custom instruments.
- **Placeholders**: 15 identical 102-byte entries (464, 469, 474, 479, 484, 489, 504, 509, 514–518, 524, 528), each
  one sequence with no notes, just two leftover text events and the usual tempo and signature events. They
  are the v0 (PC speaker) versions of 8 tunes, all five versions of tune 10, and the v0 and v4 instrument banks.

Music uses MIDI channels 2–10 (10 = percussion, with bank 127); only 533 uses channel 11.

## Sound effects
Entries 519–523 each hold one sequence per sound effect: 94 in 519, 98 in the others. **Sequence *k* is the same
effect in every collection**: where 519 and 522 both name a sequence, the names agree (checked on sequences 0, 20, 40 and 93; some
names are cut to ten characters).

| Entry | Sequences | `TIMB` banks | MIDI channels | Names |
|---|---|---|---|---|
| 519 | 94 (11 with `TIMB`) | 0 (7 instruments) | 1–2 | 53 named, by what the effect is for |
| 520 = 521 | 98 | 0, 1 | 2, 11–15 | none |
| 522 | 98 | 0, 1, 127 | 2–6, 10–16 | 82 named |
| 523 | 98 | 0 | 11–16 | text events are leftovers (the same two labels in most sequences) |

- The collection is chosen by device variant: **519 PC speaker, 520 OPL2, 521 OPL3 (identical to 520), 522 MT-32,
  523 General MIDI** ([sound devices](#sound-devices)). This fits the contents: 522 asks for 15 bank-1 instruments
  (patches 46–63) that only the MT-32 bank has; 523 uses bank 0 only, without bank select.
- The scripts play an effect by its sequence number (`0000:77B2`).
- Effects play mostly on **MIDI channels 11–16** (519: 1–2), which the music never uses (it plays on 2–10), but
  520 and 522 also use a few music channels. The driver shares them through channel locking (controller 110) and
  a pool of 20 OPL voice slots ([`../original/sound-driver.md`](../original/sound-driver.md)).
- The names are text meta events (`FF 01`); there are no track-name events. Many are generic (a tool's default names)
  or copied from another sequence, so they are hints, not labels.

## Instrument banks (Global Timbre Library)
Entries 525 (= 526) and 527:

```
records    6 bytes each: patch u8, bank u8, offset u32le (from the start of the entry)
terminator ff ff
timbres    back to back up to the end of the entry, each starting with its u16le size (counting the size field)
```

| Entry | Records | Banks: patches, timbre size | Used for |
|---|---|---|---|
| 525, 526 | 276 | 0: patches 0–127, 14 bytes each; 1: patches 0–45, 14 bytes; 2: patches 0–44, 44 of 25 bytes and 1 of 14; 127: 57 patches between 27 and 84, 14 bytes | OPL (FM): 525 for v1 (OPL2), 526 for v2 (OPL3) |
| 527 | 64 | 1: patches 0–63, 248 bytes each (an MT-32 timbre is 246 bytes) | Roland MT-32 (v3) |

- The 14-byte timbres are 2-operator FM voices and the 25-byte ones (bank 2, used only by the v2 music, the OPL3
  devices) 4-operator OPL3 voices; bank 127 is the percussion, looked up by key on MIDI channel 10 (in all 35
  sequences whose `TIMB` lists bank-127 instruments, those patch numbers match the keys played there). The timbre
  bytes: [`../original/sound-driver.md`](../original/sound-driver.md), "Timbres".
- Every (bank, patch) that a `TIMB` chunk asks for is in the OPL bank, except 4 in three songs (472: (127, 59);
  488: (127, 85); 497: (1, 50), (1, 59)) and the bank-1 patches 46–63 that 522 and 533 ask for (those are in the
  MT-32 bank). 63 OPL instruments are never asked for.
- The OPL bank is stored twice because the game loads bank `524 + v`: one copy for each OPL variant.
