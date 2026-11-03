#!/usr/bin/env python3
"""Generates the test world's far terrain: a coast around the town, as terrain tiles of binary glTF (.glb) and their
definitions.

Run it from anywhere (only the Python standard library is needed):

    python3 asset_source/scripts/placeholder_far_terrain.py

It writes, relative to the repository root:

    compile/out/asset/terrains/test_world/far/test_far_terrain_<a>_<b>.glb   each tile's mesh
    compile/out/asset/terrains/test_world/far/test_far_terrain_<a>_<b>.json  its definition (tbp:test_far_terrain_<a>_<b>)

The land lies north of the town (toward -Z) and the sea south of it (+Z), along a wavy coast that runs east to west
through it:

    - hills 3 to 8 km out (up to about 150 m), rising from the coast;
    - ridged mountains 10 to 40 km out on the land side, their peaks up to about 1.5 km;
    - the sea floor 40 m below sea level on the other side, so the test frame's sea (a water plane just under the town's
      level) has a shore; nothing is built far out to sea;
    - the ground falls to the far ground's height (0) at the inner edge, 2.5 km from the town, where the town's own
      terrain is far behind.

Its faces grow with their distance from the town: a polar grid whose rings lie 2 % of their radius apart and whose
segments are 2 % of their radius wide (50 m at 2.5 km, 800 m at 40 km), so the far terrain looks the same at every
distance at the picture's resolution for a fraction of the triangles of an even grid (references/asset_structure.md,
"Terrains", density). The grid is cut into tiles on a 14 km pitch: each triangle goes to the tile nearest its centre, and
every vertex stays inside a terrain's extent (8192 m from its origin). A tile's name carries its place: tile <a>_<b> has
its origin at ((a - 4) x 14 km, 0, (b - 4) x 14 km) in the world, which the test frame reads back from the name. Each
draws in 4 km chunks, to 100 km, casts shadows and has no collision (nothing walks there).

The palette is the placeholder terrain's four materials: grass where nothing else applies, rock on slopes over 30
degrees and above 1100 m, gravel on the shore (below 5 m).

Conventions: metres, +Y up, faces counter-clockwise seen from above. Placeholder art for checking the code, not game art:
replace it with the owner's far terrain.
"""

import json
import math
import os
import struct


# Paths.
REPOSITORY = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
TERRAIN_DIRECTORY = os.path.join(REPOSITORY, "compile", "out", "asset", "terrains", "test_world", "far")
MODEL_PREFIX = "test_world/far/"
NAME_PREFIX = "test_far_terrain_"

# glTF constants.
FLOAT = 5126
UNSIGNED_INT = 5125
ARRAY_BUFFER = 34962
ELEMENT_ARRAY_BUFFER = 34963

# The polar grid: from the inner edge to the outer one, each ring and segment 2 % of the radius.
INNER_RADIUS = 2500.0
OUTER_RADIUS = 42000.0
DENSITY = 0.02
# Tiles: the pitch between their origins, the index offset in their names, the largest distance a vertex may lie from
# its tile's origin (a terrain's extent is 8192 m).
TILE_PITCH = 14000.0
TILE_INDEX_OFFSET = 4
TILE_EXTENT = 8190.0
# The coast: its mean line (world z; land toward -z), how far it wanders, the band the land rises over, and the
# farthest out to sea (world z) a quad is built.
COAST_Z = 0.0
COAST_WANDER = 1800.0
SHORE_BAND = 1500.0
SEA_EDGE_Z = 4000.0
SEA_FLOOR = -40.0
# The ground near the inner edge: it rises from the far ground's height over this distance.
INNER_RISE = 1500.0
# The hills and the mountains.
HILL_HEIGHT = 150.0
MOUNTAIN_HEIGHT = 1500.0
CHUNK_SIZE = 4096.0
DRAW_DISTANCE = 100000.0


