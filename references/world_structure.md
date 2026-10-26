# World file structure

A world file holds one 3D world: its name, its environment (sky, sun, moon, haze, effects) and its objects with their
tags, data and components. It has two forms with the same content:

- **JSON** (`<name>.world.json`): authored worlds. Readable, editable by hand, and written so that git diffs stay
  small.
- **GHDF** (`<name>.world.ghdf`, `references/ghdf.txt`): saves, packed stories and worlds sent over the network.
  Compact: the test world is 9913 bytes as JSON and 1931 bytes as GHDF.

`WorldFile` reads and writes both (`agents/WorldFile.md`). Until story packs exist, worlds live in
`<base>/worlds/` next to the executable; the test frame loads `worlds/test.world.json`.

Every block comes from a property table (`agents/PropertyDescriptors.md`): the environment's, the object's and each
component type's. A key is a table row's key, a GHDF entry id is its wire id, and every value follows the row's type
and rules. The tables and their defaults are listed in `agents/WorldData.md` ("Property tables", "Environment").

## JSON

```json
{
    "format_version": 1,
    "name": "test",
    "next_object_id": 22,
    "environment": {
        "time_of_day": 0.25,
        "light_pollution": 0.7
    },
    "objects": [
        {
            "id": 4,
            "name": "warm_light",
            "position": [2.5, 1.6, 2.5],
            "components": {
                "light": {
                    "color": "#ff963c",
                    "intensity": 6.0,
                    "range": 10.0,
                    "casts_shadows": true
                }
            }
        },
        {
            "id": 15,
            "name": "desk",
            "position": [1.8, 0.05, -3.6],
            "rotation": [0.0, 15.0, 0.0],
            "tags": ["tbp:office", "town:furniture"],
            "data": {
                "town:drawer_count": 3
            }
        },
        {
            "id": 19,
            "name": "cup",
            "parent": 15,
            "position": [0.45, 0.84, 0.15],
            "components": {
                "model": {
                    "world_model": "pbr_metal",
                    "overrides_shadow_tier": true,
                    "shadow_tier": "near"
                },
                "collider": {
                    "uses_own_shape": true,
                    "shape_type": "cylinder",
                    "shape_radius": 0.25,
                    "shape_half_height": 0.25
                }
            }
        }
    ]
}
```

### The root

