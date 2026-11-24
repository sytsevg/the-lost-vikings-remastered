#!/usr/bin/env python3
"""Lockstep: the original (in the patched DOSBox-X) and the engine play the same input, and what each shows after
the last step is compared, pixel for pixel (docs/dosbox.md, "Lockstep comparison"). For something new or untested:
the replays' goldens only cover what was recorded before the reference engine was retired.

  tools/lockstep.py STEPS [--hold WORD] [--key STEP[-END] WORD ...] [--held STEP[-END] SCANCODE ...]
                    [--typed STEP CHARACTER ...] [--place STEP X Y ...] [--peek ADDRESS[:LENGTH] ...] [--set 'SECTION KEY=VALUE' ...]

Both start a new game at the Interplay logo (slot 39), as the original does, with the random seed 0. A step is a
tick of the main loop (the original's stop at 0000:001E), a pass of the wait-for-key loop (0000:0169), of the
pause menu's and quit box's loop (0000:04C3) or of the inventory (0000:1C1F): a step of a replay
(replays/README.md). Before each step the keyboard word ds:86DE gets the step's bits in the original (the bits of
the step before taken out, the rest left, since demo playback drives the same word). The engine gets the same
input as a replay, which is kept: build/dosbox/lockstep/run.replay.

The pictures: the original's screenshot (640 x 480, each pixel doubled) and the engine's last refresh
(original-shot.png, engine-last.png in build/dosbox/lockstep/). The exit status is 1 if they differ. To find the
first step that differs, halve STEPS. (Until 2026-11-23 this compared the reference engine's memory with the
original's, byte for byte, at every step; the engine keeps its state in its own terms, so the picture is what
there is to compare.)

Needs build/release/bin/lv-replay, DOSBox-X (tools/dosbox/build.sh) and the working copy
(tools/dosbox.py prepare).
"""

import argparse
import pathlib
import struct
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import dosbox  # noqa: E402

ROOT = dosbox.ROOT
ENGINE = ROOT / "build" / "release" / "bin" / "lv-replay"
OUT = ROOT / "build" / "dosbox" / "lockstep"
FIRST_SLOT = 39
KEYBOARD = 0x86DE  # the keyboard word the original's handler keeps (docs/original/input.md)
TICK = 0x001E  # the main loop: a tick starts here
WAIT_ENTRY = 0x0164  # the wait-for-key loop's entry; its first pass belongs to the step that entered it
WAIT_PASS = 0x0169  # each further pass of that loop is a step
MENU_PASS = 0x04C3  # a pass of the pause menu's loop (the quit box's too) is a step
INVENTORY_ENTRY = 0x1BB7  # the inventory's set-up; its first pass belongs to the step that opened it
INVENTORY_PASS = 0x1C1F  # each further pass of the inventory is a step
QUIT = 0x0E35  # the exit to DOS
TYPED = 0x028C  # the character of the key pressed last, as the keyboard handler notes it (--typed)
FIELDS = 0x14E5  # the objects' fields: an array of words per field, 0x28 apart, a word per slot (--place)
KEY_STATES = 0x916C  # the key-state array, a byte per scancode (--held)
# The original on fewer cycles and few frames drawn: about 4 times as fast, and the state per step doesn't depend on
# either. For tools that only step; a screenshot needs every frame drawn.
FAST = ("cpu cycles=fixed 2000", "render frameskip=10")


def compare_pictures(original_path, engine_path):
    """How the original's screenshot (each pixel doubled) and the engine's picture differ, as colours of 6 bits."""
    import spotcheck  # noqa: PLC0415 (its PNG reader; it imports dosbox too)
    ow, oh, original = spotcheck.read_png(original_path)
    rw, rh, engine = spotcheck.read_png(engine_path)
    if (ow, oh) != (2 * rw, 2 * rh):
        return f"pictures: sizes differ, original {ow} x {oh}, engine {rw} x {rh}"
    differing = []
    for y in range(rh):
        for x in range(rw):
            a = tuple(c >> 2 for c in original[2 * y][2 * x])
            b = tuple(c >> 2 for c in engine[y][x])
            if a != b:
                differing.append((x, y, a, b))
    if not differing:
        return f"pictures: the same ({rw} x {rh})"
    xs, ys = [d[0] for d in differing], [d[1] for d in differing]
    sample = ", ".join(f"({x},{y}) {a}/{b}" for x, y, a, b in differing[:4])
    return (f"pictures: {len(differing)} pixels differ, x {min(xs)}-{max(xs)}, y {min(ys)}-{max(ys)}; "
            f"original/engine {sample}")


