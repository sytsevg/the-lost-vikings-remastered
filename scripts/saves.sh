#!/usr/bin/env bash
# Every replay with the world written out and read back before every step (lv-replay --saves), in the release
# build: what a save holds (a quicksave; later a snapshot sent to another player) must be all a replay needs, or
# the replay differs from its golden. Run it after adding state to the world or to a kind. About a minute.
set -euo pipefail
cd "$(dirname "$0")/.."

cmake --preset release -Wno-dev >/dev/null
if ! cmake --build --preset release --target lv-replay >build/release/saves-build.log 2>&1; then
    grep -A8 'error' build/release/saves-build.log >&2 || tail -20 build/release/saves-build.log >&2
    echo "saves: the build FAILED" >&2
    exit 1
fi

log=build/release/saves.log
if printf '%s\n' replays/*.replay |
    xargs -P "$(nproc)" -n 4 build/release/bin/lv-replay --check --saves --out gamedata/previews/saves >"$log" 2>&1; then
    echo "saves: all $(grep -c 'matches its golden' "$log") replays match, written out and read back at every step"
else
    grep -v 'matches its golden\|replays match' "$log" >&2 || true
    echo "saves: FAILED" >&2
    exit 1
fi
