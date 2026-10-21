#!/usr/bin/env python3
import os
import glob
import struct
import zlib
import sys

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

    # Convert 0xAARRGGBB (Little Endian in memory: B, G, R, A) to RGBA lines
    # Preserves Alpha channel: A == 0 for transparent background outside window bounds
    raw_lines = []
    transparent_count = 0
    total_pixels = width * height

    # Balanced gamma curve (1.15): lifts murky shadows and brings out textures
    # without washing out scene geometry or highlights
    lut = [min(255, max(0, int(((i / 255.0) ** (1.0 / 1.15)) * 255.0 + 0.5))) for i in range(256)]

    for y in range(height):
        line = bytearray([0]) # PNG filter byte 0 (None)
        row_offset = y * width * 4
        for x in range(width):
            px_offset = row_offset + x * 4
            b = raw_pixels[px_offset]
            g = raw_pixels[px_offset + 1]
            r = raw_pixels[px_offset + 2]
            a = raw_pixels[px_offset + 3]
            if a == 0:
                transparent_count += 1
                line.extend((0, 0, 0, 0)) # Clean transparent pixel
            else:
                line.extend((lut[r], lut[g], lut[b], a))
        raw_lines.append(bytes(line))

    compressed = zlib.compress(b"".join(raw_lines), 9)

    def make_chunk(tag, content):
        crc = zlib.crc32(tag + content) & 0xffffffff
        return struct.pack(">I", len(content)) + tag + content + struct.pack(">I", crc)

    png = [
        b"\x89PNG\r\n\x1a\n",
        make_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)), # 8-bit RGBA
        make_chunk(b"IDAT", compressed),
        make_chunk(b"IEND", b"")
    ]

    os.makedirs(os.path.dirname(png_path), exist_ok=True)
    with open(png_path, "wb") as f:
        f.write(b"".join(png))

    pct_transparent = (transparent_count / total_pixels) * 100.0 if total_pixels > 0 else 0
    print(f"Generated PNG Screenshot (Transparent BG {pct_transparent:.1f}%): {png_path} ({width}x{height} px, {os.path.getsize(png_path)} bytes)")

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
