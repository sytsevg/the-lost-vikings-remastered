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
- **The screen's look** (2026-11-25): how the window shows the finished picture: as it is, as a CRT would
  (Hyllian's CRT shader), or with its stair-steps rounded off (Hyllian's xBR). Shaders taken from
  others only under a licence that fits the project's MIT (crt-royale, GPL, was turned down for that). A display setting of the Modern visuals, not part
  of the rules: the menu changes it (ADR-0011; F11 went through them until the user left it to the menu), and the profile keeps
  it (ADR-0011); `--shader` only for a dump. Remaster rules only (the user): Vanilla's window stays the CPU renderer's.
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
- **Sound: Classic / Modern** (ADR-0015). Vanilla rules play the Classic sound only, the original's (the user,
  2026-11-25); under Remaster rules the menu chooses either, at any moment. The simulation never knows which
  renderer or sound is active.

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
- State can be copied and hashed. That gives replays (an input log), goldens (a hash per refresh), rewind (a ring
  of snapshots), and online co-op by rollback (ADR-0012), without restructuring: a step run again from a snapshot,
  shown or not, must come out the same.

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
- **Why:** a quicksave in a file; in co-op over a network (ADR-0012), the state hashed to find a machine out of
  step, and the snapshot a player who joins a game under way is sent. So it is every kind's state that is written,
  not the keys that led to it.
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
  world out and reads it back before every step, and the replays must still match their goldens (a way through
  each level and the scenes by default, every replay with `--all`); that every kind keeps its state in its `Saved`
  is checked without playing (`scripts/kind_state.py`).
- A world is 25 to 40 KB written out under Vanilla rules and 70 to 90 KB under Remaster rules (most of it the
  larger layer of sprite pieces). Fine for a file. The snapshot sent to a player joining under way is to be
  written smaller: integers of variable length (LEB128, signed ones zigzagged), and without the free sprite pieces.

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
- **Classic is the original, limits included** (under Remaster rules, one exception, the user's: the fire jets'
  roar at 60% of its loudness): at most four effects at a time, and an effect takes channels
  (and so notes) from the music while it sounds. **Modern is ours to shape:** its effects have a synthesizer of
  their own, so the music plays on untouched, up to seven sound at a time, the last notes of an effect ring
  until its sequence ends, and one effect ending doesn't cut another short. Where the General MIDI arrangement
  of an effect grates through a SoundFont, or is missing from it (the fire jets' roar: its General MIDI stream is
  empty), the effect is remade from the Classic one's notes on an instrument of our choosing (`kRemade` in
  `src/audio/player.cpp`).
- **Modern's effects are placed by where they happen** (built 2026-11-25): an effect an entity asks for is heard
  across the view from left to right by where the entity is (the view's edges 48 of 63 to a side: nothing on
  screen is all the way to one ear), and quieter the further outside the view it is, silent half a view away;
  one of the scene's, in the middle. The request says where it happened (`SoundCue::x`, `y`); the session works
  out the rest from the view as it plays it (`Session::placed`), and the sequencer pans the effect's channels as
  its notes start (`Sequencer::set_pan`; the effects lock channels of their own, so two at once are placed
  apart). The music isn't placed, and the Classic sound never is (Remaster rules only, as the Modern sound is).
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
| SDL3_net | co-op online's TCP and UDP (patched: `cmake/sdl_net.patch`) | zlib |
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
- **The screen's look** (built, 2026-11-25; ADR-0005): with it, everything shown (the picture, the HUD, the
  minimap and the rewind's bar and mark, the menu) is drawn into a texture the window's size in the game's whole
  pixels, and a last pass makes the look from those pixels, found on the grid they were drawn on. Never in a
  picture of a whole level (a development tool's) or the checks.
- **The crt look** (built 2026-11-25, the user: "looks great"; Hyllian's CRT shader, MIT, adapted:
  THIRD-PARTY-NOTICES.md; `gpu_hyllian.frag`): its main pass and its last pass's mask in one, with its preset's
  settings: each of the picture's rows a scanline, its beam wider where it is brighter, softened along the row,
  in linear light; an aperture grille's mask in the window's pixels. Not its colour grading (the game keeps its
  colours), glow or curvature. Zoomed out the scanlines stay a row of the game's pixels each (Hyllian's cut
  them off past 400 rows, which made twice the view brighter: the user); where a row is less than three of the
  window's pixels high a window's pixel takes the beam over the whole of it. Chosen over crt-royale (GPL v2+,
  which doesn't fit the project's MIT) and crt-lottes. Tried and dropped (the user, 2026-11-25): a VGA monitor's
  look (480 fine lines; only blurry), a television of our own (240 lines and a slot mask, flat glass; "not great"
  beside this one), and curved glass.
