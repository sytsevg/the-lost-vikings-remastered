#!/usr/bin/env bash
# The Windows build: the windows preset cross-builds the game with llvm-mingw (cmake/windows.cmake). Not part of
# check.sh. What comes out: build/windows/bin/vikings-remaster.exe, one file, for Windows 10 and later. Nothing of
# it is run here: the tests and the replays are the Linux build's (the user, 2026-11-26: the Windows build worked
# as it was built; only the build is needed).
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
echo "windows: built build/windows/bin/vikings-remaster.exe"
