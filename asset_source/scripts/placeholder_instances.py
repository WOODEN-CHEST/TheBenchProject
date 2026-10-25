#!/usr/bin/env python3
"""Generates B03's placeholder content for instanced drawing: a tree with three LODs, a pebble with two, their world
models, and the test world's grove (about 400 trees on B01's placeholder terrain), as binary glTF (.glb) and JSON.

Run it from anywhere (only the Python standard library is needed):

    python3 asset_source/scripts/placeholder_instances.py

It writes, relative to the repository root:

    compile/out/asset/models/placeholder_instances/tree_lod0.glb    a 5.4 m conifer: an 8-sided trunk under three
                                                                    12-sided cones (LOD 0)
    compile/out/asset/models/placeholder_instances/tree_lod1.glb    a 4-sided trunk under two 6-sided cones (LOD 1)
    compile/out/asset/models/placeholder_instances/tree_lod2.glb    a stub of a trunk under one 4-sided cone (LOD 2)
    compile/out/asset/models/placeholder_instances/pebble_lod0.glb  a 24 cm pebble: a squashed, jittered icosahedron
    compile/out/asset/models/placeholder_instances/pebble_lod1.glb  a squashed octahedron
    compile/out/asset/models/placeholder_instances/*.json           their model definitions (test_tree_lod0, ...)
    compile/out/asset/world_models/test_world/test_tree.json        the tree's world model (LODs at 30 and 70 m)
    compile/out/asset/world_models/test_world/test_pebble.json      the pebble's (LODs at 12 m, gone past 45 m)
    compile/out/worlds/test.world.json                              object 52, "grove": a model_instances component of
                                                                    test_tree, and object 53, "grove_lamp", a warm lamp
                                                                    casting a cube shadow in a clearing of the grove
                                                                    (both replaced when they are there already)

The grove's trees stand where the terrain's tree density (its _DENSITY_0, placeholder_terrain.py's paint) is high, in
front of the start view, at least 3.5 m apart; each at the exact height of the terrain's mesh under it (the triangle it
falls in, as the mesh is built), turned about Y, a third of them stretched upward, tinted a little, and every 40th half
transparent (each copy fades on its own). The world file is written as Python's json writes it: load the test world and
save it with F5 (worlds/test.saved.world.json), then copy that over worlds/test.world.json for the game's own form.

Conventions: metres, +Y up, the models stand on their origin. Colours are materials' baseColorFactor (no textures).

Placeholder art for checking the code, not game art: replace it with the owner's trees and stones.
"""

import json
import math
import os
import random
import struct
import sys

sys.dont_write_bytecode = True  # importing the terrain script must leave no __pycache__ in the repository
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import placeholder_terrain as terrain  # noqa: E402 (the terrain's own grid and paint)


# Paths.
REPOSITORY = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
ASSET = os.path.join(REPOSITORY, "compile", "out", "asset")
MODEL_DIRECTORY = os.path.join(ASSET, "models", "placeholder_instances")
WORLD_MODEL_DIRECTORY = os.path.join(ASSET, "world_models", "test_world")
WORLD_FILE = os.path.join(REPOSITORY, "compile", "out", "worlds", "test.world.json")

# glTF constants.
FLOAT = 5126
UNSIGNED_SHORT = 5123
ARRAY_BUFFER = 34962
ELEMENT_ARRAY_BUFFER = 34963

# The materials.
BARK = ("bark", (0.33, 0.22, 0.13))
LEAVES = ("leaves", (0.15, 0.34, 0.12))
STONE = ("stone", (0.52, 0.50, 0.47))

