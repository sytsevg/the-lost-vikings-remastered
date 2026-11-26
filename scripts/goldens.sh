#!/usr/bin/env bash
# Every replay against its golden (replays/*.golden), in the release build, side by side, the largest first: that a
# change of shape changed nothing a replay shows. Before every step the world is also written out and read back,
# and the last 8 steps are run again from a snapshot (lv-replay --check: ADR-0020, ADR-0012), so a replay also
# fails when a save or a copy of the world leaves something out. About a minute and a half.
#
# A replay that fails is played again with --plain, the goldens alone, to say which it is: a change of behaviour
# (it differs there too) or state that isn't all written out or copied (it matches there: a kind's Saved, the
# world's state).
set -euo pipefail
cd "$(dirname "$0")/.."

cmake --preset release -Wno-dev >/dev/null
if ! cmake --build --preset release --target lv-replay >build/release/goldens-build.log 2>&1; then
    grep -A8 'error' build/release/goldens-build.log >&2 || tail -20 build/release/goldens-build.log >&2
    echo "goldens: the build FAILED" >&2
    exit 1
fi

log=build/release/goldens.log
if ls -S replays/*.replay | xargs -P "$(nproc)" -n 1 build/release/bin/lv-replay --check >"$log" 2>&1; then
    echo "goldens: all $(grep -c 'matches its golden' "$log") replays match," \
        "written out, read back and run again at every step"
    exit 0
fi
grep -v 'matches its golden\|replays match' "$log" >&2 || true

failed=()
for replay in replays/*.replay; do
    grep -q "^$(basename "$replay" .replay): matches its golden" "$log" || failed+=("$replay")
done
plain=build/release/goldens-plain.log
printf '%s\n' "${failed[@]}" |
    xargs -P "$(nproc)" -n 1 build/release/bin/lv-replay --check --plain >"$plain" 2>&1 || true
for replay in "${failed[@]}"; do
    name="$(basename "$replay" .replay)"
    if grep -q "^$name: matches its golden" "$plain"; then
        echo "  $name: matches with --plain: state that a save or a copy of the world leaves out" >&2
    else
        echo "  $name: differs with --plain too: the behaviour changed" >&2
    fi
done
echo "goldens: ${#failed[@]} FAILED" >&2
exit 1
