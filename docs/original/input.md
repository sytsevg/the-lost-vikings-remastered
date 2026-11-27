# Input

**Status:** observed (static disassembly of `VIKINGS.EXE`, see [`timing.md`](timing.md) for the address
conventions). What each bit *does* for each Viking: [`vikings.md`](vikings.md#keys).

## The input word
All game input is one 16-bit word per tick. It is read **once per tick**, at the start of phase A (`0000:2352`):

| Variable | Meaning |
|---|---|
| `ds:03B6` | held this tick = joystick word `ds:86DC` (only if a joystick was found, `ds:86DA`) OR keyboard word `ds:86DE` |
| `ds:03B8` | newly pressed this tick = held AND NOT previous |
| `ds:03BA` | previous tick's held word |

Default bindings (set by code at `0000:2C2A`; vanilla has no key remapping):

| Bit | Keys (scancode) | Meaning (tentative unless obvious) |
|---|---|---|
| `0x0010` | Ins `52`, PgUp `49` | next Viking |
| `0x0020` | Ctrl `1D`, Home `47` | previous Viking |
| `0x0040` | E `12` | |
| `0x0080` | S `1F` | |
| `0x0100` | Right `4D` | right |
| `0x0200` | Left `4B` | left |
| `0x0400` | Down `50`, keypad 5 `4C` | down |
| `0x0800` | Up `48` | up |
| `0x1000` | Esc `01`, P `19` | pause menu; also ends a demo |
| `0x2000` | Tab `0F`, CapsLock `3A` | the inventory ([`menus.md`](menus.md)) |
| `0x4000` | D `20` | |
| `0x8000` | F `21`, Space `39`, Enter `1C`, keypad + `4E` | |
| `0x0001`–`0x0008` | none | unused by the keyboard |

The arrow keys are the keypad scancodes, so the grey arrows and the keypad arrows act the same.

## Keyboard handler (`0000:6440`, IRQ 1)
- Keeps a **key-state array** at `ds:916C` (one byte per scancode, 1 = held).
- On a key press it ORs the key's bit into `ds:86DE`. On a key release it **clears** the key's bit, even if another
  key with the same bit is still held (hold Space and F, release F: bit `0x8000` is gone until a key is pressed
  again). The remake's vanilla mode should keep that behaviour.
- Alt+S is not passed to the game as input (S's bit is skipped while Alt is held).
- **The typed character** (`0000:647F`): every key press also writes the key's character to the script global
  `ds:028C`, from a table of one word per scancode (`ds:8E68`): the digits, the consonants (no vowels, as the
  passwords have none), `0x80` for Backspace and `0x81` for Enter, Space, `[` and `]`; 0 for every other key, so
  any other press clears it. Not written during demo playback, nor for Alt+S. The scripts read and clear it:
  the title menu's hot keys `N`, `P`, `Q` (database 454 at `85D2`), the password screen (typed letters, Backspace,
  Enter; `86D1`), and a yes/no question answered with `Y` / `N` in every level database (449 at `33C1`/`344D`).
  The lockstep runs never saw it because they write the keyboard word and the key states directly, past the
  handler. Checked in lockstep with the word poked on both sides: `P` at the title menu, then `T` `L` `P` `T` and
  Enter load level 3 in the original and in the reference engine of the time, every state and the picture the same. The game:
  `Session::key_pressed` with `input::typed_character` of the key, at every press.
- **Ctrl+Alt+Del** jumps to the BIOS reset vector (`FFFF:0000`): the game reboots the machine itself.

Hot keys, read from the key-state array once per tick:

| Keys | Effect | Where |
|---|---|---|
| Y / N (held) | in the pause menu and the quit box: yes / no | `0000:0631` ([`menus.md`](menus.md)) |
| Alt+S | toggle sound effects (`ds:0304` bit 0); stops the playing effects | `0000:08C8` |
| Alt+M | toggle music (`ds:0302` bit 0); restarts or stops the tune | `0000:08C8` |
| F4 | executes `int 3` (a debugger breakpoint; does nothing without a debugger) | `0000:00EA` |
| F5 / F6 | previous level / next level (the first stays the first), in a level, **only if `ds:0202` ≠ 0**. It is 0 in the EXE; the title sets it when its password screen is given a fifth password that is no level's. It is cleared only the first time the title runs, so it stays on back at the title (`replays/level-keys-kept.replay`). Checked in DOSBox (`replays/level-keys.replay`) | `0000:00F7` |
| F + D + E + S, alone or with Left | with `ds:0202` ≠ 0 too, and read from the keyboard word (so from a joystick as well), in the script of Olaf's shield (type 03 of every database): the next level (not from slot 36), with Left the one before (not from slot 0). Each Viking whose field `0A` is 0 gets 6 first. With the switch on, the shield stays when Olaf is gone, put away, to go on watching. Checked in DOSBox with all three alive (`replays/level-keys-four.replay`); the branch for a Viking who is gone is from the listing only | 449 at `5AD6`, `5C38` |
| Alt+X, Alt+Q, F10 (held) | quit: at once in demos and slots with header flag `0x08`, else the quit box (`ds:91A4`, `9199`, `917C`, `91B0`) | `0000:0350` ([`menus.md`](menus.md#the-quit-box-00000350041b)) |

## Joystick
At start-up `0000:79A8`/`0000:79FB` read the game port (`0x201`), timing the axes against the PIT. If a joystick
answers, its centre ±`0x19` becomes the dead-zone thresholds (`ds:86D2`/`86D4` = centre x − / + `0x19`, `86D6`/`86D8`
the same for y) and `ds:86DA` = 1.

**The joystick's word** (`0000:2EF8`, each tick before the input is read; `ds:86DC`, or-ed with the keyboard's):
buttons 1–4 (port `0x201` bits 4–7, low when pressed) set `0x8000`, `0x4000`, `0x40`, `0x80` (F, D, E, S); an x
reading at or below `86D2` sets `0x100`, at or above `86D4` `0x200`; y at or below `86D6` `0x400`, at or above
`86D8` `0x800`. During demo playback any joystick input becomes `0xFFFF` (`2F6A`), as any key does. Open: whether
the reading (`79FB`, a PIT count) grows to the right and down; if it does, the axes would be the arrows' mirror
image, so it probably counts the other way. Not checked in DOSBox (the control channel has no joystick).

**In the game** (`src/input/input.*`, the front end; the keyboard word kept as the original's handler keeps it by
`InputHandler`, `src/lvr/handler.*`): under Vanilla rules the default keys above and only those; under Remaster rules the player's, changed in the menu and kept in the profile
(actions `right`, `left`, `up`, `down`, `previous-viking`, `next-viking`, `use-item`, `act`, `ability-d`,
`ability-f`, `pause`, `inventory`, `none`, and Remaster's own). A gamepad works as the
original's joystick: A/B/X/Y (south, east, west, north) are buttons 1–4, the d-pad or the left stick (past half
way) the arrows, and beyond the joystick the shoulders switch Vikings, Start pauses and Back opens the inventory.
The keyboard's and the gamepad's bits are or-ed into the word as the original or-s the joystick's; during a demo
any key or button is `0xFFFF`. Input per player (co-op, ADR-0012) is not part of the vanilla game.

## Demo recordings
Entries 418–422, 443, 444 and 448 ([`game-flow.md`](game-flow.md#demos)) are input recordings:

```
pairs of u16: input word, number of ticks; the last pair is (0x1000, 0xFFFF)
```

- **Recording** (`ds:03CC` = `0x8001`, a developer mode; nothing in the retail game enters it): each tick after the
  input read, `0000:2D72` extends the current run if the held word (`ds:03B6`) is unchanged, otherwise it writes the
  finished `(word, count)` pair to the buffer at `ds:2193`. Pausing (`0x1000`) writes the end marker.
- **Playback** (`ds:03CC` = `0x8000`): each tick after the input read, `0000:2D72` ORs the current pair's word into
  the keyboard word `ds:86DE` and counts down; at zero it takes the next pair. The word is ORed in *after* this tick's
  read, so it reaches the game as the next tick's input (`ds:03B6`). The end marker's `0x1000` looks like the pause
  key, which ends the demo.
- During playback the keyboard handler turns any key except Ctrl, Alt, Del, S, M, X and F10 into `0xFFFF` (all
  bits, so `0x1000` too): any key press ends the demo, while the sound hot keys keep working.
- Playback stops at the **first** end marker. Most recordings have more pairs after it, left over in the buffer
  from an earlier, longer recording; they are never played. Up to the first marker: 418 (level 3) 442 ticks, 419
  652, 420 1,177, 421 930, 422 771, 448 979, 443 (the intro) 2,703; 444 is only the marker. (An earlier count
  that added up everything said 886–2,973.) How playback feeds the game tick by tick:
  [`game-loop.md`](game-loop.md#demos).
