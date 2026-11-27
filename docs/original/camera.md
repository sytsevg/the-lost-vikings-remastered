# The camera

**Status:** observed (static disassembly of `VIKINGS.EXE`). The engine's: `src/lvr/camera.*`. Checked against the original through the reference engine (since retired): its state was the original's in DOSBox, step for step, on every path tried ([`../formats/unknowns.md`](../formats/unknowns.md#5-what-has-not-been-compared-with-the-original) lists them), and the engine matches its runs (`replays/`).

The camera is the top-left pixel of the 320 × 176 playfield: `ds:0044` (x), `ds:0046` (y). `ds:257F`/`2581` are the
same divided by 8 (the drawing code works in 8-pixel columns and rows).

## Limits (`0000:13B0`, level start)
`ds:25A4` = map width × 16 − 320, `ds:25A6` = map height × 16 − 176 (header words `0x29`/`0x2B`, in cells).

## Start (`0000:13D8`, level start, after the Vikings)
Centred on the current Viking (`03C2`; slot 0 in a scene): x − 160 and y − 88, each at least 0 and at most its
limit (signed comparisons). Also stored at `257B`/`257D`, and the strip positions `92F3`/`92F5` = camera / 16.

## Following (`0000:064B`, `0704`, `0753`, one per phase of a tick)
Phase A measures how far the current Viking is outside the box **144–176 across and 80–96 down** (relative to the
camera), at most 16 pixels, one direction per axis (left before right, up before down), and keeps the amounts
(`03D8` left, `03DA` right, `03DE` up, `03DC` down). Each phase then scrolls by its third of the amount, from the
table `ds:2B80` (word i = i / 3, read at amount + 2, + 1, + 0): ⌈a/3⌉, ⌊(a+1)/3⌋, ⌊a/3⌋, the same split as the
sprites' sub-frame steps.

A scroll (`7496` left, `746C` right, `74E9` up, `74BF` down) does nothing while that axis's lock word is set
(`0394` across, `0396` down; cleared at the level start; [Open](#open)). It stops at 0 (signed) or at the
limit (unsigned), updates `257F`/`2581`, and stores how far it moved in `034E`/`0350`. Those two are cleared at the
start of `064B` and set to 5 by a Viking switch (`2E79`, `2E16`). The scripts read them (10 places in 449–454,
e.g. 449 `5D8D`: wait while either is ≥ 4, i.e. while the camera is still moving).

So the camera trails a walking Viking by up to 16 pixels a tick and settles when the Viking is in the box.

## Strips (`0000:673C`, every tick before the scripts)
When the camera / 16 (`257F` / 2) differs from `92F3` (signed), `92F3` takes the new value and the listed objects in
the strip that came into view are created: the left strip if it went down, else the right. Then the same for
`2581` / 2 and `92F5`: top or bottom ([`objects.md`](objects.md#streaming-with-the-camera)). Only one strip per axis
per tick, so a jump of more than 16 pixels can skip one (the camera never moves that fast).

## Open
- Who sets the lock words `0394`/`0396`: no code but the level start writes them, and no script names them as a
  global operand; perhaps never set.
- The pel panning and the CRTC start address the flip (`6775`) makes of the camera: the presentation's.
