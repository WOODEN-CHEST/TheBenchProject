# World models

`include/WorldModel.h`, `source/WorldModel.c` (the asset's data functions: the shadow tier's names, the default settings,
variants by name and on one material) and `source/WorldModelDefinition.c` (the `world_model` asset type: the definition's
parser and the loader). The placements' side is the model component, `include/WorldModelComponent.h` and
`source/WorldModelComponent.c` ([WorldData.md](WorldData.md)). The definition format is in
`references/asset_structure.md`, "World models". Tests: `tests/WorldModelTests.c`, the renderer's side in
`tests/WorldRenderCacheTests.c` and `tests/WorldVisibilityTests.c`; the LOD maths in `tests/WorldLodTests.c`. Built in T10 (decisions G4 and G17); T11 added
the LOD selection and cross-fades, T12 the `collision` section ("Collision" below; `tests/WorldColliderTests.c`).

A **world model** is everything that is the same for every placement of a thing: which model to draw at which distance
(its LODs), how far it is seen, the default render settings of its placements, and its material variants. A **world
object** is one placement: its id, transform, tags and data, which world model it uses, which variant, and what it
overrides (the classic prefab / instance split, PLAN.md 2.2). Models stay plain assets that the UI can use too: what
only matters in the world lives in the world model (the owner's rule G17: world-level assets are built on the plain ones
as dependencies; world sprites, T26, do the same for sprite animations: [WorldSprites.md](WorldSprites.md)).

## The asset type

- **A standard type**, registered with the others by `AssetManager_CreateStandardAssetTypes` (as T01 registered
  `texture`): name `world_model`, directory `world_models/`, JSON definitions; `StandardAssetTypes.WorldModel` and
  `AssetManager_LoadWorldModel` ([AssetManager.md](AssetManager.md)).
- **Definitions are checked when they are read** (a bad file is skipped at startup with one error, like every asset
  type): 1 to `WORLD_MODEL_MAX_LODS` (8) LODs naming valid model assets, their `max_distance` finite, above 0 and rising;
  `draw_distance` at least the last `max_distance` (left out, it is that distance); `lod_fade_seconds` >= 0;
  `lod_hysteresis` in [0, 1); `render`'s four settings; at most `WORLD_MODEL_MAX_VARIANTS` (64) variants with unique
  names (`[a-z][a-z0-9_.-]*`, at most 128 bytes) and at most `WORLD_MODEL_MAX_VARIANT_OVERRIDES` (32) material
  overrides each, one per slot; `bounds` with a centre and a radius >= 0; `animation_libraries` (T29) at most
  `WORLD_MODEL_MAX_ANIMATION_LIBRARIES` (16) unique asset names. **Unknown keys are refused** in every object
  the definition defines: the root, each LOD, `render`, each variant, each material override and `bounds`
  (`AssetJSON_RejectUnknownKeys`: the message names the key, where it is and the keys allowed there; a key from a file
  is shown as printable ASCII only). Values of the shared shapes (a vector, a colour) follow the shared GameJSON
  parsers, which ignore extra keys inside their own objects. Messages start with `World model "<name>": ` and name the place
  (`lods[1].max_distance`, `variants[0].material_overrides[2]`).
- **Loading** (`WorldModelDefinition_LoadAsset`): the LOD models and the variants' textures load as **dependencies**,
  under the world model's dependency user (`AssetManager_LoadAssetSingle`), so holding the world model holds them and
  they unload right after it. A dependency that fails fails the world model, naming it (`World model "desk": LOD 1
  (model "desk_lod1"): ...`, `... variant "red", slot 0: texture "red_albedo": ...`); the manager then releases what had
  loaded. The variants, their overrides and their names are one allocation the asset owns; the `GameModel` and
  `GameTexture` pointers in it are borrowed from the dependencies. A variant override whose slot no LOD model has is
  logged once at load (it changes nothing).
