# The game loop: one tick, the level start, the dialogue player, demos

**Status:** observed (static disassembly of `VIKINGS.EXE`, 2026-11-17). The engine's: `src/lvr/play.*` (a step) and `world.*` (a tick). **Compared with the game** through the reference engine (retired 2026-11-23; 2026-11-17, [`../dosbox.md`](../dosbox.md#lockstep-comparison)):
tick by tick the same state as the original from the logo through the title, the menu, the village intro, level 1's
opening and a walk, and the first two attract demos.

The main loop (`0000:001E`) runs one **tick** per pass in three phases of one display refresh each
([`timing.md`](timing.md#game-loop)). Only the game code is listed here; the waits, drawing and flips are the
presentation's.

## One tick
| Phase | Routine | What |
|---|---|---|
| A | `2352` | read the input word ([`input.md`](input.md)) |
| | `2D72` | demo playback: the recording drives the keyboard word (below) |
| | `02AD` | during a demo, the pause bit ends it |
| | `041C` | the pause menu (Esc/P newly pressed, in a level, no request pending, no dialogue queued): a loop of its own, [`menus.md`](menus.md) |
| | `0138` | act on the state requests `ds:0334` (below); a slot load happens **here, in the middle of the tick**, and the rest of the tick runs on the new level |
| | `1BA5` | the inventory screen (Tab newly pressed, header flag `0x01`): a loop of its own, [`menus.md`](menus.md) |
| | `2E79` | switch Viking: Ctrl/Home (`0x0020`) the previous, Ins/PgUp (`0x0010`) the next, skipping those whose field `0A` is 0. The new one's first sprite piece is shown; `ds:034E`/`0350` = 5. Not in a scene (header byte 7 = 0) |
| | `0813` | the current Viking off the screen (more than 12 pixels outside the 320 × 176 view; the vertical test only when the camera isn't at the top): its **input is dropped** this tick (`03B6`/`03B8` = 0). Otherwise `07A2`: a Viking chosen since the last time **blinks** for 21 ticks (its first piece hidden, flag `0x2000`, in 5 of every 16 ticks); the one before is shown again |
| | `673C` | the strips of the object list that scrolled into view ([`camera.md`](camera.md#strips-0000673c-every-tick-before-the-scripts)) |
| | `4207`, `386B`, `625D`, `5546`, `3916` | scripts, movement, ground snap, contact pass, attached objects ([`terrain.md`](terrain.md), [`hits.md`](hits.md)) |
| | `064B` | camera, step 1 of 3 |
| | `2FC6` | sprites, step 1 of 3 |
| | draw | the drawing's bookkeeping in the piece arrays (`0C83:15D5`, `156C`): see below; the same after phases B and C |
| B | `2E16` | the current Viking dead (field `1A` bit 15): the next living one (`034E`/`0350` = 5); none: `03C2` = `0xFFFF` and request 2 (game over) |
| | `5530` | hit pass |
| | `0704`, `2FCB` | camera and sprites, step 2 |
| | `2D2C` | screen shake: each axis with ticks left (`03A2`/`03A4`) counts down and toggles its offset (`039E`/`03A0`) by its amount (`039A`/`039C`); otherwise the offset is 0 |
| C | `0753`, `3C0C`, `2FD0` | camera step 3, objects out of view, sprites step 3 |
| | `1792` | the HUD: presentation, except that its health part (`1B0B`) keeps each Viking's field `0A` in `0423`–`0427` (and `0429`–`042D` in `042F`–`0433`), which the level start gives back to the Vikings and the game-over scene reads (`record_health`); also in the level start's tick, and in every pass of the inventory. What it last drew stays in the mirror too: the portraits' states (`0435`–`043F`, `20FF`), the items (`03FC`, `2199`) and the chosen squares (`041A`, `205B`) (`update_hud`; the level start's `17AD`: `start_hud`) |
| | `01BE` | palette animation: the counters (simulation), the colours rotated (presentation, `kPaletteRotate`); below |
| after | `08C8`, `0350` | Alt+S / Alt+M (the sound's); Alt+X, Alt+Q, F10: quit at once, or the quit box ([`menus.md`](menus.md#the-quit-box-00000350041b)) |
| | `086F` | **the dialogue player** (below) |
| | `00EA`–`012D` | F4 (`int 3`), F5/F6 (debug build only) |

**Sprites, sub-frame steps (`2FE5`):** for every object with sprite pieces, from the highest slot down, the move since
`386B` (position − previous position) is split into ⌈a/3⌉, ⌊(a+1)/3⌋, ⌊a/3⌋ and added to its pieces' x/y (piece
arrays `+0x200`/`+0x300`), which are marked for redrawing (`0x202`). Both loops test at their end: with no object at
all the original looks at slot offset −2, and an object whose first piece isn't below its end still moves that one.

**The drawing writes back:** each of the three draws counts down every piece's erase count (high byte of the redraw
word `114D`; a removed piece gets `0x0400`) while moving its last drawn position to the erased one, and counts down
the redraw count (low byte) of every piece shown, keeping its position as the last drawn. A removed piece's slot in
its pool is free for a spawn only when the whole word is 0 again, after four draws, so this belongs to the
simulation (`settle_drawn_pieces`). Without it every removed shot leaked its piece and level 1's gun stopped firing
after a while. Dialogue command 2 (clear) draws twice more.

**Palette animation** (`01BE`, also in every pass of the wait-for-key loop; the counters reloaded by the retrace
handler's job `0FFC`): the level start (`1383`, `12AE`, `133A`) reads the header's list after the object list and
the palette list into `ds:2583` (flags byte: bit *i* = animation *i* runs) and, per animation (8 at most), `2584`
delay, `258C` counter, `2594` from colour, `259C` to colour. Each tick every running animation with a non-zero
counter counts down; at 0 its range rotates by one entry (from ≤ to: each colour moves up one and the last comes round
to `from`; from > to: the other way), in both the base and the current palette, and the counter is reloaded from the
delay. Level 1 runs animations 1–5 (its flickering screens: colours 120–127, 172–175, …), the game-over scene 0–4
(fire and water: 54–63, 67–79, 83–95, …) every 2 ticks.

## State requests (`0000:0138`, `ds:0334`)
- Bit 4 (set by dialogue command 4): the **wait-for-key loop** (`0164`; its first pass runs at once, palette
  animation and flips included, but can't end): every pass reads the input, and ends when
  an action key (`0xC0C0`: E, S, D, F/Space/Enter) is newly pressed; otherwise the palette animation, three
  flips (each recording the camera as shown in `257B`/`257D`, as every flip does) and again. Its first
  read follows the previous one at once, so it can't end there.
- Bit 1 (next slot: opcode `0F`, a demo's end) or bit 2 (game over: `ds:25C9` = 37): clear the requests, the music
  action of header byte 6, the **game-over pass** (`032F` + 1, every object's script runs once from its restart
  entry), then load the slot (`1080`). Bit 1 wins if both are set.

## The level start (`0000:1080`, `1133`)
1. Reset: the game-over pass, the camera locks `0394`/`0396`, `0372`, the dialogue queue, the shakes, `03D6`,
   `0398`, `0392`, `0348`, the blink, the input words and the **keyboard word** `ds:86DE` (a key held across a level
   change counts only when pressed again). Tints 0 (after the fade out `0FA0`).
2. `16E3`: is the slot played from a recording (below)?
3. `03C2` = `03C4` = 0; `ds:044D`–`204C` cleared (the sprite pieces, every per-object array, the hit table, the
   dialogue queue), the global flag bits `0356`–`0365` cleared.
4. `1133`: previous slot = current, current = `25C9`; the object database (if the slot has one and it isn't loaded)
   and the header; the tints `0342`–`0348` cleared; unless the slot is 37 (game over), **the Vikings' items and
   health are reset** (`2CE4`: `0414`–`0418`, `03E4`–`03FB` = 0, `0423`–`0427` = 6, `0429`–`042D` = 0); in a level
   with the HUD, `17AD` then draws it and `1B0B` records the (still cleared) fields in `0423`–`0427`, so the Vikings
   start with health 0 and their scripts give them theirs (level 1: 6, three dots, from the first ticks); the slots
   emptied (`37F1`: code segment 0, field `42` = `0xFFFF`); sprite tables, pieces, the Vikings
   ([`objects.md`](objects.md#level-start)); the camera ([`camera.md`](camera.md)); the music action of header
   byte 4; the always-active and in-view objects.
5. `15D2`: one tick without input: from the scripts to phase C as above, but without `2E16` and `2D2C`, and with a
   **fourth draw** after phase C (`165F`–`1677`; it matters when a piece moved in that tick: levels 8 and 11).
6. Tints 0 (after the fade in `0F5D`), input cleared.

A new game (`2CA3`, then `2C12`, `08B8`): demo index and mode, `0336`, `034A`, `034C`, the requests, `0331` = 0,
`25AF` (the tune loaded) = `0xFFFF`, the password `STRT`, the Vikings' items and health as above, the first slot 39.
The data segment otherwise starts as the EXE has it: in `ds:0000`–`2E7F` only
`2B66`–`2E5C` has values (the attract tables, the camera's thirds `2B80`, the dialogue handlers `2BA6`, …).

## The dialogue player (`0000:086F`)
After the tick, while the queue's read position `2B64` isn't at its write position `218F`: show the current
Viking's first sprite piece, run the next command (the box, text, clear and colour commands are drawing; command 4
sets request bit 4), read the input, act on the requests. Request 4 there starts the wait-for-key loop, so a
conversation of several boxes plays out **between two ticks**, a key press each. When the queue is done it is reset
(both positions 0). A level change requested meanwhile empties the queue.

**In the engine** `Play::step(keys)` is one tick of input. While a text waits, it is one pass of the wait loop
instead, and the tick doesn't run; when the key comes, the next line is shown in the same step.

## Demos
`16E3`, at every level start: the demo random seed `0352` = `0x1E`, `03CE` = `03D0` = 0. Playback if the slot is
45, 46 or 47, or `03CC` = 1 (an attract demo was asked for): `0398` = 1, `03CC` = `0x8000`; the recording is 443
for slot 43, 444 for 44–47, otherwise the slot **and** the recording come from the attract tables at demo index
`03D4` (`2B66`: slots 2, 9, 8, 15, 23, 25; `2B74`: entries 418, 419, 420, 422, 421, 448). It is loaded to `ds:2193`
(position `2191` = 2), and the Vikings' items and health are reset. `03CC` = `0x8002`: only that reset; `0x8001`
(recording): the buffer cleared; anything else: `03CC` = 0.

`2D72`, each tick after the input read, while playing back:
- the pause bit read this tick: count and word = `0xFFFF`;
- count ≠ 0: count − 1, the keyboard word |= the word;
- else the next pair: keyboard word = its word, count = its count − 1.

So the first tick reads nothing, then each pair's word is read for its count of ticks, then the end marker's
`0x1000`, which `02AD`/`02F0` take as the end ([`game-flow.md`](game-flow.md#demos)). Recording 418 (level 3) is
442 ticks long, so its demo ends at the 444th tick and loads the logo. The scripts see `03CC` and skip the level's
password box and opening conversation.

## Golden replays
The first replays (2026-11-17) were paths of the reference engine that `tools/lockstep.py` had found the same as
the original's, state for state: from the logo with no input through the title into attract demo 1, and for each
of the 37 levels the path logo → menu → village intro skipped → the level asked for → 900 steps of input (its
opening answered, walking both ways, F, D, E, S, Viking switches, Up/Down, the inventory; in six levels the input
loses the Vikings: the game-over scene). Their input lives on in `replays/level-NN.replay` and `attract.replay`
([`replays/README.md`](../../replays/README.md)).

## Open
- `1397` at the level start (header byte `0x18` → `03A6`/`03A8`): nothing reads them, as far as seen.
- The joystick's axis direction ([`input.md`](input.md#joystick)). The keyboard word's quirks are reproduced by the
  front end (`src/lvr/main.cpp`: a release clears the bit even while another key with it is held; any key during a
  demo is `0xFFFF`).
