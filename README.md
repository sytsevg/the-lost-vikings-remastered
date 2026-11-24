# The Lost Vikings Remastered

This started as a way to learn how to remaster one of my favorite DOS games. The game is The Lost Vikings (Silicon &
Synapse, 1993), the result is a remake with the whole game running on an engine written from transcriptions of the
original's behavior. The reason I wanted to remaster it was because of the QoL improvements I wanted to add, and visual
changes to improve the experience.

The repo has no game data in it, no art and no text. You need to provide your own copy of the original DOS game
files (and, optionally, the SNES ROM) and the engine reads them as they are.

## How faithful is it?

Pixel perfect. The test for it is strict: feed the remake and the original the same key presses and compare every frame,
pixel by pixel, along with every sound the game asks for. It matches on all of the recorded runs, and those
cover every level.

Getting there took a lot of iterations to first decode the original's behavior and then replicate it accurately.

First step was to read the game assets. The archive format, the compression, the graphics, the levels, the music. All of
it is decoded straight from the original when the game starts. Nothing gets converted or saved to disk.

Next came the bytecode the game uses and the virtual machine. This required disassembly of `VIKINGS.EXE`, writing down
rules and numbers in plain words (they're in [`docs/original/`](docs/original/)). For everything visual in a running
game a patched DOSBox can drive it for pixel comparison: press these keys, stop here, render the screen.

Then came the implementation of the game engine based off the bytecode and vm. It has a small interpreter and a few
hundred scripts in bytecode. The interpreter was built first and checked against DOSBox until its memory was identical
to the original's, byte for byte. The engine was then implemented by hand, one plain C++ class for each kind of thing in
the game, and compared against the interpreter until every frame was the same. Once that was achieved, the
interpreter could be discarded. What's left of it is a set of recordings that every change still has to match.

This game is a rewrite that happens to behave the same.

## Choices made along the way

**Clean room.** Never copied decompiled code. The docs describe what the original does, with addresses as evidence, and
the code is written from the docs.

**The original's odd clock.** VGA Mode X ran at 59.71 Hz, not 60. The game does one step of logic every three screen
refreshes, which makes 19.9 steps a second.

**Integers only, no wall clock.** Same input, same result, every time. That's what makes the frame-by-frame
comparison possible.

**Two kinds of sound, out of five.** The original shipped its music and effects five times over, for five sound
devices. The remake has two, "Classic" is the OPL3 FM version, played the way the original's Sound Blaster Pro driver
played it, including its habit of cutting into the music when too many effects go off at once. "Modern" is the
General MIDI version through a SoundFont, where there have been some fixes and adjustments.

**Quirks stay, accidents go.** Everything odd that was found is in [`docs/quirks.md`](docs/quirks.md), with a note on whether it was kept.

There's more of this, with the reasoning, in [`docs/decisions.md`](docs/decisions.md). If you'd rather see how
the code is put together, start with [`docs/architecture.md`](docs/architecture.md).

## What's next

Right now it plays the game as it was. What comes next is all optional and switchable: zooming out, a
minimap, rewind, proper saves; smoother and prettier visuals; and co-op for three, one Viking each. The plan is
in [`docs/ROADMAP.md`](docs/ROADMAP.md).

## Building it

It builds on Linux. You need Podman, or your own Clang 23 with CMake and Ninja.

Put the DOS release's `DATA.DAT` and `VIKINGS.EXE` in `gamedata/original/`. If you want the Modern sound, also
put the GeneralUser GS SoundFont at `gamedata/GeneralUser-GS.sf2`. Without it it falls back to classic only.

The easy way uses the dev container, which has the whole toolchain in it:

```sh
./run-dev-env.sh build    # gives you build/release/bin/vikings-remaster
./run-dev-env.sh test     # the same, then runs every check
```

Or directly:

```sh
cmake --preset release
cmake --build --preset release
```

Then run it on your own desktop, since the container has no screen or sound:

```sh
./build/release/bin/vikings-remaster                  # from the first logo
./build/release/bin/vikings-remaster --slot 0         # straight into level 1
./build/release/bin/vikings-remaster --sound modern   # start with General MIDI
```

The keys are the original's. Arrows to move, Ctrl to switch Viking, E, S, D and F for what each of them can do,
Tab for the inventory, Esc to pause, F10 to quit. A gamepad works as well.

Everything else about building, the checks and the code style is in
[`docs/development.md`](docs/development.md).

## Want to help?

Please do. Bug reports are as welcome as code.

The most useful kind of report is "this doesn't behave like the original". Tell me the level and what you did.

If you change the engine, run `./run-dev-env.sh test` first (or `./scripts/check.sh` and `./scripts/goldens.sh`
by hand). The recordings are my only record of how the original behaves, so if one stops matching, the game has
changed, and that needs a reason.

One hard rule: nothing from the game itself goes into the repo. No data, no art, no dialogue, no decompiled
code. If you figure out something about the original, describe it in your own words in `docs/original/` and
write the code from that.

[`docs/HANDOVER.md`](docs/HANDOVER.md) says where things stand and how I work on it.

## Licence

The code is [MIT](LICENSE). The libraries it uses have their own licences, listed in
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).

The Lost Vikings belongs to Blizzard Entertainment. This project isn't affiliated with or endorsed by them, and
it doesn't include any of their game.

## A note on how this was made

This project was built with a lot of help from AI, specifically Claude (Anthropic), through Claude Code. The
reverse engineering, the engine and these docs were written together with it.
