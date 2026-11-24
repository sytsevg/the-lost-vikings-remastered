# Decisions

The decisions that shape the project, as they stand. Each keeps the number it was made under (`ADR-00NN` in code
comments and other docs points here). A decision that was later replaced is not listed; the one that replaced it is.

## The project

### ADR-0001: Clean-room engine; users supply their own game data
- Blizzard owns The Lost Vikings. No original data, executables or anything derived from them is ever committed
  (`gamedata/` is ignored), and no text of the game appears in the repository: not in code, comments, tests or docs.
- The reference release is the original DOS game. The game and `lv-export` accept only known file hashes
  ([`formats/releases.md`](formats/releases.md)).
- How the original behaves is learned by disassembling our own copy of `VIKINGS.EXE` and by running it in DOSBox
  (ADR-0016). `docs/original/` records rules, values and addresses as evidence, never decompiled or disassembled
  code, and our code is written from those write-ups.
- Other people's work: a project built from decompiled code, or under a licence that isn't permissive, may be
  read and run to compare, never copied, linked or shipped. CC0 material (RyanMallon's notes) may be used freely.

### ADR-0005: Visuals, Rules and Sound are independent switches
- **Visuals: Classic / Modern**, swappable at any moment. Presentation only.
- **Rules: Vanilla / QoL**, chosen per game session. Anything that changes what the player can see or do belongs
  to Rules. Under Vanilla rules the visible playing area is the original's, even in Modern visuals.
- **Sound: Classic / Modern** (ADR-0015), swappable at any moment.
- Every combination is allowed. The simulation never knows which renderer or sound is active.

### ADR-0017: The game's behaviour is written by hand, and checked by pixel match
- The original keeps its behaviour in bytecode scripts. The engine does not interpret them: every kind of entity
  is hand-written C++, read from the scripts' listings, so the code can be read, changed and extended (QoL rules,
  co-op).
- **Pixel match** is the bar under Classic visuals and Vanilla rules: on the same input, the same picture
  (indexed pixels and palette) and the same sound and music requests at every refresh. The insides are free.
- It was reached by matching a reference engine (an interpreter of the original's bytecode, itself compared with
  the original in DOSBox state for state), which was retired once everything matched. Its runs are the goldens in
  `replays/`; `tools/lockstep.py` compares with the original directly.
- **Accidents of the original are not copied.** A fault that only shows by luck (a stray memory write that leaves
  a wrong pixel) is traced, written into [`quirks.md`](quirks.md) as "not kept", and left out. Quirks that are part
  of how the game plays are kept.
- Imported as they are: graphics, level layouts, music, texts and the animation programs. Ours: the engine and all
  game logic.

## The simulation

### ADR-0003: Deterministic, integer-only, fixed-tick simulation
- The simulation is a pure step from state and input to state: integer arithmetic only, no wall-clock time, no
  unseeded randomness, no knowledge of rendering, audio devices or SDL. Its numbers are the original's 16-bit ones
  and wrap round on purpose.
- One tick is three refreshes of the original's display: 59.71 Hz (Mode X has 527 lines), so 19.90 Hz, 50.24 ms
  ([`original/timing.md`](original/timing.md)). The game keeps that rate with its own clock, whatever the host's
  monitor does. Input is read once per tick.
- State can be copied and hashed. That gives replays (an input log), goldens (a hash per refresh), and later
  rewind (a ring of snapshots) and online co-op (lockstep or rollback) without restructuring.

### ADR-0018: An entity is a class with explicit states
- One class per kind, derived from `Entity`, with plain data members and an enum of states: one state for each
  place the original's script waits. `update(World&)` is one tick's turn. No coroutines.
- The engine's services (tile tests, moving, gravity, creating entities, the camera) belong to `World`. A snapshot
  copies the world.
- Kept on purpose under Vanilla rules: 20 entity slots, a new entity takes the lowest free one, update and draw in
  slot order, and the 128 sprite pieces in three pools with the original's allocation, since those decide what is
  drawn over what.

### ADR-0019: A kind says what it looks for
- A kind does not run its own hit and terrain tests. `looks_for()` returns, for the state it is in, the list of
  what it looks for (`stopped_by(walls())`, `lands_on(ground())`, `touching_a_viking()`); the engine does the
  looking after the tick's move, in that order, and the kind reads the answers in `saw` at its next turn.
- Which tests run, and in which order, is behaviour: the list is part of the kind.

## Data

### ADR-0014: The game reads the original's files directly
- At start-up `lvr::assets` finds `VIKINGS.EXE` and `DATA.DAT` in any letter case, checks the release, and decodes
  what the game needs. There is no import step and nothing derived is written to the user's disk.
- `VIKINGS.EXE` is an input like `DATA.DAT`: the texts, the level passwords and several tables are read from it.
  Offsets inside it are valid for a known release only, so they belong to the release's table.
- A SNES ROM image next to `DATA.DAT` is an optional third input, for the backdrops the DOS game lacks
  ([`formats/snes.md`](formats/snes.md)).
- `lvr::assets` is plain C++ without SDL; every decoder takes bytes, is tested on hand-made data and has a fuzz
  target.
- `lv-export` is a development tool, not shipped: it writes the same data as indexed PNG and JSON for inspection,
  and listings of the scripts, into `gamedata/exported/`. Nothing loads those files.

### ADR-0015: Sound: OPL3 as the original played it, or General MIDI through a SoundFont
- The music and the effects are XMIDI, in five versions for five devices ([`formats/audio.md`](formats/audio.md)).
  The game plays two.
