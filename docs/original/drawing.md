# Drawing: the playfield, the sprite pieces, the text, the HUD

**Status:** observed (static disassembly of `VIKINGS.EXE`, 2026-11-17), the drawing code in segment `0C83` and the
HUD in `0000`. The engine's: `src/lvr/render.*` (the Classic picture, ADR-0006). A frame of level 1 as drawn
matches the user's DOSBox screenshot in the tiles, Erik's sprite and place, the item, the laser, the
speech box (position, tail, colours) and the HUD portraits. The scrolling hardware tricks (pel panning, the page
flips, the dirty-rectangle restore) are not reproduced: the renderer draws the whole picture every frame, which
gives the same result **except where the original leaves something behind**. Compared with the original's
screenshots (`tools/lockstep.py --shot`, 2026-11-17; pictures in `gamedata/previews/screens/`, `galaxy/`,
`gameover/`, `ending/`): the logos, the title, the menu, the password screen, level 1 with its HUD, the galaxy, the
game-over scene, the starfield, the village ending and the ship the same pixel for pixel, after four fixes: the
HUD's portraits and health dots were 1–2 pixels off (their blitters start at plane 1 and plane 3, below); an image
scene's picture follows the camera (below); colours 192–207 and 224–239 from opcode `13 D9` (the ship's Vikings);
and the text rule of the `0xE0` scenes ([text cells](#text-cells-dialogue)). One difference stays, the game-over
scene's lightning. Its two pieces switch between two frames at the same place without an erase (the redraw word's
erase count stays 0: nothing moved), and the original draws each frame over what the last one left in that page,
so its bolt is both frames on top of each other (`gamedata/previews/gameover/lightning-740-original-vs-remake.png`,
left the original). Reproducing that needs the original's three pages and its restore (`0C83:15D5`, the screen-cell
cache in segment `ds:2E69`); not done (2026-11-17).

