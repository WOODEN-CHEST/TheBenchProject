# Prefabs

A group of objects built once and placed many times: a bus stop, a house from kit pieces, a park corner. Editing the prefab updates every
copy, each copy sets the prefab's own properties, and each copy can change anything else it needs. This is how few models become many
specific places.

| Module | Files | What it is |
|---|---|---|
| Prefab | `include/Prefab.h`, `source/Prefab.c`, `source/PrefabDefinition.c`, `source/PrefabInternal.h` | The `prefab` asset type (`prefabs/`): the template objects, the declared properties, their targets, every rule checked when a file is read. |
| WorldPrefabComponent | `include/WorldPrefabComponent.h`, `source/WorldPrefabComponent.c` | Component type 12, `prefab`: the prefab's name, the copy's values and its overrides. Holds settings only. |
| WorldPrefabs | `include/WorldPrefabs.h`, `source/WorldPrefabs.c` (the components, the objects), `source/WorldPrefabsApply.c` (what the values and overrides come to), `source/WorldPrefabsInternal.h` | The system: it keeps every copy's objects up to date, once a tick and only when something changed; ids, selection lookups, limits. |
| Test content | objects 111 to 118 of the test world, `source/WorldTestPrefabs.{h,c}`, `asset_source/scripts/placeholder_prefabs.py` | A street: a kit, a bus stop, a street lamp, houses; the keys that change them. |

The asset's format: `references/asset_structure.md`, "Prefabs"; the kit's rules: "Building kits" there. The component's: `references/world_structure.md`,
"The prefab block". Tests: `tests/PrefabTests.c`, `tests/WorldPrefabTests.c` (also 173 km out).

## What a world keeps

The copy's root (an ordinary saved object), the prefab's name, its values and its overrides. Nothing else:

```json
"prefab": { "prefab": "tbp:small_house",
    "values": { "roof_color": "#2e4a7a", "layout": "garage" },
    "object_overrides": { "bin": { "is_removed": true }, "gate": { "rotation": [0, 35, 0] } } }
```

The prefab's objects are made at load as **runtime children** of the root, which world files never save with their subtrees, so the file
stays small and editing the prefab updates every copy. **The prefab's root is a child of the copy's root**, not the same object: the copy's
root stays plain, holding the component and whatever is hung on it. What a copy adds of its own is an ordinary saved child of the root: added
objects hang off the root, since a runtime parent's subtree is not saved. The system never touches them, and they belong to no copy.

## The asset

Objects in the world file's own format, read by the world file's own reader (`WorldFile_ReadObjectsJSON`) into a template `World` that is never
shown, so a prefab has every table, codec and rule a world has, except that what it does not know is refused rather than skipped. `format_version`
is required: the same migrations apply. The objects have one root, and each has a **name** of its own inside the prefab, a local name
(`[a-z][a-z0-9_.-]*`): that name addresses it and gives it its id.

A template's tags and data live in a key registry of the prefab's own: an asset may serve worlds of other registries, and a copy's tags go to its
world's registry by their text.

A definition keeps its file's bytes and checks them when it is read, so a bad file is skipped at startup. Loading reads them again into a prefab of
its own, which shares nothing with the definition. It has no dependencies: what its objects name is loaded by those objects when a copy makes
them, and a prefab may be nested by a name a property chooses, which no file can check.

## Properties

The prefab declares them; they are data, not a static table, so the editor builds a copy's options panel from them:

```json
"properties": {
    "roof_color": { "type": "color", "default": "#7a3b2e", "display_name": "Roof", "category": "Looks",
        "targets": [ { "tag": "tbp:roof", "row": "tint" } ] },
    "door_angle": { "type": "angle", "default": 0, "minimum": 0, "maximum": 110,
        "targets": [ { "object": "door", "row": "rotation", "axis": "y" } ] },
    "layout": { "type": "enum", "default": "plain", "options": {
        "plain":  [ { "object": "garage", "row": "is_enabled", "value": false } ],
        "garage": [ { "object": "front_door", "row": "is_enabled", "value": false } ]
    } }
}
```

