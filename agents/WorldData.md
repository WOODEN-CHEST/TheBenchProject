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
| WorldEnvironment | `include/WorldEnvironment.h`, `source/WorldEnvironment.c` | Per-world sky/sun/moon/fog/effect settings, described by a property table (defaults, validation, copying, JSON, GHDF), and the sky's model: the sun, the moon, the stars' rotation, the two lights. |
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

`WorldEnvironment` is a struct with the date and time of day, sky, sun, night sky, moon, ambient, fog and effect
settings.
**Every field is one row of its property table** (`WorldEnvironment_GetPropertyTable`, in `source/WorldEnvironment.c`; module doc
[PropertyDescriptors.md](PropertyDescriptors.md)): the key (the field name in snake_case, e.g. `light_pollution`), the
GHDF wire id, the type, the default, the rules, the inspector text. `WorldEnvironment_SetDefaults`, `_Validate`,
`_CopyTo` and `_Deconstruct` are thin wrappers over the table, and `WorldEnvironment_ReadJSON` / `_WriteJSON` are the
JSON entry points for the world files (T09). **To add a setting, add the field and one table row** with a new,
never-used wire id (the next free one is 44; 2 and 3 are retired); nothing else needs writing. The field comments state
each value's rule; validation enforces them (finite, `TimeOfDay`, `LightPollution`, `StarTwinkle` and `MoonPhase` in
[0, 1], `SunSizeMultiplier`, `MoonSizeMultiplier` and `MoonCycleDays` > 0, every strength/intensity/density/brightness
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
a name is changed through the table (`Properties_Set`), which clones the new one and frees the old. `World` and
`WorldDTO` pair them.

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

`WorldDTO` is the plain, owns-its-strings mirror of only the persistent data: world name, `NextObjectId`,
`WorldEnvironment`, and a `GenericBuffer` of `WorldObjectDTO` (a tagged struct with a per-type union).
`WorldDTO_FromWorld` snapshots a live world (clones strings, downcasts each object by `Type`, copies the
environment with `CopyTo`). `WorldDTO_ApplyToWorld` first validates the environment (a bad one rejects the whole
snapshot and leaves the world untouched), then rebuilds live objects into an (expected-empty) world, re-adding
them with their saved ids and validating transforms as it goes; sprites are bound by the world as they are added.

`WorldEncoder_EncodeToCompound` turns a `WorldDTO` into a `GHDFCompound` tree (root + environment
sub-compound + array of object compounds), borrowing all compounds/arrays/strings from a caller-owned
`GHDFObjectPool`. It does **not** write bytes. The stable numeric entry ids ARE the save schema: the root and object
ids are `WorldEncoder*Field` enums in `WorldEncoder.h` (`WORLD_ENCODER_FORMAT_VERSION` = 1); the environment compound
is written by `Properties_WriteGHDF` from the environment's table, every setting including defaults, under the table's
wire ids (1 and 4-43: the entries the hand-written encoder wrote, without the retired 2 and 3, plus T23's date as an
encoded day number and T03's moon, seasons and texture-name settings, ids 31-43; a test compares the old entries'
trees, another round-trips them all). Colors are stored as `uint8[4]` RGBA arrays, vectors as `float[3]`
arrays.

Ownership note for the encoder: `GHDFCompound_SetValue` / `GHDFArray_AddValue` ADOPT the borrowed
sub-resource, so on success it is never returned separately; on any failure the whole root is returned to
the pool (which recursively frees everything already adopted).

**There is no world decoder yet** (no GHDF → DTO, no stream read/write): marked `// TODO:` in `WorldEncoder.c`. The
environment compound already reads back with `Properties_ReadGHDF(WorldEnvironment_GetPropertyTable(), ...)`; the
objects move onto property tables in T05, and the world files (JSON and GHDF load and save) come with T09.

## Tests

`tests/WorldTests.c` (object set, sprite binding and release, renames, guards), `tests/WorldEnvironmentTests.c`
(validation including the moon settings and texture names, copying, the date and time the clock writes, the retired
ids, the celestial frame, the moon's phase against real dates, its motion and lit fraction, the moonlight, the stars'
drift and its direction, the shadow light's hand-over, the seasons' day lengths), `tests/WorldDTOTests.c` (World → DTO → World round trip of every persisted
field, bad-environment rejection) and `tests/PropertyDescriptorTests.c` (the environment's table passes its check, its
GHDF tree matches the old encoder's, its JSON round trip). They run headless with `sh tests/run_linux.sh`; `tests/TestAssets.h`
provides code-built sprite animation and model definitions that load without files or a GL context.
