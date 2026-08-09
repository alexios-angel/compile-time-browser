#!/usr/bin/env python3
"""Decode a PNG this engine wrote, with something that is not this engine.

include/ctbrowser/shell/images.hpp writes PNG with no compression library: the
zlib stream is made of STORED deflate blocks, which is valid but is a path most
encoders never take. "It has the right chunk names" is not evidence that a
decoder can read it - the CRCs, the Adler-32 and the block headers all have to
be right, and every one of those is silent when wrong.

So this decodes the file with Python's zlib and reports the pixels.

    tools/check/check-png.py build/render-encode.png [--expect AARRGGBB]
"""

import argparse
import struct
import sys
import zlib
from pathlib import Path


def chunks(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit("check-png: not a PNG signature")
    at = 8
    while at < len(data):
        (length,) = struct.unpack(">I", data[at:at + 4])
        kind = data[at + 4:at + 8]
        body = data[at + 8:at + 8 + length]
        (crc,) = struct.unpack(">I", data[at + 8 + length:at + 12 + length])
        want = zlib.crc32(kind + body) & 0xFFFFFFFF
        if crc != want:
            sys.exit(f"check-png: {kind.decode()} CRC is {crc:08x}, should be {want:08x}")
        yield kind.decode(), body
        at += 12 + length


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path")
    ap.add_argument("--expect", help="the colour every pixel should be, as AARRGGBB")
    args = ap.parse_args()

    data = Path(args.path).read_bytes()
    width = height = depth = colour = None
    compressed = b""
    saw_end = False
    for kind, body in chunks(data):
        if kind == "IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", body[:10])
        elif kind == "IDAT":
            compressed += body
        elif kind == "IEND":
            saw_end = True

    if not saw_end:
        sys.exit("check-png: no IEND")
    if (depth, colour) != (8, 6):
        sys.exit(f"check-png: expected 8-bit RGBA, got depth={depth} colour type={colour}")

    # This is the assertion that matters: zlib.decompress checks the Adler-32
    # and the deflate block headers, so a wrong one fails here rather than
    # somewhere in a viewer months later.
    raw = zlib.decompress(compressed)
    stride = width * 4 + 1
    if len(raw) != stride * height:
        sys.exit(f"check-png: {len(raw)} raw bytes, expected {stride * height}")

    pixels = []
    for y in range(height):
        row = raw[y * stride:(y + 1) * stride]
        if row[0] != 0:
            sys.exit(f"check-png: row {y} uses filter {row[0]}, only 0 is written")
        for x in range(width):
            r, g, b, a = row[1 + x * 4:5 + x * 4]
            pixels.append((a << 24) | (r << 16) | (g << 8) | b)

    print(f"check-png: {args.path} is {width}x{height} RGBA, {len(data)} bytes, "
          f"{len(pixels)} pixels decoded")
    print(f"           first pixel {pixels[0]:08X}, last {pixels[-1]:08X}")
    if args.expect:
        want = int(args.expect, 16)
        wrong = [f"{p:08X}" for p in pixels if p != want]
        if wrong:
            sys.exit(f"check-png: {len(wrong)} pixel(s) are not {want:08X}: {wrong[:4]}")
        print(f"           every pixel is {want:08X}")


if __name__ == "__main__":
    main()
