# Development

## Dev container
`dev-env/Dockerfile` is Debian trixie (`node:22-trixie-slim`, for Claude Code) plus:
- LLVM **23** from apt.llvm.org: clang, clang-tidy, clang-format, clangd, lld, lldb, libc++, sanitizer and libFuzzer runtimes. Change it with the `LLVM_VERSION` build argument.
- CMake, Ninja, ccache, and SDL3's build dependencies: Wayland (no X11, see ADR-0013), EGL/GLES, ALSA/PulseAudio/PipeWire, udev, D-Bus.
- For the GPU renderer (ADR-0006): `glslc`, which compiles its shaders when the game is built, and a software Vulkan driver (Mesa's lavapipe) with `vulkan-tools`, so its pictures can be made and compared in the container.
- For the Windows build (ADR-0021): llvm-mingw in `/opt/llvm-mingw` (the same LLVM, with libc++ and mingw-w64 for Windows; pinned by its release and checksum, `LLVM_MINGW_RELEASE`, `LLVM_MINGW_SHA256`), and Wine, which runs its tests and replays.
- The `node` base image is just Debian trixie with Node.js added, which Claude Code needs. The project itself doesn't use Node.

On the host:
```sh
./run-dev-env.sh build       # build the game in the container: build/release/bin/vikings-remaster
./run-dev-env.sh test        # build, then every check: lint, unit tests, coverage, the replays
./run-dev-env.sh             # Claude Code in the container (the same as `./run-dev-env.sh claude`)
UPDATE=1 ./run-dev-env.sh    # rebuild only the Claude Code layer, if a newer version exists
REBUILD=1 ./run-dev-env.sh   # full rebuild (newer base image, no cache): needed after changing the Dockerfile
```
The image is built the first time any of them runs.
Claude Code starts in auto mode (`--permission-mode auto`), not with `--dangerously-skip-permissions`.

### Headless
The container has no display, audio or GPU: `run-dev-env.sh` binds no Wayland or PipeWire socket and no `/dev/dri`. Everything in it runs headless (the tests, and DOSBox-X with SDL's dummy drivers, see `docs/dosbox.md`). The GPU renderer runs there on the software Vulkan driver, without a window; `LP_NUM_THREADS=0` keeps that driver to one thread when many run side by side. Run the game itself on the host, from `build/<preset>/bin/`; so `tools/dosbox.py --window` doesn't work in the container either.

The Wayland, EGL/GLES and audio `-dev` packages stay in the Dockerfile: SDL3 only compiles a backend in when its headers are there, and the binary that runs on the host needs them.

`--userns=keep-id` maps the host user to `node`, so files in `/workspace` keep the host user's ownership. No `XDG_RUNTIME_DIR` is set. On SELinux hosts (Fedora), also add `--security-opt label=disable`.

## Build
Everything goes through CMake presets (`CMakePresets.json`). Builds are out of source, in `build/<preset>/` at the repo root, and all executables end up in `build/<preset>/bin/`. Each preset needs its own tree because sanitizer and hardening flags change how *every* object is compiled, SDL3 included.

```sh
cmake --preset asan-ubsan          # configure (the first run downloads dependencies)
cmake --build --preset asan-ubsan
ctest --preset asan-ubsan
./scripts/check.sh                 # format check + ci preset + tests + coverage
./scripts/goldens.sh               # every replay against its golden, in the release build
```

`./scripts/check.sh` is the unit tests, the lint and the coverage. `./scripts/goldens.sh` plays every replay in
the engine and compares each refresh with its golden (`replays/README.md`), in under a minute: run both before a
commit that touches the engine.

After adding state to `World` or to a kind, `./scripts/saves.sh` plays the replays with the world written out and
read back before every step (`lv-replay --saves`; `--copies` does the same with a copy): what the rewind, the
quicksave and later a network snapshot hold must be all a replay needs. By default a way through each level, the
demo, the ending and a new game (40 replays, under a minute); `--all` for every replay.

After a change to the GPU renderer's drawing, `lv-replay --check --gpu REPLAY...` plays replays with it in the
Classic renderer's place, against the same goldens (all of them take about five minutes on the software driver;
one or two are enough for most changes). What it shows when zoomed has no golden: under Remaster rules
`vikings-remaster --dump STEPS FILE.png --zoom BY AT_STEP` writes what the window would show, to look at
(`--rewind FROM UNTIL` holds the rewind over those steps; `--whole AT_STEP` writes the whole level, `--showcase N`
a spot where the effects are, `--no-effects` leaves them out).

### Windows
`./scripts/windows.sh` builds the game for Windows (ADR-0021) and checks it: the `windows` preset cross-builds
with llvm-mingw (`cmake/windows.cmake`) into `build/windows/`, then the unit tests and every replay against its
golden run as Windows programs under Wine (`cmake/wine.sh`). Not part of `check.sh`.

What comes out is one file, `build/windows/bin/vikings-remaster.exe`, for Windows 10 and later. On the Windows
PC it goes in a folder with a `gamedata/` beside it (`gamedata/original/`, and the SoundFont for the Modern
sound), which the game finds wherever it is started from. Started without `--rules`, it asks which rules in a
box. Remaster rules need Vulkan, which a graphics card's own driver has. It opens no console: an error that
stops the game is shown in a box.

## Playing
The game reads your own copy of the original files from `gamedata/original/` (or `--data DIR`), and the
SoundFont from `gamedata/`: the `gamedata` beside its program if there is one, or else the one in the folder it is
started in (the repository's, for a build in `build/<preset>/bin/`). What it writes goes beside its program:
`profiles/` (Remaster rules; `--profile DIR` for another) and `logs/` (a co-op game that went out of step), each
made when it is first written to.

```sh
./build/release/bin/vikings-remaster    # a box asks which rules; --data DIR for the game's files elsewhere
./build/release/bin/vikings-remaster --rules remaster    # Remaster rules: - and = (or the wheel) zoom out and in,
                                                         # M puts the minimap away, Backspace rewinds,
                                                         # Escape (or a pad's Start) opens the menu
```

| Preset | Purpose |
|---|---|
| `debug` | Debug, libc++ hardening `extensive` |
| `asan-ubsan` | **Default for development.** Debug + AddressSanitizer + UndefinedBehaviorSanitizer |
| `tsan` | Debug + ThreadSanitizer (once there are threads) |
| `release` | Optimised, libc++ hardening `fast`, C++ runtime linked statically (one self-contained executable, ADR-0013) |
| `coverage` | Optimised (`-O2 -g`) + clang source-based coverage of our own targets (it counts by the source, optimised or not); used by `scripts/coverage.sh` |
| `fuzz` | libFuzzer targets in `src/assets/fuzz/` (ASan + UBSan, no game, no tests) |
| `ci` | `asan-ubsan` + clang-tidy + `-Werror`, optimised (`-O2 -g`; the user, 2026-11-26: the real-data tests took far too long unoptimised) |

Fuzzing:
```sh
cmake --preset fuzz && cmake --build --preset fuzz
./build/fuzz/bin/fuzz_lzss    -max_total_time=60
./build/fuzz/bin/fuzz_archive -max_total_time=60   # seed with your DATA.DAT for best coverage
```

## Coverage
```sh
./scripts/coverage.sh                    # build the coverage preset, run the tests, report; fails below 80%
COVERAGE_HTML=1 ./scripts/coverage.sh    # also an annotated source report in build/coverage/html/
```
Clang's source-based coverage (`-fprofile-instr-generate -fcoverage-mapping`, read with `llvm-profdata` and
`llvm-cov` of the same LLVM version). Only our own targets are instrumented (`LVR_COVERAGE` in
`cmake/TargetDefaults.cmake`), and the report counts `src/` without the `tests/` and `fuzz/` folders and the
programs' `main.cpp` files. The minimum is **80% of lines**, and it holds with and without the game files (the
`[real-data]` tests add coverage but are not needed to reach it). `check.sh` runs it last.

