#!/usr/bin/env python3
"""Generates the placeholder content for prefabs: a small kit of building pieces, a bus stop, a street lamp and houses built from the
kit, as binary glTF (.glb) and JSON, and the objects of the test world that use them.

Run it from anywhere (only the Python standard library is needed):

    python3 asset_source/scripts/placeholder_prefabs.py

It writes, relative to the repository root:

    compile/out/asset/models/placeholder_prefabs/*.glb, *.json   the models
    compile/out/asset/world_models/test_world/test_<model>.json  their world models
    compile/out/asset/prefabs/test_street_lamp.json              a lamp with its light: an enum for how it is, a colour
    compile/out/asset/prefabs/test_bus_stop.json                 a shelter, a bench, a bin, a sign and a nested lamp
    compile/out/asset/prefabs/test_wall_bay.json                 one 2 m bay of a wall: an enum of wall, window and door
    compile/out/asset/prefabs/test_house.json                    a two-storey house of nested bays and kit pieces
    compile/out/asset/path_objects/test_prefab_lamp_row.json     street lamps along a path, each one's state from the seed
    compile/out/worlds/test.world.json                           objects 111 to 118 (replaced when they are there already); save
                                                                 it from the game (F5) and copy the save over it for the writer's
                                                                 exact form

The kit follows the rules of references/asset_structure.md, "Building kits": one grid of 1 m and a storey of 3 m, a piece's pivot on its
floor corner, the wall's outer face on the grid line, walls running along +X with their inside toward +Z. Colours are the materials'
baseColorFactor; the walls and roofs are pale, so a tint shows.

Placeholder art for checking the code, not game art: replace it with the game's kit.
"""

import json
import os
import sys

sys.dont_write_bytecode = True  # importing the other scripts must leave no __pycache__ in the repository
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import placeholder_instances as builder  # noqa: E402 (its mesh builder and glTF writer)
import placeholder_path_objects as pieces  # noqa: E402 (its box, its world model writer and its JSON layout)
import placeholder_terrain as terrain  # noqa: E402 (the height of its ground)


# Paths.
ASSET = builder.ASSET
MODEL_DIRECTORY = os.path.join(ASSET, "models", "placeholder_prefabs")
PREFAB_DIRECTORY = os.path.join(ASSET, "prefabs")
PATH_OBJECT_DIRECTORY = os.path.join(ASSET, "path_objects")
WORLD_FILE = builder.WORLD_FILE

# The objects this script owns in the test world.
FIRST_OBJECT_ID = 111
LAST_OBJECT_ID = 118

# The world file format the prefabs' objects are in.
FORMAT_VERSION = 3

# The kit's grid: a storey's height, a wall's thickness.
STOREY = 3.0
WALL = 0.2

PLASTER = ("plaster", (0.86, 0.84, 0.78))
GLASS = ("glass", (0.18, 0.26, 0.30))
FRAME = ("frame", (0.30, 0.22, 0.16))
DOOR = ("door", (0.40, 0.18, 0.12))
SLAB = ("slab", (0.55, 0.54, 0.52))
ROOF = ("roof", (0.82, 0.80, 0.78))
STEEL = ("steel", (0.32, 0.33, 0.36))
BENCH = ("bench", (0.46, 0.33, 0.21))
BIN = ("bin", (0.20, 0.32, 0.22))
SIGN = ("sign", (0.80, 0.70, 0.20))
PANE = ("pane", (0.55, 0.65, 0.68))
BRICK = ("brick", (0.45, 0.22, 0.18))

box = pieces.box


# The models.
def wall(length):
    mesh = builder.MeshBuilder([PLASTER])
    box(mesh, "plaster", (0.0, 0.0, 0.0), (length, STOREY, WALL))
    return mesh


def wall_window():
    """A 2 m wall with a window: a sill, a lintel, two jambs, the glass set back."""
    mesh = builder.MeshBuilder([PLASTER, FRAME, GLASS])
    box(mesh, "plaster", (0.0, 0.0, 0.0), (2.0, 0.9, WALL))
    box(mesh, "plaster", (0.0, 2.2, 0.0), (2.0, STOREY, WALL))
    box(mesh, "plaster", (0.0, 0.9, 0.0), (0.4, 2.2, WALL))
    box(mesh, "plaster", (1.6, 0.9, 0.0), (2.0, 2.2, WALL))
    box(mesh, "frame", (0.4, 0.9, 0.02), (1.6, 1.0, WALL - 0.02))
    box(mesh, "frame", (0.4, 2.1, 0.02), (1.6, 2.2, WALL - 0.02))
    box(mesh, "frame", (0.95, 1.0, 0.04), (1.05, 2.1, WALL - 0.04))
    box(mesh, "glass", (0.4, 1.0, 0.09), (1.6, 2.1, 0.11))
    return mesh


