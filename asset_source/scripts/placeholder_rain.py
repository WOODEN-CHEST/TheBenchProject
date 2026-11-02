#!/usr/bin/env python3
"""Generates the placeholder art of the rain: a streak, a splash's animation, the puddle mask and lightning bolts.

Run it from anywhere (only the Python standard library is needed):

    python3 asset_source/scripts/placeholder_rain.py

It writes, relative to the repository root:

    compile/out/asset/textures/rain/streak.png                 4 x 32, RGBA: a streak along its length (the drop at
                                                               the top)
    compile/out/asset/textures/rain/puddle_mask.png            256 x 256, greyscale, tileable: where water pools first
                                                               (high in the puddles' middles), spread evenly over
                                                               0 .. 255
    compile/out/asset/textures/rain/<name>.json                their texture definitions (tbp:rain_streak,
                                                               tbp:rain_puddle_mask)
    compile/out/asset/sprite_animations/rain/splash_<n>.png    16 x 16, RGBA, n 0 to 3: a splash's frames, from the
                                                               drop's impact to its last spray (standing on the bottom
                                                               row)
    compile/out/asset/sprite_animations/rain/splash.json       the sprite animation tbp:rain_splash: those frames at
                                                               20 fps (a splash lasts 0.2 s)
    compile/out/asset/sprite_animations/rain/bolt_<n>.png      128 x 512, RGBA, n 0 to 3: a bolt each, from the clouds
                                                               (top) to the ground (bottom), branching, white with a
                                                               glow
    compile/out/asset/sprite_animations/rain/bolts.json        the sprite animation tbp:lightning_bolts: one frame per
                                                               bolt (a strike shows one; no fps)

The puddle mask tiles (periodic noise) and is spread evenly over its range by its histogram, so a threshold t keeps 1 - t
of its texels: the renderer's puddle coverage means what it says (WorldRain.h, "PUDDLES").

Placeholder art for checking the code, not game art: replace it with the owner's (a world names its own in its
environment: rain_streak_texture and puddle_mask_texture name textures, rain_splash_animation and
lightning_bolt_animation sprite animations, whose frames may have any number, size and source).
"""

import json
import math
import os
import random
import struct
import zlib


# Paths.
REPOSITORY = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
TEXTURE_DIRECTORY = os.path.join(REPOSITORY, "compile", "out", "asset", "textures", "rain")
ANIMATION_DIRECTORY = os.path.join(REPOSITORY, "compile", "out", "asset", "sprite_animations", "rain")
SEED = 1811


# PNG writing.
def png_chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def write_png(path, width, height, channels, pixels):
    """pixels: width * height tuples (or ints for one channel), row by row from the top."""
    colour_type = {1: 0, 4: 6}[channels]
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            value = pixels[(y * width) + x]
            if channels == 1:
                rows.append(value)
            else:
                rows.extend(value)
    header = struct.pack(">IIBBBBB", width, height, 8, colour_type, 0, 0, 0)
    data = b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header) + png_chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    data += png_chunk(b"IEND", b"")
    with open(path, "wb") as output:
        output.write(data)


def write_definition(file_name, name, location, properties, colour_space):
    definition = {"name": name, "location": location, "texture_properties": properties, "color_space": colour_space}
    with open(os.path.join(TEXTURE_DIRECTORY, file_name), "w", encoding="utf-8") as output:
        output.write(json.dumps(definition, indent=4) + "\n")


def write_animation(file_name, name, frame_locations, filtering, fps):
    frames = [{"source": "texture", "location": location, "texture_properties": {"filtering": filtering}}
        for location in frame_locations]
    definition = {"name": name, "frames": frames}
    if fps > 0:
        definition["fps"] = fps
    with open(os.path.join(ANIMATION_DIRECTORY, file_name), "w", encoding="utf-8") as output:
        output.write(json.dumps(definition, indent=4) + "\n")


def crop(width, pixels, left, top, crop_width, crop_height):
    """A crop_width x crop_height part of an image width pixels wide, from its column left and its row top."""
    return [pixels[((top + y) * width) + left + x] for y in range(crop_height) for x in range(crop_width)]


def clamp(value, low=0.0, high=1.0):
    return max(low, min(high, value))


def smooth(t):
    return t * t * (3.0 - (2.0 * t))


