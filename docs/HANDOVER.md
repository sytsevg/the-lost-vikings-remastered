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
- **M8, the visuals**, is under way, before co-op (M9): smooth movement and parallax are built (below, "M8").

## What the Remaster rules have

The decisions are in [`decisions.md`](decisions.md) (ADR-0005, ADR-0006, ADR-0011, ADR-0018, ADR-0020). All of it
only under Remaster rules; Vanilla rules must stay exactly what they are, and the replays are what proves that.

- **The Rules switch:** `--rules vanilla|remaster`, for the whole run (`World::rules`, set as a world is made). It
  is never changed while playing.
- **The GPU renderer** (`src/lvr/gpu.*`, SDL_GPU on Vulkan, shaders `gpu_*.vert` and `gpu_*.frag` compiled by
  `glslc` into the build): what a Remaster window shows. Vanilla rules and every check use the Classic renderer
  (`render.*`), which stays. How the GPU one works is under "M8".
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

## M8: the visuals

Decided with the user (2026-11-24): the visuals go with the rules, chosen at launch (ADR-0005): Vanilla rules
show the Classic picture, Remaster rules the Modern one. At the original's view under Vanilla rules the GPU
renderer must still make the Classic picture; under Remaster rules a visual may differ. Smooth movement first.
The rest of the list is in [`ROADMAP.md`](ROADMAP.md): events in animations and effects on them, lights and
shaders, art packs, sound placed by position.

- **The jumble at the top left (fixed, 2026-11-24).** Zooming out while the world holds (every level opens with
  its password box, which waits for a key) created what the larger view showed at once; their first turn, where
  their animations put their pieces in place, only comes with the next tick. Meanwhile their pieces were drawn
  where the pieces' last owners had left them, and the Remaster layer's back pool (from piece 512) is new at the
  level's start: at (0, 0), stacked in the level's corner. `World::set_view` now leaves the creating to the next
  tick's start (`view_to_fill`, saved: kStateVersion 2), as the original creates what comes into view: right
  before its first turn. While the world holds, the newly shown part has no entities yet.
- **Smooth movement (built, 2026-11-24).** The GPU renderer keeps a picture as its list of
  atlas rectangles and draws it when it is shown, as large as it is shown (`GpuRenderer::keep`, `show`), with
  every entity's pieces and the camera moved evenly over the tick instead of in the original's three
  whole-pixel shares, a third of a tick behind (`Motion` in `render.hpp`, ADR-0006). The window's loop passes
  how far into its refresh each show is, at the screen's own rate.