| Key | Value | |
|---|---|---|
| `format_version` | integer, required | The format the file is written in: 1. A newer version is refused ("newer than this game reads"). |
| `name` | string or `null` | The world's name: at most 256 bytes of UTF-8. |
| `next_object_id` | integer | The id the world hands to its next new object, so ids of removed objects are not reused; 18446744073709551615 once every id was handed out (no id is that large). When it is not above every object id it is raised to one that is, with a warning; without it, one above the largest id. |
| `environment` | object | The environment's settings by key (the `world_environment` table; `agents/WorldData.md`, "Environment", lists them with their rules). Absent keys keep their defaults. Since T15 the air's settings are among them: `haze_height`, `haze_albedo`, `sun_glow`, `air_density`, `ozone` and `multiple_scattering` (with `sky_turbidity` and `sky_ground_albedo`), e.g. `{"haze_height": 300, "sun_glow": 0.9, "ozone": 0}`. Since T35 the haze's settings: `aerial_distance_scale` (every haze distance that many times longer; 5 for a bigger, hazier town), `haze_start_distance`, the ground haze's `ground_haze_density` (0.02 a morning mist), `ground_haze_base`, `ground_haze_thickness` (15 for that mist), `ground_haze_albedo` and `ground_haze_glow`, and `horizon_ground_height` (the far ground's height); `fog_strength` multiplies the haze (1 the real air, 0 clear air) and `fog_color` is no longer used, e.g. `{"ground_haze_density": 0.02, "ground_haze_thickness": 15}`. Since T36 one light scale lights the sky and the surfaces: `sun_color` is the sun's colour above the air (default white: the air reddens it), `moon_intensity` the full moon's light above the air (default 0.04), `ambient_skylight_color` and `ambient_skylight_intensity` tint and scale the sky's own light on surfaces (white and 1: the sky's own), and `night_light_color` and `night_light_intensity` set the moonless night's own light, e.g. `{"night_light_intensity": 0}` for a pitch-black moonless night. |
| `objects` | array of objects | The objects. |

### An object

| Key | Value | |
|---|---|---|
| `id` | integer, required | Unique in the world, from 1 to 18446744073709551614. Integers above 9223372036854775807 are written as strings. |
| `name` | string or `null` | At most 256 bytes; need not be unique. |
| `parent` | integer | The id of the object this one is placed on; 0 or absent for none. |
| `parent_socket` | string or `null` | A socket of the parent's world model this object hangs on (T30: a cup in a hand, `[a-z][a-z0-9_.-]*`, at most 64 bytes): its position, rotation and scale are then relative to the socket's frame in the parent's current pose. A socket the parent's pose lacks is the parent's own space. Absent: the parent itself. |
| `position`, `rotation`, `scale` | `[x, y, z]` | Relative to the parent. The position is in metres, read and written as doubles (every digit is kept), each axis within ±10 000 000 (10 000 km); the rotation is in degrees, applied X, then Y, then Z. |
| `tint` | colour | `"#rrggbb"`, or `{"tint": [r, g, b], "brightness": b, "opacity": o}` when brightness or opacity is not 1. |
| `is_enabled`, `is_visible` | `true` / `false` | |
| `mobility` | `"static"` / `"movable"` | |
| `tags` | array of keys | See "Tags and data". |
| `data` | object | See "Tags and data". |
| `components` | object | One block per component, keyed by the type's name: `model`, `sprite`, `light`, `collider`, `character`, `animator`, `terrain`, `model_instances`. Each block holds that type's keys (`model_component`, `sprite_component`, `light_component`, `collider_component`, `character_component`, `animator_component`, `terrain_component`, `model_instances_component` in `agents/WorldData.md`, `agents/Characters.md`, `agents/Animation.md` and `agents/Terrain.md`). |

An object has at most one component of each type. A block whose values are all defaults is still written (`"light":
{}`): the component is there.

### The model block

The `model` component names a world model (`world_model`: a `world_models/` asset, `references/asset_structure.md`,
"World models"), which says which meshes to draw, how far they are seen and how they are drawn by default. A placement
picks a variant with `uses_variant` (true) and `variant` (its name), and overrides each default setting with two keys:
an override flag, `overrides_` and the setting's key (false by default: the world model's value applies), and its own
value next to it. The value is kept, and saved, while its flag is off, so switching the flag back on restores it. There
are no "inherit" values.

| Setting | Flag | Value |
|---|---|---|
| The outline | `overrides_has_outline` | `has_outline` (default true) |
| Full resolution over the pixelated image | `overrides_omit_pixelation` | `omit_pixelation` (default false) |
| The shadow cascades | `overrides_shadow_tier` | `shadow_tier`: `both` (default), `near`, `far` |
| Casting shadows | `overrides_casts_shadows` | `casts_shadows` (default true) |
| The haze in front of it (T35) | `overrides_haze` | `haze`: in [0, 1000], times the air's haze (default 1; 0 as if the air were clear: a sign that must read from far away) |
| The draw distance | `overrides_draw_distance` | `draw_distance`: metres, above 0 (default 600) |
| Keeping it fixed against the quality setting | `overrides_is_draw_distance_fixed` | `is_draw_distance_fixed` (default false) |
| The LOD switch distances | `overrides_lod_distances` | `lod_distances`: metres, finest LOD first, each above 0 and above the one before, at most 8 (default `[]`) |

`draw_distance_multiplier` (above 0, default 1) multiplies the draw distance in effect, the world model's or the
placement's own: 2 is seen twice as far. It is a plain setting, not an override. The LODs (T11):
- `lod_distances` applies only while it holds one distance per LOD of the world model; when the world model gains or
  loses a LOD, the world model's distances apply instead (the game warns once) until the list fits again.
- `lod_distance_multiplier` (above 0, default 1) multiplies the switch distances in effect: 2 keeps each LOD twice as
  far (more detail).
- `is_lod_forced` (default false) with `forced_lod` (an integer >= 0, default 0) draws that LOD whatever the distance;
  past the world model's last LOD it draws the last. `forced_lod` is kept while `is_lod_forced` is off.

```
"model": {
    "world_model": "office_desk",
    "uses_variant": true,
    "variant": "dark_wood",
    "overrides_draw_distance": true,
    "draw_distance": 250.0,
    "draw_distance_multiplier": 1.5,
    "overrides_lod_distances": true,
    "lod_distances": [10.0, 30.0, 90.0]
}
```

### The model instances block

The `model_instances` component (B03) places many copies of one world model, drawn a cell at a time (a few draws for
thousands of copies; `agents/WorldRuntime.md`, "Model instances"). It takes every key of the model block, with the same
meaning and defaults, but `overrides_omit_pixelation` and `omit_pixelation` (copies are never drawn at full resolution
over the pixelated image), and applies them to every copy; and `instances`, the copies:

| Key | Value (default) | |
|---|---|---|
| `instances` | an array of instances (`[]`) | At most 262 144. Each is an object of up to four keys, any left out at its default: `position` (`[x, y, z]` metres from the object, in its own space; within 10 000 km on each axis), `rotation` (`[x, y, z]` degrees, Euler XYZ like an object's; 0), `scale` (one number, the same on every axis, or `[x, y, z]`; 1) and `tint` (a colour; its alpha is the copy's opacity, times the object's; white). Any other key is refused, naming the copy and the key (`[12]: "size" is not a key of an instance ...`). |

The object's transform places every copy, its tint and opacity multiply theirs, and hiding it hides them all. A copy's
LOD and fade are its own: it changes LOD inside a band around each switch distance and fades out at the draw distance
by itself. The writer leaves each copy's defaults out (the position is always written), writes a scale that is the same
on every axis as one number, and puts each copy on lines of its own. Two copies in the form of the test world's grove
(object 52), the second stretched upward and half transparent:
```
"model_instances": {
    "world_model": "test_tree",
    "instances": [
        {
            "position": [-2.259, 1.774, 142.541],
            "rotation": [0.0, 330.9, 0.0],
            "scale": 0.88,
            "tint": "#e7fbd8"
        },
        {
            "position": [4.215, 3.618, 44.995],
            "rotation": [0.0, 66.4, 0.0],
            "scale": [1.15, 1.38, 1.15],
            "tint": "#f1f6e880"
        }
    ]
}
```

### The sprite block

The `sprite` component names a world sprite (`world_sprite`: a `world_sprites/` asset, `references/asset_structure.md`,
"World sprites"), which says which sprite animation to play and how it stands, is lit and is seen. A placement may
override every one of its settings, each with the same two keys as the model block: an override flag, `overrides_` and
the setting's key (false by default: the world sprite's value applies), and its own value next to it, kept and saved
while its flag is off.

| Setting | Flag | Value (default) |
|---|---|---|
| How the quad turns | `overrides_orientation` | `orientation`: `billboard`, `axis_billboard`, `fixed` (`billboard`) |
| The point at the object's position | `overrides_pivot` | `pivot`: `[x, y]` (`[0.5, 0.5]`) |
| Image pixels per metre | `overrides_pixels_per_unit` | `pixels_per_unit`: above 0 (32) |
| Mirrored left to right | `overrides_flip_x` | `flip_x` (false) |
| Mirrored top to bottom | `overrides_flip_y` | `flip_y` (false) |
| Transparency | `overrides_alpha_mode` | `alpha_mode`: `cutout`, `blend`, `additive` (`cutout`) |
| The cutout threshold | `overrides_alpha_cutoff` | `alpha_cutoff`: in [0, 1] (0.5) |
| Lit, or glowing as it is | `overrides_is_lit` | `is_lit` (true) |
| How bright it glows unlit | `overrides_emission` | `emission`: >= 0 (1) |
| Light wrapping around a billboard | `overrides_light_wrap` | `light_wrap`: in [0, 1] (1) |
| Casting shadows | `overrides_casts_shadows` | `casts_shadows` (true) |
| A decal on a surface (with `fixed`) | `overrides_is_decal` | `is_decal` (false) |
| A decal's lift | `overrides_decal_offset` | `decal_offset`: metres, >= 0 (0.01) |
| The outline | `overrides_has_outline` | `has_outline` (true) |
| Full resolution over the pixelated image | `overrides_omit_pixelation` | `omit_pixelation` (false) |
| The haze in front of it (T35) | `overrides_haze` | `haze`: in [0, 1000] (1; 0 as if the air were clear) |
| The draw distance | `overrides_draw_distance` | `draw_distance`: metres, above 0 (150) |
| Keeping it fixed against the quality setting | `overrides_is_draw_distance_fixed` | `is_draw_distance_fixed` (false) |

`draw_distance_multiplier` (above 0, default 1) multiplies the draw distance in effect: the placement's own, the world
sprite's, or, when neither gives one, its kind's (150 m, or 60 m for a decal, decided on the settings in effect). It is
a plain setting, not an override.

```
"sprite": {
    "world_sprite": "test_tree",
    "overrides_flip_x": true,
    "flip_x": true
}
```

### The collider block

The `collider` component (T12) makes the object collide: with its world model's collision (the model block's world
model, `references/asset_structure.md`, "World models", `collision`) or with one shape of its own. Without it an object
does not collide, whatever its world model holds. Its layer, mask, trigger flag and mass come from the world model, each
overridable with the same two keys as the model block. Since T27 it also says how the object takes part in the
collision step: a movable object's collider is pushed (`is_pushable`) or kinematic (it pushes, and is never pushed);
a static object's collider never moves in play.

