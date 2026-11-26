#!/usr/bin/env python3
"""Runs the original game in the patched DOSBox-X (ADR-0016; design in docs/dosbox.md).

  tools/dosbox.py prepare       copy gamedata/original to build/dosbox/game/ and rewrite entry 0 there:
                                  driver 5 / variant 2 (OPL3 through driver 460) at port 0x220, and the
                                  copy-protection fingerprint of our DOSBox-X
  tools/dosbox.py run [--seconds N]
                                  run VIKINGS.EXE from the working copy, headless unless --window
  tools/dosbox.py shot FRAMES [--key FRAME:HEX ...] [--out FILE]
                                  start the game, press keys at given frames (counted from the game's start),
                                  and save the screen after FRAMES frames as a PNG

As a library (`import dosbox` with scripts/ on the path), `Game()` starts the game under the control channel
(tools/dosbox/control.cpp) stopped at its first instruction, with methods for the channel's commands.

DOSBox-X itself is built by tools/dosbox/build.sh into build/dosbox/. The user's files in gamedata/original are
only ever read.
"""

import argparse
import atexit
import os
import pathlib
import shutil
import socket
import struct
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
ORIGINAL = ROOT / "gamedata" / "original"
DOSBOX = ROOT / "build" / "dosbox" / "bin" / "dosbox-x"
CONF = ROOT / "tools" / "dosbox" / "dosbox-x.conf"
GAME = ROOT / "build" / "dosbox" / "game"
CAPTURES = ROOT / "build" / "dosbox" / "captures"
SOCKET = ROOT / "build" / "dosbox" / "control.sock"
FILES = ("DATA.DAT", "VIKINGS.EXE", "SETUP.EXE")
DATA_SEGMENT = 0x184E  # the game's data segment, relative to its load segment (docs/original/static-analysis.md)

# The fingerprint VIKINGS.EXE checks at 0000:29D4-2A13 (docs/original/game-flow.md), as a .COM that prints it as four
# hex digits: the BIOS ROM checksum (F000:0000, 0x7FF8 words, shift right then add), xor int 21h/36h for drive C
# (AX, CX, DX; skipped if AX = 0xFFFF), xor int 21h/30h (AX, BX, CX).
FINGERPRINT_COM = bytes.fromhex(
    "bf0000 b800f0 8ee8 b80000 b9f87f"  # di = 0, gs = F000, ax = 0, cx = 7FF8
    "d1e8 650305 83c702 e2f6"  # shr ax,1 / add ax,gs:[di] / add di,2 / loop
    "8bf0 ba0300 b436 cd21 3dffff 7406 33f0 33f1 33f2"  # si = sum; int 21h/36h drive C, xor ax, cx, dx
    "b80030 cd21 33c6 33c3 33c1"  # int 21h/30h; ax ^= si ^ bx ^ cx
    "8bd8 b90400"  # print bx as 4 hex digits:
    "c1c304 8ad3 80e20f 80c230 80fa39 7603 80c207 b402 cd21 e2e7"  # rol 4, digit, int 21h/02h, loop
    "b8004c cd21"  # exit
)


def dosbox_args(commands, seconds=None, settings=()):
    """The command line for DOSBox-X with the working copy mounted as C:, then the DOS commands given. `settings`:
    more "section key=value" for -set (e.g. "log vga=normal")."""
    if not DOSBOX.exists():
        sys.exit(f"{DOSBOX} not found: build it with tools/dosbox/build.sh")
    CAPTURES.mkdir(parents=True, exist_ok=True)
    args = [str(DOSBOX), "-conf", str(CONF), "-nogui", "-nomenu", "-fastlaunch", "-set", f"dosbox captures={CAPTURES}"]
    for setting in settings:
        args += ["-set", setting]
    args += ["-c", f"mount c {GAME}", "-c", "c:"]
    for command in commands:
        args += ["-c", command]
    if seconds is not None:
        args += ["-time-limit", str(seconds)]
    return args


def dosbox_env(window=False, control=None):
    env = dict(os.environ)
    if not window:
        env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    env.pop("LVR_DOSBOX_CONTROL", None)
    if control is not None:
        env["LVR_DOSBOX_CONTROL"] = str(control)
    return env


def dosbox(commands, seconds=None, window=False):
    """Runs DOSBox-X to the end (no control channel)."""
    return subprocess.run(dosbox_args(commands, seconds), env=dosbox_env(window), stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL, check=False)


class ControlError(Exception):
    pass


class Stop:
    """Where the emulation stopped: reason ("start", "frames", "break SEG:OFF", "exec LOADSEG"), frame, cs, ip."""

    def __init__(self, line):
        words = line.split()
        if not words or words[0] != "stopped":
            raise ControlError(f"expected a stop, got {line!r}")
        fields = dict(w.split("=", 1) for w in words if "=" in w)
        at = words.index("frame")
        self.reason = " ".join(words[1:at])
        self.frame = int(words[at + 1])
        self.cs = int(fields["cs"], 16)
        self.ip = int(fields["ip"], 16)

    def __repr__(self):
        return f"Stop({self.reason!r}, frame {self.frame}, {self.cs:04x}:{self.ip:04x})"


