# Timing

**Status:** observed (static disassembly of `VIKINGS.EXE`) and **checked in DOSBox** ([the end of this
page](#checked-in-dosbox)).

**Evidence:** `VIKINGS.EXE` (vanilla-dos, 142,224 bytes) disassembled with `objdump -mi8086`. It is not packed.
Addresses are `segment:offset` relative to the load segment: `0000` is the main code segment, `0BAD` the Miles
AIL library (timer services and driver interface), `184E` the data segment (`ds`). Only the rules are written down
here (ADR-0001).

## Summary

| | Value |
|---|---|
| Video mode | Mode X 320×240: 25.175 MHz dot clock, 800 × 527 total → **59.71 Hz**, 16.747 ms per refresh |
| Game tick | **3 refreshes** → **19.90 Hz**, 50.24 ms |
| Rendered frames | one per refresh, **3 per tick**, sprites moved by integer thirds |
| Input | sampled once per tick, at the start |
| Timer interrupt | 120 Hz with any sound driver, 125 Hz with no sound; resynced to vertical retrace every 2nd interrupt |
| Music/effects driver | serviced on every timer interrupt: 2 per refresh → **6 XMIDI ticks per game tick** |

## Display rate
The game sets BIOS mode 13h and then reprograms it into Mode X: misc output `0xE3` (25.175 MHz clock), vertical
total 527 lines: CRTC `0x06 = 0x0D` plus bit 9 from the overflow `0x07 = 0x3E` is `0x20D` = 525, and the register
holds the lines **minus 2** (the table at `ds:89DC` is Abrash's Mode X table). With 800 dots per line that is
25,175,000 / (800 × 527) = **59.71 Hz**, as DOSBox-X computes it ([below](#checked-in-dosbox)); mode 12h's 525 lines
(`0x20B`) are the 59.94 Hz one. This answers the "maybe 70 Hz" question in `README.md`: no, Mode X 320×240 is
the 60 Hz mode.

## Timer and vertical-retrace lock
The game uses the Miles AIL timer services. AIL keeps up to 17 timers. It programs the PIT (channel 0, mode 3) with
the **shortest** requested period, and on each interrupt adds that period to every timer's accumulator and calls the
timers whose period has elapsed.
- Period from a frequency: `period_us = 1000000 / hz`; PIT divisor `= period_us × 10000 / 8380` (integer maths,
  ≈ `period_us × 1.19318`).
- **With a sound driver:** only the driver's timer is registered. All nine drivers in `DATA.DAT` (455–463) declare a
  service rate of **120 Hz** in their descriptor → 8,333 µs → divisor 9,943 (`0x26D7`) → 8.3332 ms.
- **No sound:** the game registers a dummy timer whose callback does nothing, at **125 Hz** (`0000:7693`) →
  8,000 µs → divisor 9,546 (`0x254A`) → 8.0005 ms. Its only purpose is to keep the interrupt (and so the retrace
  lock below) running.

Blizzard's addition to the AIL interrupt handler (`0BAD:040C`, called on every timer interrupt): every **2nd**
interrupt it
1. sets the PIT to `0x7FFF` (so no interrupt can arrive during the wait),
2. busy-waits until the VGA **vertical retrace** bit (`0x3DA` bit 3) is set,
3. reprograms the PIT with the normal divisor, which restarts the count at the retrace,
4. far-calls the game's **retrace handler** (`0000:797B`).

Two timer periods must fit in one refresh for this to catch every retrace: 2 × 8.0005 ms (125 Hz, which the timer
runs at with a driver too, [below](#checked-in-dosbox)) = 16.001 ms < 16.747 ms. So the retrace handler runs **once per refresh**,
and the timer is phase-locked to the display.

Retrace handler (`0000:797B`), once per refresh. It does nothing unless `ds:92FF` is set and the flag `cs:28A8` is
clear (what sets and clears them is not read; [`../formats/unknowns.md`](../formats/unknowns.md#4-vikingsexe-routines-and-variables-not-read-in-detail)). Otherwise it
- writes the horizontal pel panning (Attribute Controller register `0x13`, with bit 5 set) for smooth scrolling,
- decrements the frame counter `ds:A39C`,
- runs a pending palette job selected by `ds:7EFE` (0 = none, 2 = palette step at `0000:0FFC`, 4 = upload the full
  palette from `ds:8202` at `0000:0FE6`), then clears it.

## Game loop
The in-level loop is `0000:001E … jmp 001E`. One pass is **one game tick** and contains **three phases**. Each phase
is: game code, then
1. **wait for retrace** (`0000:0130`: loop while `ds:A39C ≥ 1`),
2. **draw** the frame into the back page ([`drawing.md`](drawing.md)): `0C83:15D5`, `0000:65AA`, `0000:6661`,
   `0000:406D` (phases A and B only), `0C83:156C`, `0C83:00C1` (with `ax = −2`), `0C83:1897`,
3. **flip** (`0000:6775`): set the CRTC start address (with screen-shake offset `ds:03A0`), latch the pel panning
   for the retrace handler, and set `ds:A39C = 1`.

The game code of each phase, in call order (all in segment `0000`; what each does: [`game-loop.md`](game-loop.md#one-tick)):

| Phase | Game code before the wait |
|---|---|
| A | input `2352`, `2D72`, `02AD`, pause menu `041C`, state changes `0138`, `1BA5`, `2E79`, `0813`, `673C`, `4207`, `386B`, `625D`, `5546`, `3916`, `064B`, **sub-frame step 0** `2FC6` |
| B | `2E16`, `5530`, `0704`, **sub-frame step 1** `2FCB`, `2D2C` |
| C | `0753`, `3C0C`, **sub-frame step 2** `2FD0`, `1792`, palette fades `01BE` |

After phase C's flip: `08C8` (Alt+S / Alt+M), `0350` (the quit keys), `086F` (the dialogue player), then the checks that leave the
loop (level end, restart).

Rules that follow:
- **Tick rate = refresh / 3 = 19.90 Hz.** The loop's pace comes from the three retrace waits.
- **Input** is read once per tick, at the start of phase A (`2352`): current state `ds:03B6`, newly pressed this
  tick `ds:03B8` (= current AND NOT previous), previous `ds:03BA`. Two input sources are ORed together (`ds:86DC`, only
  when `ds:86DA` is set: the joystick, and `ds:86DE`: the keyboard; [`input.md`](input.md)).
- **No catch-up.** The flip sets the counter to 1 and the wait only checks it, so a phase that takes longer than a
  refresh just makes that phase (and the tick) longer. The game slows down on a slow machine and never skips ticks.
  Our vanilla timing is the ideal case: exactly 3 refreshes per tick.

## Sub-frame movement (3 frames per tick)
For each moving object, the change in position over the tick (new − old, separately for x and y) is applied to its
sprites' screen positions in three integer steps, one per phase. With `a = |d|` and `s = sign(d)`:

| Phase | Step | a = 1 | a = 2 | a = 3 | a = 4 | a = 5 |
|---|---|---|---|---|---|---|
| A | `s · ⌈a/3⌉` | 1 | 1 | 1 | 2 | 2 |
| B | `s · ⌊(a+1)/3⌋` | 0 | 1 | 1 | 1 | 2 |
| C | `s · ⌊a/3⌋` | 0 | 0 | 1 | 1 | 1 |

The three steps always add up to `d` (`0000:227E`, `22C0`, `22F3`, applied by `0000:2FE5`). Only objects with both
words `[di+1355]` and `[di+1AD5]` non-zero move this way: the object's code segment and its sprite
piece count ([`script-vm.md`](script-vm.md#object-fields)).

## Audio
The driver's timer has the same period as the PIT, so the driver runs on every interrupt: exactly **2 times per
refresh**, 119.43 Hz (2 × 59.71) rather than a nominal 120 Hz, locked to the display (measured, below). One game tick is therefore exactly
**6 XMIDI ticks** (see [`../formats/audio.md`](../formats/audio.md) and [ADR-0015](../decisions.md)).

## Checked in DOSBox
`tools/spotcheck.py timing`, the original in DOSBox-X with driver 460 ([`../dosbox.md`](../dosbox.md)):
1. **PIT divisor: 9,546** (`0x254A`, 125 Hz) in level 1, with driver 460 running and sound on (`ds:0302` = 0). That
   is **not** what the static reading above expects (9,943 from the drivers' 120 Hz). The game's own 125 Hz dummy
   timer is registered only on the no-sound path (`0000:7679`), which this run didn't take. **Found:** AIL registers
   the driver's timer at the descriptor's service rate **+ 5** (`0BAD:0B71`: `si` = rate (`es:[di+0x14]`) + 5, then
   the set-frequency routine `09D6` at `0BB5`): 120 + 5 = 125 Hz. So the driver is serviced 125 times a second;
   whether its sequencer counts XMIDI time from that or assumes 120: **measured**:
   neither; the music advances at **119.43 Hz, 2 XMIDI ticks per refresh** (59.71 × 2). A raw OPL
   capture of tune 0 from the logo (40 s, `tools/spotcheck.py opl --slot 39 --frames 2400`) against entry 496's
   note ticks: the key-ons fit best at 8.373 ms per tick (119 of 224 onsets within 5 ms; at 8.000 ms, i.e. 125 Hz,
   35; at 8.333, 120 Hz, 28). So the driver is serviced twice per refresh as the game's retrace handler restarts
   the timer, one XMIDI tick each: **6 XMIDI ticks per game tick**, as above, now at the measured refresh rate.
2. **3 refreshes per tick: confirmed**, in level 1 (40 passes of `0000:001E`, each exactly 3 frames apart). Outside
   the tick, also confirmed: the wait-for-key loop (`0164`) 3 frames per pass, the pause menu (`04C3`) 3, the
   inventory (`1C1F`) **1** ([`menus.md`](menus.md#checked-in-dosbox)).
3. **Refresh rate: 59.71 Hz, not 59.94.** DOSBox-X computes the game's mode at 59.713 Hz (its VGA log: "VGA refresh
   rate is now, 59.713", mode 320 × 480 doubled). The vertical total register holds the number of lines **minus 2**,
   so `0x20D` is 527 lines: 25,175,000 / (800 × 527) = 59.71 Hz, a tick 50.24 ms (19.90 Hz). Decided:
   the game runs at this rate, the original's exactly (ADR-0003, ADR-0006 updates).

## Slot loads
A step that loads a slot also runs the fades ([`drawing.md`](drawing.md#palette)): 70 refreshes of fade out, the
level start's tick (4 draws), 71 of fade in, then the rest of the tick (3): 148 refreshes in the engine. In DOSBox-X at 25,000 cycles the same steps took 149, 150 and 160 refreshes (the logo → Silicon
& Synapse, → the title, → attract demo 1's level): the rest is the time the emulated 386 spends loading and
decompressing the slot's data, which depends on the machine, so the engine doesn't add it
(`tools/lockstep.py`-style stops, full speed).
