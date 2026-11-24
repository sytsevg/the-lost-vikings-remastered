#!/usr/bin/env bash
# The engine against every replay's golden (replays/*.golden), in the release build, side by side: the way to see
# that a change of shape changed nothing a replay shows. Under a minute.
set -euo pipefail
cd "$(dirname "$0")/.."

cmake --preset release -Wno-dev >/dev/null
if ! cmake --build --preset release --target lv-replay >build/release/goldens-build.log 2>&1; then
    grep -A8 'error' build/release/goldens-build.log >&2 || tail -20 build/release/goldens-build.log >&2
    echo "goldens: the build FAILED" >&2
    exit 1
fi

log=build/release/goldens.log
if printf '%s\n' replays/*.replay | xargs -P "$(nproc)" -n 4 build/release/bin/lv-replay --check >"$log" 2>&1; then
    echo "goldens: all $(grep -c 'matches its golden' "$log") replays match"
else
    grep -v 'matches its golden\|replays match' "$log" >&2 || true
    echo "goldens: FAILED" >&2
    exit 1
fi
