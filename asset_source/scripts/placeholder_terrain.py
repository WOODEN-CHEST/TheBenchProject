#!/usr/bin/env python3
"""Generates the placeholder terrain for the test world, as binary glTF (.glb) the terrain asset imports (painted
for its terrain materials).

Run it from anywhere (only the Python standard library is needed):

    python3 asset_source/scripts/placeholder_terrain.py

It writes, relative to the repository root:

    compile/out/asset/terrains/test_world/placeholder_terrain.glb   the mesh, as Blender 5 exports one
    compile/out/asset/terrains/test_world/placeholder_terrain.json  its definition (the palette, the mappings)

The terrain is a ring around the test world's ground slab (20 m square, its top at y = 0.05) out to 250 m:

    - its inner edge meets the slab's top, with a flat apron to walk off onto;
    - rolling hills, and a valley along Z to the east (x 60 to 120 m) whose floor lies 6 m below the slab;
    - a cliff to the north (its foot at z = -30 m) steeper than 35 degrees, 10 m high, whose top lip overhangs its foot
      (rows of the free mesh pushed back over the ones below: no heightfield could hold it), with a plateau behind it
      and steep sides;
    - a cave mouth in the cliff's foot: its faces are in the "hole" slot, and a short tunnel (a second piece of the same
      mesh, its faces facing in) shares the hole's edges, so the ground runs into it;
    - 1 m quads near the slab, then 2, 4 and 8 m ones farther out (a rectilinear grid: far out along the axes its quads
      are long and thin);
    - an outer edge that slopes down to y = 0.

Its data, as the definition maps them: COLOR_0's red, green, blue and alpha are the weights of grass, dirt, rock and
gravel (the terrain materials: asset_source/scripts/placeholder_terrain_materials.py), painted by place: dirt on the
path east of the slab, a round patch of gravel north-east of the slab (soft at its rim), grass elsewhere. The rock on the
steep and the dirt on the valley floor are not painted: the definition's rules put them there (rock above 35 degrees,
dirt below 4.5 m under the slab, broken up by noise). _DENSITY_0 is the trees' density (on gentle hills, none near the
slab, on the path or on the cliff); and a patch of faces west of the slab is in the "dirt" slot, so it takes dirt
outright, with hard edges.

Conventions: metres, +Y up (glTF's, as Blender's exporter writes it with +Y up), faces counter-clockwise seen from
their front (up for the ground; inward for the tunnel). One primitive per material slot, each with its own vertices,
as Blender exports them; the import welds them back by position.

Placeholder art for checking the code, not game art: replace it with the owner's terrain.
"""

import json
import math
import os
import struct


# Paths.
REPOSITORY = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
TERRAIN_DIRECTORY = os.path.join(REPOSITORY, "compile", "out", "asset", "terrains", "test_world")

# glTF constants.
FLOAT = 5126
UNSIGNED_SHORT = 5123
UNSIGNED_INT = 5125
ARRAY_BUFFER = 34962
ELEMENT_ARRAY_BUFFER = 34963

# The slab: a 20 m square around the origin, its top at y = 0.05.
SLAB_HALF = 10.0
SLAB_TOP = 0.05
# The grid's lines along each axis: 1 m out to 40 m, then 2 m to 80, 4 m to 140 and 8 m to 248.
BANDS = [(40.0, 1.0), (80.0, 2.0), (140.0, 4.0), (248.0, 8.0)]
# The cliff (north): its foot at z = -30, rising over four 1 m rows to a plateau 14 m deep, which falls back to the
# ground over the 16 m behind it; the cave mouth in its foot between these x.
CLIFF_X = (-18.0, 6.0)
CLIFF_FOOT_Z = -30.0
CLIFF_RISE = 10.0
PLATEAU_DEPTH = (18.0, 34.0)
OVERHANG = 4.0
CAVE_X = (-8.0, -5.0)
CAVE_DEPTH = 6.0
# The valley (east), the path (east of the slab, along +X) and the hard dirt patch (west of the slab).
VALLEY_X = (60.0, 120.0)
VALLEY_FLOOR = -6.0
PATH_HALF_WIDTH = 1.5
DIRT_PATCH = ((-30.0, -24.0), (14.0, 20.0))
# The painted gravel: a round patch north-east of the slab, in the start view: its centre (x, z), its full radius
# and the soft rim beyond it.
GRAVEL_PATCH = (6.0, 17.0, 3.5, 3.0)


