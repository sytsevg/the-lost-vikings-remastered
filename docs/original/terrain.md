# Terrain: the tile map, the tile tests, movement

**Status:** observed (static disassembly of `VIKINGS.EXE`, 2026-11-17). The engine's: `src/lvr/terrain.*` and `movement.*`. Checked against the original through the reference engine (retired 2026-11-23): its state was the original's in DOSBox, step for step, on every path tried ([`../formats/unknowns.md`](../formats/unknowns.md#5-what-has-not-been-compared-with-the-original) lists them), and the engine matches its runs (`replays/`).

## The tile map
- Segment `ds:2E63`: a word per 16 × 16 cell, row by row (row table `ds:8F68` = row × width × 2, 256 rows). Bits
  10–15: the **tile type** (0–63), bits 0–9: the metatile. The map entry of the level header is loaded as it is;
  scripts change it (`29`–`2B`, through `0000:41E0`), so it is part of the simulation state.
- `0000:4199`: the type at pixel (x, y): cell = (x >> 4, y >> 4), unsigned. A column ≥ the width (header word `0x29`,
  `ds:25DC`; also left of the map) reads as `0x400`, type **1**; a row ≥ the height (`0x2B`) as 0, type **0**.
- Types seen in the code: 0, 3, `0C` free (passable); 1, 2, 4, 5, `20` ground; 4 a step the ground snap climbs;
  `30`–`3F` **slopes**: the surface height at column c of the cell is the low nibble of `ds:897C` + (type & 15) × 16
  + c (from the bottom of the cell; 6390 gives how far an object's bottom is into it).
- **Type lists**: the tile tests take an offset into `ds:94CC`, 35 sorted lists of types, each ended by `0xFF`. They
  are the same bytes as the lists after the records in every object database (offset `0x1347`). The remake reads
  them, and the slope table, from `VIKINGS.EXE`.

## Tile tests (opcodes)
Each first looks for tiles of the list's types, then for **objects as terrain**: another object in use whose kind
(field `26`) is in the list. A hit sets `ds:03B2` (the type, or the object's kind) and, for an object, `ds:03B4`
(its slot; otherwise `0xFFFF`). Even opcodes jump if something was found, odd ones if not.

| Opcodes | Where |
|---|---|
| `1E` / `22` | the row just above the box (top − 1), across its width |
| `1F` / `23` | the row just below (bottom + 1) |
| `20` / `24` | the column behind (left − 1 facing right, right + 1 facing left), down the box's height **as it was before its last vertical step** (fields `00`/`02` − `3A`); not if its foot (x, bottom − step) is on a slope |
| `21` / `25` | the column ahead |
| `30` / `31` | the point diagonally ahead of the feet (right + 1 or left − 1, bottom + 1) |
| `49` / `4A` `G2 b j` | the point (x, y) |
| `4E` / `4F` `j` | can it walk on: 16 pixels ahead of x at the feet a slope; or a type-1 step with a free or sloped cell above it; or a free cell with ground or a slope under it (no list) |
| `BF`–`C2` / `C3`–`C6` | above / below / behind / ahead, objects only, only the Vikings' slots 0–2 |

Rows and columns are walked in 16-pixel steps from one end to the other, the last step clamped to the end.

## Terrain hits (`32`, `33`, `3C`) and the push back
In the contact pass ([`hits.md`](hits.md)):
- `32`: if it moved sideways (x ≠ the previous x), the leading column (left or right edge) of tiles in the list →
  pushed back to the cell edge (right = (right & ~15) − 1, or left = (left | 15) + 1). Else, **moved or not**, an
  object of the list whose box it ran into, compared by the two objects' horizontal steps (fields `38`) → pushed to
  its edge (an object standing still is run into by one moving towards it).
- `33`: if it moved vertically, the leading row (top or bottom) → pushed out to the cell's top (bottom = (bottom &
  ~15) − 1) or below the ceiling; else, moved or not, an object, by the vertical steps (fields `3A`). (Found by
  lockstep, 2026-11-17, in attract demo 5.)
- `3C` (moving down only): if the list has slope types, onto the slope's surface (from the slope it was on or is in);
  else, when the bottom crossed into a new cell row, onto the top of a cell of the list's types under the bottom
  edge; else onto an object whose top its bottom passed this tick (both sweeps compared).
- A push back clears the position's fraction (field `3E` or `40`).

## Movement
- `0000:386B`, after the scripts: every object stores its position as the previous one (`13A5`/`13CD`). Those
  nothing else moves (word `141D` = `0xFFFF`) move by fields `38`/`3A` (filled by the scripts, the animation and the
  speed step, [`animation.md`](animation.md#the-speed-step-35cf)): capped to ± fields `22`/`24`, in **8.8 fixed
  point**, the fraction kept in the low byte of fields `3E`/`40`. The whole pixels are added to x/y and the box;
  fields `38`/`3A` then hold that step (the tile tests use it).
- `0000:625D`, then: objects with flag `0x2000` (set by `4B`) that aren't moving up are put on the ground: up a
  step (type 4, or a type-1 cell with room above), onto a slope's surface, or down to the ground in the cell under
  their feet; a Viking standing on an object of list `0x89` stays. The flag is cleared.
- `0000:3916`, after the contact pass: the others. `141D` ≥ `0x100` (set by `48`): moved by `38`/`3A` as they are.
  Otherwise `141D` is the object it is attached to (`17`: slot 0, `18`: the target, `1B`): it turns to face the same
  way and goes to that object's position + fields `38`/`3A` (mirrored by facing). `141D` is reset to `0xFFFF`.

## Where this runs in a tick
Scripts (`4207`), movement (`386B`), ground snap (`625D`), contact pass (`5546`), attached objects (`3916`), then
the camera and the hit pass: the whole tick is in [`game-loop.md`](game-loop.md#one-tick).

## Open
- What each type means in play: the scripts decide, through their lists. Named from where the levels have them
  (the level dump's collision maps, [`../formats/level.md`](../formats/level.md#exported-output), 2026-11-18): `03`
  a **ladder** (free; list `03 04 0D`), `04` the **top of a ladder** (ground), `12` **lava** (prehistoric). Named
  by the user from the same pictures: `0A` where a **moving platform's way ends** (the ends of its range), `0B` a
  **hidden platform** that appears when its button is shot (level 21), `10` **water** and `13` **quicksand** (a
  Viking drowns in both), `14` **spikes**, `15` **electricity** (deadly to fall into), `16` **springs** (they
  bounce), `17` **rotating spikes** (a death trap to fall into). `10`–`17` are one family: list `5C` holds all
  eight, and each has a list of its own. Still open: `0D` (levels 35 and 36 only; in the ladders' list; always a row of two cells inside a ladder, below
  its top; the user doesn't know what it does either), `11` (no
  level uses it), and what sets `02`, `05`, `0C` and `20` apart from plain ground and free cells. The names say
  what a player sees; what each does exactly is confirmed when the kinds that test for it are translated.
