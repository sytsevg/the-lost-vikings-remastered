# The pause menu, the quit box and the inventory screen

**Status:** observed (static disassembly of `VIKINGS.EXE`). The engine's: `src/lvr/play.*` (the pause menu, the quit box) and `inventory.*`. Checked against the original step for step through the reference engine (since retired), and by the DOSBox checks below.

Both are loops of their own inside phase A of a tick ([`game-loop.md`](game-loop.md#one-tick)): the pause menu
(`041C`) before the state requests are acted on (`0138`), the inventory (`1BA5`) right after. While one is open
nothing else of the tick runs; when it closes, the tick goes on from where it stopped, with the input as the loop
last read it. Both share `ds:0443` (the choice or the cursor) and `ds:0445` (the blinking counter).

## The pause menu (`0000:041C`–`0648`)
**Opens** when Esc/P (`0x1000`) is newly pressed, in a level (header byte 7 non-zero), with no level change requested
(`0334` bits 1 and 2 clear) and no dialogue queued (`218F` = 0). During a demo the pause bit ends the demo first
(`02AD`), so the menu doesn't open.

**Set-up:**
1. Colour 3 black, on the screen and in the base palette buffer (`ds:7F0B`).
2. If no tint is on (`0342`–`0344` all 0): the tint 8, 8, 8 (`450B` with 4, 4, 4: the first tint, doubled), which
   darkens the picture ([`drawing.md`](drawing.md#palette)). The menu then takes it off again when it closes
   (`4590`). With a tint on already it is left alone.
3. `047C`: sound effect 0; the text cells get the framed box of dialogue text 2 (the question and two blank lines) at
   column 15, row 12 without a tail (`24A9`: frame `2388` with anchor 6), and text 5 ("YES") at column 16, row 15
   (`265B`: a text as it is, like dialogue command `A`). `0445` = `0x11`, `0443` = 1 (NO chosen).
4. Colour 3 white (`3F 3F 3F`) on the screen only; the text drawn and the picture flipped, **without a wait**.
   When the menu set the tint, the tint's palette upload at the next retrace (job 4: the tinted copy of the base
   palette, `ds:8202`) brings colour 3 back to black, so the box is white only when a tint was on already. Colours
   1–15 aren't tinted (`0E99`).

**A pass (`04C3`):** three refreshes (the pieces' background restored, `0C83:15D5`, after the first), the input read
(`2352`), then:
- `0555`, the blinking: `0445` − 1; when its low 4 bits are 0, bit 4 set: three spaces (text 6) over the chosen word
  (column `0x10` for YES, `0x15` for NO), else the word again (YES at `0x10`, NO, text 4, at `0x16`); then the pieces
  drawn (`0C83:156C`), the text, a flip. So the chosen word is off for 16 passes and on for 16.
- `05CB`, the choice: Left newly pressed with NO chosen: YES (`0443` − 1), `0445` = `0x11`, NO drawn again; Right
  with YES chosen: NO, YES drawn again. Then Fire (`0x8000`) takes the chosen one, Esc/P is NO, and so are the keys
  **Y** (scancode `0x15`) and **N** (`0x31`) when held, read from the key-state array (`ds:9181`, `919D`).

**Once chosen:** the input read again; YES is state request 2 (game over: slot 37, which the tick's `0138` loads at
once); two full draws with flips (`9569` = 1 and `98DC` = 0 meanwhile, presentation flags); the text cells cleared
(`2816`); the menu's tint off.

**The text's leftovers:** the box (`24A9`) and the words (`265B`) look their text up by number (`2515`), leaving its
offset in `ds:002A`; the words leave their column in `006C` (`24C5`); the box leaves what dialogue command 0 does
(the size in `0034`/`0036`, the first cell in `006C`/`006E`) except the anchor (`0038`/`003A`), which it doesn't look
up.

## The quit box (`0000:0350`–`041B`)
After the tick (after Alt+S/M, before the dialogue player, [`game-loop.md`](game-loop.md#one-tick)), with no
dialogue queued (`218F` = 0), the keys **F10**, or **Alt with X or Q**, held (the key-state array: `ds:91B0`,
`91A4` with `9199` or `917C`; scancodes `0x44`, `0x38`, `0x2D`, `0x10`):
- during demo playback (`03CC` = `0x8000`) or in a slot with header flag `0x08` (the logos, the galaxy, the title,
  the game-over scene): the game **quits at once** (`0E35`: the sound stopped, memory freed, `DATA.DAT` closed, the
  keyboard handler and text mode restored, `int 21h` `4C00h`);
- otherwise the **quit box**, set up as the pause menu (colour 3, the tint 8, 8, 8 unless one is on, sound 0) with
  dialogue text 3 (the question and two blank lines) at column 13, row 12, YES at column 16 of row 15 and the
  slot's password (`ds:0310`–`0316`, the low bytes, as script opcode `D2` left them) straight into the text cells
  at columns 18–21 of row 17 (`241E`). Its passes are the pause menu's (`04A1`–`0554`, the same keys, NO chosen
  first). Once chosen: if **state request 2** is set (YES sets it; so does a game over already pending, so NO
  quits then too) the game quits, from inside the box, before the tint comes off; otherwise the tint off
  (`4590`) and the tick ends with the dialogue player.

The script opcode `13 01` (the main menu's third item, which quits, in database 454) jumps to `0E35` too: the game ends in
the middle of that object's run. The remake stops the same way (`FaultKind::kQuit` unwinds the step; `Game::quit()`),
keeping the state as it was then.

## The inventory (`0000:1BA5`–`227D`)
**Opens** when Tab/CapsLock (`0x2000`) is newly pressed in a level with the HUD (header flag `0x01`).

The items: `ds:03E4` holds 12 words, a **square** each: Viking *v*'s squares *k* = 0–3 at `03E4 + 8v + 2k`, two by two
(bit 0: right, bit 1: lower). Square 12 is the **trash can**. `0414`/`0416`/`0418` is each Viking's chosen square
(0–3; the scripts read it and the items, [`script-vm.md`](script-vm.md)). The screen's own variables: `0441` the item
under the cursor or carried, `0443` the cursor's square (0–12), `0445` the blinking counter, `0447` 1 while choosing
and 0 while carrying, `0421` the Viking the carried item came from, `0449`–`044B` a byte per Viking: 0 if within reach.

**Set-up (`1BB7`):** sound 0; `0445` = `0x11`, `0447` = 1; the current Viking's first sprite piece shown (flag
`0x2000` cleared, redraw word 2: it may have been blinking), the playfield drawn and flipped; the cursor on the current
Viking's chosen square and `0441` its item; **within reach** (`1F47`): the Vikings with health (field `0A` ≠ 0) whose
|dx| + |dy| from the current one is below 64 (signed), the current one included.

**A pass (`1C1F`):** the input read, the keys, the blinking, the HUD (`1792`), a flip and **one** refresh: the screen
runs at the display's 59.71 Hz, three passes per tick's time. During a demo (`03CC` bit 15) the recording goes on
(`2D72`) and its pause bit closes the screen too. When it closes, `2199` redraws the items that changed.

**Choosing (`0447` = 1, `1D6F`)**, with the keys newly pressed, each tested in turn:
- Ctrl/Home (`0x20`) / Ins/PgUp (`0x10`): the previous / next Viking with health becomes the **current Viking**
  (`03C2`), as in the game; the cursor goes to its chosen square, `0445` = `0x11`, reach found again, sound 1.
- Left / Right / Up / Down: bit 0 / bit 1 of the chosen square cleared / set, if it isn't already; the cursor with
  it, `0441` its item, `0445` = `0x11`, sound 1.
- Fire (`21B9`): the chosen square's item, if any, is **picked up**: `0421` = the current Viking, `0441` = the item,
  the square emptied, the cursor there, `0447` = 0 (carrying), `0445` = 9, sound 2.
- Esc/P or Tab (`0x3000`): `0445` = `0x11`, the screen closes. This is tested after Fire, so Fire and Esc in the same
  pass close it while carrying: the item is gone (the next opening takes `0441` from the cursor's square again).

**Carrying (`0447` = 0, `1CBB`)**, only the first of:
- Left / Right (`2250`): `0445` = `0x11`, then from the cursor's owner (square / 4: 0–2 a Viking, 3 the can) to the
  previous / next one, round, until one takes it: the can always; a Viking only if within reach and with a free
  square, the first free one. The cursor goes there, sound 1.
- Fire: drop (below), and the screen stays.
- Tab: drop; the screen closes, unless the can refused the item.

**Drop (`1F93`):** at the can, items whose byte in the table `ds:8592` is 0 are refused (sound 3, still carrying);
the others vanish (sound 4). At a Viking's square: sound 2, the item into it, and if that Viking's chosen square is
empty (after the drop), the new square is chosen. Then (`21F6`) the cursor goes to the giver's chosen square, and if
the giver has any item left, to its first one, which becomes its chosen square; `0441` = that item or 0. `0447` = 1.

The table at `ds:8592` (by item): 0 for items 0, 6, 7, 8 and 12–18; the bytes after item 18 are the passwords
(`85A5`), which the code would read for a higher item number all the same.

**The blinking (`1C52`):** `0445` − 1; when its low 4 bits are 0: choosing, bit 4 set draws the picker over the
cursor's square (`18AD`), clear draws its item without it (`183D`); carrying, bit 4 set draws the carried item at the
cursor, clear the empty square, and both draw every Viking's picker again (`20D1`). So after a move the picker or
the item shows at once and blinks every 16 passes; after a pick-up (`0445` = 9) the square stays empty for 24 passes
before the carried item first shows.

## The HUD's items and picker
`183D` draws item *n* (icon *n* of entry 14, 16 × 16, loaded to `ds:507D`; 0 the empty square, 19 the picker, 23 the
trash can, which `183D` draws for square 12 when the item is 0) at the square's place in the table `ds:8562`
(video offsets, 86 bytes a row). In screen pixels: squares 0–3 at x 79 and 95, y 18 and 34 below the playfield, then
+72 (151, 167) and +144 (223, 239) for the other Vikings, the can at (255, 18) (the panel's own can and empty squares
are exactly icons 23 and 0 there). `18AD`, the **picker**, writes only the corners of icon 19: 5 × 2 and 2 × 3
pixels at each. The HUD (`205B`, `20D1`) keeps a picker over every Viking's chosen square, alive or not.

**Which items are drawn (`2199`, every HUD update):** each square whose item (`03E4`) differs from what was drawn
(`03FC`) is drawn and recorded; after each, `20D1` draws every picker again and leaves its register at Olaf's square
(2 × (his chosen square + 8)), where the loop goes on. So after a change the squares between it and Olaf's next
square wait for the next update (a tick later, or the inventory's next pass), and the loop can go back over some.
The engine keeps what was drawn (`World::shown_items`) and draws the items from there.

**In the engine** the HUD is drawn whole every picture from the state, with what the inventory's blinking last left
in the cursor's square.
Rare leftovers of the original's incremental drawing aren't reproduced (a Tab that drops and closes on the pass the
blinking turns the picker off leaves that Viking's picker off until its chosen square changes).

## Checked in DOSBox
`tools/spotcheck.py`, the original in DOSBox-X:
1. `pause`: **confirmed** with no tint on: the box black inside with white text, every playfield pixel darker by 8
   of 63 (or still 0), the HUD unchanged (`build/dosbox/spotcheck/pause.png`). The box with a tint already on (white
   inside) wasn't tried.
2. `timing`: **confirmed**, the inventory runs one pass per frame (the pause menu 3, as a tick).
3. `fire-esc`: **confirmed**: an item in the chosen square, Fire and Esc in the same pass while choosing: the square
   is emptied, the item carried (`0441`, `0447` = 0), the screen closes; opened again, `0441` = 0 and no square holds
   the item: it is gone.
4. `tools/lockstep.py`, every pass a step (the stop at `04C3`), the exit a step of its own (the stop
   at `0E35`): **the same state as the original's**, step for step, for the main menu's third item (Up, Enter: both quit
   at step 186), F10 during an attract demo (quits at once), F10 in level 1 with NO, Alt+Q with YES (both quit, the
   tint still on), Alt+X with N held, the pause menu with Left/Right and Fire on NO, Esc then Y held (give up: the
   game-over scene), and F10 there (flag `0x08`: quits at once).
5. `tools/lockstep.py`, the inventory's passes a step each (the stop at `1C1F`): items poked into Erik's squares,
   picked up, given to Baleog, back, into the trash can, Tab to close: **the same state as the original's**, once
   the HUD's item loop skipped squares as the original's does (above).