- **Types** (`PrefabPropertyType`): `bool`, `int`, `float`, `color`, `vector`, `vector2`, `angle`, `enum`, `asset`, `string`. A `default` is required. `minimum` and `maximum` for a number or each axis of a vector; `display_name`, `category`, `description`; `asset_type` for an asset, what the panel offers.
- **Targets**, by `object` name or `tag` (every object of the prefab with it, one colour for 30 roof pieces): a `row` of the object's table or, with `component`, of one of its components' tables (by its name in world files). `axis` writes a number into one axis of a vector row, or `all` of them, leaving the rest: an angle into a door's rotation.
- **Any row** but a `ReadOnly` one, a data map and an object's `name`, which addresses it. An override pair's flag is a row like any other.
- **Pass-through:** `{ "object": "lamp", "property": "light_color" }` writes the property of the prefab that object holds.
- **An enum is a variant:** each option is a list of values to set, and `option_display_names` names them. An object an option turns off is still there, disabled. There is no separate variant system.
- **Checked when a file is read:** every name of the file's own objects, rows and options, a default or an option's value through every row it reaches. A pass-through's property and its value are the nested prefab's, which a target may even choose: that copy checks them, and warns.
- **Left out on purpose:** expressions and logic, until scripting exists. A `window_count` drives a path object's `count`.

**A value is the JSON form of what it targets**, held as a plain `DataValue` (`PropertyType_DataValue`, [PropertyDescriptors.md](PropertyDescriptors.md)): `"#2e4a7a"`, `35` in degrees, `[0, 35, 0]`, in JSON and in GHDF alike. It is not typed by its target because a copy's rows may be saved with the prefab missing, and only the prefab knows them. `Properties_SetFromDataValue` reads it through the row's own reader, so one colour reaches a tint and a light's colour, and the row's rules apply.

A copy's value goes through `PrefabProperty_CheckValue` (its type, its range, an enum's options). A bad one, or a name the prefab no longer declares, is warned about once and the default takes its place.

**A value is plain, so its caps are `DataValue`'s:** a list or a compound holds at most 4096 elements, a text 4096 bytes, and it nests 16 deep. A row longer than that (a list of 5000 records) cannot be overridden: a world file writing one is refused, as any bad row refuses its file.

## Applying

In this order, each over the last: the prefab's objects; each property in the order declared; the copy's overrides. So an override beats a property.

The rows are worked out on a zeroed scratch instance of the object or component: the template's value, then each write in order. Only what comes out different from the real row is written, so:

- a re-apply that changes nothing advances no revision, and no system rebuilds;
- **it undoes what it no longer writes:** a row touched last time and not now comes back to the template's, so choosing an enum's next option undoes the last;
- **rows no property or override touches are left as they are,** whoever changed them. A door a script opened stays open until something writes its row.

A row that refuses a value is warned about once and left as the writes before it made it. An object a copy removed takes no writes.

## Overrides

Per object, by its name, in a block:

| Key | What it does |
|---|---|
| `is_removed` | The object and what lies under it are not made. `false` is the same as leaving it out for an object of this copy's prefab; through a nested copy (`lamp/bulb`) it says something, since that copy's own overrides may remove the object. `WorldPrefabComponent_SetObjectRemoved` writes it so. |
| a row of the object's table | Moved, turned, disabled. Not `name`, `parent` or `data`. |
| `components` | `{ "model": { "uses_variant": true, "variant": "red" } }`: rows of its components' tables. Adds no component. |

A key is a **path of names**: `lamp/bulb` is the object `bulb` of the copy the object `lamp` holds. It lands in that copy's own overrides, over what the template gave that path row by row, and the nested copy applies it.

**A name the prefab no longer has** is warned about once and ignored, so an edited prefab never breaks a copy; so is a row that does not exist and a component the object does not have.

The setters (`WorldPrefabComponent_SetValue`, `_SetObjectRow`, `_SetObjectRemoved`, `_ClearObjectOverride`, each through the table) are what the editor's inspector uses, and `_GetObjectOverride` shows which rows are overridden.

## Ids

An object's id is `WorldPrefabs_DeriveObjectId(root id, name, salt)`: FNV-1a over the root's id, the name and the salt, then a mix, the same on every machine and every load. A nested copy's objects derive from its own root's id, so "the bench in this bus stop" holds across loads. A derived id lies in the upper half of the id range (`WORLD_OBJECT_DERIVED_ID_FIRST`) and comes from `World_CreateDerivedObject`, which leaves the id counter alone: **a saved world's `next_object_id` does not grow with what copies make.** An id that is taken takes the next salt, with a warning: in practice only a hand-made object on it.

