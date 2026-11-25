# Third-party notices

The code in this repository is under the MIT licence ([`LICENSE`](LICENSE)). It builds against the libraries
below, which are downloaded at build time (`cmake/Dependencies.cmake`, each pinned to an exact version) and keep
their own licences. A distributed binary of the game has to carry the notices of the ones linked into it; their
full licence texts are in each library's source, under `build/<preset>/_deps/` after a build.

| Library | Used for | Licence | Linked into the game |
|---|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) (Sam Lantinga and contributors) | window, input, audio device, PNG | zlib | yes |
| [SDL3_net](https://github.com/libsdl-org/SDL_net) (Sam Lantinga and contributors) | the sockets of co-op online, TCP and UDP; built with a small patch of ours (`cmake/sdl_net.patch`: TCP_NODELAY, TCP_QUICKACK) | zlib | yes |
| [ymfm](https://github.com/aaronsgiles/ymfm) (Aaron Giles) | OPL3 emulation, the Classic sound | BSD 3-Clause | yes |
| [TinySoundFont](https://github.com/schellingb/TinySoundFont) (Bernhard Schelling; based on SFZero by Steve Folta) | SoundFont synthesizer, the Modern sound | MIT | yes |
| [glaze](https://github.com/stephenberry/glaze) (Stephen Berry) | JSON, in the `lv-export` development tool | MIT | no |
| [Catch2](https://github.com/catchorg/Catch2) | the tests | BSL-1.0 | no |

Adapted into this repository (not downloaded):

- **xBR** (Hyllian, from libretro's [glsl-shaders](https://github.com/libretro/glsl-shaders), `xbr/shaders/`):
  `src/lvr/gpu_xbr.frag`, adapted from `xbr-lv2.glsl` (which incorporates ideas from Joshua Street's SABR
  shader), the screen's xbr look. MIT; its copyright and permission notice is kept at the top of the file and is
  part of the game's binary.
- **Hyllian's CRT shader** (Hyllian, from libretro's [slang-shaders](https://github.com/libretro/slang-shaders),
  `crt/shaders/hyllian/`): `src/lvr/gpu_hyllian.frag`, adapted from `crt-hyllian-base.slang` and the mask of
  `support/glow/blur-glow-mask-geom.slang`, the screen's crt look. MIT; its copyright and permission notice is kept
  at the top of the file and is part of the game's binary.

Not part of the build or of any binary, and not in this repository:

- **DOSBox-X** (GPL), patched by `tools/dosbox/` into a scriptable reference to compare the game with the
  original. A development tool only: built separately, never linked or shipped.
- **GeneralUser GS** (S. Christian Collins), the SoundFont the Modern sound uses. Loaded from a file at run time;
  see its own licence. Its author asks not to link directly to his download files.
- **The Lost Vikings** itself is owned by Blizzard Entertainment. The game reads the player's own copy of the
  original's files; none of them, and nothing derived from them, is here.
