#!/usr/bin/env python3
"""Static analysis of the original DOS executables (ADR-0001 update; method in docs/original/static-analysis.md).

  tools/disasm.py dis 0000:001e 0000:0130     disassemble a range (end exclusive) ...
  tools/disasm.py dis 0bad:040c +40           ... or a length (hex)
  tools/disasm.py words 184e:a384 11          little-endian words (tables in the data segment)
  tools/disasm.py bytes 184e:85a5 16          raw bytes, hex + ASCII
  tools/disasm.py callers 0000:0982           near calls/jumps (same segment) and far calls to a routine,
                                                with the instructions leading up to each (-c N lines)
  tools/disasm.py refs 25c9                   every place a 16-bit value occurs, by segment (a variable,
                                                a constant, an entry number); --code / --data to filter
  tools/disasm.py strings [--min 6]           printable strings with their segment:offset ('$' ends DOS strings)
  tools/disasm.py segments                    the segments, from the relocation table
  tools/disasm.py --exe SETUP.EXE ...         any of the above on another executable in gamedata/original/

Addresses are segment:offset relative to the load segment, as in docs/original/: segment S, offset O is image byte
S*16 + O, the image being the file without its MZ header. Needs objdump (binutils) for the 8086 disassembly.
"""
import argparse
import pathlib
import re
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
ORIGINAL = ROOT / "gamedata" / "original"


class Exe:
    def __init__(self, name: str) -> None:
        path = ORIGINAL / name
        data = path.read_bytes()
        if data[:2] != b"MZ":
            sys.exit(f"{path}: not an MZ executable")
        header_paragraphs, = struct.unpack_from("<H", data, 8)
        self.image = data[header_paragraphs * 16:]
        relocation_count, = struct.unpack_from("<H", data, 6)
        relocation_table, = struct.unpack_from("<H", data, 0x18)
        segments = {0}
        for i in range(relocation_count):
            off, seg = struct.unpack_from("<HH", data, relocation_table + 4 * i)
            value, = struct.unpack_from("<H", self.image, seg * 16 + off)
            if value * 16 < len(self.image):
                segments.add(value)
        # The data segment is DS at start-up; Borland and Microsoft both load it from a relocated word, so it is one
        # of these. We take the highest segment that starts in the image as "data" (true for VIKINGS.EXE: 184E).
        self.segments = sorted(segments)

    def segment_of(self, image_offset: int) -> int:
        return max(s for s in self.segments if s * 16 <= image_offset)

    def label(self, image_offset: int) -> str:
        seg = self.segment_of(image_offset)
        return f"{seg:04x}:{image_offset - seg * 16:04x}"

    def is_data(self, image_offset: int) -> bool:
        return self.segment_of(image_offset) == self.segments[-1]

    def segment_end(self, seg: int) -> int:
        later = [s for s in self.segments if s > seg]
        return later[0] * 16 if later else len(self.image)

    def disassemble(self, seg: int, start: int, end: int) -> list[str]:
        base = seg * 16
        with tempfile.NamedTemporaryFile(suffix=".bin") as f:
            f.write(self.image)
            f.flush()
            out = subprocess.run(
                ["objdump", "-D", "-b", "binary", "-mi8086", "-M", "intel",
                 f"--start-address={base + start}", f"--stop-address={base + end}", f.name],
                capture_output=True, text=True, check=True).stdout
        lines = []
        for line in out.splitlines()[7:]:
            parts = line.split("\t")
            if len(parts) < 3:
                continue
            # objdump prints near jump/call targets as image addresses; make them segment-relative.
            text = re.sub(r"^((?:j\w+|call|loop\w*)\s+)0x([0-9a-f]+)$",
                          lambda m: f"{m.group(1)}0x{int(m.group(2), 16) - base:x}", parts[2].strip())
            address = int(parts[0].strip().rstrip(":"), 16) - base
            lines.append(f"{seg:04x}:{address:04x}  {parts[1].strip():<20} {text}")
        return lines

    def context(self, seg: int, off: int, lines: int) -> list[str]:
        """The instructions that end with the one at seg:off. x86 can't be decoded backwards, so try start points
        further and further back until a linear decode lands exactly on seg:off."""
        for back in range(min(off, 6 * lines + 8), 0, -1):
            decoded = self.disassemble(seg, off - back, off + 1)
            if decoded and decoded[-1].startswith(f"{seg:04x}:{off:04x}"):
                return decoded[-(lines + 1):]
        return self.disassemble(seg, off, off + 1)


def parse_address(text: str) -> tuple[int, int]:
    seg, off = text.split(":")
    return int(seg, 16), int(off, 16)


def cmd_dis(exe: Exe, a: argparse.Namespace) -> None:
    seg, off = parse_address(a.start)
    end = off + int(a.end[1:], 16) if a.end.startswith("+") else parse_address(a.end)[1]
    print("\n".join(exe.disassemble(seg, off, end)))