A path object inside a copy makes its own children from the counter, as any path object does, so that does use up ids. A piece of a path object (below) derives its id from the path object's id, its slot, its model and its index.

**In a template's data,** a `$object` value names one of the prefab's objects by the id its file gives it: in a copy it names that object of the copy, whether it is made yet or not. An id no object of the file holds reads as none (0). Tags and `$key` values go from the prefab's registry to the world's by their text.

## Nesting

A nested copy is an ordinary runtime object with the component: what the outer copy passes it lands in its `values` and `object_overrides` rows, and its own apply follows from its revision. All of it settles within one update.

**Cycles** are refused by walking up the copies a root lies in, when a copy resolves its name, never when a file is read: a target may choose a nested prefab's name. It walks through runtime objects only, so a copy hung on another's root as an ordinary child is no cycle. At most `WORLD_PREFABS_MAX_NESTING` (16) copies nest. A refused copy makes nothing, with a warning.

## In a path object's slot

A record of a path object's `models` (`path_object_model`) names a `prefab` in place of a `world_model`, with its `values` and `randomizes_enums` (every enum its values leave out is picked per copy from the seed): a lamp with its light every 25 m, each with its own state.

Each piece is a runtime child of the path object's object with this component, so it is an object, never an instance: use it where a piece needs more than a mesh. It stands as a rigid piece (a shear taken as a step, since an object has no shear) at an even scale, however its slot stretches or its node widens, since an uneven scale would shear whatever is turned inside it. Its length is the slot's `length` setting or 0, it collides as its prefab's objects do, and a model record's `tint` does not reach it (use a property). See [PathObjects.md](PathObjects.md).

## Selection

What a click needs. A click on any object walks up to the nearest copy's root, and an "enter" action reaches the objects inside:

| Function | |
|---|---|
| `WorldPrefabs_TryGetCopyRoot(world, id, &root)` | The nearest: itself if it holds the component, else the first ancestor that does, walking through runtime objects only. An ordinary saved child a copy added belongs to no copy; nor does what a saved object generates. |
| `WorldPrefabs_TryGetOutermostCopyRoot(world, id, &root)` | What a first click selects. |
| `WorldPrefabs_TryFindObject(system, root, "lamp/bulb", &id)` | An object by its path. |
| `WorldPrefabs_GetCopyObjects(system, root, &ids)` | Its objects, parents first, not the nested copies'. |
| `WorldPrefabs_GetPrefab(system, root)` | The prefab it shows, for its options panel: `Prefab_GetProperty` and its kin. |

## The system

`WorldPrefabs_Update`, once a tick, after the transform drivers and before the path objects (a prefab may hold a fence) and `WorldCollision_Step`.

**At load,** generators wait on each other: prefabs may hold path objects, whose pieces may be prefabs again, each standing on the colliders the last made. Run them in turn until a round makes nothing (`SettleGenerators` in the test frame: `WorldPrefabs_Update`, `WorldCollision_Sync`, `WorldPathObjects_Update`), so the first frame shows them whole.

**What it watches:** a change of a component's rows, by any means, through the world's per-type revision (`World_GetComponentTypeRevision`), and the component set. A quiet tick costs two comparisons, however many copies there are; a change looks at each copy's revision once.

**A changed prefab** reaches its copies with `WorldPrefabs_Rebuild`: it lets go of every prefab and makes every copy again from the definitions as they are now. Ids and overrides stay. A loaded asset never changes by itself (`AssetManager.md`, "Reload"), so a prefab no copy names any more stays held until then, or until the system goes. **A path object's pieces** pick their enums when the path object builds, from the prefab as it is then: `WorldPathObjects_Rebuild` as well, or they keep the options they picked (warned about, the default in their place) until something else rebuilds them.

**Removed from outside:** an object a script removed stays gone until the copy next changes, then comes back. Until then the lookups do not name it. An id it left is nothing of the copy's: a saved object that takes it keeps its own rows, and the copy's object comes back under the next salt.

**Going.** `WorldPrefabs_Destroy` removes what it made. A component removed takes its objects along; a new prefab name makes the copy anew.

## Limits