def wall_door():
    """A 2 m wall with a door."""
    mesh = builder.MeshBuilder([PLASTER, FRAME, DOOR])
    box(mesh, "plaster", (0.0, 2.2, 0.0), (2.0, STOREY, WALL))
    box(mesh, "plaster", (0.0, 0.0, 0.0), (0.5, 2.2, WALL))
    box(mesh, "plaster", (1.5, 0.0, 0.0), (2.0, 2.2, WALL))
    box(mesh, "frame", (0.5, 0.0, 0.02), (0.6, 2.2, WALL - 0.02))
    box(mesh, "frame", (1.4, 0.0, 0.02), (1.5, 2.2, WALL - 0.02))
    box(mesh, "door", (0.6, 0.0, 0.07), (1.4, 2.15, 0.13))
    return mesh


def corner():
    """What fills a corner: a post a wall's thickness on a side."""
    mesh = builder.MeshBuilder([PLASTER])
    box(mesh, "plaster", (-WALL, 0.0, -WALL), (0.0, STOREY, 0.0))
    return mesh


def floor_slab():
    """A 4 m floor, its top on the storey's floor. A hair short of the grid, so its edge never shows through a wall's outer face."""
    mesh = builder.MeshBuilder([SLAB])
    box(mesh, "slab", (0.02, -0.2, 0.02), (3.98, 0.0, 3.98))
    return mesh


def roof_slab():
    """A 4 m flat roof, its top a little above the wall's."""
    mesh = builder.MeshBuilder([ROOF])
    box(mesh, "roof", (0.0, 0.0, 0.0), (4.0, 0.25, 4.0))
    return mesh


def chimney():
    mesh = builder.MeshBuilder([BRICK])
    box(mesh, "brick", (-0.3, 0.0, -0.3), (0.3, 1.0, 0.3))
    return mesh


def shelter_back():
    mesh = builder.MeshBuilder([STEEL, PANE])
    box(mesh, "steel", (-2.0, 0.0, -0.05), (-1.9, 2.4, 0.05))
    box(mesh, "steel", (1.9, 0.0, -0.05), (2.0, 2.4, 0.05))
    box(mesh, "pane", (-1.9, 0.3, -0.02), (1.9, 2.3, 0.02))
    return mesh


def shelter_side():
    mesh = builder.MeshBuilder([STEEL, PANE])
    box(mesh, "steel", (-0.05, 0.0, 1.1), (0.05, 2.4, 1.2))
    box(mesh, "pane", (-0.02, 0.3, 0.05), (0.02, 2.3, 1.1))
    return mesh


def shelter_roof():
    mesh = builder.MeshBuilder([ROOF])
    box(mesh, "roof", (-2.2, 0.0, -0.3), (2.2, 0.12, 1.5))
    return mesh


def bench():
    mesh = builder.MeshBuilder([BENCH, STEEL])
    box(mesh, "bench", (-0.9, 0.42, -0.2), (0.9, 0.48, 0.2))
    box(mesh, "bench", (-0.9, 0.5, -0.24), (0.9, 0.85, -0.2))
    box(mesh, "steel", (-0.8, 0.0, -0.18), (-0.72, 0.42, 0.18))
    box(mesh, "steel", (0.72, 0.0, -0.18), (0.8, 0.42, 0.18))
    return mesh


def trash_bin():
    mesh = builder.MeshBuilder([BIN])
    box(mesh, "bin", (-0.25, 0.0, -0.25), (0.25, 0.9, 0.25))
    return mesh


def sign():
    mesh = builder.MeshBuilder([STEEL, SIGN])
    box(mesh, "steel", (-0.04, 0.0, -0.04), (0.04, 2.6, 0.04))
    box(mesh, "sign", (-0.3, 2.6, -0.03), (0.3, 3.1, 0.03))
    return mesh


