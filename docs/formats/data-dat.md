# DATA.DAT

> Status: container and compression **verified** against the reference release (2026-11-16) and locked down by
> the `[real-data]` tests in `src/assets/tests/test_data_dat.cpp`.
> Code: `src/assets/data_dat.*`. Tests: `src/assets/tests/test_data_dat.cpp`.

## Container (verified)
- The file starts with a table of little-endian `u32` offsets, one per entry.
- Entry count = `offset[0] / 4`. The reference file has **535** entries (first offset 2140).
- Entry *i* runs from `offset[i]` to `offset[i+1]`; the last entry runs to the end of the file. Offsets are ascending.
- There are no names, no magic number and no compression flag.

## Stored vs compressed (verified)
- Entries **0, 1, 380, 384, 529, 530, 531, 534** are stored as-is; all other 527 are LZSS-compressed.
- Nothing in the file says which is which. This is a fixed list (`kStoredEntries`).
- Evidence: with this list, every compressed entry decodes and uses up its input exactly. The stored ones
  "decode" too, but leave thousands of input bytes unused.
- The stored entries have recognisable structures:
  - 1, 384, 529, 531: 22018 bytes = `u16le` 0x1580 (5504) + 4 × 5504 bytes: 344×64 screen images.
  - 380, 530: 60546 bytes = `u16le` 0x3b20 (15136) + 4 × 15136 bytes: 344×176 screen images.
  - 534: 3 bytes, `20 0d 0a`.

## Entry 0: configuration
32 bytes that **`SETUP.EXE` writes**; `VIKINGS.EXE` reads them to `ds:86B0` at start-up. Layout read from both
executables (`u16le` words):

