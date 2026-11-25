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

### ADR-0005: Rules and Sound are switches; the visuals go with the rules
- **Visuals: Classic / Modern**, first planned as a switch of its own, swappable at any moment. Decided at the
  start of M8 (2026-11-24): the visuals go with the rules, chosen at launch, nothing switched while playing.
  Vanilla rules show the Classic picture (the CPU renderer); Remaster rules the Modern one (the GPU renderer, with
  smooth movement and what M8 adds after it). Presentation only all the same: the simulation doesn't know.
- **The Modern visuals' effects** (2026-11-25): light, glow and particles drawn in the game's own pixels (every
  light worked out per pixel of the picture, in steps of brightness; particles a pixel each in the game's fixed
  colours), not smoothed over it. A light is cast: it brightens what it falls on and the map's ground stops it.
  They answer to what the engine shows (the animation an entity was last started on, the map's tile types, the
  colours a level turns); the one thing the simulation keeps for them is which animation that was
  (`Sprite::started`), part of its state so a rewind or a quickload keeps it. Nothing for a blow or a hit
  landing. What is there: HANDOVER.md, "Effects".
- **Rules: Vanilla / Remaster**, chosen when the game is started (`--rules`) and fixed until it ends: there is no
  switching while playing, so nothing has to be squeezed back into the original's limits. Anything that changes
  what the player can see or do belongs to Rules. Under Vanilla rules the visible playing area is the original's,
  even in Modern visuals.
- **Sound: Classic / Modern** (ADR-0015), swappable at any moment.
- Every combination of rules and sound is allowed. The simulation never knows which renderer or sound is active.

### ADR-0017: The game's behaviour is written by hand, and checked by pixel match
- The original keeps its behaviour in bytecode scripts. The engine does not interpret them: every kind of entity
  is hand-written C++, read from the scripts' listings, so the code can be read, changed and extended (Remaster rules,
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
- **A kind keeps everything it remembers in one plain struct nested in its class**, `Saved`, as its member `s`
  (`s.timer`, `s.state`), with `LVR_SAVES(Base)` after it. `Entity` and `World` have their plain data the same
  way, in a struct they are derived from (`EntityState`, `WorldState`), so that the code using them reads as
  before. That is what lets the game's state be written out without naming any field (ADR-0020): adding a field
  to a kind is adding it to its struct. A kind others are derived from names its member otherwise (`enemy`,
  `shot`, `wall`). Entities refer to each other by slot number, never by pointer. `scripts/kind_state.py`, in
  the checks, holds every kind to it, and `LVR_SAVES` asserts at compile time that a `Saved` is standard-layout
  and trivially copyable (no vectors or strings: a list is a fixed array with a count), asks for no more
  alignment than a 64-bit number, and is the only member its class adds. How its fields are padded in memory
  doesn't matter between platforms, since they are never written as memory (ADR-0020).
- Kept on purpose under Vanilla rules: 20 entity slots, a new entity takes the lowest free one, update and draw in
  slot order, and the 128 sprite pieces in three pools with the original's allocation, since those decide what is
  drawn over what.

### ADR-0019: A kind says what it looks for
- A kind does not run its own hit and terrain tests. `looks_for()` returns, for the state it is in, the list of
  what it looks for (`stopped_by(walls())`, `lands_on(ground())`, `touching_a_viking()`); the engine does the
  looking after the tick's move, in that order, and the kind reads the answers in `saw` at its next turn.
- Which tests run, and in which order, is behaviour: the list is part of the kind.

### ADR-0020: The game's state can be written out and read back
- **Why:** a quicksave in a file now; later, in co-op over a network, the snapshots a server sends its clients
  to keep them in step. So it is every kind's state that is written, not the keys that led to it.
- **How** (`archive.hpp`): a plain struct is taken apart field by field (glaze's reflection of aggregates, by
  position, no names), and each field written with a fixed width, the low byte first: a bool as a byte, an enum
  as its number, arrays and vectors element by element. Nothing is written as the memory it sits in, so the
  bytes are the same on every platform. C++26's reflection would do this for any class; the compiler in use
  doesn't have it, hence the plain structs (ADR-0018).
- **A world** is its `WorldState`, then each slot's entity: its kind, and what it wrote, each with its length.
  It is read into a world made anew for the same level, which has the level's own data and makes the kinds. A
  part whose length is another than this engine would write (a kind remembers more or less by now) is refused.
- **One version stamp**, `kStateVersion`: the fields have no names, so what was written by an engine whose state
  was laid out otherwise can't be read by this one. It is the first thing in a quicksave's file, and what two
  machines will tell each other before a snapshot is sent; another version is refused with a message. It is
  raised by hand when a `Saved`, `EntityState` or `WorldState` changes, and a test that pins the written size
  of four worlds says when that was forgotten.
