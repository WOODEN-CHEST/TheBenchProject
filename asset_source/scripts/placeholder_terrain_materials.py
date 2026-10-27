#!/usr/bin/env python3
"""Generates the placeholder terrain materials for the test terrain: grass, dirt, rock and gravel.

Run it from anywhere (only the Python standard library is needed):

    python3 asset_source/scripts/placeholder_terrain_materials.py

It writes, relative to the repository root:

    compile/out/asset/textures/placeholder_terrain/<material>_<map>.png   16 images, 256 x 256, tileable
    compile/out/asset/textures/placeholder_terrain/<material>_<map>.json  their texture definitions
    compile/out/asset/terrain_materials/placeholder_terrain/<material>.json  the four terrain materials

Each material has the four maps a terrain material reads (references/asset_structure.md, "Terrain materials"):

    albedo        RGB, sRGB colour
    height        greyscale: the surface's height for height blending (rock's stones and gravel stand high, grass low)
    normal        RGB, a tangent-space normal map in OpenGL's convention (green toward the image's top), made from the
                  height: x = 128 + 127 * nx, as the game decodes it
    roughness_ao  RGB: red the roughness, green the ambient occlusion (from the height's hollows), blue unused

Every map tiles: the noise repeats across the image's edges (periodic lattices and cells), so the materials repeat
without seams at their tile size.

Placeholder art for checking the code, not game art: replace it with the owner's textures (one size for every
material's map of a kind; see the reference).
"""

import json
import math
import os
import random
import struct
import zlib


# Paths.
REPOSITORY = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
TEXTURE_DIRECTORY = os.path.join(REPOSITORY, "compile", "out", "asset", "textures", "placeholder_terrain")
MATERIAL_DIRECTORY = os.path.join(REPOSITORY, "compile", "out", "asset", "terrain_materials", "placeholder_terrain")

# The images' side in pixels.
SIZE = 256


# PNG writing.
def png_chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def write_png(path, channels, pixels):
    """pixels: SIZE * SIZE tuples (or ints for one channel), row by row from the top."""
    colour_type = {1: 0, 3: 2}[channels]
    rows = bytearray()
    for y in range(SIZE):
        rows.append(0)
        for x in range(SIZE):
            value = pixels[(y * SIZE) + x]
            if channels == 1:
                rows.append(value)
            else:
                rows.extend(value)
    header = struct.pack(">IIBBBBB", SIZE, SIZE, 8, colour_type, 0, 0, 0)
    data = b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header) + png_chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    data += png_chunk(b"IEND", b"")
    with open(path, "wb") as output:
        output.write(data)


# Noise that tiles across the image.
def smooth(t):
    return t * t * (3.0 - (2.0 * t))


def clamp(value, low=0.0, high=1.0):
    return max(low, min(high, value))


def mix(a, b, t):
    return a + ((b - a) * t)


class Lattice:
    """Value noise on a periodic lattice: `period` cells across the image."""

    def __init__(self, period, rng):
        self.period = period
        self.values = [rng.random() for _ in range(period * period)]

    def sample(self, u, v):
        x = u * self.period
        y = v * self.period
        x0 = math.floor(x)
        y0 = math.floor(y)
        fx = smooth(x - x0)
        fy = smooth(y - y0)
        p = self.period
        i0, i1 = x0 % p, (x0 + 1) % p
        j0, j1 = y0 % p, (y0 + 1) % p
        top = mix(self.values[(j0 * p) + i0], self.values[(j0 * p) + i1], fx)
        bottom = mix(self.values[(j1 * p) + i0], self.values[(j1 * p) + i1], fx)
        return mix(top, bottom, fy)


class Fbm:
    """Several lattices of doubling periods, each half as strong: 0 to 1."""

    def __init__(self, periods, rng):
        self.octaves = [Lattice(period, rng) for period in periods]

    def sample(self, u, v):
        total = 0.0
        weight = 1.0
        norm = 0.0
        for octave in self.octaves:
            total += octave.sample(u, v) * weight
            norm += weight
            weight *= 0.5
        return total / norm


class Cells:
    """Cellular noise on a periodic grid of `count` cells across: one jittered point per cell. nearest() gives the distance
    to the nearest point and to the second nearest (in cells) and the nearest cell's random id."""

    def __init__(self, count, rng, jitter=0.85):
        self.count = count
        self.points = []
        self.ids = []
        for _ in range(count * count):
            self.points.append((0.5 + (jitter * (rng.random() - 0.5)), 0.5 + (jitter * (rng.random() - 0.5))))
            self.ids.append(rng.random())

    def nearest(self, u, v):
        x = u * self.count
        y = v * self.count
        cx = math.floor(x)
        cy = math.floor(y)
        first = second = 9.0
        nearest_id = 0.0
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                gx = cx + dx
                gy = cy + dy
                index = ((gy % self.count) * self.count) + (gx % self.count)
                px, py = self.points[index]
                distance = math.hypot((gx + px) - x, (gy + py) - y)
                if distance < first:
                    second = first
                    first = distance
                    nearest_id = self.ids[index]
                elif distance < second:
                    second = distance
        return first, second, nearest_id


