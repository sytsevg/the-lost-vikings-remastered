#!/usr/bin/env python3
"""The DOSBox spot-checks of the engine docs, run on the original in the patched DOSBox-X (docs/dosbox.md, step 6).

  tools/spotcheck.py timing      the refresh rate, the PIT divisor, the frames per pass of the wait-for-key loop,
                                   the pause menu and the inventory (docs/original/timing.md, menus.md)
  tools/spotcheck.py pause       the pause box and the playfield's darkening, from the palette (menus.md)
  tools/spotcheck.py fire-esc    Fire and Esc in the same inventory pass while choosing (menus.md)
  tools/spotcheck.py flow        each level's next slot; what follows the galaxy and the game-over scene; which
                                   slots the game ever loads (game-flow.md, formats/unknowns.md section 5)
  tools/spotcheck.py logo        the Interplay logo's last rows (formats/unknowns.md section 4)
  tools/spotcheck.py opl [--slot N] [--frames F] [--out FILE]
                                   driver 460's OPL register writes while slot N plays (default level 1), F frames
                                   from the slot's start, as DOSBox-X's raw OPL capture (.dro), with a summary

Each prints what it found. The game is driven as tools/lockstep.py drives it: the keyboard word ds:86DE is written
at each step (a tick at 0000:001E or a pass of the wait-for-key loop at 0169), with the seed 0.
"""

import argparse
import pathlib
import re
import struct
import sys
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import dosbox  # noqa: E402

ROOT = dosbox.ROOT
OUT = ROOT / "build" / "dosbox" / "spotcheck"
TICK, WAIT_ENTRY, WAIT_PASS = 0x001E, 0x0164, 0x0169
PAUSE_PASS, INVENTORY_PASS = 0x04C3, 0x1C1F
KEYBOARD, REQUESTS, SLOT, NEXT_SLOT = 0x86DE, 0x0334, 0x25AD, 0x25C9
ENTER, PAUSE, TAB, ANY_KEY = 0x8000, 0x1000, 0x2000, 0xFFFF
AIL = 0x0BAD  # the Miles AIL segment, relative to the load segment


class Run:
    """The game from its start, one step at a time: stops at 001E and 0169 (the first 0169 after 0164 belongs to the
    step that entered the loop), and at any extra stops given."""

    def __init__(self, extra=(), settings=()):
        self.game = dosbox.Game(settings=settings)
        self.extra = set(extra)
        for offset in (TICK, WAIT_ENTRY, WAIT_PASS, *self.extra):
            self.game.break_add(self.game.load, offset)
        self.written = 0
        self.steps = 0
        self.entered = False
        self._run()

    def _run(self):
        while True:
            stop = self.game.run()
            if stop.ip == WAIT_ENTRY and stop.cs == self.game.load:
                self.entered = True
                continue
            if stop.ip == WAIT_PASS and self.entered:
                self.entered = False
                continue
            return stop

    def screenshot(self, out):
        """The screen (DOSBox-X runs a frame or two for it), then on to the next step's start."""
        path = self.game.screenshot(out)
        while self.game.stop.ip not in (TICK, WAIT_PASS, *self.extra):
            self.game.run()
        return path

    def key(self, word):
        """Our bits of the keyboard word for the next step (demo playback's own bits are kept)."""
        current = self.word(KEYBOARD)
        self.game.write(self.game.ds, KEYBOARD, struct.pack("<H", (current & ~self.written) | word))
        self.written = word

    def step(self, word=0):
        self.key(word)
        self.steps += 1
        return self._run()

    def at(self):
        return self.game.stop.ip

    def frame(self):
        return self.game.stop.frame - self.game.start_frame

    def word(self, address):
        return struct.unpack("<H", self.game.read(self.game.ds, address, 2))[0]

    def set_word(self, address, value):
        self.game.write(self.game.ds, address, struct.pack("<H", value))

    def slot(self):
        return self.word(SLOT)

    def until_slot(self, slot, limit=2000, word=0):
        for _ in range(limit):
            if self.slot() == slot and self.at() == TICK:
                return
            self.step(word)
        raise SystemExit(f"slot {slot} not reached in {limit} steps (now {self.slot()})")

    def to_level1(self):
        """The menu (Enter twice), the village intro skipped (any key), level 1; its opening dialogue answered."""
        for _ in range(150):
            self.step()
        self.step(ENTER)
        for _ in range(49):
            self.step()
        self.step(ENTER)
        self.until_slot(0x2B)
        for _ in range(45):
            self.step()
        self.step(ANY_KEY)
        self.until_slot(0)
        self.answer_dialogue()

    def answer_dialogue(self, limit=40):
        for _ in range(limit):
            if self.at() == TICK and self.word(0x218F) == 0:
                for _ in range(3):  # a few ticks with nothing queued: the opening is over
                    self.step()
                    if self.at() != TICK:
                        break
                else:
                    return
            if self.at() == WAIT_PASS:
                self.step(ENTER)
                self.step()
            else:
                self.step()

    def quit(self):
        self.game.quit()