class Control:
    """DOSBox-X under the control channel, stopped where the channel opened (the first program DOS starts).
    Use it in a `with` block, or call close(): DOSBox-X is killed when the script ends, however it ends."""

    TIMEOUT = 20  # seconds for any reply; a level runs in seconds, so longer means it hangs

    def __init__(self, commands, window=False, settings=()):
        SOCKET.unlink(missing_ok=True)
        self.log = open(ROOT / "build" / "dosbox" / "dosbox-x.log", "w")  # noqa: SIM115 (lives with the process)
        self.process = subprocess.Popen(dosbox_args(commands, settings=settings), env=dosbox_env(window, SOCKET),
                                        stdin=subprocess.DEVNULL,
                                        stdout=self.log, stderr=subprocess.STDOUT)
        atexit.register(self.close)
        try:
            self._connect()
        except BaseException:
            self.close()
            raise

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def close(self):
        """Kills DOSBox-X if it still runs."""
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait()
        self.log.close()

    def _connect(self):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        for _ in range(200):
            try:
                self.sock.connect(str(SOCKET))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if self.process.poll() is not None:
                    raise ControlError(f"DOSBox-X exited ({self.process.returncode}), see {self.log.name}")
                time.sleep(0.05)
        else:
            raise ControlError("DOSBox-X never opened the control socket")
        self.sock.settimeout(self.TIMEOUT)
        self.reader = self.sock.makefile("r", encoding="ascii", newline="\n")
        self.stop = Stop(self._line())

    def _line(self):
        try:
            line = self.reader.readline()
        except TimeoutError:
            raise ControlError(f"no reply from DOSBox-X in {self.TIMEOUT} s") from None
        if not line:
            raise ControlError(f"DOSBox-X closed the channel, see {self.log.name}")
        return line.rstrip("\n")

    def command(self, line):
        """Sends a command; returns its reply after "ok"."""
        self.sock.sendall(line.encode("ascii") + b"\n")
        reply = self._line()
        if reply.startswith("err"):
            raise ControlError(f"{line!r}: {reply}")
        if not reply.startswith("ok"):
            raise ControlError(f"{line!r}: unexpected {reply!r}")
        return reply[3:]

    def run(self, frames=None):
        """Emulates until the next stop (a breakpoint, an exec, or after `frames` frames)."""
        self.sock.sendall(b"run\n" if frames is None else f"run frames {frames:x}\n".encode("ascii"))
        self.stop = Stop(self._line())
        return self.stop

    def break_add(self, seg, off):
        self.command(f"break add {seg:x}:{off:x}")

    def break_del(self, seg, off):
        self.command(f"break del {seg:x}:{off:x}")

    def break_exec(self, name):
        self.command(f"break exec {name}")

    def regs(self):
        return {k: int(v, 16) for k, v in (w.split("=") for w in self.command("regs").split())}

    def set(self, register, value):
        self.command(f"set {register} {value:x}")

    def read(self, seg, off, length):
        return bytes.fromhex(self.command(f"read {seg:x}:{off:x} {length:x}"))

    def write(self, seg, off, data):
        self.command(f"write {seg:x}:{off:x} {bytes(data).hex()}")

    def key(self, scancodes):
        self.command(f"key {bytes(scancodes).hex()}")

    def screenshot(self, out=None):
        """The screen as DOSBox-X draws the next frame: an indexed PNG; moved to `out` if given."""
        before = set(CAPTURES.glob("*.png"))
        self.command("screenshot")
        # DOSBox-X writes it when a frame has been drawn, which can be after the next frame stop.
        for _ in range(3):
            self.run(frames=1)
            new = sorted(set(CAPTURES.glob("*.png")) - before)
            if new:
                break
        else:
            raise ControlError("no screenshot written")
        if out is None:
            return new[-1]
        return pathlib.Path(shutil.move(new[-1], out))

    def opl(self, on):
        self.command(f"opl {'start' if on else 'stop'}")

    def wave(self, on):
        self.command(f"wave {'start' if on else 'stop'}")

    def quit(self):
        try:
            self.command("quit")
            self.process.wait(timeout=5)
        except (ControlError, OSError, subprocess.TimeoutExpired):
            pass
        self.close()