# The grove: its object id and name, the area candidates are drawn from (x and z ranges, in front of the start view, which
# looks along +Z from the slab), how many trees, how far apart at least, and the seed.
GROVE_ID = 52
GROVE_NAME = "grove"
GROVE_AREA = ((-70.0, 70.0), (22.0, 150.0))
GROVE_COUNT = 400
GROVE_SPACING = 3.5
GROVE_SEED = 52
GROVE_HALF_TRANSPARENT_EVERY = 40
# The grove's lamp: its object id and name, where its clearing is looked for (the nearest point to this one, x and z,
# at least this far from every trunk), how high above the ground it hangs, and its light.
LAMP_ID = 53
LAMP_NAME = "grove_lamp"
LAMP_NEAR = (0.0, 45.0)
LAMP_CLEARANCE = 3.5
LAMP_HEIGHT = 3.0
LAMP_LIGHT = {"color": "#ffd9a0", "intensity": 30.0, "range": 18.0, "casts_shadows": True}


# Geometry.
def subtract(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def normalize(v):
    length = math.sqrt((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]))
    return (v[0] / length, v[1] / length, v[2] / length) if length > 0.0 else (0.0, 1.0, 0.0)


class MeshBuilder:
    """Flat-shaded triangles per material: every face its own three vertices with its normal."""

    def __init__(self, materials):
        self.materials = materials
        self.rows = {name: ([], [], []) for name, _ in materials}

    def triangle(self, material, a, b, c):
        """A triangle, counter-clockwise seen from its front."""
        positions, normals, indices = self.rows[material]
        normal = normalize(cross(subtract(b, a), subtract(c, a)))
        base = len(positions)
        positions += [a, b, c]
        normals += [normal] * 3
        indices += [base, base + 1, base + 2]

    def prism(self, material, sides, radius, bottom, top):
        """A closed prism around Y: its sides and its top cap (the bottom stands on the ground)."""
        ring = [(radius * math.cos(2.0 * math.pi * k / sides), radius * math.sin(2.0 * math.pi * k / sides))
                for k in range(sides)]
        for k in range(sides):
            (x0, z0), (x1, z1) = ring[k], ring[(k + 1) % sides]
            self.triangle(material, (x0, bottom, z0), (x0, top, z0), (x1, top, z1))
            self.triangle(material, (x0, bottom, z0), (x1, top, z1), (x1, bottom, z1))
            self.triangle(material, (0.0, top, 0.0), (x1, top, z1), (x0, top, z0))

    def cone(self, material, sides, radius, base, apex, turn=0.0):
        """A cone around Y with its base cap (seen from below)."""
        ring = [(radius * math.cos(turn + (2.0 * math.pi * k / sides)), radius * math.sin(turn + (2.0 * math.pi * k
                / sides))) for k in range(sides)]
        for k in range(sides):
            (x0, z0), (x1, z1) = ring[k], ring[(k + 1) % sides]
            self.triangle(material, (x0, base, z0), (0.0, apex, 0.0), (x1, base, z1))
            self.triangle(material, (0.0, base, 0.0), (x0, base, z0), (x1, base, z1))


def tree(lod):
    mesh = MeshBuilder([BARK, LEAVES])
    if lod == 0:
        mesh.prism("bark", 8, 0.16, 0.0, 2.4)
        mesh.cone("leaves", 12, 1.6, 1.4, 3.6)
        mesh.cone("leaves", 12, 1.25, 2.6, 4.6, 0.26)
        mesh.cone("leaves", 12, 0.85, 3.6, 5.4)
    elif lod == 1:
        mesh.prism("bark", 4, 0.17, 0.0, 2.0)
        mesh.cone("leaves", 6, 1.6, 1.4, 4.0)
        mesh.cone("leaves", 6, 1.05, 3.0, 5.4, 0.52)
    else:
        mesh.prism("bark", 4, 0.17, 0.0, 1.4)
        mesh.cone("leaves", 4, 1.7, 1.4, 5.4, math.pi / 4.0)
    return mesh