| Key | Value (default) | |
|---|---|---|
| `is_enabled` | `true` / `false` (true) | Off: not in the collision world. |
| `overrides_collision_set`, `collision_set` | false, a set name or `null` (null) | On: the world model's set of this name instead of its `default_set` (an unknown name: no collision, warned once). |
| `uses_own_shape` | false | On: the shape below instead of the world model's sets. |
| `shape_type` | `sphere`, `capsule`, `box`, `cylinder`, `plane` (`box`) | The own shape, as in a world model's `collision` (`references/asset_structure.md`). |
| `shape_center`, `shape_rotation` | `[x, y, z]` ([0, 0, 0]; degrees) | Where it sits in the object's space. |
| `shape_radius`, `shape_half_height` | metres (0.5, 0.5) | Sphere, capsule, cylinder. |
| `shape_half_extents` | `[x, y, z]` ([0.5, 0.5, 0.5]) | Box. |
| `shape_is_bounded`, `shape_plane_half_extents`, `shape_depth` | true, `[x, z]` ([0.5, 0.5]), metres (0) | Plane. |
| `overrides_layer`, `layer` | false, a layer (`prop`) | `world`, `prop`, `character`, `vehicle`, `dynamic`, `trigger`, `interaction`. |
| `overrides_collides_with`, `collides_with` | false, an array of layers (prop's default mask) | The layers it blocks and is blocked by. |
| `overrides_is_trigger`, `is_trigger` | false, false | A trigger reports what touches it and blocks nothing. |
| `offset`, `rotation`, `scale` | `[x, y, z]` (none, none, [1, 1, 1]) | A transform of all its shapes on top of the object's; the scale above 0 on every axis. |
| `rotation_mode` | `full`, `yaw_only`, `none` (`full`) | How the shapes follow the object's turn: fully, only about the up axis (upright), or not at all (axis-aligned). |
| `is_pushable` | `true` / `false` (false) | On a movable object: the collision step may push it (characters, NPCs, crates). Off, a movable object's collider is kinematic (doors, elevators): it pushes and is never pushed. A static object or a trigger is never pushed. |
| `overrides_mass`, `mass` | false, kilograms (1) | On: this mass instead of the world model's (above 0, at most 1000000). Pushables share a push by mass: equal masses give way equally. |
| `when_blocked` | `stop`, `push_through` (`stop`) | A kinematic mover that would squeeze a pushable against something that does not give way: `stop` stays where it was at the start of the tick, `push_through` moves on (the pushable is left overlapping it as little as it can). |
| `is_continuous` | `true` / `false` (true) | Off: its own motion is not swept, only pushed out where it ends up (cheap movers that never go fast). |
| `reports_persisting_contacts` | `true` / `false` (false) | On: a contact event every tick while it touches something, not only when a touch begins and ends. |

```
"collider": {
    "uses_own_shape": true,
    "shape_half_extents": [0.25, 0.6, 0.8],
    "overrides_layer": true,
    "layer": "trigger",
    "overrides_is_trigger": true,
    "is_trigger": true
}
```
A crate that a door or a character pushes (its object `movable`), heavier than its world model says:
```
"collider": {
    "is_pushable": true,
    "overrides_mass": true,
    "mass": 25.0
}
```

### The character block

The `character` component (T13) makes the object a character: the player or an NPC, an upright capsule whose feet are at
the object's position (`agents/Characters.md`). It names a character asset (`references/asset_structure.md`,
"Characters") and may override every one of its settings, each with its own flag next to its value, as the sprite block
does: `overrides_radius` with `radius`, `overrides_walk_speed` with `walk_speed`, ..., one pair per key of a character
definition, with the same values and rules. While a flag is off its value is kept and saved. What the character is
doing (its velocity, stance, mode) is not saved. The game gives the object a capsule collider when it has none, and keeps
that collider's shape, layer, mask, mass and motion keys in step with the character: edit them through the character.

