#!/usr/bin/env python3
"""Chooses the replays to keep from each one's coverage (scripts/replay_cover.sh makes the .lcov files).

Always kept: the scenes' replays and every level's reach-NN (each placement of the level visited, so every
level's own data is shown at least once). Then, greedily, the replay that adds most, until every line and every
branch direction that any replay reaches is reached by TIMES of the kept ones (or by all that reach it, if
fewer do). Prints the names to keep; the others add nothing by this measure.
"""
import collections
import glob
import os
import re
import sys

directory = sys.argv[1]
times = int(sys.argv[2]) if len(sys.argv) > 2 else 2

reached = {}
for path in sorted(glob.glob(os.path.join(directory, "*.lcov"))):
    name = os.path.basename(path)[: -len(".lcov")]
    items = set()
    source = ""
    with open(path) as file:
        for line in file:
            if line.startswith("SF:"):
                source = line[3:].strip().split("/src/lvr/")[-1]
            elif line.startswith("DA:"):
                number, hits = line[3:].strip().split(",")[:2]
                if hits != "0":
                    items.add((source, "line", number))
            elif line.startswith("BRDA:"):
                number, block, branch, hits = line[5:].strip().split(",")
                if hits not in ("-", "0"):
                    items.add((source, "branch", number, block, branch))
    reached[name] = items

how_many = collections.Counter(item for items in reached.values() for item in items)
needed = {item: min(times, count) for item, count in how_many.items()}


def keep(name):
    for item in reached[name]:
        if needed[item] > 0:
            needed[item] -= 1


kept = [name for name in reached if not re.search(r"-\d+$", name) or re.fullmatch(r"reach-\d+", name)]
for name in kept:
    keep(name)
others = set(reached) - set(kept)
while others:
    best = max(others, key=lambda name: (sum(1 for item in reached[name] if needed[item] > 0), name))
    if not any(needed[item] > 0 for item in reached[best]):
        break
    kept.append(best)
    others.discard(best)
    keep(best)

lines = sum(1 for item in how_many if item[1] == "line")
print(f"{len(reached)} replays reach {lines} lines and {len(how_many) - lines} branch directions", file=sys.stderr)
print(f"{len(kept)} of them reach all of that {times} times over (where {times} do)", file=sys.stderr)
print("\n".join(sorted(kept)))
