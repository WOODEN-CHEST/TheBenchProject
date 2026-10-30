# Property descriptors (property tables)

`include/PropertyDescriptor.h`; `source/PropertyDescriptor.c` (tables, instances, validation, access by key),
`source/PropertyDescriptorJSON.c`, `source/PropertyDescriptorGHDF.c`, and the private
`source/PropertyDescriptorInternal.h` they share. Tests: `tests/PropertyDescriptorTests.c`.

One static **property table** per data type describes every property of the type once: its key, binary (wire) id,
type, field offset, default, rules and flags. Generic functions driven by the table do everything else, so a new
property is one table row, not six hand-written places (struct, defaults, validation, copy, encoder, decoder) that
must agree. The editor's inspector, scripts (later: the `Script` flag, types and descriptions feed the language's type
checker and autocomplete), saves and the world files all read the same tables.

**Clients:** `WorldEnvironment` ([WorldData.md](WorldData.md), "Environment"), `GameClock` ([GameClock.md](GameClock.md)),
the world objects (`world_object`, with its `data` row for tags and custom data and its `parent` row, both ReadOnly for
`Properties_Set`, whose reads and copies the world reconciles in the table's `AfterSet`) and their components
(`model_component`, with its override pairs and LOD rows, `sprite_component`, with its 18 override pairs,
`light_component`; [WorldData.md](WorldData.md), "Property tables"), and a world sprite's settings
(`world_sprite` and `world_sprite_render`, which read its definition: [WorldSprites.md](WorldSprites.md)). The sprite
component's value rows and the world sprite's rows come from the same macros (`source/WorldSpriteInternal.h`), so the
two tables share every key, default, rule and text: a way to keep one copy of a row in two tables. The same
for characters: a character asset's settings (`character`) and the character component (`character_component`: the
asset's name, then an override flag and a value per setting) share their rows (`source/CharacterInternal.h`;
[Characters.md](Characters.md)). The weather (later) gets a table too.

## Declaring a table

Next to the struct, in its `.c` file:

```c
static const PropertyDescriptor ThingProperties[] =
{
    {
        .Key = u8"light_pollution", .WireId = 26U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(Thing, LightPollution),          // Offset, Size and MemberName from the member
        .Default.FloatValue = 0.35f, .Rules = PROPERTY_RULES_BETWEEN(0.0, 1.0),
        .Flags = PROPERTY_FLAGS_SETTING,                 // Persistent | Editable | Script
        .Category = u8"Night sky", .DisplayName = u8"Light pollution", .Description = u8"...",
        .SinceVersion = 1U
    },
    ...
};

static const PropertyTable ThingTable =
{
    .Name = u8"thing", .Version = 1U, .StructSize = sizeof(Thing),
    .Descriptors = ThingProperties, .DescriptorCount = sizeof(ThingProperties) / sizeof(ThingProperties[0]),
    .RetiredWireIds = NULL, .RetiredWireIdCount = 0U, .AfterSet = NULL, .Migrate = NULL, .MigrateGHDF = NULL
};
```

Hand the table out with a getter (`WorldEnvironment_GetPropertyTable`) and keep the type's own functions as thin
wrappers (`SetDefaults` → `Properties_Construct`, `Validate`, `CopyTo`, `Deconstruct`, `ReadJSON`, `WriteJSON`).
**Add every new table to the `tables_pass_their_check` test** in `tests/PropertyDescriptorTests.c`.

Rules for rows:
- **Keys** are snake_case (`[a-z][a-z0-9_]*`), unique in the table: the JSON key and the name scripts and
  `Properties_Get/Set` use. For struct fields, the field name in snake_case (`TimeOfDay` → `time_of_day`).
- **Wire ids** are the GHDF entry ids: not 0, unique, and **never reused or renumbered**. When a property is removed,
  put its id into the table's `RetiredWireIds` (the check rejects a retired id on a descriptor).
- **Enums** that are saved give their C constants explicit values and never renumber them: GHDF stores the value,
  JSON the entry's name.
