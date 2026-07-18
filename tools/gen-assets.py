#!/usr/bin/env python3
"""Regenerate examples/assets: a sprite sheet (32bpp BMP with alpha)
and a blip sound (16-bit mono WAV). Everything is generated
deterministically by this script - no binary blobs of unknown origin
in the repository.

Usage:  python3 tools/gen-assets.py
"""

import struct
from pathlib import Path

ASSETS = Path(__file__).resolve().parent.parent / "examples" / "assets"

# --- the sprite sheet: three 8x8 sprites side by side (24x8):
#     alien frame A, alien frame B, player ship
ALIEN_A = [
    "..#..#..",
    ".######.",
    "##.##.##",
    "########",
    "#.####.#",
    "#..##..#",
    ".#....#.",
    "#.#..#.#",
]
ALIEN_B = [
    "..#..#..",
    "#.####.#",
    "###??###".replace("?", "#"),
    "##.##.##",
    "########",
    ".######.",
    ".#....#.",
    ".#.##.#.",
]
SHIP = [
    "...##...",
    "...##...",
    "..####..",
    ".######.",
    "########",
    "########",
    "##.##.##",
    "#......#",
]
COLORS = {  # per-sprite fill color (ARGB)
    0: (0x66, 0xFF, 0x66),  # alien A: green
    1: (0x66, 0xFF, 0x66),  # alien B: green
    2: (0xFF, 0xAA, 0x33),  # ship: orange
}


def write_bmp(path: Path, w: int, h: int, pixels):
    """pixels: list of rows (top-down) of (a, r, g, b)."""
    row_size = w * 4
    data_size = row_size * h
    header = struct.pack(
        "<2sIHHI", b"BM", 14 + 108 + data_size, 0, 0, 14 + 108
    )
    # BITMAPV4HEADER for a well-defined 32bpp BGRA layout
    dib = struct.pack(
        "<IiiHHIIiiII4II36x3I",
        108, w, -h, 1, 32,  # negative height = top-down
        3,  # BI_BITFIELDS
        data_size, 2835, 2835, 0, 0,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000,  # BGRA masks
        0x73524742,  # CSType 'sRGB'
        0, 0, 0,     # gamma r/g/b (unused for sRGB)
    )
    out = bytearray(header + dib)
    for row in pixels:
        for a, r, g, b in row:
            out += struct.pack("<4B", b, g, r, a)
    path.write_bytes(bytes(out))


def sheet():
    rows = []
    sprites = [ALIEN_A, ALIEN_B, SHIP]
    for y in range(8):
        row = []
        for idx, art in enumerate(sprites):
            r, g, b = COLORS[idx]
            for ch in art[y]:
                row.append((255, r, g, b) if ch == "#" else (0, 0, 0, 0))
        rows.append(row)
    write_bmp(ASSETS / "sprites.bmp", 24, 8, rows)


def blip():
    rate, dur, freq = 8000, 0.09, 880
    n = int(rate * dur)
    samples = bytearray()
    for i in range(n):
        period = rate // freq
        v = 12000 if (i // (period // 2)) % 2 == 0 else -12000
        v = int(v * (1 - i / n))  # decay
        samples += struct.pack("<h", v)
    hdr = struct.pack(
        "<4sI4s4sIHHIIHH4sI",
        b"RIFF", 36 + len(samples), b"WAVE", b"fmt ", 16,
        1, 1, rate, rate * 2, 2, 16, b"data", len(samples),
    )
    (ASSETS / "blip.wav").write_bytes(hdr + samples)


def main():
    ASSETS.mkdir(parents=True, exist_ok=True)
    sheet()
    blip()
    print(f"wrote {ASSETS}/sprites.bmp and blip.wav")


if __name__ == "__main__":
    main()
