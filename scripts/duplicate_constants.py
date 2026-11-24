#!/usr/bin/env python3
"""Fails if a constant is defined at file or namespace level in more than one file of src/lvr/ with the same name
and value: such a number belongs in one place (lvr/names.hpp, kinds.hpp, level.hpp, shared.hpp), named once.
Constants of a class are its own and are not looked at. Part of scripts/check.sh.

  scripts/duplicate_constants.py
"""
import collections
import glob
import re
import sys

# The same name in two namespaces on purpose: a placement's flags become its entity's.
# kHome: each world's file says which database its animations are named in.
ALLOWED = {"kFacingLeft", "kCreatedAgainAtOnce", "kAlwaysActive", "kNobody", "kHome"}


def main():
    seen = collections.defaultdict(set)
    for path in sorted(glob.glob("src/lvr/*.cpp") + glob.glob("src/lvr/*.hpp")):
        space = ""  # the named namespace a line is in (key::, hit:: ...): the same name in two of them is two names
        for line in open(path):
            opened = re.match(r"namespace (\w+) \{", line)
            if opened and opened.group(1) != "lvr":
                space = opened.group(1) + "::"
            elif re.match(r"\}  // namespace \w+", line):
                space = ""
            found = re.match(r"(?:inline )?constexpr [\w:]+ (k\w+) = ([^;]+);", line)  # column 0: not a class's
            if found and found.group(1) not in ALLOWED:
                seen[(space + found.group(1), found.group(2).strip())].add(path)
    twice = {key: paths for key, paths in seen.items() if len(paths) > 1}
    for (name, value), paths in sorted(twice.items()):
        print(f"{name} = {value} is defined in {', '.join(sorted(paths))}")
    if twice:
        print(f"{len(twice)} constants defined in more than one file: give each one home")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
