# Unknowns: what is not decoded, read or compared yet

> Purpose: one place for everything in the original files that we have **not** decoded, or have only guessed at,
> and for what the game does that has not been compared with the original. Each item says what we think it is,
> the evidence, and how to find out. When something gets settled, move it into its format or engine doc and remove
> it here.
>
> Reference release `vanilla-dos` ([`releases.md`](releases.md)). The
> list of entries in section 1 is locked by a `[real-data]` test (`src/assets/tests/test_catalog.cpp`, "every
> DATA.DAT entry is accounted for"): decoding one of them makes the test fail until this page and the test are
> updated.

**In short:** every `DATA.DAT` entry the game loads is decoded, every opcode the scripts use is understood, and
the engine plays the whole game. What is left: a few header and record bytes whose meaning is unknown and which the
engine doesn't read, some routines not read in detail whose effect is already matched, and **the paths never played
next to the original** (section 5).

## 1. Entries never loaded by the game (3)
**Every `DATA.DAT` load in `VIKINGS.EXE` is known** (2026-11-16): the two loaders (`0000:0982` decompresses an
entry, `0000:0CD8` copies a stored full-screen image into video memory) are called only with fixed numbers (1–14,
384, 454, 529, 531, 533, 443, 444), numbers computed from the sound configuration (drivers, banks, effects, music),
the demo table, and the lists in the level and scene headers. So an entry in none of these is never loaded.

| Entry | Size | What it is | Evidence |
|---|---|---|---|
| 173 | 48 | 16-colour palette | Valid VGA values (max 62). Sits right after the spaceship world palette (172). |
| 241 | 48 | 16-colour palette | Albert Thalheim's list: "used with sprites 242 to 245 (not called by main palettes)". We use it as the preview palette for 242. |
| 534 | 3, stored | The bytes `20 0d 0a` (space, CR, LF) | Possibly a leftover from the tool that built `DATA.DAT`. |

The sprite entries **293** and **342** (tiled) are never loaded either, but they are in the sprite catalogue so they
can be exported (section 3). The Miles drivers other than 460 and 463 (455–463) and the MT-32 data (522, 527, 533)
are loaded only for devices the game doesn't support ([`../decisions.md`](../decisions.md), ADR-0015); they are identified, not decoded.

## 2. Bytes whose meaning is unknown
The engine doesn't read these. It matches the original on every replay all the same, so if one of them matters, it
is on a path no replay takes.

### Level header (all 47 level and scene headers)
Decoded offsets are in [`level.md`](level.md#level-header). Left:

| Offset | Values seen | What is known |
|---|---|---|
| 0x10 (word) | 0 in the levels | Field `16` of the one object a scene starts with ([`objects.md`](../original/objects.md#level-start)); not read in a level as far as seen. |
| 0x14 (word) | | Read by the scripts (`ds:25C7`, 3 times, database 453, objects with field `1A` = `0xC0`). |
| 0x18 | almost always 1 | At the level start (`0000:1397`) an index into two 6-byte tables (`ds:853E`, `8544`) whose bytes go to `ds:03A6`/`03A8`; nothing in the code or the scripts reads those that we found. |
| 0x1a, 0x1b | almost always 5, 12 | Not read by any code found. |
| 0x1e–0x28 | `40 00 00 00 20 00 18 00 10 00 0c` in 45 headers | Only scenes differ. Not read by any code found. |
| 0x2d | 1 (46), 2 (starfield) | Not read by any code found. |
| 0x34–0x42 | `10 00 08 00 02 ff ff ff ff ff ff 00 85 00 01` in level 1 | **Known since 2026-11-18:** the SNES version's backdrop: its size, its chunks (blank in the DOS headers) and its scroll speeds ([`snes.md`](snes.md#level-headers)). The DOS game reads none of it. The byte at 0x38 (always 2) says the backdrop repeats. Left: why one DOS header has `0xfa` at 0x39. |
| flag `0x04` of 0x1c | the game-over scene only | Never tested by the code ([`game-flow.md`](../original/game-flow.md#header-flags-offset-0x1c-read-by-the-loader)). |
| sprite sets: `slot`, `sheet` | | The loader skips both (`167A`). Our reading (sheet size, a numbering without overlaps) is in [`level.md`](level.md); it doesn't match 253, 286, 297, 333, 334, 337, 343, 345, 372, 428, 437, 447. |
| packed sets: 3 bytes | always 4, 4, then 0 (the Vikings' six sets) or 1 | Not traced. Entry 368 (the game-over lightning, unpacked) sits in this list; the engine draws it from it ([`drawing.md`](../original/drawing.md)). |

### Object records and per-object words
- Record bytes `+5`, `+6`, `+D…` of the object databases, and the fields set at spawn not in the table of
  [`script-vm.md`](../original/script-vm.md#object-fields) ([`objects.md`](../original/objects.md#open)).
- The level objects' `flags` and `arg` mean what each type's script makes of them (the engine's own uses:
  [`objects.md`](../original/objects.md): `0x0800` always active, `0x0100` created again, the drawing bits).
- `ds:0429`–`042D`: per-Viking words; `0429` set adds the shield to the HUD's health frame ([`drawing.md`](../original/drawing.md)); the others not traced.

### Graphics
- **Packed32 tails:** 640 bytes of `0xff` after the last frame in 44 of 56 sets: the size of the largest frame
  (4 planes × 32 rows × (1 mask + 4 bytes)). Room for a frame built at run time? Nothing reads it that we found.

### Audio
- Controllers 16, 17, 20, 21, 32–37 and 58–63 in the XMIDI data ([`audio.md`](audio.md)): driver 460 doesn't act on
  them (the OPL capture matches register for register); driver 463 passes them to the MIDI module, and the game's
  Modern sound sends them as it does. What the composer meant by them is unknown.

## 3. Choices of ours (export and previews only)
These don't touch the game: the engine draws with the piece sizes and colours the scripts give
([`drawing.md`](../original/drawing.md)). They only shape what `lv-export` writes.

| What | Our choice |
|---|---|
| Sprite frame sizes in the sheets | `kSpriteCatalog` frame runs, found by eye ([`graphics.md`](graphics.md#frame-sizes-are-not-in-the-data)) |
| Preview palettes for packed32 and tiled sprites | From Thalheim's list where it names one; picked by eye for 235, 236, 293 (292; the list says 291 or 292), 299, 310, 322, 323, 354–359, 403–412, 414–417, 441, 442 |
| Entries 293 and 342 (tiled, never loaded) | 293: a lavafall and three frames of a lava splash (our reading of the picture); 342: the bullseye target, upright and knocked flat (the bullseye object, type `0xc2` in 449, has sprite entry `0xffff`). Frames side by side. |

## 4. `VIKINGS.EXE`: routines and variables not read in detail
The engine matches the original where compared (section 5), so these are gaps in the write-up, or code that may
matter on paths not compared yet.

| What | Where | Why it may matter |
|---|---|---|
| Joystick axes: does the reading (`79FB`, a PIT count) grow to the right and down? | [`input.md`](../original/input.md#joystick) | If it does, the gamepad's axes are the arrows' mirror image. Not checkable in DOSBox yet (the control channel has no joystick). |
| Who sets the scroll locks `0394`/`0396` | [`camera.md`](../original/camera.md#open) | No code but the level start writes them and no script names them: perhaps never set. |
| Where the foreground tiles are drawn (the blitters' masks, or `406D`); `406D` (points redrawn, `ds:8734`); `0C83:00C1` with `ax` = −2; which routine draws which HUD portrait | [`drawing.md`](../original/drawing.md#open) | Presentation only; the pictures match. |
| `ds:92FF` and `cs:28A8` in the retrace handler | [`timing.md`](../original/timing.md) | Presentation only. |
| The retry conversations: which comes when (global `027A` = 5, 8, `0B`, `0F`, `32`, `33`); which scripts show texts 0 and 1 | [`dialogue.md`](../original/dialogue.md#open) | Written from the scripts; none was compared with the original. |
| The box frame and font drawing (`2388`, `24C5`) and the exact anchor rules | [`dialogue.md`](../original/dialogue.md#open) | The boxes match the original's on the paths compared. |
| Slot 42 (header 382, the title copy) | [`game-flow.md`](../original/game-flow.md#slots) | No code sets it as a constant; no path checked loads it. Probably unused. |
| Opcode `A1` | [`script-vm.md`](../original/script-vm.md) | Its operand is whatever the previous handler left in a register. No script uses it. |

## 5. What has not been compared with the original
The engine was matched in two steps. A reference engine ran the original's own bytecode on services read from
`VIKINGS.EXE` and was compared with the original in DOSBox, state for state; the engine, written by hand, was then
matched against it on every replay, and the reference engine retired. Since then `tools/lockstep.py` sets the
engine's picture beside the original's directly ([`../dosbox.md`](../dosbox.md)).

**Compared, the reference engine against the original:** the attract cycle (all six demos), the village intro,
the menu, the password screen, the pause menu, the inventory, the quit box, the galaxy, the game-over scene, the
ending, and each of the 37 levels for about 480 steps of one fixed key pattern; a camera sweep over every level,
so that every placed object comes into view and runs; homing shots, the ladder's top cells, a hint box, a
conversation, the object table full; levels 2, 6 and 7 finished through their exits from a person's recorded
playthroughs. Those runs covered 28–34% of the script instructions per level database and started 72% of the
object types the levels place.

**Compared since, the engine against the original:** the title's menu keys and a typed password, the cheat's
level keys with all three Vikings and with one gone, a Viking dying in an electric field, and a level loaded
again (`replays/level-keys*.replay`).

**Never played next to the original:**
- The bosses, the later parts of most levels and rare switch combinations: the engine matches the reference
  engine there on the reach replays, but the reference engine itself ran them alone.
- What no replay reaches in the engine (`./scripts/replay_coverage.sh` lists the lines): written from the scripts'
  listings and guarded only by unit tests, if at all.
- The typed keys `Q` and Backspace and the yes/no question answered by `Y` / `N`.

**Not comparable, by nature:**
- **The joystick** (above).
- **The 52 opcodes no script uses** (`scripts/usage.txt` of `lv-export`): nothing plays them.
- **Modern sound** (General MIDI): there is no reference for what a 1993 module sounded like
  ([`sound-driver.md`](../original/sound-driver.md#driver-463-the-midi-driver-modern)).
- **Other releases:** only one copy of the files was checked. Its `DATA.DAT` MD5 differs from the one RyanMallon
  lists, at the same size; entry 0 holds the sound setting and a copy-protection fingerprint of the
  machine ([`data-dat.md`](data-dat.md#entry-0-configuration)), so presumably nothing else differs.

**Known differences** (left as they are; [`../quirks.md`](../quirks.md), "Drawing"): the game-over lightning's two
frames pile up in the original's video pages and the engine draws each frame on its own; and a stray white dot in
Erik's portrait when he is gone, a write of the original's that goes astray, which the engine doesn't copy.