## Importing game data
```sh
./build/asan-ubsan/bin/lv-export verify gamedata/original                    # identify release, decode everything
./build/asan-ubsan/bin/lv-export assets gamedata/original gamedata/exported  # levels/, scenes/, images/, sprites/
./build/asan-ubsan/bin/lv-export dump   gamedata/original gamedata/exported  # raw/NNN.bin + raw/index.json
```
Tests tagged `[real-data]` check our format findings against `gamedata/original/`,
and are skipped when the folder is missing.

## How the build is wired
- `cmake/Toolchain.cmake`: **global** settings, also applied to dependencies: libc++, sanitizers, libc++ hardening. Sanitizers must cover every C++ object linked together, otherwise they give false positives and miss bugs.
- `cmake/Dependencies.cmake`: FetchContent, each dependency pinned to an exact version (ADR-0009).
- `cmake/TargetDefaults.cmake`: warnings, clang-tidy and coverage instrumentation, applied to **our** targets only:
  - `lvr_target_defaults()` for `src/`.
  - `lvr_target_warnings()` for tests and fuzzers (no clang-tidy: test macros are noisy).
- Third-party code is silenced: `lvr_silence_dependency_warnings()` compiles every dependency target with `-w`, and `SYSTEM` hides warnings from their headers. Remaining known noise: on a fresh build tree, SDL's Wayland protocol generation prints 4 harmless `validity error: ... attribute frozen` lines (SDL's bundled `wayland.xml` is newer than trixie's `wayland-scanner`).
- Clang is required; configuring with another compiler fails on purpose.
- `cmake/windows.cmake`: the toolchain file of the `windows` preset (llvm-mingw); `cmake/wine.sh` runs its programs under Wine, CMake's emulator for the tests. An absolute path is passed to them as `Z:/...`: a program for Windows takes an argument that starts with `/` for an option.

