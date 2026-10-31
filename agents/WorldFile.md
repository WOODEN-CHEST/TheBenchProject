# WorldFile

`include/WorldFile.h`; `source/WorldFile.c` (the loading rules both formats share, the object order, files, paths),
`source/WorldFileJSON.c`, `source/WorldFileGHDF.c`, and the private `source/WorldFileInternal.h` they share. Tests:
`tests/WorldFileTests.c`. JSON is for authored worlds; GHDF is there too, compact enough for the network. The format
reference is `references/world_structure.md`.

Saves and loads a `World` ([WorldData.md](WorldData.md)) as JSON or GHDF. Both are the same tree: the root (format
version, name, next object id, environment, objects), each object's id, its object table and its components, each
from its type's table. The property tables ([PropertyDescriptors.md](PropertyDescriptors.md)) write and read every
value, so this module only walks the tree and applies the world's loading rules, written once for both formats.

## API

| Function | |
|---|---|
| `WorldFile_ReadJSON(bytes, count, options, outWorld)` / `_ReadGHDF` | Load from memory (packed stories, the network). |
| `WorldFile_WriteJSON(world, outBytes)` / `_WriteGHDF` | Append the file's bytes to a buffer; nothing is appended on failure. The world is const. |
| `WorldFile_LoadJSON(path, options, outWorld)` / `_LoadGHDF` | Read a file (at most `WORLD_FILE_MAX_BYTE_COUNT`, 64 MiB, checked before reading), then as above. |
| `WorldFile_SaveJSON(world, path)` / `_SaveGHDF` | Write a file atomically: the bytes go to `<path>.tmp` next to it, which then replaces it (`FileSystem_ReplaceEntry`); missing parent folders are made. |
| `WorldFile_BuildPath(baseDirectory, name, format, outPath)` | `<base>/worlds/<name>.world.json` or `.world.ghdf` (until story packs exist; a `// TODO:` marks it). |

`WorldFileReadOptions` (`WorldFileReadOptions_Create(keys, assets, logger)`): the program's key registry (required:
the world borrows it), the asset manager its components load from (or NULL), the logger (the world's, and the
reader's warnings), the default namespace for keys written without one (NULL: `tbp`) and `MaxTreeByteCount`, the most
memory the parsed tree may take (`WORLD_FILE_MAX_TREE_BYTES`, 1 GiB; lower it for files from strangers when the worlds
expected are small; 0 is refused).

**`outWorld` is caller memory, not constructed.** A successful read leaves it constructed with the file's world
(release it with `World_Deconstruct`); a failed one leaves nothing there. A World must not move once constructed, so a
caller that replaces a live world loads into a second `World` and switches to it on success: a failed load never
touches the world in use.

## Loading rules (WorldFileLoad, WorldFile.c)

Downloaded worlds will use this, so every file is treated as hostile.

1. **Parse, within the tree limit:** after the arguments (a tree limit above 0) and the input's size (at most
   `WORLD_FILE_MAX_BYTE_COUNT`, 64 MiB) are checked, `JSON_DeserializeWithLimits` / `GHDF_ReadWithLimits` parse with the
   options' `MaxTreeByteCount`. A document's size bounds its tree only loosely (an empty JSON object is 3 bytes and over a
   kilobyte parsed, an empty GHDF compound in an array 1 byte and as much), so the readers estimate the tree as they
   build it and refuse the document once it passes the limit: the 64 MiB input cap alone would allow tens of gigabytes,
   and running out of memory aborts the game. A typical object (a name, a transform, a component) is estimated at about
   6 KiB in either format, so 1 GiB leaves room for 100 000 objects with more on them.
2. **Begin:** the options are checked, the key registry's count is taken, the world is constructed
   (`World_Construct2`).
3. **The root:** the format version is required, at least 1 and at most `WORLD_FILE_FORMAT_VERSION`; the name follows
   `World_SetName`'s rule (at most 256 bytes of UTF-8, no NUL); the environment is read through its table.
4. **The objects:** at most `WORLD_FILE_MAX_OBJECT_COUNT` (100 000), checked before any is made. **Every object is
   created first** (`World_CreateObjectWithId`; the id must be there, not 0, at most `WORLD_OBJECT_ID_MAX` (no world
   holds `UINT64_MAX`: a file would have no next id past it), and not used twice), so every parent named afterwards
   exists, whatever order the file lists objects in.