def street_lamp():
    """A pole at its origin and an arm toward its -Z, its head hanging there."""
    mesh = builder.MeshBuilder([STEEL, PANE])
    box(mesh, "steel", (-0.07, 0.0, -0.07), (0.07, 5.0, 0.07))
    box(mesh, "steel", (-0.04, 4.9, -1.2), (0.04, 5.0, 0.0))
    box(mesh, "pane", (-0.14, 4.82, -1.4), (0.14, 4.92, -1.0))
    return mesh


def write_model(name, mesh):
    print(name)
    builder.write_glb(os.path.join(MODEL_DIRECTORY, name + ".glb"), mesh)
    builder.write_text(os.path.join(MODEL_DIRECTORY, name + ".json"),
        '{\n    "name": "tbp:test_%s",\n    "location": "placeholder_prefabs/%s"\n}\n' % (name, name))


# The prefabs.
def write_prefab(name, properties, objects):
    definition = {"name": "tbp:" + name, "format_version": FORMAT_VERSION}
    if properties:
        definition["properties"] = properties
    definition["objects"] = objects
    builder.write_text(os.path.join(PREFAB_DIRECTORY, name + ".json"), json.dumps(definition, indent=4) + "\n")


def modelled(world_model, collides=False, **others):
    """The components of an object showing a world model, colliding as its bounds if it says so."""
    components = {"model": {"world_model": "tbp:test_" + world_model}}
    if collides:
        components["collider"] = {}
    components.update(others)
    return components


def street_lamp_prefab():
    write_prefab("test_street_lamp", {
        "state": {
            "type": "enum", "default": "on", "display_name": "State", "category": "Lamp",
            "description": "Whether the lamp burns, and how.",
            "options": {
                "on": [],
                "off": [{"object": "light", "component": "light", "row": "is_on", "value": False}],
                "broken": [
                    {"object": "light", "component": "light", "row": "flicker", "value": "broken"},
                    {"object": "light", "component": "light", "row": "flicker_amount", "value": 1.0},
                ],
            },
            "option_display_names": {"on": "Burning", "off": "Out", "broken": "Flickering"},
        },
        "light_color": {
            "type": "color", "default": "#ffd9a0", "display_name": "Light colour", "category": "Lamp",
            "targets": [{"object": "light", "component": "light", "row": "color"}],
        },
    }, [
        {"id": 1, "name": "pole", "components": modelled("street_lamp", True)},
        {"id": 2, "parent": 1, "name": "light", "position": [0.0, 4.8, -1.2], "rotation": [90.0, 0.0, 0.0], "components": {
            "light": {"type": "spot", "intensity": 3.0, "range": 12.0, "inner_angle": 35.0, "outer_angle": 60.0}}},
    ])