| Key | Value (default) | |
|---|---|---|
| `character` | an asset name or `null` (null) | The kind of character; none uses the defaults (a human). |
| `overrides_<key>`, `<key>` | false, the key's value (its default) | One pair per setting of a character definition. |

A placed NPC that is a dog that walks slower than dogs do:
```
"character": {
    "character": "dog",
    "overrides_walk_speed": true,
    "walk_speed": 0.8
}
```
Characters made at runtime (the player, spawned NPCs) are not written into world files (below).

### The animator block

The `animator` component (T29) plays the object's skeletal animation: its model component's world model must have a
skinned LOD and animation libraries (`references/asset_structure.md`, "World models" and "Animation libraries";
`agents/Animation.md`). What it plays right now (clip times, weights, fades) is not saved; these keys say how it starts
and how it plays.

| Key | Value (default) | |
|---|---|---|
| `plays_clip` | `true` / `false` (false) | Plays `clip` from the start. Off, it plays only what the game plays on it. |
| `clip` | a clip name, at most 64 bytes, or `null` (null) | The clip, from the libraries. A name they lack plays nothing (logged once). Changing it in play cross-fades. |
| `phase` | 0 to just below 1 (0) | Where the clip starts, a share of its length, so placements of one model move out of step. |
| `speed` | 0 to 100 (1) | Times every clip's own speed; 0 holds the pose. |
| `is_paused` | `true` / `false` (false) | Holds the pose: clips and cross-fades stand still. |
| `fade_seconds` | 0 to 60 (0.2) | How long a change of `clip` cross-fades. |
| `overrides_is_looped`, `is_looped` | false, false | Whether the clip loops, instead of its library's setting. |
| `overrides_animation_libraries`, `animation_libraries` | false, `[]` (at most 16 asset names) | The placement's own animation libraries instead of its world model's. |
| `overrides_animation_graph`, `animation_graph` | false, `null` (an asset name) | The placement's own animation graph instead of its world model's (T30); on with no name: no graph. |

