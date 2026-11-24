# The Vikings: keys, abilities, movement, falling

**Status:** observed (the Vikings' scripts in the level databases, read with `lv-export` in
`gamedata/exported/scripts/449.txt`; addresses below are in database 449, the first world's; the other worlds'
databases hold the same code at other offsets). The remake runs these scripts as they are (`lvr::ref::sim`), so it
needs no code of its own for them; **compared with the game** by lockstep (`tools/lockstep.py`, 2026-11-17): every
level entered and played with each key on each Viking, the same state as the original's step for step.

The Vikings are ordinary objects: Erik is type `01` in object slot 0, Baleog type `00` in slot 1, Olaf type `02` in
slot 2 ([`objects.md`](objects.md)). Field `1A` holds the type (and bit 15 once dead,
[`game-loop.md`](game-loop.md#one-tick)); field `0A` is the health × 2.

## Keys
Each script reads the input only while it is the current Viking (`ds:0042` = `ds:03C2`), and the action keys only
while one of them is held (`03B6` & `0xC0C0`). Bits as in [`input.md`](input.md); "pressed" is newly pressed
(`03B8`), "held" is `03B6`.

| Key (bit) | Erik (`01`, `42B2`) | Baleog (`00`, `3896`) | Olaf (`02`, `4F1B`) |
|---|---|---|---|
| F (`0x8000`), pressed | **jump** (`42D8`) | **sword**: one of two swings, by the random number (< `0x7530`: `3BD7`, else `3C30`) | **shield** front ↔ up (`5075`) |
| D (`0x4000`) | pressed, running at ≥ `0x600`: **head-butt** run (`440C`) | held: **bow** drawn (`3B36`); let go: the arrow (type `07`, `3B9D`) | pressed: **shield** front ↔ up, as F |
| S (`0x80`), pressed | act on a switch, button or door next to him (`5E59`: tile type `75`, or an object with it) | the same | the same |
| E (`0x40`), pressed | **use the item** in his chosen square (`6EBF`, by Viking: `03E4` + 8 × Viking + 2 × chosen square) | the same | the same |
| Up / Down, held | climb a ladder (tile `29` above / `2D` below) | the same | the same |
| Left / Right, held | walk / run (below) | the same | the same |

## Movement
Positions are in pixels, speeds in 1/256 pixel per tick (8.8 fixed point; the fraction kept in fields `3E`/`40`,
[`terrain.md`](terrain.md)). Field `12` is the horizontal speed, field `14` the vertical (down is positive).

- **The speed step** (animation `2F`, `35CF`; [`animation.md`](animation.md#the-speed-step-35cf)): the level's
  **gravity** (header word 2, `ds:25B5`; `0xF8` in level 1) is added to field `14` each tick, and both speeds are
  capped to ± fields `22`/`24` (`0x1000` for the Vikings: 16 pixels a tick).
- **Running** (Erik `46BB`–`4723`): each tick a direction is held, field `12` grows by `0xC0` (`0x100` below
  `0x100`, `0x180` while still moving the other way) up to the Viking's top speed: Erik `0x800` (8 pixels a tick,
  about 159 a second), Baleog and Olaf `0x600`.
- **Jumping** (Erik only, `42D8`): field `14` −= `0x980`, `0xA00` at a running speed of `0x600`, `0xA80` at
  `0x800`: the faster he runs, the higher he jumps.
- **Falling** (Erik `4740`): field `14` ≥ `0x1000` is a long fall (its animation, and the mark that makes the landing
  hard); ≥ `0x300` falling; ≤ −`0x300` rising.
- **Olaf's shield up** while falling (`5359`): field `14` is held at `0x200` at most: he glides, and never lands
  hard.

## Landing hard (`60FF`)
Landing at field `14` ≥ `0x1000` (the speed cap, so after a fall long enough to reach it): both speeds 0, the screen
shakes vertically (opcode `3B` with `0x0201`), and unless `ds:0276` is set, the Viking loses one point
(`6469`: field `32` = 2, subtracted from field `0A`) and plays his dizzy animation (by field `1A`: `6BBE` Erik,
`6C02` Baleog, `6C38` Olaf); with no health left, he dies (`628F`).
