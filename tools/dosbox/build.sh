#!/usr/bin/env bash
# Builds the pinned DOSBox-X (ADR-0016) with the control channel into build/dosbox/: a development tool, never
# linked into the game or shipped. The source goes to build/dosbox/src, the program to build/dosbox/bin/dosbox-x.
# The patch: hooks.patch (small hooks in DOSBox-X's files) and control.cpp (the channel, copied in as
# src/misc/lvr_control.cpp). Both are GPL, as DOSBox-X (see control.cpp's header).
set -euo pipefail
cd "$(dirname "$0")/../.."

readonly commit=78c022931f0ad9e567bcaaefc155fc428d9bdc7e # 2026-11-12
readonly repo=https://github.com/joncampbell123/dosbox-x.git
readonly root="$PWD/build/dosbox"
readonly tools="$PWD/tools/dosbox"
readonly jobs="$(nproc)"

# DOSBox-X is built and tested with gcc and libstdc++, not the project's clang + libc++.
export CC=gcc CXX=g++

if [ ! -d build/dosbox/src/.git ]; then
    mkdir -p build/dosbox
    git init -q build/dosbox/src
    git -C build/dosbox/src remote add origin "$repo"
fi
if [ "$(git -C build/dosbox/src rev-parse -q --verify HEAD || true)" != "$commit" ]; then
    git -C build/dosbox/src fetch -q --depth 1 origin "$commit"
fi

cd build/dosbox/src

# The pinned tree plus hooks.patch; a changed patch (or commit) means a fresh checkout and a new configure.
readonly stamp="$commit $(sha256sum "$tools/hooks.patch" | cut -d' ' -f1)"
configured=no
if [ "$(cat .lvr-patched 2>/dev/null || true)" != "$stamp" ]; then
    echo "== patching"
    git checkout -q --force "$commit"
    git apply "$tools/hooks.patch"
    echo "$stamp" > .lvr-patched
    rm -f .lvr-configured
fi
cmp -s "$tools/control.cpp" src/misc/lvr_control.cpp || cp "$tools/control.cpp" src/misc/lvr_control.cpp

# Its own SDL2, static, into vs/sdl2/linux-host, where DOSBox-X's configure looks; so the container needs no SDL2
# package. The flags are vs/sdl2/build-dosbox.sh's, plus no X11 (the container is Wayland only, ADR-0013).
if [ ! -x vs/sdl2/linux-host/bin/sdl2-config ]; then
    echo "== SDL2 (in-tree)"
    mkdir -p vs/sdl2/linux-build
    (cd vs/sdl2/linux-build &&
        ac_cv_header_iconv_h=no ac_cv_func_iconv=no ac_cv_lib_iconv_libiconv_open=no \
            ../configure --prefix="$PWD/../linux-host" --enable-static --disable-shared --disable-video-x11 &&
        make -j"$jobs" && make install)
fi

if [ ! -f .lvr-configured ]; then
    echo "== configure"
    ./autogen.sh
    ./configure --prefix="$root" --enable-sdl2 --enable-debug --disable-x11 \
        --with-sdl2-prefix="$PWD/vs/sdl2/linux-host"
    touch .lvr-configured
fi

echo "== DOSBox-X"
make -j"$jobs"
make install
echo "== built: $root/bin/dosbox-x"
