# World file structure

A world file holds one 3D world: its name, its environment (sky, sun, moon, fog, effects) and its objects with their
tags, data and components. It has two forms with the same content:

- **JSON** (`<name>.world.json`): authored worlds. Readable, editable by hand, and written so that git diffs stay
  small.
- **GHDF** (`<name>.world.ghdf`, `references/ghdf.txt`): saves, packed stories and worlds sent over the network.
  Compact: the test world is 7003 bytes as JSON and 1354 bytes as GHDF.

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
| `environment` | object | The environment's settings by key (the `world_environment` table). Absent keys keep their defaults. |
| `objects` | array of objects | The objects. |

### An object

| Key | Value | |
|---|---|---|
| `id` | integer, required | Unique in the world, from 1 to 18446744073709551614. Integers above 9223372036854775807 are written as strings. |
| `name` | string or `null` | At most 256 bytes; need not be unique. |
| `parent` | integer | The id of the object this one is placed on; 0 or absent for none. |
| `position`, `rotation`, `scale` | `[x, y, z]` | Relative to the parent. The rotation is in degrees, applied X, then Y, then Z. |
| `tint` | colour | `"#rrggbb"`, or `{"tint": [r, g, b], "brightness": b, "opacity": o}` when brightness or opacity is not 1. |
| `is_enabled`, `is_visible` | `true` / `false` | |
| `mobility` | `"static"` / `"movable"` | |
| `tags` | array of keys | See "Tags and data". |
| `data` | object | See "Tags and data". |
| `components` | object | One block per component, keyed by the type's name: `model`, `sprite`, `light`. Each block holds that type's keys (`model_component`, `sprite_component`, `light_component` in `agents/WorldData.md`). |

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
| The draw distance | `overrides_draw_distance` | `draw_distance`: metres, above 0 (default 600) |
| Keeping it fixed against the quality setting | `overrides_is_draw_distance_fixed` | `is_draw_distance_fixed` (default false) |

`draw_distance_multiplier` (above 0, default 1) multiplies the draw distance in effect, the world model's or the
placement's own: 2 is seen twice as far. It is a plain setting, not an override.

```
"model": {
    "world_model": "office_desk",
    "uses_variant": true,
    "variant": "dark_wood",
    "overrides_draw_distance": true,
    "draw_distance": 250.0,
    "draw_distance_multiplier": 1.5
}
```

### Values

- Numbers: integers or decimals; a float keeps its fraction when written (`2.0`). Angles are degrees.
- Vectors: `[x, y]`, `[x, y, z]` (also `{"x": ..., "y": ..., "z": ...}` when read).
- Colours: `"#rrggbb"`, `"#rrggbbaa"` when not opaque (also `[r, g, b, a]` or `{"r": ...}` when read).
- Enums: the entry's name (`"near"`, `"axis_billboard"`).
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
  (`model`, `sprite`, `light`); tags and data keys alphabetically.
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
| 2 | The components: a compound whose entry ids are the component type's value + 1 (1 `model`, 2 `sprite`, 3 `light`), each a compound keyed by the type's wire ids. |
| 3 and up | The `world_object` table's wire ids: 3 `name`, 4 `position`, 5 `rotation`, 6 `scale`, 7 `tint`, 8 `is_enabled`, 9 `is_visible`, 10 `mobility`, 11 the tags and data, 12 `parent`. |

How each type is stored (`agents/PropertyDescriptors.md`, "Types"): integers in the smallest integer type (readers take
any integer type whose value fits), floats as Float (angles in radians), vectors as Float arrays, colours as UInt8
arrays, render colours as a compound, strings as strings, enums and dates as encoded integers, the tags and data as an
array of entry compounds (`agents/ObjectData.md`, "GHDF"). A document must end where its root ends.

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
  holds is at its version 1 under format 1. One exception, while the test world was the only world file: world models
  (task T10) renamed the model block's `model` (a model asset) to `world_model` (a world model) and made its
  `has_outline`, `omit_pixelation`, `shadow_tier` and `casts_shadows` the placement's own values behind override flags,
  without a new format version. A JSON file written before reads with its `model` key skipped as unknown (the object
  draws nothing); a GHDF one keeps the name on entry 1, which now reads as the world model's name (it draws where a
  world model has the model's name). Either way its render settings stay unused until their flags are set.
