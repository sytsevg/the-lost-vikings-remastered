# Static analysis of the executables

How we read `VIKINGS.EXE` and `SETUP.EXE` directly (ADR-0001 update, 2026-11-16), what we know about their layout,
and every routine and variable identified so far. Findings go into the other `docs/original/` pages as **rules**; this
page is the toolbox and the address book. Never copy disassembly into the docs or the code (ADR-0001).

## Tools
- `objdump -D -b binary -mi8086 -M intel` (binutils, in the dev container) disassembles 16-bit real-mode code.
- **`tools/disasm.py`** wraps it and adds the searches below. It reads `gamedata/original/VIKINGS.EXE` (or
  `--exe SETUP.EXE`), strips the MZ header, and uses `segment:offset` addresses everywhere.

| Command | What it does |
|---|---|
| `dis 0000:001e 0000:0130` / `dis 0bad:040c +40` | disassemble a range (end exclusive) or a hex length; near jump/call targets are shown segment-relative |
| `words 184e:a384 11` / `bytes 184e:85a5 16` | dump a table from the data segment as words, or as hex + ASCII |
| `callers 0000:0982 [-c N]` | every near `call`/`jmp` (`E8`/`E9` rel16) from the same segment and every `call far` (`9A off seg`) to a routine, each with the N instructions leading up to it, which usually show the arguments (`mov ax, 1C7h`) |
| `refs 25c9 [--code\|--data]` | every occurrence of a 16-bit value: a variable's address, a constant, an entry number |
| `strings [--min N]` | printable strings with their address; DOS `$`-terminated strings are split |
| `segments` | the segments, from the relocation table |

The object scripts and the dialogue texts are decoded by `lvr::assets` (`src/assets/script.*`, `text.*`);
`lv-export assets` writes listings of all six databases (`scripts/NNN.txt`), the opcode counts (`scripts/usage.txt`)
and the texts (`dialogue/texts.txt`); see [`script-vm.md`](script-vm.md).

`callers` can't see calls through tables or registers (`call word [bp+7974h]`, `jmp word [si-5C86h]`): find the table
with `refs`/`words` and follow its entries.

## Layout of `VIKINGS.EXE` (vanilla-dos, 142,224 bytes)
- MZ header of 32 paragraphs (512 bytes), 93 relocations, entry `0000:0000`, stack `2289:0100`. **Not packed.**
  The header starts with a greeting from a programmer to his family.
- Segments (from the relocated segment words; `image offset = segment × 16 + offset`):

| Segment | Image | Size | Contents |
|---|---|---|---|
| `0000` | `0x00000` | `0x7A60` | the game: main loop, loaders, input, sound glue. Looks hand-written assembly: arguments in registers (`ax`, `bx`, `si`, `di`, `es:di`), `jmp word [table+si]` dispatch, no C runtime |
| `07A6` | `0x07A60` | `0x4070` | **the dialogue text**: 390 upper-case ASCII texts behind a table of offsets ([`dialogue.md`](dialogue.md)) |
| `0BAD` | `0x0BAD0` | `0x0D60` | **Miles Design AIL 2** (Audio Interface Library): timer services and the driver interface, C calling convention (`[bp+6]` = first argument of a far procedure), its own data addressed through `cs:` |
| `0C83` | `0x0C830` | `0xBCB0` | drawing: the piece blitters, the restore under the pieces, the text cells, the palette and fades ([`drawing.md`](drawing.md)); the rest not read in detail. (The script interpreter is in `0000`.) |
| `184E` | `0x184E0` | `0xA4B0` | data segment (`ds`) |

- The data segment's value is relocated, and code also loads it as an immediate (`mov bp, 184Eh; mov ds, bp` in the
  interrupt handlers).
- **Negative displacements** are 16-bit addresses: `[si-6E14h]` is `ds:91EC + si` (`0x10000 − 0x6E14 = 0x91EC`).
- An offset past `0x7A5F` "in segment 0000" is really in `07A6`. `dis` doesn't check this.

## Layout of `SETUP.EXE` (33,552 bytes)
Borland C++ 1991 (its copyright string is in the file). Segments `0000` (code), `0626`, `06EA` (data, file offset
`0x70A0`). Source file names in its assertions: `DRIVER.C`, `FILE.C`, `WIN.C`, `MMGR.C`, `GRAPH.C`, `KEYS.C`,
`OTHER.C`, `ERROR.C`.