- **Strings** (`String`, `AssetName`) must set `Rules.MaxLength` (content from other players must not grow a save
  without bound). A string that may be NULL (`IsNullAllowed`) must default to NULL, because GHDF has no null: a NULL
  string is an absent entry, which reads back as the default.
- **Display order** is table order: the inspector's order and the order the JSON writer inserts keys in.
- `SinceVersion` is the table version that added the property, in [1, `Version`].

`PropertyTable_Check` enforces all of this (plus: fields inside `StructSize` with the size their type needs, no two
fields overlapping, bounds only on bounded numeric types and finite/ordered/whole as the type needs, enum tables only
on enums with unique snake_case names and values that fit the field, only known flag bits, the override flags below)
and checks that **every default passes its own rules**.

### Override flags

A setting that a placement can override (a world model's draw distance on one placed object) is **two rows**: the flag `overrides_<key>` (`PROPERTY_OVERRIDE_FLAG_PREFIX`), a `Bool` whose
default is false, right before the value row `<key>`. While the flag is off the asset's value applies and the row's own
value is kept, saved and shown, so switching the flag back on restores it. No sentinel values (0, an empty name, -1) and
no `inherit` states: a flag cannot collide with a real value, and the editor shows a checkbox next to the field.
- `PropertyTable_Check` requires every row whose key starts with `overrides_` to be a `Bool` and the rest of its key to
  name another row of its table; the editor and scripts pair them by these names.
- Two plain rows rather than a new "optional value" property type: every reader, writer, `Properties_Set`, the copy and
  the comparison handle them as they are, JSON and GHDF carry both, and no type is needed per value type (bools,
  floats, enums, names, distance lists). The cost is that nothing stops a caller from reading a value without its
  flag: the type that owns the table resolves the pairs in one pure function (`WorldModelComponent_ResolveSettings`).
- A plain multiplier with a neutral default (`draw_distance_multiplier` 1) is not an override: one row.

## Types

| Type | In memory | JSON | GHDF |
|---|---|---|---|
| `Bool` | `bool` | `true` / `false` | Boolean |
| `Int32` / `UInt32` | `int32_t` / `uint32_t` | integer | the smallest integer type that holds the value |
| `UInt64` | `uint64_t` | integer, a decimal string above INT64_MAX | the smallest integer type that holds the value |
| `Float` | `float`, always finite | number (shortest text that reads back) | Float |
| `Double` | `double`, always finite | number (shortest text that reads back) | Double |
| `Angle` | `float`, **radians** | number, **degrees** (shortest text that reads back to the same radians) | Float, radians |
| `Vector2` / `Vector3` | raylib vectors | `[x, y]` / `[x, y, z]` | Float array of 2 / 3 |
| `Vector3Double` | `Vector3Double` (`wr/WRVectorMath.h`): positions in the world | `[x, y, z]`, each the shortest text that reads back to the same double (a value written as a float's shortest text prints the same) | Double array of 3; a Float array of 3 reads too (older saves) |
| `AngleVector3` | `Vector3`, **radians** (Euler rotations) | `[x, y, z]`, **degrees** (each the shortest text that reads back to the same radians) | Float array of 3, radians |
| `Color` | raylib `Color` (RGBA all data) | `"#rrggbb"`, `"#rrggbbaa"` when not opaque | UInt8 array of 4 |
| `RenderColor` | `RenderColor` | `"#rrggbb"` when brightness and opacity are 1, else `{tint: [r,g,b], brightness, opacity}` | Compound {1: UInt8[3] tint, 2: Float brightness, 3: Float opacity} |
| `String` | owned `unsigned char*` | string or `null` | String (absent when NULL) |
| `AssetName` | owned `unsigned char*`, `AssetManager_IsValidAssetName` | string or `null` | String (absent when NULL) |
| `Enum` | integer of 1, 2 (unsigned) or 4 bytes | the entry's name, e.g. `"near"` | EncodedInteger (the value) |
| `ObjectId` | `uint64_t`, 0 = none | like UInt64 | the smallest integer type that holds the value |
| `Date` | `int64_t` day number ([GameCalendar.md](GameCalendar.md)), inside the calendar (years 1 to 9999) | `"yyyy-mm-dd"` only, e.g. `"2026-10-18"` | EncodedInteger (the day number; three bytes for this era) |
| `DataMap` | owned `DataMap*` ([ObjectData.md](ObjectData.md)), NULL while empty (its only default) | two keys: the tags under the descriptor's `TagsKey` (`["town:lamp"]`), the values under its `Key` (`{"town:visits": 3}`) | Compound array of entry compounds |
| `FloatList` | owned `PropertyFloatList*`, NULL while empty (its only default) | array of numbers, `[3.0, 6.0, 60.0]` (`[]` when empty) | Float array (empty when empty) |
| `EnumSet` | `uint32_t` mask: bit N is the enum entry of value N | array of the entries' names in the table's order, `["world", "prop"]` (`[]` when empty) | the smallest unsigned integer type that holds the mask |
| `AssetNameList` | owned `PropertyAssetNameList*`, NULL while empty (its only default) | array of strings, `["human_motions", "gestures"]` (`[]` when empty) | String array (empty when empty) |
| `InstanceList` | owned `PropertyInstanceList*` of `PropertyInstanceEntry` (a `Vector3Double` position, a radians `Vector3` rotation, a `Vector3` scale, a `Color` tint), NULL while empty (its only default) | array of instances, each `{"position": [x, y, z], "rotation": [x, y, z], "scale": s or [x, y, z], "tint": "#rrggbb"}` with the defaults left out (rotation 0, scale 1, white; the position always written; rotations in degrees, a scale one number when even, the tint as a `Color`); any other key is refused; `[]` when empty | Compound {1: Double array of the positions (3 per instance), 2: Float array of the rotations in radians, 3: Float array of the scales, 4: UInt8 array of the tints (4 each)}; 2, 3 and 4 only when an instance differs from its default, each of exactly its length |

The integer types are written in the smallest GHDF integer type that holds the value (a `parent` of 12 is one
UInt8 byte) and read from any GHDF integer type, fixed width or encoded, whose value fits the property's type
([GameGHDF.md](GameGHDF.md)); data written with wider types still reads. `Enum` and `Date` stay encoded integers.

JSON readers go through `GameJSON` ([GameJSON.md](GameJSON.md)), so they accept every form asset definitions accept
(colors as arrays or `{r,g,b,a}`, vectors as `{x,y,z}`, numbers as numeric strings); `Vector2`, `Vector3` and
`Vector3Double` arrays must have exactly 2 and 3 elements (`GameJSON_ParseVector3Double` reads each element as a
double). A RenderColor's tint alpha is not part of its value: not written, read as 255,
skipped by comparisons.

Rules (`PROPERTY_RULES_*`): minimum/maximum, each inclusive or exclusive, for `Int32`, `UInt32`, `Float`, `Double`,
`Angle` (radians) and every vector component (`AngleVector3` in radians, `Vector3Double` in double: the object's
`position` keeps each axis within ±`WORLD_POSITION_LIMIT`) (floats compare in float precision, so a bound of 0.1 accepts 0.1f;
doubles in double precision); `PROPERTY_RULES_HALF_OPEN(min, max)` is [min, max), for values that wrap at the maximum
(seconds of a day). `MaxLength` and `IsNullAllowed` for strings. Floats and doubles are always required finite.
`UInt64`, `ObjectId`, `Bool` and `Color` accept every value; a `Date` takes no bounds, its type's rule is the
calendar's range. A JSON date must be the exact text "yyyy-mm-dd" of an existing day; a bare day number is refused, so
files stay readable. A `DataMap` takes `PROPERTY_RULES_DATA_MAP(maxCount, maxBytes)` (`Rules.MaxCount` entries and
`Rules.MaxLength` bytes, both required) and a `TagsKey` (snake_case, unique among the table's keys; only data maps take
one); its default must be NULL (empty). A `FloatList` takes `Rules.MaxCount` (required, 1 to
`PROPERTY_FLOAT_LIST_MAX_COUNT`, 4096), bounds that apply to every value (every value is finite), and
`Rules.IsStrictlyIncreasing` (each value above the one before; only lists take it): `PROPERTY_RULES_FLOAT_LIST(count)`,
`PROPERTY_RULES_INCREASING_FLOAT_LIST_ABOVE(count, minimum)` (a placement's LOD distances); its default must be NULL.
An `EnumSet` takes an enum table like an `Enum`, whose values must be 0 to `PROPERTY_ENUM_SET_MAX_VALUE` (31, the table
check refuses others); a mask with a bit that is no entry of the table is refused. JSON reads an array of names: an
unknown name or one given twice is refused, naming its index (`[1]: ...`). The collider's `collides_with` (a set of
collision layers) is the first. An `AssetNameList` takes `PROPERTY_RULES_ASSET_NAME_LIST(maxCount, maxLength)` (both
required: 1 to `PROPERTY_ASSET_NAME_LIST_MAX_COUNT`, 64, names and 1 to `PROPERTY_ASSET_NAME_LIST_MAX_LENGTH`, 256,
bytes each); every name must be a valid asset name (`AssetManager_IsValidAssetName`); its default must be NULL. The
animator's `animation_libraries` is the first. An `InstanceList` takes `PROPERTY_RULES_INSTANCE_LIST(maxCount,
positionLimit)` (`Rules.MaxCount`, required: 1 to `PROPERTY_INSTANCE_LIST_MAX_COUNT`, 262 144, and every position's
components within ±`positionLimit`); every rotation and scale must be finite; its default must be NULL. A refusal names
the instance (`"...; instance 12's are not"`), and a JSON read's error its place (`[12].scale: Expected a number or
three numbers.`). The model instances component's `instances` is the first.

Flags: `Persistent` (written and read by JSON and GHDF), `Editable` (inspector), `ReadOnly` (visible, refused by
`Properties_Set`; reads from files still fill it), `Script`. `PROPERTY_FLAGS_SETTING` = Persistent | Editable | Script.
A property without `Persistent` is runtime state the table still copies, compares and validates.

## Functions

- **`PropertyTable_Check(table)`**, **`PropertyTable_FindByKey(table, key)`** (a linear scan; resolve once when
  calling often), **`PropertyTable_FindByJSONKey(table, key)`** (the persistent row a JSON key belongs to, also a data
  map's tags key: exactly the keys the JSON reader reads; the world files report unknown keys with it once per name),
  **`PropertyType_GetName(type)`**.
- **Lifecycle.** `Properties_Construct(table, instance)` zeroes `StructSize` bytes, then writes every default (strings
  cloned): call it on **new memory only** (it does not release what the instance held) and pair it with
  `Properties_Deconstruct`, which frees the owned strings (NULLing them; safe to repeat). Fields the table does not
  describe stay zero for the type's own constructor.
- **Validation.** `Properties_Validate(table, instance)` returns `ArgumentOutOfRange` for the first broken property,
  naming the table, the key, the C member and the rule:
  `world_environment.light_pollution (LightPollution) must be finite and in [0, 1].`
  `Properties_CheckValue(table, descriptor, value)` checks one value; **hand-written setters call it**, so their rules
  live in the table only.
- **Copy and compare.** `Properties_CopyTo` deep-copies every described field (strings cloned, the destination's old
  ones released; onto itself does nothing). `Properties_Equals` compares every described field (floats by value,
  strings by bytes).
- **By key.** `Properties_Get(table, instance, key, &value)` (a string points into the instance) and
  `Properties_Set(table, instance, key, &value)`: the value must have the property's exact type, passes
  `Properties_CheckValue` before anything is written, a string is cloned; `ReadOnly` properties are refused
  (`InvalidOperation`). Build values with `PropertyValue_From*`.
- **JSON.** `Properties_WriteJSON(table, instance, pool, compound, options)` validates first, then sets one key per
  persistent property; by default values equal to their default are left out (`PropertyWriteOptions.
  AreDefaultsWritten` writes them all). WRJSON compounds keep insertion order, so the keys are serialized in
  table order and a new key moves no other. `Properties_ReadJSON(table, compound, instance, options)`:
  - **All or nothing**: it reads into a scratch copy (on the stack up to 1 KiB of struct, a heap block above),
    validates it, then commits. On any failure the instance is unchanged.
  - Every persistent property comes from its key or **gets its default when the key is absent**; non-persistent
    properties keep their current values.
  - Errors are `InvalidJSON` and name the table and key: `test_record.weight: Expected a decimal value.`
  - **Unknown keys** (and keys of non-persistent properties) follow `PropertyReadOptions.UnknownKeyMode`: `Error` (the
    default: the read fails, naming them), `Warn` (one warning per read through the options' `Logger`, naming up to 8
    keys; without a logger it acts like `Ignore`), `Ignore`.
  - **Migration**: when `options.DataVersion` is older than the table's `Version` (and not 0, "current"), the table's
    `Migrate(fromVersion, compound, options.Pool, options.Logger)` runs first and rewrites the compound in place
    (rename keys, convert values; the logger, which may be NULL, takes its warnings). `Properties_MigrateJSON(table,
    compound, options)` runs it on its own: a reader that reports unknown keys itself (the world files) migrates first,
    then reports, then reads with `DataVersion` 0. A migration's error is returned with the table's name in front. A
    table raises `Version` together with adding its migrations (the model, model instances and terrain components'
    move their version 1 shadow tier into the shadow distance, [WorldFile.md](WorldFile.md), "Versions").
  - **Data maps** are read from their two keys (both count as known keys) with `options.Keys`, the key registry their
    keys are interned in (required when the data holds anything: the read fails without it), and
    `options.DefaultNamespace` for keys written without one. Their errors name where in the data the problem is,
    prefixed with the table: `world_object: data.town:list[1]: ...`. All or nothing covers the registry too: a read
    that fails, for any reason, gives back the keys it interned (`KeyRegistry_TruncateTo`).
- **GHDF.** `Properties_WriteGHDF` / `Properties_ReadGHDF`: the same as JSON with wire ids as entry ids. Unknown and
  retired ids are skipped (forward compatibility); a known id with the wrong type, a wrong array length or a broken
  rule fails with `Deserialize`. Migration is the same as JSON's through the table's `MigrateGHDF(fromVersion, compound,
  options.GHDFPool, options.Logger)` (`Properties_MigrateGHDF` on its own); the GHDF pool is `PropertyReadOptions.GHDFPool`
  (NULL by default: a migration that adds entries needs it). A table with a `Migrate` should have a `MigrateGHDF` too,
  reading the old wire ids (retired in the table, so nothing else reads them).
- **The `AfterSet` hook** (optional, on the table) is called after `Properties_Set` with the descriptor, and after a
  committed read or a `CopyTo` with NULL (anything may have changed). The world objects use it to advance their
  revision, so caches notice changes made through tables; every component table uses the world's
  `WorldInternal_ComponentAfterSet`, which advances the component's revision and then runs its type's `OnPropertySet`
  hook with the world (the sprite rebinds its animation when its name changed).

Write functions validate first, so invalid data is never written. On a failure after validation the target compound
may hold some entries: discard it.

## Private fields

Tables write fields by offset, including `_`-prefixed (read-only) ones. **This module is the one place allowed to do
that**; everything else respects the `_` convention and goes through the type's functions or the Properties_* calls.

## Adding a property type

`PropertyType` switches have no `default:`, so `-Wswitch` lists every place that needs the new type:
`GetStorageSize`, `PropertyInternal_AreDataEqual`, `PropertyInternal_CheckData` and `PropertyType_GetName`
(`PropertyDescriptor.c`), `CreateValue` and `ParseValue` (`PropertyDescriptorJSON.c`), `PutValue` and `ParseEntry`
(`PropertyDescriptorGHDF.c`). Also decide whether it takes bounds (`IsBoundedType`, and `IsBoundValid` for their
range) and whether it owns memory (`PropertyInternal_IsOwnedType`: the strings and `DataMap`; an owned field is freed,
cloned and handed over through `PropertyInternal_AdoptOwned` and `CloneOwned`), document it in the header's type table
and here, and test it in `tests/PropertyDescriptorTests.c` (the test record has one field of every type but
`DataMap`, which has its own record: it needs a key registry). Examples to follow: `Double` and `Date`,
`AngleVector3` (an object's rotation), `DataMap` (tags and typed data), `FloatList` (a placement's LOD
distances), `EnumSet` (a collider's layer mask), `Vector3Double` (an object's position, named
`vector3_double`), `AssetNameList` (an animator's own animation libraries, named `asset_name_list`) and
`InstanceList` (a model instances component's copies, named `instance_list`).

### Float lists

`PropertyFloatList` (in `PropertyDescriptor.h`) is one heap block: its count and its values (a flexible array member).
The empty list is NULL, never an empty block. `PropertyFloatList_Create(values, count, &list)` copies (NULL for 0; at most
`PROPERTY_FLOAT_LIST_MAX_COUNT`), `_Destroy`, `_Clone`, `_Equals` (by value, NULL is empty, 0 equals -0), and the inline
`_GetCount` / `_GetValues` (0 and NULL for NULL). A list field is owned like a string: `Properties_Set` copies the list
it is given (`PropertyValue_FromFloatList`), `Properties_Get` borrows the instance's own, copies clone, reads hand a new
list over. The readers check the count against the row's `MaxCount` before they allocate anything (JSON: "The list has
9 values; at most 8 are allowed."); the values' rules are checked when the read commits, like every rule.

### Asset name lists

`PropertyAssetNameList` (in `PropertyDescriptor.h`) is one heap block too: its count, the name pointers (a flexible
array member) and the names' bytes. The empty list is NULL. `PropertyAssetNameList_Create(names, count, &list)` copies
(NULL for 0; at most `PROPERTY_ASSET_NAME_LIST_MAX_COUNT` names of at most `PROPERTY_ASSET_NAME_LIST_MAX_LENGTH` bytes;
the names' grammar is the row's rule, checked on set and read), `_Destroy`, `_Clone`, `_Equals` (the same names in the
same order; NULL is empty), `_GetCount` and `_GetName(index)`. Owned like a float list: `Properties_Set` copies,
`Properties_Get` borrows, reads hand a new list over, and the readers check the count and each name's length before
they allocate. Duplicates are allowed (the first one that has a clip wins, for the animator).

### Instance lists

`PropertyInstanceList` (in `PropertyDescriptor.h`) is one heap block: its count and its entries (a flexible array
member of `PropertyInstanceEntry`). The empty list is NULL. `PropertyInstanceList_Create(entries, count, &list)` copies
(NULL for 0; at most `PROPERTY_INSTANCE_LIST_MAX_COUNT`), `_Destroy`, `_Clone`, `_Equals` (every field of every entry,
the position in double; NULL is empty), and the inline `_GetCount` / `_GetEntries`.
`PropertyInstanceEntry_CreateDefault()` is the entry every missing JSON key falls back to: position 0, rotation 0,
scale 1, opaque white. Owned like a float list: `Properties_Set` copies (`PropertyValue_FromInstanceList`),
`Properties_Get` borrows, copies clone, reads hand a new list over, and the readers check the count before they allocate
(GHDF: the positions' array must be a multiple of 3 long, the others exactly as long as the positions say). The rules are
checked when the read commits.

## Known limits

- No cross-field rules (each property is checked on its own) and no bounds on `UInt64`.
- Tables are flat: no nested property tables. The one nested value, a `DataMap`, limits its own depth (16 levels, its
  codec; [ObjectData.md](ObjectData.md)); any later nested type needs a limit too (AGENTS.md rule 5).
- Not thread-safe (tables are immutable, instances are plain structs).
