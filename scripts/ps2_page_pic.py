#!/usr/bin/env python3
"""scripts/ps2_page_pic.py -- render the PS2 guest's [GSDUMP] page picture as text and PNG.

The bench's live-page census (src/cores/core_ps2.c, LANE 19 rung 10) reads the whole 800x600 GS page
back over Local->Host and prints it twice as a grid of 16x16 tiles: P for the page as the
Synthesizer holds it, C for the EE canvas the compositor painted.  This script turns those rows back
into something a person looks at, because the counts beside them kept disagreeing with the operator:
a census can print "93% of this window is ONE colour" and have that filed as a full client rect.

Why a renderer and not another grep: every row the census prints is a count of pixels matching a
predicate, and the complaint has never been about a count.  It is about which colour sits where, and
that is answerable by looking.

Usage:
    scripts/ps2_page_pic.py                      # the last dump in build/ps2-bench.log
    scripts/ps2_page_pic.py build/repro/.../run1.log
    scripts/ps2_page_pic.py --which 1            # the first dump of the run instead of the last

Writes build/gspage-P.png and build/gspage-C.png (each tile as an 8x8 block, 400x304 for a 50x38
grid) and prints the legend plus both grids as text.  Exits non-zero, naming the reason, when the log
holds no complete block: rendering a truncated picture anyway is the failure mode this lane exists to
avoid, because the reader cannot tell a hole in the read from a hole in the app's picture.

Copyright 2026 Synrc Research Center. MIT License.
"""

import re
import struct
import sys
import zlib

MARKER = "[GSDUMP] the page as a picture"
CHARS = "0123456789abcdefghijklmnopqrstuv"   # the guest legend's 32 characters, in order
TILE_PX = 8                                 # a 16x16 tile renders as an 8x8 block
BLANK = 0x00000000        # ' ' -- no pixel of this tile was delivered by the read
OVERFLOW = 0xffff00ff     # '.' -- a colour the 32-slot legend could not hold


def strip_prefix(line):
    """PCSX2 writes '[  123.4567] ' at the head of every guest line; the guest writes the rest."""
    m = re.match(r"^\[\s*[\d.]+\]\s?(.*)$", line)
    return m.group(1) if m else line


def parse(lines, want):
    """The want-th dump (0-based, or -1 for the last): (cols, rows, palette, grids, ndumps)."""
    starts = [i for i, l in enumerate(lines) if MARKER in l]
    if not starts:
        raise SystemExit("no %s block in this log -- the census printed no picture.  It needs "
                         "BENCH_APP=1 BENCH_GS=2, and a census whose window is not mostly ink in its "
                         "client rect does not dump at all" % MARKER)
    if want == -1:
        want = len(starts) - 1
    if want < 0 or want >= len(starts):
        raise SystemExit("this log holds %d dump(s); --which %d asks for the %d-th"
                         % (len(starts), want, want + 1))

    head = strip_prefix(lines[starts[want]])
    m = re.search(r"(\d+)x(\d+) grid", head)
    if not m:
        raise SystemExit("the header row does not name its grid: %r" % head)
    cols, rows = int(m.group(1)), int(m.group(2))

    pal = {}
    grid = {"P": [], "C": []}
    end = starts[want + 1] if want + 1 < len(starts) else len(lines)
    for raw in lines[starts[want]:end]:
        line = strip_prefix(raw)
        m = re.match(r"\[GSDUMP\] pal (.*)$", line)
        if m:
            for tok in m.group(1).split():
                if "=" in tok:
                    k, v = tok.split("=", 1)
                    pal[k] = int(v, 16)
            continue
        m = re.match(r"\[GSDUMP\] ([PC]) (\d+) (.*)$", line)
        if m:
            grid[m.group(1)].append((int(m.group(2)), m.group(3)))
    for tag in ("P", "C"):
        if len(grid[tag]) != rows:
            raise SystemExit("dump %d: the %s grid has %d row(s) but the header says %d -- the block "
                             "is truncated or interleaved with other console traffic, so it is not "
                             "rendered" % (want + 1, tag, len(grid[tag]), rows))
        # A row of blank tiles ends in spaces, and the log is line-based, so they are re-padded here
        # rather than being read as missing columns.
        grid[tag] = dict((r, s.ljust(cols)[:cols]) for r, s in grid[tag])
    return cols, rows, pal, grid, len(starts)


def tile_colour(ch, pal):
    if ch == ".":
        return OVERFLOW
    if ch == " ":
        return BLANK
    return pal.get(ch, OVERFLOW)


def write_png(path, cols, rows, pal, grid):
    """One block per tile, through the same hand-rolled filter-0 PNG path scripts/raw_to_png.py
    uses -- the house idiom, and it needs no PIL on the host.

    The words are decoded as the GS stores them, NOT as a COLOR is written: include/btron/types.h
    says a COLOR is 0xAARRGGBB but a word read out of GS VRAM as a uint32_t is 0xAABBGGRR, red and
    blue exchanged, and the canvas holds the same order because BTRON_PIXEL_FB() does the exchange
    on the way in.  Reading these words as 0xAARRGGBB turns the XMB's deep-blue gradient into a
    brown field, which is exactly the wrong-diagnosis trap this script walked into once."""
    w, h = cols * TILE_PX, rows * TILE_PX
    data = bytearray()
    for r in range(rows):
        row = grid.get(r, " " * cols)
        line = bytearray([0])                     # PNG scanline filter: None
        for c in range(cols):
            v = tile_colour(row[c], pal)
            line += bytes((v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF)) * TILE_PX
        for _ in range(TILE_PX):                  # vertical scale
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


def main():
    args = sys.argv[1:]
    log, want = "build/ps2-bench.log", -1
    if "--which" in args:
        i = args.index("--which")
        if i + 1 >= len(args):
            raise SystemExit("--which wants a dump number (1 = the first of the run)")
        want = int(args[i + 1]) - 1
        del args[i:i + 2]
    if args:
        log = args[0]

    with open(log, errors="replace") as f:
        lines = f.read().splitlines()

    cols, rows, pal, grid, ndumps = parse(lines, want)
    order = [ch for ch in CHARS if ch in pal]

    print("%s -- dump %d of %d: %dx%d tiles of 16 px, each tile its majority colour"
          % (log, want + 1, ndumps, cols, rows))
    print("legend (words in the GS's own CT32 order, 0xAABBGGRR -- r is the LOW byte; '.' = a colour "
          "outside the 32-slot legend, ' ' = pixels the read never delivered):")
    for ch in order:
        v = pal[ch]
        print("  %c = 0x%08x  rgb=(%d,%d,%d) a=%02x"
              % (ch, v, v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF))

    for tag, name in (("P", "the GS page as read back"),
                      ("C", "the EE canvas the compositor painted")):
        print("\n%s (%s):" % (name, tag))
        for r in range(rows):
            print("  %s" % grid[tag][r])
        out = "build/gspage-%s.png" % tag
        w, h = write_png(out, cols, rows, pal, grid[tag])
        print("  wrote %s (%dx%d)" % (out, w, h))


if __name__ == "__main__":
    main()