## Recipes, with the cases that worked
**1. Find code by the hardware it touches.** Search the bytes for port I/O and DOS/BIOS calls, then disassemble
around the hits:
- PIT: `B0 36 E6 43` (`mov al,36h; out 43h,al`), then `out 40h` twice. Led to the AIL timer code and its retrace
  lock ([`timing.md`](timing.md)).
- VGA retrace: `BA DA 03` (`mov dx,3DAh`), then `in al,dx; test al,8`. Mode X: `BA C2 03` + `B0 E3` (misc output),
  the CRTC table (`06 0D`, `07 3E` = vertical total 525).
- DOS files: `B8 00 3D` (open), `B4 3F` (read), `B8 00 42` (seek). Found the `DATA.DAT` loaders.
- Keyboard: `E4 60` (`in al,60h`) in an interrupt handler (ends with `out 20h` and `iret`).

**2. Find code by its strings.** `strings` gives a message's `ds:` offset; `refs` on that offset finds the code that
prints it (`mov dx, offset` or `mov ax, offset` before the error routine `0000:0DBA`). The copy-protection message →
the fingerprint check; `data.dat` (`ds:86A3`) → the file open and the configuration read.

**3. Find every use of a routine.** `callers` on a loader listed all 28 calls of `0000:0982`, each with its
`mov ax, <entry>`. Constants gave the fixed entries; `add ax, [86B6h]` / `[86B8h]` gave "entry = base + driver /
variant"; `[bx+A384h]` led to the tune table. A search for `call far` into segment `0000` (only two: the retrace
handler and the error exit) showed no other segment loads entries, which **proved** which entries the game never
loads ([`../formats/unknowns.md`](../formats/unknowns.md)).

**4. Find tables by the numbers in them.** `refs <entry> --data` for each unexplained entry number found the demo
table (418–422, 448 at `ds:2B74`, next to their levels at `ds:2B66`) and the 48-slot header table at `ds:940C` with
the object databases right after it (454 ten times). A table of words that rise in small steps, or a run of the same
value, is a good sign.

**5. Find who writes a variable.** `refs` on its address, then look for the store forms: `A3 lo hi`
(`mov [addr],ax`), `C7 06 lo hi imm` (`mov word [addr],imm`), `FF 06` (`inc`), `83 0E lo hi imm8` (`or`). This is
how `ds:25C9` turned out to be header offset `0x16` (the header is copied to `ds:25B3`): nothing wrote it except the
header copy, a password, and the demo/scene code.

**6. Recover a table of string pointers (SETUP).** Take the strings' file offsets, compute the gaps between them, and
search for words with the same gaps: the menu table at file `0x76EC` gave the data segment base (`0x70A0`) and its
offset (`ds:064C`). `refs` on that offset led to the menu code and the choice → driver / variant tables.

**7. Recognise library code and read its data, not its code.** The Miles AIL timer API (register timer, set
frequency → `1000000 / hz` µs → PIT divisor) and the AIL 2 driver descriptor (`C8 00` = version 200, type 3 =
XMIDI, port/IRQ/DMA/DRQ defaults, **service rate** in Hz) gave the 120 Hz rate of all nine drivers without
disassembling them.

**8. Check by looking.** When code says "these sprites belong to slot 37", render them: the export of 414–417
spelled G·AME·O·VER, which named the scene. Colour indices of an image against a palette's range (529 uses 48–105,
only the ship palette 214 covers them) settle which palette is right.

**9. Follow a value read from data into the code that reads it.** The sound-effect routine `77B2` reads its
number from `es:[bx]` and steps `bx`: an interpreter. `refs 77b2` found it only in a data table (`ds:87D0`), whose
neighbours are all handler addresses: the opcode table `ds:87CC`. The one `call [si+87CCh]` (`ff 94 cc 87`) is the
fetch loop. Then: read every handler, write each one's operand format into a decoder, and run the decoder over
all the real data. No errors and no overlapping instructions means the formats are right.

**10. Search for text in upper case too.** The dialogue is plain ASCII, but upper case only.

## Pitfalls
- **objdump decodes linearly**, so data inside code, or a start in the middle of an instruction, decodes as
  garbage: `(bad)`, odd segment prefixes, `int 3Fh`, `fdivr`… Start from a known routine (a call target or an
  interrupt vector) and stop at `ret`/`jmp`. `callers -c` finds a clean start by trying several.