- **Animation (T29, [Animation.md](Animation.md)):** the world model's **skeleton** is the first skinned LOD's
  (`WorldModel_GetSkeleton`, `_GetSkeletonLod`); every other LOD with bones must have its bones and bind poses
  (`GameSkeleton_HasSameBindPoses`), or the world model is refused, so one pose bends every skinned LOD (a coarse LOD may
  be rigid: it draws as it is). Its `animation_libraries` load as dependencies too (`_GetAnimationLibraryCount`,
  `_GetAnimationLibrary`, `_GetAnimationLibraryName`: the names are the world model's own copies); two of them may not
  hold a clip of the same name, and naming libraries without a skinned LOD is logged once at load (its placements play
  nothing).
- **Bounds:** the definition's sphere, authored in the object's own space, or the sphere around LOD 0's mesh bounds in
  LOD 0's mesh space; `WorldModel_GetBoundsTransform` takes either to the object's space (identity, or LOD 0's import
  transform). The renderer composes that with the object's world matrix first and scales the radius by the product's
  stretch (`WorldTransform_GetMaxScale`), so the derived sphere is exactly the drawn model's, as before world models:
  bounding the import and the placement separately would multiply an uneven import scale by an uneven placement scale
  (five or ten times too large, the review found). `WorldModel_HasOwnBounds` says which: a posed placement (T29) keeps
  the definition's sphere, and otherwise is bounded by its pose instead of LOD 0's bind pose.

## The loaded `WorldModel`

Pure data, read-only, owned by the asset manager (never keep it past your asset user). Accessors:
`WorldModel_GetLodCount`, `_GetLodModel(index)` (the `GameModel*`), `_GetLodMaxDistance(index)`, `_GetSettings` (the
placements' defaults, a `WorldModelSettings`: outline, crisp, shadow tier, casts shadows, draw distance, fixed),
`_GetLodFadeSeconds`, `_GetLodHysteresis`, `_GetBoundsCenter` / `_GetBoundsRadius` / `_GetBoundsTransform` /
`_HasOwnBounds`, the animation's `_GetSkeleton`, `_GetSkeletonLod` and libraries (T29),
`_GetVariantCount`,
`_GetVariantName(index)`, `WorldModel_TryFindVariant(name, &index)` and `WorldModel_ApplyVariant`.
`WorldShadowTier` and its names (`WORLD_SHADOW_TIER_ENUM`, which the component's `shadow_tier` row uses too) live in
`WorldModel.h` since T10; `WorldModelSettings_CreateDefault` is what a world model and a placement start from.

## Variants

A variant is another look of the same meshes: a named list of material overrides, one per material slot (the
material's index in each LOD's model: every LOD keeps the same slots, ASSETS.md). An override swaps in texture assets
(albedo, normal, the packed occlusion/roughness/metallic map, emissive) and sets the PBR factors (tint, metallic,
roughness, emissive colour and intensity).
- **Applied at draw time, never to an asset** (AGENTS.md rule 3): the renderer copies a mesh's material maps for every
  draw (`WorldRendererGeometry.c`) and `WorldModel_ApplyVariant(model, variant, materialIndex, maps)` changes that copy:
  the textures first, then the factors through `GameMaterialFactors_Apply` ([GameAssets.md](GameAssets.md)), the same
  function the model type's `material_overrides` use at load (an emissive map without a colour or intensity shows at
  white x 1). The lit pass uploads the copy's factors and map flags, so the shader sees the variant.
- **The same parser:** a variant's overrides are read by `AssetJSON_ReadMaterialSlot` and `AssetJSON_ReadMaterialFactors`
  (`source/AssetDefinitionCommon.{h,c}`), which the model type's `material_overrides` use too. Only the maps differ: a
  model's are image files under `models/` with `texture_properties`, a variant's are texture asset names (shared between
  variants and world models, filtered by their own definitions).
- **Chosen per placement:** the model component's `uses_variant` and `variant`. The render cache resolves the name to an
  index with the world model, and again when either changes; an unknown name draws the model's own materials and is
  logged once (`WorldRenderer: object 12 ("desk") draws world model "office_desk" with its own materials: it has no
  variant "pink".`). An entry is carried over to a component that replaced another on the same object only when it was
  resolved for the same world model and the same variant: two components' revisions are not comparable (one read from
  a file or copied often starts at the same revision), so the cache compares what was resolved, not revisions.

## Placements: the model component's overrides

The model component ([WorldData.md](WorldData.md), "Model component") names the world model (`world_model`; changing the
name swaps the model and keeps the object) and overrides the world model's settings per placement. Every override is
explicit (the owner's rule with G17): a flag row `overrides_<key>` (a Bool, off by default) right before the value row
`<key>`; while the flag is off the world model's value applies and the placement's own value is kept (and saved), so
switching the flag back on restores it. No sentinel values, no `inherit` states. The pairs are two plain rows of the
component's table (not a new property type): every reader, writer, the inspector and scripts handle them as they are,
and `PropertyTable_Check` enforces the pairing for every table ([PropertyDescriptors.md](PropertyDescriptors.md),
"Override flags").

| Setting (`WorldModelSetting`) | Flag | Value |
|---|---|---|
| `HasOutline` | `overrides_has_outline` | `has_outline` |
| `OmitPixelation` | `overrides_omit_pixelation` | `omit_pixelation` |
| `ShadowTier` | `overrides_shadow_tier` | `shadow_tier` |
| `CastsShadows` | `overrides_casts_shadows` | `casts_shadows` |
| `DrawDistance` | `overrides_draw_distance` | `draw_distance` |
| `IsDrawDistanceFixed` | `overrides_is_draw_distance_fixed` | `is_draw_distance_fixed` |
| `LodDistances` (T11) | `overrides_lod_distances` | `lod_distances` (a list) |

`draw_distance_multiplier` (above 0, default 1) is a plain setting: it multiplies whichever draw distance is in effect;
`lod_distance_multiplier` likewise for the LOD switch distances, and `is_lod_forced` with `forced_lod` force a LOD (see
"LODs").
In C: `WorldModelComponent_IsOverridden` / `_SetOverridden(self, setting, isOverridden)` for the flags, the typed
getters and setters for the values. **`WorldModelComponent_ResolveSettings(component, modelSettings)`** is the one pure
function that turns the world model's settings and the overrides into the settings in effect; the renderer's cache
calls it every frame (against `WorldModelSettings_CreateDefault` while the world model is not resolved).

## Draw distance (G17)

An object's draw distance is resolved in two steps:
1. **Per placement** (`WorldModelComponent_ResolveSettings`): the world model's `draw_distance`, or the placement's own
   while `overrides_draw_distance` is on, times `draw_distance_multiplier`; fixed as the world model says, or as the
   placement says while `overrides_is_draw_distance_fixed` is on.
2. **Per frame** (`WorldVisibility`, [WorldRuntime.md](WorldRuntime.md), "Visibility"): times the player's
   `draw_distance_scale` unless fixed (`WorldVisibility_ScaleDrawDistance`), compared with the camera's distance to the
   nearest point of the object's bounding sphere times the **zoom factor**, `WorldVisibility_GetZoomFactor(fovY)` =
   tan(fovY / 2) / tan(30 degrees): exactly 1 at the default 60 degrees, below 1 when zoomed in, so binoculars or a
   zoomed camera keep far objects in sight. T11's LOD selection uses the same factor.

A placement may draw less far than its world model's last LOD distance: its coarser LODs are then never reached.
Sprites follow the same two steps since T26, through world sprites ([WorldSprites.md](WorldSprites.md)).

## LODs

A world model has 1 to `WORLD_MODEL_MAX_LODS` (8) LODs, the finest first, each with its `max_distance`: LOD i is drawn
while the object is nearer than its switch distance i, the last LOD past the last boundary until the draw distance
(where T07's fade takes it; the last LOD's own `max_distance` is the default draw distance). Built in T11 (decisions G6,
G17); the maths is the private `source/WorldLod.{h,c}` ([WorldRuntime.md](WorldRuntime.md), "LODs").
- **Per placement** (`WorldModelComponent_ResolveLodSettings`, a pure function: `WorldModelLodSettings`): the switch
  distances are the world model's `max_distance`s, or the placement's own list while `overrides_lod_distances` is on and
  the list holds one distance per LOD (a `FloatList` row: finite, above 0, rising, at most 8; when the world model gains
  or loses a LOD the list no longer fits and the world model's distances apply, with one warning per object, logged when
  it starts); times `lod_distance_multiplier` (above 0, default 1: larger keeps each LOD farther, more detail). With
  `is_lod_forced` the LOD `forced_lod` is drawn whatever the distance (clamped to the last LOD; kept while not forced).
  The world model's `lod_hysteresis` and `lod_fade_seconds` come along.
- **Per frame** (`WorldRenderCache_UpdateLods`): the distance is the camera's to the object's bounding-sphere centre
  times the **zoom factor** (`WorldVisibility_GetZoomFactor`: zooming in picks finer LODs), compared with each switch
  distance times the player's **`lod_distance_scale`** (`config.json`, default 1, [0, 16]; above 1 more detail). A fixed
  draw distance does not exempt the LODs: `is_draw_distance_fixed` keeps an object visible, not detailed.
- **Hysteresis:** from the LOD an object shows (or heads to), it switches to a coarser one only at a switch distance
  x (1 + h), back to a finer one only below x (1 - h): standing at a boundary does not flicker.
- **Cross-fade:** a new LOD fades in over `lod_fade_seconds` (0 swaps at once): both LODs are drawn with complementary
  4x4 dither masks (the incoming LOD takes the share of the pixels the fade's progress gives), so no pixel is empty and
  the background never shows. Going back during a fade runs it back from the same pixels, so nothing pops; a third LOD
  fades on from the LOD with the larger share (never three LODs). The G-buffer draws the LOD with the larger share, the
  shadow passes the LOD the object heads to, both solid, so shadows never shimmer.
- **Drawing:** each LOD is drawn with its own import transform inside the object's world matrix
  (`WorldRenderModel_GetLodTransform`); the bounding sphere is the world model's (its `bounds`, or LOD 0's), the same
  for every LOD. Every LOD should share the same origin, axes, scale and material slots (variants apply to each).
- **Debug:** the test frame's Q key tints models by LOD (0 white, 1 green, 2 yellow, 3 red, coarser magenta; LODs past 0
  also glow faintly, so the colours read at night); the I key's status line counts the drawn models per LOD and the
  cross-fading ones.

## Collision (T12)

A world model may hold its placements' collision: named **collision sets** (compounds of spheres, capsules, boxes,
cylinders, convex hulls, planes and meshes, each placed in the model's space, an optional part name each), the set its
placements use by default, and the placements' default layer, mask, trigger flag and mass (T27: how pushables share a
push; above 0, at most `COLLISION_MAX_MASS`, `COLLISION_DEFAULT_MASS` (1 kg) when left out, also under `"bounds"`).
`"collision": "bounds"` is one set, `bounds`, holding a box around LOD 0 (exact under its import transform). No
section: no collision. The format is in `references/asset_structure.md`; the shapes, the collider component that places
them and the collision world are in [WorldCollision.md](WorldCollision.md).
- **Parsed with the definition** (`source/WorldModelCollisionDefinition.c`): the same rules as the rest (messages name
  the place, `collision.sets[0].shapes[2].radius`), each shape type refusing the other types' keys, the limits (32 sets,
  128 shapes a set, 1024 in all), point hulls built once to refuse flat ones.