# The grid.
def grid_lines():
    """The coordinates of the grid's lines along an axis, from -248 to 248 m, symmetric about 0."""
    positive = [0.0]
    for limit, step in BANDS:
        while positive[-1] + step <= limit + 1e-9:
            positive.append(positive[-1] + step)
    return [-value for value in reversed(positive[1:])] + positive


def smoothstep(edge0, edge1, x):
    t = min(max((x - edge0) / (edge1 - edge0), 0.0), 1.0)
    return t * t * (3.0 - (2.0 * t))


def ring_distance(x, z):
    """How far a point lies outside the slab's square (0 on its edge)."""
    return max(abs(x), abs(z)) - SLAB_HALF


def cliff_share(x):
    """1 along the cliff, falling to 0 over 4 m at its ends."""
    return smoothstep(CLIFF_X[0], CLIFF_X[0] + 4.0, x) * (1.0 - smoothstep(CLIFF_X[1] - 4.0, CLIFF_X[1], x))


def ground_height(x, z):
    """The rolling ground, the valley and the outer slope: everything but the cliff."""
    d = ring_distance(x, z)
    hills = (3.0 * math.sin(x / 23.0) * math.cos(z / 29.0)) + (2.0 * math.sin((x + (0.6 * z)) / 17.0)) + 2.5
    hills += 1.5 * math.cos(math.hypot(x, z) / 21.0)
    # The apron: flat at the slab's top for 3 m, then the hills come in over 15 m.
    height = SLAB_TOP + (smoothstep(3.0, 18.0, d) * (hills - SLAB_TOP))
    # The valley: a floor 6 m below the slab, its sides rising over 15 m.
    valley = smoothstep(VALLEY_X[0], VALLEY_X[0] + 15.0, x) * (1.0 - smoothstep(VALLEY_X[1] - 15.0, VALLEY_X[1], x))
    valley *= smoothstep(15.0, 40.0, abs(z) + 25.0)
    height += valley * (VALLEY_FLOOR - height)
    # The outer edge slopes down to 0 over its last 40 m.
    radius = max(abs(x), abs(z))
    return height * (1.0 - smoothstep(208.0, 248.0, radius))


def vertex_position(x, z):
    """A grid point's position: the ground, with the cliff (and its overhang) where it rises."""
    y = ground_height(x, z)
    share = cliff_share(x)
    if share <= 0.0 or z > CLIFF_FOOT_Z:
        return (x, y, z)
    # Past the foot the ground rises by CLIFF_RISE over four rows (the third and fourth leaning back over the foot), stays
    # up across the plateau and falls back behind it.
    s = CLIFF_FOOT_Z - z
    rise = CLIFF_RISE * (min(s / 4.0, 1.0) ** 0.8) * (1.0 - smoothstep(PLATEAU_DEPTH[0], PLATEAU_DEPTH[1], s))
    lean = OVERHANG * (smoothstep(1.0, 2.0, s) - smoothstep(3.0, 4.0, s)) * (1.0 if s < 4.0 else 0.0)
    return (x, y + (share * rise), z + (share * lean))


# Painting.
def paint(position, normal):
    """The grass, dirt, rock and gravel weights (0..1, summing to 1) and the trees' density at a vertex. Rock and the
    valley's dirt come from the definition's rules, not from the paint."""
    x, y, z = position
    steep = 1.0 - normal[1]  # 0 flat, about 0.18 at 35 degrees
    path = (1.0 - smoothstep(PATH_HALF_WIDTH, PATH_HALF_WIDTH + 1.0, abs(z))) * smoothstep(SLAB_HALF - 1.0,
        SLAB_HALF, x) * (1.0 - smoothstep(38.0, 40.0, x))
    floor = smoothstep(-3.0, -5.5, y)
    centre_x, centre_z, radius, rim = GRAVEL_PATCH
    gravel = 1.0 - smoothstep(radius, radius + rim, math.hypot(x - centre_x, z - centre_z))
    dirt = path * (1.0 - gravel)
    rock = 0.0
    grass = max(0.0, 1.0 - dirt - gravel)
    trees = (1.0 - smoothstep(0.05, 0.12, steep)) * smoothstep(20.0, 40.0, ring_distance(x, z)) * (1.0 - path)
    trees *= (1.0 - floor) * (1.0 - smoothstep(200.0, 240.0, max(abs(x), abs(z))))
    trees *= 0.5 + (0.5 * math.sin(x / 13.0) * math.sin(z / 11.0))
    return (grass, dirt, rock, gravel), max(0.0, min(trees, 1.0))