def smoothstep(edge0, edge1, x):
    t = min(max((x - edge0) / (edge1 - edge0), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def hash2(ix, iz, seed):
    h = (ix * 374761393 + iz * 668265263 + seed * 1442695041) & 0xffffffff
    h = ((h ^ (h >> 13)) * 1274126177) & 0xffffffff
    return ((h ^ (h >> 16)) & 0xffffff) / 16777215.0


def value_noise(x, z, seed):
    """Smooth value noise in [0, 1] with cells of one unit."""
    ix = math.floor(x)
    iz = math.floor(z)
    fx = x - ix
    fz = z - iz
    ux = fx * fx * (3.0 - 2.0 * fx)
    uz = fz * fz * (3.0 - 2.0 * fz)
    a = hash2(ix, iz, seed)
    b = hash2(ix + 1, iz, seed)
    c = hash2(ix, iz + 1, seed)
    d = hash2(ix + 1, iz + 1, seed)
    return a + (b - a) * ux + (c - a) * uz + (a - b - c + d) * ux * uz


def ridges(x, z):
    """Ridged noise in [0, 1]: sharp crests where the noise crosses its middle, five octaves from 7 km cells."""
    total, amplitude, norm, frequency = 0.0, 0.5, 0.0, 1.0 / 7000.0
    for octave in range(5):
        n = 1.0 - abs(2.0 * value_noise(x * frequency, z * frequency, 17 + octave) - 1.0)
        total += n * n * amplitude
        norm += amplitude
        amplitude *= 0.5
        frequency *= 2.03
    return total / norm


def coast_line(x):
    """The coast's z at x: the mean line wandering by up to COAST_WANDER."""
    return COAST_Z + COAST_WANDER * (0.6 * math.sin(x / 9000.0) + 0.4 * (2.0 * value_noise(x / 3500.0, 0.5, 3) - 1.0))


def ground_height(x, z):
    r = math.hypot(x, z)
    land = smoothstep(-SHORE_BAND, SHORE_BAND, coast_line(x) - z)
    hills = HILL_HEIGHT * value_noise(x / 1800.0, z / 1800.0, 5) * smoothstep(INNER_RADIUS, 4000.0, r)
    mountains = MOUNTAIN_HEIGHT * ridges(x, z) * smoothstep(8000.0, 14000.0, r)
    land_height = 3.0 + hills + mountains
    rise = smoothstep(INNER_RADIUS, INNER_RADIUS + INNER_RISE, r)
    far = land * land_height + (1.0 - land) * SEA_FLOOR
    near = (1.0 - land) * -2.0
    return rise * far + (1.0 - rise) * near


def build():
    """The polar grid's vertices and its triangles by tile."""
    segments = int(round(2.0 * math.pi / DENSITY))
    radii = [INNER_RADIUS]
    while radii[-1] < OUTER_RADIUS:
        radii.append(radii[-1] * (1.0 + DENSITY))
    positions = []
    for r in radii:
        for j in range(segments):
            angle = 2.0 * math.pi * j / segments
            x, z = r * math.cos(angle), r * math.sin(angle)
            positions.append((x, ground_height(x, z), z))

    def vertex(k, j):
        return k * segments + (j % segments)

    tiles = {}
    for k in range(len(radii) - 1):
        for j in range(segments):
            a, b = vertex(k, j), vertex(k, j + 1)
            c, d = vertex(k + 1, j), vertex(k + 1, j + 1)
            if min(positions[v][2] for v in (a, b, c, d)) > SEA_EDGE_Z:
                continue  # all of it far out to sea
            # Counter-clockwise seen from above (+Y), so the faces (and the smooth normals) face up: seen from above, +X
            # to the right, +Z points down the page, so the angle grows clockwise there; a quad's corners a, b (the next
            # segment), d, c (the next ring) run counter-clockwise.
            for triangle in ((a, b, c), (b, d, c)):
                cx = sum(positions[v][0] for v in triangle) / 3.0
                cz = sum(positions[v][2] for v in triangle) / 3.0
                key = (int(math.floor(cx / TILE_PITCH + 0.5)), int(math.floor(cz / TILE_PITCH + 0.5)))
                tiles.setdefault(key, []).append(triangle)
    return positions, tiles


def write_glb(path, name, positions, indices):
    binary = bytearray()
    views = []
    accessors = []

    def add(data, component, count, type_name, target, bounds=None):
        while len(binary) % 4:
            binary.append(0)
        views.append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(data), "target": target})
        binary.extend(data)
        accessor = {"bufferView": len(views) - 1, "componentType": component, "count": count, "type": type_name}
        if bounds is not None:
            accessor["min"], accessor["max"] = bounds
        accessors.append(accessor)
        return len(accessors) - 1

    rows = [positions[i:i + 3] for i in range(0, len(positions), 3)]
    bounds = ([min(row[k] for row in rows) for k in range(3)], [max(row[k] for row in rows) for k in range(3)])
    position_data = struct.pack("<%df" % len(positions), *positions)
    index_data = struct.pack("<%dI" % len(indices), *indices)
    primitive = {"attributes": {"POSITION": add(position_data, FLOAT, len(rows), "VEC3", ARRAY_BUFFER, bounds)},
                 "indices": add(index_data, UNSIGNED_INT, len(indices), "SCALAR", ELEMENT_ARRAY_BUFFER), "material": 0}
    document = {"asset": {"version": "2.0", "generator": "TheBenchProject placeholder_far_terrain.py"}, "scene": 0,
                "scenes": [{"nodes": [0]}], "nodes": [{"name": name, "mesh": 0}],
                "meshes": [{"name": name, "primitives": [primitive]}], "materials": [{"name": "blend"}],
                "bufferViews": views, "accessors": accessors}
    while len(binary) % 4:
        binary.append(0)
    document["buffers"] = [{"byteLength": len(binary)}]
    text = json.dumps(document, separators=(",", ":")).encode("utf-8")
    while len(text) % 4:
        text += b" "
    with open(path, "wb") as output:
        output.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(binary)))
        output.write(struct.pack("<II", len(text), 0x4E4F534A))
        output.write(text)
        output.write(struct.pack("<II", len(binary), 0x004E4942))
        output.write(binary)


