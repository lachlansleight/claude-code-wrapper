#!/usr/bin/env python3
"""Render Sultana Dock's static app icon from a frame the firmware drew.

    python3 make_app_icon.py FRAME.png OUT_DIR

FRAME.png is a 240x240 screen written by `sultana-replay --frames` (8-bit
RGB, unfiltered scanlines). OUT_DIR receives the AppIcon.appiconset PNGs
and Contents.json. Uses only the standard library.

The framing (screen and bezel diameters as fractions of the tile) matches
FaceTileView in DockTile.swift, so the icon and the live tile line up.
"""
import json
import os
import struct
import sys
import zlib

SCREEN_FRACTION = 0.80
BEZEL_FRACTION = 0.86
BEZEL_RGB = (0x23, 0x23, 0x26)
SUPERSAMPLE = 4

SIZES = [(16, 1), (16, 2), (32, 1), (32, 2), (128, 1), (128, 2), (256, 1), (256, 2), (512, 1), (512, 2)]


def read_png_rgb(path):
    data = open(path, "rb").read()
    pos, idat, width, height = 8, b"", 0, 0
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", body[:10])
            if depth != 8 or colour != 2:
                sys.exit("expected an 8-bit RGB PNG from sultana-replay")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = 1 + width * 3
    rows = []
    for y in range(height):
        if raw[y * stride] != 0:
            sys.exit("expected unfiltered scanlines (sultana-replay output)")
        line = raw[y * stride + 1:(y + 1) * stride]
        rows.append([tuple(line[x * 3:x * 3 + 3]) for x in range(width)])
    return rows, width, height


def write_png_rgba(path, pixels, size):
    raw = b"".join(b"\x00" + bytes(v for p in row for v in p) for row in pixels)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def render(face, fw, fh, size):
    centre = size / 2
    screen_r = size * SCREEN_FRACTION / 2
    bezel_r = size * BEZEL_FRACTION / 2
    out = []
    for y in range(size):
        row = []
        for x in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(SUPERSAMPLE):
                for sx in range(SUPERSAMPLE):
                    px = x + (sx + 0.5) / SUPERSAMPLE
                    py = y + (sy + 0.5) / SUPERSAMPLE
                    d = ((px - centre) ** 2 + (py - centre) ** 2) ** 0.5
                    if d <= screen_r:
                        u = (px - (centre - screen_r)) / (2 * screen_r)
                        v = (py - (centre - screen_r)) / (2 * screen_r)
                        c = face[min(fh - 1, int(v * fh))][min(fw - 1, int(u * fw))]
                    elif d <= bezel_r:
                        c = BEZEL_RGB
                    else:
                        continue
                    acc[0] += c[0]; acc[1] += c[1]; acc[2] += c[2]; acc[3] += 1
            n = SUPERSAMPLE * SUPERSAMPLE
            if acc[3] == 0:
                row.append((0, 0, 0, 0))
            else:
                row.append((round(acc[0] / acc[3]), round(acc[1] / acc[3]), round(acc[2] / acc[3]),
                            round(255 * acc[3] / n)))
        out.append(row)
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    face, fw, fh = read_png_rgb(sys.argv[1])
    out_dir = sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    images, cache = [], {}
    for points, scale in SIZES:
        px = points * scale
        name = f"icon_{points}x{points}{'@2x' if scale == 2 else ''}.png"
        if px not in cache:
            cache[px] = render(face, fw, fh, px)
        write_png_rgba(os.path.join(out_dir, name), cache[px], px)
        images.append({"filename": name, "idiom": "mac", "scale": f"{scale}x", "size": f"{points}x{points}"})
        print("wrote", name)
    with open(os.path.join(out_dir, "Contents.json"), "w") as f:
        json.dump({"images": images, "info": {"author": "xcode", "version": 1}}, f, indent=2)
        f.write("\n")


if __name__ == "__main__":
    main()
