#!/bin/sh
# Runs a program of the Windows build under Wine: CMake's emulator for it (cmake/windows.cmake), and the way
# scripts/windows.sh runs the replays.
#   cmake/wine.sh PROGRAM.exe [ARGUMENT...]
# An absolute path is passed as Z:/... (Wine's drive for /): a program for Windows takes an argument that starts
# with / for an option (Catch2 does). No display: Wine's drivers for one are off, and so is its log.
set -eu
program="$1"
shift
for argument in "$@"; do
    case "$argument" in
        /*) argument="Z:$argument" ;;
    esac
    set -- "$@" "$argument"
    shift
done
export WINEDEBUG="${WINEDEBUG:--all}"
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:+$WINEDLLOVERRIDES;}winex11.drv,winewayland.drv="
exec wine "$program" "$@"