def write_definition(path, name):
    definition = {
        "name": "tbp:" + name,
        "source": "model",
        "model": MODEL_PREFIX + name,
        "materials": ["tbp:grass", "tbp:dirt", "tbp:rock", "tbp:gravel"],
        "chunk_size": CHUNK_SIZE,
        "render": {"draw_distance": DRAW_DISTANCE},
        "collision": {"is_enabled": False},
        "rules": [
            {"material": "tbp:rock", "slope": {"min": 30.0, "blend": 8.0}, "only_on": ["tbp:grass"]},
            {"material": "tbp:rock", "height": {"min": 1100.0, "blend": 300.0}, "only_on": ["tbp:grass"]},
            {"material": "tbp:gravel", "height": {"max": 5.0, "blend": 4.0}, "only_on": ["tbp:grass"]},
        ],
    }
    with open(path, "w") as output:
        json.dump(definition, output, indent=4)
        output.write("\n")


def main():
    positions, tiles = build()
    os.makedirs(TERRAIN_DIRECTORY, exist_ok=True)
    for entry in os.listdir(TERRAIN_DIRECTORY):
        if entry.startswith(NAME_PREFIX):
            os.remove(os.path.join(TERRAIN_DIRECTORY, entry))
    total_vertices = 0
    total_triangles = 0
    for (ix, iz), triangles in sorted(tiles.items()):
        origin_x, origin_z = ix * TILE_PITCH, iz * TILE_PITCH
        local = {}
        tile_positions = []
        indices = []
        for triangle in triangles:
            for v in triangle:
                if v not in local:
                    x, y, z = positions[v]
                    if (abs(x - origin_x) > TILE_EXTENT) or (abs(z - origin_z) > TILE_EXTENT):
                        raise SystemExit("a vertex lies outside its tile's extent: raise the density or the pitch")
                    local[v] = len(tile_positions) // 3
                    tile_positions.extend((x - origin_x, y, z - origin_z))
                indices.append(local[v])
        name = "%s%d_%d" % (NAME_PREFIX, ix + TILE_INDEX_OFFSET, iz + TILE_INDEX_OFFSET)
        write_glb(os.path.join(TERRAIN_DIRECTORY, name + ".glb"), name, tile_positions, indices)
        write_definition(os.path.join(TERRAIN_DIRECTORY, name + ".json"), name)
        total_vertices += len(tile_positions) // 3
        total_triangles += len(indices) // 3
    print("wrote %d tiles into %s: %d vertices, %d triangles" % (len(tiles), os.path.relpath(TERRAIN_DIRECTORY,
        REPOSITORY), total_vertices, total_triangles))


if __name__ == "__main__":
    main()