- **Loaded with the asset:** a hull or mesh built from a `model` loads that model as one more **dependency** (it may be a
  LOD model too); a failure fails the world model, naming the set and the shape (`World model "shop": collision set
  "walls", shape 0: ...`). One block holds the sets, shapes, names, hulls and meshes; the asset owns it.
  `WorldModel_GetCollision` returns it (NULL without a section), with `WorldModelCollision_GetSetCount`, `_GetSet`,
  `_GetDefaultSetIndex` and `_TryFindSet(name)`.

## Hot reload (designed in, built later)

Consumers keep only names and re-resolve when something changes: the render cache resolves a world model by name and
again when the component's name changes (its revision). The editor will need the reload itself
([AssetManager.md](AssetManager.md), "Other notes"): `AssetManager_ReloadDefinition(type, name)` rereads one JSON file,
the loaded asset is rebuilt in place (its pointer stays valid), and a per-asset revision tells caches such as the render
cache to re-resolve what they derived from it (the LOD model pointers, the variant index, the bounds). A world model
depends on its models and textures, so reloading one of those must also rebuild the world models built on it. Not built
here.

## Test content

`compile/out/asset/world_models/test_world/`: `test`, `pbr_demo` and `pbr_metal` (one LOD each, the model of the same
name, 600 m: the draw distance every model had before), and `painted_cube` (the test cube with the variants `red` and
`blue`, tints only), and since T11 `lod_test`: three LODs of a sphere (the placeholder models `lod_test_lod0` to `_lod2`,
`models/placeholder_lod_test/`: UV spheres of radius 0.5 m at 32x16, 16x8 and 8x4 segments, written by a generator
script) switching at 13 and 16 m, seen 60 m. The test world (`compile/out/worlds/test.world.json`) names world models;
its per-object crisp and shadow tier settings are now override pairs. Two cubes behind the centre row show
`painted_cube`'s variants; the test frame's E key steps them (their own, swapped, none) and Z zooms the camera to 15
degrees ([WorldRuntime.md](WorldRuntime.md), "WorldTestFrame"). A row of six `lod_test` spheres stands along the left
edge of the ground (x = 8, z = -6 to 9), with two more beside it: `lod_test_forced` (LOD 2 forced) and
`lod_test_own_distances` (its own switch distances, 3 and 6 m); Q tints them by LOD.

