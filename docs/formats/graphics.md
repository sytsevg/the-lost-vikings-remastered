# Graphics

> Status: **verified** for level graphics and sprites (2026-11-16). All 37 levels decode and render correctly
> (checked by eye for several worlds), and the level-1 render matches an independent Python implementation pixel
> for pixel (`[real-data]` tests in `src/assets/tests/test_level.cpp`). Sprites: see [below](#sprites).
> Code: `src/assets/graphics.*`. Tests: `src/assets/tests/test_graphics.cpp`. Sources: RyanMallon's liblv (CC0), checked
> against our data.

Everything stays **indexed** (palette indices) until it is drawn, so palette effects keep working (ADR-0014).

## Palettes
- An entry of N × 3 bytes: VGA DAC red, green, blue, **6 bits each** (0..63; the highest value seen is 62).
- Expanded to 8 bits as `(v << 2) | (v >> 4)`, so 63 becomes 255.
- A level combines several palette entries into one 256-colour palette. Each is placed at a *first colour* index,
  in the order the level header lists them, and later ones overwrite earlier ones. Typical: a 128-colour
  world palette at 0, small 16-colour palettes for the HUD and sprites at 128..255. See [`level.md`](level.md).

## Planar images (VGA Mode X)
- The game draws in VGA Mode X, where the screen is split into 4 planes by `x % 4`.
- A planar image stores all of plane 0 first (pixels x = 0, 4, 8, … in row-major order), then plane 1, then 2, then 3.
- Pixel at offset `o = y * width + x` lives at byte `(o % 4) * (width * height / 4) + o / 4`.
- mrcrowbar calls this an "interlace transform"; it's the same thing.

## Full-screen images
Stored entries 1 (the HUD panel), 380, 384, 529, 530, 531: a `u16le` **plane size**, then a planar
image of `plane size × 4` pixels, **344 pixels wide** (the game's VGA drawing width). A full screen is 344×240: a
176-line top part plus a 64-line HUD-area part. Code: `decode_screen_image`.

**What is visible: 320×240, square pixels** (VGA Mode X, 4:3), columns 0–319 of the 344. Verified 2026-11-16
against a DOSBox screenshot of level 1 (1817×1368, nearest-neighbour scaled, 5 black rows at the top): sampled on a
320×240 grid, 95% of the playfield pixels equal our level-1 map (camera at (0, 304), the bottom of the level) and
92% of the HUD pixels equal the HUD panel's columns 0–319. Every difference is a sprite, a live HUD part or a
palette-animated colour; no map tile differs. Shifting the HUD window to columns 12 or 24 drops the match below
45%, and a 320×200 reading matches less than half. The HUD panel's columns 320–343 are black.

## Tiles
- 8×8 pixels, 64 bytes, planar (as above), one byte per pixel.
- A tileset entry is a sequence of tiles. The entry **right after** each tileset is its **transparency mask**:
  1 bit per pixel, exactly 1/8 of the tileset's size (verified for all 37 levels): 8 bytes per tile, byte *k*
  covering the planar tile bytes 8*k*..8*k*+7, most significant bit first; a set bit is an opaque pixel of a
  foreground tile ([`drawing.md`](../original/drawing.md#order)).

## Metatiles (16×16)
RyanMallon calls these "prefabs". 8 bytes each: four `u16le` for the top-left, top-right, bottom-left and
bottom-right tiles.

| Bits | Meaning |
|---|---|
| 15–6 | tile index |
| 5 | flip vertically |
| 4 | flip horizontally |
| 3 | foreground: drawn in front of the Vikings |
| 2–0 | unused by The Lost Vikings (Blackthorne uses them to pick a 16-colour palette) |

## Tilemaps
- `width × height` cells of `u16le`, row-major. Width and height come from the level header. The entry size is
  exactly `width × height × 2` for all 37 levels.
- One scene map is **short**: the Interplay logo's header says 20×15 but its map holds 20×14 (560 bytes). We keep
  the stored cells and leave the missing row undrawn (colour 0); liblv fills it with zeros. **Confirmed**
  (2026-11-17, `tools/spotcheck.py logo`): the original's picture is the same, pixel for pixel.

| Bits | Meaning |
|---|---|
| 9–0 | metatile index, as the game reads it (bit 9 is 0 in every map; always in range for all 37 levels) |
| 15–10 | the **tile type** (0–63): ground, slopes, ladders, … as the tile tests and scripts read it ([`terrain.md`](../original/terrain.md#the-tile-map)) |

## Sprites
> Status: **verified** (2026-11-16) for all 161 sprite entries: every entry that a level or scene header loads,
> the HUD graphics, and five sets no header loads. `[real-data]` tests in `src/assets/tests/test_catalog.cpp` check that
> every entry decodes exactly and compare pixel hashes for 9 entries with an independent Python decoder.
> Code: `decode_unpacked_frame`, `decode_sprite_frames`, `decode_packed32`, `decode_tiled` in `sprites.*` (tests:
> `src/assets/tests/test_sprites.cpp`); the catalogue (`kSpriteCatalog`) and `decode_sprite` in `catalog.hpp`.

Four formats, all planar (pixel `x` is in plane `x % 4`):

| Format | Used for | Layout |
|---|---|---|
| **raw** | HUD: portraits (4–12, 32×24), health (13, 32×7), items (14, 16×16) | a planar image per frame (`decode_planar`), no transparency |
| **unpacked** | objects: doors, switches, items, lasers, …; font (2) | per plane, the plane's pixels row by row; every 8 pixels are preceded by a **mask byte**, most significant bit first. 9 bytes per 8 pixels. |
| **packed32** | the Vikings, enemies, scene animations | a table of `u16` frame offsets (count = first offset / 2), then 32×32 frames: per plane and row a mask byte, then one **4-bit** pixel per set bit (high nibble first, padded to whole bytes) |
| **tiled** | only 293 and 342, which no header loads | a sprite **sheet** of 8×8 tiles, row by row across the whole sheet. Each tile is 72 bytes: **8 mask bytes**, then the 64 pixel bytes of a planar 8×8 tile (as in [Tiles](#tiles)); mask byte *k* covers pixel bytes 8*k*..8*k*+7, most significant bit first. Pixels are 1–15. |

- **Transparency:** masked-out pixels are stored as 0, and no drawn pixel in any unpacked, packed32 or tiled frame
  is 0 (checked for every entry). So colour 0 means "transparent" in our images, as it does in the game.
- **Colours:** unpacked and raw pixels are palette indices (e.g. 240–255 for sprites using palette 220 at 240).
  Packed32 and tiled pixels are 1–15; the game adds a colour base when it draws (Erik and Baleog share a format but
  use different 16-colour palettes).
- **Tiled sheets:** the frames sit side by side in the sheet, so a frame's tiles are not contiguous in the entry:
  342 is a 32×16 sheet (tiles 0 1 2 3 / 4 5 6 7) holding two 16×16 frames, 293 a 128×32 sheet holding four 32×32
  frames. The 72-byte tile has the same size as an unpacked 8×8 frame, but the masks come first; decoded as
  unpacked, both entries are noise. The JSON gives each frame's top-left tile as its offset.
- **Packed32 tails:** 44 of the 56 packed32 entries end with 640 bytes of `0xff` after the last frame (the others,
  e.g. the galaxy scene's 403–412, end right after it). 640 is the
  size of the largest possible frame (4 planes × 32 rows × (1 mask + 4 pixel bytes)), so it may be room for a
  frame built at run time; nothing reads it that we found ([`unknowns.md`](unknowns.md)). The frames themselves are always back to back, right after the table.

### Frame sizes are not in the data
An unpacked entry is just frames back to back; nothing says how big they are, and the planar layout depends on the
width. The game decides when it draws: the draw routine has three types,
**8×8**, **16×16**, and **32 pixels wide × N rows**, and larger objects are drawn from several such pieces. One entry
can mix sizes: entry 303 holds 12 items at 16×16 and then the 32×32 steak; the magnet (327) holds two 32×32 frames
and four 8×8 sparks.

So the sprite decoding uses a **catalogue** of frame runs per entry (`kSpriteCatalog` in `catalog.hpp`). It was built by
decoding every entry with every frame size the game can draw, scoring how coherent the results are, and checking
each one by eye. Wrong sizes are easy to recognise: 16×16 frames read as 32×8 show each image twice side by side at
half height, and other mistakes give noise. The catalogue is for the export only: the game
draws each piece with the size and frame offset its object's animation program gives
([`animation.md`](../original/animation.md)), so it never needs it.

Things RyanMallon's list gets wrong for our release (its sizes are right, its frame layouts often are not): arrows
(222, 223) are 8 frames of 8×8, not 4; the lightning (368) is 4 frames of 32×176, not 2; the game-over Vikings
(371–373) are six 32×32 pieces each, not one 64×96 frame.

### Sprite catalogue
- 161 entries, 1274 frames: 11 raw, 92 unpacked, 56 packed32, 2 tiled. Names are ours, mostly following Albert
  Thalheim's list.
- Every entry in any level or scene header's two sprite lists ([`level.md`](level.md#level-header)) is
  catalogued (`[real-data]` test). Catalogued but loaded by no header: the HUD graphics 2 and 4–14 (`VIKINGS.EXE`
  loads them itself), 240 (a band of Vikings, 32×32 × 12), 293 (a lavafall and three frames of a lava splash; our
  guess, shown with palette 292), 341 (an orange block), 342 (the bullseye target, upright and knocked flat), 348
  (a fist piston).
- Packed32 and tiled previews need a 16-colour palette; the catalogue names one (from the list where it gives one,
  otherwise picked by eye). It only affects the exported PNGs.

### Imported output
`lv-export assets` writes `gamedata/exported/sprites/NNN-name.png` + `.json`:
- The PNG holds the frames left to right, wrapping at 256 pixels, as an indexed image. Colour 0 is transparent
  (PNG palette alpha), except for raw sprites.
- The JSON lists every frame's byte **offset** and size in the entry and its rectangle in the PNG, the format, what
  the pixel values mean, the palettes used for the preview, and which level/scene headers load the entry.
- Preview palettes: raw and unpacked sprites use the palettes of the first level or scene that loads them (level 1
  if none does); packed32 and tiled sprites use the catalogue's 16-colour palette at colours 0–15.
