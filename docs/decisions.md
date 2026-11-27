# Decisions

The decisions that shape the project. Each has a number (`ADR-00NN`), which other docs and a few code comments
use to point here.

## The project

### ADR-0001: Clean-room engine; users supply their own game data
- Blizzard owns The Lost Vikings. No original data, executables or anything derived from them is ever committed
  (`gamedata/` is ignored), and no text of the game appears in the repository: not in code, comments, tests or docs.
  The one exception is a handful of screenshots of the remake in `docs/screenshots/`, for the README.
- The reference release is the original DOS game. The game and `lv-export` accept only known file hashes
  ([`formats/releases.md`](formats/releases.md)).
- How the original behaves is learned by disassembling our own copy of `VIKINGS.EXE` and by running it in DOSBox
  (ADR-0016). `docs/original/` records rules, values and addresses as evidence, never decompiled or disassembled
  code, and our code is written from those write-ups.
- Other people's work: a project built from decompiled code, or under a licence that isn't permissive, may be
  read and run to compare, never copied, linked or shipped. CC0 material (RyanMallon's notes) may be used freely.

### ADR-0005: Rules and Sound are switches; the visuals go with the rules
- **Rules: Vanilla / Remaster**, chosen when the game is started (a box asks, or `--rules`) and fixed until it
  ends: nothing is switched while playing, so nothing has to be squeezed back into the original's limits.
  Anything that changes what the player can see or do belongs to Rules.
- **The visuals go with the rules.** Vanilla rules show the Classic picture (the CPU renderer); Remaster rules the
  Modern one (the GPU renderer: smooth movement, parallax, the effects, the screen's looks). Presentation only:
  the simulation doesn't know which renderer draws it.
- **Sound: Classic / Modern** (ADR-0015). Vanilla rules play the Classic sound only, the original's; under
  Remaster rules the menu chooses either, at any moment.
- **The screen's look** (Remaster rules): how the window shows the finished picture: as it is, as a CRT would
  (Hyllian's CRT shader), or with its stair-steps rounded off (Hyllian's xBR). A display setting chosen in the menu
  and kept in the profile (ADR-0011); `--shader` only for a dump. Shaders from others are taken only under a
  licence that fits the project's MIT.
- **The Modern visuals' effects** (Remaster rules): light, glow and particles in the game's own pixels (every
  light worked out per pixel of the picture, in steps of brightness; particles a pixel each in the game's fixed
  colours). A light is cast: it brightens what it falls on and the map's ground stops it. They answer to what the
  engine shows (the animation an entity was last started on, the map's tile types, the colours a level turns);
  the one thing the simulation keeps for them is which animation that was (`Sprite::started`), part of its state
  so a rewind or a quickload keeps it. Nothing for a blow or a hit landing.

### ADR-0017: The game's behaviour is written by hand, and checked by pixel match
- The original keeps its behaviour in bytecode scripts. The engine does not interpret them: every kind of entity
  is hand-written C++, read from the scripts' listings, so the code can be read, changed and extended (Remaster
  rules, co-op).
- **Pixel match** is the bar under Vanilla rules: on the same input, the same picture (indexed pixels and palette)
  and the same sound and music requests at every refresh. The insides are free.
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
  of snapshots), and online co-op by rollback (ADR-0012): a step run again from a snapshot, shown or not, must
  come out the same.

### ADR-0018: An entity is a class with explicit states
- One class per kind, derived from `Entity`, with plain data members and an enum of states: one state for each
  place the original's script waits. `update(World&)` is one tick's turn. No coroutines.
- The engine's services (tile tests, moving, gravity, creating entities, the camera) belong to `World`. A snapshot
  copies the world.
- **A kind keeps everything it remembers in one plain struct nested in its class**, `Saved`, as its member `s`
  (`s.timer`, `s.state`), with `LVR_SAVES(Base)` after it. `Entity` and `World` have their plain data the same
  way, in a struct they are derived from (`EntityState`, `WorldState`). That is what lets the game's state be
  written out without naming any field (ADR-0020): adding a field to a kind is adding it to its struct. A kind
  others are derived from names its member otherwise (`enemy`, `shot`, `wall`). Entities refer to each other by
  slot number, never by pointer. `scripts/kind_state.py`, in the checks, holds every kind to it, and `LVR_SAVES`
  asserts at compile time that a `Saved` is standard-layout and trivially copyable (no vectors or strings: a list
  is a fixed array with a count), asks for no more alignment than a 64-bit number, and is the only member its
  class adds.
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
  part whose length is another than this engine would write is refused.
- **One version stamp**, `kStateVersion`: the fields have no names, so what was written by an engine whose state
  was laid out otherwise can't be read by this one. It is the first thing in a quicksave's file, and what two
  machines tell each other before a snapshot is sent; another version is refused with a message. It is raised by
  hand when a `Saved`, `EntityState` or `WorldState` changes, and a test that pins the written size of four worlds
  says when that was forgotten.
- **That nothing is missing** is checked on the replays: `lv-replay --check` (`scripts/goldens.sh`) writes the
  world out and reads it back before every step of every replay, and the replays must still match their goldens
  (a replay that matches with `--plain`, the goldens alone, and not without it is state left out); that every kind
  keeps its state in its `Saved` is checked without playing (`scripts/kind_state.py`).
- A world is 22 to 40 KB written out: the sprite pieces never used, most of Remaster rules' 2048, are written as
  how many in a row (`Pieces::save`). What is sent to a player joining under way has its numbers in as few bytes
  as they need (`Widths::kVariable`: seven bits a byte, signed ones zigzagged).

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
  with GeneralUser GS (a file in `gamedata/`; the release archives carry a copy, the repository doesn't).
- The simulation asks for the same sounds either way. The two versions are different arrangements, so a switch
  starts the tune again.
- **Classic is the original, limits included**: at most four effects at a time, and an effect takes channels (and
  so notes) from the music while it sounds. Under Remaster rules its effects have a chip of their own, so the
  volumes can be set apart, and the fire jets' roar is at 60% of its loudness.
- **Modern is ours to shape:** its effects have a synthesizer of their own, so the music plays on untouched, up to
  seven sound at a time, the last notes of an effect ring until its sequence ends, and one effect ending doesn't
  cut another short. Where the General MIDI arrangement of an effect grates through a SoundFont, or is missing
  from it (the fire jets' roar: its General MIDI stream is empty), the effect is remade from the Classic one's
  notes on an instrument of our choosing (`kRemade` in `src/audio/player.cpp`). The synthesizer has 256 voices.
- **Modern's effects are placed by where they happen:** an effect an entity asks for is heard across the view from
  left to right by where the entity is (the view's edges 48 of 63 to a side: nothing on screen is all the way to
  one ear), and quieter the further outside the view it is, silent half a view away; one of the scene's, in the
  middle. The request says where it happened (`SoundCue::x`, `y`); the session works out the rest from the view as
  it plays it (`Session::placed`), and the sequencer pans the effect's channels as its notes start
  (`Sequencer::set_pan`). The music isn't placed, and the Classic sound never is.
- Not supported: PC speaker, OPL2, and the MT-32 (its emulator is LGPL and needs copyrighted ROMs).

## Building

### ADR-0002: C++23 with Clang, sanitizers, clang-tidy and clang-format
- C++23, Clang and libc++ (hardened in debug and sanitizer builds), CMake presets with Ninja: `debug`,
  `asan-ubsan` (the default for development and tests), `release`, `fuzz`, `coverage`, `ci`, `windows`.
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
| glaze | JSON (`lv-export`), reflection of plain structs (`archive.hpp`) | MIT |
| Catch2 v3 | tests | BSL-1.0 |

The SoundFont (GeneralUser GS 2.0.3) is data, not a dependency: loaded from `gamedata/` at run time. The release
archives carry a copy with its licence, which allows that; its author asks that his download files aren't linked
directly, so links go to his website.

### ADR-0010: Layout
- `src/` holds a folder per area, flat inside: `assets` (reading the original's files), `audio`, `image`, `input`,
  `log` (the log, ADR-0023), `lvr` (the engine and the game), `net` (co-op online), `replay` (replays and goldens),
  `exporter`. One file pair per subject, not per type. Every target is defined in `src/CMakeLists.txt`.
- `<dir>/tests/test_<name>.cpp` tests `<dir>/<name>.*`; each test file stands alone. Tests that need the user's
  game files are tagged `[real-data]` and skip without them.
- Only `image`, the transport, the GPU renderer, the programs' `main.cpp` files and `lv-export` use SDL.

### ADR-0013: Platforms and one executable
- Modern desktops: Linux and Windows (ADR-0021). Linux is Wayland only (SDL3 built without X11). macOS may come
  later.
- One self-contained executable: SDL3 and every dependency linked statically, the C++ runtime too in release
  builds; on Linux only glibc and what SDL loads at run time stay dynamic.
- `make release` builds both and packs each with the SoundFont and the notices: a `.tar.gz` for Linux, a `.zip`
  for Windows ([`development.md`](development.md), "Releases").

### ADR-0016: A patched DOSBox-X as the reference
- A DOSBox-X with a small control channel (run, breakpoints, memory, keys, screenshots, OPL capture) that a script
  drives: [`dosbox.md`](dosbox.md). It is GPL, a development tool only: built outside the game's build, never
  linked or shipped.
- It is what settles a doubt about the original: `tools/lockstep.py` plays the same input to it and to the engine.

### ADR-0021: The Windows build, cross-built with llvm-mingw
- Windows 10 and later on x86-64, built in the dev container: llvm-mingw, the same LLVM as ADR-0002's with libc++
  and mingw-w64's headers and libraries (pinned by release and checksum in `dev-env/Dockerfile`), the `windows`
  preset (`cmake/windows.cmake`) and `scripts/windows.sh`. Not MSVC: one compiler and one standard library for
  both platforms, and no Windows machine needed to build.
- One file, as ADR-0013 asks: `vikings-remaster.exe` needs only Windows' own DLLs and its C runtime (UCRT). It is a
  program of Windows' desktop, without a console: stdout and stderr with nowhere to go are sent to NUL
  (`std::print` throws when it can't write). Its manifest (`src/lvr/windows.manifest`) makes its strings of `char`
  UTF-8, so paths from SDL and `std::filesystem` are what they are on Linux.
- Only built: nothing of the Windows build runs in the container, and the tests and the replays are the Linux
  build's. The game's logic is in whole numbers (floating point only in what is shown, and in a stick's reading
  before it is a whole pace), so the Windows build plays as the Linux one does, and the same sources give the same
  build ID: the two play co-op together.
- The GPU renderer's shaders are SPIR-V only, so on Windows SDL_GPU draws with Vulkan, never Direct3D 12. DXIL
  shaders would add it, if Vulkan turns out to be missing too often.
- SDL3_net's patch has one more change, for mingw-w64: its `read()` and `write()` for sockets collide with
  `io.h`'s for files.

### ADR-0022: Start-up says what is missing
- In a window, what stops the game is shown in a box as well as on stderr, on every platform (on Wayland SDL shows
  it with zenity or kdialog). A dump, a tool's run, only writes to stderr.
- Without the game's files the box says where `DATA.DAT` and `VIKINGS.EXE` go; with files of an unknown release,
  what is wrong with them.
- The box that asks for the rules notes what Remaster rules would play without: the SNES image (no backdrops) and
  the SoundFont (the Classic sound only). SDL's boxes can't colour text, so the notes are plain text under a ⚠.
- A profile that can't be written, or a `settings.txt` that can't be read, is said in a box at the start; the game
  goes on without what it can't keep.

### ADR-0023: A log beside the program
- What the game does and what goes wrong is said as lines of a log (`src/log/`, `lvr::log`): plain C++, so every
  part may use it, `net` too. A line is made where it is said and handed to the log's own thread, which writes it:
  saying one never waits for the disk or a terminal. The log is for what happens now and then, not for every step.
- The game in a window starts it: a file of its own a run in `logs/` beside the program
  (`vikings-remaster-2026-11-27-210315.log`), and on stderr as well. Each line has the local time to the
  millisecond, its level (info, warn, error) and the function that said it, its class's name before it
  (`std::source_location`, the namespaces left out), and the levels as wide as each other so that what follows lines
  up: `[2026-11-27 21:03:15.042] [info]  [Places::host]: hosting on port 47230`. A file past 10 MB is followed by
  a new one, and the newest ten of the log's files are kept (a co-op game's log in the same folder is left alone).
  Not started (a dump, a tool's run, the tests), a warning or an error goes to stderr, the rest nowhere.
- A log may be shown to anyone (pasted into an issue), so it holds nothing of the player's but his name in the game:
  no address (the one a guest joins is written "the host"; machines are peer numbers) and no path in full (what is
  beside the game from its folder, the rest of the home folder from `~`). `log::hide` takes them out of every line.
- Up to 10,000 lines wait to be written; past that they are dropped and counted. A run that ends, by any return or
  error, writes every line it said; one that crashes may lose the last few.
- What may come again and again is throttled (`log::Throttle`, on the caller's own clock), and the next line let
  through says how many were held back.
- Where lines come from: the game's events from the step that is shown (`Session::run`), never from the simulation
  (`Play`, `World`), which rollback, the replays and the goldens' checks run again unseen; each move between places
  from `Places::go`; co-op from inside `net`, with the game's number and the step (the clock every machine shares);
  the timings from the window's loop and the start-up, where the clock is (the engine reads none, ADR-0003).
- Co-op says only what is worth looking into, as it happens: the input delay chosen and the round trips it was
  chosen by; UDP silent and the fast messages over TCP, and UDP back (throttled); the game held up for a player
  (once the box shows it), for how long, and the player let go after 10 s; a connection dropped after 10 s of
  silence; out of step at a step, and each resync. Every 10 s each connection says how it went: the round trips'
  p50, p90 and p99 (a ping every half second), which way what is sent both ways came first (UDP or TCP, in
  percent), and the bytes a second each way. A game's end says the rest: the resyncs, the holds and the longest,
  the rollbacks and the deepest.
- Not fmtlog or spdlog: each brings fmt, a second way of formatting beside `std::format`, and fmtlog has no rotation
  and isn't tested with Clang on Windows. The game says too little for their speed to matter.

## The Remaster rules and co-op

### ADR-0006: A Classic software picture and a GPU renderer
- **Classic** (`render.*`): an 8-bit indexed picture of 320 × 240 (a 176-line playfield over a 64-line HUD), drawn
  on the CPU from the simulation's state, the palette applied per picture, three pictures a tick with the
  original's integer sub-steps. Scaled up by whole numbers. It is what Vanilla rules show and what the goldens
  hold: it needs no graphics card and checks every replay in under a minute.
- **The GPU renderer** (`gpu.*`, SDL_GPU, Vulkan, shaders compiled by `glslc` at build time): what Remaster rules
  show. Every picture of a scene sits in one atlas of palette indices; a view is drawn a pixel for a pixel into a
  target of indices, and shown through a second pass that looks up the palette and scales. Drawn at the
  original's view its target is the Classic picture exactly: `lv-replay --check --gpu` plays the replays against
  the same goldens (about five times slower on a software driver, so it is not part of the routine checks). What
  it does beyond that is judged by eye.
- **Kept as lists, drawn when shown:** a picture for the window is kept as its rectangles of the atlas, not as
  pixels, and drawn when it is shown, as many pixels a pixel as it is shown at. That is what lets something stop
  between the original's pixels and be shown at the moment of the screen's own refresh.
- **Smooth movement:** the original moves a tick's distance in three whole-pixel shares (3, 2, 2 of 7). Shown
  smoothly, every entity's pieces and the camera go evenly over the tick, a third of a tick behind where the
  original's picture has them (so nothing has to be known of the next tick: input is as prompt as before). Only
  that split is evened out; a new frame, a flip, a jump are shown as the game makes them. The pieces an animation
  moves itself, the HUD, and pictures that aren't a tick's (a scene's start, the inventory, a menu, rewind) aren't
  moved.
- **The window:** the picture fills it with the 4:3 shape kept, in whole pixels where they fill 90% of it, else
  drawn the next whole number of times as large, a pixel of the window the mean of what it covers.
- **The screen's look** (ADR-0005): with one, everything shown (the picture, the HUD, the minimap, the rewind's bar
  and mark, the menu) is drawn into a texture the window's size in the game's whole pixels, and a last pass makes
  the look from those pixels, found on the grid they were drawn on. Never in a picture of a whole level (a
  development tool's) or the checks.
- **The crt look** (Hyllian's CRT shader, MIT, adapted: THIRD-PARTY-NOTICES.md; `gpu_hyllian.frag`): its main pass
  and its last pass's mask in one, with its preset's settings: each of the picture's rows a scanline, its beam
  wider where it is brighter, softened along the row, in linear light; an aperture grille's mask in the window's
  pixels. Not its colour grading (the game keeps its colours), glow or curvature. Zoomed out the scanlines stay a
  row of the game's pixels each; where a row is less than three of the window's pixels high a window's pixel takes
  the beam over the whole of it.
- **The xbr look** (Hyllian's xBR-lv2, MIT, adapted: THIRD-PARTY-NOTICES.md; `gpu_xbr.frag`): it rounds a
  picture's stair-steps off from each pixel's neighbours and blends their edges. The blending makes new colours,
  which can't be palette indices, so it is a last pass over the finished picture, which is then drawn in the
  game's whole pixels: what moves between ticks isn't moved (no smooth movement), and the tiles join up. One look
  at a time: this or crt.

### ADR-0011: Remaster rules
Only under Remaster rules (ADR-0005); Vanilla rules stay exactly the original's, and the replays prove it.
- **Zoom-out:** three steps, the original's view, one and a half times and twice it. The whole screen zooms as
  one, the HUD too: a zoomed view is as high as the screen (480 × 360, 640 × 480) and the HUD lies over the middle
  of its bottom. The camera keeps the Viking in the middle of what is above the HUD and goes 64 pixels past the
  level's bottom, so the HUD never hides the floor; where a Viking is low in a level there is then nothing of the
  level beside the HUD, and it is black. A change of view takes a third of a second: the world's view grows at
  once and the picture opens up, or the picture closes in and the view shrinks when it is there; a step asked for
  while one is shown follows it. A view is never larger than the level, and one less high than the screen is in
  the middle of what is above the HUD. A level is started with the zoom the player has. Only the 37 levels zoom.
- **The active area follows the zoomed view** (where entities exist: the view plus 16 pixels), and so does the
  edge the enemies turn at. Not the whole level: that keeps the game's rhythm, an enemy starting as it comes into
  view.
- **Text boxes** are part of the playfield and zoom with it. One at a speaker is placed by the speaker anywhere
  in the view; one at a fixed place (a password, a menu) is in the middle of the screen.
- **No limit on entities:** 512 slots against the original's 20, and a layer of 2,048 sprite pieces against its
  128. The layer keeps the original's three pools in their order, so what is drawn over what stays; each piece of
  the Vikings' and enemies' pool (256, against 16) has a frame buffer of its own.
- **A demo plays at the original's view:** the zoom is the player's in the levels he plays, not the demos'
  (`World::view_for`). A larger view takes in more of the level, its entities there at other times, and the
  recording, the original's play, would come out otherwise. Should one come out otherwise all the same, a demo
  whose Vikings are all gone ends as its recording's end does.
- **Zoom is not part of a replay.** Replays are Vanilla's.
- **Minimap with fog of war** (simple on purpose: a map to look something up on). A square of the level around the
  view, 64 cells a side, in the screen's bottom left corner, as high as the HUD. Each cell is coloured by what it
  is to the entities (ground, a platform, water, lava, spikes, electricity, a spring; a ladder dark with lighter
  rungs). Only what a view has shown is drawn, the rest is dark. On it the three Vikings, the one being played
  framed; no enemies or other entities, and no outline of the view. Its frame is in the original's pixels with two
  cut off each corner, as the rewind's bar's. It is drawn smoothly at every refresh: the map scrolls with the view
  by fractions of a pixel, and what the view shows now and where the Vikings are is exact. At the original's view
  the HUD moves right by a column of its stone tiles to make room (it slides aside, then the minimap fades in; the
  other way round when it is put away); zoomed out the smaller HUD is clear of the corner and stays in the middle.
  M puts it away (not Tab, which is the inventory). What was seen is the player's, not the simulation's:
  `Session` keeps it per level, and the profile keeps it. Only in the 37 levels. Every fade darkens it with the
  picture (`darken` in `render.hpp`): each picture carries its level's minimap and how bright it is. Its slide and
  fade, and the rewind's flashing mark, go by the time gone, not by the pictures shown.
- **Rewind:** while Backspace is held, time runs backwards at twice its speed; let go, the play goes on from where
  it is. Only in a level being played (one of the 37; not a demo, a menu, or the game-over scene), and never back
  past the level's start. A level has 10 seconds of it (199 steps), counted in the play gone back over (so 5
  seconds of holding the key), and gets them again only when a level is started, the same one again too; a
  quicksave brought back doesn't. The pictures between the ticks are there as they were going forwards: a step
  that is undone is run again from its snapshot with the keys it had, for its pictures, each with the refresh it
  was first shown for (so a backdrop that moves by itself goes back too). The sound of those seconds is heard
  backwards meanwhile, twice as fast, and the music then goes on from where it was. Two triangles flash in the
  screen's top right corner while it runs. The minimap is not taken back. The zoom is the player's, not the
  play's: a step brought back keeps the view the zoom has, and so does a quicksave brought back.
- **The rewind's bar**, along the minimap's bottom, shows what is left: in the original's pixels, each one colour,
  a little see-through; a white frame with two pixels cut off each corner, light grey inside from the left as far
  as there is rewind left, dark past that, and the rewind's mark (two triangles pointing left) in its middle, dark
  on the grey and grey on the dark. It is part of the minimap: there when it is, faded in and out with it, and put
  away with it (M).
- **A world can be copied**, which is what rewind and quicksave are: a snapshot is a copy of the `World` (each
  entity its own copy) with the level it belongs to. The rewind keeps one before every step. That a copy keeps
  everything is checked by the sync test (ADR-0012).
- **Quicksave**, only with the game's own cheat on (its password typed at the title, or `--cheat`): F7 keeps the
  play as it is, in a level; F8 brings it back from wherever the game is by then, in this run or a later one. One.
- **No autosave and no level select:** the passwords are how a level is reached, under both rules.
- **Shots stop at a slope:** the original's straight shots (the gunners', the troopers') stop at walls only and fly
  on through a slope's ground; zoomed out that is seen, so they burst there.
- **In front:** the game-over letters take their pieces from the front pool, and so do Baleog's arrows (the
  original's are behind the Vikings, and behind the hint boxes, the doors and the items).
- **Keys:** Enter answers the original's pause box and quit box with the word shown chosen, as F does; Escape takes
  the title's password back to its menu.
- **The player's profile:** a folder of what is the player's and not the simulation's, kept from one run to the
  next; Vanilla rules keep none. By default the folder `profiles` beside the game's program, where it is found
  without knowing where to look (not a folder somewhere in the user's home); or `--profile DIR`. In it
  `minimap.txt` (what the minimap has seen of each level, as runs of seen and unseen pixels: read at the start,
  written when a scene is loaded and when the game is closed), `settings.txt` (the settings and controls, written
  when the menu is closed and when the game is; a line a player edited that can't be understood is said and
  skipped) and `quicksave.bin` (written when F7 keeps the play; it begins with the state's version and ends with a
  sum over the world's bytes: a file of another version, or a damaged one, is said and left alone).
- **The menu:** Escape, or a gamepad's Start, in a level being played; elsewhere they stay the original's pause key
  (P pauses in a level). While it is open the game holds: no step runs, so the game never knows (replays, rewind,
  quicksave untouched); the music plays on, so the volumes can be heard as they change, and the world is shown as
  it is, a little darker as under the original's pause menu and quit box, with what the menu changes on it at
  once, the zoom too. Its pages: the options, the keyboard, the gamepad, giving up the level (the original's pause
  box) and quitting (the original's quit box); Escape resumes. The options: the sound (Classic or Modern), a volume
  from 0 to 10 each for the music and the effects (SOUND FX), the screen's look, the zoom, the minimap, the visual
  effects (VISUAL FX: the light, glow and particles). Drawn on the CPU in the original's pixels, in the game's own
  font, in a box with the rewind bar's frame but black and hardly see-through, and laid over the screen as it is
  shown (through the screen's look like the rest), its pixels as large as the game's but never smaller than at the
  zoom's middle step.
- **The volumes** are gains on the sound made: the players play at the full volume, as written, and the music and
  the effects are each as loud as the square of their step (half way a quarter, 12 dB down).
- **Controls:** what does each action, on the keyboard and on a gamepad, is the player's, set in the menu and kept
  in `settings.txt`; Remaster rules' own actions (rewind, zoom, minimap, the quicksave) are among them. On the
  keyboard a key an action: choosing a key puts it in place of the one the action had. On a gamepad any number
  (the d-pad and the stick both walk): choosing a button adds it. Either is taken from the action that had it. The
  gamepad's page binds only with a gamepad there, and says whether one is. Fixed: Escape and Start (the menu), the
  pad's guide button, and the original's own menu keys (F10, the Alts; Y and N and F5 and F6). The keys by
  default: the arrows, Space and D the abilities (named 1 and 2 in the menu), E, S, Left Ctrl and Right Ctrl the
  Viking before and after, Tab the inventory, P the pause; the gamepad the original's joystick. Remaster's own:
  Backspace or the left trigger rewinds, - and = (or the mouse wheel) or the right stick up and down zoom out and
  in, M or the right stick's press the minimap, F7 and F8 the quicksave. Nothing pauses on the pad (the left
  stick's press is too easily pressed while walking; GIVE UP LEVEL in the menu opens the original's pause box).
  Gamepads through SDL's gamepad API, which maps every pad it knows (XInput, PlayStation, Switch, ...) onto one
  layout; several are read as one, as the original or-s its joystick into the keyboard. Vanilla rules play the
  original's keys and joystick, and only those.
- **Walking as fast as a stick is pushed:** a stick bound to left or right walks the Viking from a little past its
  middle, as fast as it is pushed: his top speed in 16ths (`World::pace`, part of the step's input like the keys),
  his walk's animation at the square root of that (`Sprite::pace`), so his legs keep up. Pushed further while he
  walks at its speed, he is at the new speed at once; from standing he speeds up as the original's run does
  (Erik's jump depends on it). The same stick steers him in the air no faster than he would walk so, and up or down
  it climbs a ladder as fast as it is pushed, the pace taken every tick; a step the way the stick no longer goes
  ends at full pace. A stick's direction counts from the dead zone only where it is the way the stick is pushed
  most, else from half way: up a ladder held a little to the side doesn't step off it, walking a little up doesn't
  climb. A key, a button or the d-pad walks at full speed. Erik pushed too little to reach a run can't charge, and
  jumps lower.
- **Parallax:** with the player's SNES image beside the game's files (ADR-0014), the SNES version's backdrops show
  behind the levels and the village's scenes, scrolled slower than the map, where the DOS game shows black.
  Without it nothing changes.

### ADR-0012: Co-op online, for up to three, by rollback
- **Online only:** up to three people, each on a machine of their own with a full screen and the view of the
  Viking they play or watch; no local co-op, no split screen. Remaster rules only; Vanilla rules, and one player
  under Remaster rules, play as before.
- **Rollback, as GGPO does it** (GGPO is MIT: its design is taken, the code is ours). Every machine runs the game
  and only inputs travel. A player's input that hasn't arrived is taken to be his last one (holding a direction is
  the common case); when it comes otherwise, the game goes back to the first step it got wrong and runs the steps
  from there again. A frame is a step, a tick of 50 ms: a window of 8 steps, and 0 to 2 steps of input delay
  chosen from the round trip at the start, which makes rollbacks rare. The machines are kept together by their
  frame advantage: the one ahead waits a refresh now and then.
- **What rollback asks of the engine:** all of a step's state in a snapshot (the world, and the play's own: the
  pause box, the fades, the demo), slots never pointers (ADR-0018); what is shown and heard only reads the
  simulation, so a step run again without pictures changes the state as a shown one does, and a sound is keyed by
  its step and its entity, so a step run again doesn't play it twice; determinism (ADR-0003). Checked by the sync
  test, part of `lv-replay --check` (`scripts/goldens.sh`): every replay, before each step the last 8 run again
  from the snapshot before them, against its golden. A step that changes the scene runs on confirmed inputs only,
  so a fade or a level's loading is never undone.
- **Out of step, put right by the host:** the state of every 8th confirmed step is hashed and each machine's hashes
  go to the host, who compares them with his own (a guest has only the host's). The host leads: a guest whose hash
  differs is sent the host's state before a step every input is known for (`net::Resync`, the bytes a mid-game join
  gets), takes it as rollback brings a state back (his level kept, what is shown and heard going on; only a state of
  another level loads it), runs the steps since again unseen with the inputs he has, and plays on; nobody else is
  touched. His hashes carry how many states he has taken, so the host doesn't take one of a state before. Put right
  3 times within 30 seconds, his game jumps too often: a box under the top one says OUT OF STEP WITH HOST while
  that lasts (a mismatch is a fault of the game, never the network's: late inputs are rollback's, a link held up the
  box above); he stays or leaves from the menu. The first step that differs is logged, and each machine
  that finds it writes its log of inputs and hashes (in `logs/` beside the program, once a game) to be played
  offline (`lv-replay --coop LOG`).
- **Held up, 10 seconds at most:** when a step can't run because someone's inputs haven't come (the window used
  up), the game waits on every screen, the box at the top naming whom it waits for and the seconds left. After 10
  seconds without a step the host lets that player go (his Viking passed on, as when one leaves; he is told, and is
  back at his title with why; he can join again) and the game goes on; a guest goes when the host's game hasn't said
  anything for those 10 seconds (it is stuck), and in any case after 15 (the host didn't let the one go). A machine
  that only runs slowly holds nobody up: time sync slows the others now and then, never stops them.
- **Transport:** SDL3_net. TCP for the handshake (the protocol's and the state's versions, the build's ID: a hash
  of the game's sources, worked out at every build; the game files' release, the names, the start). The game's own
  messages go by UDP and TCP both, the same bytes, and the first copy to come wins: every Inputs packet carries
  each input the other side hasn't acknowledged, so a dropped, reordered or doubled datagram changes nothing, and
  TCP is there behind it (TCP_NODELAY and, on Linux, TCP_QUICKACK, which SDL3_net is patched to set). What one
  machine says fast to another in a pass goes as one message (`net::Bundle`), numbered per peer and way, the same
  number on both copies; the transport takes one only if it is newer than the last it took from that peer. A peer
  silent for 10 s is gone though its socket wasn't closed. The host takes a player's inputs and hashes from his
  own connection only. Who is there when is the host's to decide: the others count a place away only as far as he
  has said it is decided (`net::Decided`, sent every pass), so word of an arrival coming late stalls a moment,
  never puts a machine out of step. Everything goes through the host, so only the host needs a port that can be
  reached; no relay server.
- **Only the host drives the game:** the title, the passwords, the scenes, the pause box and giving up, game over,
  the cheat's keys. The others play their Viking.
- **A Viking has one player at most, and any number of viewers.** The keys for the Viking before and after move a
  player to the next living Viking: he plays him if nobody does, else he joins the end of his line of viewers; the
  Viking he left goes to the first in his own line, or to nobody. One that nobody plays reads the keys of the last
  who did, as the original's Vikings not being played read its one keyboard. At the start the host plays Erik, the
  second to join Baleog, the third Olaf. A dead Viking's player and viewers move on to the next living one; all
  three dead is game over, as before.
- **What stops the game** (as little as can be): only text shown on every screen, a level's start and end and the
  scenes, which the host's key answers. Hints, remarks and the people to talk to show only to the player whose
  Viking set them off, and his key answers them; his Viking stands meanwhile and the world goes on. The inventory
  is each player's own and doesn't stop the world: an item is handed to a Viking within reach (the original's 64
  pixels) at once. The menu lies over the game going on, darkening it. No rewind (and so no rewind bar) and no
  quicksave online.
- **A text box stays at its place in the level:** laid out in the view of the player it is for (the host's for the
  shared ones) and shown where it is in every player's view that has that place. One at a place of the screen (the
  password, a menu) is in the middle of each screen; said to one player alone, only on his.
- **The active area** is every player's view together: an entity is where any view with its 16 pixels reaches,
  and an enemy turns at the edge of the views it is in. A player's zoom is part of his input.
- **Names:** under the portrait of each Viking, the name of the player who plays him, in the HUD; above it, grey,
  those watching him in their line's order (A/B). PLAYER 1 to 3 by default, at most 8 letters. The menu's first
  page lists every player with two round trips to the host, the same on every screen: the network's (answered at
  once by the transport's own thread) and the game's (an Echo answered by the other game's loop, once a refresh).
- **Hosting and joining** are in the title's own menu, under Remaster rules offline: HOST GAME, JOIN GAME and
  OPTIONS between PASSWORD and QUIT TO DOS. HOST GAME asks for a name, a port and a level's password; JOIN GAME for
  a name and the host's IP:PORT; each is kept in the profile. Joining holds the page until it is made or fails.
  Either leads into the **lobby**, over the title: who is there and their round trips, and what the game starts
  with: no password a new game from the intro (its recording played as the title's NEW GAME plays it, only the
  first player's keys meanwhile), or the level a password names. The host types the password there (the others
  see what it names as he types) and starts the game when he likes: START GAME counts down 3, 2, 1, a tick each,
  everyone told, and is CANCEL START meanwhile; anyone can leave. In the game, the host's menu has RETURN TO LOBBY:
  everyone back in the lobby with the same connections, for another level or a new game. Each game has a number
  its messages carry, so what is late of the one before isn't taken into the next. When a game online ends for
  this machine (left, let go, or the host gone), the game offline is back at its title; why, if it wasn't the player's
  wish, is said on the page he went from.
- **Joining a game under way, and coming back.** An online world has a place for each of the three players there
  can be, those nobody has taken absent (`Player::here`). One who says hello while the game goes on is given a free
  place, the game's state before a step every machine has every input for (`Session::state_bytes`), and the step
  he comes at: one no machine can have confirmed yet (`net::Joining`); the others are told (`net::Arrive`), and
  each machine runs again whatever it ran with him away from then on (`Rollback::arrive`). He comes in 16 steps on
  (he loads the level meanwhile, so nobody waits for him), runs the steps before then at once and unseen, and fades
  in. His first input is his arrival in the world (`World::player_arrives`): onto a Viking nobody plays, else into
  the line of the first living one. One who left comes back the same way.
- **How it is built:** a module for each place ([`../GLOSSARY.md`](../GLOSSARY.md): Offline, Connecting, Lobby,
  Online; `src/lvr/front.*`), the moves between them in one spot (`Places`), the window's loop playing one place at
  a time and its input a module of its own (`src/lvr/handler.*`). The protocol in `src/net` (`Connection`, `Lobby`
  for the host and for a guest, `Match`), knowing the game only as `net::Game` and the network only as
  `net::Transport`, each with two adapters: the real game and a toy game, SDL3_net and a simulated network; so the
  protocol is tested on the toy game without sockets or the game's files, and the real game's online tests are few
  (a walk through every move, joining under way).
