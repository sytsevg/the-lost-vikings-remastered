# Hit tests

**Status:** observed (static disassembly of `VIKINGS.EXE`). The engine's: `src/lvr/hits.*`, `looks.*` and `terrain.*`. Checked against the original through the reference engine (since retired): its state was the original's in DOSBox, step for step, on every path tried ([`../formats/unknowns.md`](../formats/unknowns.md#5-what-has-not-been-compared-with-the-original) lists them), and the engine matches its runs (`replays/`).

The opcodes `1A`, `1D`, `37`, `38` (another object touches this one) and `32`, `33`, `3C` (the terrain) don't test
when the script runs them. Each tick has two extra passes over every object, and each hit opcode does its test in
one of them and **latches** a bit; when the object's script runs in the next tick, the opcode calls its handler if
its bit is latched.

## The passes
| Pass | `ds:0390` | Where in the tick | What runs |
|---|---|---|---|
| scripts (`4207`) | 0 | after input and the scroll check | each hit opcode: call `c` if its bit is latched |
| contact (`5546`) | +1 | right after the movement (`386B`, `625D`) | first clears every slot's latched bits (`13F5`); then the objects that nothing else moves (word `141D` = `0xFFFF`): `32`, `33`, `3C` test |
| hit (`5530`) | −1 | after the Viking switch (`2E16`), before drawing | every object: `1A`, `1D`, `37`, `38` test |

In both passes an object's script runs from its saved program counter (no restart, no countdown) in the second
runner (`5569`), until an `01` (a no-op in the normal run). A `yield` met there saves the program counter as usual.
So the scripts put their hit opcodes right after a `yield`, followed by `01`: the same instructions test in a pass and
act in the next tick.

**Bits**: the hit opcodes of one run are counted by `ds:038E` (+2 each, from 0); the n-th uses bit n of the object's
latch word `13F5` (masks `ds:93CC`; a 17th and later would use the inverted masks after it).

## Object tests (`55D6`, `56C0`)
- `37 b c`: an object of class `b` (field `0E`); `38 w c`: an object with a bit of `w` in field `10`. `1A` and
  `1D` are the same with the search limited to the Viking slots 0–2 (`ds:0372` = 6 while they test).
- The test: the first other object, from slot 0 up, whose box overlaps this one's: this right ≥ its left, its
  right ≥ this left, this bottom ≥ its top, its bottom ≥ this top (signed, edges included). This object's box is put
  in `ds:0034`–`003A` on the way.
- A hit latches the bit and stores the object (slot × 2) in the hit table (`ds:1B25` + slot × 32 + `038E`). When
  the call is taken, that object becomes the **target** (field `3C`).

## Terrain tests (`5788`, `57EB`, `584E`)
`32 b c`, `33 b c`, `3C b c`: in the contact pass each tries the tiles, then objects as terrain (`32`: `58F5` then
`5C37`; `33`: `5911`, `5C93`; `3C`: `5AFD`, `614E`), pushes the object back (`592D` / `5D6B`, `5972` / `5DA8`) and
latches on a hit: [`terrain.md`](terrain.md#terrain-hits-32-33-3c-and-the-push-back). When called, the target is
unchanged.

## Open
- Nothing specific to the hit opcodes; see [`terrain.md`](terrain.md#open).
