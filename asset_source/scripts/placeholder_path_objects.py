#!/usr/bin/env python3
"""Generates the placeholder content for path objects: a fence's post and panel, a hedge, a rail and a lamp, their world
models and path objects, as binary glTF (.glb) and JSON, and the objects of the test world that use them.

Run it from anywhere (only the Python standard library is needed):

    python3 asset_source/scripts/placeholder_path_objects.py

It writes, relative to the repository root:

    compile/out/asset/models/placeholder_path_objects/*.glb, *.json   the models (post, corner_post, panel and panel_lod1,
                                                                      hedge and hedge_lod1, rail, lamp)
    compile/out/asset/world_models/test_world/test_<model>.json       their world models
    compile/out/asset/path_objects/test_fence.json                    posts and panels, the posts colliding
    compile/out/asset/path_objects/test_hedge.json                    a hedge bent along the path, colliding as its mesh
    compile/out/asset/path_objects/test_railing.json                  a plain rail bent along the path, with posts
    compile/out/asset/path_objects/test_lamp_row.json                 a lamp every 12 m on both sides of the path
    compile/out/worlds/test.world.json                                objects 99 to 110 (replaced when they are there already);
                                                                      save it from the game (F5) and copy the save over it for
                                                                      the writer's exact form

A piece's mesh runs along +X, stands on Y = 0 and is centred on Z = 0, with its side seen from +Z facing right of the path's
direction: the convention of references/asset_structure.md, "Path objects". Colours are the materials' baseColorFactor.

Placeholder art for checking the code, not game art: replace it with the owner's fences.
"""

import json
import math
import os
import sys

sys.dont_write_bytecode = True  # importing the other script must leave no __pycache__ in the repository
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import placeholder_instances as builder  # noqa: E402 (its mesh builder and glTF writer)


# Paths.
REPOSITORY = builder.REPOSITORY
ASSET = builder.ASSET
MODEL_DIRECTORY = os.path.join(ASSET, "models", "placeholder_path_objects")
WORLD_MODEL_DIRECTORY = os.path.join(ASSET, "world_models", "test_world")
PATH_OBJECT_DIRECTORY = os.path.join(ASSET, "path_objects")
WORLD_FILE = builder.WORLD_FILE

# The objects this script owns in the test world.
FIRST_OBJECT_ID = 99
LAST_OBJECT_ID = 110

WOOD = ("wood", (0.46, 0.33, 0.21))
PALE_WOOD = ("pale_wood", (0.62, 0.48, 0.32))
LEAF = ("leaf", (0.20, 0.38, 0.14))
DARK_LEAF = ("dark_leaf", (0.15, 0.30, 0.11))
STEEL = ("steel", (0.32, 0.33, 0.36))


def box(mesh, material, low, high):
    """A closed box, its faces counter-clockwise seen from outside."""
    (x0, y0, z0), (x1, y1, z1) = low, high
    corners = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    faces = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 4, 7, 3), (1, 2, 6, 5), (3, 7, 6, 2), (0, 1, 5, 4)]
    for a, b, c, d in faces:
        mesh.triangle(material, corners[a], corners[b], corners[c])
        mesh.triangle(material, corners[a], corners[c], corners[d])


def post():
    mesh = builder.MeshBuilder([WOOD])
    box(mesh, "wood", (-0.06, 0.0, -0.06), (0.06, 1.25, 0.06))
    return mesh


def corner_post():
    """A heavier post with a pale cap, for the fence's corners."""
    mesh = builder.MeshBuilder([WOOD, PALE_WOOD])
    box(mesh, "wood", (-0.1, 0.0, -0.1), (0.1, 1.35, 0.1))
    box(mesh, "pale_wood", (-0.13, 1.35, -0.13), (0.13, 1.42, 0.13))
    return mesh


def panel(lod):
    """A 2 m panel: two rails and pale slats, modelled from post centre to post centre, so its rails end inside the posts."""
    mesh = builder.MeshBuilder([PALE_WOOD, WOOD])
    box(mesh, "wood", (0.0, 0.32, -0.02), (2.0, 0.42, 0.02))
    box(mesh, "wood", (0.0, 0.95, -0.02), (2.0, 1.05, 0.02))
    if lod == 0:
        for slat in range(9):
            middle = 0.2 + (0.2 * slat)
            box(mesh, "pale_wood", (middle - 0.06, 0.1, 0.02), (middle + 0.06, 1.15, 0.045))
    else:
        box(mesh, "pale_wood", (0.0, 0.1, 0.02), (2.0, 1.15, 0.045))
    return mesh


