#!/usr/bin/env python3
"""scripts/ps2_page_pix.py -- render the PS2 guest's [GSPX] one-pixel-per-pixel page dump as a PNG.

The bench's live-page census (src/cores/core_ps2.c, LANE 20 rung 21) reads the whole 800x600 GS page
back over Local->Host and then prints one 96x96 square of it verbatim: eight hex digits per pixel,
centred on the GS layer's client rect, which is where the XMB puts its selected icon.  This script
turns those words back into a picture at 1:1 -- and into text, 4 px per character, for a terminal.

Why this exists beside scripts/ps2_page_pic.py: that renderer draws rung 10's grid, one character per
16x16 tile from a 32-colour legend.  An eegs page has hundreds of colours, so 1,278 of its 1,900 tiles
printed as legend overflow and the picture of the backend under test came back a magenta field.  A
half-texel bleed off an atlas cell, one flat texel, an edge a pixel late and a missing alpha ramp are
also indistinguishable in a per-tile majority.  They are not indistinguishable here.

Usage:
    scripts/ps2_page_pix.py                        # the last dump in build/ps2-bench.log
    scripts/ps2_page_pix.py build/repro/.../run1.log
    scripts/ps2_page_pix.py --which 1              # the first dump of the run instead of the last
    scripts/ps2_page_pix.py --scale 6              # nearest-neighbour upscale of the PNG
    scripts/ps2_page_pix.py --text               # the grid as characters, no PNG

Writes build/gspix-<n>.png (n = 1, 2, ... per dump in the log).  Exits non-zero, naming the reason,
when a block is incomplete: rendering a truncated picture as if it were the page is the failure mode
this lane exists to avoid.

Copyright 2026 Synrc Research Center. MIT License.
"""

import os
import re
import struct
import sys
import zlib

MARKER = "[GSPX] rect="
ROW = re.compile(r"^\[GSPX\] y=\s*(-?\d+)\s+x=\s*(-?\d+)\s*(.*)$")
HEAD = re.compile(r"^\[GSPX\] rect=(-?\d+),(-?\d+)\.\.(-?\d+),(-?\d+) (\d+)x(\d+) px")
SENT = 0x00FF00FF          # GSREAD_SENT: the read did not deliver this pixel
TILE_CHARS = " .:-=+*#%@"  # luminance ramp for --text


def strip_prefix(line):
    """PCSX2 writes '[  123.4567] ' at the head of every guest line; the guest writes the rest."""
    m = re.match(r"^\[\s*[\d.]+\]\s?(.*)$", line)
    return m.group(1) if m else line


def parse_dumps(lines):
    """Every [GSPX] block in the log, oldest first, as dicts."""
    starts = [i for i, l in enumerate(lines) if MARKER in strip_prefix(l)]
    dumps = []
    for n, s in enumerate(starts):
        end = starts[n + 1] if n + 1 < len(starts) else len(lines)
        m = HEAD.match(strip_prefix(lines[s]))
        if not m:
            raise SystemExit("dump %d: the header row does not name its rect: %r"
                             % (n + 1, strip_prefix(lines[s])))
        x0, y0, x1, y1, w, h = (int(g) for g in m.groups())
        px, rows_seen = {}, set()
        for raw in lines[s + 1:end]:
            r = ROW.match(strip_prefix(raw))
            if not r:
                continue
            y, x, rest = int(r.group(1)), int(r.group(2)), r.group(3)
            words = rest.split()
            if x + len(words) > x1:
                raise SystemExit("dump %d: the row at y=%d runs past the rect's right edge "
                                 "(%d words from x=%d, rect ends at %d)" % (n + 1, y, len(words),
                                                                            x, x1))
            for i, tok in enumerate(words):
                px[(x + i, y)] = int(tok, 16)
            rows_seen.add(y)
        need = (y1 - y0) * w
        if len(px) != need:
            raise SystemExit("dump %d: %d pixel(s) of the %dx%d rect came through, not %d -- the "
                             "block is truncated or interleaved with other console traffic, so it "
                             "is not rendered" % (n + 1, len(px), w, h, need))
        dumps.append(dict(x0=x0, y0=y0, x1=x1, y1=y1, w=w, h=h, px=px))
    return dumps