def spans(steps):
    """Steps as (first, last) runs."""
    runs = []
    for step in sorted(steps):
        if runs and step == runs[-1][1] + 1:
            runs[-1][1] = step
        else:
            runs.append([step, step])
    return runs


def write_replay(args, path):
    """The run's input as a replay for the engine."""
    lines = ["# Written by tools/lockstep.py: the input of its last run.", f"slot {FIRST_SLOT}", f"steps {args.steps}"]
    if args.hold and args.steps:
        lines.append(f"key 0-{args.steps - 1} {args.hold:#x}")
    words = {}
    for step, word in args.key:
        if step < args.steps:  # input after the last step is left out: a replay has no line past its end
            words.setdefault(word, []).append(step)
    for word, steps in words.items():
        lines += [f"key {first}-{last} {word:#x}" for first, last in spans(steps)]
    lines += [f"held {step} {scancode:#x}" for step, scancode in args.held if step < args.steps]
    lines += [f"typed {step} {character:#x}" for step, character in args.typed if step < args.steps]
    lines += [f"place {step} {x} {y}" for step, x, y in args.place if step < args.steps]
    path.write_text("\n".join(lines) + "\n")


def place_vikings(game, x, y):
    """The Vikings (slots 0 to 2) put at (x, y): fields 1E and 20, and their boxes (00 to 06) moved along."""
    def address(field, slot):
        return FIELDS + (field // 2) * 0x28 + 2 * slot

    def word(field, slot):
        return struct.unpack("<h", game.read(game.ds, address(field, slot), 2))[0]

    def put(field, slot, value):
        game.write(game.ds, address(field, slot), struct.pack("<H", value & 0xFFFF))

    for slot in range(3):
        dx, dy = x - word(0x1E, slot), y - word(0x20, slot)
        put(0x1E, slot, x)
        put(0x20, slot, y)
        for field, by in ((0x00, dy), (0x02, dy), (0x04, dx), (0x06, dx)):
            put(field, slot, word(field, slot) + by)


def word_at(args, step):
    word = args.hold
    for at, bits in args.key:
        if at == step:
            word |= bits
    return word


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("steps", type=int, help="how many steps to play")
    parser.add_argument("--hold", type=lambda t: int(t, 0), default=0, help="keyboard word held every step")
    parser.add_argument("--key", nargs=2, action="append", default=[], metavar=("STEP[-END]", "WORD"),
                        help="keyboard bits at that step only, or held from STEP to END inclusive (repeatable)")
    parser.add_argument("--held", nargs=2, action="append", default=[], metavar=("STEP[-END]", "SCANCODE"),
                        help="a key of the key-state array held at that step (or STEP to END); repeatable")
    parser.add_argument("--typed", nargs=2, action="append", default=[], metavar=("STEP", "CHARACTER"),
                        help="a character typed before that step (a letter or digit, or its code): the title's "
                             "menu keys, a password's letters; repeatable")
    parser.add_argument("--place", nargs=3, action="append", default=[], metavar=("STEP", "X", "Y"),
                        help="the three Vikings put at that point of the level before that step, their boxes with "
                             "them, as a replay's `place` does: to get beside something; repeatable")
    parser.add_argument("--peek", action="append", default=[], metavar="ADDRESS[:LENGTH]",
                        help="after the last step, print the original's word at ds:ADDRESS, or save LENGTH bytes "
                             "to peek-ADDRESS.bin; repeatable")
    parser.add_argument("--set", action="append", default=[], metavar="'SECTION KEY=VALUE'",
                        help="a DOSBox-X setting for this run (repeatable), e.g. 'cpu cycles=fixed 8000'")
    args = parser.parse_args()
    keys = []
    for steps, word in args.key:
        first, _, last = steps.partition("-")
        keys += [(step, int(word, 0)) for step in range(int(first, 0), int(last or first, 0) + 1)]
    args.key = keys
    held = []
    for steps, scancode in args.held:
        first, _, last = steps.partition("-")
        held += [(step, int(scancode, 0)) for step in range(int(first, 0), int(last or first, 0) + 1)]
    args.held = held
    args.typed = [(int(step, 0), ord(text.upper()) if len(text) == 1 else int(text, 0)) for step, text in args.typed]
    args.place = [(int(step, 0), int(x, 0), int(y, 0)) for step, x, y in args.place]
    if not ENGINE.exists():
        sys.exit(f"{ENGINE} not found: cmake --build --preset release")
    OUT.mkdir(parents=True, exist_ok=True)
    write_replay(args, OUT / "run.replay")
    result = subprocess.run([str(ENGINE), "--shot", str(OUT / "engine-last.png"), str(OUT / "run.replay")],
                            capture_output=True, text=True, timeout=600, check=False)
    if result.returncode != 0:
        sys.exit(f"the engine failed: {(result.stderr or result.stdout).strip()}")

    game = dosbox.Game(settings=tuple(args.set))  # at full speed: frameskip would delay the screenshot
    verdict = ""
    try:
        for offset in (TICK, WAIT_ENTRY, WAIT_PASS, MENU_PASS, QUIT, INVENTORY_ENTRY, INVENTORY_PASS):
            game.break_add(game.load, offset)
        game.run()
        entered_wait = None  # the loop entry seen last, until its first pass
        step = 0
        written = 0  # the bits we last put into the keyboard word
        held_scancodes = []  # the key-state bytes we last set
        while True:
            at = game.stop.ip
            if at in (WAIT_ENTRY, INVENTORY_ENTRY):
                entered_wait = at
                game.run()
                continue
            # The first pass: still the step that entered the loop.
            if (at, entered_wait) in ((WAIT_PASS, WAIT_ENTRY), (INVENTORY_PASS, INVENTORY_ENTRY)):
                entered_wait = None
                game.run()
                continue
            if at == QUIT:
                verdict = f"step {step}: the original quit"
                break
            if step == args.steps:
                for peek in args.peek:
                    first, _, size = peek.partition(":")
                    address, length = int(first, 0), int(size, 0) if size else None
                    if length:
                        (OUT / f"peek-{address:04X}.bin").write_bytes(game.read(game.ds, address, length))
                        print(f"ds:{address:04X}+{length:X} -> {OUT / f'peek-{address:04X}.bin'}")
                    else:
                        value, = struct.unpack("<H", game.read(game.ds, address, 2))
                        print(f"ds:{address:04X} = {value:04X} ({value})")
                shot = game.screenshot(OUT / "original-shot.png")
                verdict = compare_pictures(shot, OUT / "engine-last.png")
                break
            # Only our own bits change: demo playback (2D72) drives the same word, as the engine ORs its keys in.
            current, = struct.unpack("<H", game.read(game.ds, KEYBOARD, 2))
            word = word_at(args, step)
            game.write(game.ds, KEYBOARD, struct.pack("<H", (current & ~written) | word))
            written = word
            for scancode in held_scancodes:  # the key-state array: our keys only, as the handler would leave them
                game.write(game.ds, KEY_STATES + scancode, b"\0")
            for at_step, character in args.typed:
                if at_step == step:
                    game.write(game.ds, TYPED, struct.pack("<H", character))
            for at_step, x, y in args.place:
                if at_step == step:
                    place_vikings(game, x, y)
            held_scancodes = [scancode for at_step, scancode in args.held if at_step == step]
            for scancode in held_scancodes:
                game.write(game.ds, KEY_STATES + scancode, b"\1")
            game.run()
            step += 1
    finally:
        game.quit()
    print(f"after {step} steps: {verdict}")
    sys.exit(0 if "the same" in verdict else 1)


if __name__ == "__main__":
    main()