Since T12 the world models collide: `test`, `pbr_metal` and `painted_cube` with `"bounds"`, `lod_test` with a convex
hull of `lod_test_lod0` (64 vertices), `pbr_demo` with a mesh of its own model (layer `world`); and `test_door` (the test
cube through the model `test_door`'s import transform, a 1 x 2 m panel hinged at its origin) has a `closed` and an
`open` set, a panel and a named handle each ([WorldCollision.md](WorldCollision.md), "Debugging and the test content").

Since T29 two world models animate ([Animation.md](Animation.md), "Test content"): `test_humanoid` (the placeholder
figure, LOD 0 `test_humanoid` to 12 m, then the coarser `test_humanoid_lod1` to 80 m, both skinned on one skeleton) and
`test_humanoid_short` (one LOD), each playing `test_humanoid_motions`.

## Known limits

- **Surfaces** belong to T19: the `surfaces` key is refused as unknown until then.
- **A missing dependency fails the whole world model** (every LOD and texture must load), so its objects draw nothing;
  a variant texture typo costs the model, not just the variant.
- **The unknown-variant warning** comes again after `WorldRenderer_PrepareWorld` (the cache resolves everything again).
- **World files written before T10:** a JSON file names models under `model`, which is skipped as unknown; a GHDF file
  keeps them on wire 1, which now reads as the world model's name (it draws where a world model has the model's name,
  as every shipped one has). Either way their crisp and shadow tier settings read as the placement's own values and stay
  unused until their override flags are set. The world file format was not raised: the test world was the only world
  file ([WorldFile.md](WorldFile.md)); the F5 saves are rewritten by the next save.
- **The unknown-variant warning quotes the variant's name as the world file gave it** (any UTF-8 of at most 128 bytes),
  as the world's warnings already quote object names.
- **No headless mesh loading:** tests use code-built models and textures (`tests/TestAssets.h`); the GPU path (variant
  textures in the draw) was checked with the windowless EGL harness.
