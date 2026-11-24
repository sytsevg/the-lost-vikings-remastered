# Script VM

**Status:** observed: read from `VIKINGS.EXE` by static disassembly (2026-11-16). Every opcode handler was read.
The instruction **lengths** are verified against the real data: every script in the six object databases
(449–454, about 62,000 instructions reached from the records' entry points) decodes with no unknown opcode, no
invalid operand kind, and no two instructions sharing a byte (the `[real-data]` tests in
`src/assets/tests/test_script.cpp`). The **meanings** below
are as read from the handlers. The interpreter is **compared with the game** by lockstep (`tools/lockstep.py`): the same state as the original's,
step for step, on every path checked ([`../formats/unknowns.md`](../formats/unknowns.md#5-what-has-not-been-compared-with-the-original)
lists those and what is left). Cross-check: object type `3C` of the space world
(RyanMallon's "janitor alien") shows texts 38 and 39, which are the janitor dialogue
([`dialogue.md`](dialogue.md)).

Code: the decoder is `src/assets/script.*` (the opcode table with every operand format is in `script.cpp`).
Nothing runs the scripts any more: the reference engine did, and the engine's kinds are written by hand from the
listings.
`lv-export assets` writes a listing of each database (`scripts/NNN.txt`) and the opcode counts
(`scripts/usage.txt`). Addresses: [`static-analysis.md`](static-analysis.md).

## Where it is
The interpreter is in segment **`0000`**, not `0C83` as guessed earlier.

| Address | What |
|---|---|
| `0000:4207` | **run all objects, once per tick** (called from the main loop at `0000:0039`; also at level start `15D5` and on game over `015E`) |
| `0000:424C` | run one object's script until it yields |
| `0000:42A6` | the fetch loop: opcode byte → `call [ds:87CC + 2·opcode]` |
| `0000:5569` | second runner (phase `ds:0390 = −1`, from `5530`): opcode `01` ends the run instead of being a no-op |
| `ds:87CC` | opcode table, 216 entries (`00`–`D7`) |
| `ds:87AE`…`87CA` | shared tails just before the table: branch-if-carry-clear / -set, jump, skip-jump, operand readers |
| `ds:87B8` / `ds:87C2` | "get operand" / "set operand" by kind |

## Execution model
- **Object slots**: 20 at most (each field is an array of 20 words). `ds:0372` = 2 × (highest slot in use + 1).
- **Per tick** (`4207`): clear fields `38`/`3A` of every slot (`5517`, from the last slot down; it runs once even
  with no slot in use, and then clears the words just before the arrays), set `ds:0376 = 0` and `ds:0390 = 0`, then
  run slots in order `0, 1, 2…`.
  An object spawned during the pass into a **lower** slot than the running one is run right after (list at
  `ds:0378`, count `ds:0376`), so every object runs once in the tick it is created.
- **One object's run** (`424C`): skipped if the slot is empty (code segment field = 0). If flag `0x1000` is set and
  the countdown field `1C` is non-zero, the countdown drops by one. Then the program counter is loaded, and the
  object runs instruction after instruction **until a `yield`**. There is no instruction limit: a script that never
  yields hangs the game.
- **Restart**: while flag `0x0200` is set (or during the game-over pass, `ds:032F`), the run starts at the script's
  **header jump** (record offset + 0) instead of the saved program counter. If the object type has bit 15 set, the
  object doesn't run at all instead.
- **One-level call**: `call` stores the return address in a single per-object slot, `ret` jumps back to it; a call
  inside a call overwrites it. The object-search opcodes (`2C`, `35`, `D0`, `D1`) use the same slot.
- **Accumulator** `A` (`ds:008A`) is one global register shared by all objects. It is not saved across a yield.

## The object database (449–454)
- **235 records** of `0x15` bytes from offset 0 (the same count in every database), then 35 byte lists, each
  ending in `0xFF` (offset `0x1347`, identical in all six databases), then the scripts (from `0x13DB`) and the
  animation programs.
- A record's word at `+3` is the script's offset in the database. **A spawned object starts at offset + 3**; the
  three bytes at offset + 0 are a `jmp` to the script's second entry (used by the restart above).
- The other record bytes are read when an object is spawned: [`objects.md`](objects.md#records).

## Object fields
An operand "field `f`" (an even byte `00`–`42`) names a per-object word at `ds:14E5 + (f/2)·0x28 + 2·slot` (offset
table `ds:9346`). "Target" operands use the object in field `3C`.

| Field | Meaning (from the handlers) |
|---|---|
| `00` / `02` | top / bottom edge (y) |
| `04` / `06` | left / right edge (x) |
| `08` | flags: `0x0040` facing left, `0x0080` flipped vertically, `0x0200` restart at the header jump, `0x1000` countdown runs, `0x2000` set by `4B` |
| `0A` | argument (from the level's object list) |
| `0E` | class compared by `hit_class` (`1A`, `37`) |
| `10` | bit mask tested by `hit_mask` (`1D`, `38`) |
| `12` / `14` | x / y velocity |
| `18` | index of a global flag bit (`0D`/`0E`) and of the header's list entry (`C7`–`CA`) |
| `1A` | object type (record number) |
| `1C` | countdown (see above) |
| `1E` / `20` | x / y |
| `26` | kind, matched against the byte lists by the search opcodes |
| `2A` | slot of the last object spawned by `14` |
| `2C` | sprite set |
| `38` / `3A` | set by `17`/`18`/`1B`/`48` (cleared every tick) |
| `3C` | target object (slot × 2) |
| `42` | animation program (set by `19`) |

Hidden per-object words before `ds:14E5`: program counter `132D`, code segment `1355`, return address `137D`,
previous x/y `13A5`/`13CD`, latched hit bits `13F5`, `141D` (the source of fields `38`/`3A`: 0, the target, the
spawner's slot, or `0x100` for `48`). After the fields: `1A85` / `1AAD` = the object's sprite pieces (offsets
[first, end)), `1AD5` = piece count. The piece arrays are at `ds:044D` + n × `0x100` (`044D` flags: `0x0200`
mirrored, `0x0400` flipped; `064D` / `074D` x / y; `0C4D` size; `114D` redraw), changed by `0B`/`0C`/`3F`/`40`.

## Operands
- `b` u8, `s` s8, `w` u16; `j`/`c` u16 jump or call target (offset in the database).
- **Mode byte** (`G2`/`S2`): bits 0–2 = kind of the first operand, bits 3–5 = kind of the second; the operands
  follow in that order. `G1`/`S1` use only bits 0–2.

| Kind | Get | Set | Bytes |
|---|---|---|---|
| 0 | literal | (jumps to garbage) | 2 |
| 1 | field of this object | same | 1 |
| 2 | global `ds:` word | same | 2 |
| 3 | field of the target | same | 1 |
| 4 | random number | (jumps to garbage) | 0 |

- A "bit" operand `b` selects bit `b/2` (masks `ds:93CC`, inverted masks `ds:93EC`).

## Instruction set
`A` = accumulator. "jump" = take the `j` target, otherwise continue after it. Kinds in a group of five are, in
order: literal `w`, field `b`, global `w`, target field `b`, random (no operand).

| Opcodes | Operands | Meaning |
|---|---|---|
| `00` | | **yield**: save the program counter, end this object's run |
| `01` | | no-op; ends the run in the second runner (`5569`) |
| `02` | `w` | play sound effect (low byte), up to 4 at once |
| `03` | `j` | jump |
| `04`, `D7` | `b` / `b w` | stop sound effect |
| `05` / `06` | `c` / | call / return (one level) |
| `07` / `08` | | face left / face right (flip if needed) |
| `09` / `0A` | | flip vertically if not / if already flipped |
| `0B` / `0C` | | flip horizontally / vertically (also flips the sprite pieces) |
| `0D` / `0E` | | clear / set global flag bit (field `18`) in the bit array `ds:0356` |
| `0F` | | request the next slot (`ds:0334 |= 1`) and end the run |
| `10` | | remove this object, end the run |
| `11`, `12`, `3A` | | compare this object with the target (`0000:5505`)? |
| `13` | `b w` | system: `D9` = copy 16 colours (48 bytes) from the object database at offset `w` to colours 192–207 and 224–239, `11` = clear the screen, `01` = **quit to DOS** |
| `14` | `G2 G2 b` | spawn object type `b` at (x, y); the new object's field `16` = the third value, its flags = (own flags & `0x00FE`) \| (fourth value & `0x0801`) |
| `15` / `16` / `34` | `S2` | store (dx, dy) to the current Viking (`ds:03C2`) / target / nearest living Viking (slots 0–2 with field `0A` ≠ 0, smallest \|dx\| + \|dy\|, into `ds:03CA`), mirrored by facing |
| `17`, `18`, `1B` | `s s` | set fields `38`/`3A` (with `141D` = 0 / target / this, `1B` on slot 0) |
| `19` | `w` | start animation program `w` ([`animation.md`](animation.md)) |
| `1A` / `37` | `b c` | call if touching an object of class `b` (`37`: all slots, `1A`: Vikings only) |
| `1C` | `j` | jump if the animation's delay is 0 |
| `1D` / `38` | `w c` | call if touching an object whose field `10` has a bit of `w` |
| `1E`–`25`, `30`, `31`, `BF`–`C6` | `b j` | tile tests with type list `b` ([`terrain.md`](terrain.md#tile-tests-opcodes)); even = jump if found, odd = if not |
| `26`, `28` | `G2 S2` | pixels → cells (`>> 4`); the centre of the pixel's cell (`& ~15 \| 8`) |
| `27` | `G2 S1` | the tile type at cell (x, y) |
| `29`, `2A`, `2B` | `G2 G1` | write cell (x, y): the whole word / the metatile / the type (`29`, `2A` redraw it) ([`terrain.md`](terrain.md)) |
| `2C`, `35`, `D0`, `D1` | `b c` | **for each** object of a kind in list `b` at the row above this object (top − 1; `D0`/`D1`: below, bottom + 1) and overlapping it across: it becomes the target, `c` is called and returns to the next instruction (`2D`/`36`), which continues the search; when none is left the `2D`/`36` is skipped. `2C`/`D1`: the Vikings' slots only, unsigned, the row inside the other's box; `35`/`D0`: all slots, signed, and the other must lie **above** the row (row − 1 ≥ its bottom; a quirk) |
| `2E` / `3B` | `w` | screen shake x / y |
| `2F` | | one animation step, then the speed and gravity step |
| `32`, `33`, `3C` | `b c` | call on a hit test (phase-dependent, see below) |
| `39` | `b w` | no-op |
| `3D` / `4C` | `b b b` | palette tint r, g, b (two independent tints); `3E` / `4D` clear them |
| `41` / `44` / `45` | `G2 G2` / `G2 G2` / `G1 G2` | **show a text box** ([`dialogue.md`](dialogue.md)) |
| `42`, `43`/`CB`, `46 w`, `50 G2 G1` | | other text-box commands |
| `47` | | no-op |
| `48` | `G2` | fields `38`/`3A` = (x, y) − own position |
| `49` / `4A` | `G2 b j` | tile test at a point |
| `4B` | | flags \|= `0x2000` |
| `4E` / `4F` | `j` | can it walk on (`0000:63AC`): jump if so / if not |
| `51`–`55` | kinds | `A` = operand |
| `56`–`58` | `b`/`w`/`b` | store `A` to field / global / target field |
| `59`–`67` | `b`/`w`/`b` | field op= `A`: add, sub, and, or, xor |
| `68`–`6C` / `6D`–`71` | kinds `j` | jump if `A` ≥ / < operand (unsigned) |
| `72`–`76` / `77`–`7B` | kinds `j` | jump if `A` = / ≠ operand |
| `7C`–`80` / `81`–`85` | kinds `j` | jump if `A` ≥ / < operand (signed) |
| `86`–`8A` / `8B`–`8F` | kinds `c` | call if `A` = / ≠ operand |
| `90`–`92` / `93`–`95` | `b`/`w`/`b` | add / subtract `A`, the other way round when facing left |
| `96` | | target = `A` |
| `97`–`9A` | `b` + operand | `A` = bit `b` of the operand (0 or 1); `9B`: `A` = random & 1 |
| `9C`–`9E` | `b` + operand | set bit `b` of the operand to (`A` ≠ 0) |
| `9F`–`A7` | `b` + operand | `A` = `A` ≠ 0 ? mask : 0, then and / or / xor it in |
| `A8`–`AC` / `AD`–`B1` | `b` + operand `j` | jump if `A` = / ≠ bit `b` of the operand |
| `B2`–`B6` / `B7`–`BB` | `b` + operand `c` | call if `A` = / ≠ bit `b` of the operand |
| `BC`–`BE` | `b`/`w`/`b` | store `A << 8` |
| `C7`–`CA` | | store `A` in the current level header's object list (`ds:25F6 + 14·field 18`), at +0, +2, +A, +C |
| `CC`–`CF` | `j` | jump if this object / the target is (not) on screen (camera `ds:0044`/`0046`, x + 31…+289, y + 31…+145) |
| `D2` / `D3` | | password: this slot's (`ds:85A5` + 4 × `ds:25AD`, bit 7 off) into `ds:0310`–`0316` / look the typed one up: the next slot `ds:25C9`, or `ds:0308` = 1 if none |
| `D4` | `G2 b` | set the velocity (fields `12`/`14`, 8.8) towards (x, y) at speed `b`: the distances halved until the longer fits `b` |
| `D5` / `D6` | `b` / | restart / fade out the music |

## Phases of the hit opcodes
`1A`, `1D`, `32`, `33`, `37`, `38`, `3C` test in one of two extra passes per tick (`5546` with `ds:0390 = +1`, the
terrain; `5530` with −1, the other objects), latch a bit, and act on it in the next tick's run: see
[`hits.md`](hits.md).

## Random numbers (`0000:2312`)
- Normal play: a 32-bit LCG at `ds:8639`: `seed = seed × 0x015A4E35 + 1`, result = the seed's high word. The
  start-up (`0000:2948`) sets it once from the clock, `int 21h` `2Ch`: low word DX (seconds, hundredths), high word
  CX (hours, minutes). It is the only thing the game takes from the clock. The remake starts it at 0.
- **Demo playback** (`ds:03CC` ≠ 0): a separate 16-bit generator at `ds:0352`: swap its bytes and store them, rotate
  that left by 3 through the carry (clear, from the `cmp` before it), xor the rotation into the store, and return
  the rotation. So demos replay the same way whatever the main seed is.

## Quirks to keep in vanilla mode
- `A1` reads its field number and then uses `A` instead (it adds to the target field at index `A`). No script uses it.
- "Set" with kind 0 or 4 returns into garbage; no script uses it.
- `45` reads only the first operand of its first mode byte.
- `0F` and `10` end the run without saving the program counter.
- With no slot in use, the per-tick clear of fields `38`/`3A` runs once at slot offset −2, so it zeroes slot 19's
  fields `36` and `38` (`ds:1943`, `196B`).
- `14` queues a lower-slot spawn by writing a **word** at `ds:0378 + count` (count + 1 per spawn); the runner reads
  the low bytes.

## Open
- The per-object words before `14E5`, record bytes `+D…`, fields not in the table above.
- The animation programs are read ([`animation.md`](animation.md)); they can't be decoded without running them (per-piece
  operands), so the listings don't show them.
- The byte lists after the records (database offset `0x1347`) are the same bytes as `ds:94CC`, the type lists the
  tile tests and searches read ([`terrain.md`](terrain.md#the-tile-map)).
- 52 opcodes never occur in the data (`scripts/usage.txt`).
