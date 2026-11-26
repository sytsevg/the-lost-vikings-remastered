#!/usr/bin/env bash
# Which replays are needed: each replay's own coverage of src/lvr/ (lines and branch directions, in the coverage
# build, the engine against the goldens alone: --plain), then the smallest set found that still reaches everything
# all of them reach, twice over where two replays reach it (scripts/replay_cover.py). The set was pruned with this on
# 2026-11-23 (replays/README.md). About ten minutes. Shares build/coverage with check.sh: not at the same time.
#   ./scripts/replay_cover.sh [TIMES]    TIMES: by how many kept replays each line and branch is reached (2)
set -euo pipefail
cd "$(dirname "$0")/.."

build=build/coverage
out="$build/per-replay"
major="$(clang++ --version | sed -n 's/.*clang version \([0-9]*\).*/\1/p' | head -n 1)"
tool() { command -v "$1-$major" || command -v "$1" || { echo "coverage: $1 not found" >&2; exit 1; }; }
profdata="$(tool llvm-profdata)"
cov="$(tool llvm-cov)"
export build out profdata cov

cmake --preset coverage >/dev/null
cmake --build --preset coverage --target lv-replay >/dev/null
rm -rf "$out"
mkdir -p "$out"
one() {
    name="$(basename "$1" .replay)"
    LLVM_PROFILE_FILE="$PWD/$out/$name.profraw" "$build/bin/lv-replay" --check --plain "$1" >/dev/null 2>&1 ||
        echo "replay_cover: $name does not match its golden" >&2
    "$profdata" merge -sparse "$out/$name.profraw" -o "$out/$name.profdata"
    "$cov" export "$build/bin/lv-replay" -instr-profile="$out/$name.profdata" -format=lcov "$PWD/src/lvr" 2>/dev/null |
        grep -E '^(SF|DA|BRDA):' >"$out/$name.lcov"
    rm -f "$out/$name.profraw" "$out/$name.profdata"
}
export -f one
printf '%s\n' replays/*.replay | xargs -P "$(nproc)" -n 1 bash -c 'one "$0"'
python3 scripts/replay_cover.py "$out" "${1:-2}"
