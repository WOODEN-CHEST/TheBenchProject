#!/usr/bin/env python3
"""Generates the placeholder art of the nine-slice test theme: white nine-part boxes a theme's colours are multiplied into.

Run it from anywhere (only the Python standard library is needed):

    python3 asset_source/scripts/placeholder_ui.py

It writes, relative to the repository root:

    compile/out/asset/textures/ui/ui_box.png     24 x 24, margins 6: a rounded box with a lighter top edge and a darker
                                                 bottom edge, for buttons and panels
    compile/out/asset/textures/ui/ui_knob.png    16 x 24, margins 5: a slider's thumb
    compile/out/asset/textures/ui/ui_track.png   12 x 6, margins 3 and 2: a slider's track and fill
    compile/out/asset/textures/ui/ui_check.png   12 x 12, margins 3: a toggle's check
    compile/out/asset/textures/ui/*.json         their texture definitions (tbp:ui_box, ...: point filtering, clamped, no
                                                 mipmaps, sRGB)

compile/out/asset/ui_themes/test_nine_slice.json names them. They are white so any colour can be multiplied in, with grey
only in the edges that show the shape.

Placeholder art for checking the code, not game art.
"""

import json
import os
import struct
import zlib


# Paths.
REPOSITORY = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
TEXTURE_DIRECTORY = os.path.join(REPOSITORY, "compile", "out", "asset", "textures", "ui")

# How bright each edge of a box is, of the white.
TOP_EDGE = 255
BODY = 224
BOTTOM_EDGE = 150


# PNG writing.
def png_chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def write_png(path, width, height, pixels):
    """pixels: width * height RGBA tuples, row by row from the top."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            rows.extend(pixels[(y * width) + x])
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    data = b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header) + png_chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    data += png_chunk(b"IEND", b"")
    with open(path, "wb") as output:
        output.write(data)


# The art.
def rounded_box(width, height, radius):
    """A rounded box: a transparent outside, a lighter top edge, a darker bottom edge, white-ish inside."""
    pixels = []
    for y in range(height):
        for x in range(width):
            # How far the pixel's centre lies outside the box whose corners are rounded by the radius.
            near_x = max(radius - (x + 0.5), (x + 0.5) - (width - radius), 0.0)
            near_y = max(radius - (y + 0.5), (y + 0.5) - (height - radius), 0.0)
            outside = ((near_x * near_x) + (near_y * near_y)) ** 0.5 - radius
            alpha = 255 if outside <= -0.5 else (0 if outside >= 0.5 else int(255 * (0.5 - outside)))
            level = BODY
            if y == 0 or (near_y > 0.0 and y < radius and outside > -1.0):
                level = TOP_EDGE
            if y == height - 1 or (near_y > 0.0 and y >= height - radius and outside > -1.0):
                level = BOTTOM_EDGE
            pixels.append((level, level, level, alpha))
    return pixels


def write_texture(name, width, height, pixels):
    write_png(os.path.join(TEXTURE_DIRECTORY, name + ".png"), width, height, pixels)
    definition = {
        "name": "tbp:" + name,
        "location": "ui/" + name,
        "texture_properties": {"filtering": "point", "wrap": "clamp", "mipmaps": False},
        "color_space": "srgb"
    }
    with open(os.path.join(TEXTURE_DIRECTORY, name + ".json"), "w", encoding="utf-8") as output:
        output.write(json.dumps(definition, indent=4) + "\n")


def main():
    os.makedirs(TEXTURE_DIRECTORY, exist_ok=True)
    write_texture("ui_box", 24, 24, rounded_box(24, 24, 5))
    write_texture("ui_knob", 16, 24, rounded_box(16, 24, 4))
    write_texture("ui_track", 12, 6, rounded_box(12, 6, 2))
    write_texture("ui_check", 12, 12, rounded_box(12, 12, 2))


if __name__ == "__main__":
    main()
