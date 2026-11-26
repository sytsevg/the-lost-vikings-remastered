#!/usr/bin/env python3
"""The Classic sound's notes against the original's (docs/original/sound-driver.md, "Checked in DOSBox").

  tools/oplcompare.py ORIGINAL.dro OURS.txt [--offset MS]

ORIGINAL.dro: the original's OPL3 register writes, as DOSBox-X captures them (tools/spotcheck.py opl --slot 39
--frames 36000: from the game's start, ten minutes with no key pressed: the logos, the title and its demos).
OURS.txt: ours over the same, a write a line (milliseconds, register, value: lvr_tests "[.opl]").

Every key-on is a note (its block and F-number, from its channel's registers then), lasting until its key-off. The
original's notes are paired one to one with ours of the same pitch starting within 20 ms, 15 seconds at a time,
each window with the offset between the two that pairs the most (the original takes a few refreshes more or less
than ours to load a scene, so the offset moves at each). Printed: each window's pairs, and the pairs whose lengths
are more than 30 ms apart. --offset: where to start looking (default 0; its capture starts where its first tick
is, a few seconds into the logos' tune).
"""

import argparse
import bisect
import struct

WINDOW_MS = 15000
SEARCH_MS = 3000


def dro_writes(path):
    data = open(path, "rb").read()  # noqa: SIM115
    if data[:8] != b"DBRAWOPL":
        raise SystemExit(f"{path}: not a DOSBox raw OPL capture")
    pairs, _ms = struct.unpack_from("<II", data, 12)
    short_delay, long_delay, codemap_length = data[23], data[24], data[25]
    codemap = data[26:26 + codemap_length]
    at = 26 + codemap_length
    time = 0
    writes = []
    for i in range(pairs):
        code, value = data[at + 2 * i], data[at + 2 * i + 1]
        if code == short_delay:
            time += value + 1
        elif code == long_delay:
            time += (value + 1) * 256
        else:
            writes.append((time, codemap[code & 0x7F] | (0x100 if code & 0x80 else 0), value))
    return writes


def our_writes(path):
    return [(float(ms), int(reg), int(value)) for ms, reg, value in (line.split() for line in open(path))]  # noqa: SIM115


def notes(writes):
    """(start, channel, block, F-number, length or None), by start."""
    registers, playing, found = {}, {}, []
    for time, reg, value in writes:
        registers[reg] = value
        low = reg & 0xFF
        if 0xB0 <= low <= 0xB8:
            channel = (reg & 0x0F) + (9 if reg & 0x100 else 0)
            fnumber = registers.get((reg & 0x100) | (0xA0 + (reg & 0x0F)), 0) | ((value & 3) << 8)
            block = (value >> 2) & 7
            keyed = bool(value & 0x20)
            if keyed and playing.get(channel) is None:
                playing[channel] = (time, block, fnumber)
            elif not keyed and playing.get(channel) is not None:
                start, b, f = playing[channel]
                found.append((start, channel, b, f, time - start))
                playing[channel] = None
    found += [(n[0], channel, n[1], n[2], None) for channel, n in playing.items() if n is not None]
    return sorted(found)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("original")
    parser.add_argument("ours")
    parser.add_argument("--offset", type=float, default=0.0)
    args = parser.parse_args()
    original = notes(dro_writes(args.original))
    ours = notes(our_writes(args.ours))
    by_pitch = {}
    for note in ours:
        by_pitch.setdefault((note[2], note[3]), []).append(note)
    starts = {pitch: [n[0] for n in listed] for pitch, listed in by_pitch.items()}

    def pairing(window, offset, tolerance=20):
        paired, used = [], set()
        for note in window:
            pitch = (note[2], note[3])
            listed, times = by_pitch.get(pitch, []), starts.get(pitch, [])
            i = bisect.bisect_left(times, note[0] + offset - tolerance)
            best = None
            while i < len(listed) and listed[i][0] - offset - note[0] <= tolerance:
                if (pitch, i) not in used:
                    apart = abs((listed[i][4] or 0) - (note[4] or 0))
                    if best is None or apart < best[0]:
                        best = (apart, i)
                i += 1
            if best is not None:
                used.add((pitch, best[1]))
                paired.append((note, listed[best[1]]))
        return paired

    offset, total, paired_all, apart_all = args.offset, 0, 0, 0
    start = 0.0
    while original and start < original[-1][0]:
        window = [n for n in original if start <= n[0] < start + WINDOW_MS]
        if window:
            best = max((len(pairing(window, o)), o) for o in range(int(offset - SEARCH_MS), int(offset + SEARCH_MS), 5))
            if best[0] > len(window) // 2:
                offset = best[1]
            paired = pairing(window, offset)
            apart = [(a, b) for a, b in paired if a[4] and b[4] and abs(b[4] - a[4]) > 30]
            print(f"{start / 1000:5.0f} s: {len(paired):4} of {len(window):4} paired, {len(apart):3} lengths over 30 ms "
                  f"apart (offset {offset:.0f} ms)")
            total, paired_all, apart_all = total + len(window), paired_all + len(paired), apart_all + len(apart)
        start += WINDOW_MS
    print(f"all: {paired_all} of {total} of the original's notes paired; lengths over 30 ms apart: {apart_all}")


if __name__ == "__main__":
    main()
