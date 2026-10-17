# World data layer

The rendering-independent model of a 3D world: the objects in it, its environment settings, and its
persistence (snapshot DTO + GHDF encoding). No renderer is required to build, mutate, save or tear down a
world — the world renderer consumes these separately and never writes to them.

This one doc covers the whole cohesive layer; every symbol has full Doxygen in its header.

## Modules

| Module | Files | What it is |
|---|---|---|
| WorldObject | `include/WorldObject.h`, `source/WorldObject.c` | Abstract base of every world object. |
| WorldModelObject | `include/WorldModelObject.h`, `source/WorldModelObject.c` | 3D model object (references a model asset by name). |
| WorldSpriteObject | `include/WorldSpriteObject.h`, `source/WorldSpriteObject.c` | 2D sprite object (references a sprite animation by name, owns its playback instance). |
| WorldLight | `include/WorldLight.h`, `source/WorldLight.c` | Point light (color/intensity/size/shadow toggle). |
| WorldEnvironment | `include/WorldEnvironment.h`, `source/WorldEnvironment.c` | Per-world sky/sun/fog/effect settings, with validation and copying. |
| World | `include/World.h`, `source/World.c` | Owns the objects, mints ids, holds the environment and its own asset user. |
| WorldDTO | `include/WorldDTO.h`, `source/WorldDTO.c` | Flat persistent snapshot + World<->DTO conversions. |
| WorldEncoder | `include/WorldEncoder.h`, `source/WorldEncoder.c` | Encodes a WorldDTO into a GHDF compound tree. |

`source/WorldShared.h` is a private header with tiny shared helpers (finite-value checks, UTF-8 string
clone) and the two hooks the World uses to bind and rename a sprite that already belongs to it; not public API.

## Object model

`WorldObject` is an abstract vtable class (`Destroy` slot) embedded as the FIRST member of each concrete
type, so any concrete pointer is a valid `WorldObject*` and the World stores them polymorphically. Shared
state: `Type` (discriminator), read-only `_id` (assigned by the World; 0 = unassigned), owned `_name`,
and the transform (`_position`, Euler `_rotation` in radians, `_scale`) plus a `RenderColor _tint`. The
transform/tint/name are reached through **validating setters** (reject NaN/inf) and inline getters.

- `WorldModelObject`: an owned model asset name plus the render toggles `HasOutline` (default true),
  `OmitPixelation` (default false) and `ShadowTier` (`WorldShadowTier`: Both / Near / Far, default Both, the
  sun shadow cascade(s) it casts into; persisted, encoder field id 23). `WorldModelObject_SetModelAssetName`
  advances a runtime **asset revision** (`WorldModelObject_GetAssetRevision`), which is how the renderer's cache
  notices a rename without comparing names every frame.
- `WorldSpriteObject`: an owned sprite animation asset name, the same two render toggles, and an embedded
  `SpriteAnimationInstance` holding the playback state (current frame, timing, fps, running/looping) that game
  logic advances (`SpriteAnimationInstance_Update`) and the renderer only reads. The instance starts empty.
  **Binding belongs to the World** (see below): once a sprite is in a world, its own setters
  (`WorldSpriteObject_SetSpriteAnimationAssetName`, `WorldSpriteObject_SetAnimation`) refuse with
  `ErrorCode_InvalidOperation`; rename it with `World_SetSpriteAnimationAssetName`. A failed rebind leaves the
  instance empty but constructed.
- `WorldLight`: adds `Color`, validated `_intensity` and `_size` (finite, >= 0), and `CastsShadows`. It
  uses the base Position/id/name; rotation/scale/tint are inherited but unused.

Create with the concrete `_Create` factories (they heap-allocate). Destroy through
`WorldObject_Destroy` (vtable). Once handed to a `World`, the world owns and destroys it.

## World

Holds a `GenericBuffer` of `WorldObject*`, a `uint64` id counter (`_nextId`, starts at 1; 0 never valid),
an owned name, an embedded `WorldEnvironment`, and (optionally) a borrowed asset manager with its own asset user.

