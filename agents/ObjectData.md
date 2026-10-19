# Object data: tags, typed values and queries

Built in T24 (the owner's design for tags and script data, decision G14, refined in PLAN.md 2.10 of
`handoff/2026-10_graphics_and_world/`). The data foundation for tags, scripts and selectors: every world object holds a
map of namespaced keys ([NamespacedKey.md](NamespacedKey.md)) to typed values, set in the editor or by scripts, and
queries search the world by tags, data, components, flags and place. The scripting language's syntax for all this
(`obj.data[#town:visits]`, selectors) waits for the scripting round; the storage, the codecs and the queries are here.

| Module | Files | What it is |
|---|---|---|
| DataValue | `include/DataValue.h`; `source/DataValue.c`, `source/DataValueJSON.c`, `source/DataValueGHDF.c`, private `source/DataValueInternal.h` | One typed value: tag, bool, int, float, string, key, object, vector3, color, date_time, list, compound. Copy, equality, release, depth and size limits, validation, JSON and GHDF. |
| DataMap | `include/DataMap.h`, `source/DataMap.c` | Key id -> DataValue: one object's tags and data (later the global variables). Byte accounting, limits, JSON and GHDF. |
| World data | `include/World.h`, `source/WorldData.c` | The world's data functions (limits, the data revision) and the tag index. |
| WorldQuery | `include/WorldQuery.h`, `source/WorldQuery.c` | Searches over a world's objects: the engine side of selectors. |

Tests: `tests/DataValueTests.c`, `tests/ObjectDataTests.c` (data functions, limits, revisions, queries with a
brute-force comparison on a seeded random world, the tag index, saving through the object table, hostile files), and
the data map cases of `tests/PropertyDescriptorTests.c`.

## DataValue