def frames_per_pass(run, stop_at, passes=8, word=0):
    """The frames between consecutive stops at `stop_at` (the run is already in that loop)."""
    frames = []
    for _ in range(passes):
        run.key(word)
        for _ in range(100):
            if run.game.run().ip == stop_at:
                break
        else:
            raise SystemExit(f"no pass of {stop_at:04X} (stopped at {run.at():04X})")
        frames.append(run.frame())
    return [b - a for a, b in zip(frames, frames[1:], strict=False)]


def timing(_args):
    run = Run(extra=(PAUSE_PASS, INVENTORY_PASS), settings=("log vga=normal",))
    try:
        run.to_level1()
        divisor = struct.unpack("<H", run.game.read(run.game.load + AIL, 0x0122, 2))[0]
        print(f"PIT divisor (L+0BAD:0122): {divisor} = 0x{divisor:04X}, {1193182 / divisor:.3f} Hz")
        ticks = []
        for _ in range(10):
            run.step()
            ticks.append(run.frame())
        print(f"frames per tick in level 1: {[b - a for a, b in zip(ticks, ticks[1:], strict=False)]}")
        run.step(PAUSE)
        if run.at() != PAUSE_PASS:
            print(f"the pause menu didn't open (stopped at {run.at():04X})")
        else:
            print(f"frames per pass of the pause menu (04C3): {frames_per_pass(run, PAUSE_PASS)}")
            run.key(PAUSE)  # Esc: NO, back to the game
            while run.game.run().ip != TICK:
                run.key(0)
        run.step(TAB)
        if run.at() != INVENTORY_PASS:
            print(f"the inventory didn't open (stopped at {run.at():04X})")
        else:
            print(f"frames per pass of the inventory (1C1F): {frames_per_pass(run, INVENTORY_PASS)}")
    finally:
        run.quit()
    log = (ROOT / "build" / "dosbox" / "dosbox-x.log").read_text(errors="replace")
    rates = re.findall(r"VGA refresh rate is now, ([0-9.]+)", log)
    sizes = re.findall(r"(\d+x\d+, [0-9.]+Hz)", log)
    print(f"VGA refresh rates DOSBox-X set: {rates}; modes: {sizes[-3:]}")
    # The wait-for-key loop: a fresh run, stopped in level 1's first dialogue wait.
    run = Run()
    try:
        for _ in range(150):
            run.step()
        run.step(ENTER)
        for _ in range(49):
            run.step()
        run.step(ENTER)
        run.until_slot(0x2B)
        for _ in range(45):
            run.step()
        run.step(ANY_KEY)
        for _ in range(200):
            if run.at() == WAIT_PASS:
                break
            run.step()
        frames = []
        for _ in range(8):
            run.step()
            frames.append(run.frame())
        print(f"frames per pass of the wait-for-key loop (0169): {[b - a for a, b in zip(frames, frames[1:], strict=False)]}")
    finally:
        run.quit()


def read_png(path):
    """A PNG as (width, height, rows of (r, g, b)); 8-bit RGB or indexed."""
    data = path.read_bytes()
    at, chunks = 8, {}
    idat = b""
    while at < len(data):
        length, kind = struct.unpack_from(">I4s", data, at)
        body = data[at + 8:at + 8 + length]
        if kind == b"IDAT":
            idat += body
        else:
            chunks.setdefault(kind, body)
        at += 12 + length
    width, height, depth, colour = struct.unpack_from(">IIBB", chunks[b"IHDR"])
    if depth != 8 or colour not in (2, 3):
        raise SystemExit(f"{path}: not an 8-bit RGB or indexed PNG")
    bpp = 3 if colour == 2 else 1
    stride = width * bpp
    raw = zlib.decompress(idat)
    rows, prev = [], bytearray(stride)
    for y in range(height):
        kind, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b, c = prev[x], prev[x - bpp] if x >= bpp else 0
            if kind == 1:
                line[x] = (line[x] + a) & 255
            elif kind == 2:
                line[x] = (line[x] + b) & 255
            elif kind == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        if colour == 3:
            plte = chunks[b"PLTE"]
            rows.append([tuple(plte[3 * i:3 * i + 3]) for i in line])
        else:
            rows.append([tuple(line[3 * x:3 * x + 3]) for x in range(width)])
    return width, height, rows


