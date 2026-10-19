# World data layer

The rendering-independent model of a 3D world: its objects, their components, their tags and data, and its
environment settings. No renderer is required to build, change or tear down a world; the world renderer reads it
separately and never writes to it. Saving and loading worlds comes back with T09 (world files), built on the property
tables. Tags, typed object data and queries (T24) have their own doc: [ObjectData.md](ObjectData.md).

This one doc covers the whole cohesive layer; every symbol has full Doxygen in its header.

## Modules

| Module | Files | What it is |
|---|---|---|
| WorldObject | `include/WorldObject.h`, `source/WorldObject.c` | The one object type: id, name, transform, tint, enabled, visible, mobility, and its component slots. |
| WorldComponent | `include/WorldComponent.h`, `source/WorldComponent.c` | The component base, the component types and the registry entry that describes each (`WorldComponentTypeInfo`). |
| WorldModelComponent | `include/WorldModelComponent.h`, `source/WorldModelComponent.c` | A 3D model by asset name, with its render settings (outline, crisp, shadow tier, casts shadows). |
| WorldSpriteComponent | `include/WorldSpriteComponent.h`, `source/WorldSpriteComponent.c` | A sprite animation by asset name, with its playback instance, bound by the world. |
| WorldLightComponent | `include/WorldLightComponent.h`, `source/WorldLightComponent.c` | A point light (colour, intensity, range, casts shadows). |
| WorldEnvironment | `include/WorldEnvironment.h`, `source/WorldEnvironment.c` | Per-world sky/sun/moon/fog/effect settings, described by a property table (defaults, validation, copying, JSON, GHDF), and the sky's model: the sun, the moon, the stars' rotation, the two lights. |
| World | `include/World.h`, `source/World.c`, `source/WorldData.c` (tags and data, the tag index) | Owns the objects and their components, mints ids, keeps the id map and the per-type component arrays, borrows the key registry, keeps the tag index and the data byte count, holds the environment and its own asset user. |
| WorldQuery | `include/WorldQuery.h`, `source/WorldQuery.c` | Searches over the objects by tags, data, components, flags and place ([ObjectData.md](ObjectData.md)). |

`source/WorldInternal.h` is the layer's private header: the object's construction (objects are made by the world only),
the object lookup (`WorldInternal_FindObject`), `WorldInternal_ComponentAfterSet` (the hook every component table calls
after a change), the data bookkeeping (`WorldInternal_SyncObjectData` after a table write replaced an object's data,
`_ForgetObjectData` before removal, `_GetTagMembers` for queries), the world's asset manager, user and logger for
components that load assets, small helpers, and `WorldInternal_ConstructWithTypes` (a test seam, below). Not public
API.

## Objects

`WorldObject` is one concrete struct (T05 replaced the three subclasses: model, sprite and light objects are now
components). Every object has:

- `_id` (unique in its world, never 0, assigned at creation, never changed), `_name` (owned, may be NULL, at most
  `WORLD_OBJECT_NAME_MAX_LENGTH` = 256 bytes of valid UTF-8);
- the transform: `_position`, `_rotation` (Euler XYZ in radians, degrees in JSON), `_scale` (they become local
  transforms with parenting, T08);