| Limit | Value | Past it |
|---|---|---|
| Objects of a prefab | `PREFAB_MAX_OBJECTS` 16 384 | The file is refused. |
| Properties, options of one, targets of one | 256, 256, 1024 | The file is refused. |
| Objects every target of a prefab reaches together | `PREFAB_MAX_TARGET_OBJECTS` 65 536 | The file is refused: a tag can reach many, so a small file could ask for any amount of memory and work per copy. |
| Objects all of a prefab's tag targets look through together | `PREFAB_MAX_TAG_SCAN` 4 194 304 | The file is refused: each looks through every object, whatever it reaches. |
| Bytes of a value, of a copy's values, of its overrides | 64 KiB, 64 KiB, 1 MiB | The file or the row is refused. What a nested copy's row is given past them is warned about, and the rest still goes in. |
| Copies nested in one another | `WORLD_PREFABS_MAX_NESTING` 16 | That copy makes nothing, with a warning. |
| Objects a world's copies make (`WorldPrefabsLimits`) | 1 048 576 | What is past it is left out, with a warning, until an update that looks finds room: about 1.2 GiB of objects. A copy with no room at all allocates nothing. |
| Bytes the copies hold (`WorldPrefabsLimits`) | 512 MiB | A copy counts its prefab's file, whose objects it copies, and every value it writes (`DataValue_GetByteSize`). A file that does not fit makes nothing; values that do not fit are not written, and the rows come back to the template's. Both with a warning, until an update that looks finds room. Objects alone do not bound what a small file can ask for: one holding a list of 262 144 instances copies it into every object. |
| An update's work (`WorldPrefabsLimits`) | 2 097 152 | An object looked at, a row written and 64 bytes copied are one each. The copies past it wait for the next update, which starts with them, and an update always finishes the first copy it starts on (`WorldPrefabs_IsSettled`). |
| Warnings a copy gives | 64 | Past them it stays quiet. |
| Warnings all copies give together (`WorldPrefabsLimits`) | 1024 | Past them it stays quiet until the next rebuild, and says so once. |

## Drawing

Each kit piece stays an ordinary object with its own draw: nothing about prefabs reaches the renderer. Grouping identical models, or lean drawing, are candidates for later, and nothing here blocks them.

## Saved games

Runtime objects are not saved, so a door opened inside a copy would reset on load unless its changed rows are written as overrides. What the save system will need, which is not built:

- For each copy, walk `WorldPrefabs_GetCopyObjects` and compare each object's rows, and its components', with what the copy's values and overrides make of the template. What differs goes into the save as an extra block per path, in the override's own form; on load it joins the copy's `object_overrides`, or a second list applied after them.
- An object a script removed is a block with `is_removed`.
- The rows a copy's overrides write are already in the world file: a save need only hold what play changed.
- An id stays the same across loads, so what a save holds by id still means the same object, unless the prefab renamed it. A saved reference to a name a prefab no longer has reads as none.

## Not built

"Unpack" (turning a copy into ordinary objects). A randomise action in the editor that writes real values. In-place reload of a prefab (with the editor's asset reload: its revision compared where `state->Asset` is). Overrides that add a component, and tags or data set by overrides. A null for a row in a value: `is_enabled` or `is_visible` do what a null model would.

## Test content

Objects 111 to 118 of the test world, a street on the flat west of the slab, generated by `asset_source/scripts/placeholder_prefabs.py`:

| Object | What it shows |
|---|---|
| 111 `bus_stop_plain` | A copy at its defaults, with a nested street lamp. |
| 112 `bus_stop_changed`, 113 | Values (colour, layout), overrides (a moved bench, a removed bin, a row through the nested lamp) and an ordinary child of its own. |
| 114 `bus_stop_faulty` | An enum passed through to the nested lamp: a flickering light. |
| 115 `house_plain`, 116 `house_shop`, 117 `house_no_chimney` | 229 objects each: nested wall bays (an enum of wall, window and door), kit floors, a roof; a colour through nested copies to every bay. |
| 118 `street_lamps` | A path object whose pieces are street lamp prefabs, each one's state from the seed. |

**Keys** (`WorldTestPrefabs`, listed with the test frame's keys in [WorldRuntime.md](WorldRuntime.md)): Control with the keypad's 1 steps the plain bus stop's layout, 2 its roof's colour, 3 moves its bench, 4 removes its bin, 5 steps the plain house's wall colour, 6 reads the files again and makes every copy and path object anew. I logs the counts.