def pause(_args):
    OUT.mkdir(parents=True, exist_ok=True)
    run = Run(extra=(PAUSE_PASS,))
    try:
        run.to_level1()
        before = run.screenshot(OUT / "before-pause.png")
        run.step(PAUSE)
        frames_per_pass(run, PAUSE_PASS, passes=3)
        during = run.screenshot(OUT / "pause.png")
    finally:
        run.quit()
    w, h, rows_b = read_png(before)
    _, _, rows_d = read_png(during)
    six = lambda rgb: tuple(v >> 2 for v in rgb)  # noqa: E731 (DOSBox-X scales 6-bit VGA values to 8 bits)
    playfield = {"darker by 8": 0, "same": 0, "other": 0}
    hud = {"same": 0, "other": 0}
    box = {}
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            b, d = six(rows_b[y][x]), six(rows_d[y][x])
            if y >= 2 * 176:
                hud["same" if b == d else "other"] += 1
            elif d == tuple(max(0, v - 8) for v in b):
                playfield["darker by 8" if b != d else "same"] += 1
            else:
                playfield["other"] += 1
                box[d] = box.get(d, 0) + 1
    print(f"playfield pixels (320 x 176): {playfield}")
    print(f"HUD pixels (320 x 64): {hud}")
    print(f"colours where the playfield isn't darker by 8 (the box, 6-bit RGB): "
          f"{sorted(box.items(), key=lambda kv: -kv[1])[:6]}")
    print(f"pictures: {before}, {during}")