A placed figure that waves in a loop, though its library plays the wave once:
```
"animator": {
    "plays_clip": true,
    "clip": "wave",
    "overrides_is_looped": true,
    "is_looped": true
}
```

### The terrain block

The `terrain` component (B01) places a terrain: a `terrain` asset (`references/asset_structure.md`, "Terrains";
`agents/Terrain.md`) whose mesh lies in the object's space, drawn in chunks and solid. The object is normally `static`
(the default) and has no other component; a collider on the same object wins (its terrain then does not collide). Every
render and collision setting of the terrain asset can be overridden with the two keys the model block uses.

| Key | Value (default) | |
|---|---|---|
| `terrain` | an asset name or `null` (null) | The terrain. None places nothing; a name that does not load draws and collides nothing (logged once). |
| `overrides_has_outline`, `has_outline` | false, true | The outline. |
| `overrides_shadow_tier`, `shadow_tier` | false, `both` | Which cascades it casts into: `near`, `far`, `both`. |
| `overrides_casts_shadows`, `casts_shadows` | false, true | Off: it casts no shadow (it still receives them). |
| `overrides_haze`, `haze` | false, 1 | Times the air's haze in front of it, in [0, 1000] (T35; 0 as if the air were clear). |
| `overrides_draw_distance`, `draw_distance` | false, 4000 | How far each chunk is seen, in metres (above 0). |
| `draw_distance_multiplier` | above 0 (1) | Times the draw distance in effect. |
| `overrides_is_draw_distance_fixed`, `is_draw_distance_fixed` | false, true | On: the player's draw distance setting leaves it as it is. |
| `overrides_is_collision_enabled`, `is_collision_enabled` | false, true | Off: nothing stands on it or bumps into it. |
| `overrides_collision_layer`, `collision_layer` | false, `world` | Its chunks' layer. |
| `overrides_collides_with`, `collides_with` | false, an array of layers (world's default mask) | The layers it blocks and is blocked by; without it, the asset's mask or the layer's default. |

The test world's terrain (object 51), as the shipped file writes it, and a copy that casts no shadow and is seen half as
far:
```
"terrain": {
    "terrain": "placeholder_terrain"
}
```
```
"terrain": {
    "terrain": "placeholder_terrain",
    "overrides_casts_shadows": true,
    "casts_shadows": false,
    "draw_distance_multiplier": 0.5
}
```

### Values

- Numbers: integers or decimals; a float keeps its fraction when written (`2.0`). Angles are degrees.
- Lists of numbers: `[a, b, ...]` (`[]` for none), on one line.
- Vectors: `[x, y]`, `[x, y, z]` (also `{"x": ..., "y": ..., "z": ...}` when read).
- Colours: `"#rrggbb"`, `"#rrggbbaa"` when not opaque (also `[r, g, b, a]` or `{"r": ...}` when read).
- Enums: the entry's name (`"near"`, `"axis_billboard"`).
- Sets of an enum's entries: an array of names in the enum's order (`["world", "prop"]`, `[]` for none).
- Dates: `"yyyy-mm-dd"`, a real day of the calendar (years 1 to 9999).
- Asset names: `[a-z][a-z0-9_.-]*`, at most 128 bytes.

### Tags and data

Every object can carry tags and typed data under namespaced keys (`agents/ObjectData.md`, `agents/NamespacedKey.md`):
`namespace:path`, lowercase, at most 128 bytes. A key written without its namespace (`office`) gets the default one
(`tbp`, or the story's once packs exist) and is always written back in full (`tbp:office`).

```json
{
    "id": 7,
    "tags": ["tbp:office", "town:street_lamp"],
    "data": {
        "town:delivery": {"$type": "town:delivery", "days": 3, "form": {"$key": "town:tax_form"}},
        "town:full": 2.0,
        "town:inventory": ["pen", "stamp"],
        "town:kind": {"$key": "town:desk"},
        "town:open": true,
        "town:opened": {"$date_time": "2026-10-18T09:30:00"},
        "town:owner": "Bob",
        "town:rate": 1.5,
        "town:shade": {"$color": "#ffcc00"},
        "town:spawn": {"$vector3": [1.0, 2.0, 3.0]},
        "town:switch": {"$object": 12},
        "town:visitors": {"$list": "string"},
        "town:visits": 3
    }
}
```

- **Tags** are a list of keys under `tags`; everything else is a map of key to value under `data`. The writer lists
  both in alphabetical order of their keys.
- **Plain JSON types map directly:** an integer is an `int` (64-bit), a number with a fraction or an exponent (`1.5`,
  `2.0`, `3e2`) a `float`, then string, bool, an array a list (every element of its first element's type), an object
  a compound. `null` is refused (leave the entry out).
- **Wrappers** (an object with one entry whose name starts with `$`): `$key`, `$object` (an object id),
  `$vector3`, `$color` (written `"#rrggbb"`, `"#rrggbbaa"` when not opaque), `$date_time`
  (`"yyyy-mm-ddThh:mm:ss"`, up to six fractional digits), `$float` (`{"$float": 2}` reads as a float, for tools that
  drop the `.0`; the writer never uses it), and `$list` for an empty list, naming its element type (`[]` is refused:
  its element type would be unknown).
- **Compounds:** names are 1 to 256 bytes of UTF-8, not starting with `$`; `$type`, when present, is a key naming a
  script-declared type.
- **Limits** (refused when passed): 256 entries and 64 KiB per object, 16 MiB for all of a world's objects together,
  4096 bytes per string, 4096 elements per list or compound, 16 levels of nesting.

### What the writer writes

- Only values that differ from their defaults, so files stay short and pick up changed defaults.
- Keys in table order: `id` first, then the object table's keys, `components` last; component blocks in type order
  (`model`, `sprite`, `light`, `collider`, `character`, `animator`, `terrain`, `model_instances`); tags and data keys
  alphabetically.
- Not the runtime objects: the player and NPCs made during play (`WorldObject_IsPersistent` off) are left out with
  everything under them. The game state will save them on its own.
- The objects **parents first**: the roots in id order, each followed by its subtree depth first, the children in the
  order they were attached to their parent (which a load restores).
- Pretty-printed with four spaces, arrays of numbers and strings on one line, and a newline at the end.
- Floats as the shortest text that reads back to the same float; angles as the shortest degrees that read back to the
  same radians. So a world read and written back is the same file byte for byte, and changing one property in the
  game changes or adds one line of the file.

## GHDF

The same tree with numeric ids instead of keys (the tables' wire ids), only values that differ from their defaults, and
every integer in the smallest GHDF type that holds it.

| Root entry | Value |
|---|---|
| 1 | The format version (an unsigned integer). |
| 2 | The world's name (a string; absent without one). |
| 3 | The next object id (an unsigned integer). |
| 4 | The environment: a compound keyed by the `world_environment` table's wire ids. |
| 5 | The objects: an array of compounds. |

| Object entry | Value |
|---|---|
| 1 | The id (an unsigned integer). |
| 2 | The components: a compound whose entry ids are the component type's value + 1 (1 `model`, 2 `sprite`, 3 `light`, 4 `collider`, 5 `character`, 6 `animator`, 7 `terrain`, 8 `model_instances`), each a compound keyed by the type's wire ids (`model_instances` shares the model block's, without 3 and 9, `omit_pixelation`'s pair, and adds 22 `instances`). |
| 3 and up | The `world_object` table's wire ids: 3 `name`, 4 `position`, 5 `rotation`, 6 `scale`, 7 `tint`, 8 `is_enabled`, 9 `is_visible`, 10 `mobility`, 11 the tags and data, 12 `parent`, 13 `parent_socket` (T30). |

How each type is stored (`agents/PropertyDescriptors.md`, "Types"): integers in the smallest integer type (readers take
any integer type whose value fits), floats as Float (angles in radians), vectors and float lists as Float arrays, but
positions as Double arrays (files written before positions were doubles hold Float arrays, which still read),
colours as UInt8 arrays, render colours as a compound, strings as strings, enums and dates as encoded integers, sets of
enum entries as their mask in the smallest unsigned integer type, the tags
and data as an array of entry compounds (`agents/ObjectData.md`, "GHDF"), and a model instances block's copies as a
compound of parallel arrays: 1 the positions (a Double array, three per copy), then only when a copy differs from its
default 2 the rotations (a Float array, radians, three each), 3 the scales (a Float array, three each) and 4 the tints
(a UInt8 array, four each), each exactly as long as the positions say. A document must end where its root ends.

## Loading rules

Downloaded worlds use the same code, so loading treats every file as hostile:

- **All or nothing.** A world is loaded into fresh memory and handed over only when everything read. On any error
  nothing is left behind (not even the keys the read interned), and the message names the object and the key:
  `object 12: world_object.position: ...`, `object 2: its parent 9 was refused: no object has that id.`
- **Limits:** at most 64 MiB of input, a parsed tree of at most 1 GiB as the readers estimate it while they parse
  (so a file of a few megabytes of empty objects cannot take gigabytes of memory; a game may set a smaller limit for
  downloads), and 100 000 objects; GHDF strings may not declare more than the data holds.
- **Ids:** unique, from 1 to 18446744073709551614.
- **A JSON key given twice** in one object: the last value counts.
- **Parents:** every object is created before any parent is linked, so the file may list objects in any order; a
  parent must exist, must not be the object or one of its descendants, and the hierarchy is at most 64 levels deep.
- **Unknown names** (keys or entries no table knows, component types this game does not have) are skipped with one
  warning per name for the whole file, so a world from a newer game version still loads without the parts it does
  not know. GHDF entry ids a table retired are skipped silently.
- **Versions:** the format version is raised only for changes an older game cannot read by skipping what it does not
  know (a renamed key, a value read another way); new keys and new component types keep it. Every table a world file
  holds is at its version 1 under format 1. Two exceptions, while the test world was the only world file, made without
  a new format version:
  - World models (task T10) renamed the model block's `model` (a model asset) to `world_model` (a world model) and made
    its `has_outline`, `omit_pixelation`, `shadow_tier` and `casts_shadows` the placement's own values behind override
    flags. A JSON file written before reads with its `model` key skipped as unknown (the object draws nothing); a GHDF
    one keeps the name on entry 1, which now reads as the world model's name (it draws where a world model has the
    model's name). Either way its render settings stay unused until their flags are set.
  - World sprites (task T26) did the same to the sprite block: `sprite_animation` (a sprite animation) became
    `world_sprite` (a world sprite) on the same entry 1, and every look setting (`orientation` to `omit_pixelation`)
    became the placement's own value behind an override flag. A JSON file written before draws no sprite (its
    `sprite_animation` key is skipped as unknown); a GHDF one reads the name as a world sprite's (it draws where a
    world sprite has the animation's name, as every shipped one has), with the world sprite's look until the flags are
    set.