- **Construction:** `World_Construct1(world)` makes a world without assets (tools, tests, a headless server):
  its sprites stay empty and draw nothing. `World_Construct2(world, assets, logger)` makes a world that loads
  its sprites' animations: it mints an asset user, which `World_Deconstruct` retires after destroying every
  object (objects first, so no playback instance points into a released animation).
- **Objects:** `World_AddObject` assigns the next id and takes ownership; `World_AddObjectWithId` is for the
  loader (explicit id, advances the counter past it). Both bind a sprite's animation by its asset name under the
  world's user (replacing anything set before, and dropping event subscriptions made on the instance before the
  add, so subscribe afterwards); a missing or broken animation is logged once and leaves the sprite empty without
  failing the add (AGENTS.md rule 7). `World_SetSpriteAnimationAssetName(world, id, name)`
  renames a sprite in the world and rebinds it (playback restarts from the animation's defaults). Animations
  stay held until the world is destroyed (marked `// TODO:` in `World.c`: a release pass for long-lived edited
  worlds).
- **Change tracking:** `World_GetObjectSetVersion` changes on every addition and removal (not on property
  changes); the renderer's cache uses it to know when to re-match its entries.
- Lookup by id is a linear scan (fine for expected counts; can be indexed later without API change).
  `World_Deconstruct` destroys every contained object.

## Environment

`WorldEnvironment` is a value type with the sky, sun, day-night, star, ambient, fog and effect settings. Its field
comments state each value's rule; `WorldEnvironment_Validate` enforces them (finite, `TimeOfDay` in [0, 1],
`DayLengthSeconds` and `SunSizeMultiplier` > 0, every strength/intensity/density >= 0) and names the first broken
field. Validate anything that came from outside the program before using it. Copy with
`WorldEnvironment_CopyTo`, never by assignment, so a future owned field cannot turn copies into double frees.
`WorldEnvironment_Advance` ignores negative or non-finite steps and any step whose result would not be finite, so
nothing can leave the time of day NaN.

## Persistence

`WorldDTO` is the plain, owns-its-strings mirror of only the persistent data: world name, `NextObjectId`,
`WorldEnvironment`, and a `GenericBuffer` of `WorldObjectDTO` (a tagged struct with a per-type union).
`WorldDTO_FromWorld` snapshots a live world (clones strings, downcasts each object by `Type`, copies the
environment with `CopyTo`). `WorldDTO_ApplyToWorld` first validates the environment (a bad one rejects the whole
snapshot and leaves the world untouched), then rebuilds live objects into an (expected-empty) world, re-adding
them with their saved ids and validating transforms as it goes; sprites are bound by the world as they are added.

`WorldEncoder_EncodeToCompound` turns a `WorldDTO` into a `GHDFCompound` tree (root + environment
sub-compound + array of object compounds), borrowing all compounds/arrays/strings from a caller-owned
`GHDFObjectPool`. It does **not** write bytes. The stable numeric entry ids ARE the save schema; they live
as `WorldEncoder*Field` enums in `WorldEncoder.h` (`WORLD_ENCODER_FORMAT_VERSION` = 1). Colors are stored
as `uint8[4]` RGBA arrays, vectors as `float[3]` arrays.

Ownership note for the encoder: `GHDFCompound_SetValue` / `GHDFArray_AddValue` ADOPT the borrowed
sub-resource, so on success it is never returned separately; on any failure the whole root is returned to
the pool (which recursively frees everything already adopted).

**There is no decoder yet** (no GHDF → DTO, no stream read/write): marked `// TODO:` in `WorldEncoder.c`. The
hand-written decoder is deferred until the owner decides architecture A1/A5
(`handoff/2026-10_code_review/ARCHITECTURE_PROPOSAL.md`), because worlds may become JSON with codecs generated
from property descriptors.

## Tests

`tests/WorldTests.c` (object set, sprite binding and release, renames, guards), `tests/WorldEnvironmentTests.c`
(validation, copying, advance) and `tests/WorldDTOTests.c` (World → DTO → World round trip of every persisted
field, bad-environment rejection). They run headless with `sh tests/run_linux.sh`; `tests/TestAssets.h`
provides code-built sprite animation and model definitions that load without files or a GL context.