- **How a picture gets to the window.** A step of the game (`Session::step`) is a tick: three pictures, each a
  `View` (the camera, the sprites' pieces in order, the text layer, the HUD, the palette) made by `view_of`. The
  Classic renderer draws it into a `Screen` of palette indices at once. The GPU renderer keeps its list and
  the `Screen` only says which one (`Screen::kept`, of the last 512), with the palette and the `Motion`; the
  window shows one a refresh of its own (`gpu_show.frag` looks up the palette and scales, then the minimap and
  the rewind's mark go over it).
- **The atlas** has pages (`GpuRenderer::State::Page`): the shared pictures, the scene's (tileset, the tiles'
  masks, its full pictures) and the sprites' pieces, each put there the first time it is shown and found again
  by a hash of its pixels. A kept picture is drawn from one page; a page is used again only when no kept
  picture is drawn from it.
- **What effects have.** Animations are the original's programs, played as they are (`sprites.*`); a program
  reports only the sounds it asks for. The roadmap's "events" (a frame shown, a sound asked for, an end), named
  beside the animations in `animations.hpp`, don't exist yet.
- **Art packs** need a piece named by the frame it shows (which set, which frame, which colours), not by its
  pixels' hash.
- **How visuals are checked.** `lv-replay --check --gpu` (one or two replays are enough for most changes, all of
  them take five minutes on the container's software driver) draws the kept list at the original's size with
  nothing moved: it must be the Classic picture. Everything beyond that has no golden: under Remaster rules
  `vikings-remaster --dump STEPS FILE.png` writes what the window would show (three times the size) for looking
  at in the container; `--zoom BY AT_STEP` zooms out, `--hold KEY` holds a key, `--tap KEY AT_STEP` presses one
  (a level's password box waits for F), and `--moments N` writes the last step's pictures at N moments of their
  refresh each. The user judges motion on the host. Timings in the renderer are counted in pictures (a zoom
  change takes 20, a third of a second).

### Parallax (built, 2026-11-24)

The SNES version has a **backdrop** behind every level's map, scrolled slower than it, where the DOS game shows
black. Under Remaster rules, with the user's SNES image (`*.sfc`) in `gamedata/original/` beside `DATA.DAT`
(ADR-0014), the game shows it; without the image (a note on stderr) or under Vanilla rules nothing changes.
- **Where:** a pixel of the map in colour 0 of one of the palette rows 0-7 (`shows_backdrop`, `backdrop.hpp`).
  The DOS palette's rows 0-7 are the SNES level's colours (all 37 levels compared), and a row's colour 0 is where
  a SNES tile shows what is behind it; the DOS game blacks those colours out. The backdrop's own pixels go in as
  they are, with the level's palette, so they fade and turn with it. Sprites and text never use those colours (all
  sprite sets checked), and the HUD's place is left alone. Other black colours stay black: they are black on the
  console too.
- **Checked against the console's own picture:** `lv-export assets` writes each level and scene with its backdrop
  (`snes/NN-level.png`, `snes/<scene>-scene.png`, each 320 x 176 screen with the camera there). Drawn the same
  way from the SNES level's own map, 32 of the 37 levels and the village are the same pixel for pixel; levels 21,
  24, 25, 33 differ in less than 1% (their maps differ a little), 28 is a column wider on DOS. Stills of both and
  camera pans for levels 5-7, 12, 26 and 27: `gamedata/previews/parallax/`.
- **How it moves** (`backdrop_offset`, from the SNES code, [`formats/snes.md`](formats/snes.md)): the camera
  without the shake times the level's speed in 256ths, repeated past its size; the spaceship's stars move by
  themselves, 5 pixels a refresh across (the DOS and SNES refreshes are 59.7 and 60.1 a second). Shown, it is
  where the smooth camera is at that moment, and the stars at the refresh plus `between`. `Screen::refresh`
  counts the refreshes the session has handed out pictures for (a picture shown again counts again), so the stars
  keep their pace in a text box's wait; they don't go back while the play rewinds.
- **Where it lives:** `load_slot` takes the image (`Session` opens it under Remaster rules and gives it to
  `Play`) and puts the backdrop into the scene's `SceneGraphics::backdrop`; the GPU renderer puts it on the atlas
  page with the scene's tiles, a kept picture records where it is and where the map is, and the show pass
  (`gpu_show.frag`) looks it up where the picture has such a colour inside the map. Past the map's edges (a view
  larger than a small level) it stays black. The Classic renderer and `GpuRenderer::draw` (the checks) never
  draw it.
- **Scenes:** the village's three scenes get the SNES sky and mountains (the DOS village leaves its sky in colour
  0). The ship and the game-over scene have backdrops too, but the DOS pictures cover every such pixel (sprites
  over the ship's few, none at all in game over), so nothing shows there.

## Open

- **A scene's fade-out shows no entities** (both rules; the user, 2026-11-24): when a level ends, the Vikings
  and every other entity are gone before the picture darkens. `Play::change_scene` runs the original's
  game-over pass first (every entity's turn as on leaving the active area: each is removed and its pieces let
  go) and then draws each of the 70 fade pictures again from that emptied world. The original's fade (`0FA0`)
  only darkens the palette and flips its two pages, which still hold the last pictures drawn, so it keeps
  showing everyone. A fix: the fade drawn from the picture before the pass. It changes the goldens of every
  replay that changes scene (the goldens hold our renderer's picture of the original's state); not checked in
  DOSBox (the user dropped that).
- **Frames now and then a refresh or more late** in the window (the user's AMD Radeon 890M, RADV), mostly noticed
  while moving. `--slow-frames` shows the time is spent waiting for the window's next picture (the card or the
  compositor holding frames), not in the game's step, its drawing, or its uploads; it was there before smooth
  movement too, and mailbox presenting or a third frame in flight didn't change it. Likely the system (the
  GPU's power management); not confirmed (vkcube, a fixed clock).
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
