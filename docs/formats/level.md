# Levels

> Status: level table and header layout **verified**: for all 37 levels and 10 scene headers the parser
> ends exactly at the last byte of the entry. The fields the game reads are decoded from `VIKINGS.EXE` (the loader
> copies the header to `ds:25B3`); the bytes nothing reads, with their statistics, are in
> [`unknowns.md`](unknowns.md#level-header-all-47-level-and-scene-headers).
> Code: `src/assets/level.*`. Tests: `src/assets/tests/test_level.cpp`.

## Level table
The game hardcodes which `DATA.DAT` entry holds each level's header and each world's object database
(`kLevels` in `catalog.hpp`, from liblv):

| Levels | World | Header entries | Object database |
|---|---|---|---|
| 1–4 | spaceship | 198, 200, 202, 204 | 449 |
| 5–11 | prehistoric | 40, 42, …, 52 | 450 |
| 12–17 | egypt | 75, 77, …, 85 | 451 |
| 18–25 | factory | 114, 116, …, 128 | 452 |
| 26–33 | wacky | 156, 158, …, 170 | 453 |
| 34–37 | spaceship-return | 206, 208, 210, 212 | 449 |

The entries between the headers hold each level's tileset, transparency mask, metatiles and tilemap, plus the
world palettes (see the entry table in [`data-dat.md`](data-dat.md)). The world names are our own labels.

## Scenes
Non-playable screens use the level header format too (`kScenes` in `catalog.hpp`; names are ours). Exported by
`lv-export assets` to `gamedata/exported/scenes/<name>.png` + `<name>.json`.

| Scene | Header | Kind | Image data | Palettes (from the header) |
|---|---|---|---|---|
| interplay | 390 | tiled 20×15 (map short by one row) | 386–388 | 385 |
| silicon-synapse | 396 | tiled 20×15 | 392–394 | 391 |
| title | 381 (382 is identical) | image | 380 (344×176) + **384** (HUD area, 344×64) | 379, 3, 3, 383 |
| home-intro / home-demo / home-ending | 431 / 432 / 434 | tiled 144×24 (2304×384) | 424–427 | intro: 430, 3, 3, 291, 292, 229, 246, 251, 220 (demo and ending differ; ending uses 429) |
| galaxy (between worlds) | 402 | tiled 20×14 | 398–400 | 397, 229, 220 |
| starfield | 433 | image | 530 (344×176) + **531** (344×64) | 532, 3, 440, 446, 279 |
| ship (ending) | 218 | tiled 22×15 | 215–217, 219 + **529** (HUD area) | 214, 3, 3, 291, 292, 229, 383, 220 |
| game-over (moonlit sea, with its two words) | 369 | tiled 20×15 | 364–366 | 363, 423, 3, 383, 374, 370 |

- **Image scenes:** the header's tileset and metatiles are `0xffff` and the map field points at a full-screen
  image ([`graphics.md`](graphics.md)). The **HUD-area** image below it is not in the header: `VIKINGS.EXE` picks it
  from the header flags (byte `0x1C`): `0x02` → 384 (title), `0x40` → 531 (starfield)
  ([`game-flow.md`](../original/game-flow.md#header-flags-offset-0x1c-read-by-the-loader)).
- Entry **529** is a 344×64 HUD-area image: the bottom of the ship (dragon-head prow between torches). Header flag
  `0x20` makes `VIKINGS.EXE` draw it below the **ship** scene, which is the only header with that flag, so it is
  shown with the ship's palettes (its colours 48–105 come from 214). Exported as `images/hud-logo.png` with 214.
- Entry **1** is the **HUD panel** shown below the playfield in every level (344×64, of which columns 0–319 are
  visible: stone frame, three Viking slots, the trash can); `VIKINGS.EXE` loads it itself. It uses only colours
  1–14, and every level loads the HUD palette (entry 3) at colour 0 with no later palette overwriting colours 0–15,
  so it looks the same in all levels (`[real-data]` test). Exported as `images/hud-panel.png` with entry 3.
  The panel holds the Vikings' default look, and the game draws the live state over it (checked against a DOSBox
  screenshot of level 1):

  | Slot (panel pixels) | Erik | Baleog | Olaf | Drawn with |
  |---|---|---|---|---|
  | Portrait, 32×24 | (45, 19) | (117, 19) | (189, 19) | 4–6 for the selected Viking (colour), 7–9 for the others (grey; the panel holds exactly these), 10–12 when dead (field `1A` bit 15; [`drawing.md`](../original/drawing.md#hud-00001792-levels-with-header-flag-0x01-not-slot-44)) |
  | Health, 32×7 | (47, 43) | (119, 43) | (191, 43) | entry 13; the panel holds frame 3 (three dots) |
  | Items, 32×32 | (79, 18) | (151, 18) | (223, 18) | entry 14 (16×16 items) in a 2×2 grid (x + 0/16, y + 0/16); a yellow square picker marks the chosen square ([`menus.md`](../original/menus.md#the-huds-items-and-picker)) |

  Portrait and health positions are exact matches of those sprites in the panel. The item area is an empty square
  of that size; the picker is the corners of icon 19 of entry 14 ([`menus.md`](../original/menus.md#the-huds-items-and-picker)).
- The scene sprites between the scenes (403–447) are in the sprite catalogue: the Vikings warping in (403–412,
  galaxy), the game-over scene's letters (414–417, in four pieces), the alien ship, moon, stars and spaceship of the village
  and starfield scenes. 418–422, 443, 444 and 448 among them are not graphics but demo input recordings
  ([`input.md`](../original/input.md#demo-recordings)).

## Level header
| Offset | Type | Meaning | Status |
|---|---|---|---|
| 0x00 | u16 | horizontal push added to field `12` of objects with flag `0x4000` (`0x100` in every header) | `VIKINGS.EXE` ([`animation.md`](../original/animation.md#the-speed-step-35cf)) |
| 0x02 | u16 | **gravity**, added to field `14` of objects with flag `0x8000` (`0xF8` in every header) | `VIKINGS.EXE` ([`vikings.md`](../original/vikings.md#movement)) |
| 0x04 | u8 | music action when the level starts: 0 play tune, 1/4 keep the current music, 3 load the tune only | `VIKINGS.EXE` ([`audio.md`](audio.md#music)) |
| 0x05 | u8 | tune number (index into `kTunes`) | `VIKINGS.EXE` |
| 0x06 | u8 | music action when the level ends: 2 fade out over 1 s, 1 keep playing | `VIKINGS.EXE` |
| 0x07 | u8 | start mode: 0 a scene (one object), 2/4/5 the Vikings at fixed places from `VIKINGS.EXE`, else at the header's position | `VIKINGS.EXE` ([`objects.md`](../original/objects.md#level-start)) |
| 0x08, 0x0a | u16 | start x, y | `VIKINGS.EXE` |
| 0x0c | u16 | a scene's object type (mode 0) | `VIKINGS.EXE` |
| 0x0e | u16 | flags of the started objects | `VIKINGS.EXE` |
| 0x10 | u16 | field `16` of a scene's object (mode 0) | `VIKINGS.EXE` |
| 0x12 | u16 | the **switch word**: the level's switches at the start, a bit each (`ds:25C5`). Buttons, keyholes and switches flip bits (`xor`); lasers, bridges, elevators, doors, fans, crushers, crumbling tiles, conversations and others test their own bit, named by the object's argument (field `16`). Found from the object types whose code reaches each `xor` or read of `25C5`, named by their sprites. Level 2: `0x20F`, so the janitor alien (argument `0x20`) doesn't talk until its bit is set | `VIKINGS.EXE`, the scripts |
| 0x14 | u16 | read by the scripts (`ds:25C7`, database 453 only); meaning per object type ([`unknowns.md`](unknowns.md#level-header-all-47-level-and-scene-headers)) | `VIKINGS.EXE` |
| 0x16 | u16 | **next slot**: the level or scene that follows ([`game-flow.md`](../original/game-flow.md#slots)) | `VIKINGS.EXE` |
| 0x1c | u8 | flags: `0x01` HUD, `0x02` / `0x40` image scene with HUD-area image 384 / 531, `0x20` image 529 in the HUD area, `0x08` quit without the quit box, `0x80` the text layer drawn straight onto the pages; `0x04` never tested ([`game-flow.md`](../original/game-flow.md#header-flags-offset-0x1c-read-by-the-loader)) | `VIKINGS.EXE` |
| 0x29 | u16 | width in metatiles | **verified** |
| 0x2b | u16 | height in metatiles | **verified** |
| 0x2d | u8 | not read by any code found | |
| 0x2e | u16 | tilemap entry | **verified** |
| 0x30 | u16 | tileset entry (its transparency mask is the next entry) | **verified** |
| 0x32 | u16 | metatiles entry | **verified** |
| 0x34–0x42 | | the SNES version's backdrop (size, chunks, scroll speeds); not read by the DOS game, its chunk numbers blank | [`snes.md`](snes.md#level-headers) |
| 0x43 | list | objects, 14 bytes each: x, y (centre), half width, half height, type, flags, arg (all `u16`); ended by x = `0xffff` | **verified** (parses for all levels) |
| … | list | palettes, 3 bytes each: entry (`u16`), first colour (`u8`); ended by entry = `0xffff` | **verified** |
| … | u16 + list | palette animations: flags, then per animation delay, from colour, to colour (`u8` each) and `u16` values ended by `0xffff`; the list ends with delay = 0 | `VIKINGS.EXE` ([`game-loop.md`](../original/game-loop.md#one-tick)) |
| … | list | sprite sets, 6 bytes each: entry, slot, sheet (`u16`); ended by entry = `0xffff` | **verified** |
| … | list | packed sets, 5 bytes each: entry (`u16`), 3 bytes; ended by entry = `0xffff` | **verified** |

Notes on the last four lists (all 47 headers):
- 164 palette animations in total; 24 run from a higher to a lower colour, and only one has values in its list.
  The flags: bit *i* set = animation *i* runs (level 1: 6 animations, flags `0x3e`, so the first doesn't).
- **Sprite sets** are the unpacked sprite entries objects draw from (doors, switches, items, …). The game's loader decompresses them back to back into one sprite memory and **skips** the two
  `u16` after the entry. Our reading of those: `sheet` is the size of the artist's sprite sheet in 8-pixel blocks
  (high byte rows, low byte columns; entry 303's `0x0410` is 32×128 pixels, exactly two rows of six 16×16 items
  plus the 32×32 steak), and `[slot, slot + columns)` never overlaps another set of the same header, except 4 times
  where a level puts a few frames over another set (level 20: tools over items; level 25: also entry 271 over 302).
  Some sheet sizes don't match their entry, so this is a hint, not a rule.
- **Packed sets** are mostly the packed32 sets (the Vikings and enemies), plus the game-over scene's unpacked
  lightning (368). The three bytes are always 4, 4, then 0 for the Vikings' six sets and 1 for everything else.
- Every entry in either sprite list is in the sprite catalogue ([`graphics.md`](graphics.md#sprite-catalogue)).

Object types index the world's **object database** ([`script-vm.md`](../original/script-vm.md)). The databases and
their scripts are decoded (`src/assets/script.*`); the engine's kinds are written by hand from them (ADR-0017).

## Exported output
`lv-export assets` writes flat folders under `gamedata/exported/`, every PNG next to a JSON that names the
`DATA.DAT` entries it came from (ADR-0014):

| Folder | Files | Keyed by |
|---|---|---|
| `levels/` | the level dump: `NN-level.json`, `NN-world.png`, `NN-collision.png`; and `NN-tileset.png`, `NN-metatiles.png` | the level number 01–37 (the header entries are not in play order); the cutscenes' dumps by name (`galaxy-level.json`, …) |
| `scenes/` | `<name>.png`, `<name>.json` | the scene name from `kScenes` |
| `images/` | `<name>.png`, `<name>.json` (`hud-panel`, `hud-logo`) | the image name from `kImages` |
| `sprites/` | `NNN-<name>.png`, `NNN-<name>.json` | the `DATA.DAT` entry, which is how headers refer to sprites ([`graphics.md`](graphics.md#imported-output)) |
| `scripts/` | `NNN.txt` (a listing of the object database), `usage.txt` (instructions per opcode) | the `DATA.DAT` entry 449–454; the first line names the source ([`script-vm.md`](../original/script-vm.md)) |
| `dialogue/` | `texts.txt`: number, offset, box size, text | the text number; the first line names the `VIKINGS.EXE` bytes ([`dialogue.md`](../original/dialogue.md)) |

The **level dump** (per level, and per cutscene: the village's three, the galaxy, the starfield, the ship, game
over) shows the level as the engine has it (`lvr::Level`, `src/lvr/level.*`; made by `src/exporter/level_dump.*`):
- `NN-level.json`: the size, the source entries, the switches, gravity, music, the next slot; `start` (the Vikings
  where they start, or a cutscene's one entity); `placements` (position, box, kind, flags, argument, the kind's
  sprite entry); `tile_types` (the legend: number, name, colour, cells); and per cell the metatile and the tile type.
- `NN-world.png`: the map with **every placement on it**: its box outlined, with its kind's number in hex. Pixel
  values are the level's palette indices.
- `NN-collision.png`: a colour per **tile type** (the same colour in every level; the pixel value is the type's
  number), the legend below the map (number, name or `?`, cells), and the entities that are terrain to others
  outlined in white, with the colour of the type they act as inside the outline. Not written for the starfield,
  which has no map.
- `NN-tileset.png`, `NN-metatiles.png`: the level's 8×8 tiles and 16×16 metatiles, 16 per row.

An entity **acts as** a tile type through its field `26`: the others' tile tests find it as if it were cells of
that type ([`terrain.md`](../original/terrain.md#tile-tests-opcodes)). Of the 1,655 placements, 941 set one in their
first tick: `01` 176 (doors, elevators, blocks: ground), `05` 93, `0A` 4, `0C` 60, `12` 1, `14` 2, `16` 111, `17`
5; and numbers no map has, which tell kinds of entities apart: `29` 206, `2A` 7, `2C` 74, `2D` 8, `2E` 194. The
dump outlines the ones below `28`.
