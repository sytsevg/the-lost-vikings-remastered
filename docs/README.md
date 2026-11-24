# Documentation

What the project is, how to build it and how to contribute: the [top-level README](../README.md).

| Path | Contents |
|---|---|
| [`HANDOVER.md`](HANDOVER.md) | Where the project stands, how to work on it, what is open |
| [`architecture.md`](architecture.md) | How the game is built: the parts, a tick, entities and kinds, how it is checked |
| [`decisions.md`](decisions.md) | The decisions that shape it, and why |
| [`ROADMAP.md`](ROADMAP.md) | What is built and what is ahead |
| [`development.md`](development.md) | Dev container, building, presets, code style and conventions |
| [`original/`](original/) | How the original's engine behaves, read from its code (timing, physics, the Vikings, the scripts) |
| [`formats/`](formats/) | The original's file formats and known releases; [`formats/unknowns.md`](formats/unknowns.md): what is not known |
| [`quirks.md`](quirks.md) | The odd things the original does (flaws, leftovers, debug switches), and which are kept |
| [`dosbox.md`](dosbox.md) | The scriptable DOSBox: checking the game against the original |
| [`../replays/README.md`](../replays/README.md) | The replays and their goldens |

## Local layout (not committed)

```
gamedata/
  original/            your DOS DATA.DAT + VIKINGS.EXE (checked by hash)
  GeneralUser-GS.sf2   the SoundFont of the Modern sound (optional)
  exported/            output of lv-export: PNG + JSON for inspection (dev tool)
```
