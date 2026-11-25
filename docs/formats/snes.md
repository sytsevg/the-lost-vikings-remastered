# The SNES version's data

**Status:** observed (2026-11-18), from the user's own ROM image with a Python prototype (`tools/snes.py`); the
pictures it renders were looked at. **Implemented** for the chunk table and the backdrops in `src/assets/snes.*`
(`SnesRom`, `load_level_backdrop`; `[real-data]` tests: every chunk decodes and uses up its bytes, all 37
backdrops load, level 1's the same picture as the prototype's; fuzzed, `fuzz_snes`). The game reads the image
directly, as it reads `DATA.DAT` (ADR-0014); `lv-export assets` writes the backdrops to `snes/NN-backdrop.png`
when the image is in the folder, and each level and scene with its backdrop behind it (`snes/NN-level.png`,
`snes/<scene>-scene.png`). Under Remaster rules the game shows them ([below](#in-the-game)). Everything below
was found in this image.

**The image:** *The Lost Vikings* (USA), the 1993 cartridge: 1,048,576 bytes, no copier header, LoROM (internal
header at `0x7FC0`, title `THE LOST VIKINGS`, checksum `0x6CE9`, which matches the bytes' sum). SHA-256
`9f7782a92fda789f9d119b1f0a2f7da0f35606357556a48eca9487797ee1a888`. It is the user's, lives in
`gamedata/original/` and is never in the repository (ADR-0001), like `DATA.DAT`.

**In short:** the SNES game keeps its data the way the DOS game does. The ROM holds a table of numbered chunks
packed with the same LZSS, and the level headers are the DOS headers almost byte for byte. What the DOS headers
leave blank, the SNES ones fill in: a **backdrop** for every level, drawn behind the map and scrolled slower than it.

## The chunk table
- At file offset `0x58000`: one entry of 4 bytes per chunk, `u16` offset and `u16` bank. The chunk starts at
  `0x58000 + bank × 0x8000 + offset`. The first chunk starts where the table ends, so there are **361 chunks**
  (0–360). The offsets rise all the way.
- A chunk is **LZSS** as in [`data-dat.md`](data-dat.md#lzss-verified), with one difference: the `u16` header is
  the decoded size itself (in `DATA.DAT` it is the size − 1).
- Evidence: 343 of the 361 chunks decode and use up exactly the bytes to the next chunk; the last one (360)
  decodes to 680 bytes and is followed by zeros to the image's end.
- The other 17 are **stored** as they are (as in `DATA.DAT`, nothing says which): 4–12 (384 bytes each), 13 (192),
  14 (3,200), 134 (27,136) and 137–141 (21,504–33,280). Not decoded. They look like tile graphics (sizes that are
  multiples of 32 bytes, rows of bit patterns), not sound.
- **The sound is not in the chunk table**: no chunk looks like it. It must be in the banks below `0x58000`, with
  the code; a second table of rising (offset, bank) pairs is at `0x2A64F` (107 entries), not looked into.

## Level headers
The header is [`level.md`](level.md#level-header)'s. Level 1's (chunk 111) and the DOS one (entry 198) have the
same length, 400 bytes, and differ only in the numbers that name other chunks and in the backdrop's fields. The
objects, the switch word, the start position: the same.

Each DOS level's SNES header was found by likeness (`tools/snes.py levels`). The spaceship levels are chunks 111,
113, … 125; the other worlds follow the DOS order too.

**The levels are the same game.** In 26 of the 37 levels the fixed fields and the whole object list are identical
in both versions. The other 11 (compared 2026-11-18, object by object):

| Level | Difference |
|---|---|
| 5, 6, 7, 13 | the SNES level has one object more (types `0x62`, `0x63`, `0x69`) |
| 9 | three objects more and one moved on the SNES, two of them gone on DOS |
| 33 | four objects more on the SNES (types `0xB6`, `0xB8`) |
| 10 | one object 144 pixels further left on the SNES; the start's y differs |
| 21, 25 | two objects each 16 pixels further left on the SNES |
| 27 | two objects 16 pixels less high on the SNES |
| 28 | the DOS map is wider: every object is 16 pixels further right, the start too |

What the extra objects are is not looked up. For the backdrops none of it matters: the maps' sizes are the same
but for level 28's one column.

| Offset | Type | Field |
|---|---|---|
| 0x2e, 0x30, 0x32 | u16 | the map, the tiles and the quads (the DOS "metatiles"), as chunk numbers |
| 0x34, 0x36 | u16 | the backdrop's width and height in quads (16 × 16 pixels each) |
| 0x38 | u8 | how the backdrop's map goes on past its size: bit 1 = it **repeats** (2 in every level), bit 0 = it stops at its edge ([below](#how-the-game-moves-the-backdrop)) |
| 0x39 | u16 | the **backdrop's map** chunk |
| 0x3b | u16 | the backdrop's tiles; `0xFFFF` in every level = the level's own tiles |
| 0x3d | u16 | the backdrop's quads; in every level the level's own quads chunk |
| 0x3f, 0x41 | u16 | how fast the backdrop scrolls across and down, in 256ths of the camera's movement (`0x40` a quarter, `0x80` half, `0xC0` three quarters, `0x100` with the camera). Bit 15 (the spaceship levels' horizontal word, `0x8500`): that axis moves **by itself**, the other 15 bits being 256ths of a pixel per refresh ([below](#how-the-game-moves-the-backdrop)) |
| after the objects | list | the palettes, as in the DOS header: chunk and first colour |

## The scenes, and what this image doesn't have
Besides the 37 levels the chunk table holds 11 more headers, the DOS game's scenes one for one (found by the
header's first two words; the "next slot" numbers are the DOS slots'):

| Chunk | Scene | Backdrop map |
|---|---|---|
| 130 | the ship (the ending's last scene), next 39 | 131 |
| 284 | game over | 283 |
| 297, 298 | the title, **twice** as in `DATA.DAT` (381, 382) | none |
| 305, 310 | the two logos | none |
| 311 | the galaxy | none |
| 343, 344, 346 | the village: intro, demo, ending (144 × 24 quads) | 339, 340, 339 |
| 345 | the starfield of the ending: a 16 × 8 map that is the spaceship levels' backdrop map (107) | none |

So the 1993 cartridge is the DOS game's content: **no level or scene the DOS game lacks**. The console-only
levels, the interludes and the languages of later editions and of the Genesis version are not in this image. Three scenes have backdrops of their own (the village's sky, the ship, game
over), in the same fields as a level's (`load_scene_backdrop`). The DOS game draws those three scenes as tiled maps
too: the village leaves its sky in colour 0 as the levels do, and with the backdrop behind it is the console's
picture; the ship and game over cover every such pixel (game over has none), so their backdrops never show. The
village's demo (344, the night the Vikings are taken) has a backdrop of its own (340): the console's night with the
ship flying in. Behind the DOS night, whose sky and ship are its own, it shows as the ship scrolling past behind the
houses: the Remaster leaves it out (the user, 2026-11-25; `kSnesSceneHeaders`).

## Maps, quads, tiles, colours
- **Map** (the level's and the backdrop's): `u16` per cell, row by row. The low 10 bits are the quad; the level
  map's upper bits are the tile type, as in the DOS map.
- **Quads:** 8 bytes each, four `u16` for the top-left, top-right, bottom-left and bottom-right 8 × 8 tile. A word
  is what the console's picture chip takes: bits 0–9 the tile, 10–12 the palette row (16 colours each), 13 priority,
  14 mirrored, 15 flipped.
- **Tiles:** 32 bytes each, 8 × 8 pixels of 4 bits: per row two bytes holding bit 0 and bit 1 of its eight pixels
  (the leftmost pixel in the top bit), then 16 more bytes the same way for bits 2 and 3. Colour 0 of a row shows
  what is behind.
- **Colours:** a palette chunk is `u16` words of 5 bits each for red, green and blue (red lowest). A pixel's colour
  is palette row × 16 + its 4 bits.

## The backdrops
Every one of the 37 levels has one. There are eight, shared within a world:

| Backdrop map (chunk) | Size in pixels | Levels | Scroll (across, down) |
|---|---|---|---|
| 107 | 256 × 128 | 1–4, 34–37 (spaceship): stars | `0x8500`, `0x100` |
| 18 | 256 × 512 | 5, 7, 10 (prehistoric): mountains at dusk | ¼, ¼ |
| 19 | 256 × 256 | 6, 8, 9, 11 (prehistoric) | ½, ½ |
| 40 | 256 × 256 | 12 (Egypt): pyramids | ¼, 1/16 |
| 41 | 128 × 128 | 13–17 (Egypt) | ¾, ¾ |
| 64 | 128 × 128 | 18–25 (factory): a brick wall | ¾, ¾ |
| 88 | 256 × 256 | 26, 29, 32, 33 (wacky) | ¾, ¾ |
| 85 | 256 × 256 | 27, 28, 30, 31 (wacky): candy canes | ½, ½ |

A backdrop is drawn from the level's own tiles and quads with the level's colours, so it needs nothing but its map.
Previews: `tools/snes.py previews` → `gamedata/previews/snes/` (not in the repository).

**The DOS headers carry the same fields.** Level 1's DOS header has the backdrop's size (16 × 8), the byte 2 and
both scroll words, with `0xFFFF` where the SNES header names the chunks: the DOS game never reads them. So the
backdrops fit the DOS levels by design; only the pictures are missing there.

## How the game moves the backdrop
Read from the game's code in the ROM (`tools/dis65816.py`; addresses are SNES ones in bank `80`, the file offset
is the address − `0x8000`). The level header is copied to RAM at `$19C7`, found by the addresses the code uses
most: so the backdrop's size is `$19FB`/`$19FD`, the byte 0x38 `$19FF`, its map `$1A00`, the scroll words
`$1A06`/`$1A08`.

- **At the level's start** (`9B94`): bit 15 of each scroll word is taken out into a flag (`$19BD` across, `$19BE`
  down), leaving the speed. The backdrop's place (`9EAF`): camera × speed, a 16 × 16 bit multiply (`9186`), the
  whole pixels into `$16A5`/`$16A7`, the 256ths into `$16B1`/`$16B3`.
- **When the camera moves** by *n* pixels (`C6BD` right, `C6FD` left, the same pair for down and up): the backdrop
  moves *n* × speed, the 256ths carried over. Nothing happens on an axis whose flag is set.
- **Every refresh** (`8636`, called at the start of each of the three camera steps of a tick, `8527`, `85C2`,
  `85FC`): an axis whose flag is set moves by its speed. The spaceship's `0x8500` is 5 pixels a refresh across:
  the stars fly past whatever the Vikings do.
- **Past its size** (`C984`, where a backdrop cell is looked up): with bit 1 of the byte 0x38 the cell's column
  and row are taken modulo the size (an `and` with size − 1: the sizes are powers of two), so the backdrop
  **repeats**. With bit 0 the position stops at an edge instead (`C6DF`); no level uses that.
- The camera here is the one without the screen shake's offset.

So, for a level that starts with the camera at 0: the backdrop's top-left is at (camera × speed) / 256 on an
ordinary axis, (refreshes × speed) / 256 on a flagged one, both modulo its size.

## In the game
Under Remaster rules, with the image there (`src/lvr/backdrop.*`, HANDOVER.md "Parallax"):
- **Where:** a pixel of the map in colour 0 of one of the palette rows 0-7 shows the backdrop. In all 37 levels
  the DOS level palette's rows 0-7 are these colours (but for 5 bits against 6) except each row's colour 0, which
  the console never shows (it shows what is behind) and the DOS game blacks out. So the backdrop's pixels go in
  as they are, as the DOS palette's indices.
- **Checked:** the DOS map with the backdrop laid in that way, each 320 x 176 screen with the camera there, against
  the SNES level's own map with its backdrop drawn the same way: 32 of the 37 levels and the village's scenes are
  the same pixel for pixel. Levels 21, 24, 25 and 33 differ in less than 1% of their pixels, and level 28's DOS
  map is a column wider. The black that is left (10-45% of the map in the spaceship, prehistoric, Egypt and
  factory levels) is black on the console too.
- **Scrolling:** as [above](#how-the-game-moves-the-backdrop), with the DOS camera; the DOS refresh (59.7 a second)
  stands in for the console's (60.1), and between pictures it moves on evenly.

## Open
- Bit 3 of the byte at 0x38 (a third way past the size, `C9AE`; no level sets it); the priority bit's use in the
  level map; the backdrop's place when a level starts with the camera away from 0 on a flagged axis.
- The stored chunks; the sound; the scenes' headers; the six levels that differ; the console-only content.
