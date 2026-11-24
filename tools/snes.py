#!/usr/bin/env python3
"""The SNES version's data, read from the user's own ROM image (docs/formats/snes.md). A prototype in Python, as the
other formats started; the importer in src/assets/ follows it.

  tools/snes.py chunks                 the chunk table: number, offset, stored size, decoded size
  tools/snes.py levels                 each PC level's SNES header and its backdrop fields
  tools/snes.py render CHUNK NAME      a level header's map and backdrop as NAME-level.png, NAME-backdrop.png
  tools/snes.py previews               one level and backdrop per distinct backdrop into gamedata/previews/snes/

The ROM: gamedata/original/*.sfc, the 1993 USA cartridge (1 MiB LoROM, SHA-256 9f7782a9...7ee1a888). Nothing of it
is written anywhere but previews under gamedata/, which is not in the repository.
"""
import pathlib
import re
import struct
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
SHA256 = "9f7782a92fda789f9d119b1f0a2f7da0f35606357556a48eca9487797ee1a888"
BASE = 0x58000  # the chunk table: (offset u16, bank u16) per chunk, from this address in 0x8000-byte banks


def load_rom():
    found = sorted((ROOT / "gamedata" / "original").glob("*.sfc"))
    if not found:
        sys.exit("no SNES ROM image (*.sfc) in gamedata/original/")
    return found[0].read_bytes()


ROM = load_rom()


def lzss(data, at, plus=0):
    size = struct.unpack_from('<H', data, at)[0] + plus
    at += 2
    window = bytearray(4096); w = 0; out = bytearray()
    while len(out) < size:
        flags = data[at]; at += 1
        for bit in range(8):
            if len(out) >= size: break
            if flags >> bit & 1:
                c = data[at]; at += 1
                out.append(c); window[w] = c; w = (w + 1) & 4095
            else:
                ref = struct.unpack_from('<H', data, at)[0]; at += 2
                pos, n = ref & 4095, (ref >> 12) + 3
                for _ in range(n):
                    if len(out) >= size: break
                    c = window[pos]; pos = (pos + 1) & 4095
                    out.append(c); window[w] = c; w = (w + 1) & 4095
    return bytes(out), at


def chunk_offset(i):
    a, b = struct.unpack_from('<HH', ROM, BASE + 4 * i)
    return BASE + b * 0x8000 + a
COUNT = (chunk_offset(0) - BASE) // 4
def chunk(i):
    """(decoded bytes, compressed size used, room to the next chunk)"""
    at = chunk_offset(i)
    end = chunk_offset(i + 1) if i + 1 < COUNT else len(ROM)
    out, stop = lzss(ROM, at)
    return out, stop - at, end - at


def png(path, w, h, rgb):
    raw = b''.join(b'\0' + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    def ch(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))

def header(c):
    h = chunk(c)[0]
    w = lambda o: struct.unpack_from('<H', h, o)[0]
    d = dict(raw=h, width=w(0x29), height=w(0x2b), tilemap=w(0x2e), tileset=w(0x30), metatiles=w(0x32),
             bg_w=w(0x34), bg_h=w(0x36), b38=h[0x38], bg_map=w(0x39), bg_tiles=w(0x3b), bg_quads=w(0x3d), fx=w(0x3f), fy=w(0x41))
    at = 0x43
    while w(at) != 0xffff: at += 14
    at += 2
    pals = []
    while w(at) != 0xffff:
        pals.append((w(at), h[at + 2])); at += 3
    d['palettes'] = pals
    return d

