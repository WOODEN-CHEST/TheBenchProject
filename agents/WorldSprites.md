# World sprites

`include/WorldSprite.h`, `source/WorldSprite.c` (the asset's data: the enums' names, the settings' two property tables
and their defaults) and `source/WorldSpriteDefinition.c` (the `world_sprite` asset type: the definition's parser and the
loader). The private `source/WorldSpriteInternal.h` holds the property rows the world sprite's tables and the sprite
component's table share. The placements' side is the sprite component, `include/WorldSpriteComponent.h` and
`source/WorldSpriteComponent.c` ([WorldData.md](WorldData.md), "Sprite component"). The definition format is in
`references/asset_structure.md`, "World sprites". Tests: `tests/WorldSpriteTests.c`, the renderer's side in
`tests/WorldRenderCacheTests.c` and `tests/WorldVisibilityTests.c`. World sprites are the sprite counterpart of world
models ([WorldModels.md](WorldModels.md)).

A sprite animation is a plain asset that text uses too, where world settings mean nothing. A **world sprite** holds
what only matters in the world: which animation to play, how its quad stands (orientation, pivot, pixels per unit,
flips), its transparency, how it is lit, whether it is a decal, its render flags and how far it is seen. A **world
object** is one placement: it names its world sprite and may override **every** setting (the principle: the more
settings creators get, the better).

## The asset type

- **A standard type** (the ninth), registered by `AssetManager_CreateStandardAssetTypes`: name `world_sprite`, directory
  `world_sprites/`, JSON definitions; `StandardAssetTypes.WorldSprite` and `AssetManager_LoadWorldSprite`
  ([AssetManager.md](AssetManager.md)).
- **Definitions are checked when they are read** (a bad file is skipped at startup with one error): `name` and
  `sprite_animation` (a valid asset name) are required, every other key is optional and gets its default. **Unknown
  keys are refused** in the root and in `render` (`AssetJSON_RejectUnknownKeys`: `Unknown key "x" in world sprite
  "tree", render; the keys allowed there are: ...`, a key from a file shown as printable ASCII). Every other message
  starts with `World sprite "<name>": `.
