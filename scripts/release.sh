#!/usr/bin/env bash
# The release archives (docs/development.md, "Releases"): the game built for Linux (the release preset) and for
# Windows (scripts/windows.sh), each packed with the SoundFont and its licence, a player's README, the licence and
# the notices, into build/dist/:
#   lost-vikings-remastered-VERSION-linux-x86_64.tar.gz
#   lost-vikings-remastered-VERSION-windows-x86_64.zip
# VERSION is the project's in CMakeLists.txt. Needs gamedata/GeneralUser-GS.sf2. Run in the dev container (or
# `make release` on the host).
set -euo pipefail
cd "$(dirname "$0")/.."

version=$(sed -n 's/^project(vikings_remaster VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt)
[[ -n "$version" ]] || { echo "release: no version in CMakeLists.txt" >&2; exit 1; }
name="lost-vikings-remastered-$version"
soundfont=gamedata/GeneralUser-GS.sf2
[[ -f "$soundfont" ]] || { echo "release: $soundfont is needed: the archives carry it" >&2; exit 1; }
strip="$(dirname "$(readlink -f "$(command -v clang)")")/llvm-strip"  # (the compiler's own LLVM)

echo "== build (linux)"
cmake --preset release -Wno-dev >/dev/null
mkdir -p build/release
if ! cmake --build --preset release --target vikings-remaster >build/release/release.log 2>&1; then
    grep -A8 'error' build/release/release.log >&2 || tail -20 build/release/release.log >&2
    echo "release: the Linux build FAILED (build/release/release.log)" >&2
    exit 1
fi
echo "== build (windows)"
./scripts/windows.sh

# The newest glibc symbol version the Linux program needs: what a player's system must have at least.
glibc=$(objdump -T build/release/bin/vikings-remaster | grep -o 'GLIBC_[0-9.]*' | sort -t. -k2,2n -k3,3n -u | tail -1)

# The notice an adapted shader keeps at its top (THIRD-PARTY-NOTICES.md): compiled to SPIR-V, it isn't in the program.
shader_notice() {
    awk '/Its notice:/ { on = 1; next } on && /^\/\// { sub(/^\/\//, ""); sub(/^   /, ""); print; next } on { exit }' "$1"
}

# One platform's folder: build/dist/PLATFORM/NAME, made afresh (the folders below build/dist are named in full).
rm -rf build/dist/linux build/dist/windows
stage() {
    local platform=$1 program=$2 preset=$3
    local folder="build/dist/$platform/$name"
    local deps="build/$preset/_deps"
    mkdir -p "$folder/gamedata/original" "$folder/licenses"
    cp "$program" "$folder/"
    "$strip" "$folder/$(basename "$program")"
    sed "s/@VERSION@/$version/" scripts/release-README.txt >"$folder/README.txt"
    cp LICENSE THIRD-PARTY-NOTICES.md "$folder/"
    # The licences of what is linked into the program, and the adapted shaders' notices.
    cp "$deps/sdl3-src/LICENSE.txt" "$folder/licenses/SDL3.txt"
    cp "$deps/sdl3_net-src/LICENSE.txt" "$folder/licenses/SDL3_net.txt"
    cp "$deps/ymfm-src/LICENSE" "$folder/licenses/ymfm.txt"
    cp "$deps/tsf-src/LICENSE" "$folder/licenses/TinySoundFont.txt"
    shader_notice src/lvr/gpu_xbr.frag >"$folder/licenses/xBR-shader.txt"
    shader_notice src/lvr/gpu_hyllian.frag >"$folder/licenses/Hyllian-CRT-shader.txt"
    if [[ $platform == windows ]]; then
        cp /opt/llvm-mingw/x86_64-w64-mingw32/share/mingw32/COPYING.MinGW-w64-runtime.txt \
            "$folder/licenses/mingw-w64-runtime.txt"
    fi
    cp "$soundfont" "$folder/gamedata/"
    # The SoundFont's licence, as the file itself carries it (its INFO list's comment).
    python3 -I - "$soundfont" >"$folder/gamedata/GeneralUser-GS-LICENSE.txt" <<'EOF'
import struct, sys
data = open(sys.argv[1], 'rb').read(1 << 16)
at = data.find(b'ICMT')
if at < 0:
    sys.exit('release: the SoundFont has no licence comment')
size, = struct.unpack('<I', data[at + 4:at + 8])
print(data[at + 8:at + 8 + size].rstrip(b'\0').decode('latin-1').strip())
EOF
    cat >"$folder/gamedata/original/PUT-THE-GAME-HERE.txt" <<'EOF'
Put DATA.DAT and VIKINGS.EXE of your own copy of the DOS release of The Lost Vikings here.
Optional: your own SNES ROM of the game (*.sfc, the US release), for the backdrops under Remaster rules.
EOF
}
stage linux build/release/bin/vikings-remaster release
stage windows build/windows/bin/vikings-remaster.exe windows

echo "== pack"
linux="build/dist/$name-linux-x86_64.tar.gz"
windows="build/dist/$name-windows-x86_64.zip"
rm -f "$linux" "$windows"
tar -C build/dist/linux --sort=name --owner=0 --group=0 --numeric-owner -czf "$linux" "$name"
(cd build/dist/windows && cmake -E tar cf "../$name-windows-x86_64.zip" --format=zip "$name")
ls -l "$linux" "$windows"
echo "release: $version built: $linux, $windows (Linux: $glibc or later)"
