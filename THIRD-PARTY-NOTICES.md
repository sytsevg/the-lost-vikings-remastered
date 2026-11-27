# Third-party notices

The code in this repository is under the MIT licence ([`LICENSE`](LICENSE)). It builds against the libraries
below, which are downloaded at build time (`cmake/Dependencies.cmake`, each pinned to an exact version) and keep
their own licences. A distributed binary of the game has to carry the notices of the ones linked into it; their
full licence texts are in each library's source, under `build/<preset>/_deps/` after a build, and the release
archives (`make release`, `scripts/release.sh`) carry them in a `licenses/` folder beside the program.

| Library | Used for | Licence | Linked into the game |
|---|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) (Sam Lantinga and contributors) | window, input, audio device, PNG | zlib | yes |
| [SDL3_net](https://github.com/libsdl-org/SDL_net) (Sam Lantinga and contributors) | the sockets of co-op online, TCP and UDP; built with a small patch of ours (`cmake/sdl_net.patch`: TCP_NODELAY, TCP_QUICKACK, and two names for mingw-w64) | zlib | yes |
| [ymfm](https://github.com/aaronsgiles/ymfm) (Aaron Giles) | OPL3 emulation, the Classic sound | BSD 3-Clause | yes |
| [TinySoundFont](https://github.com/schellingb/TinySoundFont) (Bernhard Schelling; based on SFZero by Steve Folta) | SoundFont synthesizer, the Modern sound | MIT | yes |
| [glaze](https://github.com/stephenberry/glaze) (Stephen Berry) | JSON, in the `lv-export` development tool | MIT | no |
| [Catch2](https://github.com/catchorg/Catch2) | the tests | BSL-1.0 | no |

Linked into the Windows build by its toolchain (llvm-mingw, in the dev container):

- **mingw-w64's runtime** (the mingw-w64 project): the program's start-up code and what joins it to Windows' C
  runtime. Its own licence, BSD-like; some parts under others. A distributed `.exe` has to carry their notices,
  which mingw-w64 keeps together in `COPYING.MinGW-w64-runtime/COPYING.MinGW-w64-runtime.txt` in its source.
  (libc++ and libunwind, from LLVM, are under the Apache License 2.0 with LLVM Exceptions, which asks for no
  notice with a binary.)

Adapted into this repository (not downloaded):

- **xBR** (Hyllian, from libretro's [glsl-shaders](https://github.com/libretro/glsl-shaders), `xbr/shaders/`):
  `src/lvr/gpu_xbr.frag`, adapted from `xbr-lv2.glsl` (which incorporates ideas from Joshua Street's SABR
  shader), the screen's xbr look. MIT; its copyright and permission notice is kept at the top of the file (the
  compiled shader has no comments, so the release archives carry it in `licenses/`).
- **Hyllian's CRT shader** (Hyllian, from libretro's [slang-shaders](https://github.com/libretro/slang-shaders),
  `crt/shaders/hyllian/`): `src/lvr/gpu_hyllian.frag`, adapted from `crt-hyllian-base.slang` and the mask of
  `support/glow/blur-glow-mask-geom.slang`, the screen's crt look. MIT; its copyright and permission notice is kept
  at the top of the file, and in the release archives' `licenses/`.

Shipped beside the game in the release archives, not in this repository:

- **GeneralUser GS** 2.0.3 (S. Christian Collins, <https://www.schristiancollins.com>), the SoundFont the Modern
  sound uses: `gamedata/GeneralUser-GS.sf2`, loaded from the file at run time. Its licence (v2.0), which allows
  using it in software and providing a copy of it, is carried in the file itself; the release script writes it
  beside it as `gamedata/GeneralUser-GS-LICENSE.txt`. Its author asks not to link directly to his download files:
  link his website.

Not part of the build or of any binary, and not in this repository:

- **DOSBox-X** (GPL), patched by `tools/dosbox/` into a scriptable reference to compare the game with the
  original. A development tool only: built separately, never linked or shipped.
- **The Lost Vikings** itself is owned by Blizzard Entertainment. The game reads the player's own copy of the
  original's files; none of them, and nothing derived from them, is here, but for the README's screenshots of the
  remake (`docs/screenshots/`).
