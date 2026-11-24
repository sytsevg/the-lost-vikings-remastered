# Replays

The input of a run, step by step, for the pixel-match harness (`lv-replay`, `src/replay/`; ADR-0017): the engine
plays a replay, and what it shows at every refresh (the Classic picture, 320 × 240 palette indices, its palette,
and what the game asks of the sound) is hashed and compared with the replay's golden.

```
lv-replay replays/*.replay                  # the engine against the goldens (./scripts/goldens.sh: all, side by side)
lv-replay --record replays/NAME.replay      # NAME.golden next to it, from the engine as it is: for a new replay
```

The goldens here were recorded from the reference engine (the original's own bytecode on a rebuilt interpreter,
compared with the original in DOSBox), which was retired on 2026-11-23 once the engine matched it on every
replay. They are what is left of it: record one again only for a new replay, never to make a difference go away.

A difference is reported with its first refresh and written to `gamedata/previews/match/` (`--out DIR`):
`NAME-shown.png` (what the engine shows at that refresh) and `NAME.txt` (the refresh, and the entities then:
slot, kind, position, flags). The golden keeps only hashes, so to see what it should look like, run the commit
before the change the same way and compare the two pictures.

## Which replays are here

115 of the 227 that were recorded with the reference engine, and four added since (`level-keys`, `level-keys-four`, `level-keys-kept`, `level-keys-gone`). On 2026-11-23, before the reference engine was retired, the set was pruned by
measurement (`scripts/replay_cover.sh`): each replay's own coverage of `src/lvr/`, lines and branch directions,
and then the smallest set found that still reaches everything the 227 reached together, by two different replays
wherever two reached it. Always kept: the five scenes' replays and every level's `reach-NN`, so each level's own
data is shown at least once. Of the other sets a level's replay is here only where it adds something: 8 of
`level-NN`, 9 of `reach2-NN`, 7 of `reach3-NN`, 31 of `reach4-NN`, 18 of `reach5-NN`. The 115 were then played in
both engines once more (all match) and their goldens recorded again from the reference engine (identical to the
ones here).

A replay reaching a line is not that line's behaviour checked; the unit tests (`src/lvr/tests/`) guard what the
replays pass by. A new replay needs a golden, and with the reference engine gone a golden can only be recorded
from the engine: it then pins what the engine does, not what the original did.

## The files
| Files | What |
|---|---|
| `level-NN.replay` | every level, started in it, on the first golden replays' input: Enter taps through the opening, walking both ways with F, D, E and S, Viking switches, Up and Down, the inventory. 479 steps |
| `attract.replay` | the attract cycle from the first logo with no input: the logos, the title, the six demos (levels 3, 10, 9, 16, 24, 26) |
| `ending.replay` | the ending with no input, from the starfield: the galaxy, the village, the ship |
| `new-game.replay` | from the title: the menu's first choice, the village intro, the night the Vikings are taken, level 1's start |
| `pause-menu.replay` | written by hand: the pause menu and the quit box in level 2, giving up, the game-over scene, the level again |
| `level-keys-four.replay` | the same way in, then the cheat's other level keys: F, D, E and S together, and with Left. The engine's own run, compared with the original in DOSBox at five points |
| `level-keys-kept.replay` | the cheat's password, level 1 given up, "no" at the game-over scene, the title and a new game again: F6 still works. The engine's own run, compared with the original in DOSBox at its end |
| `level-keys-gone.replay` | the same way in, Erik walked into level 1's electric field, then the four keys: on to level 2 with all three. The engine's own run, compared with the original in DOSBox at three points |
| `level-keys.replay` | written by hand: the title's cheat password, then F6 and F5 in level 1. Its golden is the engine's own run, compared with the original in DOSBox at six points (`tools/lockstep.py`) |
| `sound-switches.replay` | written by hand: Alt+S and Alt+M in level 1, the effects and the music off and on |
| `reach-NN.replay` (all 37) | every placement of level NN visited (`tools/reach_replays.py`): the Vikings and their boxes put beside each in turn, the same moves at each |
| `reach4-NN.replay` | the fourth set of visits (`--moves fight`): Erik charges and Baleog strikes and shoots, each way, at each placement |
| `reach5-NN.replay` | the fifth set (`--moves duel`): only the enemies, each from both sides; Baleog is put two cells from one, facing it, and strikes and shoots until it is dead or he is |
| `NAME.golden` | the reference engine's run of `NAME.replay`, recorded before it was retired: a line per run of refreshes that hash the same, the hash in hex (32 bits: FNV-1a of 64 over the pixels, the palette and the sounds, its halves folded) and the run's length when it is more than one (`6a37ace2 5`). 13 MB for all of them |

The reference engine was the oracle; it had been compared with the original in DOSBox on the same inputs
([`docs/dosbox.md`](../docs/dosbox.md)): the golden replays' on their path from the title.

## The format
A text file; `#` starts a comment. Steps count from 0. A step is one pass of input as the original's loop reads
it: a tick of a level (3 refreshes), a pass of a menu's loop or of the wait for a key.

| Line | Meaning |
|---|---|
| `slot N` | the original's slot the run starts in (0–36 the levels, 37–47 the scenes; 39 is the first logo) |
| `steps N` | how many steps; must come before the lines below |
| `key A-B WORD` | the bits of the keyboard word held in steps A to B, or `key A WORD` for one step ([`docs/original/input.md`](../docs/original/input.md)); lines add up |
| `held A SCANCODE` | a key the game reads from the key-state array, held in that step: Y `0x15`, N `0x31`, Alt `0x38`, X `0x2D`, Q `0x10`, F10 `0x44`, S `0x1F`, M `0x32` |
| `typed A CHARACTER` | a character typed before that step, as the original's keyboard handler notes it (`ds:028C`): the password's letters, the menus' keys |
