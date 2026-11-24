# Animation programs

**Status:** observed (static disassembly of `VIKINGS.EXE`, 2026-11-17). The engine's: `src/lvr/sprites.*`. Checked against the original through the reference engine (retired 2026-11-23): its state was the original's in DOSBox, step for step, on every path tried ([`../formats/unknowns.md`](../formats/unknowns.md#5-what-has-not-been-compared-with-the-original) lists them), and the engine matches its runs (`replays/`).

A second, smaller bytecode in the same object database, that moves and changes an object's **sprite pieces**
([`objects.md`](objects.md)). Scripts drive it:
- `19 w`: start the program at offset `w`: field `42` = `w`, delay (word `1A35`) = 1.
- `2F`: one step (`0000:3031` → `303A`, then the speed step `35CF`). Scripts call it once per tick, before `yield`.
- `1C j`: jump if the delay is 0 (the program has finished waiting, or ended).

## One step (`303A`)
Nothing if field `42` = `0xFFFF`. Otherwise the object's delay, return address (word `1A5D`) and piece range
(`1A85`/`1AAD`) are copied to `ds:0078`/`007A`/`007C`/`0080`, the piece mask `ds:038C` = 0. If the delay is
non-zero it counts down; the program runs when it reaches 0 (or was 0), opcode after opcode (table `ds:86E6`),
until one ends the step (`0E`, `0F`, `1A`). Then field `42` = the program counter, and the delay and return address
go back to the object.

**Mask**: while `ds:038C` ≠ 0 (set by `0D`, cleared at every step), opcodes marked *masked* only touch the pieces
whose group (piece word `054D`) has a bit of it, and per-piece operands are only read for those pieces. So a
program's length depends on the object's piece count and groups: it can't be decoded without running it.

## Opcodes

| Op | Operands | What it does (piece arrays by offset, below) |
|---|---|---|
| `00` | `b` | *masked*: frame (`084D`) += n × 72 |
| `01` | `b` per piece | *masked*: frame = n × 72 + field `2C` |
| `02` | `w` | sound effect (low byte), the same routine as script opcode `02` |
| `03` / `05` / `06` | `w` / `w` / | jump / call / return (one level, the return address is kept per object) |
| `04`, `16` | `b` | nothing (skips the byte) |
| `07` / `09` | `s` | move by n, mirrored by facing left / flipped. *Masked* with a mask: the pieces' x / y; without: fields `38` / `3A` += n × 256 (8.8 fixed point) |
| `08` / `0A` | `w` per piece | each piece's x / y = object x / y + w (the first time also `0D4D`/`0F4D` / `0E4D`/`104D`); `08` then mirrors the pieces around x if facing left; `0A` see quirks |
| `0B` | | `int 3`: nothing |
| `0C` | `b` per piece | flags bits 4–6 = (n << 3) & `0x70` (colour); see quirks |
| `0D` | `b` | piece mask for the rest of the step |
| `0E` | | end of the step |
| `0F` | `b` | wait n steps, end of the step |
| `10`, `11`, `12` | | *masked*: flags ^= `0x200` (mirror), `0x400` (flip), `0x600` |
| `13` | `b` per piece | *masked*: group = n |
| `14` | `b` | decompress packed frame n into the first piece's buffer, if it isn't field `36` already (field `36` = n) |
| `15` | `b` | *masked*: size and drawing mode: 0 → 8 px, mode 1; 1 → 32, mode 2; 2 → 16, mode 4; 3 and up → the word at `cs:32D7` + 2n, mode 2, frame = the packed set's offset + 1 (also field `2C`) |
| `17` | `w` | all pieces draw from packed sprite set `w` (table `ds:124D`; not found: the first), frame buffer from `ds:871C` by piece, flags \|= `0xA`; field `36` = `0xFFFF` |
| `18` / `19` | | *masked*: hide (flags \|= `0x4000`) / show (flags &= `0x9FFF`) |
| `1A` | | the end: field `42` = `0xFFFF` |

Every change marks the piece for redraw (word `114D`). Piece arrays: `044D` flags (bits 0–2 mode, 4–6 colour,
`0x200` mirrored, `0x400` flipped, `0x800`/`0x1000` y/x set, `0x4000` hidden, `0x8000` in use), `054D` group, `064D`
/ `074D` x / y, `084D` frame, `094D` its segment, `0A4D`/`0B4D` packed set (far pointer), `0C4D` size, `0D4D`–`104D`
first x/y, `114D` redraw.

## The speed step (`35CF`)
Unless something else moves the object (word `141D` ≠ `0xFFFF`): flag `0x8000` adds the level's gravity (header
word 2, `ds:25B5`) to the y velocity (field `14`), flag `0x4000` header word 0 to the x velocity (field `12`); both
are capped to ± fields `22` / `24`, then added to fields `38` / `3A` (negated when facing left / flipped).

## Quirks to keep in vanilla mode
- `0C` with a mask tests the group of piece `0x18` (the opcode's own table index) instead of each piece's.
- `0A` mirrors if bit 7 of the word at `ds:1585` + the piece end offset is set, i.e. the flags of the "object" at
  that offset, and then mirrors that "object's" pieces.
- `15` with n ≥ 4 reads its size from the code after the 4-word table.
- `17` reads `ds:871C` by (piece − `0x30`): pieces outside pool 1 read past the table.

## In the engine
`src/lvr/sprites.*` plays the same programs on its own pieces (`Piece`, `Sprite`, `Body`), checked against this
one step by step over the replays (`src/lvr/tests/test_sprites.cpp`). Kept: the `0C` quirk (it changes how many
operands are read) and `17`'s frame table by piece. **Left out**: `0A`'s mirroring; in the replays its test fires
about 330 times, always for a piece offset of 50–64 used as an object slot (past the 20 there are), where the
"object" has no pieces, so nothing is mirrored and no piece differs. The speed step is the world's, not the
animation's.