def bus_stop_prefab():
    write_prefab("test_bus_stop", {
        "roof_color": {
            "type": "color", "default": "#7a3b2e", "display_name": "Roof colour", "category": "Looks",
            "description": "The colour of the shelter's roof.", "targets": [{"tag": "tbp:stop_roof", "row": "tint"}],
        },
        "layout": {
            "type": "enum", "default": "open", "display_name": "Layout", "category": "Layout",
            "description": "Which sides the shelter has.",
            "options": {
                "open": [{"object": "left_side", "row": "is_enabled", "value": False},
                    {"object": "right_side", "row": "is_enabled", "value": False}],
                "windy": [{"object": "right_side", "row": "is_enabled", "value": False}],
                "closed": [],
            },
        },
        "bench_angle": {
            "type": "angle", "default": 0.0, "minimum": -45.0, "maximum": 45.0, "display_name": "Bench angle", "category": "Layout",
            "targets": [{"object": "bench", "row": "rotation", "axis": "y"}],
        },
        "lamp_state": {
            "type": "enum", "default": "lit", "display_name": "Lamp", "category": "Lamp",
            "options": {
                "lit": [{"object": "lamp", "property": "state", "value": "on"}],
                "dark": [{"object": "lamp", "property": "state", "value": "off"}],
                "faulty": [{"object": "lamp", "property": "state", "value": "broken"}],
            },
        },
        "lamp_color": {
            "type": "color", "default": "#ffd9a0", "display_name": "Lamp colour", "category": "Lamp",
            "targets": [{"object": "lamp", "property": "light_color"}],
        },
        "sign_model": {
            "type": "asset", "default": "tbp:test_stop_sign", "asset_type": "world_model", "display_name": "Sign", "category": "Looks",
            "targets": [{"object": "sign", "component": "model", "row": "world_model"}],
        },
    }, [
        {"id": 1, "name": "root"},
        {"id": 2, "parent": 1, "name": "back", "components": modelled("shelter_back", True)},
        {"id": 3, "parent": 1, "name": "left_side", "position": [-2.0, 0.0, 0.0], "components": modelled("shelter_side", True)},
        {"id": 4, "parent": 1, "name": "right_side", "position": [2.0, 0.0, 0.0], "components": modelled("shelter_side", True)},
        {"id": 5, "parent": 1, "name": "roof", "position": [0.0, 2.4, 0.0], "tags": ["tbp:stop_roof"],
            "components": modelled("shelter_roof", True)},
        {"id": 6, "parent": 1, "name": "bench", "position": [0.0, 0.0, 0.55], "components": modelled("stop_bench", True)},
        {"id": 7, "parent": 1, "name": "bin", "position": [-2.6, 0.0, 0.3], "components": modelled("stop_bin", True)},
        {"id": 8, "parent": 1, "name": "sign", "position": [2.8, 0.0, 0.8], "components": modelled("stop_sign", True)},
        {"id": 9, "parent": 1, "name": "lamp", "position": [-3.4, 0.0, 1.8], "components": {"prefab": {"prefab": "tbp:test_street_lamp"}}},
    ])


def wall_bay_prefab():
    write_prefab("test_wall_bay", {
        "kind": {
            "type": "enum", "default": "wall", "display_name": "Kind", "category": "Bay",
            "options": {
                "wall": [{"object": "with_window", "row": "is_enabled", "value": False},
                    {"object": "with_door", "row": "is_enabled", "value": False}],
                "window": [{"object": "plain", "row": "is_enabled", "value": False},
                    {"object": "with_door", "row": "is_enabled", "value": False}],
                "door": [{"object": "plain", "row": "is_enabled", "value": False},
                    {"object": "with_window", "row": "is_enabled", "value": False}],
            },
        },
        "wall_color": {
            "type": "color", "default": "#ffffff", "display_name": "Wall colour", "category": "Looks",
            "targets": [{"tag": "tbp:bay_wall", "row": "tint"}],
        },
    }, [
        {"id": 1, "name": "root"},
        {"id": 2, "parent": 1, "name": "plain", "tags": ["tbp:bay_wall"], "components": modelled("kit_wall_2", True)},
        {"id": 3, "parent": 1, "name": "with_window", "tags": ["tbp:bay_wall"], "components": modelled("kit_window_2", True)},
        {"id": 4, "parent": 1, "name": "with_door", "tags": ["tbp:bay_wall"], "components": modelled("kit_door_2", True)},
    ])


