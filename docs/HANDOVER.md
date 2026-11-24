# Handover: where the project stands

For whoever picks the work up next, person or session. How the game is built: [`architecture.md`](architecture.md);
why: [`decisions.md`](decisions.md); what is ahead: [`ROADMAP.md`](ROADMAP.md).

## Where it stands

- **The whole game plays** under Vanilla rules with Classic visuals: all 37 levels and every scene, each kind of
  entity written by hand (`src/lvr/`), with Classic and Modern sound.
- **It matches the original.** At every refresh of every replay the engine shows the picture and asks for the
  sounds that the original's own bytecode did (the goldens in `replays/`), and `tools/lockstep.py` compares it
  with the original in DOSBox directly.
- **It is published:** MIT, at <https://github.com/sytsevg/the-lost-vikings-remastered>. History is kept: new
  work is new commits; the user pushes.
- **M7, the Remaster rules, is built** (`--rules remaster`): the GPU renderer, zoom-out, no limit on entities,
  a minimap, rewind, a quicksave, a profile on disk, and the game's state written out and read back. What each
  is and where it lives is in the next section.
- **Next is M8, the visuals**, before co-op (M9). Nothing of it is started. "Starting M8" below says what the
  code has for it and what has to be decided first.

## What the Remaster rules have

The decisions are in [`decisions.md`](decisions.md) (ADR-0005, ADR-0006, ADR-0011, ADR-0018, ADR-0020). All of it
only under Remaster rules; Vanilla rules must stay exactly what they are, and the replays are what proves that.

- **The Rules switch:** `--rules vanilla|remaster`, for the whole run (`World::rules`, set as a world is made). It
  is never changed while playing.
- **The GPU renderer** (`src/lvr/gpu.*`, SDL_GPU on Vulkan, shaders `gpu_*.vert` and `gpu_*.frag` compiled by
  `glslc` into the build): what a Remaster window shows. Vanilla rules and every check use the Classic renderer
  (`render.*`), which stays. How the GPU one works is under "Starting M8".
- **Zoom-out:** `-` and `=`, or the mouse wheel; the original's view, one and a half times and twice it. The view
  is a value of the camera (`view_width`, `view_height`), and everything that was 320 × 176 reads it. `Session`
  owns the zoom and its transition; `World::set_view` changes the world's view. A zoomed view is as high as the
  screen with the HUD over its bottom; the camera goes 64 pixels past the level's bottom so the HUD never hides
  the floor (`Camera::covered`).
- **The text layer** is as large as the view; a box at a speaker is placed in it as it is, one at a fixed cell in
  its middle (`Line::by_point`).
- **512 entity slots and 2,048 sprite pieces** (`World::set_rules`; `kSlots` and `kOriginalPieces` are Vanilla's
  20 and 128). The layer's size and its three pools are a `PieceLayout`.
- **The minimap** (`minimap.*`, M): a picture of the whole level from the map's tile types, with how much of it a
  view has shown in its alpha (`Session::seen_`, per level and per pixel of the minimap, kept in the profile).
  The GPU renderer shows a square of it around the view in the bottom left corner (`gpu_over.frag`): it adds what
  the view shows now and the Vikings (`Screen::level_x`, `Screen::vikings`) at every refresh, and moves the HUD
  right for it.
