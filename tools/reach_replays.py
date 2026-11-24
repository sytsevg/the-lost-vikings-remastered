#!/usr/bin/env python3
"""Writes replays/reach-NN.replay for the levels: replays that visit every placement of a level.

The golden replays walk a little way from each level's start, and most of a level's entities never wake up in
them. A reach replay puts the Vikings (the replay format's `place` line, in both engines alike) beside one
placement after another and makes the same moves at each: wait, walk right, left, right, act, and each Viking's own
keys. What the entities there do is then held by the replay's golden like everything else
(`lv-replay replays/reach-NN.replay`; docs/ROADMAP.md, M6: "script paths no replay reaches reached on purpose").

  tools/reach_replays.py [--levels gamedata/exported/levels] [--out replays] [--moves walk|updown|ride|fight|duel] [LEVEL..]

`--moves updown` writes reach2-NN.replay: the same visits with Up, Down and the items instead of the walk.
`--moves ride` writes reach3-NN.replay: the Vikings put on top of each placement that can be stood on.
`--moves fight` writes reach4-NN.replay: Erik charges and Baleog strikes and shoots, each way, at each placement.
`--moves duel` writes reach5-NN.replay: only the enemies, each from both sides, Baleog striking until one is dead.

It reads the level dumps `lv-export assets` writes (NN-level.json). Where the Vikings are put: a free cell with two
free cells above it and ground under it, the nearest to the placement; a placement with no such cell near it is
left out, and so are the visits in LEFT_OUT. Visits are in the level's own order of placements; one visit per place the
Vikings are put at.
"""
import argparse
import json
import pathlib

GROUND = {0x01, 0x02, 0x20}
# Visits left out, by level: where the reference engine, which the goldens were recorded from, stopped (a read
# outside the data it modelled). A replay made today needs no such list, but then its golden is the engine's own.
LEFT_OUT = {
    2: {14},  # kind 47 at 568,688: "slot 10 at 7C4E: address 344D is outside the modelled data"
}
VISIT = 150  # steps a visit
# What is done at a visit, from its first step: (from, to, keyboard word).
MOVES = [
    (15, 34, 0x0100),   # right
    (35, 74, 0x0200),   # left
    (75, 94, 0x0100),   # right
    (95, 95, 0x0080),   # S: act
    (100, 100, 0x8000),  # F (also answers what waits for a key)
    (104, 104, 0x8000),
    (108, 108, 0x4000),  # D
    (112, 112, 0x0010),  # the next Viking: Baleog
    (116, 116, 0x8000),  # his sword
    (120, 120, 0x4000),  # his bow
    (124, 124, 0x0080),  # act
    (128, 128, 0x0010),  # the next: Olaf
    (132, 132, 0x8000),  # his shield up
    (136, 136, 0x8000),  # and down
    (140, 140, 0x0010),  # back to Erik
    (144, 144, 0x8000),  # once more, for a box still up
]


# The second set ("--moves updown", reach2-NN.replay): Up and Down at each placement, for lifts and ladders, with
# each Viking, and the item each holds used.
MOVES_UPDOWN = [
    (15, 44, 0x0800),   # up
    (45, 49, 0x0000),
    (50, 79, 0x0400),   # down
    (80, 80, 0x0040),   # E: the item held
    (84, 84, 0x0010),   # Baleog
    (88, 104, 0x0800),  # up
    (106, 120, 0x0400),  # down
    (122, 122, 0x0010),  # Olaf
    (126, 136, 0x0800),  # up
    (138, 146, 0x0400),  # down
    (148, 148, 0x0010),  # back to Erik
]
# The third set ("--moves ride", reach3-NN.replay): the Vikings put on top of what can be stood on (a placement that
# acts as ground or a platform: a lift, a block, a bridge), then Up, Down, and a step each way.
MOVES_RIDE = [
    (15, 59, 0x0800),   # up
    (60, 64, 0x0000),
    (65, 109, 0x0400),  # down
    (115, 124, 0x0100),  # right
    (130, 139, 0x0200),  # left
]
# The fourth set ("--moves fight", reach4-NN.replay): the Vikings fight at each placement, so that what is there is
# hurt, killed or broken: Erik runs and charges each way, Baleog swings his sword and shoots each way.
MOVES_FIGHT = [
    (12, 29, 0x0100),   # Erik runs right ...
    (30, 30, 0x4100),   # ... and charges (D)
    (31, 39, 0x0100),
    (45, 62, 0x0200),   # and left
    (63, 63, 0x4200),
    (64, 72, 0x0200),
    (78, 78, 0x0010),   # Baleog
    (80, 81, 0x0100),   # facing right
    (84, 84, 0x8000),   # his sword, three times
    (92, 92, 0x8000),
    (100, 100, 0x8000),
    (104, 112, 0x4000),  # his bow drawn, and loosed
    (116, 117, 0x0200),  # facing left
    (120, 120, 0x8000),
    (128, 128, 0x8000),
    (132, 138, 0x4000),
    (142, 142, 0x0010),  # Olaf
    (146, 146, 0x0010),  # back to Erik
]
# The fifth set ("--moves duel", reach5-NN.replay): only the enemies are visited, each twice: Baleog is put two
# cells to its left facing it, then two cells to its right, and strikes and shoots until it is dead or he is.
def moves_duel(towards):
    """towards: the keyboard word of the direction the enemy is in."""
    moves = [(3, 3, 0x0010), (6, 7, towards)]  # Baleog, facing it
    moves += [(step, step, 0x8000) for step in range(10, 96, 7)]  # his sword, again and again
    moves += [(100, 108, 0x4000), (114, 122, 0x4000)]  # two arrows
    moves += [(128, 128, 0x8000), (135, 135, 0x8000)]
    moves += [(142, 142, 0x0010), (146, 146, 0x0010)]  # Olaf, and back to Erik
    return moves