def house_prefab():
    """Twelve by eight metres, two storeys: walls of nested bays, kit floors and a roof. Sides run with their inside to their +Z, by
    the convention: side 0 along +X at z = 0, then round."""
    width = 12
    depth = 8
    objects = [{"id": 1, "name": "root"}]
    next_id = [2]

    def add(row):
        row["id"] = next_id[0]
        next_id[0] += 1
        objects.append(row)
        return row["id"]

    def bay(storey, side, index, x, z, turn, kind):
        add({"parent": 1, "name": "bay_%d_%d_%d" % (storey, side, index), "position": [float(x), storey * STOREY, float(z)],
            "rotation": [0.0, float(turn), 0.0], "tags": ["tbp:bay"],
            "components": {"prefab": {"prefab": "tbp:test_wall_bay", "values": {"kind": kind}}}})

    for storey in range(2):
        for index in range(width // 2):
            is_door = (storey == 0) and (index == 2)
            bay(storey, 0, index, 2 * index, 0, 0.0, "door" if is_door else ("window" if (index % 2) == 1 else "wall"))
            bay(storey, 2, index, width - (2 * index), depth, 180.0, "window" if (index % 2) == 0 else "wall")
        for index in range(depth // 2):
            bay(storey, 1, index, width, 2 * index, -90.0, "window" if (index % 2) == 0 else "wall")
            bay(storey, 3, index, 0, depth - (2 * index), 90.0, "window" if (index % 2) == 1 else "wall")
        for corner_index, (x, z, turn) in enumerate([(0, 0, 0.0), (width, 0, -90.0), (width, depth, 180.0), (0, depth, 90.0)]):
            add({"parent": 1, "name": "corner_%d_%d" % (storey, corner_index), "position": [float(x), storey * STOREY, float(z)],
                "rotation": [0.0, turn, 0.0], "tags": ["tbp:bay_corner"], "components": modelled("kit_corner", True)})
        for x in range(0, width, 4):
            for z in range(0, depth, 4):
                add({"parent": 1, "name": "floor_%d_%d_%d" % (storey, x, z), "position": [float(x), storey * STOREY, float(z)],
                    "components": modelled("kit_floor_4", True)})
    for x in range(0, width, 4):
        for z in range(0, depth, 4):
            add({"parent": 1, "name": "roof_%d_%d" % (x, z), "position": [float(x), 2.0 * STOREY, float(z)], "tags": ["tbp:house_roof"],
                "components": modelled("kit_roof_4", True)})
    add({"parent": 1, "name": "chimney", "position": [9.0, (2.0 * STOREY) + 0.25, 2.0], "components": modelled("kit_chimney", True)})
    add({"parent": 1, "name": "porch_light", "position": [5.0, 2.6, -0.5], "components": {
        "light": {"type": "point", "intensity": 1.5, "range": 6.0, "color": "#ffcf90"}}})
    write_prefab("test_house", {
        "wall_color": {
            "type": "color", "default": "#f0e6d2", "display_name": "Walls", "category": "Looks",
            "description": "The plaster's colour, on every bay.",
            "targets": [{"tag": "tbp:bay", "property": "wall_color"}, {"tag": "tbp:bay_corner", "row": "tint"}],
        },
        "roof_color": {
            "type": "color", "default": "#5a4a48", "display_name": "Roof", "category": "Looks",
            "targets": [{"tag": "tbp:house_roof", "row": "tint"}],
        },
        "has_chimney": {
            "type": "bool", "default": True, "display_name": "Chimney", "category": "Parts",
            "targets": [{"object": "chimney", "row": "is_enabled"}],
        },
        "chimney_height": {
            "type": "float", "default": 1.0, "minimum": 0.5, "maximum": 3.0, "display_name": "Chimney height", "category": "Parts",
            "targets": [{"object": "chimney", "row": "scale", "axis": "y"}],
        },
        "is_porch_lit": {
            "type": "bool", "default": True, "display_name": "Porch light", "category": "Parts",
            "targets": [{"object": "porch_light", "component": "light", "row": "is_on"}],
        },
        "front": {
            "type": "enum", "default": "door", "display_name": "Front", "category": "Parts",
            "options": {
                "door": [],
                "shop": [
                    {"object": "bay_0_0_1", "property": "kind", "value": "door"},
                    {"object": "bay_0_0_3", "property": "kind", "value": "door"},
                ],
            },
        },
    }, objects)


# The objects of the test world.
def place(x, z, lift=0.0):
    """A place on the ground of the test terrain."""
    return [float(x), round(terrain.ground_height(x, z) + lift, 3), float(z)]


def world_objects():
    # A street on the flat west of the slab, running along Z: lamps on its west side, bus stops and houses on its east. A bus stop's
    # open side and a house's front look west, to the road.
    stop_turn = [0.0, -90.0, 0.0]
    house_turn = [0.0, 90.0, 0.0]
    return [
        # Three copies of a bus stop.
        {"id": 111, "name": "bus_stop_plain", "position": place(-50.5, 18.0), "rotation": stop_turn,
            "components": {"prefab": {"prefab": "tbp:test_bus_stop"}}},
        # Another colour and layout, by its values; its bench moved and its bin gone, by its overrides. The object after it is an
        # ordinary child it adds of its own.
        {"id": 112, "name": "bus_stop_changed", "position": place(-50.5, -2.0), "rotation": stop_turn,
            "components": {"prefab": {"prefab": "tbp:test_bus_stop",
                "values": {"lamp_color": "#a0c8ff", "layout": "closed", "roof_color": "#2e4a7a"},
                "object_overrides": {"bench": {"position": [0.6, 0.0, 0.7], "rotation": [0.0, 20.0, 0.0]}, "bin": {"is_removed": True},
                    "lamp/light": {"components": {"light": {"intensity": 6.0}}}}}}},
        {"id": 113, "name": "bus_stop_added_crate", "parent": 112, "position": [-2.6, 0.25, 0.3],
            "components": {"model": {"world_model": "tbp:test"}}},
        {"id": 114, "name": "bus_stop_faulty", "position": place(-50.5, -22.0), "rotation": stop_turn,
            "components": {"prefab": {"prefab": "tbp:test_bus_stop", "values": {"bench_angle": 30.0, "lamp_state": "faulty", "layout": "windy"}}}},
        # Three houses of one prefab.
        {"id": 115, "name": "house_plain", "position": place(-49.0, 14.0), "rotation": house_turn,
            "components": {"prefab": {"prefab": "tbp:test_house"}}},
        {"id": 116, "name": "house_shop", "position": place(-49.0, -8.0), "rotation": house_turn,
            "components": {"prefab": {"prefab": "tbp:test_house",
                "values": {"chimney_height": 2.5, "front": "shop", "is_porch_lit": False, "roof_color": "#3a5a48", "wall_color": "#b8c8e0"}}}},
        {"id": 117, "name": "house_no_chimney", "position": place(-49.0, -24.0), "rotation": house_turn,
            "components": {"prefab": {"prefab": "tbp:test_house", "values": {"has_chimney": False, "wall_color": "#d8b8a0"}}}},
        # Street lamps along the road: a path object whose pieces are copies of a prefab, each one's state from the seed.
        {"id": 118, "name": "street_lamps", "components": {
            "path": {"nodes": [{"position": [-55.0, -1.0, -36.0], "mode": "corner"}, {"position": [-55.0, -1.0, 26.0], "mode": "corner"}]},
            "path_object": {"path_object": "tbp:test_prefab_lamp_row"}}},
    ]


def write_world():
    with open(WORLD_FILE) as source:
        world = json.load(source)
    kept = [row for row in world["objects"] if not (FIRST_OBJECT_ID <= row["id"] <= LAST_OBJECT_ID)]
    world["objects"] = kept + world_objects()
    world["next_object_id"] = max(world["next_object_id"], LAST_OBJECT_ID + 1)
    builder.write_text(WORLD_FILE, pieces.dump(world, 0) + "\n")


def main():
    write_model("kit_wall_1", wall(1.0))
    write_model("kit_wall_2", wall(2.0))
    write_model("kit_wall_4", wall(4.0))
    write_model("kit_window_2", wall_window())
    write_model("kit_door_2", wall_door())
    write_model("kit_corner", corner())
    write_model("kit_floor_4", floor_slab())
    write_model("kit_roof_4", roof_slab())
    write_model("kit_chimney", chimney())
    write_model("shelter_back", shelter_back())
    write_model("shelter_side", shelter_side())
    write_model("shelter_roof", shelter_roof())
    write_model("stop_bench", bench())
    write_model("stop_bin", trash_bin())
    write_model("stop_sign", sign())
    write_model("street_lamp", street_lamp())
    for name in ("kit_wall_1", "kit_wall_2", "kit_wall_4", "kit_window_2", "kit_door_2", "kit_corner", "kit_floor_4", "kit_roof_4",
            "kit_chimney", "shelter_back", "shelter_side", "shelter_roof", "stop_bench", "stop_bin", "stop_sign", "street_lamp"):
        pieces.write_world_model(name, [(name, 400)], 400, "bounds")
    street_lamp_prefab()
    bus_stop_prefab()
    wall_bay_prefab()
    house_prefab()
    builder.write_text(os.path.join(PATH_OBJECT_DIRECTORY, "test_prefab_lamp_row.json"), json.dumps({
        "name": "tbp:test_prefab_lamp_row",
        "models": [{"slot": "middle", "prefab": "tbp:test_street_lamp", "randomizes_enums": True}],
        "fit": "fixed",
        "align": "between",
        "uses_gap": True,
        "gap": 11.0,
        "seed": 5,
        "slots": [{"slot": "middle", "length": 0.0, "side": "right", "side_offset": 1.5, "collision": "none"}],
    }, indent=4) + "\n")
    write_world()


if __name__ == "__main__":
    main()
