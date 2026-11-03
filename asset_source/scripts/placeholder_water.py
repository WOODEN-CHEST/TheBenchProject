#!/usr/bin/env python3
"""Generates the placeholder art of the water: two wave normal maps, a foam pattern, a water plane, a stone gutter, a
fountain's basin, their definitions, the test world's water materials and the world models that draw them.

Run it from anywhere (only the Python standard library is needed):

    python3 asset_source/scripts/placeholder_water.py

It writes, relative to the repository root:

    compile/out/asset/textures/water/normal_a.png         256 x 256, RGB, tileable: the larger waves, sums of 40 sine
                                                          waves of 1 to 8 cycles a tile in every direction, as normals in
                                                          OpenGL's convention (green toward the image's top)
    compile/out/asset/textures/water/normal_b.png         256 x 256, RGB, tileable: the smaller waves (48 of 6 to 18
                                                          cycles a tile)
    compile/out/asset/textures/water/foam.png             256 x 256, greyscale, tileable: where foam lies (bubbly cells
                                                          and noise), spread evenly over 0 .. 255
    compile/out/asset/textures/water/<name>.json          their texture definitions (tbp:water_normal_a,
                                                          tbp:water_normal_b: linear; tbp:water_foam), mipmapped
    compile/out/asset/models/placeholder_water/water_plane.obj (.mtl, .json)
                                                          tbp:water_plane: a flat 1000 x 1000 m grid of 32 x 32 quads
                                                          around its origin, one material (slot 0, the water)
    compile/out/asset/models/placeholder_water/gutter.obj (.mtl, .json)
                                                          tbp:test_gutter: a stone channel 8 m long along +X, its water
                                                          (slot 1) 17 cm deep, its texture coordinates in metres: V along
                                                          it (the stream flows toward +X), U across it
    compile/out/asset/models/placeholder_water/fountain.obj (.mtl, .json)
                                                          tbp:test_fountain: a round stone basin 3 m across, its water
                                                          (slot 1) a disc 30 cm deep
    compile/out/asset/water_materials/test_world/<name>.json
                                                          the water materials: tbp:test_sea, tbp:test_pond,
                                                          tbp:test_stream, tbp:test_fountain_clear, tbp:test_fountain_murky
    compile/out/asset/world_models/test_world/<name>.json the world models: tbp:test_sea (the plane, seen at any
                                                          distance), tbp:test_pond (the plane for the valley), tbp:test_gutter,
                                                          tbp:test_fountain (variants clear and murky)

The normal maps tile exactly (every wave a whole number of cycles a tile in both directions), their slopes 0.12 as a
root mean square; their waves run every way (the maps lie on the world's axes whatever the wind, so no direction is
baked in), the larger ones stronger (an amplitude falling as the 1.6th power of the wave number, a sea's spectrum's
way); the foam pattern is spread evenly over its range by its histogram, so a foam depth reaches out to the share of the
pattern it says.

Placeholder art for checking the code, not game art: replace it with the owner's (a world model's water section names
any water material; a water material names any textures).
"""

import json
import math
import os
import random
import re
import struct
import zlib


# Paths.
REPOSITORY = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
ASSET = os.path.join(REPOSITORY, "compile", "out", "asset")
TEXTURE_DIRECTORY = os.path.join(ASSET, "textures", "water")
MODEL_DIRECTORY = os.path.join(ASSET, "models", "placeholder_water")
WATER_MATERIAL_DIRECTORY = os.path.join(ASSET, "water_materials", "test_world")
WORLD_MODEL_DIRECTORY = os.path.join(ASSET, "world_models", "test_world")
SEED = 4127

# The maps: their side, and the waves' root-mean-square slope each map is scaled to.
MAP_SIDE = 256
MAP_SLOPE = 0.12

# The models' materials (raylib reads Kd as the albedo colour; the water slot's colour is never drawn).
STONE = 0
WATER = 1
STONE_COLOUR = (0.52, 0.5, 0.47)
WATER_COLOUR = (0.1, 0.2, 0.25)
STONE_ROUGHNESS = 0.85


# PNG writing.
def png_chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def write_png(path, width, height, channels, pixels):
    """pixels: width * height tuples (or ints for one channel), row by row from the top."""
    colour_type = {1: 0, 3: 2}[channels]
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