The original draws three times per tick ([`timing.md`](timing.md#game-loop)); the engine draws the state as it is at each of those points.

## Palette
The header's palettes in order (`12AE`), then every 16th colour (16, 32, … 240) set to black (`12DA`); colour 3 is
also kept at `ds:25A8`. The palette animations rotate colour ranges over time ([`game-loop.md`](game-loop.md#one-tick)).

**Tints** (`0E99`, after opcodes `3D`/`4C` and the pause menu's `450B`): the palette uploaded is the base palette
(`ds:7F02`) with each 6-bit component less the two tints' or-ed value (`0342`–`0344` | `0345`–`0347`, each the
opcode's byte doubled), at least 0; colour 0 and 16–255 only, colours 1–15 (the HUD's and the text's) are copied as
they are. Uploaded at the next retrace (job 4). The engine applies them to every picture from the state. (`0F03` mixes
all 256 colours the same way: the fades.) **The fades** (every slot load, `1080`): `0FA0` before loading, steps 0 to
69, and `0F5D` after the level start's tick, steps 70 down to 0, one refresh each (70 + 71, about 2.4 s): each
step is put in the first tint and every one of the 256 colours mixed by `0F03` (the step or-ed with the second
tint, subtracted, at least 0), then flipped. In between, the faded palette stays uploaded, so the level start's
four draws are black. The engine darkens the palette by the step at each of those refreshes (`Play::fade`). The
fade out draws nothing: the pages still hold the last pictures, from before the game-over pass removed everyone,
so everyone stays on screen while it goes dark (seen in DOSBox, 2026-11-24). The engine draws those refreshes
from the world as it was before that pass (`Play::shown`).

## Order
0. **The shown view** (the flip, `6775`): the camera plus the shake's offsets (`039E` across, `03A0` down; counted
   by `2D2C` each tick), or the camera minus them where the sum would pass the camera's limit (`25A4` / `25A6`,
   compared unsigned). Everything in the playfield moves with it (tiles, pieces, text, an image scene's picture);
   the HUD doesn't (it is below the split). Checked against the original's screenshots with a shake poked in.
1. Image scenes (header flags `0x02`, `0x40`): `1254`/`127B` put the image at row 32, column 32 of each page, and
   the flip (`6775`) shows a page from the camera's row and column + 32 (`92F9` only picks the page: 0, `0x34`,
   `0x68`, 26 row-groups of 8 apart). So the picture is at (−camera x, 32 − camera y): the title (camera y 32) from
   its top, the starfield (y 24) 8 rows lower.
1. The tile map at the camera (`0000:6661` scrolls in the strips; the picture is the map's metatiles at
   `ds:0044`/`0046`, from the live map, which scripts change).
2. The sprite pieces (`0C83:156C`): pieces `0x7F` down to 0, each in use (flag `0x8000`) and not hidden
   (`0x4000`, or `0x2000` while a Viking blinks). Lower pieces end up on top.
3. Foreground tiles: metatile parts with bit 3 drawn over the sprites through the tileset's transparency mask (the
   entry after the tileset, 8 bytes per tile; byte *k* covers the planar tile bytes 8*k*..8*k*+7, most
   significant bit first, as in the tiled sprites). Where exactly the original does this is not read; the
   result is what the mask means, and the pictures match.
4. The text cells (`0C83:1897`).
5. The HUD (`0000:1792`).

## Sprite pieces
Per piece (arrays in [`animation.md`](animation.md#opcodes)): mode = flags bits 0–2 (table `0C83:15CB`):

| Mode | Blitter | Size |
|---|---|---|
| 1 | `0C83:0648` | 8 × 8 |
| 4 | `0C83:0B82` | 16 × 16 |
| 2 | `0C83:1078` | 32 wide × the size word (`0C4D`) rows |
| 0, 3 | none | not drawn |

- **Where:** the piece's x/y (`064D`/`074D`) are the world position of its top-left pixel; the camera is
  subtracted; pieces wholly outside the 320 × 176 view are skipped, the others clipped.
- **What:** the frame word (`084D`) is an offset **+ 1** into the segment in `094D` (the blitter reads the mask
  byte at `[si − 1]`): the sprite buffer (the header's sprite sets back to back, which is why the sprite table holds
  offset + 1) or the packed-frame buffer. The data is in the unpacked layout ([`../formats/graphics.md`](../formats/graphics.md#sprites)):
  per plane and row a mask byte and 8 pixels; masked-out pixels are not drawn; pixels are palette indices.
- **Segments:** besides the sprite buffer and the packed-frame buffer, a piece's segment can be a packed set's own
  whose bytes it then shows as they are: sets that hold frames already in the
  unpacked layout (the game-over scene's lightning, 32 × 176, two pieces side by side).
- **Mirror / flip:** flags `0x200` / `0x400`, the picture reversed within its own width / height.
- **Packed frames** (the Vikings, enemies): animation opcode `14` (`0000:34DC`) unpacks frame *n* of the packed
  set (far pointer `0A4D`/`0B4D`) into the first piece's buffer (frame − 1 in its segment, buffers at `ds:871C` by
  piece), into the unpacked layout: each 4-bit pixel becomes `0x80 | (flags & 0x70) | nibble`. So flags bits 4–6
  pick one of eight 16-colour palettes at 128–255 (Erik and Baleog share frames, not colours). Field `36` keeps the
  frame last unpacked; the engine unpacks it again before drawing, which gives the same bytes unless the colour bits
  changed in between.

## Text cells (dialogue)
A 40 × 22 grid of characters (`ds:956C`, cell = row × 40 + column), filled by the dialogue player's commands and
cleared by command 2 ([`dialogue.md`](dialogue.md)):
- **Box** (command 0, `2709` → `2388`, `24C5`): corners `12` `14` `17` `19`, edges `13` (top), `18` (bottom),
  `15` (left), `16` (right), inside spaces; the tail at the anchor cell (`2549`: column 1 for anchors 1 and 4,
  width − 2 for 2 and 5, width / 2 otherwise; the bottom row for 0, 1, 2 and 6, the top row for 3, 4, 5) is `1A` at
  the bottom or `1B` at the top; anchor 6 has none. The text starts one cell in; a carriage return fills the rest of
  the line with spaces.
- **Plain text** (command `A`, `2738`): from the text's offset as it is (its width and height bytes too), no padding.
- **One character** (command 8, `26EF`): the password letters.
- **Drawing** (`1897`): each non-zero cell is glyph *c* − `0x10` of the font (entry 2, loaded to `ds:687E`, 72
  bytes per 8 × 8 glyph) through the 8 × 8 blitter, at (column × 8, row × 8) on the camera's 8-pixel grid, so
  shifted left/up by camera x & 7 / y & 7.
- **Straight onto the pages** (header flags `0xE0`: the village scenes, the ship, the starfield; `0C83:18A0`): the
  text is drawn only while some is pending (`956B`, set by commands 0, 8, `A`; cleared by a clear and the level
  start) and at most 4 times after a clear (`98DC`, reset by command 2 and a closing menu, not by the level start).
  The draws are the tick's three (`006C`, `00A1`, `00D3`) and one after each command the dialogue player runs
  (`0898`). So text added after the fourth draw isn't shown until the next clear: in the village ending only the
  first speech box appears. Not reproduced: such text being erased where sprites later pass (its cells
  aren't in the screen-cell cache).
- **Colour 3** (command 6, `27E4`): bits 0–4 red, 5–9 green, 10–14 blue, each doubled into a 6-bit VGA value. The
  box's inside uses colour 3 (level 1's speaker colour `0x12`: dark red).

## HUD (`0000:1792`, levels with header flag `0x01`, not slot 44)
The panel (entry 1) below the playfield, and over it (`20FF`, `2199`, `205B`, `1B0B`):
- portraits (entries 4–12) at (45, 19), (117, 19), (189, 19) (`17D0`: offsets `066D`, `067F`, `0691` from `ds:855C`, the
  planes written from plane 1 on, so 4 × byte + 1; checked against the original's screenshots) for object slots 0, 1, 2 (Erik, Baleog, Olaf: the start
  spawns types 1, 0, 2): coloured for the current Viking, grey for the others, the third set when dead (field `1A`
  bit 15), as the original's screenshots show. Which routine draws which is not read in detail.
- health (`1B0B` → `1AA4`): field `0A` holds health × 2 (6 at a level start); it, + 4 when the Viking's word at
  `ds:0429` is set (the shield), is a byte offset into the word table `ds:8582`, so the frame of entry 13 (7 frames,
  32 × 7: 0–3 dots, then with the shield) is half of it, at (47, 43), (119, 43), (191, 43) (screen offsets `ds:857C`; `1AA4` writes from plane 3 on, so 4 × byte + 3); redrawn when it changed
  (last drawn: `0423`, `042F`). That record is game state: the level start and the game-over scene read `0423`
  ([`game-loop.md`](game-loop.md#one-tick)), so the engine keeps it in the simulation.
- the items (entry 14) in each Viking's four squares and the yellow picker's corners over each one's chosen square:
  [`menus.md`](menus.md#the-huds-items-and-picker).

## Open
- The foreground tiles: where the original draws them (the blitters' masks, or `406D`).
- `406D` (a list of points redrawn, `ds:8734`), `0C83:00C1` with `ax` = −2.
- Which HUD routine draws which portrait (the result is checked).
- What `0429`–`042D` mean beyond `0429` (the shield on the health frame).
