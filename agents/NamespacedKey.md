# NamespacedKey and KeyRegistry

`include/NamespacedKey.h`, `source/NamespacedKey.c`. Tests: `tests/NamespacedKeyTests.c`.

A **namespaced key** names something across story packs without collisions, like Minecraft's identifiers:
`town:street_lamp`, `game:office`. Tags and the keys of object data use them today
([ObjectData.md](ObjectData.md)); asset names across packs, climate presets, actions and events will
use them later.

## The key

- **Syntax.** `namespace:path`. The namespace is `[a-z0-9_.-]+`, the path `[a-z0-9_./-]+`, and the whole key, colon
  included, is at most `NAMESPACED_KEY_MAX_LENGTH` (128) bytes. Uppercase letters, spaces, a second colon and every
  other character are refused, so a key has exactly one spelling. `NAMESPACED_KEY_BUFFER_SIZE` (129) holds any key and
  its terminator.
- **The default namespace.** A key written without a namespace (`office`) gets its context's default namespace: the
  story or pack id once packs exist, `NAMESPACED_KEY_DEFAULT_NAMESPACE` (`tbp`, The Bench Project) until then. Keys are stored, compared
  and written always in the full form: the writers never leave the namespace out, so a file moved to another pack
  keeps its meaning.
- **Functions.**
  - `NamespacedKey_IsValid(key)` and `NamespacedKey_Validate(key)`: a full key. The error
    (`ArgumentOutOfRange`) says what is wrong: an empty namespace or path, no colon, uppercase, a character a namespace
    or path may not use, too long. It quotes the key with every byte that is not printable ASCII as `?`, cut to 64
    bytes, so a hostile key cannot put control characters or broken UTF-8 into a message.
  - `NamespacedKey_Parse(text, byteCount, defaultNamespace, outKey)`: a key as people write it, with or without its
    namespace (`text` need not be terminated: JSON strings are not). It appends the full form, NUL-terminated, to a
    byte buffer (one growing string, like the string writers), and appends nothing on failure. A stack buffer of
    `NAMESPACED_KEY_BUFFER_SIZE` bytes through `GenericBuffer_CreateConstant` always suffices.
  - `NamespacedKey_IsNamespaceValid(text, byteCount)`, `NamespacedKey_GetNamespaceLength(key)` (the colon's index) and
    `NamespacedKey_GetPath(key)` (a pointer after the colon).

## The registry

A `KeyRegistry` interns keys: it maps the full text to a `uint32_t` id and back, so code compares keys as integers and
an object stores 4 bytes per key. **One registry per program**: `main.c` constructs it before the frames, `Services.Keys`
carries it, every `World` borrows it (`World_Construct1(world, keys)`, `World_Construct2(world, keys, assets,
logger)`), and it is torn down after the frames. Ids exist only at runtime; files always hold the text.

- `KeyRegistry_Construct` / `KeyRegistry_Deconstruct`.
- `KeyRegistry_Intern(registry, key, &id)`: a full key (validated); the same text always gives the same id. Ids run
  from 1 to `KeyRegistry_GetCount` in interning order; `NAMESPACED_KEY_ID_NONE` (0) is never a key.
- `KeyRegistry_InternText(registry, text, byteCount, defaultNamespace, &id)`: parse, then intern. What the file
  readers use.
- `KeyRegistry_TryFind(registry, key, &id)`: look up without interning (any text; an invalid one is not found). A key
  that was never interned cannot be on any object, so a search can stop early.
- `KeyRegistry_GetText(registry, id)`: the full key, or NULL for an id the registry does not hold. **The text keeps its
  address as long as the key is interned**: texts sit in 16 KiB blocks that never move (a key never spans two), so a
  caller may hold the pointer across later interning.
- `KeyRegistry_TruncateTo(registry, count)`: forgets every key interned after the registry held `count` keys, the
  readers' undo: take `KeyRegistry_GetCount` before a read and truncate to it when the read fails, so a refused file
  leaves no keys behind. The forgotten ids are handed out again (interning the same keys in the same order gives the
  same ids), so truncate only when nothing holds an id or a text of those keys any more, which is true once a failed
  read has released what it built. The kept keys keep their ids, texts and addresses; the storage of the forgotten
  texts is reused, and blocks past the last kept text are freed.
- `KeyRegistry_FindByPrefix(registry, prefix, outIds)`: appends the ids whose text starts with `prefix`, in
  alphabetical (byte) order of their text, for the autocomplete of the planned scripting language's terminal
  (`#town:` lists the known town keys). A linear scan, well under a millisecond for thousands of keys; the appended
  range is sorted in place through a view, so the buffer's earlier contents are untouched.

**Inside.** A `WRHashMap` maps the text's 64-bit FNV-1a hash to the id of the most recent key with that hash, and an
array chains the ids whose texts share a hash (`_nextWithSameHash`): a lookup compares texts along the chain, so two
texts with one hash still get two ids. Interning is O(1) on average. Each id's text is found by its location (a block
index and an offset, `_locations`); texts are stored in id order. Only truncation removes keys: it walks the forgotten
ids from the newest, handing each hash chain back to the key that headed it before (the newest key always heads its
chain), then cuts the storage after the last kept text.

**Not a Registry.** The key registry interns key texts for the whole program; a `Registry` ([Registry.md](Registry.md))
maps some of those ids to entries of one kind (actions, commands), and its prefix search scans only its own entries.

**Limits.** At most `KEY_REGISTRY_MAX_KEY_COUNT` (2^20) keys: interning more fails with `InvalidOperation`, so content
from other players cannot grow the registry without bound. Interning is permanent for the program's life (keys of every
story loaded in a session stay), except that a read that fails gives back the keys it interned (above): the data
readers all do, and a loader that refuses a whole file after reading parts of it must do the same.

## Thread safety

Not thread-safe. The registry is used from the main thread only, like the worlds.
