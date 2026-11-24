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
- **Next is M7, the QoL rules.** Nothing of it is started.

## Starting M7

The list and the decisions are in [`ROADMAP.md`](ROADMAP.md) and [`decisions.md`](decisions.md) (ADR-0005,
ADR-0011): zoom-out, an unlimited active area as its own switch, a minimap with fog of war, a 10-second rewind,
autosave and a level select. All of it only under QoL rules; Vanilla rules must stay exactly what they are, and
the replays are what proves that.

What the code has and hasn't, as a starting point:

- **There is no Rules switch yet.** Nothing in `Play`, `World` or `Session` knows about Vanilla and QoL. The first
  piece of M7 is that setting, chosen per session, and a rule for every change after it: with Vanilla chosen, all
  119 replays still match.
- **The view is a constant.** `kViewWidth` and `kViewHeight` (320 × 176, `src/lvr/camera.hpp`) are used directly in
  about thirty places: the camera's limits, the active area, the renderer, "in view" tests in the kinds
  (`Enemy::stay_in_view`, `World::well_in_view`), text boxes. Zoom-out means the view becomes a value of the world.
  `src/lvr/enemies.hpp` has a second `kViewWidth` of its own.
- **The active area** is `World::mark_outside_active_area` (`src/lvr/world.cpp`): the view plus a margin, by
  which entities are removed and created again. Zoom-out moves it with the view; the unlimited switch turns it
  off. The 20 entity slots (`kSlots`) and the 128 sprite pieces are Vanilla's limits and are kept there on
  purpose (ADR-0018); QoL rules need more of both, and the draw order they imply decided another way.
- **A world can't be copied yet.** Every entity can (`Entity::clone`, set up by `make_entity`), and the simulation
  is deterministic and integer-only, but `World` itself holds its entities by pointer and has no copy. Rewind
  needs that copy first: a ring of about 200 worlds for 10 seconds. What carries between scenes is listed in
  `Play::change_scene`.
- **Input per player** was planned from the start (ADR-0012) but the engine takes one keyboard word. Not needed
  for M7; worth not making harder.
- **The minimap and the saves are profile data**, outside the simulation: they must never change what a replay
  shows. There is no profile or configuration file yet; the game takes command-line options only
  (`src/lvr/main.cpp`).
- **Drawing** is one Classic picture of 320 × 240 (`src/lvr/render.*`). A zoomed view is a bigger playfield; how
  it is shown (scaled down, or a larger window) is undecided.

Decide with the user before building: the order of the five features, how zoom is shown, and the keys for the
new things.

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
  `./build/release/bin/vikings-remaster --slot 0 --sound modern`. Rebuild the release preset before asking the
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