class Game(Control):
    """The game from the working copy, stopped at VIKINGS.EXE's first instruction. `load` = its load segment,
    `ds` = its data segment (load + 0x184E), so addresses in docs/original/ are `load + seg : off`.

    The one thing the game takes from the clock is its random seed: the start-up sets `ds:8639` from int 21h/2Ch
    (DX = seconds and hundredths, CX = hours and minutes) at 0000:297B. `seed` replaces that time, so runs repeat;
    the default 0 is the engine's starting seed. `seed=None` leaves the clock's. The BIOS tick
    count (0040:006C), which DOSBox-X starts from the host's clock, is set to 0 either way; with both, two runs with
    the same input have the same conventional memory, byte for byte (docs/dosbox.md)."""

    SEED_SET = 0x2980  # 0000:2980, right after the int 21h/2Ch, before the seed is stored

    def __init__(self, window=False, seed=0, settings=()):
        if not (GAME / "DATA.DAT").exists():
            sys.exit("no working copy: run 'tools/dosbox.py prepare' first")
        super().__init__(["VIKINGS.EXE", "exit"], window=window, settings=settings)
        try:
            self.break_exec("VIKINGS.EXE")
            stop = self.run()
            if not stop.reason.startswith("exec "):
                raise ControlError(f"expected the game's start, got {stop}")
        except BaseException:
            self.close()
            raise
        self.load = int(stop.reason.split()[1], 16)
        self.ds = self.load + DATA_SEGMENT
        self.start_frame = stop.frame
        self.write(0x0040, 0x006C, bytes(4))
        if seed is not None:
            self._set_seed(seed)

    def _set_seed(self, seed):
        self.break_add(self.load, self.SEED_SET)
        try:
            stop = self.run()
            if stop.reason != f"break {self.load:04x}:{self.SEED_SET:04x}":
                raise ControlError(f"expected the seed's start-up, got {stop}")
            self.set("dx", seed & 0xFFFF)
            self.set("cx", seed >> 16)
            self.break_del(self.load, self.SEED_SET)
        except BaseException:
            self.close()
            raise


def fingerprint():
    """The fingerprint as VIKINGS.EXE computes it in our DOSBox-X with the working copy as C:."""
    (GAME / "FP.COM").write_bytes(FINGERPRINT_COM)
    dosbox(["FP.COM > FP.TXT", "exit"], seconds=60)
    text = (GAME / "FP.TXT").read_text(errors="replace").strip()
    (GAME / "FP.COM").unlink()
    (GAME / "FP.TXT").unlink()
    if len(text) != 4:
        sys.exit(f"fingerprint program printed {text!r}")
    return int(text, 16)


def prepare(_args):
    GAME.mkdir(parents=True, exist_ok=True)
    for name in FILES:
        shutil.copyfile(ORIGINAL / name, GAME / name)
    value = fingerprint()
    data = bytearray((GAME / "DATA.DAT").read_bytes())
    entry0 = struct.unpack_from("<I", data, 0)[0]
    words = list(struct.unpack_from("<16H", data, entry0))
    if words[10] != 0x6969:
        sys.exit(f"entry 0 has no magic 0x6969: {words}")
    # Driver 460 (455 + 5), variant 2 (OPL3), the fingerprint. The port has to be given: with 0xFFFF no driver
    # starts (the game says so), see docs/formats/data-dat.md.
    words[3], words[4], words[5], words[9] = 5, 2, 0x220, value
    struct.pack_into("<16H", data, entry0, *words)
    (GAME / "DATA.DAT").write_bytes(data)
    print(f"{GAME}: entry 0 = {' '.join(f'{w:04x}' for w in words)} (fingerprint {value:04x})")


def run(args):
    if not (GAME / "DATA.DAT").exists():
        sys.exit("no working copy: run 'tools/dosbox.py prepare' first")
    result = dosbox(["VIKINGS.EXE > RUN.TXT", "exit"], seconds=args.seconds, window=args.window)
    output = (GAME / "RUN.TXT").read_text(errors="replace").strip()
    print(f"exit {result.returncode}; the game printed: {output!r}")


def shot(args):
    keys = []
    for item in args.key:
        frame, _, codes = item.partition(":")
        keys.append((int(frame), bytes.fromhex(codes)))
    keys.sort()
    game = Game(window=args.window)
    try:
        start = game.start_frame
        for frame, codes in keys:
            if frame > game.stop.frame - start:
                game.run(frames=frame - (game.stop.frame - start))
            game.key(codes)
        if args.frames > game.stop.frame - start:
            game.run(frames=args.frames - (game.stop.frame - start))
        path = game.screenshot(args.out)
        print(f"{path} (frame {game.stop.frame - start} of the game, load segment {game.load:04x})")
    finally:
        game.quit()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("prepare", help="make the working copy under build/dosbox/game").set_defaults(func=prepare)
    p = sub.add_parser("run", help="run the game from the working copy")
    p.add_argument("--seconds", type=int, help="stop DOSBox-X after this many seconds")
    p.add_argument("--window", action="store_true", help="show the window instead of running headless")
    p.set_defaults(func=run)
    p = sub.add_parser("shot", help="the screen after some frames, with key presses on the way")
    p.add_argument("frames", type=int, help="frames after the game's start")
    p.add_argument("--key", action="append", default=[], metavar="FRAME:HEX",
                   help="scan-code bytes into the keyboard at a frame (e.g. 200:1c for Enter, 202:9c release)")
    p.add_argument("--out", type=pathlib.Path, help="where to put the PNG (default: build/dosbox/captures/)")
    p.add_argument("--window", action="store_true", help="show the window instead of running headless")
    p.set_defaults(func=shot)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