- **Rewind** (`Session`, Backspace): `World` can be copied (its slots are `OwnedEntity`, whose copy is the
  entity's clone), and `Play::Snapshot` is a world with its level. `Session` keeps one before every step in a
  level with the step's keys, 199 of them (10 seconds), and the last 10 seconds of the sound as it was heard,
  for playing backwards. Going back undoes two steps a tick and runs each again from its snapshot for the
  pictures in between (`Session::back_to`). Two triangles flash meanwhile (`gpu_mark.frag`).
- **State that can be written out** (`archive.hpp`): every kind's own state is the plain struct `Saved` nested in
  it (member `s`), `Entity`'s and `World`'s are `EntityState` and `WorldState`. `World::save` and `World::load`
  write and read all of it, fixed-width and field by field, with `kStateVersion` to tell versions apart.
- **Quicksave** (F7 and F8, only with the game's cheat on: its password typed at the title, or `--cheat`): that
  written world as a file.
- **The profile** (`profile.*`): the folder `profiles` beside the game's program, or `--profile DIR`. In it
  `minimap.txt` and `quicksave.bin`. What the game writes goes there, not into the user's home.

## Starting M8: the visuals

The list is in [`ROADMAP.md`](ROADMAP.md): smooth movement between ticks, parallax with the SNES backdrops,
events in animations and effects on them, lights and shaders, art packs, sound placed by position. What the code
has for it:

- **How a picture gets to the window today.** A step of the game (`Session::step`) is a tick: three pictures,
  each a `View` (the camera, the sprites' pieces in order, the text layer, the HUD, the palette) made by
  `view_of` and drawn at once. The Classic renderer draws it into a `Screen` of palette indices. The GPU
  renderer (`GpuRenderer::keep`) draws the same `View` into a texture of indices on the card and the `Screen`
  only says which one (`Screen::kept`, of the last 512) with the palette; the window shows one kept picture a
  refresh (`GpuRenderer::show`: palette lookup and scaling in `gpu_show.frag`, then the minimap and the
  rewind's mark over it). The display's rate is the original's, 59.7 a second (`kDisplayHz`); the window's loop
  in `main.cpp` shows a picture for every refresh that is due.
- **The atlas.** Everything a scene is drawn from is in one texture of palette indices: the shared pictures
  (font, HUD), the scene's (tileset, the tiles' masks, its full pictures), and the sprites' pieces, each put
  there the first time it is shown and found again by a hash of its pixels (`GpuRenderer::State::place_pieces`).
  A picture is a list of rectangles of the atlas (`Quad`), drawn in the Classic renderer's order.
- **What smooth movement runs into.** A kept picture is already drawn: there is nothing left to move. Movement
  between ticks at the screen's own rate needs the window to draw from what a `View` describes, at the time it
  is shown, between two of them; and for that a sprite's piece has to be the same piece in both (today a `View`
  is a list with no names in it, and it points into data that is gone after the step: the world's map, the
  painter's pictures). A piece's place in the layer (its number) is the natural name. The game's own three
  pictures a tick already move sprites and camera in thirds (`share_of_move`).
- **What parallax has.** The SNES version's backdrops are decoded (`src/assets/snes.*`,
  [`formats/snes.md`](formats/snes.md); `lv-export assets` writes all 37) and nothing draws them. In the DOS
  game the background is part of the level's tiles, so drawing something behind it needs to know which tiles are
  background: the roadmap's note on the collision map (free cells are background) is the plan. The backdrops
  have colours of their own, outside the level's palette of 256.
- **What effects have.** Animations are the original's programs, played as they are (`sprites.*`); a program
  reports only the sounds it asks for. The roadmap's "events" (a frame shown, a sound asked for, an end), named
  beside the animations in `animations.hpp`, don't exist yet.
- **Art packs** need a piece named by the frame it shows (which set, which frame, which colours), not by its
  pixels' hash.
- **Visuals as a switch.** ADR-0005 has Visuals (Classic, Modern) as a switch of its own, changed at any moment.
  Today there is no such switch: the GPU renderer comes with Remaster rules, and it shows the Classic look. Whether
  Modern visuals are a switch within Remaster rules, or their own, is to be decided before the first of them is
  built.
- **How visuals are checked.** At the original's view the GPU renderer must still make the Classic picture
  (`lv-replay --check --gpu`; one or two replays are enough for most changes, all of them take five minutes on
  the container's software driver). Everything beyond that has no golden: under Remaster rules
  `vikings-remaster --dump STEPS FILE.png --zoom BY AT_STEP` writes what the window would show (three times the
  size) for looking at in the container, and the user judges motion on the host. Timings in the renderer are
  counted in pictures (a zoom change takes 20, a third of a second).

Decide with the user before building: the order of the M8 items, the Visuals switch, and whether a visual that
changes what is seen where (a backdrop, a light) may differ from the original's picture at the original's view,
which today's check forbids.

## Open

- **Co-op and the active area:** with several views of one world, either the active area is all the views
  together or everything runs (ADR-0011). Not decided.
- **Co-op's snapshots.** What is written of a world (ADR-0020) is all of it, 25 to 90 KB. Sent over a network
  many times a second it wants trimming: the sprite layer's free pieces (most of a Remaster world's size), the
  level's own data that both sides have, and only what changed since the last one.
- **Input per player** was planned from the start (ADR-0012) but the engine takes one keyboard word.
- **Not checked by anything but the eye:** whatever the GPU renderer shows beyond the original's view, and
  whether a level still plays well zoomed out (enemies run for longer and further; nothing resets as often).
- **The GPU renderer has run on one graphics card** (the user's) and the container's software driver. Vulkan
  only.
- Written from the script's listing and never compared with the original: that Olaf's shield stays, put away,
  when Olaf himself is gone and the cheat is on. And whatever else no replay reaches (below;
  [`formats/unknowns.md`](formats/unknowns.md), section 5).
- One reading of the original that the goldens carry was checked only indirectly: a level loaded again reloads
  its palette. (F5 in level 1 loads the same level again and the pictures match; the case it was first written
  for, an exit reached with a Viking gone, was not tried.)
- The live sound (the sound card pulling, `Session::render_live`) was tested by ear on the user's machine only;
  no automated test opens a device.
- Screenshots for the README: wanted, undecided. If added, hosted outside the repository.

## The layout

| Where | What |
|---|---|
| `src/assets/` | reading the original's files: `DATA.DAT`, levels, graphics, texts, object databases and their scripts |
| `src/audio/`, `src/image/`, `src/input/` | sound (OPL3 and General MIDI), PNG, keys and gamepad |
| `src/lvr/` | the engine and the game (`vikings-remaster`); [`src/lvr/README.md`](../src/lvr/README.md) names every file |
| `src/replay/` | replays and goldens, and `lv-replay`, which plays one against the other |
| `src/exporter/` | `lv-export`, a development tool: the game's data as PNG and JSON, script listings |
| `replays/` | the replays with their goldens ([`replays/README.md`](../replays/README.md)) |
| `scripts/` | the checks: `check.sh`, `goldens.sh`, `saves.sh`, coverage, `replay_cover.sh`, and the small checkers `check.sh` runs |
| `tools/` | research tools: the scriptable DOSBox (`dosbox.py`, `lockstep.py`), disassemblers, the replay maker |
| `docs/original/`, `docs/formats/` | our write-ups of the original's engine and file formats |

## How the work is done

- **Before a commit that touches the engine:** `./scripts/goldens.sh` (every replay against its golden, under a
  minute) and `./scripts/check.sh` (format, constants defined once, every kind's state in its `Saved`,
  ASan/UBSan build with lint, unit tests, coverage of at least 80%). After a change that is only formatting,
  comments or docs neither needs running again. On the host, `./run-dev-env.sh test` does both in the container.
- **After adding state to `World` or to a kind:** `./scripts/saves.sh` (every replay with the world written out
  and read back before every step, about a minute), and raise `kStateVersion` (a test in `test_play.cpp` fails
  until its sizes and version are brought up to date).
- **After a change to how the GPU renderer draws:** `lv-replay --check --gpu` on a replay or two.
- **What only the eye can judge is tried before it is committed.** Since M7 the user tries a change on the host
  first and says when to commit; stills for them go into `gamedata/previews/` (not in the repository).
- **The game itself runs on the host**, not in the container (no display or sound there):
  `./build/release/bin/vikings-remaster --slot 0 --sound modern`, with `--rules remaster` for the Remaster rules.
  What a Remaster window would show can be written as a picture in the container (`--dump`, with `--zoom`).
  Rebuild the release preset before asking the user to try something: `goldens.sh` only builds the replay tool.
- **A golden is never recorded again to make a difference go away.** There is no oracle left to record from: a
  difference is a change of behaviour until shown otherwise. `lv-replay` writes what the engine shows at the
  first refresh that differs; run the commit before the change the same way to see what it showed before.
- **Something new or in doubt about the original:** `tools/lockstep.py` plays the same input to the original in
  DOSBox and to the engine and compares the pictures ([`dosbox.md`](dosbox.md)). It starts at the first logo;
  with the cheat's password typed, F6 goes on a level at a time, and `--place` puts the Vikings where they are
  needed. A run worth keeping becomes a replay.
- **A fault of the original is not copied.** When a comparison turns up an accident of the original (a stray
  write, a glitch pixel), trace it far enough to be sure, write it into [`quirks.md`](quirks.md) as "not kept", and
  leave it out.
- **Classic is the original; Modern is ours.** The Classic sound keeps the original's limits and faults. The
  Modern sound is shaped by ear: its effects have their own synthesizer, and an effect that grates is remade in
  the `kRemade` table (`src/audio/player.cpp`). The user judges sound; it can't be judged from here.
- **Nothing of the game goes into the repository:** no data, art, text or decompiled code, in code, comments,
  tests or docs. Passwords as test input are the one exception the user allowed.
- **What the replays don't reach** is measured by `./scripts/replay_coverage.sh` (about 82% of the engine's lines
  are reached). The unit tests in `src/lvr/tests/` guard much of the rest. The last level's fight and the cranes
  are the thinnest.
- **Conventions** are in [`development.md`](development.md): hex only for what points into the original's files
  or defines a bit, decimal and `px()` for values; 16-bit arithmetic wraps on purpose.
- **Writing a kind:** [`architecture.md`](architecture.md), "Entities and kinds".

## Known differences from the original

Two, both meant ([`quirks.md`](quirks.md), "Drawing"): the game-over lightning that the original never erases, and
a stray white dot in Erik's portrait when he is gone. `tools/lockstep.py` reports the second as one pixel at
(46, 197) whenever Erik is gone in a game begun from the title's first choice (not by password).