# Geometry.
def subtract(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def normalize(v):
    length = math.sqrt((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]))
    return (v[0] / length, v[1] / length, v[2] / length) if length > 0.0 else (0.0, 1.0, 0.0)


def build():
    """The mesh: positions, triangles (three vertex indices, counter-clockwise from the front) and each triangle's
    slot."""
    lines = grid_lines()
    count = len(lines)
    positions = []
    index_of = {}
    for j, z in enumerate(lines):
        for i, x in enumerate(lines):
            index_of[(i, j)] = len(positions)
            positions.append(vertex_position(x, z))
    triangles = []
    slots = []
    for j in range(count - 1):
        for i in range(count - 1):
            x0, x1 = lines[i], lines[i + 1]
            z0, z1 = lines[j], lines[j + 1]
            center_x = 0.5 * (x0 + x1)
            center_z = 0.5 * (z0 + z1)
            if max(abs(center_x), abs(center_z)) < SLAB_HALF:
                continue  # the slab's square stays open
            a, b, c, d = index_of[(i, j)], index_of[(i + 1, j)], index_of[(i, j + 1)], index_of[(i + 1, j + 1)]
            slot = "blend"
            # The cave mouth: the cliff's foot band (its first row) between the cave's sides.
            if CAVE_X[0] <= center_x <= CAVE_X[1] and CLIFF_FOOT_Z - 1.0 <= center_z <= CLIFF_FOOT_Z:
                slot = "hole"
            elif DIRT_PATCH[0][0] <= center_x <= DIRT_PATCH[0][1] and DIRT_PATCH[1][0] <= center_z <= DIRT_PATCH[1][1]:
                slot = "dirt"
            # Seen from above (+Y), with X to the right and Z toward the viewer, (a, c, b) is counter-clockwise.
            triangles += [(a, c, b), (b, c, d)]
            slots += [slot, slot]
    # The tunnel: from the mouth's edges (the hole's: the foot row and the row above it) straight back into the
    # cliff, its floor, walls, ceiling and back wall facing in.
    mouth = [x for x in lines if CAVE_X[0] - 1e-9 <= x <= CAVE_X[1] + 1e-9]
    foot_j = lines.index(CLIFF_FOOT_Z)
    above_j = lines.index(CLIFF_FOOT_Z - 1.0)
    back_z = CLIFF_FOOT_Z - CAVE_DEPTH
    floor_front = [index_of[(lines.index(x), foot_j)] for x in mouth]
    ceiling_front = [index_of[(lines.index(x), above_j)] for x in mouth]
    floor_back = []
    ceiling_back = []
    for k, x in enumerate(mouth):
        floor_y = positions[floor_front[k]][1]
        ceiling_y = positions[ceiling_front[k]][1]
        floor_back.append(len(positions))
        positions.append((x, floor_y, back_z))
        ceiling_back.append(len(positions))
        positions.append((x, ceiling_y, back_z - 1.0))
    def quad(p, q, r, s):
        """Two triangles of the quad p, q, r, s, given counter-clockwise from the side that sees them."""
        triangles.extend([(p, q, r), (p, r, s)])
        slots.extend(["blend", "blend"])
    for k in range(len(mouth) - 1):
        # The floor faces up, the ceiling down (X to the right, the tunnel running toward -Z).
        quad(floor_front[k], floor_front[k + 1], floor_back[k + 1], floor_back[k])
        quad(ceiling_front[k], ceiling_back[k], ceiling_back[k + 1], ceiling_front[k + 1])
        # The back wall faces the mouth (+Z).
        quad(floor_back[k], floor_back[k + 1], ceiling_back[k + 1], ceiling_back[k])
    # The side walls face each other: the west one +X, the east one -X.
    quad(floor_front[0], floor_back[0], ceiling_back[0], ceiling_front[0])
    last = len(mouth) - 1
    quad(floor_front[last], ceiling_front[last], ceiling_back[last], floor_back[last])
    return positions, triangles, slots


def vertex_normals(positions, triangles):
    sums = [[0.0, 0.0, 0.0] for _ in positions]
    for a, b, c in triangles:
        n = cross(subtract(positions[b], positions[a]), subtract(positions[c], positions[a]))
        for corner in (a, b, c):
            for axis in range(3):
                sums[corner][axis] += n[axis]
    return [normalize(tuple(total)) for total in sums]


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

    def add_accessor(self, data, component_type, count, type_name, target, normalized=False, bounds=None):
        accessor = {"bufferView": self.add_view(data, target), "componentType": component_type, "count": count,
                    "type": type_name}
        if normalized:
            accessor["normalized"] = True
        if bounds is not None:
            accessor["min"], accessor["max"] = bounds
        self.accessors.append(accessor)
        return len(self.accessors) - 1

    def write(self, path, document):
        document["asset"] = {"version": "2.0", "generator": "TheBenchProject placeholder_terrain.py"}
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