| Type | Value | Holds |
|---|---|---|
| `tag` | 0 | Nothing: presence only. A tag is a map entry of this type. |
| `bool` | 1 | true / false |
| `int` | 2 | 64-bit signed: the one integer type. |
| `float` | 3 | 64-bit (double), always finite: the one float type. |
| `string` | 4 | UTF-8, at most `DATA_VALUE_MAX_STRING_LENGTH` (4096) bytes, no NUL. |
| `key` | 5 | A namespaced key (a `KeyRegistry` id): a reference to an asset, a preset, a tag ... |
| `object` | 6 | A world object by id (0 = none). |
| `vector3` | 7 | Three finite floats (the engine's `Vector3`). |
| `color` | 8 | RGBA bytes. |
| `date_time` | 9 | Microseconds since 0001-01-01T00:00:00, inside the calendar (see [GameCalendar.md](GameCalendar.md)). |
| `list` | 10 | One element type (fixed at creation, never `tag`), at most `DATA_VALUE_MAX_ELEMENT_COUNT` (4096) elements. |
| `compound` | 11 | Names -> values (never tags), at most 4096 entries, kept sorted by name. |

The values are saved (GHDF writes the type's number): never renumber. One integer type and one float type, not
int8/16/32/64 plus float/double: scripts need no promotion rules or number suffixes (Minecraft's `1b`/`1s`/`1L`/`1f`/
`1d`), and the binary writer still stores each integer in the smallest GHDF type that holds it. If explicit widths are
ever needed (packed binary data), they can come back as storage hints without changing the language's numbers.

`DataValue` is the **storable part** of the future scripting VM's values (`handoff/2026-10_scripting_language/
PROPOSAL.md`): the VM adds functions, class instances and fibers on top. Keep it free of anything that could not be
saved.

- **Building.** `DataValue_CreateTag`, `DataValue_FromBool` / `FromInt` / `FromFloat` / `FromKey` / `FromObject` /
  `FromVector3` / `FromColor` / `FromDateTime` (no memory); `DataValue_CreateString` (and `_CreateStringFromBytes` for
  JSON and GHDF strings: length, NUL and UTF-8 checked), `DataValue_CreateList(elementType)`,
  `DataValue_CreateCompound`. `DataValue_ListAdd` and `DataValue_CompoundSet` **move** the value in (on success the
  container owns it and the source becomes a tag), so building a tree copies nothing. `DataValue_ListRemoveAt`,
  `DataValue_CompoundRemove`; readers: `DataValue_GetListElementType` / `_GetListCount` / `_GetListElement`,
  `DataValue_GetCompoundCount` / `_GetCompoundEntryName` / `_GetCompoundEntryValue` / `_FindCompoundEntry` (const
  pointers: elements are never changed in place).
- **Ownership.** `DataValue_Clone` (deep), `DataValue_Deconstruct` (releases, leaves a tag). Never copy a value that
  owns memory by assignment.
- **Compound names.** 1 to `DATA_VALUE_MAX_NAME_LENGTH` (256) bytes of UTF-8 (spaces allowed: a script's
  `map<string, V>` stores as a compound). A name starting with `$` is reserved, except `DATA_VALUE_TYPE_ENTRY_NAME`
  (`$type`): a key naming a script-declared struct type. These are the "custom types", checked against their
  declaration once the language exists. They are compounds, never arbitrary binary formats, so saves of downloaded
  stories stay safe, versionable and always showable in the editor.
- **Depth.** A scalar, an empty list and an empty compound have depth 1; a container one more than its deepest
  element; at most `DATA_VALUE_MAX_DEPTH` (16). Containers remember their depth, so `DataValue_GetDepth` is O(1) and
  the builders refuse anything deeper: no value past the limit can exist, and every recursive walk is bounded
  (AGENTS.md rule 5). The file readers count levels while reading and refuse a 17th before building it.
- **Equality** (`DataValue_Equals`): same type and value, deeply; floats by value (0 equals -0); an int never equals a
  float; compounds compare as maps (their order is canonical); lists compare element type, count and elements.
- **Size** (`DataValue_GetByteSize`): what the limits count, an estimate of the bytes it takes saved: tag 0, bool 1,
  int/float/object/date_time 8, vector3 12, color 4, string its bytes and 1 (its end), key its text; a list 1 plus its
  elements; a compound 1 plus, per entry, its name's bytes, 1 and the value. Every value but a tag counts at least one
  byte, so a byte limit also caps how many values there are, however they nest: an object's 64 KiB hold at most 65536.
- **Validation** (`DataValue_Validate(value, keys)`): what the builders cannot check: floats and vector3 components
  finite, keys known to the registry, date_times inside the calendar, every element and entry. Messages say where:
  `speeds[1]: the float must be finite.`

## DataMap

A map of key ids to values, sorted by id (binary search), with the bytes it counts (`DataMap_GetByteSize`: per entry the
key's text, 1 and the value's size). Heap-allocated (`DataMap_Create(keys)`, `DataMap_Clone`, the readers;
`DataMap_Destroy`); every reading function takes NULL as an empty map, so an owner can keep NULL until the first entry.
It borrows its key registry and holds no world-specific code: the global and game-state variables (the scoreboard
replacement, any type, not just int32) will use the same type.

`DataMap_Find`, `DataMap_HasTag`, `DataMap_GetEntry` (iteration, by id), `DataMap_Equals` (NULL equals empty),
`DataMap_Set` (validates and copies), `DataMap_Adopt` (moves; for readers), `DataMap_Remove`,
`DataMap_GetEntryByteSize`, `DataMap_CheckLimits`. Limits are the owner's policy, passed as a `DataMapLimits` (entries,
bytes).

## Object data

Every `WorldObject` holds `_data` (a `DataMap*`, **NULL while empty**: no allocation for objects without data, and it
goes back to NULL when the last entry is removed), read with `WorldObject_GetData` and `WorldObject_HasTag`. Change it
only through the world:

- `World_SetData(world, id, key, value)`: copies the value (another type replaces the old one; the same value changes
  nothing); `World_TryGetData(world, id, key, &value)` (named in the house style, the task's `World_GetData`);
  `World_RemoveData(world, id, key, &wasRemoved)`.
- Tags: `World_AddTag` (sets the key to a tag, replacing a value), `World_RemoveTag` (removes only a tag: a value under
  the key stays), `World_HasTag`.
- **Limits** (hostile content):

  | Constant | Value | Per |
  |---|---|---|
  | `WORLD_OBJECT_DATA_MAX_ENTRIES` | 256 (tags included) | object |
  | `WORLD_OBJECT_DATA_MAX_BYTES` | 65536 (64 KiB, `DataMap_GetByteSize`) | object |
  | `WORLD_DATA_MAX_BYTES` | 16777216 (16 MiB) | world (`World_GetDataByteSize`) |

  Past one the call fails with `ErrorCode_InvalidOperation` and changes nothing; the world's logger hears of each limit
  once per world (never per tick: scripts may hit a limit every tick).
- **Revisions.** Data changes advance the object's **data revision** (`WorldObject_GetDataRevision`), never its render
  revision: changing a script variable must not make the renderer recompute anything.
- **Object references.** An `object` value holds an id. It reads as none once that object is removed:
  `World_CreateObject` never hands an id out twice, so `World_TryGetObject` fails for it. (`World_CreateObjectWithId`,
  for loaders and a future undo, may bring an id back; a reference to it then sees the restored object.)

**The tag index.** The world keeps tag key id -> the ids of the objects carrying it, ascending (`_tagIndex`, private,
`source/WorldData.c`), updated on every tag change and object removal, so tag queries do not scan the world. Table
writes replace an object's whole map behind the world's back (a JSON or GHDF read, `Properties_CopyTo`): the object
table's `AfterSet` then calls `WorldInternal_SyncObjectData`, which recounts the object's bytes and indexes its tags
again. Index entries are never removed (a tag that lost its last object keeps an empty entry), and nothing is
recomputed per frame.

## Queries

`World_Query(world, &query, outIds)` appends the ids of the matching objects; `World_DoesObjectMatch(world, &query, id,
&isMatch)` tests one object (a trigger checking who entered, a script testing an object). A `WorldQuery`
(`WorldQuery_Create` matches everything; the arrays are borrowed for the call) combines with AND:

- tags: `AllOfTags`, `AnyOfTags`, `NoneOfTags`;
- `DataKeys` present (any type, tags included);
- `Comparisons`: `{Key, Operator, Value}`, the key must be present. `Equal` and `NotEqual` compare numbers by value
  across int and float (3 equals 3.0, exactly, even for large ints) and other types exactly; `Less`, `LessOrEqual`,
  `Greater`, `GreaterOrEqual` order numbers (int and float) and date_times, and never match other types;
- `ComponentTypes` present;
- `IsEnabledOnly`, `IsVisibleOnly` (off by default; scripts' searches set `IsEnabledOnly`, since a disabled object is
  skipped by every system, and the editor leaves it off). Both are effective (T08): an object under a disabled or
  hidden ancestor counts as disabled or hidden;
- within `MaximumDistance` of `Origin`; inside the box `BoxMinimum`..`BoxMaximum` (faces inside); positions are the
  objects' world positions (`WorldObject_GetWorldPosition`, through their parents; T08);
- the order: object ids ascending, or nearest to `Origin` first (`IsSortedByDistance`, ties by id); then `Limit`.

Malformed queries (a NULL array with a count, key id 0, an ordering operator on a string, a NaN, a negative distance,
an inverted box, a component type the world does not have) fail with `IllegalArgument`. With an all-of tag the
candidates are the smallest of those tags' lists; with only any-of tags, their lists merged; otherwise every object is
scanned, which is fine for hundreds of objects. The world reuses two scratch buffers, so a query allocates nothing
once warm.

**No selector syntax here.** A string like `@objects[tag=town:street_lamp, data.town:power>3, distance<20]` belongs to
the scripting language, which compiles it to a `WorldQuery`.

## Files

The object table's `data` row (wire id 11, type `DataMap`, [PropertyDescriptors.md](PropertyDescriptors.md)) saves the
map: one row, two JSON keys. The row is `ReadOnly` for `Properties_Set` (changes go through the world, which keeps the
index, the limits and the revision); world files and copies still fill it. Reading data needs the key registry:
`PropertyReadOptions.Keys`, and `DefaultNamespace` for keys written without one (the story's or pack's id; `tbp` when
NULL).

### JSON (until T09 writes `references/world_structure.md`, this is the reference)

```json
{
    "name": "street lamp",
    "tags": ["game:office", "town:street_lamp"],
    "data": {
        "town:visits": 3,
        "town:owner": "Bob",
        "town:rate": 1.5,
        "town:full": 2.0,
        "town:open": true,
        "town:kind": {"$key": "town:desk"},
        "town:switch": {"$object": 12},
        "town:spawn": {"$vector3": [1, 2, 3]},
        "town:shade": {"$color": "#ffcc00"},
        "town:opened": {"$date_time": "2026-10-18T09:30:00"},
        "town:inventory": ["pen", "stamp"],
        "town:visitors": {"$list": "string"},
        "town:delivery": {"$type": "town:delivery", "form": {"$key": "town:tax_form"}, "days": 3}
    }
}
```

- **Tags** are a list of keys under `tags`; everything else is a map of key -> value under `data`. A key may leave out
  its namespace (`"office"` reads as `tbp:office`, or in the reader's default namespace); the writer always writes
  the full key. The writer lists the tags in alphabetical order; `data`'s order is WRJSON's (hash order).
- **Plain JSON types map directly:** an integer is an `int`, a number with a fraction or an exponent (`1.5`, `2.0`,
  `3e2`) a `float`, then string, bool, an array a list (all elements of its first element's type), an object a
  compound. `null` is refused (leave the entry out).
- **Wrappers** (an object with exactly one entry whose name starts with `$`): `$key`, `$object`, `$vector3`, `$color`
  (any colour form GameJSON reads; written as `"#rrggbb"`, `"#rrggbbaa"` when not opaque), `$date_time`
  (`"yyyy-mm-ddThh:mm:ss"`, up to six fractional digits), `$float`, and `$list` for an **empty list**, which names its
  element type (`[]` is refused: its element type would be unknown).
- **Whole floats keep their fraction.** WRJSON keeps integers and fractions apart when reading, and since
  2026-10-19 its writer keeps a whole real number's fraction too (`JSON_Serialize` writes `2.0`, not `2`), so a float
  is always written plain. `{"$float": 2}` still reads as a float, for files from tools that drop the `.0`; the
  writer never uses it.
- **Compounds**: names are any 1-256 bytes of UTF-8 not starting with `$`; `$type`, when present, is the key's text.
- **Errors** say where, prefixed with the table: `world_object: data.town:inventory[2]: the element's type is string,
  but the list holds int values (its first element's type).`, `world_object: tags[1]: Namespaced key "Town:B":
  uppercase letters are not allowed (keys are lowercase).`

### GHDF

The `data` entry is a Compound array of entry compounds, sorted by key text: entry 1 the key's full text (String),
2 the type (UInt8), 3 the value (absent for a tag), 4 a list's element type (UInt8). Values: Boolean; ints in the
smallest of Int8/16/32/64; floats as Float when exact, else Double; strings and keys as String; objects in the smallest
of UInt8/16/32/64; vector3 as a Float array of 3; color as a UInt8 array of 4; date_time as Int64; a list as one array
of its elements (the smallest integer type for all of an int list, Float or Double for floats, 3 floats per vector3, 4
bytes per color, Int64 per date_time, a Compound array of value compounds for lists and compounds); a compound as a
Compound array of entry compounds with names. Readers accept any integer type that holds the value, skip unknown
entry ids (forward compatibility), require full keys, and refuse duplicates.

### Hostile content

Every reader checks every rule while reading, before building anything large: the depth (16), string and list sizes,
names, keys (a key listed twice, also once as a tag and once with a value, is refused), types (a GHDF float or vector3
component must be finite, as in JSON), and the object's limits (the descriptor's rules: 256 entries, 64 KiB). The bytes
are counted while a value is read, against what the limit leaves after the entries before it: a value too large stops
where the bytes run out (`world_object: data.town:big[15][4070]: the data takes at most 65536 bytes.`), not after it
was built whole. Each value is then validated again (`DataValue_Validate`) before it joins the map, as a last check
behind the readers' own.

**A refused read leaves nothing behind**: not in the object (table reads are all or nothing), and not in the key
registry: the readers (`DataValue_ReadJSON`/`ReadGHDF`, `DataMap_ReadJSON`/`ReadGHDF`, `Properties_ReadJSON`/
`ReadGHDF`) take `KeyRegistry_GetCount` before reading and give back the keys they interned when they fail
(`KeyRegistry_TruncateTo`), so content cannot fill the registry by being refused again and again.

The world's 16 MiB cannot be checked by the generic table code (it does not see the world while it reads one object):
it is counted, logged once when passed, and **a world loader (T09) must refuse a world whose `World_GetDataByteSize`
passes `WORLD_DATA_MAX_BYTES` after reading its objects**. Refusing a whole world after some objects were read, the
loader must do the same as the readers: destroy what it built, then truncate the registry to the count it took before
reading. The key registry has its own cap (2^20 keys).

### Authored and runtime data

Values set in the editor are saved in the world file. Values scripts set at runtime will be saved in game saves as
differences from the world, when the save round designs that (architecture 3.7). Nothing here prevents it: the data
revision says which objects changed, `DataMap_Equals` compares, and the map is plain data.
