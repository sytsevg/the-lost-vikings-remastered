# Game flow: start-up, levels, scenes, demos

**Status:** observed (static disassembly of `VIKINGS.EXE`; addresses as in [`timing.md`](timing.md)). Open points
are marked.

## Start-up (`0000:0000`)
1. Open `data.dat` (`0000:2989`) and read **entry 0**, the 32-byte configuration `SETUP.EXE` wrote, to `ds:86B0`
   (layout in [`../formats/data-dat.md`](../formats/data-dat.md#entry-0-configuration)).
   - Word 10 must be `0x6969`, else it prints that the data file is corrupted and stops.
   - **Copy protection:** a fingerprint of the machine (a checksum of the BIOS ROM at `F000:0000`, DOS "free disk
     space" for drive C, the DOS version) must equal word 9, else it prints a copy-protection message and stops.
     `SETUP.EXE` writes the fingerprint after asking for a word from the manual. Not reproduced.
2. Check for a VGA (`int 10h`, `AX=1A00h`) and a 386 (the FLAGS bits 12–15 test), else exit with a message.
3. Calibrate the joystick ([`input.md`](input.md#joystick)).
4. Load the fixed entries (`0000:2B0E`): the sound driver, instrument bank and sound-effect collection for the
   configured device ([`../formats/audio.md`](../formats/audio.md#sound-devices)), the scenes' object database
   **454**, and the HUD sprites **2** and **4–14**.
5. Start the sound (`0000:7561`). With an MT-32 (device variant 3) it prints a line that asks the player to
   wait and plays entry 533, which uploads the custom instruments.
6. Set the default key bindings and the password `STRT`, then load slot **39** (the Interplay logo).

## Slots
Every level and scene is a **slot**, an index into two tables in the data segment: header entries at `ds:940C` and
object databases at `ds:946C` (48 entries each). The current slot is `ds:25AD`, the previous one `ds:25AB`.

Loading a slot (`0000:1133`) loads its object database (unless it is already loaded, `ds:2E71`) and copies its
header to `ds:25B3`. That sets `ds:25C9` (header offset `0x16`) to the **next slot**, which the game loads when the
level ends. Then the header's lists are loaded ([`../formats/level.md`](../formats/level.md#level-header)).

| Slots | Headers | Object DB | Next (header `0x16`) |
|---|---|---|---|
| 0–36 | the 37 levels in play order (`kLevels`) | 449–453 by world | slot + 1; the last level of each world → 41 (galaxy); level 37 → 45 (starfield) |
| 37 | 369, moonlit sea | none (`0xFFFF`) | 4 (see below) |
| 38 | 381, title | 454 | 0 (level 1) |
| 39 | 390, Interplay logo | 454 | 40 |
| 40 | 396, Silicon & Synapse logo | 454 | 38 |
| 41 | 402, galaxy | 454 | 38 |
| 42 | **382**, title (identical copy of 381) | 454 | 0 |
| 43 | 431, village, intro | 454 | 44 |
| 44 | 432, village, demo | 454 | 0 |
| 45 | 433, starfield | 454 | 41 |
| 46 | 434, village, ending | 454 | 47 |
| 47 | 218, ship | 454 | 39 |

- **Slot 37** (369) is entered from two places: when no Viking is left to switch to (`0000:2E27`), and from pause-menu
  item 0 (`0000:04FA`). So it is the **game-over / give-up** scene, not a farewell. Its "next" (level 5) can't be
  what happens after it; open. Its objects are in every level's database (types `D2`–`D7`): the slot keeps the
  database loaded and skips the reset of the Vikings' health (`2CE4`), so `ds:0423`–`0427` still hold the health the
  HUD last recorded ([`game-loop.md`](game-loop.md#one-tick)). Erik (`D4`), Baleog (`D3`) and Olaf (`D5`) stay hidden
  while their word is 0; `D6` (the burning ship) only stays when one of them is 0. On an action key, `D2` brings each
  dead Viking back: a lightning animation (two 32 × 176 pieces whose frames are read straight from a packed set's
  segment, [`drawing.md`](drawing.md#sprite-pieces)), then his word = 6.
- **The galaxy** (41) is shown between worlds, but its header says "next = title": its script (database 454 at
  `8C00`) sets the next slot from the previous one (`ds:25AB`): 3 → 4, 10 → 11, 16 → 17, 24 → 25, 32 → 33 (the
  worlds' last levels), 45 → 46 (the starfield, at the end), anything else → 38 (the title). Checked in DOSBox
  (`tools/spotcheck.py flow`): after levels 4, 11 and 17 it plays 77 steps, then loads the next level.
  **Compared in lockstep** (the reference engine against the original): level 4 ended → the galaxy → level 5, and level
  1 ended into the galaxy → the title, every state the same, and the galaxy's picture pixel for pixel.
- **The game-over scene** (37), checked the same way: without a key it stays; a key restarts the level it came from
  (the previous slot), not its header's "level 5".
- How **slot 42** (header 382) is reached is open: no code sets it as a constant, and no path checked so far (the
  menu, the intro, the attract cycle, every level's end, the galaxy, game over) loads it. Perhaps unused.

## Header flags (offset `0x1C`) read by the loader
| Bit | Effect | Headers |
|---|---|---|
| `0x01` | load the HUD panel (entry 1) and the HUD | the 37 levels, the village scenes |
| `0x02` | image scene: the header's image into the three pages, **384** below it (`0000:1254`); no tileset, map or frames are loaded, so the last map stays | title (381, 382) |
| `0x20` | **529** into the HUD area, then load as a tiled scene (`0000:12A2`) | ship (218) |
| `0x40` | image scene: the header's image into the three pages, **531** below it (`0000:127B`); no map loaded, as `0x02` | starfield (433) |

Read elsewhere (every test of `ds:25CF`):
| Bit | Effect | Headers |
|---|---|---|
| `0x08` | Alt+X/Q/F10 quit at once, without the quit box (`0000:037F`; demo playback does the same) | Interplay, Silicon & Synapse, galaxy, title, game-over scene |
| `0x80`, `0x20`, `0x40` (`0xE0`) | the text layer is drawn straight onto the pages: at most 4 draws after a clear (`0C83:18A0`), its cells not marked in the cell cache (`0C83:190B`), and dialogue command 2 flips and draws it once more (`0000:2765`) | village scenes (`0x80`), ship, starfield |
| `0x04` | never tested | game-over scene (`0x0C`) |

## Demos
`ds:03CC` is the demo mode: `0x8000` = play back, `0x8001` = record (developer only), `0x8002` = recording stopped
([`input.md`](input.md#demo-recordings)).

| Slot | Recording |
|---|---|
| 43 (village intro) | **443**: the intro scene is driven by recorded input |
| 44–47 (village demo, starfield, village ending, ship) | **444**, an empty recording (no input) |
| attract-mode demos, in turn (`ds:03D4`) | level 3 → **418**, level 10 → **419**, level 9 → **420**, level 16 → **422**, level 24 → **421**, level 26 → **448** (tables `ds:2B66`, `ds:2B74`) |

When a demo ends (a key press, or the recording's end marker), `0000:02F0` decides what comes next: from the village
intro (43), a key press starts level 1; after an attract-mode level, the game moves on to the next demo in the table
and goes back to the Interplay logo (39).

## Passwords
Four characters per level at `ds:85A5` (bit 7 set on every character; the game masks it off). A password sets the
next slot directly (`0000:2870`).

| Level | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| | STRT | GR8T | TLPT | GRND | LLM0 | FL0T | TRSS | PRHS | CVRN | BBLS | VLCN | QCKS | PHR0 |

| Level | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 | 24 | 25 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| | C1R0 | SPKS | JMNN | TTRS | JLLY | PLNG | BTRY | JNKR | CBLT | H0PP | SMRT | V8TR |

| Level | 26 | 27 | 28 | 29 | 30 | 31 | 32 | 33 | 34 | 35 | 36 | 37 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| | NFL8 | WKYY | CMB0 | 8BLL | TRDR | FNTM | WRLR | TRPD | TFFF | FRGT | 4RN4 | MSTR |

(`0` is the digit zero.)