def to_byte(value):
    return int(round(clamp(value) * 255.0))


# The streak: white, its alpha along its length; strongest near the drop, thinning toward the end of its trail, with soft
# ends so a streak never starts or stops on a hard pixel.
def streak():
    width, height = 4, 32
    pixels = []
    for y in range(height):
        t = (y + 0.5) / height
        alpha = (1.0 - (0.45 * t)) * min(1.0, t / 0.12, (1.0 - t) / 0.2)
        for _ in range(width):
            pixels.append((255, 255, 255, to_byte(alpha)))
    return width, height, pixels


# The splash: four frames of a crown of water thrown up where a drop lands, rising, spreading and falling apart.
def splash():
    size, frames = 16, 4
    width = size * frames
    pixels = [(255, 255, 255, 0)] * (width * size)
    rng = random.Random(SEED)
    droplets = [(rng.uniform(-1.0, 1.0), rng.uniform(0.55, 1.0)) for _ in range(9)]

    def put(frame, x, y, alpha):
        xi = int(round(x))
        yi = int(round(y))
        if (0 <= xi < size) and (0 <= yi < size):
            index = (yi * width) + (frame * size) + xi
            old = pixels[index][3]
            pixels[index] = (255, 255, 255, max(old, to_byte(alpha)))

    centre = (size - 1) / 2.0
    ground = size - 2.0
    for frame in range(frames):
        age = (frame + 0.5) / frames
        # The crown: a ring on the ground, wider each frame, fading.
        spread = 1.5 + (5.5 * age)
        for step in range(48):
            angle = (2.0 * math.pi * step) / 48.0
            x = centre + (math.cos(angle) * spread)
            y = ground - (abs(math.sin(angle)) * spread * 0.25)
            put(frame, x, y, (1.0 - age) * 0.95)
        # The column at the first frames, the droplets thrown out along arcs after.
        if frame < 2:
            for y in range(int(ground - (4.0 * (1.0 - age))), int(ground) + 1):
                put(frame, centre, y, 0.9 - (0.3 * frame))
        for direction, strength in droplets:
            distance = age * 6.5 * strength
            x = centre + (direction * distance)
            y = ground - (distance * 1.6 * (1.0 - age) * strength)
            put(frame, x, y, (1.0 - (age * age)) * 0.9)
    return width, size, pixels


