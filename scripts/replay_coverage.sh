#!/usr/bin/env bash
# What of the engine the replays reach: lv-replay --check over every replay in the coverage build (against the
# goldens, the world written out, read back and run again as goldens.sh plays them), and the line coverage of
# src/lvr/ from those runs only. The lines no replay reaches are the ones a refactor can break without a replay
# noticing (docs/HANDOVER.md).
#   ./scripts/replay_coverage.sh   report in build/coverage/replays.txt, annotated sources in
#                                  build/coverage/replays-html/, unreached lines per file in
#                                  build/coverage/replays-unreached.txt
set -euo pipefail
cd "$(dirname "$0")/.."

build=build/coverage
profiles="$build/replay-profiles"
major="$(clang++ --version | sed -n 's/.*clang version \([0-9]*\).*/\1/p' | head -n 1)"
tool() { command -v "$1-$major" || command -v "$1" || { echo "coverage: $1 not found" >&2; exit 1; }; }
profdata="$(tool llvm-profdata)"
cov="$(tool llvm-cov)"

cmake --preset coverage >/dev/null
cmake --build --preset coverage --target lv-replay

rm -rf "$profiles"
mkdir -p "$profiles"
# One process per replay, each with a profile of its own (%p: its process id).
printf '%s\n' replays/*.replay |
    LLVM_PROFILE_FILE="$PWD/$profiles/%p.profraw" xargs -P "$(nproc)" -n 1 "$build/bin/lv-replay" --check \
        >"$build/replays-run.txt"
"$profdata" merge -sparse "$profiles"/*.profraw -o "$build/replays.profdata"

options=(-instr-profile="$build/replays.profdata")
"$cov" report "$build/bin/lv-replay" "${options[@]}" -show-region-summary=false -show-branch-summary=false \
    "$PWD/src/lvr" >"$build/replays.txt"
"$cov" show "$build/bin/lv-replay" "${options[@]}" -format=html -output-dir="$build/replays-html" "$PWD/src/lvr"
# The unreached lines as text: FILE:LINE: the line.
"$cov" show "$build/bin/lv-replay" "${options[@]}" "$PWD/src/lvr" |
    awk '/^\/.*:$/ { file = $0; sub(/:$/, "", file); sub(/.*\/src\//, "src/", file); next }
         /^ +[0-9]+\| +0\|/ { split($0, part, "|"); gsub(/ /, "", part[1]); line = $0; sub(/^[^|]*\|[^|]*\|/, "", line);
                              print file ":" part[1] ":" line }' >"$build/replays-unreached.txt"
cat "$build/replays.txt"
