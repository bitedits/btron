#!/usr/bin/env python3
import os
import glob
import struct
import zlib
import sys
import math

def build_map_lut():
    """
    Quake Gothic S-curve for map screenshots.
    Authentic Quake lighting has strong contrast — deep shadows and warm bright
    torch halos. The BSP lightmapper bakes near-full-range values so we need to:
      • Darken the midtones (0.3–0.7) to restore shadow depth
      • Keep near-blacks dark (multiply below 0.25)
      • Slightly boost highlights (torches / sky) above 0.75
    This is a classic Quake-style gamma-below-1 with toe/shoulder rolloff.
    """
    lut = []
    for i in range(256):
        t = i / 255.0
        # Sigmoid-based contrast: pull midtones down, preserve toe/shoulder
        # contrast = 2.0 gives a moderate S-shape centred at 0.5
        contrast = 2.20
        if t < 0.0001:
            out = 0.0
        elif t > 0.9999:
            out = 1.0
        else:
            # Logistic S-curve
            x = (t - 0.5) * contrast
            s = 1.0 / (1.0 + math.exp(-x * 4.0))
            # Blend: 60% S-curve + 40% linear to prevent full crush
            out = 0.60 * s + 0.40 * t
        lut.append(min(255, max(0, int(out * 255.0 + 0.5))))
    return lut

def build_model_lut():
    """
    Gentle gamma lift for model portraits (transparent background).
    Lifts murky shadows and brings out texture detail.
    """
    return [min(255, max(0, int(((i / 255.0) ** (1.0 / 1.15)) * 255.0 + 0.5))) for i in range(256)]

# Map-screenshot filenames start with these prefixes
_MAP_PREFIXES = ("Quake_Start", "Quake_E1M", "Quake_E2M", "Quake_E3M", "Quake_E4M")

def raw_to_png(raw_path, png_path):
    with open(raw_path, "rb") as f:
        data = f.read()

    if len(data) < 8:
        return

    width, height = struct.unpack("<II", data[:8])
    raw_pixels = data[8:]

    expected_len = width * height * 4
    if len(raw_pixels) < expected_len:
        print(f"Truncated data for {raw_path}")
        return

    base = os.path.splitext(os.path.basename(raw_path))[0]
    is_map = any(base.startswith(p) for p in _MAP_PREFIXES)
    lut = build_map_lut() if is_map else build_model_lut()

    raw_lines = []
    transparent_count = 0
    total_pixels = width * height

    for y in range(height):
        line = bytearray([0])  # PNG filter byte 0 (None)
        row_offset = y * width * 4
        for x in range(width):
            px_offset = row_offset + x * 4
            b = raw_pixels[px_offset]
            g = raw_pixels[px_offset + 1]
            r = raw_pixels[px_offset + 2]
            a = raw_pixels[px_offset + 3]
            if a == 0:
                transparent_count += 1
                line.extend((0, 0, 0, 0))
            else:
                line.extend((lut[r], lut[g], lut[b], a))
        raw_lines.append(bytes(line))

    compressed = zlib.compress(b"".join(raw_lines), 9)

    def make_chunk(tag, content):
        crc = zlib.crc32(tag + content) & 0xffffffff
        return struct.pack(">I", len(content)) + tag + content + struct.pack(">I", crc)

    png = [
        b"\x89PNG\r\n\x1a\n",
        make_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)),
        make_chunk(b"IDAT", compressed),
        make_chunk(b"IEND", b"")
    ]

    os.makedirs(os.path.dirname(png_path), exist_ok=True)
    with open(png_path, "wb") as f:
        f.write(b"".join(png))

    pct_transparent = (transparent_count / total_pixels) * 100.0 if total_pixels > 0 else 0
    curve = "map-gothic" if is_map else "model-gamma"
    print(f"[{curve}] {png_path} ({width}x{height} px, {os.path.getsize(png_path)} bytes, {pct_transparent:.1f}% transparent)")

def convert_all(base_dir=None):
    if not base_dir:
        base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

    raw_files = glob.glob("/tmp/btron_raw_screens/*.raw")
    for r in sorted(raw_files):
        base = os.path.splitext(os.path.basename(r))[0]
        png_path = os.path.join(base_dir, "b-system", "img", "screens", f"{base}.png")
        raw_to_png(r, png_path)

if __name__ == "__main__":
    base = sys.argv[1] if len(sys.argv) > 1 else None
    convert_all(base)