| Word | Meaning | Reference file |
|---|---|---|
| 0 | written as 2 by `SETUP.EXE`; not read by the game | 1 |
| 1 | music: `0x8000` = no sound; bit 0 = music off (Alt+M toggles it) | 0 |
| 2 | sound effects: `0x8000` = no sound; bit 0 = effects off (Alt+S) | 0 |
| 3 | driver index `d`: entry 455 + `d` ([`audio.md`](audio.md#sound-devices)) | 0 (PC speaker) |
| 4 | device variant `v` (0–4): music, effects and instrument bank | 0 |
| 5–8 | I/O port, IRQ, DMA, DRQ for the driver; `0xFFFF` = not given. Seen in DOSBox-X (2026-11-17): with the port `0xFFFF` none of the FM drivers 456, 457, 458, 459 and 460 starts (the game prints that the sound card could not be set up); with the port given (`0x388` AdLib, `0x220` Sound Blaster) they do, and IRQ/DMA can stay `0xFFFF` | all `0xFFFF` |
| 9 | **copy-protection fingerprint** of the machine `SETUP.EXE` ran on ([`game-flow.md`](../original/game-flow.md#start-up-00000000)) | `0x3DEB` |
| 10 | magic `0x6969` | `0x6969` |
| 11–15 | unused | 0 |

Consequence: entry 0, and so the hash of the whole `DATA.DAT`, differs between installations and even between
machines. The reference file has the same size as the one RyanMallon lists (1,570,444 bytes) but a
different MD5. Releases are therefore identified by a **content hash that leaves entry 0 out** (see
[`releases.md`](releases.md)).

## LZSS (verified)
- `u16le` header = **decompressed size − 1**, so 1..65536 bytes.
  - ⚠️ The Shikadi wiki says the header is the size itself, and mrcrowbar reads a `u32`. Both are wrong for this
    file. With "size" the output is one byte short: every XMIDI file then has chunk sizes that overrun the data by
    exactly 1 byte, and 244 entries leave 1–2 input bytes unused. With "size − 1", all 527 entries use their
    input exactly and all 63 XMIDI files are structurally valid.
- Then groups of up to 8 items, each group preceded by a **flag byte read least-significant bit first**:
  - bit = 1: literal, one byte.
  - bit = 0: reference, a `u16le`: low 12 bits = window position, high 4 bits = length − 3 (3..18).
- 4096-byte ring-buffer window, **zero-filled**, write position starts at 0.
- A reference copies byte by byte, and may read bytes it has just written (overlap = run-length encoding).
- Decoding stops as soon as the declared size is reached, even in the middle of a reference or group.

## Entry map
Sources: RyanMallon's `docs/chunks.txt` and liblv's level table (which match our release), and Albert Thalheim's
annotated list `docs/vikings_data.txt` in the same repo. That list was made from a **later re-release**: it
mentions a "Blizzard Entertainment" splash screen, and its numbering drifts by 1–2 entries after about 380. For
entries 0–378 its sizes match ours exactly in 366 of 379 cases. The other 13 differ systematically: stored
entries by +2 (header), 7 sprite entries by exactly +640 bytes, and the ending scene.

Everything not decoded yet, with evidence and guesses, is collected in [`unknowns.md`](unknowns.md).

| Entries | Type | Status |
|---|---|---|
| 0 | configuration from `SETUP.EXE`: sound device, copy-protection fingerprint | layout read from both executables ([below](#entry-0-configuration)) |
| 1 | HUD panel (a 344×64 full-screen-format image, palette entry 3 at colour 0) | **verified**; exported as `images/hud-panel.png` ([`level.md`](level.md#scenes)) |
| 2–14 | HUD: font/dialog glyphs (2), palette (3), Viking portraits (4–12), health (13), items (14) | sprites **verified** ([`graphics.md`](graphics.md#sprites)) |
| 15–219 | per world: palettes, then per level: tileset, transparency mask, metatiles, header, tilemap | **verified** for all 37 levels ([`level.md`](level.md), [`graphics.md`](graphics.md)); 173 is a palette the game never loads |
| 220–362 | sprites (raw, unpacked, packed32; 293 and 342 tiled), each group preceded by its 16-colour palette | **verified**: sprite catalogue ([`graphics.md`](graphics.md#sprite-catalogue)); 241 (a palette), 293 and 342 are never loaded by the game |
| 363–448 | scenes: title, logos, galaxy, starfield, village, game over; plus scene sprites | scenes and sprites **verified** |
| 418–422, 443, 444, 448 | **demo input recordings**: `(input word, ticks)` pairs ([`input.md`](../original/input.md#demo-recordings)) | identified from `VIKINGS.EXE` ([`game-flow.md`](../original/game-flow.md#demos)) |
| 449–453 | object database per world: 235 records, byte lists, script bytecode ([`script-vm.md`](../original/script-vm.md#the-object-database-449454)) | **verified**: every script decodes (`src/assets/script.*`) |
| 454 | the **scenes' object database** (same layout) | identified from `VIKINGS.EXE`; **verified** like 449–453 |
| 455–463 | **Miles Design sound drivers** (AIL 2; x86 code), entry 455 + driver index | identified ([`audio.md`](audio.md#sound-devices)); 460 and 463 read, their tables used ([`../original/sound-driver.md`](../original/sound-driver.md)) |
| 464–524, 528, 533 (63) | **XMIDI**: `FORM XDIR` + `CAT XMID` (`TIMB`, `EVNT`). Music (11 tunes × 5 device versions, 464–518), the **sound effects** (519–523, one collection per device), 524 and 528 (empty placeholders in the instrument-bank slots), 533 (MT-32 instrument upload) | verified (structure, [`audio.md`](audio.md)) |
| 525–527 | instrument banks (Miles Global Timbre Library): 525 = 526 for OPL, 527 for Roland MT-32 | container verified ([`audio.md`](audio.md)); the OPL timbres decoded ([`../original/sound-driver.md`](../original/sound-driver.md)), MT-32 not (unsupported) |
| 529–531 | HUD-area logo of the ship scene; starfield (top + HUD part) | verified; where they are shown read from `VIKINGS.EXE` |
| 532 | starfield palette (256 colours) | verified |
| 534 | 3 bytes, `20 0d 0a` | never loaded by the game |

`lv-export dump` writes every decoded entry to `gamedata/exported/raw/NNN.bin` plus `index.json` (sizes, type,
hash) for investigation.

## Graphics and levels
See [`graphics.md`](graphics.md) and [`level.md`](level.md).
