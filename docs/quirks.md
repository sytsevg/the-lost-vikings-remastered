# Quirks of the original

> One list of the odd things the 1993 DOS game does: flaws, leftovers, debug switches, things that only work by
> accident. Collected for a write-up later. Each entry is short and points at the doc with the details and the
> evidence; add to this page whenever a new one is found. "Kept" means the engine does the same
> (Classic matches the original); "not kept" says why.
>
> Not here: what we merely don't understand yet ([`formats/unknowns.md`](formats/unknowns.md)).

## Input
- **A key's release clears its action even if another key for the same action is still held.** Hold Space and F
  (both "F" action), release F: the action is gone until a key is pressed again. Kept.
  [`original/input.md`](original/input.md#keyboard-handler-00006440-irq-1)
- **A tap between two input reads is lost.** The handler sets a bit at the press and clears it at the release; the
  game looks 20 times a second. A key pressed and released within one tick never reaches it. Kept (a latch for it is a
  candidate for the QoL rules).
- **The passwords have no vowels, and neither has the keyboard.** Every key press leaves "the typed character" in
  a script variable, from a table that has the digits and the consonants only. So the password screen can't be
  typed a vowel, the title menu has hidden hot keys (`N`, `P`, `Q`), and any other key press wipes the last
  character. Kept. [`original/input.md`](original/input.md#keyboard-handler-00006440-irq-1)
- **Ctrl+Alt+Del is the game's own.** The keyboard handler jumps to the BIOS reset vector itself. Not kept.
- **Debug keys left in:** F4 executes `int 3` (a debugger breakpoint) every tick it is held; F5 / F6 (previous /
  next level) are behind a switch that is 0 in the retail EXE, and that the title turns on when a password is
  typed that is no level's: a cheat left in. The same switch gives a second pair of level keys that a joystick can
  press too (F, D, E and S together; with Left, back), in the script of Olaf's shield of all things. A recording mode for the demos
  (`ds:03CC` = `0x8001`) is there too, with nothing to enter it. [`original/input.md`](original/input.md)
- **During a demo, any key ends it by becoming every key at once** (`0xFFFF`, which includes the pause bit that
  ends a demo), except the sound hot keys. Kept.

## The demos and the game flow
- **Demo recordings carry leftovers.** Playback stops at the first end marker; most recordings have more input
  after it, from an earlier, longer take that was recorded over. [`original/input.md`](original/input.md)
- **The village intro is a demo:** a recording of someone playing the scene, driving the Vikings.
- **The title screen is in the data twice** (headers 381 and 382, identical), and slot 42 with the copy is never
  loaded by any path found. [`original/game-flow.md`](original/game-flow.md#slots)
- **The galaxy's "next slot" in its header is the title.** Where it really goes is decided by its script from the
  slot before it; the same for the game-over scene, whose header says "level 5".
  [`original/game-flow.md`](original/game-flow.md#slots)
- **The last ship scene never ends** and ignores every key (8,000 steps tried): only quitting leaves it.
- **The DOS level headers still carry the SNES backdrops' fields:** each level's backdrop size and scroll speeds
  are there, with the picture's chunk numbers blanked out. The DOS game shows black and reads none of it.
  [`formats/snes.md`](formats/snes.md#the-backdrops)
- **On the SNES the spaceship's stars fly past at 5 pixels a refresh** by a flag in the level header, while every
  other backdrop only follows the camera. [`formats/snes.md`](formats/snes.md#how-the-game-moves-the-backdrop)
- **Entry 534 of `DATA.DAT` is three bytes: space, CR, LF.** Probably the build tool's leftover.
  [`formats/unknowns.md`](formats/unknowns.md)
- **Tune 10 (the game-over scene) is empty in all five device versions:** the scene is silent by data, not by
  code. Tune 1 is a byte-for-byte copy of tune 5. [`formats/audio.md`](formats/audio.md#music)
- **The effects' General MIDI file still has the sound designer's labels** as text events, the same
  two in most sequences. [`formats/audio.md`](formats/audio.md)

## The script interpreter and the animation programs
- **Opcode `A1` reads its operand and then uses the accumulator instead**; "set" with kinds 0 and 4 returns into
  garbage; `45` reads only part of its operands. No script uses them, so they never fire.
  [`original/script-vm.md`](original/script-vm.md#quirks-to-keep-in-vanilla-mode)
- **With no object alive, a per-tick clear runs once at slot −2** and zeroes two fields of slot 19.
- **Spawn queues a byte list by writing words**, each overwriting the high byte of the last.
- **Animation opcode `0C` with a mask tests piece `0x18`**, the opcode's own index into the handler table, instead
  of each piece. `0A` mirrors by the flags of an "object" at an offset that is really a piece offset. `17` reads a
  16-entry table by piece number and can read past it. Kept as far as the game's own data goes (where the original would read foreign memory, nothing
  is modelled). [`original/animation.md`](original/animation.md#quirks-to-keep-in-vanilla-mode)
- **Every per-piece loop runs at least once**, even for an object without pieces.

## Drawing
- **The HUD's item loop skips squares:** a routine it calls clobbers the loop's register (`20D1` changes `di`),
  so after a changed item it goes on from Olaf's chosen square. Kept (found by a playtest, checked in lockstep).
  [`original/menus.md`](original/menus.md)
- **The game-over lightning is never erased:** its two frames pile up over the three video pages. Not kept (one of
  the two known picture differences). [`original/drawing.md`](original/drawing.md)
- **A stray dot in Erik's portrait when he is gone.** The routine that makes a newly chosen Viking blink (`07A2`)
  first shows "the one chosen before" again; at a scene's start there was none (slot −1), and it reads his
  sprite's pieces from a byte before the first slot's. In the village's scene of a new game the first slot's sprite
  starts at piece 24, the word it reads is `0x3000`, and its two writes land `0x3000` past the pieces' tables: in
  the HUD's portraits, which are loaded once (`ds:344D` loses a bit, in Baleog's; `ds:414D` becomes 2, in Erik's
  as he is gone). From then on that portrait has one white pixel at (1, 2); a game started by password has none.
  Found 2026-11-24 with `tools/lockstep.py`, traced in DOSBox. Not kept (the other known picture difference; the
  user's rule: a fault like this isn't copied).
- **The health the game-over scene shows is the HUD's bookkeeping,** not the Vikings': the scene reads the words
  the HUD last recorded. [`original/game-flow.md`](original/game-flow.md#slots)
- **Header flag `0x04` is set for the game-over scene and tested by nothing.** Several header bytes are the same in
  45 of 47 headers and read by no code found. [`formats/unknowns.md`](formats/unknowns.md)
- **44 of 56 packed sprite sets end in 640 bytes of `0xFF`,** the size of the largest frame; nothing reads them.

## The scenes and the kinds (found while writing the engine)
All kept: the engine does the same, and the replays check it ([`HANDOVER.md`](HANDOVER.md)).
- **A Viking's message can return through a stale call.** In the air, a dizzy Viking jumps to what he is told
  instead of calling it; a message he ignores ends with a return, which goes back through whatever he last called.
  After a hard landing that is the landing's loss of health: its sound plays and his stars start again (without
  the loss).
- **Baleog decides how he falls before gravity, Erik and Olaf after it:** in every world's listing his branch on
  the speed (landing tests or ceiling tests) comes before the animation step that adds the tick's gravity.
- **Each world's Vikings know their own set of messages:** Egypt's ignore electricity and the ground going from
  under them; the wacky world's the latter too; the spaceship's and the wacky world's ignore the drop and the
  mummy's grip. In the factory a Viking who acts on kind 95 (the wacky world's pump) is removed: the test is
  still there and leads nowhere. The crane's controls and the pump refuse a dizzy Viking.
- **A Viking carried off while he holds a crane's controls still works that crane**; leaving the controls in the
  air, he falls.
- **The last Viking dying at a level's end in the tick he reaches it** starts the level again instead of the game
  over: the end's own choice wins. An exit reached with a Viking gone starts the level again; the level's unseen end
  (kind 43) leads to the game-over scene then.
- **The view goes on scrolling after the last Viking dies:** a tick's scroll is measured at its first picture and
  moved in three shares, and the other two shares are moved whoever is left.
- **A placement created again at once** (a popped bubble) has its first turn after the tick's moves; nothing is
  created again while a scene ends.
- **The buffer packed frames are unpacked into is never cleared:** a piece that hasn't had a frame of its own shows
  what the last one left (seen when an enemy is created in view). A packed set the level doesn't load is taken
  from the level's first set: in the ship's curtain call the Vikings ask for two (entries `DD`, `E0`).
- **A scene that is one picture keeps the map of the scene before it,** but takes the camera's limits from its own
  header. A scene sets up only its own colour cycles; those the scene before had more of stay. The fade-out leaves
  the tints at 0. In a scene without Vikings the view follows what is in the first slot.
- **Scenes with header flags `E0`** (village, ship, starfield) draw their text straight onto the picture, at most
  four times after a clear: text added later is never seen (the villagers' second and third shout).
- **The second set of switches starts from header word `0x14`.**
- **Dead stores:** the ship's stops (kind 56) count themselves in word `0282`, which nothing reads; the game-over
  scene restores items from words nothing writes; word `0348` (set to `0x81` by the ship's alarm) is read by
  nothing found. Tomator's arcing shot's spark is placed by a stale scratch value.
- **The spaceship's bombable wall reads bit 15 of its argument the other way round** from what its name suggests:
  set, it is *not* kept active out of view.

## Timing
- **The sound driver's 125 Hz timer is bent to the display:** every second interrupt waits for the vertical
  retrace, so the music runs at twice the refresh rate (119.43 Hz), not the 120 or 125 Hz the driver asks for.
  [`original/timing.md`](original/timing.md)
- **The game runs at 59.71 Hz, not 60:** its Mode X has 527 lines. A tick is three refreshes, but a pass of the
  inventory is one: its cursor moves at three times the game's pace. [`original/menus.md`](original/menus.md)

## Sound (Miles driver 460, Sound Blaster Pro FM)
- **Voice stealing can steal from itself.** A 4-operator voice that takes a 2-operator channel releases the
  channel's partner too, but the driver looks the partner up by the victim's *slot* number instead of its channel.
  When that number happens to be the victim's own channel, the victim is released twice, the new voice is given
  channel `0xFF` (none) and never sounds, its MIDI channel's voice count stays one too high for good, and two
  stray bytes are written into the driver's own code (its card detection routine, which luckily ran long before).
  Reached in the game by pressing Down quickly on the password screen: the beeps pile up over the title music.
  Kept. [`original/sound-driver.md`](original/sound-driver.md)
- **The driver ignores a set of controllers the music data carries** (16, 17, 20, 21, 32–37, 58–63); the General
  MIDI driver passes them on to the module unexplained. [`formats/unknowns.md`](formats/unknowns.md)
- **"Stop effect" exists twice** (`04` and `D7`), the second with a word the routine skips.
  [`original/sound.md`](original/sound.md)