def rgb(v):
    """A page word as bytes.  NOT a COLOR: the GS stores a CT32 pixel with r in the low byte and the
    canvas holds the same order (BTRON_PIXEL_FB, include/btron/types.h), so decoding these as
    0xAARRGGBB exchanges red and blue -- the wrong-diagnosis trap scripts/ps2_page_pic.py names."""
    return (v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF)


def write_png(path, d, scale):
    w, h = d["w"] * scale, d["h"] * scale
    data = bytearray()
    for y in range(d["y0"], d["y1"]):
        for _ in range(scale):
            line = bytearray([0])                     # PNG scanline filter: None
            for x in range(d["x0"], d["x1"]):
                v = d["px"][(x, y)]
                r, g, b = (255, 0, 255) if v == SENT else rgb(v)
                line += bytes((r, g, b, 255)) * scale
            data += line

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" +
                chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(bytes(data), 9)) +
                chunk(b"IEND", b""))
    return w, h


def print_text(d):
    """The same rect as characters, 4 px per character, on a luminance ramp.  Coarse on purpose:
    this is for reading a shape's edge in a terminal, and the PNG is for judging a colour."""
    print("  +" + "-" * (d["w"] // 4))
    for y in range(d["y0"], d["y1"]):
        row = []
        for x in range(d["x0"], d["x1"], 4):
            v = d["px"][(x, y)]
            if v == SENT:
                row.append("?")
                continue
            r, g, b = rgb(v)
            lum = (r * 30 + g * 59 + b * 11) // 100
            row.append(TILE_CHARS[min(len(TILE_CHARS) - 1, lum * (len(TILE_CHARS) - 1) // 255)])
        print("y%-4d %s" % (y, "".join(row)))


def summarise(d):
    cols = {}
    for v in d["px"].values():
        cols[v] = cols.get(v, 0) + 1
    sent = cols.pop(SENT, 0)
    top = sorted(cols.items(), key=lambda kv: -kv[1])[:8]
    print("  distinct colours: %d%s" % (len(cols), "  (+%d px SENT, unread)" % sent if sent else ""))
    print("  hottest: " + ", ".join("%08x=%d" % (v, n) for v, n in top))


def main():
    args = sys.argv[1:]
    log, want, scale, text = "build/ps2-bench.log", -1, 6, False
    if "--which" in args:
        i = args.index("--which")
        if i + 1 >= len(args):
            raise SystemExit("--which wants a dump number (1 = the first of the run)")
        want = int(args[i + 1]) - 1
        del args[i:i + 2]
    if "--scale" in args:
        i = args.index("--scale")
        if i + 1 >= len(args):
            raise SystemExit("--scale wants a whole-number upscale")
        scale = int(args[i + 1])
        del args[i:i + 2]
    if "--text" in args:
        text = True
        args.remove("--text")
    if args:
        log = args[0]

    with open(log, "r", errors="replace") as f:
        lines = f.read().splitlines()
    dumps = parse_dumps(lines)
    if not dumps:
        raise SystemExit("no %s block in %s -- the census printed no pixel dump.  It needs "
                         "BENCH_APP=1 BENCH_GS=2 and a GS-layer census that is mostly ink in its "
                         "client rect (rung 10's gate); the run emits BENCH_PIC of them (Makefile, "
                         "default 2), positioned by BENCH_PIC_X/Y" % (MARKER, log))
    if want == -1:
        want = len(dumps) - 1
    if want < 0 or want >= len(dumps):
        raise SystemExit("this log holds %d dump(s); --which %d asks for the %d-th"
                         % (len(dumps), want + 1, want + 1))

    print("%s: %d dump(s); showing the %d-th" % (log, len(dumps), want + 1))
    picked = []
    for n, d in enumerate(dumps, start=1):
        if n != want + 1 and not text:
            continue
        print("dump %d: rect=%d,%d..%d,%d (%dx%d px)"
              % (n, d["x0"], d["y0"], d["x1"], d["y1"], d["w"], d["h"]))
        summarise(d)
        if text:
            print_text(d)
        else:
            path = os.path.join("build", "gspix-%d.png" % n)
            w, h = write_png(path, d, scale)
            print("  wrote %s (%dx%d, %dx upscale)" % (path, w, h, scale))
            picked.append(path)


if __name__ == "__main__":
    main()
