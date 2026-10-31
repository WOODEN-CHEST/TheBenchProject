# Registry and RegistryRoot

`include/Registry.h`, `source/Registry.c`. Tests: `tests/RegistryTests.c`.

A **registry** holds the named registrations of one kind: entries that engine code, an asset type's loader (from its
definitions) and scripts register under namespaced keys ([NamespacedKey.md](NamespacedKey.md)). Input actions and
their categories are the planned first users; commands, events, script modules and story-defined terminal commands
are the same shape. The **root registry** is the registry of registries: tools that work on every kind (the terminal's
listings, autocomplete for a parameter typed "a key of registry X", the editor's pickers, a debug dump) find any
registry through it.

## What is not a registry

A registry is for named registrations, not for everything with a name:
- **Assets** stay in the asset manager ([AssetManager.md](AssetManager.md)): it loads lazily, holds assets per user,
  unloads on the last release, loads dependencies and overrides definitions by search-root priority. A registry has
  none of that. Data that packs declare goes through asset types; an asset type's loader may register what its
  definitions declare into a registry.
- **World component types** (numbers in saved files, C hooks), **UI widget types** (their own pools) and **`GameKey`**
  (the hardware's keys) keep their own machinery.
- The **key registry** (`KeyRegistry`) is the interner under every registry: it maps key texts to ids for the whole
  program; a registry maps some of those ids to entries of its kind.

When a tool needs to see one of these (the terminal listing asset names, a picker of component types), the root can
list a read-only view over it; no such view exists yet.

## The registry

```c
RegistryConstructOptions Options = RegistryConstructOptions_CreateDefault(keys, ownKeyId, sizeof(MyEntry), 4096U);
Options.DeconstructElement = DeconstructMyEntry; // when an entry owns memory
Registry Things;
Registry_Construct(&Things, &Options);
uint32_t Index;
Registry_Register(&Things, keyId, &Entry, NAMESPACED_KEY_ID_NONE, &Index); // moves Entry in
const MyEntry* Stored = Registry_Get(&Things, Index);
Registry_Deconstruct(&Things); // deconstructs the active entries
```

- **One kind per registry:** elements of one fixed size (`sizeof` the element type), stored by value. A registry has
  its own key (`Registry_GetRegistryKey`, the key the root lists it under), borrows the program's key registry
  (`Services.Keys`) and has a maximum entry count, so content from other players cannot grow it without bound.
- **Stable indices.** Entries get indices 0, 1, 2, ... in registration order and keep them for the registry's life:
  slots are never removed. `Registry_Withdraw(index)` deconstructs the element and hides the entry from finds, gets and
  lists, keeping its slot and key; registering the same key again revives the same index. An index is a safe handle to
  keep (a binding, a cached lookup), and the engine's own entries can be indices 0 to N-1 in their enum's order.
  Indices exist only at runtime: files hold key texts.
- **No silent replacement.** `Registry_Register` on an active key fails with `ErrorCode_InvalidOperation`, naming the
  registry, the key and both sources. Packs override one another's definitions in the asset manager's search roots,
  before anything registers.
- **Sources.** Every entry records who registered it, as a key id: `NAMESPACED_KEY_ID_NONE` for the engine, a pack's or
  a story's key otherwise (`Registry_GetSource`, kept while withdrawn). `Registry_WithdrawSource` withdraws a source's
  entries together (a story unloading); other sources' entries and every index stay.
- **Moving elements in.** Registering copies the element's bytes, which moves it: on success the registry owns what the
  element owns, on failure the caller still does. The optional element deconstructor runs exactly once per registered
  element: on its withdrawal, or at `Registry_Deconstruct` while it is active. The registry zeroes a withdrawn element's
  bytes afterwards.
- **Stable addresses.** Elements live in blocks of 64 that never move, so `Registry_Get`'s pointer stays valid until
  the entry is withdrawn or the registry is deconstructed.
- **Reads.** `Registry_Find(keyId)` and `Registry_FindText(fullKey)` (never interns; a key written without its namespace
  goes through `NamespacedKey_Parse` first) find active entries; `Registry_FindSlot(keyId)` finds a key's slot also
  while it is withdrawn (for a user that keeps data per index, such as the input's bindings of a withdrawn action);
  `Registry_GetCount` (slots, withdrawn included),
  `Registry_GetActiveCount`, `Registry_IsActive`, `Registry_GetKey` / `Registry_GetKeyText` (also while withdrawn),
  `Registry_Get` (NULL while withdrawn). `Registry_FindByPrefix` appends the indices of the active entries whose key
  starts with a prefix, in byte order of the key text, scanning only the registry's own entries (for autocomplete:
  `mymod:` lists a mod's entries). `Registry_GetRevision` advances on every registration and withdrawal, never on
  reads, for consumers that cache data derived from the entries.
- **Lookups** go through a `WRHashMap` from key id to index: constant time on average, about 75 ns per find (two map
  calls) at -O1 with 4096 entries, and memory in proportion to the entries. That is fast enough for registration time
  and file loads; nothing looks an entry up by key per tick (keep the index).

### Typed wrappers

C has no generics, so each user wraps the functions it needs in small inline functions over its element type and its
callers never cast:

```c
static inline const MyEntry* MySystem_GetEntry(const MySystem* self, uint32_t index)
{
    return Registry_Get(&self->_entries, index);
}
```

## The root

`RegistryRoot` is a registry of borrowed `Registry*` pointers under the key `tbp:registry` (`REGISTRY_ROOT_KEY`), at
most `REGISTRY_ROOT_MAX_ENTRY_COUNT` (256) registries.
- `main.c` constructs the program's root right after the key registry and tears it down after everything that added a
  registry withdrew it, before the key registry; `Services.Registries` carries it.
- A system adds each of its registries once at startup (`RegistryRoot_Add`, under the registry's own key; a second
  registry under a taken key is refused) and withdraws it (`RegistryRoot_Withdraw`) before it deconstructs it. The root
  never owns a registry. A registry added under the key of a withdrawn one takes its old index.
- Tools find registries with `RegistryRoot_Find` / `RegistryRoot_FindText` / `RegistryRoot_Get`, and search their keys
  through `RegistryRoot_AsRegistry` (`Registry_FindByPrefix`, the key texts).
- **The root does not list itself:** its entries are exactly the systems' registries; its own key is
  `Registry_GetRegistryKey(RegistryRoot_AsRegistry(root))`.
- Systems keep direct pointers to their own registries and never look them up in the root per tick.
- At startup `main.c` logs one line with the root's registries and their active entry counts ("Registries: none." while
  nothing has added one).
- The registries in it today: the input's actions (`tbp:input_action`) and their categories (`tbp:input_category`),
  added by `GameInput_Construct` and withdrawn by `GameInput_Deconstruct` ([GameInput.md](GameInput.md)). Their elements
  are the input's own records: read them through the input's functions, which also apply overrides.

## Not built

No tags over entries (sets such as Minecraft's `#minecraft:logs`), no freezing, no network ids, no reload, and no
read-only views of the asset types, component types or `GameKey` in the root yet; each comes when something needs it.

## Pitfalls

- Every entry's key and source, and the registry's own key, are ids of the borrowed key registry; a registry added to
  the root must borrow the root's key registry.
- A registry holds key ids, so the key registry must never be truncated below them (`KeyRegistry_TruncateTo`, the
  readers' undo): a loader that interns keys while it reads a definition registers its entries only once the whole
  definition read successfully, so a refused definition gives its keys back before anything registered them.
- `Registry_GetSource` returns `NAMESPACED_KEY_ID_NONE` both for the engine and for an index the registry does not
  have: check the index against `Registry_GetCount` first.
- The element deconstructor must not call back into the registry.
- Main thread only, like the key registry.