# Colour helpers (sRGB bytes; mixed in linear light).
def to_linear(c):
    return (c / 255.0) ** 2.2


def to_byte(c):
    return int(round(clamp(c) ** (1.0 / 2.2) * 255.0))


def mix_colour(a, b, t):
    return tuple(to_byte(mix(to_linear(a[k]), to_linear(b[k]), clamp(t))) for k in range(3))


def scale_colour(colour, factor):
    return tuple(to_byte(to_linear(c) * factor) for c in colour)


# The materials: each returns the height (0..1) and the albedo (sRGB bytes) of every pixel, and its roughness range.
def grass(rng):
    clumps = Cells(24, rng)
    blades = Fbm([32, 64, 128], rng)
    # Small dry patches only: a patch a quarter of the tile wide repeats as a grid you can see from afar.
    patches = Fbm([16, 32], rng)
    heights, colours = [], []
    for y in range(SIZE):
        for x in range(SIZE):
            u, v = x / SIZE, y / SIZE
            first, second, cell = clumps.nearest(u, v)
            clump = clamp(1.0 - (first * 1.35)) ** 0.7
            blade = blades.sample(u, v)
            height = clamp(0.12 + (0.38 * clump) + (0.18 * (blade - 0.5)) + (0.06 * cell))
            dry = smooth(clamp((patches.sample(u, v) - 0.5) * 3.0))
            base = mix_colour((44, 70, 26), (98, 134, 52), clamp((0.7 * height) + (0.3 * blade)))
            colour = mix_colour(base, (120, 124, 58), 0.3 * dry)
            heights.append(height)
            colours.append(colour)
    return heights, colours, (0.88, 1.0), 6.0


def dirt(rng):
    ground = Fbm([4, 8, 16, 32], rng)
    grain = Fbm([64, 128], rng)
    stones = Cells(40, rng)
    heights, colours = [], []
    for y in range(SIZE):
        for x in range(SIZE):
            u, v = x / SIZE, y / SIZE
            soil = ground.sample(u, v)
            first, second, cell = stones.nearest(u, v)
            pebble = clamp(1.0 - (first * 2.4)) if cell < 0.22 else 0.0
            height = clamp(0.22 + (0.3 * soil) + (0.08 * (grain.sample(u, v) - 0.5)) + (0.4 * pebble))
            colour = mix_colour((88, 64, 42), (140, 106, 70), clamp((soil * 1.2) - 0.1))
            if pebble > 0.0:
                colour = mix_colour(colour, (150, 138, 120), smooth(clamp(pebble * 2.5)))
            heights.append(height)
            colours.append(colour)
    return heights, colours, (0.82, 0.95), 5.0


def rock(rng):
    blocks = Cells(10, rng, 0.9)
    detail = Fbm([16, 32, 64, 128], rng)
    lichen = Fbm([8, 16], rng)
    heights, colours = [], []
    for y in range(SIZE):
        for x in range(SIZE):
            u, v = x / SIZE, y / SIZE
            first, second, cell = blocks.nearest(u, v)
            crack = smooth(clamp((second - first) / 0.16))
            grit = detail.sample(u, v)
            height = clamp((crack * (0.55 + (0.3 * cell))) + (0.15 * (grit - 0.5)) + 0.05)
            shade = 0.75 + (0.35 * cell) + (0.3 * (grit - 0.5))
            colour = scale_colour((128, 123, 116), shade)
            colour = mix_colour((52, 48, 44), colour, 0.35 + (0.65 * crack))
            spots = smooth(clamp((lichen.sample(u, v) - 0.62) * 6.0)) * crack
            colour = mix_colour(colour, (126, 124, 78), 0.6 * spots)
            heights.append(height)
            colours.append(colour)
    return heights, colours, (0.68, 0.86), 10.0


