#!/usr/bin/env bash
# The Windows build (the windows preset: cross-built with llvm-mingw, cmake/windows.cmake) and its checks under Wine:
# the tests, as check.sh runs them, and every replay against its golden, as goldens.sh plays them. Not part of
# check.sh. What comes out: build/windows/bin/vikings-remaster.exe, one file, for Windows 10 and later.
set -euo pipefail
cd "$(dirname "$0")/.."

echo "== configure + build (windows)"
cmake --preset windows -Wno-dev >/dev/null
mkdir -p build/windows
if ! cmake --build --preset windows >build/windows/build.log 2>&1; then
    grep -A8 'error' build/windows/build.log >&2 || tail -20 build/windows/build.log >&2
    echo "windows: the build FAILED (build/windows/build.log)" >&2
    exit 1
fi

echo "== tests (windows, under Wine)"
ctest --preset windows --parallel "$(nproc)"

echo "== replays (windows, under Wine)"
log=build/windows/goldens.log
if printf '%s\n' replays/*.replay |
    xargs -P "$(nproc)" -n 4 cmake/wine.sh build/windows/bin/lv-replay.exe --check >"$log" 2>&1; then
    echo "windows: all $(grep -c 'matches its golden' "$log") replays match"
else
    grep -v 'matches its golden\|replays match' "$log" >&2 || true
    echo "windows: the replays FAILED" >&2
    exit 1
fi
echo "windows: built build/windows/bin/vikings-remaster.exe"
