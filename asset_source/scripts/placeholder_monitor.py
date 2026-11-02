#!/usr/bin/env python3
"""Generates the placeholder monitor: a flat screen on a stand whose screen is its own material slot, for showing a UI on
an object in the world, as an OBJ with its MTL, and its model and world model definitions.

Run it from anywhere (only the Python standard library is needed):

    python3 asset_source/scripts/placeholder_monitor.py

It writes, relative to the repository root:

    compile/out/asset/models/placeholder_monitor/monitor.obj         the mesh: a 48 x 37 cm panel 4 cm deep on a neck
                                                                     and a foot, standing on its origin, its screen
                                                                     facing +Z
    compile/out/asset/models/placeholder_monitor/monitor.mtl         its two materials: monitor_body (slot 0, the bezel,
                                                                     back, neck and foot) and monitor_screen (slot 1, the
                                                                     glass: near black, as a screen is when off)
    compile/out/asset/models/placeholder_monitor/monitor.json        the model definition (tbp:test_monitor)
    compile/out/asset/world_models/test_world/test_monitor.json      the world model: drawn crisp, colliding by its
                                                                     bounds, its surface "screen" on slot 1 at 640 x 480

The screen is one quad, 44 x 33 cm (4:3, as its picture), flush with the bezel around it, its texture coordinates 0..1
over it with V up (the OBJ convention: raylib flips V when it loads an OBJ, so the loaded coordinates have (0, 0) at the
picture's top-left). Every other face has the texture coordinates (0, 0) (they carry no texture).

Placeholder art for checking the code, not game art: the real monitor model (or the CRT) replaces it.
"""

import json
import os
import re


# Paths.
REPOSITORY = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
ASSET = os.path.join(REPOSITORY, "compile", "out", "asset")
MODEL_DIRECTORY = os.path.join(ASSET, "models", "placeholder_monitor")
WORLD_MODEL_DIRECTORY = os.path.join(ASSET, "world_models", "test_world")

# The panel (metres): its outer size and depth, and the height of its bottom edge above the foot's underside.
PANEL_WIDTH = 0.48
PANEL_HEIGHT = 0.37
PANEL_DEPTH = 0.04
PANEL_BOTTOM = 0.12
# The screen inside the bezel (4:3, the picture's 640 x 480).
SCREEN_WIDTH = 0.44
SCREEN_HEIGHT = 0.33
# The neck behind the panel and the foot under it: x, y and z ranges.
NECK = ((-0.03, 0.03), (0.02, 0.25), (-0.06, -0.02))
FOOT = ((-0.12, 0.12), (0.0, 0.02), (-0.14, 0.06))

# The materials (raylib reads Kd as the albedo colour; the model definition sets their roughness).
MATERIALS = [
    ("monitor_body", (0.10, 0.10, 0.11)),
    ("monitor_screen", (0.015, 0.015, 0.02)),
]
BODY = 0
SCREEN = 1
PICTURE = [640, 480]
SCREEN_EMISSION = 0.2


