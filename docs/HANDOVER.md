# Handover: where the project stands

For whoever picks the work up next, person or session. How the game is built: [`architecture.md`](architecture.md);
why: [`decisions.md`](decisions.md); what is ahead: [`ROADMAP.md`](ROADMAP.md).

## Where it stands

- **The whole game plays** under Vanilla rules with Classic visuals: all 37 levels and every scene, each kind of
  entity written by hand (`src/lvr/`), with Classic and Modern sound.
- **It matches the original.** At every refresh of every replay the engine shows the picture and asks for the
  sounds that the original's own bytecode did (the goldens in `replays/`), and `tools/lockstep.py` compares it
  with the original in DOSBox directly.
- **Next:** squash the history before publishing (old commits hold text of the game and replays converted from a
  GPL project), then the loose ends and M7 in the roadmap.

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
| `tools/` | research tools: the scriptable DOSBox (`dosbox.py`, `lockstep.py`), disassemblers, the replay makers |
| `docs/original/`, `docs/formats/` | our write-ups of the original's engine and file formats |

## How the work is done

- **Before a commit that touches the engine:** `./scripts/goldens.sh` (every replay against its golden, under a
  minute) and `./scripts/check.sh` (format, constants defined once, ASan/UBSan build with lint, unit tests,
  coverage of at least 80%). Commit when both pass. After a change that is only formatting, comments or docs
  neither needs running again.
- **A golden is never recorded again to make a difference go away.** There is no oracle left to record from: a
  difference is a change of behaviour until shown otherwise. `lv-replay` writes what the engine shows at the
  first refresh that differs; run the commit before the change the same way to see what it showed before.
- **Something new or in doubt** (a path no replay reaches, a question about the original): `tools/lockstep.py`
  plays the same input to the original in DOSBox and to the engine and compares the pictures
  ([`dosbox.md`](dosbox.md)). It starts at the first logo; with the cheat's password typed, F6 goes on a level at
  a time, and `--place` puts the Vikings where they are needed. A run worth keeping becomes a replay.
- **A fault of the original is not copied.** When a comparison turns up an accident of the original (a stray
  write, a glitch pixel), trace it far enough to be sure, write it into [`quirks.md`](quirks.md) as "not kept", and
  leave it out.
- **What the replays don't reach** is measured by `./scripts/replay_coverage.sh` (about 82% of the engine's lines
  are reached). The unit tests in `src/lvr/tests/` guard much of the rest. The last level's fight and the cranes
  are the thinnest.
- **Conventions** are in [`development.md`](development.md): hex only for what points into the original's files
  or defines a bit, decimal and `px()` for values; no text of the game anywhere in the repository; 16-bit
  arithmetic wraps on purpose.
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