def hedge(lod):
    """A 2 m block of hedge: a box with a lumpy crown and no edge loops: the game slices it where it bends."""
    mesh = builder.MeshBuilder([LEAF, DARK_LEAF])
    box(mesh, "leaf", (0.0, 0.0, -0.45), (2.0, 1.0, 0.45))
    if lod == 0:
        box(mesh, "dark_leaf", (0.05, 1.0, -0.3), (0.95, 1.2, 0.3))
        box(mesh, "leaf", (1.0, 1.0, -0.35), (1.95, 1.15, 0.35))
    return mesh


def rail():
    """A plain rail, 1 m long: one box with its ends open to nothing but a neighbour's."""
    mesh = builder.MeshBuilder([STEEL])
    box(mesh, "steel", (0.0, 0.92, -0.03), (1.0, 1.0, 0.03))
    box(mesh, "steel", (0.0, 0.5, -0.02), (1.0, 0.55, 0.02))
    return mesh


def lamp():
    """A lamp: a pole at its origin and an arm over the path, to the -Z of the right-hand copy."""
    mesh = builder.MeshBuilder([STEEL])
    box(mesh, "steel", (-0.06, 0.0, -0.06), (0.06, 4.2, 0.06))
    box(mesh, "steel", (-0.04, 4.1, -1.4), (0.04, 4.2, 0.0))
    box(mesh, "steel", (-0.12, 4.0, -1.6), (0.12, 4.12, -1.2))
    return mesh


def write_model(name, mesh):
    print(name)
    builder.write_glb(os.path.join(MODEL_DIRECTORY, name + ".glb"), mesh)
    builder.write_text(os.path.join(MODEL_DIRECTORY, name + ".json"),
        '{\n    "name": "tbp:test_%s",\n    "location": "placeholder_path_objects/%s"\n}\n' % (name, name))


def write_world_model(name, lods, draw_distance, collision=None):
    rows = ",\n".join('        { "model": "tbp:test_%s", "max_distance": %s }' % (lod, distance) for lod, distance in lods)
    extra = (',\n    "collision": %s' % json.dumps(collision)) if collision is not None else ""
    builder.write_text(os.path.join(WORLD_MODEL_DIRECTORY, "test_" + name + ".json"),
        '{\n    "name": "tbp:test_%s",\n    "lods": [\n%s\n    ],\n    "draw_distance": %s%s\n}\n'
        % (name, rows, draw_distance, extra))


def write_path_object(name, definition):
    definition = dict(name="tbp:" + name, **definition)
    builder.write_text(os.path.join(PATH_OBJECT_DIRECTORY, name + ".json"), json.dumps(definition, indent=4) + "\n")


def slope_fence(object_id, name, z, slope):
    """A fence down the side of the valley, the slope's rule its placement's."""
    return {
        "id": object_id,
        "name": name,
        "components": {
            "path": {"nodes": [{"position": [50.0, 3.0, z], "mode": "corner"}, {"position": [74.0, -3.0, z], "mode": "corner"}]},
            "path_object": {"path_object": "tbp:test_fence", "slots": [{"slot": "middle", "overrides_slope": True, "slope": slope}]},
        },
    }