def gravel(rng):
    stones = Cells(36, rng, 0.95)
    grit = Fbm([64, 128], rng)
    heights, colours = [], []
    for y in range(SIZE):
        for x in range(SIZE):
            u, v = x / SIZE, y / SIZE
            first, second, cell = stones.nearest(u, v)
            gap = smooth(clamp((second - first) / 0.28))
            dome = clamp(1.0 - (first * 1.6)) ** 0.5
            height = clamp((gap * dome * (0.5 + (0.45 * cell))) + (0.05 * (grit.sample(u, v) - 0.5)) + 0.04)
            stone = mix_colour((96, 92, 88), (172, 162, 146), cell)
            if (cell * 7.0) % 1.0 < 0.3:
                stone = mix_colour(stone, (140, 112, 86), 0.5)
            colour = mix_colour((44, 40, 36), stone, gap)
            heights.append(height)
            colours.append(colour)
    return heights, colours, (0.72, 0.9), 8.0


# The maps made from a material's height.
def height_at(heights, x, y):
    return heights[((y % SIZE) * SIZE) + (x % SIZE)]


def normal_map(heights, depth):
    """OpenGL's convention: x toward the image's right, y toward its top; bytes 128 + 127 * component."""
    pixels = []
    for y in range(SIZE):
        for x in range(SIZE):
            dx = (height_at(heights, x + 1, y) - height_at(heights, x - 1, y)) * 0.5 * depth
            dy_down = (height_at(heights, x, y + 1) - height_at(heights, x, y - 1)) * 0.5 * depth
            # The surface rises along +x by dx and toward the image's top by -dy_down.
            nx, ny, nz = -dx, dy_down, 1.0
            length = math.sqrt((nx * nx) + (ny * ny) + (nz * nz))
            pixels.append(tuple(int(round(128.0 + (127.0 * (c / length)))) for c in (nx, ny, nz)))
    return pixels


def roughness_ao_map(heights, roughness_range, rng):
    """Roughness from the height (smoother tops) within the range; occlusion from the hollows around each texel."""
    pixels = []
    low, high = roughness_range
    for y in range(SIZE):
        for x in range(SIZE):
            here = height_at(heights, x, y)
            around = 0.0
            for oy in (-3, 0, 3):
                for ox in (-3, 0, 3):
                    around += height_at(heights, x + ox, y + oy)
            around /= 9.0
            occlusion = clamp(1.0 - (1.6 * max(around - here, 0.0)) - (0.1 * (1.0 - here)), 0.5, 1.0)
            roughness = clamp(high - ((high - low) * here) + (0.02 * (rng.random() - 0.5)))
            pixels.append((int(round(roughness * 255.0)), int(round(occlusion * 255.0)), 0))
    return pixels


# The materials' settings (references/asset_structure.md, "Terrain materials").
MATERIALS = [
    ("grass", grass, 7, {"tile_size": 2.0, "surface": "grass", "puddles": 0.0, "porosity": 0.6}),
    ("dirt", dirt, 13, {"tile_size": 2.0, "surface": "dirt", "puddles": 0.35, "porosity": 0.8}),
    ("rock", rock, 29, {"tile_size": 4.0, "height_blend_strength": 1.2, "roughness": 1.0, "surface": "rock",
                        "puddles": 0.15, "porosity": 0.25}),
    ("gravel", gravel, 41, {"tile_size": 1.5, "surface": "gravel", "puddles": 0.2, "porosity": 0.4}),
]
MAPS = [("albedo", "srgb"), ("height", "linear"), ("normal", "linear"), ("roughness_ao", "linear")]


def write_json(path, document):
    with open(path, "w") as output:
        output.write(json.dumps(document, indent=4) + "\n")
    print("wrote %s" % os.path.relpath(path, REPOSITORY))


def main():
    os.makedirs(TEXTURE_DIRECTORY, exist_ok=True)
    os.makedirs(MATERIAL_DIRECTORY, exist_ok=True)
    for name, make, seed, settings in MATERIALS:
        rng = random.Random(seed)
        heights, colours, roughness_range, depth = make(rng)
        images = {
            "albedo": (3, colours),
            "height": (1, [int(round(h * 255.0)) for h in heights]),
            "normal": (3, normal_map(heights, depth)),
            "roughness_ao": (3, roughness_ao_map(heights, roughness_range, rng)),
        }
        material = {"name": name}
        for map_name, colour_space in MAPS:
            stem = "%s_%s" % (name, map_name)
            channels, pixels = images[map_name]
            write_png(os.path.join(TEXTURE_DIRECTORY, stem + ".png"), channels, pixels)
            texture = "placeholder_terrain_%s" % stem
            write_json(os.path.join(TEXTURE_DIRECTORY, stem + ".json"),
                       {"name": texture, "location": "placeholder_terrain/%s" % stem, "color_space": colour_space})
            material[map_name] = texture
        material.update(settings)
        write_json(os.path.join(MATERIAL_DIRECTORY, name + ".json"), material)
        mean = sum(heights) / len(heights)
        print("  %s: mean height %.2f" % (name, mean))


if __name__ == "__main__":
    main()
