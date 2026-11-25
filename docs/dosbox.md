# A scriptable DOSBox: checking the game against the original

Built 2026-11-17. The decision: [`decisions.md`](decisions.md), ADR-0016.

Everything in `docs/original/` was read from `VIKINGS.EXE` without running it. What only a running game can show
is checked in a DOSBox that a script can drive, so those checks run as code instead of by hand. This is how it was
built, what it found, and how it is used now.

## What it is for
1. **The spot-checks** listed in the engine docs ([`timing.md`](original/timing.md#checked-in-dosbox),
   [`menus.md`](original/menus.md#checked-in-dosbox), [`formats/unknowns.md`](formats/unknowns.md#5-what-has-not-been-compared-with-the-original)
   and the "open" sections): each one a short script.
2. **Lockstep comparison**: the original and ours play the same input and are compared. At first that was the
   original's data segment against the reference engine's copy of it, at every tick and every address; since the
   reference engine was retired it is the original's picture against the engine's (below).
3. **The reference for the Classic sound** ([ADR-0015](decisions.md)): the original
   driver's OPL register writes for any tune or effect, to compare ours with.
4. **Pictures**: the original's screen (and palette) at any tick, for the renderer.

The fork is **DOSBox-X** (ADR-0016 compares it with DOSBox Staging).

## What DOSBox-X has without changes
- **No window needed**: SDL's dummy video driver runs it headless in the dev container (Wayland only, ADR-0013).
- **Screenshots** to PNG, and **video capture**.
- **Raw OPL capture**: every register write to the OPL chip, with timing (`.dro` files).
- **The debugger** (in builds with it): breakpoints at `segment:offset`, memory and register views, memory dumps to a
  file. It is an interactive full-screen (curses) program, so driving it from a script would be fragile, which is
  why a control channel is added below.
- **Save states**: the whole machine to a file and back, so a run can start at level 1 or at any tick.
- **Sound cards**: the Sound Blaster Pro 2 with OPL3, which runs driver **460** (Sound Blaster Pro FM, the one
  ADR-0015 follows). It also emulates the Pro Audio Spectrum 16, but driver 462 needs MVSOUND.SYS (ADR-0016).

## The change: a control channel
`tools/dosbox/control.cpp` (copied into DOSBox-X as `src/misc/lvr_control.cpp`) and four hooks in
`tools/dosbox/hooks.patch`: the normal core asks before each instruction whether to stop (only while a breakpoint is
set), the VGA's vertical timer counts frames, DOS exec reports each program it starts, and the Makefile builds the
file. With `LVR_DOSBOX_CONTROL=<socket path>` in the environment, DOSBox-X listens there at the first program DOS
starts (with `-c "mount ..."`, that is MOUNT) and stops. One command per line, one reply per line (`ok ...` or
`err ...`), numbers in hex:

| Command | What |
|---|---|
| `run` / `run frames N` | emulate to the next stop: a breakpoint, an exec, or N frames (vertical retraces); the reply is `stopped REASON frame F cs=.. ip=..` |
| `break add SEG:OFF` / `del` / `clear` | breakpoints by real-mode address |
| `break exec NAME` | stop at the first instruction of the next program NAME that DOS starts; reason `exec LOADSEG` |
| `regs` / `set REG HEX` | the CPU registers |
| `read SEG:OFF LEN` / `write SEG:OFF HEX` | memory (at most 64 KB per read) |
| `key HEX` | raw scan-code bytes into the keyboard controller (set 1: make; make \| 80 = break; `E0` first for extended keys) |
| `screenshot` | the next frame drawn, as a PNG in `build/dosbox/captures/` (DOSBox-X's own capture: 640 × 480, each pixel doubled) |
| `opl start` / `opl stop` | DOSBox-X's raw OPL capture (`.dro`) into the same folder; it starts with the first note |
| `quit` | |

While stopped, DOSBox-X emulates nothing (no wall-clock time passes for the game), so a script can take its time
between steps. If the client goes away, DOSBox-X exits at once (not through its "Are you sure to quit?" question,
which without a terminal repeats forever).

`tools/dosbox.py` is the client: `Game()` starts the game stopped at its first instruction (`load`, `ds`), with
`run`, `break_add`, `read`, `write`, `key`, `screenshot`, `opl` and the rest; every reply has to come within 20 s, and
DOSBox-X is killed when the script ends. `tools/dosbox.py shot FRAMES --key FRAME:HEX ...` is the command-line
version for a picture.

Still to add, when needed: the raw VGA memory and the DAC (a 320 × 240 index picture instead of the doubled RGB
PNG), and save states.

### First results (2026-11-17)
- The game loads at segment **`0824`** (so `ds` = `2072`); its entry is `0824:0000`.
- **3 frames per tick**: in level 1, 40 passes of the main loop `0000:001E` in a row, each exactly 3 frames apart
  ([`original/timing.md`](original/timing.md#checked-in-dosbox)).
- From the start: Interplay logo (slot 39) at frame 120, Silicon & Synapse (40) at 450, title (38) at 780; Enter
  (pressed at 843 and 1086) → village intro (43) at 1176; Enter at 1329 → level 1 (slot 0) at 1419. Level 1's opening
  dialogue waits in the loop at `0000:0164` (it jumps back to `0169`, so that is where to stop) and took two Enters.
- Two runs with the same input give the same `ds:0000`–`2E7F` at level 1 (frame 1500), byte for byte, and the same
  pictures; **the random seed `ds:8639` differs** (`5E03`, `631C`, ...), so it comes from the clock.
- **Fast forward** (`turbo=true`) runs about 300 frames per second instead of 60, with the same memory as in real
  time: emulated time depends only on the fixed cycles. A run to level 1 takes about 5 s.

## Making runs repeatable
- **Fixed cycles**, no frame throttling to the wall clock; sound output off or to a null device. The emulated
  machine's timers (PIT, retrace) then depend only on emulated time.
- **Clock and random seed**: the game reads the clock once, at start-up: `int 21h` `2Ch` at `0000:297B`
  gives the random seed `ds:8639` (DX = seconds and hundredths, CX = hours and minutes). Nothing else in
  `VIKINGS.EXE` reads the time: no `int 1Ah`, no other `int 21h` `2Ah`/`2Ch`, no read of the BIOS tick count
  `0040:006C`. The PIT reads at `0000:79A8` time the joystick port, so they depend only on emulated time. `Game()`
  stops at `0000:2980`, right after that `int 21h`, and sets DX and CX to the seed asked for (`seed=`, default 0,
  the engine's starting seed). It also sets the BIOS tick count to 0 at the game's start, because DOSBox-X starts
  it from the host's clock (with that alone DOS reports 00:00:00.00 and the seed is 0 as well).
- **Checked** (2026-11-17): two runs to level 1 (frame 1500, same Enters) have the same conventional memory
  (`0000:0000`–`9FFF:FFFF`), byte for byte. Without the two fixes the only differences were the seed and the tick
  count. The intro and level 1's opening draw no random numbers (the seed is still 0 at frame 1500).
- **Game files**: `tools/dosbox.py prepare` copies the user's `gamedata/original` to `build/dosbox/game/`
  (the original folder is only read) and rewrites entry 0 there: driver 5 / variant 2 (OPL3 through 460) at port
  `0x220` (with `0xFFFF` no driver starts, [`formats/data-dat.md`](formats/data-dat.md#entry-0-configuration)), and the
  copy-protection fingerprint ([`original/game-flow.md`](original/game-flow.md)). The fingerprint is computed in our
  DOSBox-X by a small `.COM` that does what `0000:29D4`–`2A13` does (BIOS ROM checksum, `int 21h` `36h` for C:, `30h`)
  and prints it; with the working copy mounted as C: it is `02BA`, the same every run. A wrong value gives "Copy
  protection failure", the right one doesn't (checked 2026-11-17).
- **Input**: for lockstep runs, the keyboard word `ds:86DE` (and the key-state array `ds:916C` for Y/N and the hot
  keys) is written at the start of each tick, so both sides get exactly the same input; key events through the
  keyboard controller are for checks of the keyboard handler itself.

## By hand, in a window (on the host)
The same DOSBox-X and working copy, played by hand, from the repository's root:

```sh
SDL_VIDEODRIVER=wayland build/dosbox/bin/dosbox-x -conf tools/dosbox/dosbox-x.conf -set "cpu turbo=false" -nomenu \
    -c "mount c build/dosbox/game" -c "c:" -c "VIKINGS.EXE"
```

- `SDL_VIDEODRIVER=wayland`: DOSBox-X asks SDL for X11 on Linux unless told otherwise, and its SDL2 here is built
  without X11 (ADR-0013); without it, it stops at "Can't init SDL x11 not available". In the environment, not in
  the config: the scripts start the same config headless with `SDL_VIDEODRIVER=dummy`, which a driver named in the
  config would override.
- `cpu turbo=false`: the config fast-forwards (for the scripts); played by hand the game would run some 5 times
  too fast.
- Another DOSBox with the same folder may stop at "Copy protection failure": the working copy's fingerprint is this
  build's (above).

## Lockstep comparison

`tools/lockstep.py STEPS [--hold WORD] [--key STEP[-END] WORD ...] [--held STEP[-END] SCANCODE ...] [--typed STEP
CHARACTER ...] [--place STEP X Y ...]` plays the same input to the original and to the engine and compares what
each shows after the last step, pixel for pixel (as 6-bit colours). Both start a new game at the Interplay logo
(slot 39), as the original does, with the seed 0. The engine gets the input as a replay, which is kept
(`build/dosbox/lockstep/run.replay`), so a run worth keeping becomes a replay by copying it and recording its
golden (`replays/level-keys*.replay` were made that way). The exit status is 1 if the pictures differ; to find the
first step that does, halve the step count.
- **Steps**: the original stops at `0000:001E` (a tick), `0169` (a pass of the wait-for-key loop), `04C3` (a pass of
  the pause menu or the quit box) and `1C1F` (a pass of the inventory): a replay's steps. A loop's first pass, which
  follows its entry (`0164`, the inventory's set-up `1BB7`) at once, belongs to the step that entered it.
- **Input**: before each step the original's keyboard word `ds:86DE` gets that step's bits; the bits of the step
  before are taken out and the rest are left, because demo playback (`2D72`) drives the same word. During a demo the
  keyboard handler turns any key into `0xFFFF` (that ends the village intro into level 1; the pause bit alone ends it
  into the village demo), so a script presses `0xFFFF` there. Keys the game reads from the key-state array instead
  (Y, N, Alt, X, Q, F5, F6, F10) are `--held`: their byte at `ds:916C` + scancode is 1 for that step, 0 after.
  `--typed` writes the character the keyboard handler would note (`ds:028C`): the title's menu keys, a password.
- **Getting somewhere**: a level is reached through the title; with the cheat's password typed, F6 goes on a level
  at a time ([`original/input.md`](original/input.md)). `--place` puts the three Vikings at a point of the level
  (their position fields and boxes; their sprite pieces follow at the next draw), as a replay's `place` does.
- **Known differences**: the two in [`quirks.md`](quirks.md), "Drawing". While Erik is gone in a game begun with
  the title's first choice (not by password), every comparison reports one pixel at (46, 197).

### Before: memory against a reference engine
Until 2026-11-23 the lockstep compared state, not pictures. A reference engine ran the original's own bytecode and
mirrored its data segment, and after every step that memory was compared with the original's, byte for byte. Every
state matched over the whole attract cycle, the menus and scenes, a fixed pattern of input in each of the 37
levels, and a camera sweep over every level. The engine was then matched against that reference engine on the
replays, and the reference engine retired ([`formats/unknowns.md`](formats/unknowns.md#5-what-has-not-been-compared-with-the-original)
says what was compared and what never was).

## Where things go
- `tools/dosbox/`: `build.sh` fetches the pinned DOSBox-X commit, applies `hooks.patch`, copies `control.cpp` in,
  and builds it with its in-tree SDL2 (no X11) and its debugger into `build/dosbox/` (`bin/dosbox-x`; the source in
  `build/dosbox/src`; a changed patch means a fresh checkout and configure, otherwise only `make`). `dosbox-x.conf` is
  the machine: plain VGA, 386, normal core, fixed 25000 cycles, fast forward, Sound Blaster Pro 2 with OPL3 at
  220/7/1. The patch is GPL, as DOSBox-X (the licence note is in `control.cpp`'s header). Not part of the CMake
  build of the game; nothing of it is linked into the game or shipped (ADR-0016).
- `tools/dosbox.py`: the client; the checks as small scripts or as `[dosbox]`-tagged tests that skip without the
  built tool and the user's game files, as `[real-data]` tests skip without the files.
- The dev container gets DOSBox's build dependencies (`dev-env/Dockerfile`).