class Mesh:
    """Faces grouped by material, with their vertices, normals and texture coordinates indexed as OBJ wants."""

    def __init__(self):
        self.vertices = []
        self.normals = []
        self.texcoords = [(0.0, 0.0)]
        self.faces = {index: [] for index in range(len(MATERIALS))}

    def quad(self, corners, normal, material, texcoords=None):
        """Adds a quad: four corners counter-clockwise seen from the side the normal points to."""
        first = len(self.vertices) + 1
        self.vertices.extend(corners)
        self.normals.append(normal)
        normal_index = len(self.normals)
        if texcoords is None:
            uv = [1, 1, 1, 1]
        else:
            uv = []
            for coordinate in texcoords:
                self.texcoords.append(coordinate)
                uv.append(len(self.texcoords))
        self.faces[material].append([(first + corner, uv[corner], normal_index) for corner in range(4)])

    def box(self, ranges, material, skip_front=False):
        """Adds a box's faces (the front, +Z, left out when asked)."""
        (x0, x1), (y0, y1), (z0, z1) = ranges
        if not skip_front:
            self.quad([(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], (0, 0, 1), material)
        self.quad([(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)], (0, 0, -1), material)
        self.quad([(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)], (-1, 0, 0), material)
        self.quad([(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)], (1, 0, 0), material)
        self.quad([(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)], (0, 1, 0), material)
        self.quad([(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], (0, -1, 0), material)


def build():
    mesh = Mesh()
    half_width = PANEL_WIDTH / 2.0
    top = PANEL_BOTTOM + PANEL_HEIGHT
    front = PANEL_DEPTH / 2.0
    mesh.box(((-half_width, half_width), (PANEL_BOTTOM, top), (-front, front)), BODY, skip_front=True)
    # The front: the bezel's four strips around the screen, and the screen.
    screen_left = -SCREEN_WIDTH / 2.0
    screen_right = SCREEN_WIDTH / 2.0
    screen_bottom = PANEL_BOTTOM + ((PANEL_HEIGHT - SCREEN_HEIGHT) / 2.0)
    screen_top = screen_bottom + SCREEN_HEIGHT
    strips = [
        (-half_width, half_width, PANEL_BOTTOM, screen_bottom),
        (-half_width, half_width, screen_top, top),
        (-half_width, screen_left, screen_bottom, screen_top),
        (screen_right, half_width, screen_bottom, screen_top),
    ]
    for (x0, x1, y0, y1) in strips:
        mesh.quad([(x0, y0, front), (x1, y0, front), (x1, y1, front), (x0, y1, front)], (0, 0, 1), BODY)
    mesh.quad([(screen_left, screen_bottom, front), (screen_right, screen_bottom, front),
               (screen_right, screen_top, front), (screen_left, screen_top, front)], (0, 0, 1), SCREEN,
              texcoords=[(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)])
    mesh.box(NECK, BODY)
    mesh.box(FOOT, BODY)
    return mesh


def write_obj(mesh, path):
    lines = [
        "# The placeholder monitor: a flat screen on a stand, its screen (material monitor_screen, slot 1) a quad whose",
        "# texture coordinates cover 0..1. Written by asset_source/scripts/placeholder_monitor.py; replace it with a real",
        "# monitor.",
        "mtllib monitor.mtl",
        "o Monitor",
    ]
    lines += ["v %.6f %.6f %.6f" % vertex for vertex in mesh.vertices]
    lines += ["vt %.6f %.6f" % texcoord for texcoord in mesh.texcoords]
    lines += ["vn %.6f %.6f %.6f" % normal for normal in mesh.normals]
    for material, (name, _) in enumerate(MATERIALS):
        lines.append("usemtl %s" % name)
        for face in mesh.faces[material]:
            lines.append("f " + " ".join("%d/%d/%d" % corner for corner in face))
    with open(path, "w", encoding="ascii", newline="\n") as file:
        file.write("\n".join(lines) + "\n")


def write_mtl(path):
    lines = ["# The placeholder monitor's materials, in slot order: the body (0) and the screen's glass (1)."]
    for name, colour in MATERIALS:
        lines += ["newmtl %s" % name, "Kd %.4f %.4f %.4f" % colour, ""]
    with open(path, "w", encoding="ascii", newline="\n") as file:
        file.write("\n".join(lines))


def write_json(value, path):
    """Writes JSON as the game's definitions read: four spaces a level, arrays of numbers and the LODs on one line."""
    text = json.dumps(value, indent=4)
    # Arrays of scalars, and objects without nesting, are put back on one line.
    text = re.sub(r"\[\s+([^\[\]{}]*?)\s+\]", lambda match: "[" + re.sub(r",\s+", ", ", match.group(1)) + "]", text)
    text = re.sub(r"\{\s+([^\[\]{}]*?)\s+\}", lambda match: "{ " + re.sub(r",\s+", ", ", match.group(1)) + " }", text)
    with open(path, "w", encoding="ascii", newline="\n") as file:
        file.write(text + "\n")


def main():
    os.makedirs(MODEL_DIRECTORY, exist_ok=True)
    mesh = build()
    write_obj(mesh, os.path.join(MODEL_DIRECTORY, "monitor.obj"))
    write_mtl(os.path.join(MODEL_DIRECTORY, "monitor.mtl"))
    write_json({
        "name": "tbp:test_monitor",
        "location": "placeholder_monitor/monitor",
        "material_overrides": [
            {"slot": BODY, "roughness": 0.6},
            {"slot": SCREEN, "roughness": 0.35},
        ],
    }, os.path.join(MODEL_DIRECTORY, "monitor.json"))
    write_json({
        "name": "tbp:test_monitor",
        "lods": [{"model": "tbp:test_monitor", "max_distance": 60}],
        "render": {"omit_pixelation": True},
        "collision": "bounds",
        "surfaces": [
            {"name": "screen", "material_slot": SCREEN, "resolution": PICTURE, "kind": "screen",
             "emission": SCREEN_EMISSION},
        ],
    }, os.path.join(WORLD_MODEL_DIRECTORY, "test_monitor.json"))
    print("Wrote the placeholder monitor: %d vertices, %d faces." % (
        len(mesh.vertices), sum(len(faces) for faces in mesh.faces.values())))


if __name__ == "__main__":
    main()