def cmd_words(exe: Exe, a: argparse.Namespace) -> None:
    seg, off = parse_address(a.start)
    base = seg * 16 + off
    words = [struct.unpack_from("<H", exe.image, base + 2 * i)[0] for i in range(int(a.count, 0))]
    for row in range(0, len(words), 16):
        print(f"{seg:04x}:{off + 2 * row:04x}  " + " ".join(f"{w:04x}" for w in words[row:row + 16]))


def cmd_bytes(exe: Exe, a: argparse.Namespace) -> None:
    seg, off = parse_address(a.start)
    base = seg * 16 + off
    data = exe.image[base:base + int(a.count, 0)]
    for row in range(0, len(data), 16):
        chunk = data[row:row + 16]
        text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        print(f"{seg:04x}:{off + row:04x}  {chunk.hex(' '):<48}  {text}")


def cmd_callers(exe: Exe, a: argparse.Namespace) -> None:
    seg, off = parse_address(a.target)
    lo, hi = seg * 16, exe.segment_end(seg)
    img = exe.image
    hits = []
    for i in range(lo, hi - 3):
        if img[i] in (0xE8, 0xE9):  # near call / jmp rel16, same segment
            rel, = struct.unpack_from("<h", img, i + 1)
            if (i - lo + 3 + rel) & 0xFFFF == off:
                hits.append((i, "call" if img[i] == 0xE8 else "jmp"))
    far = struct.pack("<BHH", 0x9A, off, seg)  # call far seg:off, from any segment
    i = img.find(far)
    while i >= 0:
        hits.append((i, "call far"))
        i = img.find(far, i + 1)
    for i, kind in sorted(hits):
        caller_seg = exe.segment_of(i) if kind == "call far" else seg
        print(f"== {kind} from {caller_seg:04x}:{i - caller_seg * 16:04x}")
        print("\n".join(exe.context(caller_seg, i - caller_seg * 16, a.context)))
    print(f"{len(hits)} caller(s); near calls through tables or registers are not found this way")


def cmd_refs(exe: Exe, a: argparse.Namespace) -> None:
    pattern = struct.pack("<H", int(a.value, 16))
    i = exe.image.find(pattern)
    count = 0
    while i >= 0:
        data = exe.is_data(i)
        if not ((a.code and data) or (a.data and not data)):
            before = exe.image[max(0, i - 3):i].hex(" ")
            print(f"{exe.label(i)}  {'data' if data else 'code'}  bytes before: {before}")
            count += 1
        i = exe.image.find(pattern, i + 1)
    print(f"{count} hit(s); in code, look at each one with 'dis' (a hit can be a false match inside other bytes)")


def cmd_strings(exe: Exe, a: argparse.Namespace) -> None:
    for m in re.finditer(rb"[\x20-\x7e\r\n]{%d,}" % a.min, exe.image):
        start = m.start()
        for piece in re.split(rb"(?<=\$)", m.group()):  # DOS strings (int 21h, ah=9) end with '$'
            if len(piece.strip()) >= a.min:
                text = piece.decode("ascii").replace("\r", "\\r").replace("\n", "\\n")
                print(f"{exe.label(start)}  {text}")
            start += len(piece)


def cmd_segments(exe: Exe, a: argparse.Namespace) -> None:
    for s in exe.segments:
        size = exe.segment_end(s) - s * 16
        print(f"{s:04x}  image {s * 16:#07x}  size {size:#07x}" + ("  (data, presumably)" if s == exe.segments[-1] else ""))


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--exe", default="VIKINGS.EXE", help="executable in gamedata/original/ (default VIKINGS.EXE)")
    sub = p.add_subparsers(dest="command", required=True)
    s = sub.add_parser("dis", help="disassemble")
    s.add_argument("start")
    s.add_argument("end", help="seg:off (exclusive) or +LENGTH (hex)")
    s.set_defaults(func=cmd_dis)
    for name, func in (("words", cmd_words), ("bytes", cmd_bytes)):
        s = sub.add_parser(name)
        s.add_argument("start")
        s.add_argument("count")
        s.set_defaults(func=func)
    s = sub.add_parser("callers")
    s.add_argument("target")
    s.add_argument("-c", "--context", type=int, default=4, help="instructions to show before each call")
    s.set_defaults(func=cmd_callers)
    s = sub.add_parser("refs")
    s.add_argument("value", help="16-bit value in hex")
    s.add_argument("--code", action="store_true", help="only hits in code segments")
    s.add_argument("--data", action="store_true", help="only hits in the data segment")
    s.set_defaults(func=cmd_refs)
    s = sub.add_parser("strings")
    s.add_argument("--min", type=int, default=6)
    s.set_defaults(func=cmd_strings)
    s = sub.add_parser("segments")
    s.set_defaults(func=cmd_segments)
    a = p.parse_args()
    a.func(Exe(a.exe), a)


if __name__ == "__main__":
    main()
