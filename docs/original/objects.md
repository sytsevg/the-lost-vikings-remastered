# Objects: spawning, removing, the level's object list

**Status:** observed (static disassembly of `VIKINGS.EXE`). The engine's: `src/lvr/world.*`. Checked against the original through the reference engine (since retired): its state was the original's in DOSBox, step for step, on every path tried ([`../formats/unknowns.md`](../formats/unknowns.md#5-what-has-not-been-compared-with-the-original) lists them), and the engine matches its runs (`replays/`).
The camera is in [`camera.md`](camera.md), the whole level start in [`game-loop.md`](game-loop.md#the-level-start-00001080-1133). Field numbers and the per-object arrays are in
[`script-vm.md`](script-vm.md#object-fields).

## Records
An object type's record (`0x15` bytes, object database 449–454) as spawning reads it (`0000:3E52`):

| Offset | Size | Goes to |
|---|---|---|
| `+0` | word | sprite entry → field `2C` via the sprite table (`0xFFFF`: none; `0xFFFE`: none, pieces from pool 1); an entry the level didn't load makes the spawn fail |
| `+2` | byte | sprite-piece count (bits 0–6); bit 7: pieces from pool 2 |
| `+3` | word | script offset; the object starts at offset + 3 |
| `+7` | word | field `0A` |
| `+9` / `+A` | byte | width / height (per-object words `1445` / `146D`) |
| `+B`, `+D`, `+F`, `+11`, `+13` | word | fields `0C`, `0E`, `10`, `22`, `24` (`22`/`24` cap the movement, `0000:386B`) |

## Spawning (`0000:3809`)
Inputs: type (`ax`), the header-list index (`di`, `0xFFFF` = not listed), flags (`si`); position `ds:006C`/`006E`,
field 16 `ds:0374`, half size `ds:03E0`/`03E2` (`03E0` < 0: half the record's width and height), the spawner
`ds:0042`.
1. Fails during the game-over pass (`ds:032F`), and for a listed object whose **global flag bit** (`ds:0356`, bit
   = list index) is set: that is how collected or destroyed objects stay away.
2. The first free slot (code segment word = 0) of the 20; none → fails.
3. From the record: type, list index (field `18`), flags, field `16`, sprite set, piece count, program counter,
   code segment (`ds:2E67`), the fields above, x/y (also the previous x/y), spawner (field `28`). Cleared: fields
   `12`, `14`, `1C`, `26`, `2E`, `30`, `32`, `34`, `38`, `3A`, `3E`, `40`; `0xFFFF`: fields `2A`, `36`, `42`, word
   `141D`. Edges: left = x − width/2, right = left + width − 1, the same for top/bottom.
4. Sprite pieces, if any: the first run of free pieces (piece flags and redraw word both 0) in the pool:
   pool 0 = piece offsets `0x48`–`0x100`, pool 1 = `0x30`–`0x50`, pool 2 = `0x00`–`0x30`. Each gets flags
   ((object flags & `0xCE`) << 3) | `0x8000` | (object flag 1 ? `2` : `1`), size 32 or 8, redraw `0x204`, the
   sprite set and the sprite-buffer segment. No room → fails and frees the slot.
5. `ds:0372` grows to cover the slot.

## Removing (`0000:3C93`, opcode `10`)
Clears the pieces (flags 0, redraw `0x400`), unlinks the spawner's field `2A` and the last spawned object's field
`28`, frees the slot, sets field `42` to `0xFFFF`, and lowers `ds:0372` past empty slots. A listed object with
flag `0x0100` is then **created again at once** from its list entry if that is inside the camera's view (and its
global flag bit isn't set).

## Level start
After loading the slot's entries (`0000:1133`):
- **Sprite table** (`167A`): for each of the header's sprite sets, `ds:12AD` + 2i = the entry, `ds:12ED` + 2i = 1 +
  its offset in the sprite buffer (the sets are loaded one after the other, each its decompressed size). 32 at most.
- **Sprite pieces** cleared (`2FB3`).
- **The Vikings** (`1446`), by header byte 7:
  - 0 (scenes): one object of the type in header word `0x0C`, at (`0x08`, `0x0A`), flags `0x0E`, field 16 `0x10`.
  - 2, 4, 5: types 1, 0, 2 at the places in `ds:8508`, `851A`, `852C` (x, y, flags each; flags or-ed with header
    word `0x0E`). Level 1 uses mode 2. The remake reads these tables from `VIKINGS.EXE`
    (`Release::viking_starts`).
  - otherwise: type 1 at the header's position, type 0 and 2 at 32 and 64 pixels behind it (ahead if header flag
    `0x40`, facing left); mode `0x10` raises them by 16 and 8 pixels.
  - Then (not in mode 0) the Vikings' field `0A` = `ds:0423`/`0425`/`0427`; `ds:033C` = 6 (mode 0: 2), the first
    slot searched for listed objects.
- **Always-active objects** (`3BA5`): every list entry with flag `0x0800`.
- **Objects in view** (`3A0E`).
- One tick, physics, and the hit passes (`15D2`).

## Streaming with the camera
The header's object list (copied to `ds:25F6`; 14 bytes each: x, y, half width, half height, type, flags,
argument; argument → field 16) is not all alive at once.
- **View**: camera − 16 … + 336 across, camera − 16 … + 192 down (`39EF`), edges clamped to ≥ 0. Every tick
  `0000:673C` compares `ds:257F` / `2581` (the camera in 8-pixel cells, [`camera.md`](camera.md)) halved with the values
  of the last change (`ds:92F3` / `92F5`); when one changed, only the strip that came into view is checked: the 32
  pixels at the left (`3A14`) or right (`3A34`), then the top (`3A74`) or bottom (`3A54`).
- **In** (`3AE0`): an entry overlapping the view is created, unless an object from slot `ds:033C` on came from it
  (field `18` = its index).
- **Out** (`3C0C`, every tick from the main loop): objects from slot 3 on that are outside the view and not always
  active get the **restart** flag; their script's header entry then deals with it (usually removing the object).
  Here the left edge is clamped to 0 but the right edge comes from the unclamped one.

## Open
- What the records' remaining bytes (`+5`, `+6`) and the other fields set at spawn mean.
- Where the packed sprite sets are loaded (`16AE` fills `ds:124D`/`126D`/`128D`; not needed by the engine).
