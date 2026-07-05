# World data layer

The rendering-independent model of a 3D world: the objects in it, its environment settings, and its
persistence (snapshot DTO + GHDF encoding). No renderer is required to build, mutate, save or tear down a
world — the world renderer (a later module) consumes these separately.

This one doc covers the whole cohesive layer; every symbol has full Doxygen in its header.

## Modules

| Module | Files | What it is |
|---|---|---|
| WorldObject | `include/WorldObject.h`, `source/WorldObject.c` | Abstract base of every world object. |
| WorldModelObject | `include/WorldModelObject.h`, `source/WorldModelObject.c` | 3D model object (references a model asset by name). |
| WorldSpriteObject | `include/WorldSpriteObject.h`, `source/WorldSpriteObject.c` | 2D sprite object (references a sprite animation by name). |
| WorldLight | `include/WorldLight.h`, `source/WorldLight.c` | Point light (color/intensity/size/shadow toggle). |
| WorldEnvironment | `include/WorldEnvironment.h`, `source/WorldEnvironment.c` | Per-world sky/sun/fog/effect settings (plain data). |
| World | `include/World.h`, `source/World.c` | Owns the objects, mints ids, holds the environment. |
| WorldDTO | `include/WorldDTO.h`, `source/WorldDTO.c` | Flat persistent snapshot + World<->DTO conversions. |
| WorldEncoder | `include/WorldEncoder.h`, `source/WorldEncoder.c` | Encodes a WorldDTO into a GHDF compound tree. |

`source/WorldShared.h` is a private header with tiny shared helpers (finite-value checks, UTF-8 string
clone); not public API.

## Object model

`WorldObject` is an abstract vtable class (`Destroy` slot) embedded as the FIRST member of each concrete
type, so any concrete pointer is a valid `WorldObject*` and the World stores them polymorphically. Shared
state: `Type` (discriminator), read-only `_id` (assigned by the World; 0 = unassigned), owned `_name`,
and the transform (`_position`, Euler `_rotation` in radians, `_scale`) plus a `RenderColor _tint`. The
transform/tint/name are reached through **validating setters** (reject NaN/inf) and inline getters.

- `WorldModelObject` / `WorldSpriteObject`: add an owned asset name (model / sprite-animation) plus two
  render toggles honored by the renderer — `HasOutline` (default true) and `OmitPixelation` (default
  false).
- `WorldLight`: adds `Color`, validated `_intensity` and `_size` (finite, >= 0), and `CastsShadows`. It
  uses the base Position/id/name; rotation/scale/tint are inherited but unused.

Create with the concrete `_Create` factories (they heap-allocate). Destroy through
`WorldObject_Destroy` (vtable). Once handed to a `World`, the world owns and destroys it.

## World

Holds a `GenericBuffer` of `WorldObject*`, a `uint64` id counter (`_nextId`, starts at 1; 0 never valid),
an owned name, and an embedded `WorldEnvironment`. `World_AddObject` assigns the next id and takes
ownership; `World_AddObjectWithId` is for the loader (explicit id, advances the counter past it).
Lookup by id is a linear scan (fine for expected counts; can be indexed later without API change).
`World_Deconstruct` destroys every contained object. Free the object buffer's `_data` with `Memory_Free`.

## Persistence

`WorldDTO` is the plain, owns-its-strings mirror of only the persistent data: world name, `NextObjectId`,
`WorldEnvironment`, and a `GenericBuffer` of `WorldObjectDTO` (a tagged struct with a per-type union).
`WorldDTO_FromWorld` snapshots a live world (clones strings, downcasts each object by `Type`);
`WorldDTO_ApplyToWorld` rebuilds live objects into an (expected-empty) world, re-adding with saved ids and
validating transforms as it goes (so a corrupt DTO is rejected).

`WorldEncoder_EncodeToCompound` turns a `WorldDTO` into a `GHDFCompound` tree (root + environment
sub-compound + array of object compounds), borrowing all compounds/arrays/strings from a caller-owned
`GHDFObjectPool`. It does **not** write bytes — turning the compound into a binary document/file
(`GHDF_Write`) is deliberately left for later. The stable numeric entry ids ARE the save schema; they live
as `WorldEncoder*Field` enums in `WorldEncoder.h` (`WORLD_ENCODER_FORMAT_VERSION` = 1). Colors are stored
as `uint8[4]` RGBA arrays, vectors as `float[3]` arrays.

Ownership note for the encoder: `GHDFCompound_SetValue` / `GHDFArray_AddValue` ADOPT the borrowed
sub-resource, so on success it is never returned separately; on any failure the whole root is returned to
the pool (which recursively frees everything already adopted).

## Status

Built and runtime-verified via a round-trip scratch test (world -> DTO -> GHDF compound with verified
fields -> back to world; ids, transforms, type-specific fields, environment, and non-finite rejection all
correct; clean teardown). Not yet wired into `main.c` (that is the next step, alongside the camera,
services bundle, bootstrap and the world renderer).