def write_json(value, path):
    """Writes JSON as the game's definitions read: four spaces a level, arrays of numbers and flat objects on one line."""
    text = json.dumps(value, indent=4)
    text = re.sub(r"\[\s+([^\[\]{}]*?)\s+\]", lambda match: "[" + re.sub(r",\s+", ", ", match.group(1)) + "]", text)
    text = re.sub(r"\{\s+([^\[\]{}]*?)\s+\}", lambda match: "{ " + re.sub(r",\s+", ", ", match.group(1)) + " }", text)
    with open(path, "w", encoding="ascii", newline="\n") as file:
        file.write(text + "\n")


def to_byte(value):
    return int(round(max(0.0, min(1.0, value)) * 255.0))


# The normal maps: sums of sine waves h = sum a sin(2 pi (kx x + ky y) / N + phase), every wave a whole number of
# cycles a tile, written as normals (-dh/dx, -dh/dup, 1) normalised, up toward the image's top (rows run down).
def wave_set(rng, magnitudes, count):
    waves = []
    for index in range(count):
        magnitude = rng.uniform(*magnitudes)
        angle = rng.uniform(0.0, 2.0 * math.pi)
        kx = int(round(magnitude * math.cos(angle)))
        ky = int(round(magnitude * math.sin(angle)))
        if (kx == 0) and (ky == 0):
            kx = 1
        length = math.hypot(kx, ky)
        waves.append((kx, ky, 1.0 / (length ** 1.6), rng.uniform(0.0, 2.0 * math.pi)))
    return waves


def normal_map(waves):
    side = MAP_SIDE
    slopes = []
    for row in range(side):
        for column in range(side):
            dx = 0.0
            drow = 0.0
            for kx, ky, amplitude, phase in waves:
                angle = (2.0 * math.pi * ((kx * column) + (ky * row)) / side) + phase
                derivative = amplitude * math.cos(angle) * 2.0 * math.pi / side
                dx += derivative * kx
                drow += derivative * ky
            slopes.append((dx, drow))
    mean_square = sum((dx * dx) + (drow * drow) for dx, drow in slopes) / len(slopes)
    scale = MAP_SLOPE / math.sqrt(mean_square)
    pixels = []
    for dx, drow in slopes:
        # Up the image is down the rows: dh/dup = -dh/drow.
        nx = -dx * scale
        ny = drow * scale
        length = math.sqrt((nx * nx) + (ny * ny) + 1.0)
        pixels.append((to_byte(((nx / length) + 1.0) * 0.5), to_byte(((ny / length) + 1.0) * 0.5),
                       to_byte(((1.0 / length) + 1.0) * 0.5)))
    return pixels


# The foam: bubbly cells (bright in their middles) and periodic value noise, spread evenly over 0 .. 255.
def periodic_value_noise(rng, cells):
    grid = [[rng.random() for _ in range(cells)] for _ in range(cells)]

    def sample(x, y):
        fx = (x / MAP_SIDE) * cells
        fy = (y / MAP_SIDE) * cells
        x0 = int(math.floor(fx)) % cells
        y0 = int(math.floor(fy)) % cells
        tx = fx - math.floor(fx)
        ty = fy - math.floor(fy)
        tx = tx * tx * (3.0 - (2.0 * tx))
        ty = ty * ty * (3.0 - (2.0 * ty))
        x1 = (x0 + 1) % cells
        y1 = (y0 + 1) % cells
        top = (grid[y0][x0] * (1.0 - tx)) + (grid[y0][x1] * tx)
        bottom = (grid[y1][x0] * (1.0 - tx)) + (grid[y1][x1] * tx)
        return (top * (1.0 - ty)) + (bottom * ty)
    return sample