5. **Each object's table** is read (`Properties_ReadJSON` / `_ReadGHDF`, with `UnknownKeyMode` Ignore: the load reports
   unknown names itself). The table's `AfterSet` attaches the object to the parent it brought through the world's
   checks; when the world refused it (`World_GetRefusedParentCount` rose), the load fails naming the object, the
   parent and why: no such object, the object itself, a cycle (the parent is inside the object's subtree), or more than
   64 levels.
6. **Its components:** each block is added (`World_AddComponent`) and read through its type's table.
7. **The data limit:** after each object, the world's data must stay within `WORLD_DATA_MAX_BYTES` (16 MiB): the
   table readers count but cannot refuse it, and checking per object stops a hostile file early.
8. **Finish:** a `next_object_id` that is not above every id is raised to one that is, with a warning (the counter is
   already past every id: `World_CreateObjectWithId` moves it); `UINT64_MAX` means every id was handed out (the world's
   counter is used up, `World_EnsureNextObjectIdAtLeast`); the world matrices are cached (`World_UpdateTransforms`);
   the warnings are logged.
9. **Fail** (any error): the world is deconstructed and the registry truncated to the count taken at the start
   (`KeyRegistry_TruncateTo`), so a refused file leaves no keys behind either. Every error carries the format's code
   (`ErrorCode_InvalidJSON` / `ErrorCode_Deserialize`) and names where: `objects[3]: the id 1 is used twice.`,
   `object 12: world_object.position: ...`, `environment: world_environment.light_pollution ...`. A message that quotes
   the file (a key, a name) is made printable first (`WorldFileLoad_Wrap`: control characters, C1 controls too, and
   every byte of text that is not UTF-8 show as `?`), so a hostile file cannot put terminal escapes in the log.

**Unknown names** (`WorldFileLoad_NoteUnknown`): keys (JSON) or entry ids (GHDF) that neither the block nor its table
knows, and component types the world does not have, are skipped and logged **once per name for the whole file**, with
a count and the first object (`World file: skipped the unknown key "positon" in world_object (3 times; first in object
1).`). At most 32 distinct names are listed, the rest counted; names are cut to 64 bytes and their control characters
shown as `?`. GHDF ids a table retired are skipped silently. These warnings are logged only when the load succeeds (a
failed load's error says what is wrong; the world it was building may still log its own lines, such as a refused
parent, before it is destroyed).

**Versions** (`GetTableVersion` in WorldFile.c): `WORLD_FILE_FORMAT_VERSION` is 3. Format 3 writes asset names in full
(`tbp:desk`, which a format-2 game refuses: its names had no colon); formats 1 and 2 wrote them short, which every read
takes as `tbp`'s (the asset name rows keep names in full, [PropertyDescriptors.md](PropertyDescriptors.md)), so they need
no migration. Format 1 holds every table at its version 1; formats 2 and 3 every table at its current `Version` (2 for
`model_component`, `model_instances_component` and `terrain_component`, 1 for the others). The reads pass that as `PropertyReadOptions.DataVersion`, with the load's
`Logger`, JSON pool and GHDF pool. Each block (the environment, each object, each component) is **migrated before its
unknown names are noted** (`Properties_MigrateJSON` / `Properties_MigrateGHDF`, [PropertyDescriptors.md](PropertyDescriptors.md)),
so the keys a migration consumes are no unknown names; a migration's error fails the load, naming the object. The three
tables' migration from version 1 reads the shadow tier pair (`WorldModelPlacement_MigrateShadowTierJSON` / `GHDF` in
`source/WorldModelComponentInternal.h`): `near` becomes a 16 m `shadow_distance` (`WORLD_MODEL_NEAR_TIER_SHADOW_DISTANCE`,
with `overrides_shadow_distance` on when `overrides_shadow_tier` was), each only where the block does not give it
already; `both` changes nothing; `far` is dropped with one warning per block through the load's logger
(`model_component: an older file's shadow_tier "far" is dropped: ...`); anything else fails the load. The tier's GHDF
ids are retired (10 and 4 in the model tables, 4 and 5 in the terrain's), so a newer file never reuses them. A table
that raises its `Version` again needs a new world format version and that mapping (a `// TODO:` there says so, and
`every_table_in_a_world_file_is_at_the_version_its_format_writes` fails until the mapping and the test agree).

## Writing

- **Validate first:** the property tables validate every object and component before writing (a position that is not
  finite, say). Writers append nothing on failure.
- **The next id** is the world's counter, or `UINT64_MAX` once every id was handed out
  (`WorldFileInternal_GetNextObjectIdToWrite`): no id is that large, so the world read back has its counter used up too
  instead of a warning and a counter that starts over.
- **The object order** (`WorldFileInternal_CollectObjects`): the roots in id order, each followed by its subtree depth
  first (no recursion: the links lead back up), children in attach order. Parents come before their children, diffs
  stay stable, and a load restores the attach order. **Runtime objects** (`WorldObject_IsPersistent` false: the
  player and spawned NPCs, which the game state will save on its own) are left out with their whole subtrees; a saved
  object never hangs under one.
- **JSON:** `id`, the object table's keys (defaults left out), then `components` keyed by registry name in type
  order. Serialized pretty (`JSONSerializeFlags_PrettyPrinted | JSONSerializeFlags_InlineScalarArrays`: WRJSON keeps
  insertion order, so keys come out in table order and vectors on one line) with a final newline.
- **GHDF:** the root entries 1 to 5 (`WorldFileRootEntry`), objects with entry 1 the id and 2 the components (keyed by
  the type's value + 1, `WorldFileObjectEntry`), the tables' wire ids for the rest; integers in the smallest type
  ([GameGHDF.md](GameGHDF.md)).
- **Positions are doubles** ([WorldData.md](WorldData.md)): JSON writes each axis as the shortest text that reads
  back the same double, so every digit survives (`[100001.333333333, 0.5, -99999.25]`) and a position written as a
  float's shortest text prints as it was; GHDF writes a Double array of 3. An older GHDF save with Float arrays still
  loads, each position the float it held. A position that is not finite, or beyond `WORLD_POSITION_LIMIT` (1e7 m) on
  an axis, is refused like any value outside its row's rules.
- **Pools:** each read and write makes its own JSON or GHDF pool and destroys it afterwards, the tree with it (not
  returned first, which would walk it once more): a JSON pool keeps a copy of every key until it goes, so a shared one
  would only grow, and a large world's compounds should not stay pooled. Walking a compound uses an enumerator on the
  stack (`WorldFileInternal_BeginEnumeration`); the GHDF reader walks components by their ids, as the entry
  collection's enumerator would allocate an inner one per object.
- **Scale:** 100 000 objects with a name and a position, all roots in descending id order, on the owner's machine at
  -O3: written in 0.71 s as JSON (11.9 MB) and 0.25 s as GHDF (2.9 MB), read in 0.60 s and 0.41 s.
  `a_world_at_the_object_limit_saves_and_loads` keeps the case: the roots' sort must not recurse once per root (that
  overflows the stack) and the readers' pools must not take quadratic time.

Saving a world read from a file without changing it writes the same bytes (a test checks it with the shipped test
world), so the test frame's F5 save diffs against its file only where the game changed something.

## Known limits

- **The tree limit is an estimate.** Its charges come out at or a little above what a 64-bit build measures (1.0 to
  1.5 times), but the pools keep up to one section of spare capacity per kind of value (at most 1 MiB each), which the
  estimate leaves out. At the default a hostile file can still make a load take about a gigabyte before it is refused;
  lower `MaxTreeByteCount` for downloads once their sizes are known.
- **Log volume.** A component whose asset is missing logs a warning each (the world's, not the reader's), so a file of
  many sprites naming a missing animation logs a line per sprite, even when the load then fails.
- **Duplicate JSON keys** (two `objects`, two `components` in one object): the last one counts, as WRJSON keeps it.
- **Durability.** The save replaces the file atomically, but nothing flushes it to the disk first (WRFramework has no
  sync call): after a power cut the new file could be empty on some file systems. Fine for development saves.
- **Compression** for the network is left to later (a `// TODO:` in WorldFileGHDF.c: untrusted compressed data needs
  its own size limit before it is decompressed).
- **Runtime state** (sprite playback, revisions, script data changed in play) is not part of a world file; game saves
  as differences from the world come later.