def pebble(lod):
    """A stone squashed to 24 by 13 by 19 cm, its middle 3 cm up (sunk a little into the ground)."""
    mesh = MeshBuilder([STONE])
    scale = (0.12, 0.065, 0.095)
    lift = 0.03
    if lod == 0:
        t = (1.0 + math.sqrt(5.0)) / 2.0
        corners = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
                   (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
        faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2),
                 (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11),
                 (6, 2, 10), (8, 6, 7), (9, 8, 1)]
        jitter = random.Random(7)
        points = []
        for x, y, z in corners:
            length = math.sqrt(x * x + y * y + z * z)
            wobble = 1.0 + jitter.uniform(-0.12, 0.12)
            points.append((x / length * wobble, y / length * wobble, z / length * wobble))
    else:
        points = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]
        faces = [(2, 4, 0), (2, 0, 5), (2, 5, 1), (2, 1, 4), (3, 0, 4), (3, 5, 0), (3, 1, 5), (3, 4, 1)]
    placed = [(p[0] * scale[0], (p[1] * scale[1]) + lift, p[2] * scale[2]) for p in points]
    for a, b, c in faces:
        # Each face counter-clockwise from outside: flip those whose normal points inward.
        centre = tuple((placed[a][k] + placed[b][k] + placed[c][k]) / 3.0 for k in range(3))
        normal = cross(subtract(placed[b], placed[a]), subtract(placed[c], placed[a]))
        outward = subtract(centre, (0.0, lift, 0.0))
        if sum(normal[k] * outward[k] for k in range(3)) < 0.0:
            b, c = c, b
        mesh.triangle("stone", placed[a], placed[b], placed[c])
    return mesh


# The binary glTF writer.
class GltfWriter:
    def __init__(self):
        self.binary = bytearray()
        self.buffer_views = []
        self.accessors = []

    def add_view(self, data, target):
        while len(self.binary) % 4:
            self.binary.append(0)
        self.buffer_views.append({"buffer": 0, "byteOffset": len(self.binary), "byteLength": len(data),
                                  "target": target})
        self.binary += data
        return len(self.buffer_views) - 1

    def add_accessor(self, data, component_type, count, type_name, target, bounds=None):
        accessor = {"bufferView": self.add_view(data, target), "componentType": component_type, "count": count,
                    "type": type_name}
        if bounds is not None:
            accessor["min"], accessor["max"] = bounds
        self.accessors.append(accessor)
        return len(self.accessors) - 1

    def write(self, path, document):
        document["asset"] = {"version": "2.0", "generator": "TheBenchProject placeholder_instances.py"}
        document["bufferViews"] = self.buffer_views
        document["accessors"] = self.accessors
        while len(self.binary) % 4:
            self.binary.append(0)
        document["buffers"] = [{"byteLength": len(self.binary)}]
        text = json.dumps(document, separators=(",", ":")).encode("utf-8")
        while len(text) % 4:
            text += b" "
        total = 12 + 8 + len(text) + 8 + len(self.binary)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as output:
            output.write(struct.pack("<III", 0x46546C67, 2, total))
            output.write(struct.pack("<II", len(text), 0x4E4F534A))
            output.write(text)
            output.write(struct.pack("<II", len(self.binary), 0x004E4942))
            output.write(self.binary)
        print("wrote %s (%d bytes)" % (os.path.relpath(path, REPOSITORY), total))