def cgram(pals):
    col = [(0, 0, 0)] * 256
    for entry, first in pals:
        data = chunk(entry)[0] if chunk(entry)[1] == chunk(entry)[2] else ROM[chunk_offset(entry):chunk_offset(entry + 1)]
        for i in range(len(data) // 2):
            v = struct.unpack_from('<H', data, 2 * i)[0]
            if first + i < 256:
                col[first + i] = ((v & 31) * 255 // 31, (v >> 5 & 31) * 255 // 31, (v >> 10 & 31) * 255 // 31)
    return col

def tile_pixels(tiles, n):
    t = tiles[n * 32:n * 32 + 32]
    px = []
    for y in range(8):
        b0, b1, b2, b3 = t[2 * y], t[2 * y + 1], t[16 + 2 * y], t[17 + 2 * y]
        px.append([(b0 >> (7 - x) & 1) | (b1 >> (7 - x) & 1) << 1 | (b2 >> (7 - x) & 1) << 2 | (b3 >> (7 - x) & 1) << 3 for x in range(8)])
    return px

def draw_map(mp, w, h, quads, tiles, col, out, back=(0, 0, 0)):
    W, H = w * 16, h * 16
    rgb = bytearray(bytes(back) * (W * H))
    for my in range(h):
        for mx in range(w):
            q = struct.unpack_from('<H', mp, 2 * (my * w + mx))[0] & 0x3ff
            for part in range(4):
                word = struct.unpack_from('<H', quads, q * 8 + part * 2)[0]
                if (word & 0x3ff) * 32 + 32 > len(tiles): continue
                px = tile_pixels(tiles, word & 0x3ff)
                pal = word >> 10 & 7
                for y in range(8):
                    for x in range(8):
                        c = px[7 - y if word & 0x8000 else y][7 - x if word & 0x4000 else x]
                        if c:
                            o = ((my * 16 + (part // 2) * 8 + y) * W + mx * 16 + (part % 2) * 8 + x) * 3
                            rgb[o:o + 3] = bytes(col[pal * 16 + c])
    png(out, W, H, rgb)


def stored(i):
    """A chunk's bytes: decoded, or as they are when the decoder doesn't use its room exactly (stored chunks)."""
    out, used, room = chunk(i)
    return out if used == room or i == COUNT - 1 else ROM[chunk_offset(i):chunk_offset(i) + room]


def render(c, name):
    h = header(c)
    print({k: (hex(v) if isinstance(v, int) else v) for k, v in h.items() if k != "raw"})
    col = cgram(h["palettes"])
    tiles = chunk(h["tileset"])[0]
    draw_map(chunk(h["tilemap"])[0], h["width"], h["height"], chunk(h["metatiles"])[0], tiles, col, f"{name}-level.png")
    if h["bg_map"] != 0xFFFF:
        bt = tiles if h["bg_tiles"] == 0xFFFF else chunk(h["bg_tiles"])[0]
        bq = chunk(h["bg_quads"] if h["bg_quads"] != 0xFFFF else h["metatiles"])[0]
        draw_map(chunk(h["bg_map"])[0], h["bg_w"], h["bg_h"], bq, bt, col, f"{name}-backdrop.png")


def pc_levels():
    """(level number, DATA.DAT header entry) from src/assets/catalog.hpp."""
    text = (ROOT / "src" / "assets" / "catalog.hpp").read_text()
    return list(enumerate((int(x) for x in re.findall(r'\{\d+, "[a-z0-9-]+", (\d+), 4\d\d\}', text)), 1))


def match_levels():
    """Each PC level's SNES header: the chunk whose first 0x43 bytes are most like the PC header's (needs
    gamedata/exported/raw/ from lv-export)."""
    chunks = {i: chunk(i) for i in range(COUNT)}
    chunks = {i: c[0] for i, c in chunks.items() if c[1] == c[2] and len(c[0]) >= 0x43}
    rows = []
    for number, entry in pc_levels():
        pc = (ROOT / "gamedata" / "exported" / "raw" / f"{entry:03}.bin").read_bytes()
        _, best = max((sum(a == b for a, b in zip(c[:0x43], pc[:0x43])) + (20 if len(c) == len(pc) else 0), i)
                      for i, c in chunks.items())
        rows.append((number, entry, best, len(chunks[best]) == len(pc)))
    return rows


def main():
    command = sys.argv[1] if len(sys.argv) > 1 else ""
    if command == "chunks":
        for i in range(COUNT):
            out, used, room = chunk(i)
            print(f"{i:3} {chunk_offset(i):#07x} {room:6} " + (f"{len(out):6}" if used == room else "stored"))
    elif command == "levels":
        for number, entry, c, same in match_levels():
            h = header(c)
            print(f"level {number:2}: PC {entry:3}, SNES {c:3}{'' if same else ' (differs in length)'}; backdrop "
                  f"{h['bg_w']}x{h['bg_h']} map {h['bg_map']} quads {h['bg_quads']} tiles {h['tileset']}, "
                  f"scroll {h['fx']:#06x} {h['fy']:#06x}")
    elif command == "render" and len(sys.argv) == 4:
        render(int(sys.argv[2], 0), sys.argv[3])
    elif command == "previews":
        out = ROOT / "gamedata" / "previews" / "snes"
        out.mkdir(parents=True, exist_ok=True)
        seen = set()
        for number, _, c, _ in match_levels():
            if (key := header(c)["bg_map"]) not in seen:
                seen.add(key)
                render(c, str(out / f"{number:02}"))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