MOVES_DUEL = moves_duel(0x0100)  # the set's name in MOVE_SETS; replay_of makes both sides
ENEMY = 0x2e  # what an enemy acts as
MOVE_SETS = {"walk": ("reach", MOVES), "updown": ("reach2", MOVES_UPDOWN), "ride": ("reach3", MOVES_RIDE),
             "fight": ("reach4", MOVES_FIGHT), "duel": ("reach5", MOVES_DUEL)}
STOOD_ON = {0x01, 0x05}  # what a placement acts as that a Viking can stand on
FEET = 13  # a Viking's feet are this far below his position


def spot(level, px, py):
    """Where to put the Vikings for a placement at px, py: (x, y), or None."""
    width, height, types = level["width"], level["height"], level["cell_tile_types"]

    def cell(column, row):
        return types[row * width + column]

    def free(column, row):
        return 0 <= column < width and 0 <= row < height and cell(column, row) == 0

    best = None
    for across in (0, 1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 6, -6):
        for down in (0, 1, -1, 2, -2, 3, -3, 4):
            column, row = px // 16 + across, py // 16 + down
            if (free(column, row) and free(column, row - 1) and free(column, row - 2) and row + 1 < height
                    and cell(column, row + 1) in GROUND):
                score = abs(across) + 2 * abs(down)
                if best is None or score < best[0]:
                    best = (score, column * 16 + 8, (row + 1) * 16 - 16)
    return best[1:] if best else None


def on_top(placement):
    """Where to put the Vikings to stand on a placement: (x, y), or None if it can't be stood on."""
    if placement["acts_as"] not in STOOD_ON:
        return None
    return placement["x"], placement["box"][1] - 1 - FEET


def replay_of(level, number, moves=MOVES):
    spots = []
    for placement in level["placements"]:
        if moves is MOVES_DUEL:
            if placement["acts_as"] != ENEMY:
                continue
            for across, towards in ((-32, 0x0100), (32, 0x0200)):
                at = spot(level, placement["x"] + across, placement["y"])
                if at:
                    spots.append((at, placement, moves_duel(towards)))
            continue
        if moves is MOVES_RIDE:
            at = on_top(placement)
        else:
            at = spot(level, placement["x"], placement["y"])
        if at and at not in [s[0] for s in spots]:
            spots.append((at, placement, moves))
    if moves is not MOVES_DUEL:  # the visits left out are the other sets'
        spots = [s for visit, s in enumerate(spots) if visit not in LEFT_OUT.get(number, set())]
    lines = [
        f"# Level {number}: every placement visited (tools/reach_replays.py). The Vikings are put beside one after",
        "# another and make the same moves at each.",
        f"slot {number - 1}",
        f"steps {10 + VISIT * len(spots)}",
        "key 3 0x8000",
    ]
    for visit, (at, placement, moves) in enumerate(spots):
        first = 10 + VISIT * visit
        lines.append(f"place {first} {at[0]} {at[1]}  # kind {placement['kind_hex']} at "
                     f"{placement['x']},{placement['y']}")
        for start, end, word in moves:
            steps = f"{first + start}" if start == end else f"{first + start}-{first + end}"
            lines.append(f"key {steps} 0x{word:04x}")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--levels", default="gamedata/exported/levels")
    parser.add_argument("--out", default="replays")
    parser.add_argument("--moves", choices=sorted(MOVE_SETS), default="walk")
    parser.add_argument("level", nargs="*", type=int)
    args = parser.parse_args()
    for number in args.level or range(1, 38):
        level = json.loads((pathlib.Path(args.levels) / f"{number:02d}-level.json").read_text())
        prefix, moves = MOVE_SETS[args.moves]
        if moves is MOVES_RIDE and not any(on_top(p) for p in level["placements"]):
            continue
        if moves is MOVES_DUEL and not any(p["acts_as"] == ENEMY for p in level["placements"]):
            continue
        (pathlib.Path(args.out) / f"{prefix}-{number:02d}.replay").write_text(replay_of(level, number, moves))


if __name__ == "__main__":
    main()