def write_glb(path, positions, triangles, slots):
    normals = vertex_normals(positions, triangles)
    writer = GltfWriter()
    slot_names = ["blend", "hole", "dirt"]
    primitives = []
    for material, name in enumerate(slot_names):
        own = [triangle for triangle, slot in zip(triangles, slots) if slot == name]
        if not own:
            continue
        # Its own vertices, as Blender exports a slot's faces.
        local = {}
        order = []
        for triangle in own:
            for corner in triangle:
                if corner not in local:
                    local[corner] = len(order)
                    order.append(corner)
        rows = [positions[v] for v in order]
        bounds = ([min(row[k] for row in rows) for k in range(3)], [max(row[k] for row in rows) for k in range(3)])
        attributes = {
            "POSITION": writer.add_accessor(b"".join(struct.pack("<3f", *row) for row in rows), FLOAT, len(rows),
                "VEC3", ARRAY_BUFFER, bounds=bounds),
            "NORMAL": writer.add_accessor(b"".join(struct.pack("<3f", *normals[v]) for v in order), FLOAT, len(rows),
                "VEC3", ARRAY_BUFFER),
        }
        colours = []
        densities = []
        for v in order:
            weights, trees = paint(positions[v], normals[v])
            colours.append(struct.pack("<4H", *[int(round(min(max(w, 0.0), 1.0) * 65535.0)) for w in weights]))
            densities.append(struct.pack("<f", trees))
        attributes["COLOR_0"] = writer.add_accessor(b"".join(colours), UNSIGNED_SHORT, len(rows), "VEC4",
            ARRAY_BUFFER, normalized=True)
        attributes["_DENSITY_0"] = writer.add_accessor(b"".join(densities), FLOAT, len(rows), "SCALAR", ARRAY_BUFFER)
        indices = [local[corner] for triangle in own for corner in triangle]
        index_type = UNSIGNED_SHORT if len(order) <= 65535 else UNSIGNED_INT
        index_data = struct.pack("<%d%s" % (len(indices), "H" if index_type == UNSIGNED_SHORT else "I"), *indices)
        primitives.append({"attributes": attributes, "indices": writer.add_accessor(index_data, index_type,
            len(indices), "SCALAR", ELEMENT_ARRAY_BUFFER), "material": material})
        print("  slot %-5s: %6d triangles, %6d vertices" % (name, len(own), len(order)))
    document = {"scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"name": "placeholder_terrain", "mesh": 0}],
                "meshes": [{"name": "placeholder_terrain", "primitives": primitives}],
                "materials": [{"name": name} for name in slot_names]}
    writer.write(path, document)


DEFINITION = """{
    "name": "placeholder_terrain",
    "source": "model",
    "model": "test_world/placeholder_terrain",
    "materials": [ "grass", "dirt", "rock", "gravel" ],
    "density_channels": [ "trees" ],
    "weights": [ { "attribute": "COLOR_0", "r": "grass", "g": "dirt", "b": "rock", "a": "gravel" } ],
    "densities": [ { "attribute": "_DENSITY_0", "r": "trees" } ],
    "rules": [
        { "material": "rock", "slope": { "min": 35.0, "blend": 8.0 }, "only_on": [ "grass", "dirt" ] },
        { "material": "dirt", "height": { "max": -4.5, "blend": 2.5 },
            "noise": { "scale": 2.5, "strength": 0.6, "seed": 11 }, "only_on": [ "grass" ] }
    ]
}
"""


def write_definition(path):
    with open(path, "w") as output:
        output.write(DEFINITION)
    print("wrote %s" % os.path.relpath(path, REPOSITORY))


def main():
    positions, triangles, slots = build()
    used = len({corner for triangle in triangles for corner in triangle})
    print("%d vertices, %d triangles" % (used, len(triangles)))
    write_glb(os.path.join(TERRAIN_DIRECTORY, "placeholder_terrain.glb"), positions, triangles, slots)
    write_definition(os.path.join(TERRAIN_DIRECTORY, "placeholder_terrain.json"))


if __name__ == "__main__":
    main()