def write_glb(path, mesh):
    writer = GltfWriter()
    primitives = []
    triangle_count = 0
    for index, (name, _) in enumerate(mesh.materials):
        positions, normals, indices = mesh.rows[name]
        if not positions:
            continue
        bounds = ([min(p[k] for p in positions) for k in range(3)], [max(p[k] for p in positions) for k in range(3)])
        attributes = {
            "POSITION": writer.add_accessor(b"".join(struct.pack("<3f", *p) for p in positions), FLOAT,
                len(positions), "VEC3", ARRAY_BUFFER, bounds),
            "NORMAL": writer.add_accessor(b"".join(struct.pack("<3f", *n) for n in normals), FLOAT, len(normals),
                "VEC3", ARRAY_BUFFER),
        }
        index_data = struct.pack("<%dH" % len(indices), *indices)
        primitives.append({"attributes": attributes, "indices": writer.add_accessor(index_data, UNSIGNED_SHORT,
            len(indices), "SCALAR", ELEMENT_ARRAY_BUFFER), "material": index})
        triangle_count += len(indices) // 3
    document = {"scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"name": "placeholder", "mesh": 0}],
                "meshes": [{"name": "placeholder", "primitives": primitives}],
                "materials": [{"name": name, "pbrMetallicRoughness": {
                    "baseColorFactor": [color[0], color[1], color[2], 1.0], "metallicFactor": 0.0,
                    "roughnessFactor": 0.9}} for name, color in mesh.materials]}
    writer.write(path, document)
    print("  %d triangles" % triangle_count)