- **That nothing is missing** is checked on the replays: `scripts/saves.sh` (`lv-replay --saves`) writes the
  world out and reads it back before every step, and all 119 must still match their goldens.
- A world is 25 to 40 KB written out under Vanilla rules and 70 to 90 KB under Remaster rules (most of it the
  larger layer of sprite pieces). Fine for a file; a snapshot sent many times a second will want only what
  changed.

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

### ADR-0006: A Classic software picture and a GPU renderer
- **Classic** (`render.*`): an 8-bit indexed picture of 320 × 240 (a 176-line playfield over a 64-line HUD), drawn
  on the CPU from the simulation's state, the palette applied per picture, three pictures a tick with the
  original's integer sub-steps. Scaled up by whole numbers. It is what Vanilla rules show and what the goldens
  hold, and it stays: it needs no graphics card and checks every replay in under a minute.
- **The GPU renderer** (`gpu.*`, SDL_GPU, Vulkan, shaders compiled by `glslc` at build time): what Remaster rules
  show. Every picture of a scene sits in one atlas of palette indices; a view is drawn a pixel for a pixel into a
  target of indices, and shown through a second pass that looks up the palette and scales. Drawn at the
  original's view its target is the Classic picture exactly: `lv-replay --gpu` plays the replays against the same
  goldens (all 119 matched when it was written; about five times slower on a software driver, so it is not part
  of the routine checks). What it does beyond that (zoom, and later effects) is judged by eye.
- **Kept as lists, drawn when shown** (M8): a picture for the window is kept as its rectangles of the atlas, not
  as pixels, and drawn when it is shown, as many pixels a pixel as it is shown at. That is what lets something
  stop between the original's pixels and be shown at the moment of the screen's own refresh.
- **Smooth movement** (built, Remaster rules): the original moves a tick's distance in three whole-pixel shares
  (3, 2, 2 of 7). Shown smoothly, every entity's pieces and the camera go evenly over the tick, a third of a tick
  behind where the original's picture has them (so nothing has to be known of the next tick: input is as
  prompt as before). Only that split is evened out; a new frame, a flip, a jump are shown as the game makes
  them. The pieces an animation moves itself, the HUD, and pictures that aren't a tick's (a scene's start, the
  inventory, a menu, rewind) aren't moved.
- **Ahead** (ROADMAP, Modern visuals): parallax, lights, shaders, art packs that replace sprites one for one,
  on the GPU renderer.

### ADR-0011: Remaster rules
Only under Remaster rules (ADR-0005). First called the QoL rules.
- **Zoom-out** (built): three steps, the original's view, one and a half times and twice it; no more. The whole
  screen zooms as one, the HUD too: a zoomed view is as high as the screen (480 × 360, 640 × 480) and the HUD
  lies over the middle of its bottom. The camera keeps the Viking in the middle of what is above the HUD and
  goes 64 pixels past the level's bottom, so the HUD never hides the floor; where a Viking is low in a level
  there is then nothing of the level beside the HUD, and it is black. (Tried and not kept: the camera staying
  inside the level and the HUD sliding to the top of the screen while the Viking would be behind it.) A change of view takes a third
  of a second: the world's view grows at once and the picture opens up, or the picture closes in and
  the view shrinks when it is there. A view is never larger than the level. A level is started with the zoom
  the player has (no change of view to show as it comes in). A view less high than the screen (a level that
  low) is in the middle of what is above the HUD. Only the 37 levels zoom (not the village, which has a map and
  the Vikings too).
- **The active area follows the zoomed view** (where entities exist: the view plus 16 pixels), and so does the
  edge the enemies turn at. Not the whole level: that keeps the game's rhythm, an enemy starting as it comes into
  view.
- **Text boxes** are part of the playfield and zoom with it. One at a speaker is placed by the speaker anywhere
  in the view; one at a fixed place (a password, a menu) is in the middle of the screen.
- **No limit on entities** (built): 512 slots against the original's 20, and a layer of 2,048 sprite pieces
  against its 128. The layer keeps the original's three pools in their order, so what is drawn over what stays;
  each piece of the Vikings' and enemies' pool (256, against 16) has a frame buffer of its own. A side effect:
  a new enemy hardly ever shows, for a moment, the frame the buffer's last owner left there.