## Where code goes
See [ADR-0010](decisions.md) for the full layout (shared `assets`, `audio`, `image`, `input`; the engine in `src/lvr/`; the replay harness in `src/replay/` with its replays and goldens in `replays/`). In short:
- **One file pair per domain**, e.g. `src/assets/data_dat.*` or `sprites.*`, not one per type. A new
  kind of data is a new section in an existing file.
- All targets are defined in `src/CMakeLists.txt`.
- `<dir>/tests/test_<name>.cpp` tests `<dir>/<name>.*` (e.g. `src/assets/tests/test_level.cpp`). Each test file
  is self-contained. Tests needing your game files are tagged `[real-data]`.
- All fuzz targets live in `src/assets/fuzz/fuzzers.cpp` (one `#if LVR_FUZZ_<TARGET>` block each, plus one
  `lvr_add_fuzzer()` line in `src/CMakeLists.txt`).

## Code style
- `.clang-format`: Google base, 4-space indent, 120 columns (ADR-0002). Check with `./scripts/check.sh`.
- `.clang-tidy`: a relaxed set (ADR-0002): static analyzer, bugprone, performance, modernize, a few bug-catching guideline checks, and naming rules:
  - Types: `CamelCase`
  - Functions and variables: `snake_case`
  - Constants and enumerators: `kCamelCase`
  - Private members: `trailing_`
  - Namespaces: `lvr::…`
- Files are `.hpp` / `.cpp`, side by side. Include them by their path below `src/`: `"assets/level.hpp"`.
- Errors in `assets/` are returned as `std::expected`. Untrusted input must never crash, and fuzzers enforce that.
- `clangd` reads `build/asan-ubsan/compile_commands.json` (see `.clangd`).
- **Numbers.** Hex is for what points into the original's files (an animation's address, a text's, kind's or
  sound's number, an offset in `VIKINGS.EXE`) and for bits where they are defined. Everything else is written
  as what it is: a speed is `px(2.5)`, counts, distances and waits are decimal, a mask is made of names
  (`bits(hit::kEnemy, hit::kHurtByArrow)`). A number several files need has one name, in `lvr/names.hpp` or
  `lvr/kinds.hpp`; `scripts/duplicate_constants.py` (part of `check.sh`) keeps it so.
- **Nothing of the game's own in the repo.** No text of the game, in code, comments, tests or docs: a text is
  named by its number, a test uses invented text, a doc describes a line. The game's files stay in
  `gamedata/`, which git ignores.
- **The engine's 16-bit numbers wrap by design.** `speed_x += px(2)` on a 16-bit field needs no cast (the lint's
  integer-narrowing warning is off for that); a plain assignment that narrows is an error from the compiler.