- **The settings are read through property tables**, not hand-written code: `WorldSprite_GetSettingsPropertyTable`
  (`world_sprite`, the root's 15 rows, over `WorldSpriteSettings`) and `WorldSprite_GetRenderPropertyTable`
  (`world_sprite_render`, the `render` block's three flags and its `haze` multiplier, over
  `WorldSpriteRenderSettings`). Together they describe
  every field of `WorldSpriteSettings`, so copying, comparing, validating and writing go through both. A value that breaks a rule
  is refused with the table's message: `World sprite "tree": world_sprite.pixels_per_unit (PixelsPerUnit) must be
  finite and > 0.`
- **Loading** (`WorldSpriteDefinition_LoadAsset`): the sprite animation loads as a **dependency**, under the world
  sprite's dependency user, so holding the world sprite holds it and it unloads right after it. A missing animation
  fails the world sprite and names it (`World sprite "broken": sprite animation "nowhere": No definition named
  "nowhere" ...`).

## One copy of the rules

The world sprite's tables and the sprite component's table are built from the same rows: `WorldSpriteInternal.h` holds
one macro per setting with its key, type, default, rules, enum table, flags, category, display name and description,
and each table adds its wire id, its member and its `SinceVersion`:

```c
{ WORLD_SPRITE_ROW_PIVOT, .WireId = 5U, PROPERTY_MEMBER(WorldSpriteComponent, _pivot), .SinceVersion = 1U },
```

So an asset's value and a placement's own value follow the same rules, and a new rule changes both. Both tables give a
setting the same wire id (world sprites are JSON only today). The one row of the asset alone is `has_draw_distance` (a
Bool, off by default, right before `draw_distance`; wire 32, where the component has its `overrides_draw_distance`).
The test `the_definition_shares_the_components_rows` checks every row pair. The enums' names are public (`WORLD_SPRITE_ORIENTATION_ENUM`, `WORLD_SPRITE_ALPHA_MODE_ENUM`),
as the shadow tier's are for world models.

## The loaded `WorldSprite`

Pure data, read-only, owned by the asset manager (never keep it past your asset user): `WorldSprite_GetAnimation` (the
`SpriteAnimation*`, borrowed from the dependency) and `WorldSprite_GetSettings` (a `WorldSpriteSettings`: the defaults of
its placements). `WorldSpriteSettings_CreateDefault` gives the tables' defaults (a centred, camera-facing, lit
cutout sprite at 32 pixels per metre, outlined, casting shadows) and no draw distance of its own.
`WorldSpriteSettings_IsDrawnAsDecal` (marked a decal and fixed) and `WorldSpriteSettings_GetDefaultDrawDistance` (the
kind's) work on any settings.

## Placements: the sprite component's overrides

The sprite component names the world sprite (`world_sprite`; changing the name swaps the sprite and keeps the object,
its tags, data, children and overrides). Every setting of `WorldSpriteSetting` is a pair of rows: the flag
`overrides_<key>` (a Bool, off by default) right before the placement's own value `<key>`; while the flag is off the
world sprite's value applies and the placement's value is kept and saved (no sentinels, no
`inherit`). In C the flags are one array, `_overrides[WorldSpriteSetting_Count]`, read and set with
`WorldSpriteComponent_IsOverridden` / `_SetOverridden(self, setting, isOverridden)`; the values have typed
getters and setters, which read and write the placement's own value.

| Setting (`WorldSpriteSetting`) | Flag | Value |
|---|---|---|
| `Orientation` | `overrides_orientation` | `orientation` |
| `Pivot` | `overrides_pivot` | `pivot` |
| `PixelsPerUnit` | `overrides_pixels_per_unit` | `pixels_per_unit` |
| `FlipX` | `overrides_flip_x` | `flip_x` |
| `FlipY` | `overrides_flip_y` | `flip_y` |
| `AlphaMode` | `overrides_alpha_mode` | `alpha_mode` |
| `AlphaCutoff` | `overrides_alpha_cutoff` | `alpha_cutoff` |
| `IsLit` | `overrides_is_lit` | `is_lit` |
| `Emission` | `overrides_emission` | `emission` |
| `LightWrap` | `overrides_light_wrap` | `light_wrap` |
| `CastsShadows` | `overrides_casts_shadows` | `casts_shadows` |
| `IsDecal` | `overrides_is_decal` | `is_decal` |
| `DecalOffset` | `overrides_decal_offset` | `decal_offset` |
| `HasOutline` | `overrides_has_outline` | `has_outline` |
| `OmitPixelation` | `overrides_omit_pixelation` | `omit_pixelation` |
| `Haze` | `overrides_haze` | `haze` |
| `DrawDistance` | `overrides_draw_distance` | `draw_distance` |
| `IsDrawDistanceFixed` | `overrides_is_draw_distance_fixed` | `is_draw_distance_fixed` |

`haze` ([0, 1000], default the world sprite's `render.haze`, 1) multiplies the haze in front of the sprite
([WorldRuntime.md](WorldRuntime.md), "Haze"): 1 the real air's, 0 as if the air were clear (a sign that must read from
far away). It applies to every kind of sprite (lit, unlit, blended, additive, decals) and to a cutout sprite's outline.
`draw_distance_multiplier` (above 0, default 1) is a plain setting: it multiplies whichever draw distance is in effect.
**`WorldSpriteComponent_ResolveSettings(component, spriteSettings)`** is the one pure function that turns the world
sprite's settings and the overrides into the settings in effect; the renderer's cache calls it for every sprite each
frame.

## Draw distance

1. **Per placement** (`WorldSpriteComponent_ResolveSettings`): the placement's own `draw_distance` while
   `overrides_draw_distance` is on; else the world sprite's while its `has_draw_distance` is on (`HasDrawDistance`: an
   explicit row; a definition that gives `draw_distance` without it has it on); else
   the kind's default, `WORLD_SPRITE_DEFAULT_DRAW_DISTANCE` (150 m) or, for a decal,
   `WORLD_SPRITE_DEFAULT_DECAL_DRAW_DISTANCE` (60 m). The kind is decided on the
   settings in effect: a placement that turns a sprite into a decal (fixed and `is_decal`) gets the decals' distance.
   Then times `draw_distance_multiplier`. Fixed as the world sprite says, or as the placement says while
   `overrides_is_draw_distance_fixed` is on.
2. **Per frame** (`WorldVisibility`, [WorldRuntime.md](WorldRuntime.md), "Visibility"): the models' rules,
   for sprites too: times the player's `draw_distance_scale` unless fixed, against the distance to the nearest point of
   the sprite's sphere times the zoom factor.

## Binding

The component binds its world sprite under the world's asset user (`AssetManager_LoadWorldSprite`) when it is attached
and whenever the name changes (through its setter, `Properties_Set`, a read or a copy), restarts its playback instance
from the world sprite's animation, and remembers the world sprite (`WorldSpriteComponent_GetBoundWorldSprite`: borrowed,
valid while the world is). A missing world sprite, or one whose animation is missing, leaves the sprite empty and is
logged once: `World: the sprite of object 7 ("tree") shows nothing: world sprite "x" could not be loaded (...)`. The
renderer reads the bound world sprite's settings; it never loads world sprites itself.

## Hot reload (designed in, built later)

As for world models ([WorldModels.md](WorldModels.md)): the editor's `AssetManager_ReloadDefinition` will rebuild a world
sprite in place (its pointer stays valid), and the cache reads the settings every frame, so changed settings show at
once; a reloaded animation must also rebuild the world sprites built on it and rebind their placements' instances. Not
built yet. The editor can edit a world sprite through its two tables and write it back as JSON: the root's keys, the
`render` block from the second table, and `name` and `sprite_animation`. A writer that leaves out default values must
still write `has_draw_distance` whenever it writes `draw_distance` (or write every key, `PropertyWriteOptions.
AreDefaultsWritten`): left out, a `draw_distance` kept while the flag is off would read back as on.

## Test content

`compile/out/asset/world_sprites/test_world/`: `test_sprite` (the test image, 1280 pixels per metre), `test_tire_mark`
(a fixed, blended decal), `test_tree` (an axis billboard standing on its pivot), `test_sign` (fixed, standing),
`test_smoke` (blended), `test_glow` (additive, unlit, glow 2) and `test_neon` (fixed, unlit, glow 3). Each has its
animation's name. The test world (`compile/out/worlds/test.world.json`) names them, and places the tree a second time,
mirrored by its placement (`tree_mirrored`, id 24: `overrides_flip_x` and `flip_x` on).

## Known limits

- **No sprite LODs, variants or collision**; a different look is another world sprite naming the same
  animation, which still loads once.
- **Changing a world sprite's name rebinds and restarts the playback**.