# The puddle mask: blobs of value noise on periodic lattices (tileable), then spread evenly over the range by rank.
def puddle_mask():
    size = 256
    rng = random.Random(SEED + 1)
    lattices = []
    for period, weight in ((4, 1.0), (8, 0.55), (16, 0.3), (32, 0.15)):
        values = [rng.random() for _ in range(period * period)]
        lattices.append((period, weight, values))
    raw = []
    for y in range(size):
        for x in range(size):
            total = 0.0
            norm = 0.0
            for period, weight, values in lattices:
                u = (x + 0.5) * period / size
                v = (y + 0.5) * period / size
                x0 = math.floor(u)
                y0 = math.floor(v)
                fx = smooth(u - x0)
                fy = smooth(v - y0)
                i0, i1 = x0 % period, (x0 + 1) % period
                j0, j1 = y0 % period, (y0 + 1) % period
                top = values[(j0 * period) + i0] + ((values[(j0 * period) + i1] - values[(j0 * period) + i0]) * fx)
                bottom = values[(j1 * period) + i0] + ((values[(j1 * period) + i1] - values[(j1 * period) + i0]) * fx)
                total += (top + ((bottom - top) * fy)) * weight
                norm += weight
            raw.append(total / norm)
    order = sorted(range(len(raw)), key=lambda index: raw[index])
    pixels = [0] * len(raw)
    for rank, index in enumerate(order):
        pixels[index] = int((rank * 256) // len(raw))
    return size, size, pixels


# The bolts: each a jagged channel from the clouds to the ground by midpoint displacement, with branches, drawn as a hot
# core and a soft glow.
def bolt_path(rng, start, end, roughness, depth, low, high):
    """A jagged path from start to end, its x kept within [low, high] (its column's margins)."""
    points = [start, end]
    for _ in range(depth):
        refined = [points[0]]
        for a, b in zip(points, points[1:]):
            length = math.hypot(b[0] - a[0], b[1] - a[1])
            x = (a[0] + b[0]) / 2.0 + (rng.uniform(-1.0, 1.0) * roughness * length)
            middle = (clamp(x, low, high), (a[1] + b[1]) / 2.0)
            refined.extend([middle, b])
        points = refined
    return points


def bolts():
    columns, width, height = 4, 128, 512
    image_width = columns * width
    glow = [0.0] * (image_width * height)
    rng = random.Random(SEED + 2)

    def stroke(column, points, core, radius):
        for a, b in zip(points, points[1:]):
            length = max(1, int(math.hypot(b[0] - a[0], b[1] - a[1]) * 2.0))
            for step in range(length + 1):
                t = step / length
                x = a[0] + ((b[0] - a[0]) * t)
                y = a[1] + ((b[1] - a[1]) * t)
                for dy in range(-radius, radius + 1):
                    for dx in range(-radius, radius + 1):
                        px = int(round(x)) + dx
                        py = int(round(y)) + dy
                        if (0 <= px < width) and (0 <= py < height):
                            distance = math.hypot(dx, dy)
                            value = core * max(0.0, 1.0 - (distance / (radius + 0.5))) ** 2
                            index = (py * image_width) + (column * width) + px
                            glow[index] = max(glow[index], value)

    for column in range(columns):
        top = (width / 2.0 + rng.uniform(-12.0, 12.0), 0.0)
        bottom = (width / 2.0 + rng.uniform(-28.0, 28.0), height - 1.0)
        margin = 10.0
        main = bolt_path(rng, top, bottom, 0.12, 8, margin, width - margin)
        stroke(column, main, 0.55, 4)
        stroke(column, main, 1.0, 0)
        for _ in range(rng.randint(2, 4)):
            start = main[rng.randint(len(main) // 8, (len(main) * 2) // 3)]
            angle = rng.uniform(0.35, 1.0) * (1 if rng.random() < 0.5 else -1)
            length = rng.uniform(50.0, 140.0)
            end = (start[0] + (math.sin(angle) * length), start[1] + (math.cos(angle) * length))
            branch = bolt_path(rng, start, (clamp(end[0], margin, width - margin), end[1]), 0.22, 6, margin,
                width - margin)
            stroke(column, branch, 0.35, 3)
            stroke(column, branch, 0.75, 0)
    pixels = []
    for value in glow:
        pixels.append((255, 255, 255, to_byte(value)))
    return image_width, height, pixels


def main():
    os.makedirs(TEXTURE_DIRECTORY, exist_ok=True)
    os.makedirs(ANIMATION_DIRECTORY, exist_ok=True)
    point_clamp = {"filtering": "point", "wrap": "clamp", "mipmaps": False}
    width, height, pixels = streak()
    write_png(os.path.join(TEXTURE_DIRECTORY, "streak.png"), width, height, 4, pixels)
    write_definition("streak.json", "tbp:rain_streak", "rain/streak", point_clamp, "srgb")
    width, height, pixels = puddle_mask()
    write_png(os.path.join(TEXTURE_DIRECTORY, "puddle_mask.png"), width, height, 1, pixels)
    write_definition("puddle_mask.json", "tbp:rain_puddle_mask", "rain/puddle_mask",
        {"filtering": "trilinear", "wrap": "repeat", "mipmaps": True}, "linear")
    # The splash: its frames cut from the strip, one image each.
    width, height, pixels = splash()
    for frame in range(width // height):
        write_png(os.path.join(ANIMATION_DIRECTORY, "splash_%d.png" % frame), height, height, 4,
            crop(width, pixels, frame * height, 0, height, height))
    write_animation("splash.json", "tbp:rain_splash", ["rain/splash_%d" % frame for frame in range(width // height)],
        "point", 20)
    # The bolts: one image each, from their columns.
    width, height, pixels = bolts()
    columns = 4
    for column in range(columns):
        write_png(os.path.join(ANIMATION_DIRECTORY, "bolt_%d.png" % column), width // columns, height, 4,
            crop(width, pixels, column * (width // columns), 0, width // columns, height))
    write_animation("bolts.json", "tbp:lightning_bolts", ["rain/bolt_%d" % column for column in range(columns)],
        "bilinear", 0)


if __name__ == "__main__":
    main()