def foam_map():
    rng = random.Random(SEED + 2)
    side = MAP_SIDE
    points = [(rng.uniform(0.0, side), rng.uniform(0.0, side)) for _ in range(28)]
    octaves = [(periodic_value_noise(rng, cells), weight) for cells, weight in ((8, 0.5), (16, 0.3), (32, 0.2))]
    values = []
    for row in range(side):
        for column in range(side):
            nearest = side
            for px, py in points:
                dx = abs(column - px)
                dy = abs(row - py)
                dx = min(dx, side - dx)
                dy = min(dy, side - dy)
                nearest = min(nearest, math.hypot(dx, dy))
            cell = max(0.0, 1.0 - (nearest / 28.0))
            noise = sum(sample(column, row) * weight for sample, weight in octaves)
            values.append((0.55 * cell) + (0.45 * noise))
    order = sorted(range(len(values)), key=lambda index: values[index])
    pixels = [0] * len(values)
    for rank, index in enumerate(order):
        pixels[index] = int((rank * 256) // len(values))
    return pixels


def write_textures():
    os.makedirs(TEXTURE_DIRECTORY, exist_ok=True)
    rng = random.Random(SEED)
    large = wave_set(rng, (1.0, 8.0), 40)
    small = wave_set(rng, (6.0, 18.0), 48)
    write_png(os.path.join(TEXTURE_DIRECTORY, "normal_a.png"), MAP_SIDE, MAP_SIDE, 3, normal_map(large))
    write_png(os.path.join(TEXTURE_DIRECTORY, "normal_b.png"), MAP_SIDE, MAP_SIDE, 3, normal_map(small))
    write_png(os.path.join(TEXTURE_DIRECTORY, "foam.png"), MAP_SIDE, MAP_SIDE, 1, foam_map())
    properties = {"filtering": "trilinear", "wrap": "repeat", "mipmaps": True}
    for file_name, name, location in (("normal_a.json", "tbp:water_normal_a", "water/normal_a"),
                                      ("normal_b.json", "tbp:water_normal_b", "water/normal_b"),
                                      ("foam.json", "tbp:water_foam", "water/foam")):
        write_json({"name": name, "location": location, "texture_properties": properties, "color_space": "linear"},
                   os.path.join(TEXTURE_DIRECTORY, file_name))


# The models.
class Mesh:
    """Faces grouped by material, with their vertices, normals and texture coordinates indexed as OBJ wants."""

    def __init__(self, material_count):
        self.vertices = []
        self.normals = []
        self.texcoords = []
        self.faces = {index: [] for index in range(material_count)}

    def polygon(self, corners, normal, material, texcoords):
        """Adds a polygon: its corners counter-clockwise seen from the side the normal points to (checked)."""
        a, b, c = corners[0], corners[1], corners[2]
        edge1 = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        edge2 = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        cross = ((edge1[1] * edge2[2]) - (edge1[2] * edge2[1]), (edge1[2] * edge2[0]) - (edge1[0] * edge2[2]),
                 (edge1[0] * edge2[1]) - (edge1[1] * edge2[0]))
        assert sum(cross[axis] * normal[axis] for axis in range(3)) > 0.0, "a face is wound the wrong way"
        first = len(self.vertices) + 1
        self.vertices.extend(corners)
        self.normals.append(normal)
        normal_index = len(self.normals)
        uv = []
        for coordinate in texcoords:
            self.texcoords.append(coordinate)
            uv.append(len(self.texcoords))
        self.faces[material].append([(first + corner, uv[corner], normal_index) for corner in range(len(corners))])

    def quad(self, corners, normal, material, texcoords=None):
        self.polygon(corners, normal, material, texcoords or [(0.0, 0.0)] * 4)


def write_obj(mesh, path, comment, material_file, material_names):
    lines = ["# " + line for line in comment] + ["mtllib %s" % material_file, "o Water"]
    lines += ["v %.6f %.6f %.6f" % vertex for vertex in mesh.vertices]
    lines += ["vt %.6f %.6f" % texcoord for texcoord in mesh.texcoords]
    lines += ["vn %.6f %.6f %.6f" % normal for normal in mesh.normals]
    for material, name in enumerate(material_names):
        if not mesh.faces[material]:
            continue
        lines.append("usemtl %s" % name)
        for face in mesh.faces[material]:
            lines.append("f " + " ".join("%d/%d/%d" % corner for corner in face))
    with open(path, "w", encoding="ascii", newline="\n") as file:
        file.write("\n".join(lines) + "\n")


def write_mtl(path, comment, materials):
    lines = ["# " + comment]
    for name, colour in materials:
        lines += ["newmtl %s" % name, "Kd %.4f %.4f %.4f" % colour, ""]
    with open(path, "w", encoding="ascii", newline="\n") as file:
        file.write("\n".join(lines))


def water_plane():
    """1000 x 1000 m around the origin, 32 x 32 quads facing up; texture coordinates 0..1 over it (V along +Z)."""
    mesh = Mesh(1)
    size = 1000.0
    count = 32
    step = size / count
    for row in range(count):
        for column in range(count):
            x0 = -size / 2.0 + (column * step)
            z0 = -size / 2.0 + (row * step)
            x1 = x0 + step
            z1 = z0 + step
            uv = lambda x, z: ((x / size) + 0.5, (z / size) + 0.5)
            mesh.quad([(x0, 0.0, z0), (x0, 0.0, z1), (x1, 0.0, z1), (x1, 0.0, z0)], (0.0, 1.0, 0.0), 0,
                      [uv(x0, z0), uv(x0, z1), uv(x1, z1), uv(x1, z0)])
    return mesh


def box(mesh, ranges, material):
    """A box's six faces, outward."""
    (x0, x1), (y0, y1), (z0, z1) = ranges
    mesh.quad([(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], (0.0, 0.0, 1.0), material)
    mesh.quad([(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)], (0.0, 0.0, -1.0), material)
    mesh.quad([(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)], (-1.0, 0.0, 0.0), material)
    mesh.quad([(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)], (1.0, 0.0, 0.0), material)
    mesh.quad([(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)], (0.0, 1.0, 0.0), material)
    mesh.quad([(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], (0.0, -1.0, 0.0), material)


GUTTER_LENGTH = 8.0
GUTTER_INNER = 0.25
GUTTER_WALL = 0.15
GUTTER_FLOOR = 0.08
GUTTER_RIM = 0.35
GUTTER_WATER = 0.25


def gutter():
    """A stone channel along +X from x = 0 to its length: a floor, two side walls, two end walls (boxes, so every
    face shows from outside), and its water: a strip inside, its texture coordinates in metres, U across (0 at -Z) and V
    along it (V grows with x: the stream flows toward +X), so a map lies on it as on the world, unstretched."""
    mesh = Mesh(2)
    length = GUTTER_LENGTH
    outer = GUTTER_INNER + GUTTER_WALL
    box(mesh, ((0.0, length), (0.0, GUTTER_FLOOR), (-outer, outer)), STONE)
    box(mesh, ((0.0, length), (0.0, GUTTER_RIM), (GUTTER_INNER, outer)), STONE)
    box(mesh, ((0.0, length), (0.0, GUTTER_RIM), (-outer, -GUTTER_INNER)), STONE)
    box(mesh, ((-GUTTER_WALL, 0.0), (0.0, GUTTER_RIM), (-outer, outer)), STONE)
    box(mesh, ((length, length + GUTTER_WALL), (0.0, GUTTER_RIM), (-outer, outer)), STONE)
    segments = 16
    for segment in range(segments):
        x0 = length * segment / segments
        x1 = length * (segment + 1) / segments
        z0 = -GUTTER_INNER
        z1 = GUTTER_INNER
        uv = lambda x, z: (z - z0, x)
        mesh.quad([(x0, GUTTER_WATER, z0), (x0, GUTTER_WATER, z1), (x1, GUTTER_WATER, z1), (x1, GUTTER_WATER, z0)],
                  (0.0, 1.0, 0.0), WATER, [uv(x0, z0), uv(x0, z1), uv(x1, z1), uv(x1, z0)])
    return mesh


FOUNTAIN_OUTER = 1.5
FOUNTAIN_INNER = 1.3
FOUNTAIN_RIM = 0.55
FOUNTAIN_FLOOR = 0.12
FOUNTAIN_WATER = 0.42
FOUNTAIN_SEGMENTS = 32


def fountain():
    """A round stone basin around the origin: its outer wall, its rim, its inner wall and its floor, and its water: a
    disc inside (texture coordinates planar, 0..1 over the basin's width)."""
    mesh = Mesh(2)
    count = FOUNTAIN_SEGMENTS

    def at(radius, index, height):
        angle = 2.0 * math.pi * index / count
        return (radius * math.cos(angle), height, radius * math.sin(angle))

    def outward(index):
        angle = 2.0 * math.pi * (index + 0.5) / count
        return (math.cos(angle), 0.0, math.sin(angle))

    for index in range(count):
        following = index + 1
        out = outward(index)
        inward = (-out[0], 0.0, -out[2])
        # The outer wall, facing out; the rim, facing up; the inner wall, facing in; the floor, facing up.
        mesh.quad([at(FOUNTAIN_OUTER, following, 0.0), at(FOUNTAIN_OUTER, index, 0.0),
                   at(FOUNTAIN_OUTER, index, FOUNTAIN_RIM), at(FOUNTAIN_OUTER, following, FOUNTAIN_RIM)], out, STONE)
        mesh.quad([at(FOUNTAIN_INNER, following, FOUNTAIN_RIM), at(FOUNTAIN_OUTER, following, FOUNTAIN_RIM),
                   at(FOUNTAIN_OUTER, index, FOUNTAIN_RIM), at(FOUNTAIN_INNER, index, FOUNTAIN_RIM)], (0.0, 1.0, 0.0),
                  STONE)
        mesh.quad([at(FOUNTAIN_INNER, index, FOUNTAIN_FLOOR), at(FOUNTAIN_INNER, following, FOUNTAIN_FLOOR),
                   at(FOUNTAIN_INNER, following, FOUNTAIN_RIM), at(FOUNTAIN_INNER, index, FOUNTAIN_RIM)], inward,
                  STONE)
        mesh.polygon([(0.0, FOUNTAIN_FLOOR, 0.0), at(FOUNTAIN_INNER, following, FOUNTAIN_FLOOR),
                      at(FOUNTAIN_INNER, index, FOUNTAIN_FLOOR)], (0.0, 1.0, 0.0), STONE, [(0.0, 0.0)] * 3)
        uv = lambda point: ((point[0] / (2.0 * FOUNTAIN_OUTER)) + 0.5, (point[2] / (2.0 * FOUNTAIN_OUTER)) + 0.5)
        corners = [(0.0, FOUNTAIN_WATER, 0.0), at(FOUNTAIN_INNER, following, FOUNTAIN_WATER),
                   at(FOUNTAIN_INNER, index, FOUNTAIN_WATER)]
        mesh.polygon(corners, (0.0, 1.0, 0.0), WATER, [uv(point) for point in corners])
    return mesh


def write_models():
    os.makedirs(MODEL_DIRECTORY, exist_ok=True)
    plane = water_plane()
    write_obj(plane, os.path.join(MODEL_DIRECTORY, "water_plane.obj"),
              ["A flat water plane, 1000 x 1000 m of 32 x 32 quads around its origin (material water_surface, slot 0).",
               "Written by asset_source/scripts/placeholder_water.py."], "water_plane.mtl", ["water_surface"])
    write_mtl(os.path.join(MODEL_DIRECTORY, "water_plane.mtl"), "The water plane's one material (slot 0, drawn as water).",
              [("water_surface", WATER_COLOUR)])
    write_json({"name": "tbp:water_plane", "location": "placeholder_water/water_plane"},
               os.path.join(MODEL_DIRECTORY, "water_plane.json"))
    for name, mesh, title in (("gutter", gutter(), "A stone gutter 8 m long along +X with its stream"),
                              ("fountain", fountain(), "A round stone basin 3 m across with its water")):
        write_obj(mesh, os.path.join(MODEL_DIRECTORY, name + ".obj"),
                  [title + " (materials stone, slot 0, and water_surface, slot 1, drawn as water).",
                   "Written by asset_source/scripts/placeholder_water.py."], name + ".mtl", ["stone", "water_surface"])
        write_mtl(os.path.join(MODEL_DIRECTORY, name + ".mtl"), "In slot order: the stone (0) and the water (1).",
                  [("stone", STONE_COLOUR), ("water_surface", WATER_COLOUR)])
        write_json({"name": "tbp:test_" + name, "location": "placeholder_water/" + name,
                    "material_overrides": [{"slot": STONE, "roughness": STONE_ROUGHNESS}]},
                   os.path.join(MODEL_DIRECTORY, name + ".json"))
    return plane


# The water materials and the world models. Every tile laid on the world divides 61 440 m, where the rain's and the
# clouds' patterns repeat too, so a test world shifted that far shows the same picture.
WATER_MATERIALS = {
    # The sea: waves with the wind, a deep blue, clear, surf at the shore.
    "test_sea": {"normal_a": "tbp:water_normal_a", "normal_a_tile_size": 15, "normal_b": "tbp:water_normal_b",
                 "normal_b_tile_size": 4.8, "wave_strength": 1.0, "color": "#0e2a3a", "absorption": [0.35, 0.07, 0.04],
                 "foam": "tbp:water_foam", "foam_tile_size": 3, "foam_depth": 0.6, "foam_strength": 0.9},
    # The pond in the valley: smaller waves, greener and murky, its bottom gone a few metres down.
    "test_pond": {"normal_a": "tbp:water_normal_a", "normal_a_tile_size": 6, "normal_b": "tbp:water_normal_b",
                  "normal_b_tile_size": 1.6, "wave_strength": 0.7, "color": "#1f3a32", "absorption": [0.9, 0.35, 0.45],
                  "foam": "tbp:water_foam", "foam_tile_size": 1.5, "foam_depth": 0.2, "foam_strength": 0.6},
    # The stream in the gutter: along the texture coordinates at 0.8 m/s, quick small waves, clear and shallow.
    "test_stream": {"normal_a": "tbp:water_normal_a", "normal_a_tile_size": 1.4, "normal_a_speed": 1.0,
                    "normal_b": "tbp:water_normal_b", "normal_b_tile_size": 0.5, "normal_b_speed": 0.7,
                    "normal_b_turn": 20, "wave_strength": 1.4, "wind_response": 0.3, "flow": "uv", "flow_speed": 0.8,
                    "color": "#1d3838", "absorption": [0.6, 0.15, 0.12], "foam": "tbp:water_foam",
                    "foam_tile_size": 0.4, "foam_depth": 0.06, "foam_strength": 0.5},
    # The fountain: still water (no flow, the wind barely touches it), clear.
    "test_fountain_clear": {"normal_a": "tbp:water_normal_a", "normal_a_tile_size": 2, "normal_b": "tbp:water_normal_b",
                            "normal_b_tile_size": 0.75, "wave_strength": 0.35, "wind_response": 0.3, "flow": "fixed",
                            "flow_speed": 0.0, "color": "#1c3c44", "absorption": [0.3, 0.06, 0.04], "roughness": 0.04},
    # The same basin gone green: murky, its bottom hidden within a few centimetres, only fine ripples on it (its one
    # normal map the smaller waves' texture).
    "test_fountain_murky": {"normal_a": "tbp:water_normal_b", "normal_a_tile_size": 1.2, "wave_strength": 0.35,
                            "wind_response": 0.3, "flow": "fixed", "flow_speed": 0.0, "color": "#2e3a1e",
                            "absorption": [6.0, 4.5, 5.5], "roughness": 0.08},
}


def write_water_materials():
    os.makedirs(WATER_MATERIAL_DIRECTORY, exist_ok=True)
    for name, settings in WATER_MATERIALS.items():
        write_json(dict({"name": "tbp:" + name}, **settings), os.path.join(WATER_MATERIAL_DIRECTORY, name + ".json"))


def write_world_models():
    os.makedirs(WORLD_MODEL_DIRECTORY, exist_ok=True)
    # The sea: the plane scaled huge by its placement, seen at any distance (its draw distance fixed), casting nothing.
    write_json({
        "name": "tbp:test_sea",
        "lods": [{"model": "tbp:water_plane", "max_distance": 1000000}],
        "is_draw_distance_fixed": True,
        "render": {"casts_shadows": False, "occludes_sky": False},
        "water": [{"material_slot": 0, "water_material": "tbp:test_sea"}],
    }, os.path.join(WORLD_MODEL_DIRECTORY, "test_sea.json"))
    write_json({
        "name": "tbp:test_pond",
        "lods": [{"model": "tbp:water_plane", "max_distance": 3000}],
        "render": {"casts_shadows": False, "occludes_sky": False},
        "water": [{"material_slot": 0, "water_material": "tbp:test_pond"}],
    }, os.path.join(WORLD_MODEL_DIRECTORY, "test_pond.json"))
    write_json({
        "name": "tbp:test_gutter",
        "lods": [{"model": "tbp:test_gutter", "max_distance": 150}],
        "water": [{"material_slot": WATER, "water_material": "tbp:test_stream"}],
    }, os.path.join(WORLD_MODEL_DIRECTORY, "test_gutter.json"))
    write_json({
        "name": "tbp:test_fountain",
        "lods": [{"model": "tbp:test_fountain", "max_distance": 150}],
        "water": [{"material_slot": WATER, "water_material": "tbp:test_fountain_clear"}],
        "variants": [
            {"name": "clear"},
            {"name": "murky", "material_overrides": [{"slot": WATER, "water_material": "tbp:test_fountain_murky"}]},
        ],
    }, os.path.join(WORLD_MODEL_DIRECTORY, "test_fountain.json"))


def main():
    write_textures()
    plane = write_models()
    write_water_materials()
    write_world_models()
    print("Wrote the placeholder water: maps of %d x %d, a plane of %d quads, %d water materials." % (
        MAP_SIDE, MAP_SIDE, len(plane.faces[0]), len(WATER_MATERIALS)))


if __name__ == "__main__":
    main()
