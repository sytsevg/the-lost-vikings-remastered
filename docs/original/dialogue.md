# Dialogue

**Status:** observed (static disassembly of `VIKINGS.EXE`). The text table is read from the real file
(`src/assets/text.*`; `lv-export assets` writes them to `dialogue/texts.txt`). The engine's: `src/lvr/dialogue.*` (the text layer and the boxes) and `conversations.cpp` (who says what). Checked against the original through the reference engine (since retired): every level's opening answered in lockstep, and the boxes' pictures (level 1, the village intro, the ending's scenes) pixel for pixel.

## Where the text is
- All dialogue is in **`VIKINGS.EXE` segment `07A6`** (image offset `0x7A60`, `0x4070` bytes), plain **upper-case
  ASCII (names too are written in capitals).
- `07A6:0000`: a table of word offsets, one per text. The first offset is `0x030C`, so there are **390 texts**
  (0–389). Some entries share an offset (16/17, 34–37), and 4–6 are short placeholders.
- Each text: byte **width**, byte **height** (the box size in 8×8 cells, frame included), then the characters, with
  `0x0D` starting a new line, ended by a `0x00`. Lines are already broken by hand; the game does no word wrap.
- 2 is the pause menu's question and 3 the quit box's; 4–6 hold the menus' words (yes, no, three
  spaces) ([`menus.md`](menus.md)). 0 and 1 are named by no script as a
  constant (6 text opcodes take their number from a global); level 1 opens with texts 81 and 82.
- Text numbers come from the scripts (operand of `41`/`44`/`45`, [`script-vm.md`](script-vm.md)). Example: object
  type `3C` of the space world shows texts 38 and 39, the janitor-alien conversation.

## How a script shows text
Scripts don't draw. The text opcodes append commands to a **dialogue queue** (`ds:1DA7`, write position `ds:218F`):

| Opcode | Queue command | What the command does (handler) |
|---|---|---|
| `41 G2 G2` | 0 | framed box: text, anchor; position relative to this object, in cells, clamped to the screen (`2709`) |
| `44 G2 G2` | 0 | the same at an absolute cell position |
| `45 G1 G2` | `A` | text at a cell position, no frame (`2738`) |
| `42` | 2 | clear: redraw the playfield on both pages and empty the 40×22-cell text buffer `ds:956C` (`2758`) |
| `43`, `CB` | 4 | **wait for a key**: sets state request 4 (`ds:0334`), the wait-for-key loop `0000:0164` (`27DB`) |
| `46 w` | 6 | set VGA colour 3 to `w` (bits 0–4 red, 5–9 green, 10–14 blue, each × 2 into a 6-bit VGA value; level 1's speaker colour `0x12` is a dark red) (`27E4`): the box's inside, set per speaker |
| `50 G2 G1` | 8 | one character (its code, column, row) in the text cells (`26EF` → `0000:241E`): the password box's four letters |

- The box anchor (second operand of `41`/`44`, `0000:2549`) picks the cell of the box that is placed at the given
  position (where the speech-bubble tail is): column 1 for anchors 1 and 4, width − 2 for 2 and 5,
  width / 2 for 0, 3 and 6; row height − 1 for 0, 1, 2 and 6, row 0 for 3, 4 and 5. For `41` the position is the
  object's x/y plus the offsets, minus the camera, divided by 8, and at least 2. Cells: 0–39 across, 0–21 down
  (the 320×176 playfield); the box is kept inside that.
- A typical exchange (type `3C`): `sfx`, `text_at_object` (text 38), `A = target`, wait for key, clear, colour,
  `text_at_object` (text 39), …

## Does dialogue pause the game? Yes
- The queue is played by **`0000:086F`**, called from the main loop at `0000:00E7`, **after** the tick's draw and
  page flip. While commands are queued it loops: run the next command, draw (`0C83:1897`), read input (`2352`),
  act on state requests (`0138`, which runs the wait-for-key loop). Then it resets the queue, flips and waits for
  the retrace.
- So a whole conversation (box, key, clear, box, key…) plays out **between two ticks**: no object script, no
  physics, no tick counting runs meanwhile. Palette work done by the retrace handler (and the music, driven by the
  timer interrupt) keeps going.
- The dialogue is a state of the simulation that holds the tick, advanced by key presses in the input, so
  replays include it (ADR-0003).
- The queue's commands: 0: column, row, anchor, text offset; `A`: column, row, text offset; 6: colour; 8: three
  values. The player runs it after the tick and holds the game at each wait
  ([`game-loop.md`](game-loop.md#the-dialogue-player-0000086f)). In level 1 the level-start object waits until the Vikings'
  scripts have cleared their bits of global `0278`, so the conversation comes in the second tick: colour, box (Erik's
  first line), wait, clear, box (his second, of two lines), wait, clear.
- The same object shows the level's **password** box first (text `0x10D` and the four letters, command 8) on every
  level after the first, and skips both during a demo (`ds:03CC` ≠ 0). After a game over (previous slot 37) it
  counts the retries in global `027A` and picks other conversations by that count.

## Open
- The box frame and font drawing (`0000:2388`, `24C5`, the font entry 2), and the exact anchor rules.
- What the retry conversations say and when each comes (global `027A` = 5, 8, `0B`, `0F`, `32`, `33`).