def world_objects():
    """The test world's objects: what the game's own save of them looks like."""
    return [
        # The walk's fence: a child of the path, so moving one of the walk's nodes (Shift + Backspace) moves it live.
        {"id": 99, "name": "walk_fence", "parent": 95, "components": {"path_object": {"path_object": "tbp:test_fence"}}},
        # Its row of lamps, a child too, on both sides, standing off the walk.
        {"id": 100, "name": "walk_lamps", "parent": 95, "components": {"path_object": {"path_object": "tbp:test_lamp_row"}}},
        # The park's hedge: a closed ring has no caps and one joint where it closes.
        {"id": 101, "name": "park_hedge", "parent": 96, "components": {"path_object": {"path_object": "tbp:test_hedge"}}},
        # A fence floating over the slab, the same asset as the walk's: only its placement says it does not snap.
        {
            "id": 102, "name": "floating_fence",
            "components": {
                "path": {"nodes": [
                    {"position": [-14.0, 3.0, 14.0], "mode": "corner"},
                    {"position": [-7.0, 3.5, 20.0]},
                    {"position": [0.0, 4.5, 17.0]},
                    {"position": [6.0, 3.0, 22.0], "mode": "corner"},
                ]},
                "path_object": {"path_object": "tbp:test_fence", "overrides_snaps_to_terrain": True, "snaps_to_terrain": False},
            },
        },
        # A railing bent along a tight curve: plain boxes with no edge loops.
        {
            "id": 103, "name": "bent_railing",
            "components": {
                "path": {"nodes": [
                    {"position": [-22.0, 2.0, -6.0]},
                    {"position": [-27.0, 2.0, -1.0]},
                    {"position": [-27.0, 2.0, 6.0]},
                    {"position": [-22.0, 2.0, 11.0]},
                ]},
                "path_object": {"path_object": "tbp:test_railing"},
            },
        },
        # Three fences down the valley's side, for how a piece takes a slope.
        slope_fence(104, "slope_shear", -6.0, "shear"),
        slope_fence(105, "slope_tilt", -11.0, "tilt"),
        slope_fence(106, "slope_step", -16.0, "step"),
        # The same fence fixed, its panels and gaps by each alignment: the fit's table as objects.
        {
            "id": 107, "name": "fixed_between",
            "components": {
                "path": {"nodes": [{"position": [-44.0, 1.0, -6.0], "mode": "corner"}, {"position": [-33.5, 1.0, -6.0], "mode": "corner"}]},
                "path_object": {"path_object": "tbp:test_fence", "overrides_fit": True, "fit": "fixed", "overrides_align": True,
                    "align": "between", "overrides_has_separator": True, "has_separator": False},
            },
        },
        {
            "id": 108, "name": "fixed_center",
            "components": {
                "path": {"nodes": [{"position": [-44.0, 1.0, -3.0], "mode": "corner"}, {"position": [-33.5, 1.0, -3.0], "mode": "corner"}]},
                "path_object": {"path_object": "tbp:test_fence", "overrides_fit": True, "fit": "fixed", "overrides_align": True,
                    "align": "center"},
            },
        },
        # A fence whose end pieces are cut to fit, the cuts hidden in the caps.
        {
            "id": 109, "name": "cut_ends",
            "components": {
                "path": {"nodes": [{"position": [-44.0, 1.0, 0.0], "mode": "corner"}, {"position": [-33.5, 1.0, 0.0], "mode": "corner"}]},
                "path_object": {"path_object": "tbp:test_fence", "overrides_fit": True, "fit": "resize_ends",
                    "overrides_resize_method": True, "resize_method": "cut"},
            },
        },
        # The fence with a heavier post at its corners, placed here alone: the asset has no corner slot. A skipped section lies
        # in it, with caps on both of its sides.
        {
            "id": 110, "name": "corner_fence",
            "components": {
                "path": {"nodes": [
                    {"position": [8.0, 1.0, -22.0], "mode": "corner"},
                    {"position": [18.0, 1.0, -22.0], "mode": "corner"},
                    {"position": [18.0, 1.0, -29.0], "mode": "corner", "skips_section": True},
                    {"position": [18.0, 1.0, -32.0], "mode": "corner"},
                    {"position": [10.0, 1.0, -36.0], "mode": "corner"},
                ]},
                "path_object": {"path_object": "tbp:test_fence", "overrides_has_corner": True, "has_corner": True,
                    "overrides_models": True, "models": [{"slot": "corner", "world_model": "tbp:test_corner_post"}],
                    "slots": [{"slot": "corner", "overrides_length": True, "length": 0.0, "overrides_collision": True, "collision": "model"}]},
            },
        },
    ]


