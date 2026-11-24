#!/usr/bin/env python3
"""Generates the small PNG files used by Tests/Resource/ImageDecoderTests.cpp.

Uses only the Python standard library (zlib, struct), so the files can be reproduced exactly:

    python3 Tests/Assets/generate_test_images.py

The pixel values written here are the expected values in ImageDecoderTests.cpp; change both
together. See Tests/Assets/README.md for what each file covers.
"""

import os
import struct
import zlib

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Images")


def chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def png(width, height, color_type, bit_depth, rows, extra_chunks=(), interlace=False):
    """rows: list of raw (unfiltered) scanline byte strings, one per row, full image."""
    header = struct.pack(">IIBBBBB", width, height, bit_depth, color_type, 0, 0, 1 if interlace else 0)
    raw = b""
    if interlace:
        raw = adam7(width, height, color_type, bit_depth, rows)
    else:
        for row in rows:
            raw += b"\x00" + row  # filter type 0 (None)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
    for kind, payload in extra_chunks:
        data += chunk(kind, payload)
    data += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    return data


def adam7(width, height, color_type, bit_depth, rows):
    assert bit_depth == 8 and color_type == 6, "only RGBA8 interlacing is generated"
    passes = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]
    out = b""
    for x0, y0, dx, dy in passes:
        xs = range(x0, width, dx)
        if not xs:
            continue
        for y in range(y0, height, dy):
            out += b"\x00" + b"".join(rows[y][x * 4:x * 4 + 4] for x in xs)
    return out


def write(name, data):
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(data)


def rgba(*pixels):
    return b"".join(bytes(p) for p in pixels)


# 4x2 RGBA8 with every interesting alpha value, including colour under alpha 0.
RGBA_4x2 = [
    [(255, 0, 0, 255), (0, 255, 0, 128), (0, 0, 255, 0), (255, 255, 255, 255)],
    [(0, 0, 0, 255), (10, 20, 30, 40), (200, 100, 50, 255), (1, 2, 3, 0)],
]

# 5x5 RGBA8 pattern for the interlaced (Adam7) file.
def interlaced_pixel(x, y):
    return (x * 50, y * 50, (x + y) * 25, 255 - x * 10 - y)


def main():
    os.makedirs(OUT, exist_ok=True)

    rgba_png = png(4, 2, 6, 8, [rgba(*row) for row in RGBA_4x2])
    write("rgba_4x2.png", rgba_png)

    write("rgb_3x1.png", png(3, 1, 2, 8, [bytes([255, 0, 0, 0, 128, 0, 12, 34, 56])]))

    # Palette with tRNS: index 0 opaque red, 1 half-transparent green, 2 fully transparent blue.
    plte = bytes([255, 0, 0, 0, 255, 0, 0, 0, 255])
    trns = bytes([255, 128, 0])
    write("palette_trns_2x2.png",
          png(2, 2, 3, 8, [bytes([0, 1]), bytes([2, 0])], extra_chunks=[(b"PLTE", plte), (b"tRNS", trns)]))

    write("gray_alpha_2x1.png", png(2, 1, 4, 8, [bytes([200, 100, 50, 255])]))

    # 1-bit greyscale: 10110010 -> white, black, white, white, black, black, white, black.
    write("gray1_8x1.png", png(8, 1, 0, 1, [bytes([0b10110010])]))

    # 16-bit RGBA declared sRGB (otherwise 16-bit PNGs are treated as linear by the decoder).
    srgb = [(b"sRGB", bytes([0]))]
    row16 = struct.pack(">8H", 0xFFFF, 0x0000, 0x8080, 0xFFFF, 0x0000, 0xFFFF, 0x0000, 0x8080)
    write("rgba16_2x1.png", png(2, 1, 6, 16, [row16], extra_chunks=srgb))

    rows = [b"".join(bytes(interlaced_pixel(x, y)) for x in range(5)) for y in range(5)]
    write("interlaced_rgba_5x5.png", png(5, 5, 6, 8, rows, interlace=True))

    # Invalid files.
    write("not_an_image.png", b"This is a text file with a .png extension.\n")
    write("truncated.png", rgba_png[: len(rgba_png) // 2])
    corrupt = bytearray(rgba_png)
    idat = corrupt.index(b"IDAT")
    corrupt[idat + 8] ^= 0xFF  # flip a byte inside the compressed data: CRC mismatch
    write("corrupt_crc.png", bytes(corrupt))


if __name__ == "__main__":
    main()