- A `refs` hit in code may be part of another instruction. Always `dis` around it.
- AIL routines take C-style stack arguments. The game's own routines take registers, so read the instructions
  before a call to see what it passes. (One game call into AIL even pushes its arguments in the wrong order and
  works only because the timer handle is 0: `0000:769F`.)
- Variables the code never writes have their value in the EXE: read them with `words` (e.g. the level-keys switch
  `ds:0202` = 0; machine code never writes it, the title's script does).
- In Python, search for bytes with plain literals (`b'\xB8\x00\x3D'`), not `rb'…'` with `re.escape`.

## Address book
Routines in `0000` unless the segment is given. "?" means seen but not understood.

| Address | What |
|---|---|
| `0000` | start-up: the calls to the init routines, then the main loop at `001E` |
| `001E`–`012D` | **in-level main loop**: three phases of game code, wait, draw, flip ([`timing.md`](timing.md#game-loop)) |
| `0130` | wait for the retrace counter `ds:A39C` to drop below 1 |
| `0138` | act on the state-change requests in `ds:0334` (1 = load the next slot, 2 = game over, 4 = wait-for-key screen) |
| `0164` | a screen loop (three flips per pass) that runs until an action key is newly pressed (input bits `0xC0C0`: E, S, D, F/Space/Enter) |
| `01BE` / `0FFC` | palette animation: count down and rotate / reload the counters and upload the colours ([`game-loop.md`](game-loop.md#one-tick)) |
| `12AE` / `133A` / `1383` | the header's palettes (then every 16th colour black) / its palette animations / find the end of its object list |
| `02AD` / `02F0` | pause key during a demo: end the recording, or decide what follows the demo |
| `0350` | after the tick: Alt+X/Q, F10 quit at once or open the quit box (`0389`, `03CA`; passes `04A1`) |
| `041C`, `047C`, `04C3`, `0555`, `05CB` | pause menu: set-up, its text, loop, blinking, choice ([`menus.md`](menus.md)) |
| `0813` / `07A2` | the current Viking off screen: its input dropped / a newly chosen Viking blinks ([`game-loop.md`](game-loop.md#one-tick)) |
| `064B`, `0704`, `0753` | the camera follows the current Viking, a third per phase ([`camera.md`](camera.md)) |
| `086F` | **dialogue player**: runs the queued text commands (table `ds:2BA6`) after the tick's flip ([`game-loop.md`](game-loop.md#the-dialogue-player-0000086f)) |
| `08B8` | start the attract sequence (slot 39) |
| `08C8` | hot keys Alt+S / Alt+M |
| `0982` | **load entry `ax` to `es:di`** (seek `ax × 4`, read the offset, LZSS-decompress) |
| `0CD8` | **load stored full-screen image `ax`** into video memory at `di` (all four planes) |
| `0DBA` | fatal error: print message `ax`, exit |
| `0E35` | quit to DOS: sound off, memory freed, `DATA.DAT` closed, keyboard and text mode restored, `int 21h` `4C00h` |
| `0D96`, `0D9F`, `0E85` | memory: allocate paragraphs, advance `es` past the data just loaded (?) |
| `1080` | start or restart the level: reset state, load slot `ds:25C9` (`1133`) ([`game-loop.md`](game-loop.md#the-level-start-00001080-1133)) |
| `0FA0` / `0F5D` | fade out / in through the tints |
| `0E99` / `0F03` | the tinted palette into `ds:8202`: colours 0 and 16–255 / all 256 ([`drawing.md`](drawing.md#palette)) |
| `0FE6` | retrace job 4: upload the palette at `ds:8202` |
| `11A1` / `1192` / `11DF` | clear `ds:044D`–`204C` / the global flag bits / the tints |
| `13B0` / `13D8` | the camera's limits / start position ([`camera.md`](camera.md)) |
| `15D2` | the level start's tick (no input, no `2E16`, no `2D2C`) |
| `16E3` | the demo for the slot: seed, playback mode, the attract tables, the recording into `ds:2193` |
| `1784` | current Viking and the blinking one = slot 0 |
| `1BA5`, `1C1F` | Tab: the inventory screen, its loop (one pass per refresh) ([`menus.md`](menus.md)) |
| `1C52` / `1CBB` / `1D6F` | inventory: the blinking / the keys while carrying / while choosing |
| `1F47` | inventory: which Vikings are within reach (`ds:0449`) |
| `1F93` / `21B9` / `21F6` / `2250` | inventory: drop / pick up / the cursor back to the giver / the next square that takes an item |
| `183D` / `18AD` | HUD: draw item `ax` in square `di` / the picker's corners there |
| `205B` / `20D1` / `2199` | HUD: the chosen squares changed / every picker / the items changed |
| `17D0` | HUD: a portrait (`ds:854A` frames, `855C` places) |
| `1133` | **load a slot**: object database and header (`11B1`), then everything the header lists |
| `1204`, `1254`, `127B`, `12A2` | HUD-area image by header flag (384, 531, 529) |
| `12AE`, `167A`, `16AE` | load the header's palettes, sprite sets, packed sets |
| `16E3`/`1733` | pick the demo recording for the slot |
| `17AD` | the HUD at a level start, if flag `0x01`: the panel (entry 1), then `200A`, `201D`, `2034` (→ `1B0B`) force everything to be drawn |
| `2312` | random number (LCG `ds:8639`; demos: `ds:0352`) ([`script-vm.md`](script-vm.md#random-numbers-00002312)) |
| `2352` | **read input** into `ds:03B6`/`03B8`/`03BA` |
| `241E`–`2634` | text-box helpers: read a text (`250B`), its size (`2529`), anchor (`2549`), frame (`2388`), text (`24C5`) |
| `24A9` / `265B` | text `ax` in a box without a tail / as it is, at column `si`, row `di` (the pause menu) |
| `2816` | clear the text cells |
| `450B` / `4590` | set the first tint to `ax`, `si`, `di` (× 2) / clear it (opcode `3D`'s and `3E`'s tails) |
| `77BB` | play sound effect `ax` (the menus) |
| `2709`…`27E4` | dialogue queue commands 0, 2, 4, 6, 8, `A` |
| `227E`, `22C0`, `22F3` | the three sub-frame steps (⌈a/3⌉, ⌊(a+1)/3⌋, ⌊a/3⌋) |
| `2870` | a password sets the next slot |
| `2948` | start-up: shrink the memory block, `ds`, the `int 24h` handler, the random seed from the clock (`int 21h` `2Ch` at `297B`: `ds:8639` = DX, `863B` = CX) |
| `2989` | open `data.dat`, read the configuration, copy-protection check, VGA and 386 checks, joystick |
| `2B0E` | load the fixed entries (driver, bank, effects, 454, HUD sprites) |
| `2C12`–`2CA2` | default password `STRT` and key bindings |
| `2CA3` | reset the game state |
| `2CE4` | the Vikings' items and health as at the start of a game (every level start but slot 37's) |
| `2D2C` | the screen shakes count down |
| `2D72` | demo recording and playback (each tick) |
| `2E16`/`2E27` | the current Viking dead (field `1A` bit 15): switch to the next living one; none left → game over |
| `2E79` | Ctrl/Home, Ins/PgUp: switch to the previous / next Viking |
| `2FC6`, `2FCB`, `2FD0` → `2FE5` | apply sub-frame step 0, 1, 2 to the moving objects' sprites |
| `1446` | the Vikings at level start, by header byte 7 ([`objects.md`](objects.md#level-start)) |
| `2F82` | a record's sprite entry → its place in the sprite buffer (table `ds:12AD`/`12ED`) |
| `3031`, `303A`/`3084`, `30A2`–`35CE`, `35CF` | animation: opcode `2F`, one step, the 27 opcodes (table `ds:86E6`), the speed step ([`animation.md`](animation.md)) |
| `2FB3` | clear the sprite pieces |
| `386B` / `3916` | **move the objects** / the attached ones ([`terrain.md`](terrain.md#movement)) |
| `3809` | **spawn an object** (type `ax`, list index `di`, flags `si`): `3D30` may it, `3D52` free slot, `3E52` from its record, `3D68`/`3DD6`/`3E15` sprite pieces ([`objects.md`](objects.md)) |
| `39EF`, `3A0E`–`3A74`, `3A94`, `3AE0` | the view, its strips; spawn the listed objects in it |
| `3BA5` | spawn the always-active listed objects (flag `0x0800`) |
| `3C0C` | restart flag for objects out of view (every tick) |
| `3C93` | **remove an object** |
| `4207` | **run every object's script** (once per tick, from `0039`) |
| `424C` / `42A6` | run one object / the bytecode fetch loop ([`script-vm.md`](script-vm.md)) |
| `42B7`–`63AC` | opcode handlers; `5470`/`5473` get operand by kind, `54BC`/`54BF` set operand |
| `4199`, `41A7`/`41B3`/`41BA`, `41E0` | the tile map: type / word at a pixel or cell, write a cell ([`terrain.md`](terrain.md)) |
| `5530` / `5546` / `5569` | script passes with `ds:0390` = −1 / +1; second runner (opcode `01` ends it) ([`hits.md`](hits.md)) |
| `55D6` / `56C0` | hit tests against other objects by class / by mask |
| `5788`, `57EB`, `584E` → `58F5`–`6150` | terrain hit tests (`32`, `33`, `3C`) and their tile helpers |
| `58AA`–`58E6`, `589B`; `59C6`–`5AE9`; `5DE5`–`60CF` | tile tests: entries; tiles in a column / row / point; objects as terrain |
| `5E7C`–`5F2C` | the object-search opcodes (`2C`, `2D`, `35`, `36`, `D0`, `D1`) |
| `625D` / `6390` / `63AC` | ground snap / slope depth / can it walk on |
| `6235` / `6243` | store / fetch the object a hit opcode found (`ds:1B25`) |
| `6440` / `6528` | keyboard interrupt handler / install it |
| `673C` | when the camera / 16 changed (`ds:257F`/`2581` halved vs `92F3`/`92F5`): spawn the strip that came into view |
| `6775` | **page flip**: CRTC start address (with shake), pel panning, `ds:A39C = 1` |
| `7496`, `746C`, `74E9`, `74BF` | scroll the camera left, right, up, down (locks `0394`/`0396`, limits `25A4`/`25A6`) |
| `37F1` | empty all 20 object slots (code segment 0, field `42` = `0xFFFF`) |
| `7561` | start the sound (AIL, driver, MT-32 upload) |
| `7749` / `774F` | music action for header byte `0x04` / `0x06` (table `ds:A37A`) |
| `775D` | load the level's tune; `7791` play it; `78F1` fade it out |
| `77B2` | play a sound effect: script opcode `02` |
| `797B` | **retrace handler** (called from the AIL timer interrupt) |
| `79A8`, `79FB` | joystick detection and calibration |
| `0BAD:040C` | retrace lock (every 2nd timer interrupt) |
| `0BAD:044B` | AIL timer interrupt handler |
| `0BAD:056B` / `05B7` | install / remove the timer interrupt |
| `0BAD:05E7` / `0610` / `09D6` | set PIT divisor / period in µs / frequency in Hz |
| `0BAD:07C3`, `08F5` | register a timer, start it |
| `0BAD:0A67`, `0B1F`, `0B45`, `0B4B` | register driver, describe it, detect the device, initialise it |
| `0BAD:0C93`…`0CF3` | stubs into the loaded driver (`ax` = driver function number) |
| `0C83:15D5` / `156C` | restore the background under the pieces / draw the pieces by mode (table `0C83:15CB`: `0648` 8×8, `1078` 32 wide, `0B82` 16×16) ([`drawing.md`](drawing.md)) |
| `0C83:1897` | draw the dialogue's text cells (font at `ds:687E`) |
| `0C83:00C1` | drawing, called with `ax` = −2; not read (the pictures match without it) |
| `1AA4`, `1B0B`, `205B`, `20FF`, `2199` | the HUD: health, portraits ([`drawing.md`](drawing.md#hud-00001792-levels-with-header-flag-0x01-not-slot-44)) |
| `2388` / `241E` / `24C5` / `2549` | text cells: a box frame / one character / a text / the anchor's tail |
| `07A6:0000` | dialogue text offsets (390) |

Variables in the data segment (`ds` = `184E`):

| Address | What |
|---|---|
| `0032`, `006C`/`006E`, `0374`, `03E0` | spawn (`14`): flags part, x / y, field 16 value, `0xffff`. `0032` and `006C`/`006E` are scratch for many routines: the text opcodes' mode word (`250B`), the side test's tops (`5CF9`), the changed cells' positions (`3FC2`, `406D`), the dialogue commands' first cell (`2388`, `24C5`) |
| `0034`–`003A` | spawn arguments (type, list index, flags, pieces) / the view's left, right, top, bottom |
| `0042` | the object slot whose script is running (× 2) |
| `0044` / `0046` | camera x / y ([`camera.md`](camera.md)) |
| `0078`/`007A`/`007C`/`0080` | animation step: delay, return address, first / end piece |
| `008A` | the scripts' accumulator `A` |
| `0202` | level keys (F5/F6) enabled; 0 in the EXE, set by the title's script for the cheat's password |
| `0302` / `0304` | music / sound-effects flags (`0x8000` = no sound, bit 0 = off) |
| `0308`, `0310`–`0316` | password: not found / the four letters |
| `0334` | state-change requests (see `0138`) |
| `033C` | first slot searched for listed objects (2 or 6) |
| `0340` | 2, set at every level start (`17AD`); no reader found |
| `0342`–`0347` | palette tints (opcodes `3D`, `4C`) |
| `0352` | demo random seed |
| `0356` | global flag bits (opcodes `0D`/`0E`) |
| `0372` | 2 × number of object slots in use |
| `0376` / `0378` | objects spawned into a lower slot this pass: count / slot offsets (bytes) |
| `038C` | animation piece mask |
| `038E` / `0390` | hit-opcode counter / script pass (0, −1, +1) |
| `034E` / `0350` | how far the camera last scrolled across / down (5 after a Viking switch) |
| `0394` / `0396` | camera locks across / down |
| `0398` | 1 while a demo plays (read by the drawing) |
| `039A`–`03A4` | screen shake: amount `039A`/`039C`, offset `039E`/`03A0`, ticks left `03A2`/`03A4` (opcodes `2E`, `3B`; `2D2C`) |
| `03A6` / `03A8` | bytes from `853E` / `8544` by header word `0x18`, at every level start (`1397`); no reader found |
| `03AA`–`03B0` | object search: list, handler, row, the last object found |
| `03B2` / `03B4` | what a tile test found: the type or kind / the object (`0xFFFF`: a tile) |
| `03B6` / `03B8` / `03BA` | input: held / newly pressed / previous ([`input.md`](input.md)) |
| `03C2` | current Viking (`0xFFFF` after the last one died) |
| `03C4` / `03C6` | the Viking last made to blink / blink ticks left |
| `03CA` | nearest Viking (opcode `34`) |
| `03CC` / `03D4` | demo mode (`0x8000` playback, `0x8001` recording, `0x8002` stopped, 1 = attract demo asked for) / attract demo index × 2 |
| `03CE` / `03D0` | demo: ticks left of the current pair / its word |
| `03D8` / `03DA` / `03DC` / `03DE` | how far the Viking is left / right / below / above the camera's box (phase A, for the three steps) |
| `03E0` / `03E2` | spawn: half width / height (`03E0` < 0: from the record) |
| `0423`–`0427` | the Vikings' field `0A` as the HUD (`1B0B`) last recorded it: the level start (`154A`) and the game-over scene read it |
| `042F`–`0433` | the shield words `0429`–`042D` as `1B0B` last recorded them |
| `0435`–`0439` / `043B`–`043F` | the HUD's portraits per Viking as last drawn (0 the current one, 1 another, 2 dead; `0xFFFF` at the level start) / the ones before (`20FF`, `200A`) |
| `03E4` | the items: 12 squares (Viking *v*'s *k*-th at `+8v+2k`) ([`menus.md`](menus.md)) |
| `03FC` / `041A` | the HUD's last drawn items / chosen squares |
| `0414` | each Viking's chosen square (0–3) |
| `0421` | inventory: the Viking the carried item came from |
| `0441` / `0443` / `0445` / `0447` | inventory: the item under the cursor or carried / the cursor's square (pause menu: the choice) / the blinking counter / 1 choosing, 0 carrying |
| `0449`–`044B` | inventory: per Viking, 0 if within reach |
| `124D` / `126D` / `128D` | packed sprite sets: entries / far pointers |
| `12AD` / `12ED` | sprite table: the level's sprite entries / their offset in the sprite buffer + 1 |
| `132D`–`1AD5` | **object fields**: arrays of 20 words, `0x28` apart; script fields start at `14E5` ([`script-vm.md`](script-vm.md#object-fields)) |
| `13F5` | per object: latched hit bits |
| `1B25` | hit table: per object 16 words, the object each hit opcode found |
| `1DA7` / `218F` / `2B64` | dialogue queue / its write / read position |
| `257B` / `257D` | the camera as last shown: set at the level start and by every flip (`6775`) |
| `257F` / `2581` | camera x / y divided by 8 |
| `25A4` / `25A6` | the camera's limits |
| `25A8`–`25AA` | colour 3 of the header's palette, kept at the level start (`132B`) before dialogue command 6 changes it |
| `25AB` / `25AD` | previous / current slot |
| `25AF` | tune loaded (music actions 0 and 3, `775D`, with sound on) |
| `25B3` | **copy of the current header**: `25B7` = byte 4, `25B8` = byte 5, `25B9` = byte 6, `25C9` = next slot (`0x16`), `25CF` = flags (`0x1C`), `25E1`/`25E3`/`25E5` = map/tileset/metatiles entries, `25F6` = the lists (`0x43`) |
| `2191` / `2193` | demo: position of the next pair / the recording |
| `2B66` / `2B74` | attract demos: level slots / recording entries |
| `2B80` | word i = i / 3, the camera's three steps |
| `2E5D`–`2E7B` | segments of the loaded data: `2E67` object DB, `2E6B` music, `2E6D` effects, `2E6F` instrument bank, `2E71` = entry of the loaded object DB, `2E73` sprite buffer; `2E5D` packed frame buffer |
| `507D` | the item icons (entry 14, 256 bytes each) |
| `7EFE` | palette job for the retrace handler (0, 2, 4) |
| `7F02`, `8202` | palette buffers (256 × 3) |
| `8508`, `851A`, `852C` | Viking start places for header start modes 2, 4, 5 ([`objects.md`](objects.md#level-start)) |
| `8562` | the inventory's squares: video offsets (86 bytes a row) |
| `8592` | by item: non-zero if the trash can takes it |
| `853E` / `8544` | 6 bytes each, by header word `0x18`, into `03A6` / `03A8` |
| `85A5` | passwords, 4 bytes per level, bit 7 set |
| `8639` | random seed (32 bit; set from the clock at start-up, `2948`) |
| `86A3` | `"data.dat"` |
| `86B0` | the configuration (entry 0): `86B6` driver, `86B8` variant, `86C2` fingerprint, `86C4` magic |
| `86DA` / `86DC` / `86DE` | joystick present / joystick word / keyboard word |
| `86E6` / `871C` | animation opcode table / packed frame offsets by piece (`17`) |
| `8734` | the cells changed this tick near the view: count × 3, then (cell offset, x, y) per cell (`4030`, drawn by `406D`, emptied at `00DB`) |
| `87CC` | **script opcode table** (216 words), shared tails from `87AE` |
| `897C` | slope heights (16 × 16 bytes) |
| `8F68` | tile map row offsets (256) |
| `916C` | key-state array (one byte per scancode); `9181` Y, `919D` N, `917C` Q, `9199` X, `91A4` Alt, `91B0` F10 |
| `91EC` | scancode → input bit (words) |
| `92EE` | pel panning value for the next retrace |
| `92F3` / `92F5` | camera / 16 when `673C` last spawned a strip |
| `9346` | field number → offset in the field arrays |
| `93BC` / `93C4` | byte masks / inverted byte masks (global flag bits) |
| `93CC` / `93EC` | bit masks / inverted bit masks |
| `940C` / `946C` | slot tables: header entries / object databases (48 each) |
| `94CC` | tile-type lists (35, each ended by `0xFF`; = the databases' lists at `0x1347`) |
| `956C` | text buffer, 40 × 22 cells |
| `A37A` | music action jump table |
| `A384` | tune table (`kTunes`) |
| `A39C` | retrace counter |

`SETUP.EXE` (data segment `06EA`): `064C` menu string pointers; `0B82` driver → menu choice; `0B94` / `0BA8` menu
choice → driver / variant; `116A` the configuration it writes; `1164` the current choice.

## Not looked at yet
- Segment `0C83` (drawing) beyond what [`drawing.md`](drawing.md) lists.
- Everything else not read in detail, and what has not been compared with the original, is in
  [`../formats/unknowns.md`](../formats/unknowns.md#4-vikingsexe-routines-and-variables-not-read-in-detail).