- `_tint`: a `RenderColor` (brightness and opacity in [0, 1]; drawing the opacity is T07's);
- `_isEnabled` (default true): false makes **every system** skip the object: the renderer, later collision and
  scripts' searches;
- `_isVisible` (default true): false makes **the renderer** skip it (its model, its sprite and its light); it still
  exists for collision and interaction (invisible walls, hidden triggers);
- `_mobility`: `WorldMobility_Static` (default: scenery that does not move in play) or `WorldMobility_Movable`
  (see "Ready for things that move");
- `_revision`: advances on every property change (runtime only), except the tags and data;
- `_data`: its tags and custom data, a `DataMap` (NULL while empty; `WorldObject_GetData`, `WorldObject_HasTag`),
  changed only through the world's data functions ([ObjectData.md](ObjectData.md)); `_dataRevision` advances on every
  change of them (`WorldObject_GetDataRevision`), so script data never makes the renderer recompute anything;
  `_countedDataBytes` and `_indexedTagCount` are the world's bookkeeping;
- `_components[WORLD_MAX_COMPONENT_TYPES]`: one slot per component type, NULL when absent
  (`WorldObject_GetComponent`);
- `_world`: the world that owns it. **A World must never be moved or copied once constructed** (objects and
  components point back to it).

Every field is reached through getters and validating setters (`WorldObject_SetPosition`, `_SetEnabled`, ...). The
setters check through the object's property table (`Properties_CheckValue`), so the rules live in the table only, and
`Properties_Set` / the JSON and GHDF readers / `Properties_CopyTo` work on objects too; either way the revision
advances (the table's `AfterSet`). The `data` row is `ReadOnly` for `Properties_Set`; a read or a copy (whose
`AfterSet` gets no row) advances both revisions and has the world recount the object's bytes and re-index its tags.

## Components

A component is a struct that embeds `WorldComponent` first (`_type`, `_owner`, `_revision`). An object carries **at
most one component of each type**; a street lamp is one object with a model and a light.

| Type | Value | Name | Struct |
|---|---|---|---|
| `WorldComponentType_Model` | 0 | `model` | `WorldModelComponent` |
| `WorldComponentType_Sprite` | 1 | `sprite` | `WorldSpriteComponent` |
| `WorldComponentType_Light` | 2 | `light` | `WorldLightComponent` |

The values are explicit and never renumbered (world files will save them). `WorldComponentType_Count` is the number
of standard types; `WORLD_MAX_COMPONENT_TYPES` (16) is how many a world can hold, room for the types later tasks add
(collider T12, character T13, ambient zone T14, screen T19) and for test-only types.

**The registry.** Each type has one `WorldComponentTypeInfo` (a static constant of its module, handed out by
`Xxx_GetTypeInfo`; `WorldComponent_GetStandardTypeInfo(type)` maps the standard types): `Type`, `Name` (the key of its
block in world files and its label in the editor), `Size`, `Table` (its property table, whose `AfterSet` must be
`WorldInternal_ComponentAfterSet`), `IsTransformDriver`, and the hooks `Construct` (must call `Properties_Construct`,
which zeroes the whole struct, then set up runtime state), `Deconstruct`, `OnAttach(world, object, component)`,
`OnDetach` and `OnPropertySet(world, object, component, descriptor)` (descriptor NULL after a read or a copy).
`WorldComponentTypeInfo_Check` verifies an entry (a test runs it for every standard type). **A new component type is
one enum value, one registry entry (and its case in `WorldComponent_GetStandardTypeInfo`), a table and the hooks.**

**Lifetime.** Components exist only inside a world: `World_AddComponent` allocates the type's `Size`, constructs it,
sets the base, appends it to the world's array of that type, puts it in the object's slot, then calls `OnAttach`.
`World_RemoveComponent`, removing the object, or destroying the world calls `OnDetach`, then removes and destroys it.

**Changes.** Every property change advances the component's revision (`WorldComponent_GetRevision`), from the typed
setters and from every table write alike: they all end in `WorldInternal_ComponentAfterSet`, which advances the
revision and then calls the type's `OnPropertySet` with the world. That is how a component keeps runtime state in step
with its properties, whichever way they were written.

**Downcasts.** `WorldModelComponent_FromComponent(component)` (and the sprite's and light's) returns the concrete
struct, or NULL for NULL or another type; `..._AsComponent` upcasts.

### Model component (`model_component`)
`_model` (an asset name, NULL draws nothing), `_hasOutline` (true), `_omitPixelation` (false: drawn crisp at full
resolution over the pixelated image when true), `_shadowTier` (`WorldShadowTier`: Both / Near / Far, the shadow
cascade(s) it casts into) and `_castsShadows` (true; false skips it in every shadow pass, it still receives shadows).
The component holds no asset: the renderer resolves the model by name and resolves it again when the name changes
(it watches the revision, then compares the name).

### Sprite component (`sprite_component`)
`_spriteAnimation` (an asset name, NULL plays nothing), `_hasOutline` (true), `_omitPixelation` (false), and the
runtime `_animationInstance` (`SpriteAnimationInstance` by value: the playback state game logic advances and the
renderer reads; `WorldSpriteComponent_GetAnimationInstance`).

**Binding.** The playing animation always matches the name. `OnAttach` binds it, and `OnPropertySet` binds it again
whenever the name differs from the one it was bound for (`_boundAnimation`, an owned copy): after
`WorldSpriteComponent_SetSpriteAnimation`, `Properties_Set`, a JSON or GHDF read, or `Properties_CopyTo`. Binding loads
the animation under the world's asset user and restarts the instance from the animation's defaults (dropping its event
subscriptions: subscribe after setting the name). Setting the name it already has changes nothing (playback goes on).
A world without an asset manager, a NULL name or a failed load leaves the instance empty (it draws nothing); a failure
is logged once, when it happens (`World: the sprite of object N ("name") has no animation: ...`). The animations stay
held until the world is destroyed, after every component (asset holds are not counted, so releasing one on a rename
could unload it under another sprite; a `// TODO:` in `source/WorldSpriteComponent.c` marks the release pass that
long-lived edited worlds may need).

### Light component (`light_component`)
`_color` (white), `_intensity` (>= 0, default 1), `_range` (>= 0, default 1: how far the light reaches; T05 renamed it
from `Size`), `_castsShadows` (false). It uses its object's position; rotation, scale and tint do not affect it. A
hidden or disabled object's light is off.

## World

Holds the objects in creation order (`_objects`) with an id -> object map (`_objectsById`, a `WRHashMap`), the
registry entries of its component types, one dense array of components per type (`_components[type]`, in add order),
the id counter (`_nextId`, starts at 1; 0 once every id was handed out), two versions, an owned name, an embedded
`WorldEnvironment`, the borrowed key registry (`World_GetKeys`), the tag index and data byte count
([ObjectData.md](ObjectData.md)), the query scratch, and (optionally) a borrowed asset manager with its own asset user.

- **Construction:** every world borrows the program's key registry ([NamespacedKey.md](NamespacedKey.md);
  `Services.Keys`). `World_Construct1(world, keys)` makes a world without assets (tools, tests, a headless server): its
  sprites stay empty. `World_Construct2(world, keys, assets, logger)` makes a world whose components load their assets
  (the sprites' animations) under its own asset user, which `World_Deconstruct` retires after destroying every object
  and component. The logger also hears of each data limit once.
- **Objects:** `World_CreateObject(world, name, &object)` (the next id) and `World_CreateObjectWithId(world, name, id,
  &object)` (the loader's version: refuses 0 and used ids, and advances the counter past the id); `World_RemoveObject
  (world, id)` (its components first, then the object; ordered removal, so the other objects keep their order);
  `World_TryGetObject(world, id, &object)` (a hash lookup); `World_GetObjectCount` / `World_GetObjectByIndex` for
  iteration in creation order. `World_EnsureNextObjectIdAtLeast` and `World_GetNextObjectId` are for world files.
- **Components:** `World_AddComponent(world, id, type, &component)`, `World_RemoveComponent(world, id, type)`,
  `World_TryGetComponent(world, id, type, &component)`, and **`World_GetComponents(world, type, &count)`**: the world's
  own dense array of every component of a type, in add order (removal keeps the order), valid until a component of that
  type is added or removed. Systems iterate these instead of walking objects (the renderer's cache does); they still
  check each owner's flags.
- **Component types:** `World_GetComponentTypeCount`, `World_GetComponentTypeInfo(world, type)` and
  `World_TryFindComponentType(world, name, &type)` (world files and the editor name types).
- **Change tracking:** `World_GetObjectSetVersion` changes on every object creation and removal,
  `World_GetComponentSetVersion` on every component addition and removal (also with its object); property changes
  advance the object's and the component's own revisions instead, and data changes the object's data revision.
- **Tags and data:** `World_SetData`, `World_TryGetData`, `World_RemoveData`, `World_AddTag`, `World_RemoveTag`,
  `World_HasTag`, `World_GetDataByteSize`, with the limits `WORLD_OBJECT_DATA_MAX_ENTRIES` (256),
  `WORLD_OBJECT_DATA_MAX_BYTES` (64 KiB) and `WORLD_DATA_MAX_BYTES` (16 MiB). **Queries:** `World_Query`,
  `World_DoesObjectMatch` (`WorldQuery.h`). Both in [ObjectData.md](ObjectData.md).
- **The test seam:** `WorldInternal_ConstructWithTypes` (private header) builds a world whose component types are a
  given list: the standard ones first, as their own entries, then test-only types. `tests/WorldTests.c` adds two
  transform drivers that way to test the one-driver rule before the game has a real driver.

## Ready for things that move

The game will have objects that move and react to each other: the player and NPCs (a `character` component, T13),
cars (a `vehicle` component, later), physics objects (a `rigid_body` component, later, decision G13). None is built
yet, but the model is ready for them:

- **One driver per object.** A component type flagged `IsTransformDriver` writes its object's transform every tick.
  `World_AddComponent` refuses a second driver on the same object (`ErrorCode_InvalidOperation`), so two systems never
  fight over one transform. Adding a driver makes the object movable. A driven object is normally a root object; with
  a parent (T08) the driver writes the local transform through the parent's inverse.
- **Mobility is a hint for performance, never a lock.** Collision (T12) keeps static colliders in a structure built
  once and movable ones in one updated every tick; the renderer can later cache static shadows. Moving a static object
  still works (the editor does it constantly): it costs a rebuild of whatever cached it, and in play the system that
  rebuilds logs one warning per object. A driven object may even be set back to static; it still moves, at that cost.
  T05 has no such cache yet, so nothing warns yet: T12's static grid is the first (it sees the move through the
  object's revision).
- **The player is an ordinary object** with a character component. NPCs, triggers, vehicles and physics then all
  treat it like any other object; no frame special-cases it.
- **The tick order.** Each game tick runs the systems that change a world in this order, so each later system knows
  its slot:
  1. input (`GameInput`, polled by the frame manager);
  2. scripts (later);
  3. transform drivers: characters, vehicles, physics (later);
  4. collision sync (T12);
  5. queued events: trigger enter and exit, later contacts (T12; the queued-event rule, architecture A4);
  6. animation (today: sprite playback, `AdvanceSpriteAnimations` in the test frame, enabled objects only);
  7. the renderer, which only reads (AGENTS.md rule 3).

## Property tables

Every object and component property is one row of its type's table ([PropertyDescriptors.md](PropertyDescriptors.md));
a test checks every table. Wire ids are the save schema: never renumber or reuse one.

| Table | Key | Wire | Type | Default |
|---|---|---|---|---|
| `world_object` | `name` | 3 | String (256, null allowed) | null |
| | `position` | 4 | Vector3 | (0, 0, 0) |
| | `rotation` | 5 | AngleVector3 (degrees in JSON) | (0, 0, 0) |
| | `scale` | 6 | Vector3 | (1, 1, 1) |
| | `tint` | 7 | RenderColor | white |
| | `is_enabled` | 8 | Bool | true |
| | `is_visible` | 9 | Bool | true |
| | `mobility` | 10 | Enum `static` / `movable` | `static` |
| | `data` (JSON: `data` and `tags`) | 11 | DataMap, ReadOnly for `Properties_Set`; 256 entries, 64 KiB | empty (NULL) |
| `model_component` | `model` | 1 | AssetName (128, null allowed) | null |
| | `has_outline` | 2 | Bool | true |
| | `omit_pixelation` | 3 | Bool | false |
| | `shadow_tier` | 4 | Enum `both` / `near` / `far` | `both` |
| | `casts_shadows` | 5 | Bool | true |
| `sprite_component` | `sprite_animation` | 1 | AssetName (128, null allowed) | null |
| | `has_outline` | 2 | Bool | true |
| | `omit_pixelation` | 3 | Bool | false |
| `light_component` | `color` | 1 | Color | white |
| | `intensity` | 2 | Float >= 0 | 1 |
| | `range` | 3 | Float >= 0 | 1 |
| | `casts_shadows` | 4 | Bool | false |

The object table's wire ids start at 3: 1 and 2 are left for the world file's own entries per object (T09: the id and
the components). T24 added `data` (one row: the tags and the values are one map, written as two JSON keys, `tags` and
`data`, and as one GHDF entry); T08's `parent` takes the next free id, 12.

## Environment

`WorldEnvironment` is a struct with the date and time of day, sky, sun, night sky, moon, ambient, fog and effect
settings.
**Every field is one row of its property table** (`WorldEnvironment_GetPropertyTable`, in `source/WorldEnvironment.c`; module doc
[PropertyDescriptors.md](PropertyDescriptors.md)): the key (the field name in snake_case, e.g. `light_pollution`), the
GHDF wire id, the type, the default, the rules, the inspector text. `WorldEnvironment_SetDefaults`, `_Validate`,
`_CopyTo` and `_Deconstruct` are thin wrappers over the table, and `WorldEnvironment_ReadJSON` / `_WriteJSON` are the
JSON entry points for the world files (T09). **To add a setting, add the field and one table row** with a new,
never-used wire id (the next free one is 46; 2 and 3 are retired); nothing else needs writing. The field comments state
each value's rule; validation enforces them (finite, `TimeOfDay`, `LightPollution`, `StarTwinkle`, `MoonPhase`,
`NightVisionStrength` and `NightVisionBlueShift` in [0, 1], `SunSizeMultiplier`, `MoonSizeMultiplier` and `MoonCycleDays` > 0, every strength/intensity/density/brightness
>= 0, `Date` inside the calendar, the texture names valid asset names of at most 128 bytes, `MoonPhaseSource` one of
its names) and names the first broken field by key and C name: `world_environment.light_pollution (LightPollution)
must be finite and in [0, 1].` `SunAngle` and `MoonOrbitTilt` are `Angle`s: radians in memory and GHDF, degrees in
JSON.

**Date and time of day are what the sky shows, not a clock.** `TimeOfDay` (a fraction of the day) and `Date` (a day
number of [GameCalendar.md](GameCalendar.md), a `Date` property: `"2026-10-18"` in JSON, default 2026-10-18 like a new
game clock) never advance by themselves. While a game runs, the game clock ([GameClock.md](GameClock.md)) writes both
every tick; in a world file they are where the sky starts (the editor's preview uses them, and the test frame starts
its clock from them). T23 removed the old day-night timer (`is_day_night_cycle_enabled`, `day_length_seconds` and
`WorldEnvironment_Advance`; the clock's pause and time scale replace them). Their wire ids 2 and 3 are retired, so GHDF
written before still reads (the entries are skipped). The table stayed at version 1: no environment JSON had been
written yet (world files come with T09), so no migration is needed.

**The environment owns its texture names** (`MilkyWayTexture`, `MoonTexture`; T03): `WorldEnvironment_SetDefaults`
constructs and allocates them (call it on a new environment only; to reset one, `Deconstruct` first) and pairs with
`WorldEnvironment_Deconstruct`; copies go through `CopyTo` (never assignment: a copied pointer would be freed twice);
a name is changed through the table (`Properties_Set`), which clones the new one and frees the old. `World` pairs
them.

The night sky settings: `StarSeed` (which stars), `StarDensity` (1 = an Earth-like sky of about 9000 stars, the
default; 0 none; at most 20000 stars), `StarBrightness`, `StarTwinkle` (0..1, default 0.25), `MilkyWayBrightness`
(default 1), `MilkyWayTexture` (default `sky_milky_way`), `LightPollution` (0 = a dark site .. 1 = a city centre,
default 0.35, a small town) and `LightPollutionColor` (sodium orange by default).

The moon settings (T03; the owner's rule: as many settings for story creators as possible):

| Key | Rule | Default | Meaning |
|---|---|---|---|
| `is_moon_enabled` | bool | true | false: no moon at all (no disc, moonlight or moonlit sky). |
| `moon_phase_source` | `calendar` / `fixed` | `calendar` | The phase from the date, or always `moon_phase` (a story pins a full moon). |
| `moon_phase` | [0, 1] | 0.5 | The fixed phase: 0 new, 0.25 first quarter, 0.5 full, 0.75 last quarter. |
| `moon_phase_offset_days` | finite | 0 | Shifts the calendar phase (7.4 days is a quarter). |
| `moon_cycle_days` | > 0 | 29.530588 | One cycle of phases. |
| `moon_orbit_tilt` | finite (an angle) | 0.09 rad (5.2 degrees) | How far the moon's path strays from the sun's. |
| `moon_size_multiplier` | > 0 | 5 | The disc x the real 0.52 degrees (5: about 16 pixels at 640x360). |
| `moon_brightness` | >= 0 | 1 | Multiplies the disc's brightness in the sky. |
| `moon_intensity` | >= 0 | 0.02 | The full moon's light relative to the sun's (`WORLD_ENVIRONMENT_DEFAULT_MOON_INTENSITY`). |
| `moon_color` | colour | (200, 214, 255) | The moonlight's and the disc's colour. |
| `moon_texture` | asset name | `sky_moon` | The moon's face. |

And `are_seasons_enabled` (default false): the sun's declination follows the date (23.44 degrees x the sine of its
place in the year, 0 at the March equinox; a uniform year, so the September equinox falls about two days early), so
with `SunAngle` > 0 summer days are longer and the noon sun higher, and the moon follows the ecliptic (a full moon low
in summer, high in winter). Off by default: it changes how much daylight the working hours get.

The night vision settings (the eye's dim-light vision, [WorldRuntime.md](WorldRuntime.md), "Night vision"):
`night_vision_strength` ([0, 1], default 1: the eye; 0 keeps every colour, a long-exposure photo's night) and
`night_vision_blue_shift` ([0, 1], default 0.5: how blue the rods' grey is, from a neutral grey to the night blue
painters use). Neither changes anything by day.

**The sky's model** (pure functions; the renderer only draws their results, [WorldRuntime.md](WorldRuntime.md), "Moon"
and "Night sky"):
- `WorldEnvironment_GetSunDirection`, `_GetDaylightFactor` (with the seasons when on).
- `WorldEnvironment_GetCelestialToWorld`: the sun's frame for the time of day (the sun at (1,0,0), at (cos d, 0,
  -sin d) with the seasons; the pole above the horizon stays at world (0, sin SunAngle, -cos SunAngle)).
- `WorldEnvironment_GetStarsToWorld`: the stars' frame, the celestial frame of the June solstice turned ahead of the
  sun's by its progress through the tropical year (about 0.99 degrees a day, continuous): the stars rise about 4
  minutes earlier each day.
- `WorldEnvironment_GetMoonPhase` (from the date: `frac((Date + TimeOfDay - WORLD_ENVIRONMENT_MOON_EPOCH_DAY +
  offset) / cycle)` in double, the epoch being the real new moon of 2000-01-06 18:14 UTC, so with the defaults the
  phase is the real moon's within about a day; or fixed), `_GetMoonDirection` (east of the sun by 2pi x phase, the tilt
  term north of it), `_GetMoonIllumination` ((1 - dot(sun, moon)) / 2), `_GetMoonBrightness` (the total light for an
  illumination: the opposition-surge fit of Krisciunas and Schaefer, a half moon gives about a tenth of the full
  moon's) and `_GetMoonlightFactor` (brightness x its height x (1 - daylight); 0 without a moon).
- `WorldEnvironment_GetSkyLights`: both lights at once (directions, intensities, the moon's phase and illumination)
  and the shadow light: the brighter by luminance once it stands high enough, with a fade (in from 0.05 to 0.15 of
  elevation, out around the hand-over between the two and for a light too dim to show a shadow), so the hand-over from
  the sun to the moon never jumps.

Validate anything that came from outside the program before using it.

## Persistence

**Nothing saves worlds at runtime yet.** T05 removed the old snapshot (`WorldDTO`) and its GHDF encoder
(`WorldEncoder`) together with their tests: they were written for the three object subclasses, only tests used them,
and the property tables now describe every object and component. T09 (world files) rebuilds JSON and GHDF loading and
saving on the tables, with the root ids the old encoder used (format version 1, name 2, next id 3, environment 4,
objects 5). Reading objects with tags and data needs `PropertyReadOptions.Keys` (the world's registry,
`World_GetKeys`) and `DefaultNamespace` (the story's namespace); after reading the objects, the loader refuses a world
whose `World_GetDataByteSize` passes `WORLD_DATA_MAX_BYTES` (the table readers count it but cannot refuse it). The environment's table already reads and writes itself (`WorldEnvironment_ReadJSON` / `_WriteJSON`,
`Properties_ReadGHDF` / `_WriteGHDF`), and `tests/PropertyDescriptorTests.c` still compares its GHDF tree with a
literal copy of what the old encoder wrote.

## Tests

`tests/ObjectDataTests.c` covers tags, data and queries ([ObjectData.md](ObjectData.md)); `tests/TestWorld.h` builds a
world with its own key registry for tests. `tests/WorldTests.c`: objects (ids unique and never 0, loader ids advance the counter, an exhausted counter, the
object-set version, validated setters and table writes advancing the revision, rotation in degrees in JSON),
components (add, find, remove; one per type; removing an object removes its components; the per-type arrays stay in
order across interleaved adds and removes; types by name; revisions), the sprite binding (no assets, a shared
animation released with the world last, a missing animation logged once, renaming, the same name keeps playing, table
writes rebind), flags, and mobility and drivers (through two test-only driver types: adding one makes the object
movable, a second is refused; the test seam refuses impostors). `tests/WorldEnvironmentTests.c` (validation including
the moon settings and texture names, copying, the date and time the clock writes, the retired ids, the celestial frame,
the moon's phase against real dates, its motion and lit fraction, the moonlight, the stars' drift and its direction, the
shadow light's hand-over, the seasons' day lengths). `tests/PropertyDescriptorTests.c` (every table passes its check,
the environment's GHDF tree matches the old encoder's, its JSON round trip). `tests/WorldRenderCacheTests.c` covers the
renderer's side ([WorldRuntime.md](WorldRuntime.md)). They run headless with `sh tests/run_linux.sh`;
`tests/TestAssets.h` provides code-built sprite animation and model definitions that load without files or a GL
context.