def write_text(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as output:
        output.write(text)
    print("wrote %s" % os.path.relpath(path, REPOSITORY))


def write_model_definition(name, location):
    write_text(os.path.join(MODEL_DIRECTORY, location + ".json"),
        '{\n    "name": "%s",\n    "location": "placeholder_instances/%s"\n}\n' % (name, location))


TREE_WORLD_MODEL = """{
    "name": "test_tree",
    "lods": [
        { "model": "test_tree_lod0", "max_distance": 30 },
        { "model": "test_tree_lod1", "max_distance": 70 },
        { "model": "test_tree_lod2", "max_distance": 250 }
    ],
    "lod_hysteresis": 0.1
}
"""

PEBBLE_WORLD_MODEL = """{
    "name": "test_pebble",
    "lods": [
        { "model": "test_pebble_lod0", "max_distance": 12 },
        { "model": "test_pebble_lod1", "max_distance": 45 }
    ],
    "lod_hysteresis": 0.15
}
"""


# The grove.
def cell_of(lines, value):
    """The index of the grid cell holding a coordinate (lines[i] <= value < lines[i + 1])."""
    for index in range(len(lines) - 1):
        if lines[index] <= value < lines[index + 1]:
            return index
    raise ValueError("%.3f is off the terrain's grid" % value)


def mesh_point(lines, x, z):
    """The terrain mesh's height and face normal at (x, z): the triangle of its grid cell the point falls in, split
    from (x1, z0) to (x0, z1) as placeholder_terrain.build() splits it."""
    i = cell_of(lines, x)
    j = cell_of(lines, z)
    x0, x1, z0, z1 = lines[i], lines[i + 1], lines[j], lines[j + 1]
    a = terrain.vertex_position(x0, z0)
    b = terrain.vertex_position(x1, z0)
    c = terrain.vertex_position(x0, z1)
    d = terrain.vertex_position(x1, z1)
    u = (x - x0) / (x1 - x0)
    v = (z - z0) / (z1 - z0)
    if u + v <= 1.0:
        height = a[1] + (u * (b[1] - a[1])) + (v * (c[1] - a[1]))
        normal = normalize(cross(subtract(c, a), subtract(b, a)))
    else:
        height = d[1] + ((1.0 - u) * (c[1] - d[1])) + ((1.0 - v) * (b[1] - d[1]))
        normal = normalize(cross(subtract(c, b), subtract(d, b)))
    return height, normal


def grove():
    """The trees: (position, yaw degrees, scale, tint) each."""
    lines = terrain.grid_lines()
    rng = random.Random(GROVE_SEED)
    trees = []
    attempts = 0
    while len(trees) < GROVE_COUNT:
        attempts += 1
        if attempts > 200000:
            raise RuntimeError("only %d trees found room" % len(trees))
        x = rng.uniform(*GROVE_AREA[0])
        z = rng.uniform(*GROVE_AREA[1])
        if (terrain.cliff_share(x) > 0.0) and (z < terrain.CLIFF_FOOT_Z + 2.0):
            continue  # the cliff's free mesh is no heightfield
        height, normal = mesh_point(lines, x, z)
        _, density = terrain.paint((x, height, z), normal)
        if rng.random() >= density:
            continue
        if any(math.hypot(x - other[0][0], z - other[0][2]) < GROVE_SPACING for other in trees):
            continue
        yaw = round(rng.uniform(0.0, 360.0), 1)
        size = round(rng.uniform(0.8, 1.3), 2)
        scale = (size, round(size * 1.2, 2), size) if (len(trees) % 3) == 2 else size
        alpha = 0x80 if (len(trees) % GROVE_HALF_TRANSPARENT_EVERY) == (GROVE_HALF_TRANSPARENT_EVERY - 1) else 0xff
        tint = "#%02x%02x%02x%02x" % (rng.randint(205, 255), rng.randint(225, 255), rng.randint(205, 245), alpha)
        trees.append(((round(x, 3), round(height, 3), round(z, 3)), yaw, scale, tint))
    print("the grove: %d trees (%d candidates)" % (len(trees), attempts))
    return trees


def lamp_position(trees):
    """A clearing for the lamp: the whole metre nearest LAMP_NEAR at least LAMP_CLEARANCE from every trunk, LAMP_HEIGHT
    above the terrain's mesh there."""
    lines = terrain.grid_lines()
    candidates = sorted(((x, z) for x in range(-20, 21) for z in range(25, 66)),
        key=lambda point: math.hypot(point[0] - LAMP_NEAR[0], point[1] - LAMP_NEAR[1]))
    for x, z in candidates:
        if all(math.hypot(x - tree[0][0], z - tree[0][2]) >= LAMP_CLEARANCE for tree in trees):
            height, _ = mesh_point(lines, float(x), float(z))
            return [float(x), round(height + LAMP_HEIGHT, 3), float(z)]
    raise RuntimeError("no clearing for the lamp")


def write_grove(trees):
    with open(WORLD_FILE) as source:
        world = json.load(source)
    instances = []
    for position, yaw, scale, tint in trees:
        instance = {"position": list(position), "rotation": [0.0, yaw, 0.0], "scale": scale}
        if tint != "#ffffffff":
            instance["tint"] = tint
        instances.append(instance)
    obj = {"id": GROVE_ID, "name": GROVE_NAME,
           "components": {"model_instances": {"world_model": "test_tree", "instances": instances}}}
    lamp = {"id": LAMP_ID, "name": LAMP_NAME, "position": lamp_position(trees), "components": {"light": LAMP_LIGHT}}
    world["objects"] = [other for other in world["objects"] if other.get("id") not in (GROVE_ID, LAMP_ID)] + [obj, lamp]
    world["next_object_id"] = max(world["next_object_id"], LAMP_ID + 1)
    with open(WORLD_FILE, "w") as output:
        json.dump(world, output, indent=4)
        output.write("\n")
    print("wrote %s (object %d, \"%s\": %d trees; object %d, \"%s\", at %s)" % (os.path.relpath(WORLD_FILE, REPOSITORY),
        GROVE_ID, GROVE_NAME, len(instances), LAMP_ID, LAMP_NAME, lamp["position"]))


def main():
    for lod in range(3):
        write_glb(os.path.join(MODEL_DIRECTORY, "tree_lod%d.glb" % lod), tree(lod))
        write_model_definition("test_tree_lod%d" % lod, "tree_lod%d" % lod)
    for lod in range(2):
        write_glb(os.path.join(MODEL_DIRECTORY, "pebble_lod%d.glb" % lod), pebble(lod))
        write_model_definition("test_pebble_lod%d" % lod, "pebble_lod%d" % lod)
    write_text(os.path.join(WORLD_MODEL_DIRECTORY, "test_tree.json"), TREE_WORLD_MODEL)
    write_text(os.path.join(WORLD_MODEL_DIRECTORY, "test_pebble.json"), PEBBLE_WORLD_MODEL)
    write_grove(grove())


if __name__ == "__main__":
    main()