def dump(value, indent):
    """JSON as the game writes its worlds: lists of numbers on one line, everything else on its own lines."""
    pad = " " * indent
    inner = " " * (indent + 4)
    if isinstance(value, dict):
        rows = ",\n".join('%s"%s": %s' % (inner, key, dump(row, indent + 4)) for key, row in value.items())
        return "{\n%s\n%s}" % (rows, pad)
    if isinstance(value, list) and all(isinstance(row, (int, float)) for row in value):
        return "[%s]" % ", ".join(json.dumps(row) for row in value)
    if isinstance(value, list):
        rows = ",\n".join(inner + dump(row, indent + 4) for row in value)
        return "[\n%s\n%s]" % (rows, pad)
    return json.dumps(value)


def write_world():
    """The objects into the world file; a save from the game (F5) gives it the writer's exact form."""
    with open(WORLD_FILE) as source:
        world = json.load(source)
    kept = [row for row in world["objects"] if not (FIRST_OBJECT_ID <= row["id"] <= LAST_OBJECT_ID)]
    world["objects"] = kept + world_objects()
    world["next_object_id"] = max(world["next_object_id"], LAST_OBJECT_ID + 1)
    builder.write_text(WORLD_FILE, dump(world, 0) + "\n")


def main():
    write_model("post", post())
    write_model("corner_post", corner_post())
    write_model("panel", panel(0))
    write_model("panel_lod1", panel(1))
    write_model("hedge", hedge(0))
    write_model("hedge_lod1", hedge(1))
    write_model("rail", rail())
    write_model("lamp", lamp())
    write_world_model("post", [("post", 60)], 150, "bounds")
    write_world_model("corner_post", [("corner_post", 60)], 150, "bounds")
    write_world_model("panel", [("panel", 30), ("panel_lod1", 90)], 150)
    write_world_model("hedge", [("hedge", 40), ("hedge_lod1", 120)], 200)
    write_world_model("rail", [("rail", 60)], 120)
    write_world_model("lamp", [("lamp", 100)], 300, "bounds")
    write_path_object("test_fence", {
        "models": [
            {"slot": "middle", "world_model": "tbp:test_panel"},
            {"slot": "separator", "world_model": "tbp:test_post"},
            {"slot": "start_cap", "world_model": "tbp:test_post"},
            {"slot": "end_cap", "world_model": "tbp:test_post"},
        ],
        "has_separator": True,
        "has_start_cap": True,
        "has_end_cap": True,
        # The posts stand on the panels' ends: panels run post centre to post centre, the posts take no length.
        "slots": [
            {"slot": "separator", "length": 0.0, "collision": "model"},
            {"slot": "start_cap", "length": 0.0, "collision": "model"},
            {"slot": "end_cap", "length": 0.0, "collision": "model"},
            {"slot": "middle", "collision": "none"},
        ],
    })
    write_path_object("test_hedge", {
        "models": [{"slot": "middle", "world_model": "tbp:test_hedge"}],
        "seed": 3,
        "slots": [{"slot": "middle", "curve": "bent", "slice_spacing": 0.5, "collision": "mesh", "flip": "random"}],
    })
    write_path_object("test_railing", {
        "models": [
            {"slot": "middle", "world_model": "tbp:test_rail"},
            {"slot": "separator", "world_model": "tbp:test_post"},
            {"slot": "start_cap", "world_model": "tbp:test_post"},
            {"slot": "end_cap", "world_model": "tbp:test_post"},
        ],
        "has_separator": True,
        "has_start_cap": True,
        "has_end_cap": True,
        "separator_every": 3,
        "slots": [
            {"slot": "middle", "curve": "bent", "slice_spacing": 0.25, "collision": "boxes", "box_spacing": 1.0},
            {"slot": "separator", "length": 0.0},
            {"slot": "start_cap", "length": 0.0},
            {"slot": "end_cap", "length": 0.0},
        ],
    })
    write_path_object("test_lamp_row", {
        "models": [{"slot": "middle", "world_model": "tbp:test_lamp"}],
        "fit": "fixed",
        "align": "between",
        "uses_gap": True,
        "gap": 12.0,
        "ground_offset": -0.05,
        "seed": 7,
        "slots": [{"slot": "middle", "length": 0.0, "slope": "step", "side": "both", "side_offset": 2.4,
            "yaw_jitter": 0.03, "collision": "model"}],
    })
    write_world()


if __name__ == "__main__":
    main()