def fire_esc(_args):
    run = Run(extra=(INVENTORY_PASS,))
    try:
        run.to_level1()
        current = run.word(0x03C2)
        square = run.word(0x0414 + current)
        slot_address = 0x03E4 + 2 * (4 * (current // 2) + square)
        run.set_word(slot_address, 1)  # an item (1) in the current Viking's chosen square
        print(f"Viking {current // 2}, square {square}: item {run.word(slot_address)} at ds:{slot_address:04X}")
        run.step(TAB)
        if run.at() != INVENTORY_PASS:
            raise SystemExit(f"the inventory didn't open (stopped at {run.at():04X})")
        frames_per_pass(run, INVENTORY_PASS, passes=3)
        print(f"choosing: 0447 = {run.word(0x0447)}, item under the cursor 0441 = {run.word(0x0441)}")
        run.key(ENTER | PAUSE)  # Fire and Esc in the same pass
        while run.game.run().ip not in (TICK, INVENTORY_PASS):
            pass
        run.key(0)
        while run.at() != TICK:
            run.game.run()
        items = [run.word(0x03E4 + 2 * i) for i in range(12)]
        print(f"after Fire + Esc: the items {items}, 0441 = {run.word(0x0441)}, 0447 = {run.word(0x0447)}")
        run.step(TAB)
        frames_per_pass(run, INVENTORY_PASS, passes=2)
        print(f"opened again: 0441 = {run.word(0x0441)}, the items {[run.word(0x03E4 + 2 * i) for i in range(12)]}")
    finally:
        run.quit()


def load_slot(run, slot, limit=400):
    """Asks for `slot` next (ds:25C9 and request bit 1, as opcode 0F does) and plays until it is loaded."""
    run.answer_dialogue()
    while run.at() != TICK:
        run.step(ENTER if run.at() == WAIT_PASS else 0)
    run.set_word(NEXT_SLOT, slot)
    run.set_word(REQUESTS, run.word(REQUESTS) | 1)
    for _ in range(limit):
        run.step()
        if run.slot() == slot:
            break
    else:
        raise SystemExit(f"slot {slot} not reached in {limit} steps (now {run.slot()})")
    run.answer_dialogue()


def follow(run, steps, word=0):
    """The slots the game goes through in `steps` steps (step, slot, previous slot)."""
    seen = [(run.steps, run.slot(), run.word(0x25AB))]
    for _ in range(steps):
        run.step(word)
        if run.slot() != seen[-1][1]:
            seen.append((run.steps, run.slot(), run.word(0x25AB)))
    return seen


def flow(_args):
    run = Run()
    try:
        run.to_level1()
        nexts = {}
        for slot in range(37):
            load_slot(run, slot)
            nexts[slot] = run.word(NEXT_SLOT)
            print(f"  slot {slot}: next {nexts[slot]} (step {run.steps})", flush=True)
        print(f"each level's next slot (header 0x16): {nexts}")
        for last in [s for s, n in nexts.items() if n == 41][:3] + [36]:
            load_slot(run, last)
            load_slot(run, nexts[last])
            print(f"after level {last + 1} (slot {last}): {follow(run, 400)}", flush=True)
    finally:
        run.quit()
    run = Run()
    try:
        run.to_level1()
        run.set_word(REQUESTS, run.word(REQUESTS) | 2)  # game over
        print(f"game over from level 1, no keys: {follow(run, 600)}", flush=True)
        print(f"  then a key: {follow(run, 3, ENTER) + follow(run, 400)}", flush=True)
    finally:
        run.quit()


def logo(_args):
    OUT.mkdir(parents=True, exist_ok=True)
    run = Run()
    try:
        for _ in range(40):
            run.step()
        shot = run.screenshot(OUT / "interplay.png")
    finally:
        run.quit()
    w, h, rows = read_png(shot)
    # 640 x 480, each pixel doubled: the 240 rows are every second one.
    lines = [rows[2 * y][::2] for y in range(h // 2)]
    lit = [y for y in range(len(lines)) if any(p != (0, 0, 0) for p in lines[y])]
    print(f"{shot}: rows with anything but colour 0: {lit[0]}-{lit[-1]} of {len(lines)}")
    print(f"the last lit rows' colour counts: {[(y, len(set(lines[y]))) for y in lit[-4:]]}")


def dro_summary(path):
    """DOSBox raw OPL v2: header, then (register code, value) pairs; codes for delays and the high chip."""
    data = path.read_bytes()
    if data[:8] != b"DBRAWOPL":
        return f"{path}: not a DOSBox raw OPL capture"
    major, minor = struct.unpack_from("<HH", data, 8)
    if major != 2:
        return f"{path}: DRO version {major}.{minor}"
    pairs, ms, hardware, _, _, short_delay, long_delay, codes = struct.unpack_from("<IIBBBBBB", data, 12)
    table = data[26:26 + codes]
    at, writes, chips, delay = 26 + codes, 0, set(), 0
    for _ in range(pairs):
        code, value = data[at], data[at + 1]
        at += 2
        if code == short_delay:
            delay += value + 1
        elif code == long_delay:
            delay += (value + 1) << 8
        else:
            writes += 1
            chips.add(code >> 7)
    kinds = {0: "OPL2", 1: "dual OPL2", 2: "OPL3"}
    return (f"{path}: {writes} register writes over {ms} ms ({delay} ms of delays), {kinds.get(hardware, hardware)}, "
            f"chip {'s' if len(chips) > 1 else ''}{sorted(chips)}, {codes} registers used")


def opl(args):
    run = Run()
    try:
        if args.slot != 0x27:
            run.to_level1()
            if args.slot != 0:
                load_slot(run, args.slot)
        run.game.command("break clear")  # run the frames straight through
        before = set(dosbox.CAPTURES.glob("*.dro"))
        run.game.opl(True)
        run.game.run(frames=args.frames)
        run.game.opl(False)
        run.game.run(frames=2)
    finally:
        run.quit()
    new = sorted(set(dosbox.CAPTURES.glob("*.dro")) - before)
    if not new:
        raise SystemExit("no capture written (no note played?)")
    path = new[-1] if args.out is None else pathlib.Path(new[-1].replace(args.out))
    print(dro_summary(path))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="check", required=True)
    for name, func in (("timing", timing), ("pause", pause), ("fire-esc", fire_esc), ("flow", flow),
                       ("logo", logo)):
        sub.add_parser(name).set_defaults(func=func)
    p = sub.add_parser("opl")
    p.add_argument("--slot", type=int, default=0, help="the slot whose music to capture (39: from the game's start)")
    p.add_argument("--frames", type=int, default=1800, help="frames to capture (60 a second)")
    p.add_argument("--out", type=pathlib.Path, help="where to move the .dro (default: build/dosbox/captures/)")
    p.set_defaults(func=opl)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