- **Zoom is not part of a replay.** Replays are Vanilla's.
- **Minimap with fog of war** (built, simple on purpose: just a map to look something up on). A square of the
  level around the view, 64 cells a side, in the screen's bottom left corner, as high as the HUD. Each cell is
  coloured by what it is to the entities (ground, a platform, water, lava, spikes, electricity, a spring; a
  ladder dark with lighter rungs). Only what a view has shown is drawn, the rest is dark. On it the three
  Vikings, the one being played framed; no enemies or other entities, and no outline of the view. Its frame
  is in the original's pixels with two cut off each corner, as the rewind's bar's (the user, 2026-11-25). It is
  drawn
  smoothly at every refresh: the map scrolls with the view by fractions of a pixel, and what the view shows now
  and where the Vikings are is exact, not rounded to the map's cells. At the original's view the HUD moves right
  by a column of its stone tiles to make room (it slides aside, then the minimap fades in; the other way round
  when it is put away); zoomed out the smaller HUD is clear of the corner and stays in the middle. M puts it
  away (not Tab, which is the inventory). What was seen is the player's, not the simulation's: `Session` keeps
  it per level, and it is kept in the player's profile (below). Only in the 37 levels, and not while a scene
  fades in or out.
- **Shots stop at a slope** (Remaster rules): the original's straight shots (the gunners', the troopers')
  stop at walls only and fly on through a slope's ground; zoomed out that is seen, so they burst there.
- **The game-over letters drop in front of the Vikings** (Remaster rules): their pieces come from the front
  pool instead of the middle one, where they end up under the Vikings' figures in the original.
- **Rewind** (built): while Backspace is held, time runs backwards at twice its speed; let go, the play goes on
  from where it is. Only in a level being played (one of the 37; not a demo, a menu, or the game-over scene),
  and never back past the level's start: what was kept is let go whenever a scene starts. A level has 10
  seconds of it (the user, 2026-11-25), counted in the play gone back over (so 5 seconds of holding the key),
  and gets them again only when a level is started, the same one again too (after the game-over scene, or the
  cheat's F5 and F6); a quicksave brought back doesn't. A bar along the minimap's bottom shows what is left
  (the user, 2026-11-25: in the minimap so as not to take from the original's HUD; simple, horizontal, and in the
  original's pixels to go with the game, not smoothed, a little see-through): a white frame with two pixels cut
  off each corner, light grey inside from the left as far as there is rewind left, dark past that, and the
  rewind's mark (two triangles pointing left) in its middle, dark on the grey and grey on the dark. It is part of
  the minimap: there when it is, faded in and out with it, and put away with it (M). With nothing left to go
  back to, or none of the 10 seconds, the rewind is over and the play goes on. The pictures between the ticks are there as they were
  going forwards: a step that is undone is run again from its snapshot with the keys it had, for its pictures.
  The sound of those seconds is heard backwards meanwhile, twice as fast (the last 10 seconds of what was
  played are kept), and the music then goes on from where it was. Two triangles flash in the screen's top
  right corner while it runs. The minimap is not taken back. The zoom is the player's, not the play's: a step
  brought back keeps the view the zoom has, and so does a quicksave brought back.
- **A world can be copied**, which is what rewind and quicksave are: a snapshot is a copy of the `World` (each
  entity its own copy) with the level it belongs to. The rewind keeps one before every step. That a copy keeps
  everything is checked on the replays: `lv-replay --copies` replaces the world by a copy of itself before
  every step, and all 119 still match their goldens.
- **Quicksave** (built), only with the game's own cheat on (its password typed at the title, or `--cheat`): F7
  keeps the play as it is, in a level; F8 brings it back from wherever the game is by then, in this run or a
  later one. One.
- **No autosave and no level select** (taken out of M7): the passwords are how a level is reached, under both
  rules.
- **The player's profile** (built): a folder of what is the player's and not the simulation's, kept from one
  run to the next; Vanilla rules keep none. By default the folder `profiles` beside the game's program, where it is found without knowing
  where to look (not a folder somewhere in the user's home); or `--profile DIR`. In it so far `minimap.txt`: what the minimap has seen of each level, as text (runs of seen
  and unseen pixels). It is read at the start and written when a scene is loaded and when the game is closed; a
  file that can't be read is said and written over.
- **The quicksave is a file in the profile too** (`quicksave.bin`, ADR-0020): written when F7 keeps the play,
  read when the game starts. It begins with the state's version (ADR-0020) and ends with a sum over the world's
  bytes: a file of another version, or a damaged one, is said and left alone.
- **An unlimited active area** is not built: one player doesn't need it. It comes back with co-op (ADR-0012),
  where several views share one world: either the active area is every view's together, or everything runs.

### ADR-0012: Co-op for up to three
- One to three players; with fewer players than Vikings, a player can switch to a Viking nobody plays.
- Local: a split screen that merges when everyone fits in one view, an active area per view. Online: a full screen
  each, lockstep or rollback (ADR-0003).