- **The xbr look** (built and approved by the user, 2026-11-25; Hyllian's xBR-lv2, MIT, adapted: THIRD-PARTY-NOTICES.md). xBR
  rounds a picture's stair-steps off from each pixel's neighbours and blends their edges. The blending makes new
  colours, which can't be palette indices, so it is a last pass over the finished picture (`gpu_xbr.frag`),
  which is then drawn in the game's whole pixels: what moves between ticks isn't moved (no smooth movement), and
  the tiles join up. One look at a time: this or crt. Tried and dropped (the user, 2026-11-25:
  "doesn't work"): xBR without blending (Hyllian's noblend rules) on each tile and sprite's piece by itself as it
  is drawn, which kept palette indices and smooth movement.

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
  file that can't be read is said and written over. And `settings.txt`: the player's settings (below), written
  when the menu is closed and when the game is; a line in it a player may edit by hand that can't be understood
  is said and skipped, the rest is kept. (It took the place of `screen.txt`, which kept the screen's look alone.)
- **The menu** (built, 2026-11-25; the user: "a simple overlay with some options", in a level): Escape, or a
  gamepad's Start, in a level being played; elsewhere they stay the original's pause key (P pauses in a level).
  While it is open the game holds: no step runs, so the game never knows (replays, rewind, quicksave untouched);
  the music plays on, so the volumes can be heard as they change, and the world is shown as it is, a little darker
  as under the original's pause menu and quit box (the user), with what the menu changes on it at once, the zoom too (as while the world holds for a key: what a larger view brings in is
  created when the play goes on). Its pages: the options, the keyboard, the gamepad, giving up the level (it opens
  the original's pause box, where that is, by giving the game its pause key) and quitting (the original's quit
  box); no line to resume (the user: Escape does that). The options: the sound (Classic or Modern), a volume from 0
  to 10 each for the music and the effects (the user: sliders rather than on and off), the screen's look, the
  zoom, the minimap, the visual effects (the Modern visuals' light, glow and particles). Named SOUND FX and VISUAL
  FX in the menu (the user: "lights" undersold the second). Drawn on the CPU in the original's pixels, in the
  game's own font, in a box with the rewind bar's frame (white, its corners cut off) but black and hardly
  see-through (the user), and laid over the screen as it is shown (through the screen's look like the rest), its
  pixels as large as the game's: smaller zoomed out, as the HUD and the text boxes are, but never smaller than at
  the zoom's middle step, 480 across (the user: at twice the view it was too small).
- **The volumes** go through the sequencer's own volume of a tune or an effect (the driver's, which the game uses
  for its fades): the same for both sounds, and at 10 the Classic sound's chip gets what it always did. At 0 no
  note is played (the chip's quietest level can still be heard). The Classic sound is one chip for the music and
  the effects together, so a gain on what it makes couldn't tell them apart.
- **Controls** (built, 2026-11-25): what does each action, on the keyboard and on a gamepad, is the player's,
  set in the menu and kept in `settings.txt`; Remaster rules' own actions (rewind, zoom, minimap, the quicksave) are among them. On the keyboard a key an action (the user: not the original's several,
  the keypad's too): choosing a key puts it in place of the one the action had. On a gamepad any number (the
  d-pad and the stick both walk): choosing a button adds it. Either is taken from the action that had it. The
  gamepad's page binds only with a gamepad there, and says whether one is (found, or flashing that none is) as
  pads are plugged in and out. Fixed: Escape and Start (the menu), the pad's guide
  button, and the original's own menu keys (F10, the Alts; Y and N and F5 and F6 stay as they were). The keys by
  default (the user): the arrows, Space and D the abilities (named 1 and 2 in the menu), E, S, Left Ctrl and
  Right Ctrl the Viking before and after, Tab the inventory, P the pause; the gamepad the original's joystick.
  Remaster's own on what was free: Backspace or the left
  trigger rewinds, - and = or the right stick up and down zoom out and in, M or the right stick's press the
  minimap, F7 and F8 the quicksave (the sound and the screen's look are the menu's alone: the user; Vanilla
  rules, which have no menu, keep F12 for the sound). Nothing pauses on the pad (the left stick's
  press did, but it is pressed by accident while walking: the user; the menu's "give up level" opens the
  original's pause box). Gamepads through
  SDL's gamepad API, which maps every pad it knows (XInput, PlayStation, Switch, ...) onto one layout; several
  are read as one, as the original or-s its joystick into the keyboard. Vanilla rules don't change: the
  original's keys and joystick, and a keys file (`--keys`) if the player wants others.
- **Walking as fast as a stick is pushed** (built, 2026-11-25; the user's): a stick bound to left or right walks
  the Viking from a little past its middle, as fast as it is pushed: his top speed and his walk's animation in
  16ths (`World::pace`, part of the step's input like the keys), his walk's animation at the square root of
  that (`Sprite::pace`: a quarter of the speed, half the steps; the user: at the same pace he slid ahead of his
  legs). Pushed further while he walks at its speed, he is at the new speed at once (the user: it didn't snap
  in); from standing he speeds up as the original's run does (Erik's jump depends on it). The same stick steers
  him in the air no faster than he would walk so, and up or down it climbs a ladder as fast as it is pushed (a
  climb's step is its animation, so his hands and feet keep up), the pace taken every tick; a step the way the
  stick no longer goes ends at full pace (the user: else a slow step was sat out before the next). A stick's
  direction counts from the dead zone only where it is the way the stick is pushed most, else from half way as
  without: up a ladder held a little to the side doesn't step off it, walking a little up doesn't climb. A key, a button or the
  d-pad walks at full speed, as before. Erik pushed too little to reach a run can't charge, and jumps lower.
- **The quicksave is a file in the profile too** (`quicksave.bin`, ADR-0020): written when F7 keeps the play,
  read when the game starts. It begins with the state's version (ADR-0020) and ends with a sum over the world's
  bytes: a file of another version, or a damaged one, is said and left alone.
- **An unlimited active area** is not built: one player doesn't need it. Online co-op (ADR-0012) makes it every
  player's view together (the user, 2026-11-25; not everything running, which would lose the game's rhythm): an
  entity is where any view with its 16 pixels reaches, and an enemy turns at the edge of the views it is in.

### ADR-0012: Co-op online, for up to three, by rollback
Planned with the user on 2026-11-25; built in the parts ROADMAP.md lists for M9.
- **Online only** (the user: no local co-op, no split screen): up to three people, each on a machine of their own
  with a full screen and the view of the Viking they play or watch. Remaster rules only; Vanilla rules, and one
  player under Remaster rules, play as before.
- **Rollback, as GGPO does it** (the user's checklist; GGPO is MIT: its design is taken, the code is ours). Every
  machine runs the game and only inputs travel. A player's input that hasn't arrived is taken to be his last one
  (holding a direction is the common case); when it comes otherwise, the game goes back to the first step it got
  wrong and runs the steps from there again. A frame is a step, a tick of 50 ms: a window of 8 steps, and 0 to 2
  steps of input delay chosen from the round trip at the start, which makes rollbacks rare. The machines are kept
  together by their frame advantage: the one ahead waits a refresh now and then. Talked over and left (the user):
  lockstep, and a host streaming its state to clients that only draw it.
- **What rollback asks of the engine:** all of a step's state in a snapshot (the world, and the play's own: the
  pause box, the fades, the demo), slots never pointers (ADR-0018); what is shown and heard only reads the
  simulation, so a step run again without pictures changes the state as a shown one does, and a sound is keyed by
  its step and its entity, so a step run again doesn't play it twice; determinism (ADR-0003). Checked by the sync
  test, `lv-replay --rollback N`: every replay, each step run again after going back N steps, against its golden.
  A step that changes the scene runs on confirmed inputs only, so a fade or a level's loading is never undone.
- **Out of step:** the state of every 8th confirmed step is hashed and the hashes exchanged; on a difference the
  first step that differs is logged, and each machine writes its state and its log of inputs (in `logs/` beside the
  program) to be played offline.
- **Transport:** SDL3_net. TCP for the handshake (the protocol's and the state's versions, the build's commit, the
  game files' release, the names, the start); UDP for the inputs, every packet carrying each input the other side
  hasn't acknowledged; the TCP connection carries them instead if UDP doesn't get through (TCP_NODELAY and, on
  Linux, TCP_QUICKACK, which SDL3_net is patched to set: it sets neither). Through the host at first, so only the
  host needs a port that can be reached; direct, no relay server.
- **Only the host drives the game** (the user): the title, the passwords, the scenes, the pause box and giving up,
  game over, the cheat's keys. The others play their Viking.
- **A Viking has one player at most, and any number of viewers** (the user). The keys for the Viking before and
  after move a player to the next living Viking: he plays him if nobody does, else he joins the end of his line of
  viewers; the Viking he left goes to the first in his own line, or to nobody. One that nobody plays reads the keys
  of the last who did, as the original's Vikings not being played read its one keyboard. At the start the host
  plays Erik, the second to join Baleog, the third Olaf. A dead Viking's player and viewers move on to the next
  living one; all three dead is game over, as before.
- **What stops the game** (the user: gameplay shouldn't interrupt where that can be helped): only text shown on
  every screen, a level's start and end and the scenes, which the host's key answers. Hints, remarks and the people
  to talk to show only to the player whose Viking set them off, and his key answers them; his Viking stands
  meanwhile and the world goes on. The inventory is each player's own and doesn't stop the world: an item is handed
  to a Viking within reach (the original's 64 pixels) at once. The menu lies over the game going on. No rewind and
  no quicksave online.
- **A text box stays at its place in the level** (the user): the shared ones, the hints, the people talking, laid
  out in the view of the player they are for (the host's for the shared ones) and shown where they are in every
  player's view that has that place. One at a place of the screen (the password, a menu) is in the middle of each
  screen; said to one player alone, only on his.
- **The active area** is every player's view together (ADR-0011); a player's zoom is part of his input.
- **Names** under the Vikings their players play or watch: PLAYER 1 to 3 at first, at most 8 letters. Every
  player's latency in the menu. Hosting and joining from the command line first, then from the menu (a port, a
  level's password and a name; an address and a name), kept in the profile.
