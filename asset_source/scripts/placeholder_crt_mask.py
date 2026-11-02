#!/usr/bin/env python3
"""Generates the placeholder mask tile of the test monitor's CRT: one tile of a slot mask.

Run it from anywhere (only the Python standard library is needed):

    python3 asset_source/scripts/placeholder_crt_mask.py

It writes, relative to the repository root:

    compile/out/asset/textures/crt/crt_mask.png    6 x 4, RGB: two triads of red, green and blue columns, each column
                                                   broken by a dark row (the first triad's at the tile's bottom row, the
                                                   second's two rows above it), so the slots stand staggered
    compile/out/asset/textures/crt/crt_mask.json   its texture definition (tbp:crt_mask: point filtering, repeating, no
                                                   mipmaps, sRGB)

A CRT names a tile as its "mask" (a world model's crt surface); the renderer repeats it over the window's pixels and
divides it by its mean colour, so any tile keeps the picture's mean light (its strength mixes it with white).

Placeholder art for checking the code, not game art: replace it with the owner's (any small tile, at most 64 pixels a
side).
"""

import json
import os
import struct
import zlib


# Paths.
REPOSITORY = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
TEXTURE_DIRECTORY = os.path.join(REPOSITORY, "compile", "out", "asset", "textures", "crt")

# The tile: its size, its phosphors' colours (a column each) and each triad's dark row.
WIDTH = 6
HEIGHT = 4
PHOSPHORS = [(255, 32, 32), (32, 255, 32), (48, 48, 255)]
DARK = (8, 8, 8)
DARK_ROWS = [3, 1]


# PNG writing.
def png_chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def write_png(path, width, height, pixels):
    """pixels: width * height RGB tuples, row by row from the top."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            rows.extend(pixels[(y * width) + x])
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    data = b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header) + png_chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    data += png_chunk(b"IEND", b"")
    with open(path, "wb") as output:
        output.write(data)


def tile_pixels():
    pixels = []
    for y in range(HEIGHT):
        for x in range(WIDTH):
            triad = x // len(PHOSPHORS)
            pixels.append(DARK if y == DARK_ROWS[triad] else PHOSPHORS[x % len(PHOSPHORS)])
    return pixels


def main():
    os.makedirs(TEXTURE_DIRECTORY, exist_ok=True)
    write_png(os.path.join(TEXTURE_DIRECTORY, "crt_mask.png"), WIDTH, HEIGHT, tile_pixels())
    definition = {
        "name": "tbp:crt_mask",
        "location": "crt/crt_mask",
        "texture_properties": {"filtering": "point", "wrap": "repeat", "mipmaps": False},
        "color_space": "srgb"
    }
    with open(os.path.join(TEXTURE_DIRECTORY, "crt_mask.json"), "w", encoding="utf-8") as output:
        output.write(json.dumps(definition, indent=4) + "\n")


if __name__ == "__main__":
    main()