- **Classic: OPL3 FM.** The OPL3 version of the data and the instrument bank, with the behaviour of the original's
  Sound Blaster Pro driver (460) read from its code, through ymfm's chip emulation. The goal is the original's
  register writes, checked against a capture from DOSBox.
- **Modern: General MIDI through a SoundFont.** The General MIDI version of the data, rendered by TinySoundFont
  with GeneralUser GS (loaded from a file, not in the repository).
- The simulation asks for the same sounds either way. The two versions are different arrangements, so a switch
  starts the tune again.
- **Classic is the original, limits included:** at most four effects at a time, and an effect takes channels
  (and so notes) from the music while it sounds. **Modern is ours to shape:** its effects have a synthesizer of
  their own, so the music plays on untouched, up to seven sound at a time, the last notes of an effect ring
  until its sequence ends, and one effect ending doesn't cut another short. Where the General MIDI arrangement
  of an effect grates through a SoundFont, the effect is remade from the Classic one's notes on an instrument
  of our choosing (`kRemade` in `src/audio/player.cpp`).
- Not supported: PC speaker, OPL2, and the MT-32 (its emulator is LGPL and needs copyrighted ROMs).

## Building

### ADR-0002: C++23 with Clang, sanitizers, clang-tidy and clang-format
- C++23, Clang and libc++ (hardened in debug and sanitizer builds), CMake presets with Ninja: `debug`,
  `asan-ubsan` (the default for development and tests), `release`, `fuzz`, `coverage`, `ci`.
- Warnings `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`; `-Werror` and clang-tidy in the `ci` preset
  (`scripts/check.sh`). Third-party code is compiled without warnings and included as system headers.
- clang-tidy: the bug-finding groups in full, a few guideline checks, the naming rules, and no result of our own
  functions dropped (`(void)` marks a deliberate discard). clang-format: Google base, 4 spaces, 120 columns.
- Coverage of our own code by the unit tests: at least 80% of lines, with and without the user's game files.
- Details and commands: [`development.md`](development.md).

### ADR-0009: Dependencies pinned and built from source
CMake `FetchContent`, each pinned to a tag or commit and built from source so the sanitizers cover it. All
permissively licensed; the project's own code is MIT ([`../THIRD-PARTY-NOTICES.md`](../THIRD-PARTY-NOTICES.md)).

| Library | Use | Licence |
|---|---|---|
| SDL3 | window, input, audio, PNG | zlib |
| ymfm | OPL3 emulation (Classic sound) | BSD-3 |
| TinySoundFont | SoundFont synthesizer (Modern sound) | MIT |
| glaze | JSON (`lv-export`) | MIT |
| Catch2 v3 | tests | BSL-1.0 |

The SoundFont (GeneralUser GS 2.0.3) is data, not a dependency: loaded from `gamedata/` at run time. For
publishing: its author asks not to link his download files directly.

### ADR-0010: Layout
- `src/` holds a folder per area, flat inside: `assets` (reading the original's files), `audio`, `image`, `input`,
  `lvr` (the engine and the game), `replay` (replays and goldens), `exporter`. One file pair per subject, not per
  type. Every target is defined in `src/CMakeLists.txt`.
- `<dir>/tests/test_<name>.cpp` tests `<dir>/<name>.*`; each test file stands alone. Tests that need the user's
  game files are tagged `[real-data]` and skip without them.
- Only `image`, the programs' `main.cpp` files and `lv-export` use SDL.

### ADR-0013: Platforms and one executable
- Modern desktops. Linux is Wayland only (SDL3 built without X11).
- One self-contained executable: SDL3 and every dependency linked statically, the C++ runtime too in release
  builds; on Linux only glibc and what SDL loads at run time stay dynamic.
- Windows and macOS come later.

### ADR-0016: A patched DOSBox-X as the reference
- A DOSBox-X with a small control channel (run, breakpoints, memory, keys, screenshots, OPL capture) that a script
  drives: [`dosbox.md`](dosbox.md). It is GPL, a development tool only: built outside the game's build, never
  linked or shipped.
- It is what settles a doubt about the original: `tools/lockstep.py` plays the same input to it and to the engine.

## Ahead

### ADR-0006: A Classic software picture and a Modern GPU renderer
- **Classic** (built): an 8-bit indexed picture of 320 × 240 (a 176-line playfield over a 64-line HUD), drawn on
  the CPU from the simulation's state, the palette applied per picture, three pictures a tick with the original's
  integer sub-steps. Scaled up by whole numbers.
- **Modern** (ROADMAP): SDL_GPU, smooth movement between ticks at the host's rate, parallax, lights, shaders, art
  packs that replace sprites one for one. Both read the same state.

### ADR-0011: QoL rules
Only under QoL rules (ADR-0005).
- **Zoom-out:** up to 2× the original view by default, 4× allowed in the configuration. The active area (where
  entities exist: the view plus 16 pixels in the original) follows the zoomed view.
- **Unlimited active area:** a switch of its own, to try how the levels behave; no limit on the number of
  entities.
- **Minimap with fog of war:** only what a Viking has seen, kept in the player's profile per level. Profile data,
  not simulation state.
- **Rewind:** up to 10 seconds, from a ring of snapshots; the minimap is not taken back.
- **Saves:** automatic progress and a level select. Passwords keep working; Vanilla rules have only passwords.

### ADR-0012: Co-op for up to three
- One to three players; with fewer players than Vikings, a player can switch to a Viking nobody plays.
- Local: a split screen that merges when everyone fits in one view, an active area per view. Online: a full screen
  each, lockstep or rollback (ADR-0003).
