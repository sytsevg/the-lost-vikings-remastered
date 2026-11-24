# Handover: where the project stands

For whoever picks the work up next, person or session. How the game is built: [`architecture.md`](architecture.md);
why: [`decisions.md`](decisions.md); what is ahead: [`ROADMAP.md`](ROADMAP.md).

## Where it stands

- **The whole game plays** under Vanilla rules with Classic visuals: all 37 levels and every scene, each kind of
  entity written by hand (`src/lvr/`), with Classic and Modern sound.
- **It matches the original.** At every refresh of every replay the engine shows the picture and asks for the
  sounds that the original's own bytecode did (the goldens in `replays/`), and `tools/lockstep.py` compares it
  with the original in DOSBox directly.
- **It is published:** one initial commit on `main`, MIT, at
  <https://github.com/sytsevg/the-lost-vikings-remastered>. From here on, history is kept: new work is new
  commits.
- **M7, the Remaster rules, is built** but for what waits on a profile on disk: the Rules switch, the GPU
  renderer, zoom-out, no limit on entities, the minimap, the rewind and a quicksave. The details are below.

## M7: where it stands

The list and the decisions are in [`ROADMAP.md`](ROADMAP.md) and [`decisions.md`](decisions.md) (ADR-0005,
ADR-0006, ADR-0011). All of it only under Remaster rules (first called the QoL rules); Vanilla rules must stay
exactly what they are, and the replays are what proves that.

Built:

- **The Rules switch:** `--rules vanilla|remaster`, for the whole run (`World::rules`, set as a world is made). It
  is never changed while playing.
- **The GPU renderer** (`src/lvr/gpu.*`): what a Remaster window shows. At the original's view it makes the
  Classic picture exactly (`lv-replay --gpu`, against the same goldens). The Classic renderer stays for Vanilla
  rules and the checks.
- **Zoom-out:** `-` and `=`, or the mouse wheel; the original's view, one and a half times and twice it. The view
  is a value of the camera (`view_width`, `view_height`), and everything that was 320 × 176 reads it. `Session`
  owns the zoom and its transition; `World::set_view` changes the world's view. A zoomed view is as high as the
  screen with the HUD over its bottom; the camera goes 64 pixels past the level's bottom so the HUD never hides
  the floor (`Camera::covered`).
- **The text layer** is as large as the view; a box at a speaker is placed in it as it is, one at a fixed cell in
  its middle (`Line::by_point`).
- **512 entity slots and 2,048 sprite pieces** under Remaster rules (`World::set_rules`; `kSlots` and
  `kOriginalPieces` are Vanilla's 20 and 128). The layer's size and its three pools are a `PieceLayout`.

- **The minimap** (`minimap.*`, M): a picture of the whole level from the map's tile types, with how much of
  it a view has shown in its alpha (`Session::seen_`, per level and per pixel of the minimap, forgotten when the
  game is closed). The GPU renderer shows a square of it around the view in the bottom left corner
  (`gpu_over.frag`): it adds what the view shows now and the Vikings (`Screen::level_x`, `Screen::vikings`) at
  every refresh, and moves the HUD right for it.

- **Rewind and quicksave** (`Session`, Backspace; F7 and F8 with the cheat on): `World` can be copied (its
  slots are `OwnedEntity`, whose copy is the entity's clone), and `Play::Snapshot` is a world with its level.
  `Session` keeps one before every step in a level with the step's keys, 233 of them (10 seconds), and the
  last 10 seconds of the sound as it was heard, for playing backwards. Going back undoes two steps a tick and
  runs each again from its snapshot for the pictures in between (`Session::back_to`). `lv-replay --copies` proves on the replays that a copy keeps
  everything; run it after adding state to `World` or to a kind.

Open:

- **A sprite's picture is known to the GPU renderer only by what it looks like:** it is put into the atlas the
  first time it is shown and found again by a hash of its pixels. Art packs that replace sprites one for one
  (M8) need a piece named by the frame it shows instead.
- **Co-op and the active area:** with several views of one world, either the active area is all the views
  together or everything runs (ADR-0011). Not decided.
- **Not checked by anything but the eye:** whatever the GPU renderer shows beyond the original's view, and
  whether a level still plays well zoomed out (enemies run for longer and further; nothing resets as often).
- **What the minimap has seen and the quicksave are the player's**, outside the simulation: they must never change
  what a replay shows. There is no profile or configuration file yet, so the minimap's fog starts over with
  every run and the quicksave is gone when the game is closed (both are to go to disk with the profile); the game takes command-line options only
  (`src/lvr/main.cpp`).
- **Input per player** was planned from the start (ADR-0012) but the engine takes one keyboard word. Not needed
  for M7; worth not making harder.

## The layout

| Where | What |
|---|---|
| `src/assets/` | reading the original's files: `DATA.DAT`, levels, graphics, texts, object databases and their scripts |
| `src/audio/`, `src/image/`, `src/input/` | sound (OPL3 and General MIDI), PNG, keys and gamepad |
| `src/lvr/` | the engine and the game (`vikings-remaster`); [`src/lvr/README.md`](../src/lvr/README.md) names every file |
| `src/replay/` | replays and goldens, and `lv-replay`, which plays one against the other |
| `src/exporter/` | `lv-export`, a development tool: the game's data as PNG and JSON, script listings |
| `replays/` | the replays with their goldens ([`replays/README.md`](../replays/README.md)) |
| `scripts/` | the checks: `check.sh`, `goldens.sh`, coverage, `replay_cover.sh` |
| `tools/` | research tools: the scriptable DOSBox (`dosbox.py`, `lockstep.py`), disassemblers, the replay maker |
| `docs/original/`, `docs/formats/` | our write-ups of the original's engine and file formats |

## How the work is done

- **Before a commit that touches the engine:** `./scripts/goldens.sh` (every replay against its golden, under a
  minute) and `./scripts/check.sh` (format, constants defined once, ASan/UBSan build with lint, unit tests,
  coverage of at least 80%). Commit when both pass. After a change that is only formatting, comments or docs
  neither needs running again. On the host, `./run-dev-env.sh test` does both in the container.
- **The game itself runs on the host**, not in the container (no display or sound there):
  `./build/release/bin/vikings-remaster --slot 0 --sound modern`, with `--rules remaster` for the Remaster rules.
  What a Remaster window would show can be written as a picture in the container (`--dump`, with `--zoom`). Rebuild the release preset before asking the
  user to try something: `goldens.sh` only builds the replay tool.
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

## Open

- Written from the script's listing and never compared with the original: that Olaf's shield stays, put away,
  when Olaf himself is gone and the cheat is on. And whatever else no replay reaches (above;
  [`formats/unknowns.md`](formats/unknowns.md), section 5).
- One reading of the original that the goldens carry was checked only indirectly: a level loaded again reloads
  its palette. (F5 in level 1 loads the same level again and the pictures match; the case it was first written
  for, an exit reached with a Viking gone, was not tried.)
- The live sound (the sound card pulling, `Session::render_live`) was tested by ear on the user's machine only;
  no automated test opens a device.
- Screenshots for the README: wanted, undecided. If added, hosted outside the repository.
