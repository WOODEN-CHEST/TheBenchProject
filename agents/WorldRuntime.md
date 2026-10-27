# World runtime layer (camera, renderer, services, bootstrap)

The runtime pieces that turn the (rendering-independent) world data layer into something you can see and
fly around: the camera, the world renderer, the services bundle, the test frame and the bootstrap in `main.c`.
See [WorldData.md](WorldData.md) for the data model it draws.

## Modules

| Module | Files | What it is |
|---|---|---|
| GameCamera | `include/GameCamera.h`, `source/GameCamera.c` | The 3D camera (a double position, fov/yaw/pitch/roll), converted to raylib's render camera at the origin only at render time. |
| Services | `include/Services.h`, `source/Services.c` | A bundle of BORROWED pointers to the shared game services (logger, config, unicode, asset manager + standard type ids, frame manager, per-tick input, GHDF pool, the key registry, base directory). |
| WorldRenderer | `include/WorldRenderer.h`, `source/WorldRenderer*.c`, `source/WorldRendererInternal.h` | Renders a World through a GameCamera; fully separate from the world data. Split by render pass, see "Renderer structure". |
| WorldRenderCache | `source/WorldRenderCache.h`, `source/WorldRenderCache.c` (private) | The renderer's per-component cache and per-frame draw lists (models with their resolved world models, their LOD state, the variant, the settings in effect and their pose; since T30 the camera's pose, the first-person object's with its hidden bones collapsed; sprites with their world sprites' settings in effect; lights; since B01 terrains and their chunks; since B03 the model instances components with their cells), built from the world's per-type component arrays. |
| WorldInstancing | `source/WorldInstancing.h`, `source/WorldInstancing.c` (private) | Instanced drawing as pure maths (B03, see "Model instances"): a component's copies sorted into cells with their records, the copies' LOD bands, fades and per-pass draws, and the LODs a cell can show: the C mirror of `shaders/common/instancing.glsl`. |
| WorldLod | `source/WorldLod.h`, `source/WorldLod.c` (private) | Levels of detail as pure maths (see "LODs"): the LOD selection by distance with hysteresis, the scale and a forced LOD, and the cross-fade's state and its two draws. |
| WorldAtmosphere | `source/WorldAtmosphere.h`, `source/WorldAtmosphere.c` (private) | The atmosphere as pure maths (T15, see "Atmosphere"): the air's coefficients from the environment, the height the sky is seen from, heights and distances along rays without cancellation, and the lookup tables' layout: the C mirror of `shaders/common/atmosphere.glsl`'s mappings. |
| WorldRenderSpace | `source/WorldRenderSpace.h`, `source/WorldRenderSpace.c` (private) | Render space as pure maths (see "Render space", T32): the frame's origin and its rotation-only view and projection, the C mirror of the sky shader's ray, the shadow cascades snapped to their texel grids, each grid anchored to the last frame's box. |
| WorldVisibility | `source/WorldVisibility.h`, `source/WorldVisibility.c` (private) | What each pass draws this frame (see "Visibility"): frustum planes and sphere tests, draw distances and their fade band, opacity, the dither's C mirror, and the per-pass lists built from the render cache. |
| SpriteTransform | `source/SpriteTransform.h`, `source/SpriteTransform.c` (private) | A sprite quad's pure maths: its matrix for a viewer (billboard, axis billboard, fixed; the camera or a light), its world size, its bounds, and the back-to-front order of a transparent list (see "Sprites"). |
| NightVision | `source/NightVision.h`, `source/NightVision.c` (private) | The eye's dim-light vision as pure maths: its calibration on the game's light scale, the rods' share, the rods' grey (see "Night vision"). |
| WorldTestFrame | `include/WorldTestFrame.h`, `source/WorldTestFrame.c`, `source/WorldTestCharacters.{h,c}` (private: walk mode and the test NPCs), `source/WorldTestAnimation.{h,c}` (private: the animation system and its keys), `source/WorldTestInstances.{h,c}` (private, B03: the pebble field and the instancing A/B) | A concrete GameFrame: a WASD + mouse-look flycam over a test world, and since T13 walk mode (the player as a character) and test NPCs. |

`source/main.c` is the composition root (bootstrap).

## GameCamera

Coordinate convention: **+Y up, +Z forward** (yaw=pitch=roll=0 looks toward +Z). Plain value type (no
Construct/Deconstruct — like ProgramTime); build with `GameCamera_Create`, mutate public fields directly.
`Position` is a `Vector3Double` (since T32: far from the world's origin a float's steps would shake the view); the
angles, the field of view and the directions stay floats. Code that moves the camera works in double (the flycam
adds its float movement to the double position; the player controller puts it at the character's eyes, a double
since T33).
`GameCamera_GetForward` derives the view dir from yaw/pitch; `GetRight` is the horizontal strafe axis
(`cross(worldUp, forward)`, roll-independent); `GetUp` rotates world up around forward by roll.
`GameCamera_ToRenderCamera` is the single place a raylib `Camera3D` is produced: the camera in render space, at
(0, 0, 0) with forward as its target (see "Render space"), so `BeginMode3D` loads a view matrix that is a pure
rotation. Instance/runtime data — never saved to a world. With +Z forward and +Y up in raylib's
right-handed pipeline, world +X renders on the camera's LEFT; the owner confirmed this is wanted.

## Render space (T32)

The renderer draws in **render space**: the world's orientation with the camera at (0, 0, 0). A float holds a world
position only to millimetres or worse far from the world's origin (its step is 0.24 mm at 2–4 km, 1 mm past 8 km),
which made the world and the sky shake there; an offset from the camera keeps a float's full precision near the
camera, however far out it is. The maths is the private `source/WorldRenderSpace.{h,c}` (tested in
`tests/WorldRenderSpaceTests.c`).
- **The origin.** Each frame takes the camera's position, in double, as its origin (`WorldRenderFrame.Origin`). Every
  position the GPU sees is that position minus the origin, subtracted in double and rounded once
  (`GameMath_ToVector3Relative`, `GameMath_ToMatrixRelative`, [GameMath.md](GameMath.md)). Never round a far position
  to float and subtract after: the float keeps the absolute position's error.
- **One place converts.** `WorldRenderCache_Sync` takes the frame's origin: every entry keeps its world placement in
  double (`WorldMatrix`, `Center`, the sprites' and lights' `Position`) and the same placement in render space
  (`RenderMatrix`, `RenderCenter`, the sprites' `Shape` and `FixedTransform`, the lights' `RenderPosition`). The passes,
  the visibility, the LOD selection, the light culling and the uniforms read only the render-space values. Since T33
  the world's positions and world matrices are doubles ([WorldData.md](WorldData.md), "Positions are doubles"), and the
  sync reads them as they are. Debug draws convert the same way (the collision world's double transforms and contact
  points, the grid around the world's origin).
- **The view** (`WorldRenderSpace_BuildView`): raylib's camera at the origin looking along the camera's forward, so
  the view matrix is a pure rotation and the view-projection and its inverse hold no large numbers. The sky's ray is
  `normalize(invViewProj × the point at clip depth 1)` (the far plane's, or with reversed depth the near plane's: the
  same direction; the C mirror is `WorldRenderSpace_GetSkyRay`), the stars and the sun's
  place on screen (the sun shafts) are directions projected with w = 0, the outlines' sun is the sun's direction turned
  by the view. Measured against a double-precision camera 50 km out: points 0.6 m to 30 m away land within 2e-5 scene
  pixels and the sky's rays within 3e-5 (the world-space floats before missed by 0.9 and 23 pixels).
- **Reversed depth** (T39, decision G41; `source/GLDepth.{h,c}`, `WorldRenderSpace_GetReversedProjection`): where the
  GPU has clip control (OpenGL 4.5 or `GL_ARB_clip_control`), the camera's passes (the scene, the G-buffer, the crisp
  overlay) draw with a projection that has no far plane: x and y as raylib's, depth 1 at the 5 cm near plane and
  near / distance beyond it, toward 0 at infinity, into 32-bit float depth buffers cleared to 0 and tested `GL_GEQUAL`.
  A float's exponent spreads its precision evenly over the orders of magnitude, so a hill 20 km away is resolved to about
  a ten-millionth of its distance, 2 mm (raylib's 24-bit depth stepped about 19 m at its fixed 4 km far plane, which
  nothing beyond passed). The frame's projection (`WorldRenderFrame.Projection`, its inverse in closed form) is the reversed one; each
  camera pass loads it in place of `BeginMode3D`'s (`WorldRendererCommon_LoadCameraProjection`) between
  `WorldRendererCommon_BeginCameraDepth` and `_EndCameraDepth`, which switch GL to the reversed state before the pass's
  clear and give raylib its own back after its last draw (-1 to 1, `GL_LEQUAL`, cleared to 1): the shadow passes, the
  post passes and the UI never see it, and their own depth stays raylib's. The camera's frustum has no far plane
  (`WorldVisibility_ExtractReversedFrustum`): the draw distances alone end what is drawn. Every shader that reads the
  camera's depth takes the convention from `shaders/common/depth.glsl` and the `depthReversed` uniform
  (`WorldRendererCommon_SetDepthConvention`): `depthIsSky` (nothing drawn: exactly 0, or in standard depth the cleared
  1), `depthToNdc` (a depth for the inverse projection: the AO's and the outlines' positions) and `depthIsBehind` (the
  crisp composite's occlusion, by a share of the depth); the meter, the bloom's prefilter and the sun shafts read the
  sky with the first, the stars sit at depth 0. Without clip control the renderer logs it once and keeps raylib's
  projection, its 4 km and its depth formats (the status's "reversed depth" unavailable).
- **Stable shadow cascades** (`WorldRenderSpace_GetCascade`, `WorldRenderSpace_StepCascade`): each cascade is a
  fixed-size box around the camera along the shadow light, its centre kept on the cascade's texel grid (1.6 cm near, 7.8
  cm far) in the light's frame, in double. Each frame the centre moves from where it was (its anchor, which
  `WorldRenderSpace_StepCascade` keeps in the cascade's `WorldRenderCascadeGrid`) to the camera by whole texels along
  the light's right and up axes. While the light holds still the grid stands still in the world, so shadow edges do not
  crawl while the camera moves (a point keeps its texel; when the centre steps, the map's contents move by one whole
  texel). While the sun or the moon turns, the grid turns about the anchor, within half a texel of the camera, so edges
  near the camera slide as they would under a box centred on the camera, wherever the camera is: a point 4 m from a
  still camera moves 0.24 texels a frame on the near map with a 2-minute day, 0.016 with a 24-minute day, 0.0004 with a
  24-hour day, from 7 m to 28 km out. (A grid fixed to the world's origin would turn about that origin: 0.38–0.48 texels
  a frame of shimmer far out, whatever the clock's speed.) Stepping the shadow direction's updates is not done.
- **The rule for every shader:** "world space" in a shader means render space. `fragPosition` is the fragment's
  position relative to the camera; light positions (`pointLightPositions`, `pointShadowLightPos`, the cube depth
  shaders' `lightPosition`) and the shadow matrices (`lightVP`, `lightVPFar`) are in render space; the camera sits at
  the origin, so there is no camera uniform (`viewPos` and the sky's `cameraPos` are gone): the view vector is
  `-fragPosition`, the haze's distance `length(fragPosition)`, a decal's side `dot(normal, -position)`. Directions (the
  sun, the moon, normals) are the world's. The haze, specular and `PerturbNormal` (`dFdx` of the position) work
  unchanged and lost their large-coordinate error. The haze's heights (T35) come from the origin's Y in double (the
  camera's height above the ground haze's base and above the atmosphere's ground), never from a render-space position.
- **Absolute coordinates in a shader** (world-anchored noise, rain streaks wrapping around the camera, wave phases,
  puddle masks; T14, T17, T18, T19, T21 and T22 will want them): a shader never receives the origin as floats. It gets
  the origin **wrapped by the pattern's period**, computed in double on the CPU (`fmod(origin, period)` per axis), and
  adds that to its render-space position: `pattern(fragPosition + wrappedOrigin)` repeats exactly like
  `pattern(worldPosition)` and stays precise. A pattern without a period is a precision bug far out.

## WorldRenderer

### Ownership and lifetime

- Borrows the `AssetManager`, the `Logger` and the `GameConfig` (config-side effect multipliers).
- Owns two asset users: one holds the pass shaders for its lifetime, the other the world's world models (and through
  them, as their dependencies, their LOD models and variant textures). `WorldRenderer_PrepareWorld` replaces the model
  user with a fresh one after resolving every world model the world still uses, so world models no component uses any
  more are released with their meshes (asset holds are not counted, so this swap is how unused ones get released). Call it after building or loading a world, after large changes, and
  whenever a different World is (re)built at the address of the previous one.
- Never writes to the world or to shared assets (AGENTS.md rule 3):
  - meshes draw with a `Material` built on the stack (`WorldRendererGeometry`): the asset's 12 material maps
    copied, the pass shader, the object tint multiplied into the copy's albedo colour;
  - the PBR shader's location table is a renderer-owned copy (`_pbr._locs`) whose material map samplers point
    at `mraMap` / `normalMap` / `emissiveMap`, so `DrawMesh` binds each material's textures without touching the
    shared shader asset;
  - world sprites and their animations belong to the World (its sprite components bind them under the world's asset
    user); the renderer only reads the current frame and the bound world sprite's settings.

### Per-component render cache

`WorldRenderCache_Sync` runs at the start of every render, for the frame's origin (the camera's position: see
"Render space"), and walks the world's per-type component arrays once (`World_GetComponents`), never the objects:
- **Models:** one entry per model component, in the world's order, remembering the owner's id and the component. An
  entry's **world model** ([WorldModels.md](WorldModels.md)) is resolved once (`AssetManager_LoadWorldModel` under the
  model user, which loads its LOD models and variant textures with it); when the component's revision changes the name
  is hashed again, and the world model is resolved again only if the name changed. A failed resolution is logged once
  (`WorldRenderer: object N ("name") draws nothing: world model ...`) and not retried until the name changes. The entry
  keeps its **LOD state** across frames (`Lod`, see "LODs"; started afresh when the world model is resolved again) and
  resolves the component's **variant** to
  an index (`HasVariant`, `VariantIndex`) with the world model, and again when `uses_variant` or `variant` changes (an
  entry is carried over to a replacing component only when it was resolved for the same world model and variant); an
  unknown variant draws the model's own materials and is logged once (`... draws world model "x" with its own
  materials: it has no variant "y".`). Each frame the entry takes the **settings in effect**
  (`WorldModelComponent_ResolveSettings`: the world model's, or the placement's own while their override flags are on:
  outline, crisp, shadow tier, casts shadows, the draw distance times the multiplier, and whether it is fixed), gets the
  object's world matrix (`WorldObject_GetWorldMatrix`: scale → Euler XYZ → translation, raylib's `DrawModelEx`
  convention, times its parents'; read without writing to the world, a dirty one computed on the fly; each LOD is drawn
  with its own import transform inside it, `WorldRenderModel_GetLodTransform`) and a conservative world bounding sphere:
  the world model's (its `bounds`, or the
  sphere around LOD 0's mesh bounds, computed once at load) moved by its bounds transform (identity, or LOD 0's import
  transform) composed with the world matrix and scaled by that product's `WorldTransform_GetMaxScale` (never below the
  true stretch: the longest axis, bit for bit, for an unturned object; an ulp or so more for a turned one with an
  uneven scale, whose axes' dot products round a hair from zero; more under the shear a hierarchy can add, where the
  longest axis fell short), and `IsDrawn`: its world model resolved and its object
  effectively enabled and visible (`WorldObject_IsEffectivelyEnabled` / `_IsEffectivelyVisible`: a disabled or hidden
  ancestor counts). The world matrix and the sphere's centre are kept in double (`WorldMatrix`, `Center`) and in render
  space (`RenderMatrix`, `RenderCenter`: relative to the frame's origin, subtracted in double). Passes draw only the
  `IsDrawn` entries; a hidden object keeps its entry and its world model. Every
  pass draws a model one LOD at a time through `WorldRendererGeometry_DrawModel`, which applies the entry's variant to
  the per-draw material copy (`WorldModel_ApplyVariant`) before the lit pass uploads the copy's factors. Since T29 an
  entry also takes its object's **pose** (`Pose`: the animator's, when it was made for the entry's world model,
  [Animation.md](Animation.md)); then, unless the world model gives bounds of its own (`WorldModel_HasOwnBounds`), its
  sphere is the one around the pose's box, moved by the skeleton LOD's import transform, so culling, shadows and LOD
  distances follow the pose.
- **Sprites:** the sprites of effectively enabled and visible objects whose playback instance has a frame: the current
  frame and its area in its texture (normalized: the shaders' `uvRect`), the quad before a viewer turns it
  (`SpriteShape`: the orientation, the object's world position and rotation axes, the world size from the frame's
  pixels, the pixels per unit and the object's world X and Y scale, the pivot, the flips; `WorldObject_GetWorldTransform`:
  a root's own values, a child's decomposed from its world matrix), a fixed sprite's matrix (it needs no viewer), a sphere around the
  pivot that holds the quad in every orientation, the tint, and the settings: alpha mode and cutoff, lit, emission,
  light wrap, decal (marked as one and fixed) and its lift, casts shadows, outline, crisp (only for cutout sprites that
  are not decals: the crisp overlay composites opaque surfaces), and the draw distance and whether it is fixed. Every
  setting is the one in effect, taken every frame: the sprite's world sprite's (the one its component bound,
  `WorldSpriteComponent_GetBoundWorldSprite`), or the placement's own while its override flag is on
  (`WorldSpriteComponent_ResolveSettings`, [WorldSprites.md](WorldSprites.md)). A sprite without a bound world sprite is
  not listed.
- **Lights:** the lights of effectively enabled and visible objects, with their world position (in double, and in render
  space: `RenderPosition`), reach (the range), intensity, colour and shadow flag.
- **Terrains** (B01, [Terrain.md](Terrain.md)): one entry per terrain component (`WorldRenderTerrain`), kept across
  frames and matched by owner id and component serial; its terrain is resolved as a model's world model is
  (`AssetManager_LoadTerrain` under the model user, again only when the name changes; a failure logged once, `...
  draws nothing: terrain ...`). Every frame each **chunk** with a surface of a terrain whose object is shown is listed
  (`WorldRenderTerrainChunk`, `WorldRenderCache_GetTerrainChunks`), terrain by terrain, in chunk order: the owner's id
  and serial, the asset and the chunk index, its surface's triangle count, the settings in effect
  (`WorldTerrainComponent_ResolveSettings`: outline, shadow tier, casts shadows, the draw distance times the multiplier,
  fixed or not; terrain is never crisp), the chunk's world matrix (the object's world matrix times the translation to
  the chunk's centre: the chunk's positions are relative to it) and its sphere through it (scaled by
  `WorldTransform_GetMaxScale`), both in double and in render space, and the object's tint. The GPU meshes are the
  renderer's (`WorldRendererTerrain.c`, see "Renderer structure"): the cache lists, the renderer uploads.
- **Model instances** (B03, see "Model instances"): one entry per `model_instances` component
  (`WorldRenderInstances`, `WorldRenderCache_GetInstances`), kept across frames and matched by owner id and component
  serial (a replaced component gets a new entry); its world model and variant are resolved as a model's are (under the
  model user, again only when the name or the variant changes; failures logged once: `... draws no instances: world
  model ...`, `... draws the instances of world model "x" with its own materials: it has no variant "y"`). Its copies are
  sorted into **cells** (`WorldInstancing.h`) when the entry is new, when the component's instances revision changes
  (`WorldModelInstancesComponent_GetInstancesRevision`) or when its world model is resolved again (the bounds change),
  never because the object moved: a cell lives in the object's own space. Each build gets an id no other build of the
  cache had (`CellsBuildId`, from a counter `Reset` keeps), which the renderer's GPU copy follows. Every frame each cell
  of a shown object is listed (`WorldRenderInstanceCell`, `WorldRenderCache_GetInstanceCells`), entry by entry: its
  records' range, the settings in effect (`WorldModelInstancesComponent_ResolveSettings`: never crisp), the object's
  tint, the cell's world matrix (the object's world matrix times the translation to the cell's centre: its records are
  relative to it) and its sphere through it, both in double and in render space. `WorldRenderCache_UpdateLods` gives
  each drawn entry its LOD bands for the frame's `lod_distance_scale` (`WorldInstancing_GetLodBands`) and each cell the
  LODs its copies can show from the camera (`LodMask`, `WorldInstancing_GetCellLodMask`). The cache lists, the renderer
  uploads (`WorldRendererInstances.c`).

Entries follow the components across additions and removals: when the world's component-set version changes, entries
are re-matched by owner id, and an entry is carried over only if its component still names the model it was
resolved for (an id can come back with another object, e.g. an editor undo or a re-applied save). A different
World resets the cache, and the per-frame walk also checks each entry's component pointer and owner id, so a world
rebuilt at the same address is caught as soon as a component differs. Components added at runtime appear without
calling `PrepareWorld`; a model that failed to load is retried only after its name changes or `PrepareWorld` (call it
after asset definitions change).

### Visibility (culling, draw distance, dithered fades and opacity)

Right after the cache's sync, `WorldVisibility_Build` (private `source/WorldVisibility.{h,c}`, tested in
`tests/WorldVisibilityTests.c`) decides what every pass draws, and every pass iterates its list instead of the cache's
whole arrays. The lists hold indices into the cache's models, sprites and (B01) terrain chunks, ascending (so the
world's order is kept), and are rebuilt every frame without allocating once grown; each entry also gets its fades. Everything here is in render
space (T32): the cache's render-space centres, the camera at (0, 0, 0), the frustum, the cascades' boxes and the cube's
lamp all relative to the frame's origin.
- **Frustum culling.** The camera's frustum is extracted once per frame from its view-projection (Gribb and Hartmann, six
  unit inward planes; `WorldVisibility_ExtractFrustum`; with reversed depth, T39, `WorldVisibility_ExtractReversedFrustum`:
  the same sides and near plane, and a far plane everything passes). The scene, the G-buffer and the crisp overlay draw
  only what touches it (`WorldVisibility_IsSphereInFrustum`). A model's sphere is its bounding sphere (around `RenderCenter`); a
  sprite's is the sphere around its pivot that holds its quad in every orientation (`BoundsRadius`: half its diagonal
  around its centre for the default centre pivot), so one sphere serves the camera and the lights.
- **Shadow casters are never culled by the camera:** a caster off screen can still shadow what is on it. Each cascade
  takes the casters in its own orthographic box (its planes, from the light matrices `WorldRendererShadows_PrepareFrame`
  builds once per frame), routed by their shadow tier; the cube shadow takes the casters touching its lamp's reach.
  A model casts when it has `casts_shadows`; a sprite when it is a cutout sprite that is not a decal and casts
  (`WorldRenderSprite_CastsShadows`).
- **Draw distance.** Each model has its own since T10 and each sprite since T26 (decision G17,
  [WorldModels.md](WorldModels.md), [WorldSprites.md](WorldSprites.md)): its world model's or world sprite's
  `draw_distance` (a world sprite without one: its kind's, 150 m or 60 m for a decal, decided per placement), or its
  placement's own while `overrides_draw_distance` is on, times the placement's `draw_distance_multiplier` (the cache
  resolves it); then times the config's `draw_distance_scale` (default 1, [0, 16]) unless the asset or the placement
  keeps it fixed (`WorldVisibility_ScaleDrawDistance`). The distance measured is the camera's to the nearest point of
  the entry's sphere (0 inside it), so a large object does not fade while its near end is close, multiplied by the
  **zoom factor**, `WorldVisibility_GetZoomFactor(fovY)` = tan(fovY / 2) / tan(30 degrees)
  (`WorldVisibilityView.ZoomFactor`, from the camera's field of view): exactly 1 at the default 60 degrees, below 1 when
  zoomed in, so a zoomed view (binoculars, a camera) keeps far models and sprites instead of fading them. T11's LOD
  selection uses the same factor. Beyond its draw distance an entry is in no list (its shadow goes too); over the last
  stretch (10% of the distance, at least 3 m, at most all of it) its distance fade rises linearly from 0 to 1.
- **Opacity.** The tint's opacity (the alpha byte of the final tint) asks for a fade of 1 - opacity, and a draw's fade is
  the larger of that and the distance fade. An entry whose fade is 1 is in no list. Models no longer use alpha blending:
  the PBR output alpha is 1 and their opacity is the dither (`// TODO:` in the PBR shader: alpha-tested material
  textures, leaves on a model, need a material alpha mode).
- **How a fade shows.** Models and cutout sprites dither by their fade, in every pass that draws them (the scene, the
  crisp overlay, both cascades and the cube shadow), so a fading object's shadow thins with it. Blended and additive
  sprites are transparent already: their draw-distance fade scales their alpha (their opacity is in it already) and they
  do not dither. The G-buffer never dithers (holes would break the outline into noise and speckle the AO): it draws an
  entry solid while its fade is below 0.5 (`WORLD_VISIBILITY_GBUFFER_FADE_LIMIT`) and skips it from there on, so the
  outline and the AO switch off once, at mid-fade. The outline pass reads the G-buffer's depth for the same reason.
- **The dither** (`shaders/common/dither.glsl`, included by every world geometry shader: `world_pbr`, `world_normal`,
  `world_depth`, `world_cube_depth`, `world_sprite`, `world_normal_sprite`, `world_sprite_depth`,
  `world_sprite_cube_depth`, `world_terrain`; each tests `DitherDiscard()` once). Uniforms `ditherFade` (this draw's fade: 0 solid .. 1
  gone), `ditherLodProgress` and `ditherIsIncomingLod` (its part of a LOD cross-fade, T11: the share of the pixels the
  incoming LOD takes, and whether this draw is that LOD; 0 and false for every other draw) and `ditherPixelScale` (the
  pass's pixels per scene pixel: 1 in the scene-resolution passes and in the shadow maps, on their texel grid; window /
  scene in the crisp overlay), uploaded per draw (once per LOD for models: its meshes share it);
  `WorldRendererCommon_PrepareDither` caches them and uploads the defaults at load, `WorldRendererCommon_SetDither` sets
  all of them for every draw. The formula, which `WorldVisibility_GetDitherCell`, `WorldVisibility_GetDitherThreshold`
  and `WorldVisibility_IsDitherDiscarded` mirror in C for the tests:

  ```
  BAYER (rows from the target's bottom row, gl_FragCoord's origin):
       0  8  2 10
      12  4 14  6
       3 11  1  9
      15  7 13  5
  cell       = floor(gl_FragCoord.xy / max(ditherPixelScale, 1)), each coordinate mod 4
  threshold  = (BAYER[cell.y][cell.x] + 0.5) / 16
  keptByFade = threshold > fade
  keptByLod  = isIncomingLod ? (threshold <= lodProgress) : (threshold > lodProgress)
  discard    = !(keptByFade && keptByLod)
  ```

  A fade of k/16 keeps 16 - k pixels of every 4x4 block (half a fade is a checkerboard). The two LODs of a cross-fade
  compare the same progress from both sides, so they keep exactly complementary pixels at every progress (the incoming
  one k of 16 at k/16), and the draw's fade removes its pixels from both alike: the object as a whole shows its fade,
  each of its pixels drawn by one LOD. (T07 had planned the incoming LOD as a mirrored pattern at fade 1 - p; computing
  1 - p in floats drew a few pixels twice right next to the pattern's thresholds, so T11 compares the progress itself.)
  In the crisp overlay every window pixel of one scene pixel gets that scene pixel's cell, so its dither sits on the same
  pixel grid as the rest.
  Shaders that take derivatives or sample mipmapped textures test it after those (the PBR shader last), so they stay
  defined for the pixels that are drawn.
- **Terrain chunks** (B01) enter every list as models do, each by its own sphere: the camera's frustum, each
  cascade's box by the terrain's shadow tier (when it casts), the cube's reach, the draw distance (4 km and fixed by
  default, so the quality setting never thins the hills; the zoom factor applies) and the fade, which their draws
  dither (and the G-buffer's limit) like a model's. Their lists are their own
  (`WorldVisibility_GetTerrainChunks(visibility, cache, pass)`, the fades `WorldVisibility_GetTerrainChunkFade`), and
  their counts too: a chunk is not counted as a model.
- **Instance cells** (B03) enter every list as models do, each by its sphere around all its copies: the camera's
  frustum, each cascade's box by the component's shadow tier (when it casts), the cube's reach, the draw distance
  measured to the sphere's nearest point (the quality setting scales it unless fixed; the zoom applies) and the opacity
  (the object's: each copy's own alpha fades only that copy, in the vertex stage). A cell no copy of which can be seen
  leaves every list; inside one, every copy fades and picks its LOD on its own (see "Model instances"). Their lists and
  fades are their own (`WorldVisibility_GetInstanceCells`, `WorldVisibility_GetInstanceCellFade`), and their counts too
  (`DrawnInstanceCellCount`, `DrawnInstanceCount`: the copies of the drawn cells, `InstanceCellsOutsideViewCount`,
  `InstanceCellsFadedOutCount`).
- **A/B.** `WorldRenderer_SetCullingEnabled` (U in the test frame) switches the camera's frustum and the cascades' boxes
  off: everything within its draw distance is drawn, as before culling existed, and the image must not change. The
  draw distances, fades and the cube's reach stay.
- **Status.** The status line (after the first render, the I key) gives what the camera's passes drew (models, sprites,
  how many of them fading; the models by LOD and how many cross-fade), how many entries lay outside the view, and how
  many faded out (beyond their draw distance or fully transparent); since B01 the terrains placed (and those without a
  terrain), the chunks drawn of all listed and their triangles, and whether the chunk key is on; since B03 a line of
  its own for the model instances: the components (and those without a world model), their copies and cells, the cells
  drawn with their copies and how (instanced, or one at a time), the cells outside the view and faded out, and the
  camera's copies by the LOD they show (the larger share), how many sit in a band and how many faded out, worked out
  with the C mirror when the line is logged (never per frame).
- **Known limits.** The cube shadow of a fading object is grainy: its 20-tap PCF spreads over about 17 texels and samples
  the dither pattern unevenly (the cascades' 2x2 PCF always takes one texel of each quarter of the pattern, so their
  shadows thin smoothly). Without reversed depth the far plane is extracted from a float matrix, about 10 m uncertain at
  4 km.

### LODs (T11)

Every model draws one of its world model's LODs ([WorldModels.md](WorldModels.md), "LODs"), picked each frame by
`WorldRenderCache_UpdateLods` (right after the frame is built, before the visibility) with the pure maths of the private
`source/WorldLod.{h,c}` (tested in `tests/WorldLodTests.c`):
- **Selection** (`WorldLod_Select`): the camera's distance to the entry's bounding-sphere centre (in render space: the
  length of `RenderCenter`) times the zoom factor,
  against the placement's switch distances in effect (`WorldModelComponent_ResolveLodSettings`) times the config's
  `lod_distance_scale`: LOD i below switch distance i, the last LOD past the last boundary. From the LOD it shows (or
  heads to, `WorldLodState_GetDestinationLod`) an object moves coarser only at a switch distance x (1 + h) and back only
  below x (1 - h), h the world model's `lod_hysteresis`. A forced LOD is taken as it is. Chosen afresh (no hysteresis)
  for an entry's first frame.
- **The cross-fade** (`WorldLodState`, per cache entry: `CurrentLod` the LOD a fade started from, `TargetLod` the one it
  started toward, `Progress` the second one's share, `IsReturning`): a new LOD starts a fade that moves by the frame's
  real seconds over the world model's `lod_fade_seconds` (0 swaps at once; a frame without time holds it). Changes
  during a fade never pop back: when the selection returns to the LOD the fade started from (the camera turns back
  within the fade, Z pressed twice), the fade runs back (`IsReturning`, the share falls) and every pixel keeps its LOD
  at the turn; a third LOD keeps the LOD with the larger share and fades from it to the new one (the other LOD's pixels,
  at most half, switch at once; never three LODs). An entry that is not drawn (no world model, hidden or disabled)
  starts afresh, so it appears at its LOD at once when drawn again; entries outside the view or beyond their draw
  distance still update (their shadows use their LOD).
- **In the passes:** the scene and the crisp overlay draw both LODs of a fade (`WorldLodState_GetDraws`) with
  complementary dither masks (see "Visibility", the dither): every pixel is drawn by one of them and the background
  never shows; the object's lights are uploaded once for both. The G-buffer draws the LOD with the larger share
  (`WorldLodState_GetSolidLod`: the incoming one from half the fade on), solid, as T07's rule draws a fading object
  solid; the shadow cascades and the cube shadow draw the LOD the object heads to (`WorldLodState_GetShadowLod`), solid
  (only the object's own fade dithers them), so a shadow changes once per change and never shimmers. Without the PBR
  shader (raylib's default shader, no dither) the larger share is drawn alone.
- **LOD tinting** (`WorldRenderer_SetLodTintingEnabled`, Q in the test frame): every LOD's debug colour multiplied into
  its albedo (LOD 0 white, i.e. its own colours, 1 green, 2 yellow, 3 red, coarser magenta), and LODs past 0 glow
  faintly in it (`LOD_TINT_GLOW_INTENSITY`, a linear emission: the eye's night vision greys reflected colour, not
  emitted light, so the colours read at night too). Off by default; the status line says when it is on.
- A placement whose own LOD list no longer fits its world model gets the world model's distances and one warning, logged
  when that starts: `WorldRenderer: object 12 ("lamp") uses its world model "street_lamp"'s LOD distances: its own
  lod_distances holds 2, the world model has 3 LODs.`
- **Not built:** impostors (a sprite as the last LOD; a `// TODO:` in `WorldModel.h`). The renderer keeps one LOD state
  per model, for its one camera: a second view of the same world (a mirror, a security camera) would need its own.

### Model instances (B03)

A `model_instances` component ([WorldData.md](WorldData.md)) places many copies of one world model, each with its own
position (double, relative to the object), rotation, scale and tint; it shares the model component's settings (every
copy has them) except `omit_pixelation` (copies are never crisp). The renderer draws all the copies of a **cell** at
once, each mesh of each LOD in one instanced draw, so tens of thousands of copies cost a few dozen draws.

- **Cells** (`WorldInstancing.h`, built by the render cache): the copies are sorted into squares of
  `WORLD_INSTANCE_CELL_SIZE` (32 m) of the object's own X and Z. A cell's centre is the middle of its square, and the
  middle of its copies' heights; each copy's **record** (`WorldInstanceRecord`, 56 bytes) holds the first three rows
  of its matrix relative to that centre (scale, the Euler XYZ rotation, the position minus the centre: composed in
  double, rounded once), its tint and a stable random value (a hash of the object's id and the copy's index, for B10's
  wind). The cell's sphere holds every copy's sphere (the world model's under the copy's matrix). Drawing a cell takes
  one matrix, the object's world matrix times the translation to the cell's centre in render space, so the camera's
  movement never touches the records (T32): 141 km out the copies stand where they stand at the origin, to the pixel.
- **GPU buffers** (`WorldRendererInstances_PrepareFrame`): one vertex buffer per build of an entry's cells, uploaded
  once (`rlLoadVertexBuffer`) and kept while the entry holds that build, hidden or not; a cell is a range of it. A new
  build replaces the buffer; buffers no entry holds are unloaded, all of them at `PrepareWorld`.
- **Per copy, in the vertex stage** (`shaders/common/instancing.glsl`, whose C mirror is `WorldInstancing.c`): a copy is
  measured as an object is, its LOD by the camera's distance to its sphere's centre and its fade by the distance to the
  sphere's nearest point, both times the zoom factor. Its fade is T07's (the last band of the draw distance in effect)
  against 1 - its opacity (the object's times its tint's alpha). **LOD changes are distance bands**, not T11's timed
  cross-fades (100 000 copies cannot keep a state each): around each switch distance s (times `lod_distance_scale`), from
  s(1 - h) to s(1 + h) with h the world model's `lod_hysteresis`, the incoming LOD's share rises linearly
  (`WorldInstancing_GetLodBands`; a band stops at the middle between two switch distances, so bands never overlap), drawn
  with T11's complementary dither. A standing camera never changes the picture. The fade and the band reach the fragment
  stage as flat varyings: with `WORLD_INSTANCED` defined (the twins' definitions), `common/dither` reads them instead of
  its uniforms, through the same formula.
- **The passes' rules, per copy** (`WorldInstancing_GetDraw`, as T07 and T11 set them for objects): the lit pass draws
  both LODs of a band, each with its share of the dither, and the copy's fade; the G-buffer the LOD with the larger
  share (the incoming one from `WORLD_LOD_SOLID_SHARE` of the band), solid, until mid-fade; the shadow passes that LOD,
  dithered by the copy's own fade; the unlit fallback that LOD, solid. A copy with a fade of 1 is drawn by none.
- **Which LODs a cell draws:** only those its copies can show this frame (`LodMask`: the bands met by the cell's range
  of zoomed distances, widened a little for the GPU's rounding). The draw of LOD k keeps a copy only if it shows LOD k;
  the others get every vertex at one point outside the clip volume, so they make no fragments.
- **The draw** (`DrawMeshCopies`): raylib's `DrawMesh` (its GL 3.3 path: the material's colours, the matrices with the
  cell's as the model matrix, the maps on their units), with the mesh's vertex array bound and the cell's records as
  instanced attributes (divisor 1) at locations 9 to 13 for `rlDrawVertexArrayElementsInstanced`; then the attributes
  are taken off the vertex array again (disabled, divisor 0, pointing at no buffer), so the mesh's ordinary draws are
  untouched. raylib's `DrawMeshInstanced` is not used: it uploads every matrix into a new buffer on every call. The
  per-draw material copy is `WorldRendererGeometry_PrepareMaterial`'s (the variant, the LOD tinting, the PBR factors),
  the LOD's import transform goes in as `instanceLodTransform`, and the copy's tint multiplies its vertex colour.
- **Lights:** a cell takes up to `WORLD_MAX_FORWARD_LIGHTS` forward lights by its sphere, as an object does.
- **The A/B and the fallbacks** (`WorldRenderer_SetInstancingEnabled`, the semicolon in the test frame): off, every pass
  draws each copy on its own through its ordinary shader (`world_pbr`, `world_normal`, `world_depth`,
  `world_cube_depth`) with the copy's values from the C mirror as uniforms. A pass whose twin is missing, an entry
  whose buffer raylib could not make (`rlLoadVertexBuffer` gives 0 only without a GL context), and the unlit fallback
  (no PBR lighting: raylib's default shader, solid) draw so too, each logged once. An upload the GPU runs out of
  memory for is not caught (`glBufferData`'s `GL_OUT_OF_MEMORY`: raylib checks none of its uploads' errors, meshes and
  textures alike, and only `GLTextureArray` may call GL itself); the copies then draw from a buffer without storage. **The picture is the same up to float rounding** (measured, B03's status block): the instanced
  path places a vertex by the record's rows and then the cell's matrix, the other by their product, and normals by
  cofactors against an inverse transpose; without shadows and post effects some views are bit-identical and others
  differ in a few pixels where two triangles meet; with them, shadow tests and AO round differently (most pixels by 1
  of 255); a copy tint other than white also rounds (an integer multiply into the material colour, against a float
  one in the shader).
- **Costs** (B03, GTX 980 Ti, the start of the pebble field, a 1280 x 720 frame to a `glFinish`): 3.85 ms without
  copies, 4.98 ms with the grove's 400 trees, 6.40 ms with the 20 000 pebbles and a lamp casting a cube shadow among
  them; one at a time, 212.6 ms. The instanced lit pass's own GPU time is 0.67 ms there.
- **Not built** (out of B03's scope): GPU culling (a cell is all or nothing for the frustum), collision for copies (B06
  and B09 add them as a static source), scattering rules (B09), grass and wind (B10: the random value is in every record
  and unused). Copies never skin (a skinned world model's copies draw rigid, in the bind pose) and never draw crisp.

### Pipeline (one `WorldRenderer_RenderToTarget`)

1. Sync the cache for the frame's origin (the camera's position: every entry in render space); upload the terrain
   chunks that are new or changed (`WorldRendererTerrain_PrepareFrame`, B01) and the copies' new builds of cells
   (`WorldRendererInstances_PrepareFrame`, B03); make sure the targets match the window size and pixelation setting
   (`WorldRendererScene_EnsureTargets`).
2. Build the frame context (`WorldRenderFrame`): the origin, the render camera, the zoom factor, the scene's
   rotation-only view and projection matrices (`WorldRenderSpace_BuildView`: the reversed infinite projection the camera's
   passes load in place of `BeginMode3D`'s where the GPU has clip control (T39, see "Render space"), otherwise the one
   `BeginMode3D` builds for the scene target; shared by every pass that reconstructs positions or rays) and the camera's
   frustum from them (without a far plane when reversed), the
   sky
   (`WorldEnvironment_GetSkyLights`: the sun's and the moon's directions, daylight, the moon's phase and lit fraction,
   the two lights' intensities above the air), the stars' rotation (`StarsToWorld`), the render
   clock (`TimeSeconds`), sizes, the night sky values (`WorldRendererSky_PrepareFrame`: skyglow, the moonlit sky's
   strength and the moon's disc; after the sky's light, `WorldRendererSky_PrepareNightSky` (T38): the limiting magnitude
   and the Milky Way's radiance; first the star catalogue is rebuilt if the
   world's seed or density changed, and the sky textures reloaded if the world names others), the atmosphere's
   (`WorldRendererAtmosphere_PrepareFrame`, T15: the air's coefficients, the height the sky is seen from, whether the
   tables draw the sky; T35: the frame's haze, light pollution's light in the air, whether the volume hazes the frame),
   the sky's light (`WorldRendererLighting_PrepareSkyLight`, T36, see "Sky's light on surfaces": the C mirror's tables
   kept on the air, the light at the camera, the light the air is lit by, the sun's disc, the scene luminance, night
   vision's field and calibration, and the shadow light chosen from the light through the air), every model's LOD and
   its cross-fade (`WorldRenderCache_UpdateLods`, see "LODs"), and every
   pass decision:
   first which shadow passes run (`DecideShadows`) and their light matrices (`WorldRendererShadows_PrepareFrame`: both
   cascades' view, projection and box, snapped to their texels; the cube shadow's lamp and far range), then the frame's visibility (what every
   pass draws, see "Visibility"), then the rest (`DecidePasses`; the crisp overlay runs only for crisp objects in view).
3. Lighting values for the frame (`WorldRendererLighting_PrepareFrame`): the shadow strength and the forward-light
   candidates (every light except the cube-shadow light) with their linear radiance.
   Then the eye's night vision for the frame (`DecideNightVision`: its strength, the field luminance, the rods' grey),
   and each sprite's matrices (`WorldRendererSprites_PrepareFrame`: facing the camera, facing the cascades' light and the
   cube shadow's lamp while those run) with the blended and additive sprites sorted back to front.
4. The atmosphere's tables (T15, `WorldRendererAtmosphere_Render`, see "Atmosphere"): the transmittance and multiple
   scattering tables when the air changed, the sky's irradiance table after them (T36), then the sky view (the sun's
   half, and the moon's while the moonlit sky shows), then the aerial-perspective volume (T35, see "Haze") with the
   sky's light by height in its last row (T36; only that row while the haze is off); the tables left bound on units 7
   and 8. Then the shadow cascades of the shadow light (the sun or the moon), then the
   point-light cube shadow, each drawing its
   visibility list (the casters in its box; within the light's reach). Models (the LOD they show or fade to), the
   model instances' copies (B03: each the LOD with the larger share, dithered by its own fade), terrain chunks (B01;
   their back faces in the cascades, as the models') and cutout sprites cast; sprites turned toward the light; each
   caster dithered by its fade.
5. The normal/flag G-buffer (the camera's models, the LOD with the larger share, the copies the same way, terrain
   chunks and cutout sprites, solid below half their fade) and the AO pre-pass, when AO or outlines run.
6. The scene pass: clear, atmospheric sky (the sky-view table's glow, with the moonlit sky and light pollution's
   light, and the far ground below the horizon; the ground haze in front of it all; the moon's disc, the Milky Way), the
   stars (none behind the moon), debug grid, the camera's PBR models (each dithered by its fade, both LODs
   of a cross-fade with complementary dithers; the sun and the moon; per object: up to
   `WORLD_MAX_FORWARD_LIGHTS` reach-culled point lights; per material: PBR factors and map flags), the camera's
   instance cells (B03: lit like the models, the lights per cell, both LODs of a copy's band), the camera's terrain
   chunks (B01: the `world_terrain` shader, lit as the models, with up to `WORLD_MAX_FORWARD_LIGHTS` lights per chunk;
   before the sprites, so decals lie on it), the camera's lit sprites
   (the cutout ones, then decals, then blended and additive ones back to front), light gizmos, and collider wireframes
   and contact points while a collision world is set (T12, T27). The sky's glow and the models' and lit sprites' reflected light go through the
   eye's night vision; every lit surface is seen through the haze in front of it. Then, while one is chosen, the haze's
   debug view over the scene (`WorldRenderer_SetHazeView`, see "Haze").
7. Outlines, then bloom and sun shafts from the (outlined) HDR scene.
8. Eye adaptation, then the tonemapped point-upscale into the frame target.
9. The crisp overlay for OmitPixelation models and cutout sprites, composited over the finished image.

### Effects

**Pixelation.** When enabled (default), the scene target is sized to the low pixel resolution (longer window
axis pinned to 640, shorter derived → square pixels; 16:9 = 640x360) with point filtering, and point-upscaled to
the window. The scene→frame blit uses a **negative source height** (the flip convention every render-texture hop
uses). Toggle with `WorldRenderer_SetPixelationEnabled`.

**HDR.** The scene target is RGBA16F (built with rlgl; `LoadRenderTexture` is 8-bit only), with three tiers:
HDR + samplable depth (everything available), HDR + depth renderbuffer (AO, outlines, sun shafts and the crisp
overlay unavailable), 8-bit (last resort). The tier is logged once. Everything is composited in linear HDR and
tonemapped (ACES + gamma, `world_tonemap`) only in the final blit, after the pixelation upscale.

**PBR.** `world_pbr` (vertex + fragment pair): Cook-Torrance GGX with two directional lights, the sun and the moon
(one `DirectionalLight` function; since T36 their light through the air at the fragment's height), the sky's own
light as the ambient (T36: the sky's irradiance from above, the ground's bounce from below, the moonless night's own
light; see "Sky's light on surfaces"), forward point lights, per-material metallic/roughness/occlusion/emissive factors and
optional albedo / normal / packed ORM (in the METALNESS slot) / emissive maps from the model definition's
`material_overrides`, derivative-based normal mapping, and the haze in front of every surface (T35's aerial
perspective and ground haze, see "Haze": `ApplyAerialPerspective`, which replaced T34's distance fog). The lighting
itself (the frame's uniforms, the BRDF, the sky's lights with the cascades' shadow, point lights with the cube shadow,
the AO, the night vision and the haze) lives in
`shaders/common/lighting.glsl`, which the sprite shader includes too: the renderer prepares and feeds every lit shader
the same way (`WorldRendererLighting_PrepareLitShader`, `WorldRendererLighting_UploadFrame`). T06 moved it there without
changing a bit of the PBR image (checked against the previous build on two drivers). Without it models draw unlit with
raylib's default shader.

**Terrain materials** (B02, [Terrain.md](Terrain.md)). `world_terrain` blends up to sixteen terrain materials per pixel:
the painted weights (a face material's faces outright), the terrain's rules (slope and height in the world, noise in the
terrain's space), height blending, and the maps sampled from texture arrays (planar, or triplanar on steep ground)
in the terrain's own space, the normal maps reoriented onto the surface without mesh tangents and turned into the world;
lit as `world_pbr`'s dielectric with the map's roughness and AO, through the night vision. The arrays are made once per
terrain from the materials' pixels and sit on units 9 to 11; without them (no GL support, logged once) each material
shows its mean colour. The C mirror (`TerrainBlend.h`) gives the same answer on the CPU. The equals key cycles the views
(shaded, painted weights, rule weights, the dominant material).

**Sprites** (T06; since T26 their settings are their world sprite's, each overridable per placement:
[WorldSprites.md](WorldSprites.md), [WorldData.md](WorldData.md)). Every sprite is one unit quad
(`WorldRendererSprites.c`: x and y in [0, 1] from the image's bottom left, z 0, normal +Z; six unindexed vertices,
because raylib's `DrawMesh` draws indexed only while the CPU index array is kept) drawn with `DrawMesh` and a matrix from
`SpriteTransform`, with a material built on the stack (the frame's texture as the albedo map, the tint as `colDiffuse`).
- **Shape.** The frame's size in pixels divided by `pixels_per_unit`, times the object's X and Y scale; the pivot lands
  on the object's position and the flips mirror the image about it. The frame's area in its texture reaches the vertex
  stage as `uvRect`, so sprite-sheet frames work.
- **Orientation.** A `billboard` faces the camera's position (its normal points at the camera, the image's top leans
  toward the camera's up); an `axis_billboard` turns only around the object's local Y axis; a `fixed` sprite lies in the
  object's local XY plane facing +Z. In a shadow pass the light is the viewer: billboards face the sun or the moon along
  its rays (with world up as their up) or the cube shadow's lamp, axis billboards turn around their axis toward it, fixed
  sprites stay as they are. So a tree's shadow does not turn with the camera. The matrices (camera, cascades' light,
  cube's lamp) are made once per frame and shared by every pass.
- **Lighting** (`world_sprite`, `#include "common/lighting"`): a rough dielectric (roughness 0.9, metallic 0, occlusion
  1) lit by the sun, the moon, the forward point lights (reach-culled per sprite on the sphere around its pivot), the
  cube-shadow lamp, the ambient and the haze, its reflected light through the eye's night vision. Fixed sprites and decals
  are two-sided cards lit with their normal turned toward the viewer. Billboards and axis billboards face the camera
  and stand in for round things, so their diffuse light wraps: (n.l + w) / (1 + w) with w their `light_wrap` (default 1:
  n.l x 0.5 + 0.5), half lit from the side and dark only from straight behind; the specular keeps n.l.
- **No self-shadowing.** A billboard's shadow-map entry faces the light while the visible quad faces the camera; the two
  cross at the pivot, and half of the visible quad would lie in its own shadow. So the cascades and the cube are read on
  the light-facing quad, at the point matching each fragment (`matShadowModel`, `matPointShadowModel` in the vertex
  stage): the sprite is shadowed by everything else where its own entry stands, never by itself.
- **Unlit** sprites (`is_lit` false) show their colours times `emission` as emitted light: no night vision, still
  hazed.
- **Alpha modes.** `cutout` discards texels whose alpha is below `alpha_cutoff` (the texture's alpha; the object's opacity
  is the dither, see "Visibility") and writes opaque colour and depth. `blend` writes premultiplied colour (blended ONE,
  ONE_MINUS_SRC_ALPHA, so the scene's alpha stays 1) with the texture's alpha times the object's opacity. `additive`
  writes premultiplied colour with alpha 0 (added ONE, ONE), dimmed by the haze's transmittance alone (an emitter seen
  through haze: the haze's own light is already in what lies behind). A blended sprite's premultiplied haze composites
  exactly over the haze of what lies behind it. Neither of the two writes depth.
- **Order** in a lit pass (`WorldRendererSprites_DrawLit`, after the opaque models): the cutout sprites; then the decals
  (depth tested, not written; blended as their alpha mode says; in world order); then the blended and additive sprites,
  farthest first by view depth (equal depths keep world order), so each blends over everything behind it, the others
  included. Culling is off for sprites (two-sided, and one flip mirrors the quad).
- **Decals** (`is_decal` with the fixed orientation): the vertex stage lifts the quad `decal_offset` metres along its
  normal toward the side the camera is on (raylib has no polygon offset). No shadow, no outline, not in the G-buffer;
  the AO and the shadow they get are their surface's.
- **Shadows.** Cutout sprites that are not decals and have `casts_shadows` draw into both cascades and the cube shadow
  through alpha-tested depth shaders (`world_sprite_depth`, `world_sprite_cube_depth`), so the shadow has the sprite's
  shape.
- **G-buffer.** Cutout sprites that are not decals (`world_normal_sprite`: alpha-tested at their cutoff, their quad's
  view normal turned toward the camera) get outlines and AO like models; decals, blended and additive sprites stay out.
- **Crisp.** `omit_pixelation` draws a cutout sprite that is not a decal in the crisp overlay; decals and blended or
  additive sprites stay pixelated (the overlay composites opaque surfaces with depth only).
- **A/B.** `WorldRenderer_SetSpriteLightingEnabled` (K in the test frame) draws every sprite unlit at its full colours,
  the look sprites had before T06. Without the sprite shader or the quad no sprite is drawn (logged once; status
  "sprite shading" unavailable).

**Sky.** `world_sky`: the atmosphere (`shaders/common/atmosphere.glsl`) lit by the sun and, at night, by the moon: since
T15 the sky-view table's glow by direction (single and multiple scattering, with light pollution's skyglow; see
"Atmosphere"), or, with the tables off (the A/B, the fallback), T34's march of single scattering per pixel; the sun disc
(dimmed and reddened by the transmittance table since T15: orange as it sets; since T36 at a radiance of its own,
`SUN_DISC_RADIANCE` 22 x `sun_color` x `sun_intensity`, scaled against the exposure: "Sky's light on surfaces"), the
moon's disc, the Milky Way, drawn as
a full-screen pass before the geometry (it writes no depth). Each pixel's ray comes from the inverse of the frame's view-projection,
so the sky lines up with the 3D camera; in render space that matrix is a rotation and a projection, so the ray is exact
at any distance from the world's origin (see "Render space"). Falls back to a CPU gradient clear. Its textures are the ones the world's
environment names (`MilkyWayTexture`, default `sky_milky_way`; `MoonTexture`, default `sky_moon`): each sky texture
slot holds its texture under its own asset user, loads it when the environment names it (at `PrepareWorld` or the
first render after a change) and releases it when the environment names another; a name that fails is logged once
and not retried until the name changes (`WorldRendererSky_SyncTextures`).

**Atmosphere** (T15, decisions G36 and G37; `source/WorldRendererAtmosphere.c`, the C mirror `source/WorldAtmosphere.h`,
the GLSL `shaders/common/atmosphere.glsl`). The sky's light is computed once per frame into lookup tables (Hillaire, "A
Scalable and Production Ready Sky and Atmosphere Rendering Technique", EGSR 2020) that the sky pass, the stars and,
through the aerial-perspective volume built from them (T35, "Haze"), every lit surface read.
- **The air** (`WorldAtmosphere_GetMedium`, from the environment: [WorldData.md](WorldData.md), "Environment"): the air's
  Rayleigh scattering (5.5, 13.0, 22.4)e-6 per metre at sea level x `air_density`, scale height 8 km; the haze's
  extinction 23.1e-6 x `sky_turbidity` / 3, of which it scatters `haze_albedo`, scale height `haze_height`, forward
  scattering `sun_glow` (Cornette-Shanks); the ozone layer's absorption (0.650, 1.881, 0.085)e-6 at its peak x `ozone`, a
  tent from 10 to 40 km. A planet of 6371 km under a 100 km atmosphere. Every reader gets the same uniforms
  (`WorldRendererAtmosphere_UploadFrame`), so the A/B march uses the same air (with the ozone too).
- **The tables**, RGBA16F, bilinear, clamped (`WorldRendererAtmosphereState`), drawn with blending off (every texel as
  computed: the default blending would keep an old NaN for good on drivers that blend by IEEE rules, Mesa's):
  - *transmittance*, 256 x 64: the share of light that crosses the air from a height toward a zenith angle to the top
    (Bruneton's parameterization: rows by the distance to the planet's axis, 25 m apart near the ground; 40 steps each);
  - *multiple scattering*, 32 x 32: the light scattered twice or more toward a point, by its height and the light's
    zenith angle, per unit of scattering (64 directions, 20 steps each, the lit ground's reflection, the geometric series
    L2 / (1 - f_ms));
  - *sky view*, 192 x 216 (two halves of 192 x 108): the light the sky sends toward the camera by direction, the sun's
    half (with light pollution's light in the air, "Night sky") built every frame, the moon's half while the moonlit
    sky shows (`MoonSkyIntensity` above 0, as before), each lit by its light above the air on the surfaces' scale (T36:
  `ATMOSPHERE_SUN_LIGHT` x `sun_intensity` x `sun_color`, before T36 22 x `sun_intensity`); azimuth from the half's light across (the sine of half the angle:
    0.6 degrees a texel next to the light), zenith angle up, quadratic toward the horizon (from 1.7 m the two rows next
    to it are 0.008 degrees from it); 32 steps growing with the square of their index, each step's light integrated
    exactly over its own air, the light through the transmittance table and the planet's shadow (soft over the sun's
    disc), plus `multiple_scattering` of the multiple scattering table's glow; the in-scatter tinted by `sky_tint`.
    **Below the horizon** (T35) a ray ends on the *far ground*: the planet's surface at the atmosphere's ground
    (`horizon_ground_height`), seen through the haze in front of it (that path scaled by the frame's haze like every
    surface's, through the ground where the scale takes it past it). The ground is `sky_ground_albedo` lit as the lit
    shaders light a dielectric facing up (T36): the half's light through the air at the far point of the curved planet,
    at the light's zenith cosine there (the transmittance with its disc's share above the horizon), times the cosine to
    it, plus the sky's irradiance at the ground (the irradiance table, with light pollution's in the sun's half) times
    the ambient's tint, plus the moonless night's own light in the sun's half. So a far point the sun has set on is lit
    by the sky alone, and the far ground meets a terrain's edge without a seam. It replaced T15's darkened mirror.
  - *irradiance* (T36), 65 x 32: the sky's irradiance on a surface facing up per unit of a light above the air, by the
    light's elevation and the height, and light pollution's in its last column ("Sky's light on surfaces").
  The transmittance and multiple scattering tables are built again only when their air changes
  (`WorldAtmosphere_UpdateTableRecord`, with `WorldAtmosphere_AreTablesStale`: not for `sun_glow` or
  `multiple_scattering`, which only the sky view and the irradiance table use; the irradiance table follows those too:
  `WorldAtmosphere_UpdateIrradianceRecord`). A rebuild costs 0.47 ms on a GTX 980 Ti, so whatever moves the
  air every frame (T16's weather) should step it. A light far brighter than the sun saturates the sky view at its half
  floats' largest value (65504) instead of turning infinite there; a haze too thick to see through gives no light, never
  a NaN (each step's share of its own light is selected, not blended, near 0 depth).
- **The camera's height**: the tables see the sky from the camera's height above the atmosphere's ground (world Y minus
  `horizon_ground_height`, default 0: sea level at world Y 0), at least 1 m and at most 99 km
  (`WorldAtmosphere_GetViewHeight`), from the frame's origin in double: flying up darkens the zenith and drops the horizon
  (1.44 degrees at 2 km). The direct march starts there too.
- **Precision**: a float holds a radius of 6.4e6 m only to half a metre, so heights are carried as heights above the
  ground (r^2 - R^2 as h (2R + h), never a radius minus the planet's), in C and in GLSL alike. The GPU's `acos` (NVIDIA)
  is off by about 6.5e-5 rad near the horizon, so the sky view's rows there hold directions about 0.004 degrees lower
  than the C mirror's; the readers' lookups make the same error, so a direction still finds its own row (a tenth of a
  pixel).
- **The readers**: the sky pass (the glow and the far ground by direction, both halves; the discs and the Milky Way
  through the transmittance toward them, the Milky Way and the stars relative to the zenith's, where their brightness is
  tuned; the discs only where a ray misses the planet, never over the far ground), the stars' vertex stage (their
  magnitude and colour), and the volume's builder. The tables sit on fixed units
  for their readers (7 the transmittance, 8 the sky view: "Renderer structure"), bound by
  `WorldRendererAtmosphere_BindTables` after the pass and again before each reader; the builders take their inputs
  through the 2D batch.
- **A/B and fallback**: `WorldRenderer_SetAtmosphereTablesEnabled` (keypad 1 in the test frame) switches the sky to the
  direct march (with T15's darkened mirror below the horizon and T02's skyglow), which is also what runs when a table
  shader or target is missing (logged once); nothing is hazed then (the volume is built from the tables). The status's
  "atmosphere tables" feature says which; `WorldRendererAtmosphere_LogStatus` logs the sizes, the build count, the height
  the sky is seen from and the render's CPU time. TODO in the code: remove the switch and the march once approved.
- **Accuracy and cost** (T15's and T35's status blocks have the measurements): the sky-view table, sampled as the
  shaders sample it, is within 0.2 to 0.4 % on average of single scattering with 1024 steps and an exact light path at
  the default haze with the sun above 4 degrees (multiple scattering off; 0.41 % over every sun height from -4 to 85
  degrees against a soft-shadowed reference, 7.6 % at worst at the moment of sunset, where the darkened mirror gave
  19 %), the transmittance table within 0.5 % of an exact optical depth; the sky pass costs 0.024 ms instead of 0.215 by
  day, the tables 0.06 ms (0.11 at night with the moon's half).

**Haze** (T35, decisions G36 and G37; the pure maths in `source/WorldAtmosphere.{h,c}`, the readers' GLSL
`shaders/common/haze.glsl`, the volume's builder `shaders/atmosphere/volume.frag`). A surface's light reaches the camera
dimmed by the air in front of it (the transmittance) plus the light that air scatters toward the camera (the
in-scatter), from the same atmosphere as the sky along the real path: at the horizon that is exactly the sky, so far
things still dissolve into it; nearer, a hill ends as a flat, pale silhouette, never see-through; dark things turn
blue, bright ones warm. It replaced T34's distance fog (`fogSkyColor`, `FogFactor` and `BASE_FOG_DENSITY` are gone).
- **The aerial-perspective volume** (Hillaire 2020, section 5.6): for 32 x 32 screen directions of the scene camera, at 32
  slices, the in-scatter (RGB, the sky's light units) and the RGB transmittance of the path so far. The slices' distances
  grow by a fixed ratio (1.50) from 1 m to 200 km, 21 within 4 km, and the last holds the whole ray to the
  top of the atmosphere, so every distance has an answer; a distance between two slices reads both, linearly in
  distance (`WorldAtmosphere_GetVolumeSlice`; from the camera to the first slice, clear air to it). Its rays pass through
  the planet's surface with the air below it as on it: the world's ground is flat and goes on where the planet curves
  away. Built every frame after the sky view (`BuildVolume`) into one RGBA16F texture of 1024 x 65: the slices side by
  side, the in-scatter's tiles in rows 0 to 31, the transmittance's in rows 32 to 63 (RGB transmittance in tiles of its
  own keeps the blue shift exact), and a row of the frame's values (`WorldAtmosphereVolumeValue`: the light at the ground
  haze's base: the sun's and the moon's through the air and the planet's shadow, and the light from all around, the
  sky's multiple scattering and light pollution's; since T36 also the sky's light by height every lit surface reads:
  "Sky's light on surfaces"). Each in-scatter texel marches its path (8 steps within 4 km, 24
  beyond, 32 for the whole ray: within 0.13 % and 1.2 % of 512 steps), lit by the sun, by the moon while the moonlit sky
  shows and by light pollution's light, tinted by `sky_tint`; each transmittance texel marches the air alone. A texel's
  path ends at its slice's distance or at the top of the atmosphere, whichever is nearer.
- **The readers** (`common/haze`): a point at render-space `p` is looked up at its screen place through the scene's
  view-projection (`hazeViewProjection`: the crisp pass at window resolution reads the same directions) and at
  `length(p)`, scaled. `ApplyAerialPerspective` in `common/lighting` gives every lit shader colour x transmittance + the
  in-scatter through the eye's night vision: models (rigid, skinned, instanced), terrain, lit and unlit sprites and
  decals, at scene and at window resolution; an additive sprite takes the transmittance alone (`AerialTransmittance`). The
  lit programs read the volume on unit 8 (`WorldRendererTextureUnit_HazeVolume`); the sky pass and the outline pass bind
  it with `SetShaderValueTexture`.
- **The frame's haze** (`WorldAtmosphere_GetHaze`): every haze distance (and the ground haze's depth) is multiplied by
  `aerial_distance_scale` x `fog_strength` x the player's `fog_strength` (`DistanceScale`; 1 the real air), and the
  first `haze_start_distance` metres are clear. At scale 0 the volume is not built and nothing is hazed (the far ground
  then meets the sky with no haze in front of it). A draw multiplies the scale by its placement's `haze` (an override
  pair on models, model instances, sprites and terrain: [WorldModels.md](WorldModels.md),
  [WorldSprites.md](WorldSprites.md), [Terrain.md](Terrain.md)): 0 shows it as if the air were clear (a sign that must
  read from far away). Every lit draw sets `hazeMultiplier` (`WorldRendererLighting_SetDrawHaze`), as the uniform keeps
  the last draw's value. **T14's hook**: an ambient zone's haze multiplier multiplies the draw's where the callers of
  `WorldRendererLighting_SetDrawHaze` pass it.
- **The ground haze**: a low layer of mist, `ground_haze_density` x exp(-max(height - `ground_haze_base`, 0) /
  `ground_haze_thickness`), its optical depth along each ray in closed form (`hazeGroundDepth`, C mirror
  `WorldAtmosphere_GetGroundHazeDepth`: the mean density over the heights crossed, so a level ray needs no special case),
  in the flat world's heights, per pixel (a few metres of mist would blur across the volume's rows). Its light is
  `ground_haze_albedo` x (the sun's and the moon's light at its base through its glow toward them, Cornette-Shanks with
  `ground_haze_glow`, plus the light from all around), seen through the air in front of where half of it comes from (the
  distance where its depth reaches -ln((1 + e^-depth) / 2): `hazeGroundDistance`, C mirror
  `WorldAtmosphere_GetGroundHazeDistance`), so a mist around the camera is not hazed by the far air and a far misty
  valley is: the pixel is mix(the mist's light, what lies behind, the mist's transmittance), the mist's light at most
  65504 (the scene target's half floats). The sky pass lays it in front of the sky and the far ground the same way (to
  the far ground's distance below the horizon, to `WORLD_ATMOSPHERE_SKY_DISTANCE` above it: `hazeGroundInFrontOfSky`), so
  a misty horizon meets misty ground without a seam; a ray that looks down but passes over the planet's horizon (from up
  high, between level and the horizon) follows the curved planet instead (`hazeGroundDepthCurved`, C mirror
  `WorldAtmosphere_GetGroundHazeDepthCurved`: the ray's height a parabola around its lowest point, the depth a Gaussian
  there in closed form through the scaled complementary error function, within 0.01 % of the exact ray), because
  beyond its lowest point it climbs out of the mist, where the flat world's straight ray would sink into it. The discs,
  the Milky Way and the stars (`hazeGroundTransmittance`, per star) are dimmed by it. Off by default (density 0); T16's
  `fog_density` and `fog_height` and T18's mist will drive it.
- **The outlines** fade by the haze's transmittance per colour at the object's own multiplier: an outlined surface's
  G-buffer alpha holds its haze (`OUTLINE_HAZE_BASE` 0.76 at 0 to 1 at 1; above 1 counts as 1), and `world_postfx` reads
  the volume's transmittance and the ground haze's at the pixel ("G-buffer, AO and outlines").
- **Light pollution** is the town's light scattered by the air ("Night sky").
- **The debug view** (`WorldRenderer_SetHazeView`; keypad 4 and 5 in the test frame): one slice's in-scatter or
  transmittance stretched over the scene in its place, as the readers sample it (`WorldRendererAtmosphere_DrawView`).
- **Status**: the "haze" feature (active while the volume is built; inactive while the tables are off or the haze's
  scale is 0; unavailable without the tables or the volume's shader or target), and a status log line with the volume,
  the scale, the ground haze and the debug view.
- **Units** (G39, answered (b); done in T36): the in-scatter is on the surfaces' scale, so a dark surface hazes at the
  real rate: from 300 m up the terrain 400 to 600 m away changes 0.5 % in brightness (28.7 % before T36: the sky was
  22 times the surfaces' sun; the air takes about 1.8 % of the grass's light over 500 m and gives back the sky's, nearly
  as bright), black things still vanish at the real visibility (about 100 km at turbidity 3), and a morning mist toward
  a low sun glows instead of blowing out to white.
- **Cost** (GTX 980 Ti, T35's status block): the volume and its use 0.085 ms by day and 0.13 ms at night (the moon's
  march), against T34's fog march of 0.26 and 0.5 to 0.67 ms; a morning mist adds about 0.18 ms in a view full of
  terrain.

**Sky's light on surfaces** (T36, decision G39 (b); the C mirror `source/WorldSkyLight.{h,c}`, tested in
`tests/WorldSkyLightTests.c`; the layouts in `source/WorldAtmosphere.h`; the lit shaders' part in
`shaders/common/lighting.glsl`). The sky, the haze, the far ground and every surface are lit by the same sun and moon,
on the surfaces' scale: lamps, emissives, the bloom threshold, night vision's candelas and the night sky's radiances
keep their calibration, the sky's moved.
- **One scale.** The sun above the air lights a surface facing it with `ATMOSPHERE_SUN_LIGHT` (1.078; C twin
  `WORLD_ATMOSPHERE_SUN_LIGHT`, pinned by the shader source tests) x `sun_intensity` x `sun_color` (linear; white by
  default: the air reddens it by itself): the sun 60 degrees up lights the ground in the default air as the sun before
  T36 did (luminance 0.911). The air is lit by the same light (before T36 by 22 times it). The moon above the air is
  `moon_intensity` x its phase's brightness x (1 - the daylight factor) x the sun's light, in `moon_color`, for the
  surfaces and the air alike.
- **Through the air, at each surface's own height.** The sun's and the moon's light on a surface is their light above
  the air times `atmosphereLightTransmittance` toward them at the surface's height (the transmittance with the share of
  the light's disc above the planet's horizon there): near white at noon, golden then red as the sun sets, gone once the
  disc has set there. So from 5 km at dusk the ground lies in the planet's shadow while the camera still sees the sun, and
  a hill keeps the sun longer than the valley. It replaced the daylight ramp and the moonlight's height ramp.
- **The ambient** is the sky's own light: its irradiance on a surface facing up (the sky's model: single scattering
  through the transmittance with the phase functions, plus `multiple_scattering` of the multiple scattering table's
  glow, lit by the sun, the moon and light pollution's light in the air, times `sky_tint`) and the ground's bounce on one
  facing down (`sky_ground_albedo` x the sun's and the moon's light on the ground and the sky's irradiance). A surface
  with normal n takes (sky (1 + n.y) / 2 + ground (1 - n.y) / 2) / pi x `ambient_skylight_color` x
  `ambient_skylight_intensity` + the moonless night's own light (`night_light_color` x `night_light_intensity`), times its
  albedo and AO (`AmbientLight` in `common/lighting`: **T14's hook** for the sky visibility and the ambient zones). It
  replaced the painted hues (`AMBIENT_DAY_*`, `AMBIENT_SUNSET_*`) and their ramps. At a clear noon shade is about a tenth
  of sunlit ground (the default air is clear; hazier air brightens it), and walls facing away from the sun get the
  ground's bounce.
- **The irradiance table** (`world_atmosphere_irradiance`, `shaders/atmosphere/irradiance.frag`): 65 x 32 RGBA16F, the
  irradiance per unit of a light above the air by the light's elevation (64 columns from -20 degrees to the zenith, even
  in asinh(elevation / 0.07 rad): about 0.4 degrees apart at the horizon, where the twilight's light falls fastest) and
  the height (32 rows from the ground to 100 km, even in log(1 + height / 1 km): even in the height near the ground), the
  last column light pollution's per unit of its radiance; natural logarithms (at least -46), so a read between texels
  follows light falling off exponentially: within 1 % of the quadrature from 2 degrees below the horizon up, 5 % from 6
  below (lower, the sky's light is a thousandth of the night's own). The quadrature, the C mirror's (pinned by the
  shader source tests): Gauss-Legendre rings of the zenith cosine (6) times even azimuths over half a turn (6) for the
  air's own light, the multiple scattering's glow once a ring; the haze's lobe around the light by Henyey and Greenstein's
  inverse weighted by Cornette and Shanks's (6 rings x 6 azimuths since T37: in the default 20 km haze 4 x 4 missed the
  light at the zenith by 3 %; each up to 10 haze scale heights up); light
  pollution's over the rings, each up to 10 of its falloff lengths; Simpson's rule over 8 stretches (the haze's 4)
  growing with the square of their index, the optical depth between points exact for exponential densities. Within 2 %
  of a fine integral of the same model from the zenith to 4 degrees below the horizon (light pollution's within 0.5 %).
  Built after the transmittance and multiple scattering tables, and when `sun_glow` or `multiple_scattering` changes:
  0.235 ms on a GTX 980 Ti.
- **By height: the volume's last row.** The volume pass writes the sun's and the moon's light through the air, the sky's
  irradiance and the ground's bounce at 64 heights from the ground to 20 km (even in the height's square root: 5, 20,
  45 m at the ground) into its row 64, a block of 64 columns each from column 64 on (`WorldAtmosphereSkyLight`,
  `WorldAtmosphere_GetSkyLightColumn`): no new texture unit. It is written whenever the tables run, with the haze off
  too (then only that row is drawn). The lit shaders read the two heights around a fragment's own (the view height plus
  its render-space y) and mix them (`SkyLightAt`); while the tables or the volume do not run (keypad 1, a missing
  shader), every fragment takes the camera's light from uniforms, the C mirror's.
- **The C mirror** (`WorldSkyLight`): the light at the camera, for the exposure, night vision, the shadow light, the
  status line and the fallback uniforms. The transmittance exactly (Gauss-Legendre over the ray's stretches between its
  lowest point and the ozone layer's corners: within 1e-6 of a long double integral); the sky's irradiance from the
  irradiance table built in C by the same quadrature (from a transmittance table of half the GPU's size each way and a
  multiple scattering table with half its steps: within 0.3 % of full-size tables) and read as the GPU reads it. A
  frame's light costs about 2.3 microseconds; the lights above the air, the ambient's tint and the night's light are at
  most `WORLD_SKY_LIGHT_MAX_LIGHT` (65504, the GPU's half-float limit), so absurd but valid intensities stay finite. Its
  tables (about 85 KB, two sets on the heap) are kept by a `WorldSkyLightBuilder` (pure, tested): built whole when a
  world is prepared (`WorldRenderer_PrepareWorld`, only when the world's air differs from what they hold) or on the first
  frame (about 70 ms in the default haze); when the air changes they are rebuilt into the second set over frames (193
  steps of at most 0.75 ms: about 70 frames), the old set lighting the frames meanwhile, and swapped in when done; a change during a
  rebuild waits for the next one. `WorldRendererAtmosphere_UpdateSkyLightTables` takes about 1 ms of steps a frame: a
  step runs while the time so far and the last step's fit in 1 ms, the first always. So T16's weather can move the air
  every frame. The GPU's light at the camera matches it within 0.4 % (2.5 % for the sun at the moment of sunset: the
  GPU's transmittance table against the exact transmittance). The moon's light counts while the moonlit sky shows
  (`WorldSkyLight_GetMoonGate`: the sun below about 3 degrees, the moon above about -6), for the surfaces and the air
  alike.
- **The scene luminance** (`WorldSkyLight_GetSceneLuminance`): the ambient's luminance on a white surface facing up
  plus 0.40 (`WORLD_SKY_LIGHT_DIRECTIONAL_ADAPT_WEIGHT`) of the sun's and the moon's light, 0.371 at a clear noon in
  the default haze. Since T37 the eye adapts to the picture ("Eye adaptation"): this sets its anchor, and its split into
  the daylight's share and the night lights' (`WorldSkyLight_GetDayLuminance`) is how the eye and night vision read the
  game's light on the real scale (`NightVision_GetFieldLuminance`, "Night vision").
- **The sun's disc** keeps its look on screen whatever the exposure: its radiance is `SUN_DISC_RADIANCE` (22,
  `world_sky`; C twin `WORLD_ATMOSPHERE_SUN_DISC_RADIANCE`) x `sun_color` x `sun_intensity` x 1.08 / the exposure (the
  eye's, before the frame's own is set; `SUN_DISC_SCREEN_EXPOSURE`), so the setting disc stays orange while the
  exposure rises through dusk (at most 65504 in all). It does not bloom (the bloom masks the sky) unless the scene's depth cannot be sampled.
- **The sun shafts'** strength is multiplied by 8 / the exposure (`SUNSHAFT_SKY_SCREEN_SCALE`): their light on screen is
  8 times the sky's pixels they gather toward the sun, mostly the disc, so it falls as the exposure rises with the disc;
  tuned on pictures to within about 25 % of before T36 with the sun 30, 15 and 5 degrees up.
- **Status**: a log line with the light at the camera (the sun's and the moon's light, the sky's irradiance, the
  ground's bounce, the night's light, the scene luminance, the eye's level and whether it was measured, the exposure,
  the C tables' builds) and the same in `WorldRendererStatus`. The light before T36 (keypad 9's A/B) went in T37.
- **Cost** (GTX 980 Ti, T36's status block): the values row 0.004 ms with the haze on, 0.0125 ms alone; the lit terrain
  about 0.06 ms more (5 to 8 %); the irradiance table 0.235 ms when the air changes; the C mirror 2.3 microseconds a
  frame.

**Night sky.** The pure maths is in the private `source/NightSky.{h,c}` (tested in `tests/NightSkyTests.c`); the GLSL
the passes share is `shaders/common/night_sky.glsl`.
- **The sky turns.** `WorldEnvironment_GetStarsToWorld` turns everything fixed among the stars once a day around the
  celestial pole, which stays at world (0, sin SunAngle, -cos SunAngle): low toward -Z, at the height SunAngle (the
  latitude). The stars also drift through the year: the star frame is the celestial frame of the June solstice (where
  `NightSky_GetCelestialToGalactic` places the galaxy), turned ahead of the sun's frame by the sun's progress through
  the tropical year (continuous, no jump at New Year). So at the same time of day the stars stand about 0.99 degrees
  further along each day and rise about 4 minutes earlier: the galactic centre is high at midnight in June, set by
  midnight in late September, below the horizon all night in December. `WorldEnvironment_GetCelestialToWorld` is the
  sun's own frame (the sun at (1,0,0)), which the moon is placed in.
- **Stars** (`WorldRendererStars.c`, shader `world_stars`). A catalogue generated from `StarSeed`: `StarDensity` 1 is
  an Earth-like sky (about 9000 stars, at most 20000), magnitudes -1..7 by the real count law (2.8 times as many per
  magnitude), B-V colours (mostly white to yellow, some blue and orange), up to twice as dense along the galactic
  plane. Each star has its own random stream, so a higher density only adds stars. It becomes one mesh of one-pixel
  quads (six vertices per star, unindexed: raylib's 16-bit indices would cap an indexed mesh at 16384 stars), rebuilt
  only when the seed or the star count changes. The vertex shader projects each star as a point at infinity (at depth 0
  with reversed depth, T39: the cleared sky's, so whatever is drawn hides it; without, just inside the far plane) and
  widens it to exactly one scene pixel, so a star lights one pixel at a steady brightness and
  never flickers. Drawn right after the sky: additive (ONE, ONE) with alpha 0 (the scene's alpha reaches the frame
  target), depth-tested, writing no depth (the bloom prefilter and the sun shafts read the cleared depth as sky),
  culling off.
- **What shows** (T38, decision G45: by the sky's brightness behind the stars). The limiting magnitude
  (`NightSky_GetLimitingMagnitude`, one value per frame) is the naked eye's threshold against the sky's luminance
  overhead: Schaefer's ("Astronomy and the limits of vision", 1993), the rods' and the cones' thresholds alike, the
  lower counting (`NightSky_GetThresholdMagnitude`: about 6.5 against a dark site's 2.2e-4 cd/m^2, 3.2 against 1
  cd/m^2, none by day). The background is the night's plus the twilight's:
  - the night's: the background that gives the night limit T02 and T03 tuned (`NightSky_GetNightLimitingMagnitude`,
    `NightSky_GetThresholdBackground`): 6.5 at a dark site, minus 3.5 x `LightPollution`, minus the moonlit sky's share
    (a high full moon at the default moonlight takes 1.75, so the faint stars and most of the Milky Way go; it grows with
    the logarithm of the moon's light, so a quarter moon still takes about 0.6, and it shrinks with light pollution,
    which already brightened the sky);
  - the twilight's: the sky's light from the sun at the camera (the C mirror's, `WorldSkyLight_GetTwilightLuminance`),
    as the sky's mean luminance on the real scale (`WORLD_ATMOSPHERE_SUN_LIGHT_CANDELAS`: the game's sun above the air
    stands for the real sun's 128 klux; night vision's scale is the eye's adaptation level, not the sky's radiance).
  In the default air: no star with the sun 4 degrees up or higher, the brightest at sunset (0.2), 1.7 at -3 degrees,
  3.5 at -6, 4.1 at -9, 4.7 at -12, the night's by -14 (5.28 with the default light pollution; 6.5 at a dark site by
  -15). Stars fade out over the last 0.5 magnitude, so at dusk the brightest appear first. Before T38 the twilight's
  limit rose 0.5 magnitudes a degree below the horizon (1.5 at -6). The moon's disc hides the stars behind it
  (the star shader drops them). Near the horizon stars and the Milky Way dim by extinction (Kasten-Young air mass, 0.25
  magnitudes per air mass; since T15 by the transmittance table relative to the zenith's, which reddens them too, while
  the atmosphere's tables run); below it nothing is drawn. Stars twinkle by `StarTwinkle` x noise, more near the horizon,
  on `WorldRenderFrame.TimeSeconds` (the renderer's own clock, wrapped hourly).
- **Milky Way.** The texture the world names (`MilkyWayTexture`, default `sky_milky_way`: sRGB, trilinear with
  mipmaps), sampled once per pixel in galactic coordinates: `NightSky_GetCelestialToGalactic` places the galaxy as on
  Earth (plane 62.9 degrees to the equator, centre at declination -29) in the star frame, which the date turns (above).
  The panorama's longitude grows to the left, as in astronomical all-sky maps. Explicit mip level from the pixel's angular size
  (automatic derivatives would draw a seam line where the longitude wraps). Brightness: `MilkyWayBrightness` x full
  at a limiting magnitude of 6 or more, gone at 4.5 or less x extinction: since T38 (by the sky's brightness) it comes in
  with the sun between about 11.5 and 14 degrees down in the default world (half strength at night under the default
  light pollution), between 11 and 13 at a dark site (before T38: 12 to 13.6). A missing texture is logged once and the
  sky draws no Milky Way.
- **Light pollution.** At night the town's light fills the air: since T35 it is what it physically is, light scattered
  by the air, so it grows with the haze. The air below about `WORLD_ATMOSPHERE_LIGHT_POLLUTION_HEIGHT` (500 m) and within
  about `WORLD_ATMOSPHERE_LIGHT_POLLUTION_REACH` (10 km) of the camera holds `LightPollutionColor` x `LightPollution` x
  `WORLD_ATMOSPHERE_LIGHT_POLLUTION_RADIANCE` (0.049), faded out by daylight (`WorldAtmosphere_GetLightPollutionRadiance`),
  scattered like any light (the air's scattering per metre x that, `WorldAtmosphere_GetLightPollutionShare`): in the
  sky-view table's sun half, in the volume (the haze over the town glows) and in the ground haze's light. The radiance is
  calibrated so the glow at the horizon is T02's skyglow (0.012 x the colour at `LightPollution` 1: 99 % of it in green),
  a little bluer (Rayleigh scattering), 5.5 % of it overhead (T02: 6 %); doubling `sky_turbidity` brightens it about 1.4
  times at the horizon. The direct march (the A/B) keeps T02's skyglow profile (`SKYGLOW_BASE_RADIANCE`). Since T36 the
  town's light reaches surfaces as part of the sky's irradiance (the irradiance table's last column: "Sky's light on
  surfaces"), and the moonless night's own light is the environment's `night_light_color` x `night_light_intensity`
  (before T36 the `NIGHT_AMBIENT_*` floor and `NIGHT_AMBIENT_LIGHT_POLLUTION_SHARE`).
- **Brightness knobs** (tuned for the night's exposure, about 24, which the eye keeps for an open field since T37:
  "Eye adaptation"): `STAR_RADIANCE_SCALE` in
  `WorldRendererStars.c`, `MILKY_WAY_BASE_RADIANCE` and `SKYGLOW_BASE_RADIANCE` in `WorldRendererSky.c` (the direct march's skyglow), `WORLD_ATMOSPHERE_LIGHT_POLLUTION_RADIANCE` in
  `WorldAtmosphere.h` (the tables'); per world
  `StarBrightness` and `MilkyWayBrightness` scale them.

**Moon.** The sky's model is pure maths in `WorldEnvironment` (`agents/WorldData.md`); the renderer draws it.
- **Phase and position.** The phase comes from the date (`WorldEnvironment_GetMoonPhase`: the real moon's by default,
  shifted by `MoonPhaseOffsetDays`, cycling every `MoonCycleDays`) or is fixed (`MoonPhaseSource`). The moon stands on
  the sun's path, east of it by 2pi x phase and `MoonOrbitTilt` x sin(2pi x phase) north of it, so the new moon is with
  the sun, the full moon opposite, the first quarter rises at noon, and it rises about 50 minutes later each day.
- **The disc** (`world_sky`, `WorldRendererMoonDisc` from `WorldRendererSky_PrepareFrame`): angular radius 0.26 degrees
  x `MoonSizeMultiplier` (at most 45 degrees), its axes toward the sky's pole (the face turns as it crosses the sky),
  shaded as a sphere lit by the sun with Lommel-Seeliger (normalized to 1 at full moon, which looks flat as the real one
  does; the terminator and the crescent face the sun), plus earthshine on the dark side (`MOON_EARTHSHINE_SHARE` x
  (1 - illumination)). The face is the texture the world names (`MoonTexture`, default `sky_moon`, point-filtered for
  crisp pixel art; a flat grey without it), tinted by `MoonColor`, at `MOON_DISC_BASE_RADIANCE` (the "MOON DISC KNOB") x
  `MoonBrightness`. The disc covers what lies behind it (the sun disc, the Milky Way, the stars) and is dimmed by the
  view ray's transmittance (the transmittance table since T15; the direct march's out parameter while the tables are
  off), with the sky's in-scatter added in front: so
  by day it is pale (a quarter moon is findable in the afternoon), at night bright, and reddened low over the horizon.
  It fades out within 6 to 12 degrees of the sun (lost in the glare; this also keeps the virtual moon from eclipsing
  the sun) and is skipped once below the horizon.
- **The moonlit sky.** The same atmosphere lit by the moon's light above the air (since T36 all of it, the moonlight
  the surfaces get; before T36 `MOON_SKY_SHARE` 0.3 of it): a dark blue sky with a soft halo at full moon, faded out
  while the sun is above about 3 degrees and the moon below about -3 to -6 (outside that it is skipped: since T15 the
  sky-view table's moon half is built only then, with the moon as its light). The haze's volume is lit by the moon then too, so
  hazed silhouettes match the sky behind them.
- **Moonlight.** The second directional light of the PBR shader, with the sun's BRDF: `MoonIntensity` (default 0.04 of
  the sun since T36, `WORLD_ENVIRONMENT_DEFAULT_MOON_INTENSITY`: tuned on rendered frames so a full-moon night's ground
  is as bright as before T36, within 2 %, clearly lighter than a new-moon night and still night) x the phase's
  brightness (`WorldEnvironment_GetMoonBrightness`, the opposition-surge fit of Krisciunas and Schaefer: a half moon
  gives about a tenth of the full moon's light) x (1 - daylight), in `MoonColor`, above the air; since T36 it reaches a
  surface through the air at the surface's height like the sun (reddened and dimmed low over the horizon, gone once
  set), its fill is the moonlit sky's own irradiance (before T36 a painted `MOON_AMBIENT_SHARE` of 0.3 of the moonlight,
  3.5 times the sky's: moonlit shadows have more contrast now), and the eye adaptation counts it like the sunlight. New-moon
  nights look as before.

**Night vision.** The eye's dim-light vision: in dim light the rods take over from the cones and colour fades into a
grey with a faint blue cast. A low moon's sky is as orange as a sunset's (moonlight is sunlight, scattered by the same
air; a long exposure shows it), but the eye sees it grey-blue, and a moonlit field loses its greens. The maths is the
private `source/NightVision.{h,c}` (tested in `tests/NightVisionTests.c`; its header explains the model), the GLSL
`shaders/common/night_vision.glsl`.
- **The model.** Hunt's rod sensitivity as Krawczyk, Myszkowski and Seidel use it (2005): the rods' share
  sigma = 0.04 / (0.04 + L) for a luminance L in cd/m^2, half and half at 0.04 (a bright moonlit scene). The game's
  light scale is compressed (its noon, a scene luminance of 0.371 on T36's scale in T37's default 20 km haze, stands
  for 5000 cd/m^2, its moonless night, 0.00602, for 0.001 cd/m^2), so a game luminance Y is read as
  5000 x (Y / 0.371)^3.74 cd/m^2, which makes sigma = 1 / (1 + (Y / 0.0161)^3.74). A pixel is blended by sigma toward its luminance in the rods' grey: white shifted
  toward the night blue (CIE x, y = 0.25, 0.25, Jensen et al. 2001) by the world's `NightVisionBlueShift` (default
  0.5; the full blue looks like a film's day-for-night filter, Krawczyk's own tint reads violet), at the same luminance,
  so the night's tuned brightness does not change.
- **Which luminance.** The larger of the pixel's own and the field luminance: the light the sky gives the whole scene
  (the sun, the moon, the ambient; lamps do not enter it). Since T36 it is read in two parts
  (`NightVision_GetFieldLuminance`, the frame's `NightVisionField`): the daylight (the sun's and its sky's light) is on the
  real scale, linearly (the noon's 0.371 stands for 5000 cd/m^2), while the night's lights (the moon, the town, the
  moonless night's own light) are raised for play and go through the power law; their real luminances add. So the
  twilight keeps its colours until the sun is about 4 degrees down, loses them through the nautical twilight (half at
  about -8 degrees) and is the rods' by -12 degrees, as for the eye; without the split the night's own light alone would
  grey the sunset. By day the field is far above 0.103, where sigma is below 1/1024, and the shaders return the colour
  untouched: day frames are bit for bit what they were. At night each pixel is seen by its own light: a lamp's bright pool keeps its
  colour, the dark beyond it greys. Lamps lower the exposure (by their share of the view, T37) but do not raise the
  field: the rods adapt locally, so a
  lamp beside the camera darkens the dim sky but gives it no colour.
- **What goes through it.** The sky pass's glow (the in-scatter of the sun and the moon, the skyglow, the Milky Way;
  before the discs are added) and the lit shaders' reflected light (ambient, the sun and the moon, point lights) and the
  haze's in-scatter. Not the emitters: the sun's and the moon's discs, the stars and emissive materials (the game draws
  emitters far dimmer than they are, and a real emitter that bright is seen in colour: the orange rising moon, a lit
  screen at night). Lit sprites' reflected light goes through it like the models'; unlit sprites are emitters.
- **Switches.** The world's `NightVisionStrength` (1 the eye, 0 colourful nights: a long-exposure photo) and
  `NightVisionBlueShift`; the renderer toggle `WorldRenderer_SetNightVisionEnabled` (V in the test frame) for A/B.
  Cost: a `pow` per sky pixel and per lit fragment.
- **Not modelled.** The rods' slow dark adaptation (minutes; the vision follows the sky's light at once), a lit
  interior at night (its dark corners would grey: its light is not part of the field; revisit with interiors), the
  loss of sharpness in the dark.

**Sun and moon shadows.** Two orthographic cascades around the camera (2048² depth textures), their centres snapped to
their texel grids so shadow edges stand still while the camera moves (see "Render space"): NEAR (32 m, 1.6 cm texels,
sharp) and FAR (160 m, 7.8 cm texels, coarse, optional). Each model component's `WorldShadowTier` (Both / Near / Far) routes which
cascade(s) it casts into, and one with `casts_shadows` off casts into none (nor into the point-light shadow). They are rendered from the frame's shadow light (`WorldEnvironment_ChooseShadowLight`, since T36 from the light the sun and the moon give the camera's surroundings through the air): the
brighter of the sun and the moon by luminance, once it stands above 0.05 (the sine of its elevation; full shadows from
0.15), faded out while the two are about as bright (0 where they are equal, full once one is 4 times the other) and for
a light too dim for a shadow to show (a thin crescent). So dusk with the moon up goes from sun shadows through none to
moon shadows without a jump. The PBR shader samples near first, then far, with 2×2 PCF and a slope-scaled bias along
the shadow light, darkening only that light's direct term (`sunShadowFade` / `moonShadowFade`; both 0, and no map
sampled, while the cascades do not run). Active when shadows are enabled, the strength is above 0 and there is a
shadow light.

**Point-light shadow.** One cube shadow for the shadow-casting light nearest the camera (`CastsShadows`): six
90° faces of packed linear distance (RGBA8 cube, `common/distance_packing`), sampled with 20-tap PCF. The faces are
written with colour blending off: the packed value's alpha byte is a digit of the distance, not a coverage (until T06
the default blending mixed every texel with the cleared "far", so casters beyond half the light's reach left no shadow
and nearer ones were stored too far away). Models and cutout sprites (turned toward the light) cast. That light is
shaded separately and is excluded from the forward candidates, so an object can still get eight other point lights.

**G-buffer, AO and outlines.** `world_normal` (models) and `world_normal_sprite` (cutout sprites, alpha-tested) write
view-space normals (RGB) and a surface/outline flag (A: 0 = nothing, 0.5 = surface, 0.76 to 1 = surface with outline,
its haze multiplier over that range)
into a low-res 8-bit target with blending disabled. `world_ao` turns it into an AO multiplier that the PBR pass
applies to its ambient term only (so AO never dims direct light). `world_postfx` draws outlines after the scene:
depth/surface silhouettes darken, view-normal creases are recoloured sun-aware; the sky is never outlined. Since T34 a
line fades with the haze as the surface under it does: since T35 its strength is scaled, per colour, by the haze's
transmittance to the pixel at the object's own haze multiplier (the volume's and the ground haze's, `hazeTransmittance`;
the world's up in view space, `worldUpView`, gives the ray's climb), so a hill the haze has all but hidden keeps no dark
rim, and a sign with `haze` 0 keeps its lines. It reads the
G-buffer's depth (the scene's only where the G-buffer's is not samplable), which matches its normals and flags: a fading
object is solid in the G-buffer but has the dither's holes in the scene's depth. (Since then the debug light gizmos,
which write the scene's depth but are not in the G-buffer, are no longer darkened as if they were the rim of the surface
behind them.) Both
are under the post-effects toggle (`WorldRenderer_SetPostEffectsEnabled`, O key in the test frame).

**Bloom and sun shafts.** Half-resolution HDR passes added by the tonemap: a depth-masked bright pass (the sky
does not bloom) with a separable blur, and a radial blur of the unoccluded sky toward the sun's screen position
that fades as the sun nears the screen margin or the horizon; since T36 the shafts' strength is divided by the exposure
(times `SUNSHAFT_SKY_SCREEN_SCALE`), so they follow the sky's light on screen and do not glare at dusk ("Sky's light
on surfaces"). The march toward the sun ends at the screen's edge (beyond it nothing is known; clamped, the samples
repeated the edge row into streaks), and each sample adds at most `SUNSHAFT_SOURCE_SCREEN_LIMIT` (1.0) on screen (its
luminance times the last exposure), about the sky's glow near the sun: the disc's few pixels, far brighter, would smear
their square shape into a blocky cross. The scene's colour texture clamps at its edges (raylib makes colour textures
repeat, and a read at an edge took the opposite edge's pixels: the town at the bottom of the screen as white and aqua
streaks at its top).

**Eye adaptation** (T37, decision G42; the eye's model `source/EyeAdaptation.{h,c}`, tested in
`tests/EyeAdaptationTests.c`; the pass `source/WorldRendererExposure.c`; the readback `source/GLReadback.{h,c}`, direct
GL calls; the shader `shaders/luminance/fragment.frag`, `world_luminance`). The eye adapts to the picture, as a
camera's meter does:
- **The meter.** After the sun shafts the HDR picture the tonemap reads (the outlines' target or the scene's) is
  measured into a 64 x 36 target of one float a block: each block the mean log2 luminance of an 8 x 8 grid of its
  pixels, every pixel held within a window around the anchor (below), 6 stops under it to 2 over, and the sky's pixels
  (the cleared depth: `depthIsSky`) at least at the night's own light: the game lights surfaces at night with the natural night sky's
  glow (`night_light_color` x `night_light_intensity`) but does not draw it, and a dark site's black sky would otherwise
  drag the eye far below the light its ground is seen in. A pixel-pack buffer and a fence per copy (three in flight)
  bring the blocks to the CPU a frame or two later, never waiting; the C side weighs them toward the middle of the view
  (a Gaussian 0.35 view heights wide) into the view's log-average luminance.
- **The anchor.** 0.11 (`EYE_ADAPTATION_ANCHOR_SHARE`) of the scene luminance at the camera: the log-average an open
  view has under the sky's light (measured on the test world by day and by night). The eye starts there, and stays
  there where the meter is unavailable. A dark interior opens the eye at most 6 stops, a bright view closes it at most
  2 (further greyed a white platform or a lamp-lit square at night); a black corner, the sun's disc or a bulb count no
  further. A window around the eye's own level would chase a black sky down without end.
- **Easing.** In stops: 0.6 a second, faster by a quarter per stop of the step, five times faster while brightening, at
  most a tenth of a second of a frame counted (the speeds before T37). The first measurement sets the eye at once.
- **The curve: partial adaptation.** The eye's level is read on the real light scale (night vision's: daylight linear,
  the night's raised lights through its power law, at the camera's mix, `EyeAdaptation_GetRealScale`; at least the
  moonless night's ratio, so a darker night or a world lit by lamps alone is read as a dark night) as R cd/m^2, and the
  screen shows it at `EYE_ADAPTATION_KEY` (0.0873) x (R / 5000)^`EYE_ADAPTATION_POWER` (0.0596): a sunny day looks a
  little brighter than a dull one, the dusk darkens steadily, the night keeps its look. The exposure is that over the
  level, within 1/64 and 512 (guards only). Calibrated on rendered frames so an open field keeps its exposure from
  before T37 at noon (2.2) and at night (24); through dusk it now rises to about 40 with the sun 3 degrees down and is
  back at the night's by 12 degrees down, so the twilight stays on screen while its real light fades (before T37 the
  exposure reached the night's 24 with the sun still 5 degrees up). The picture as seen (its mean tonemapped sRGB) on
  the open field: noon 0.33, sunset 0.33, 2 degrees down 0.24, 6 down 0.18, 10 down 0.15, night 0.15 (before T37:
  0.32, 0.24, 0.17, 0.14, 0.14, 0.14).
- **Lamps** count by what they light on screen. Beside the grove's lamp by day the view keeps the day's look (before
  T37 it was darkened to a quarter), and near it at night the grove shows as any night's view.
- **What changed besides** (T37's status block has the measurements): a view of the white test platform by day gets
  about 40 % less exposure than before (the meter sees a bright view; the picture as seen about a fifth darker); a
  full-moon night is darkened toward the moonless night's look (the eye adapts to the moonlight: about 0.4 of its
  exposure before T37, the picture as seen about half as bright, the moon now showing in its shadows, sky and colours
  more than in the overall brightness); a dark site (no light pollution) keeps its look.
- **Cost.** The meter's pass and copy 0.06 to 0.18 ms on a GTX 980 Ti; the C side 0.01 ms.
- **Status.** `WorldRendererStatus.AdaptedLuminance` (the eye's level, game units), `AdaptedRealLuminance` (cd/m^2),
  `IsExposureMetered` and the `ExposureMetering` feature: unavailable without the shader, the target or the readback's GL
  functions (logged once; the eye then adapts to the anchor, lamps counting for nothing), inactive until the first
  measurement.
- **Knobs** (`source/EyeAdaptation.h`): `EYE_ADAPTATION_KEY` brightens or darkens every scene alike;
  `EYE_ADAPTATION_POWER` sets how far days, dusks and nights stand apart (0 every scene alike, more: a brighter noon
  against a darker night unless the key follows); the window and the anchor's share; the speeds.

**Crisp overlay.** OmitPixelation models and cutout sprites render lit at window resolution and are composited over the
pixelated image, depth-tested against the scene so they are occluded correctly (e.g. a readable in-world
screen). When the overlay cannot run they fall back to the pixelated passes, so they never vanish. Crisp objects
get no AO and no outlines.

**Effect strengths.** Bloom, sun shafts, shadows and AO combine the world's multiplier
(`WorldEnvironment`), the config's (`GameConfig`) and a built-in base; the haze's strength is the world's
`fog_strength` x the config's `FogStrength` (1 the real air: "Haze"). Outlines are a code-side toggle.

### Failures and status

- A shader that fails to load is logged once (warning) and switches its feature off. A GPU target that cannot be
  created is logged once and switches its pass off. The only fatal failure is a scene target that cannot be
  created at all: `WorldRenderer_RenderToTarget` returns `ErrorCode_InvalidState`.
- `WorldRenderer_GetStatus` reports the scene tier, each feature as active / inactive (switched off or not
  applicable this frame) / unavailable (missing shader or target), and object counts (the star count included; the
  sprites with how many are decals and how many blended or additive). The
  "stars" feature is inactive by day, while twilight, light pollution and the moonlit sky hide every star, and at
  density 0; the "Milky Way" is unavailable without its texture and inactive while it cannot be seen; "sun shadows" and
  "moon shadows" are the cascades cast by either light; "moon" is the disc (inactive below the horizon, near the sun or
  switched off; drawn flat grey without its texture), "moonlight" the moon's light on the world, "night vision" the
  eye's night vision (active while it changes something: dusk and night, with a strength above 0), "sprite shading" the
  sprites at all (unavailable without the sprite shader or quad: then no sprite is drawn), "sprite lighting" the lit
  sprites (inactive while switched off with the K key's toggle), "sprite shadows" the shadows of cutout sprites
  (active while one casts into a running shadow; unavailable without the sprite depth shaders), "skinning" the posed
  models' skinned meshes (T29: available with the skinned twin of the shader models draw with, active while a shown
  model has a pose; the status line counts the posed models), "terrain shading" the lit terrain shader (B01: unavailable
  without `world_terrain`, when the chunks draw unlit; the status counts the terrains placed, those without a terrain,
  the chunks drawn of all listed and their triangles, and whether the chunk key is on), "terrain materials" the terrains'
  texture arrays (B02: active while a drawn terrain has them, unavailable without texture arrays or the terrain shader;
  the status log adds a line per terrain with its materials and its arrays' sizes, and the status names a debug view
  while one shows), "atmosphere tables" the atmosphere's tables (T15: unavailable without their three shaders or targets,
  inactive while switched off, when the sky is marched per pixel; the status adds the build count, the height the sky is
  seen from, the render's CPU time and the frame time the caller gave), "haze" the aerial-perspective volume (T35:
  unavailable without the tables or the volume's shader or target, inactive while the tables are off or the haze's scale
  is 0; the status adds the scale, the ground haze's density and the debug view), "reversed depth" the camera's reversed
  depth (T39, see "Render space": unavailable without clip control, logged once, when the camera keeps raylib's
  projection and its 4 km; a camera pass's 32-bit depth that cannot be made is logged once and the pass's target fails
  as any target does). It also counts what the camera's passes
  drew and what culling and fading removed (see "Visibility"). The renderer logs the status once after its first
  render; `WorldRenderer_LogStatus` logs it again (the I key in the test frame), with a line for the atmosphere's tables
  and one for the haze.

### Renderer structure (keep it clean)

| File | What it holds |
|---|---|
| `WorldRenderer.c` | Public API: create/destroy, `PrepareWorld`, `RenderToTarget` (frame context, pass decisions, the visibility build, pass order), status. Orchestration only. |
| `WorldRendererInternal.h` | The renderer state (one state struct per pass), `WorldRenderFrame`, fixed texture units, shared constants, and every internal function, documented. |
| `WorldRendererCommon.c` | Config multipliers, colour conversion, shader loading, one-time warnings, render-target constructors; the camera passes' depth (T39: their depth buffers, the reversed state around them, the projection they load, the depth convention's uniform). |
| `WorldRendererScene.c` | Scene target size, HDR tiers and recreation on resize (asks each pass to recreate its targets); the scene pass. |
| `WorldRendererGeometry.c` | Model meshes one LOD at a time with per-draw materials (both LODs of a cross-fade in the lit passes; `WorldRendererGeometry_PrepareMaterial` builds each, the instanced draws use it too), each mesh with its pass's rigid shader or, posed and skinned, its skinned twin after the pose's palette (T29); the unlit fallback's skinned shader; the LOD tinting, light gizmos, the pixelation filter. |
| `WorldRendererInstances.c` | The model instances' copies (B03, see "Model instances"): each entry's records buffer, the G-buffer's and the shadows' instanced twins (the lit one is the lighting file's), the instanced draw that mirrors `DrawMesh`, each pass's cells instanced or one at a time (the A/B and every fallback), the status line. |
| `WorldRendererSprites.c` | The sprites: the unit quad, the lit sprite shader, each frame's sprite matrices (camera, cascades' light, cube's lamp) and transparent draw order, the lit cutout / decal / sorted transparent draws. The G-buffer and shadow passes draw sprites with this file's quad and matrices. |
| `WorldRendererLighting.c` | The PBR programs (rigid; T29 skinned; B03 instanced: `WorldRendererPbrProgram`) and their location tables, the lit shaders' frame uniforms (`PrepareLitShader`, `UploadFrame`: every PBR program, the sprite shader and, B01, the terrain shader, the night vision's among them), forward point lights, per-material factors. |
| `WorldRendererShadows.c` | The shadow cascades of the sun or the moon, and the point-light cube shadow (models, terrain chunks, and cutout sprites through alpha-tested depth shaders). |
| `WorldRendererGBuffer.c` | The normal/flag G-buffer (models, terrain chunks and cutout sprites) and the AO pre-pass. |
| `WorldRendererTerrain.c` | The terrain's chunks (B01, [Terrain.md](Terrain.md)): their GPU meshes, kept by the renderer per terrain asset and chunk and uploaded again when a chunk's revision changes (all of them when the chunk layout changes; those of terrains no entry resolves any more unloaded); the `world_terrain` shader and its lit draw (per-chunk lights and dither; the unlit fallback with raylib's default shader); the G-buffer's and the shadow passes' draws with their shared shaders; the chunk key (the minus key: a colour per chunk and the wireframe); since B02 each terrain's materials as texture arrays (made once per terrain through `GLTextureArray`, bound on units 9 to 11), its materials' and rules' uniforms, each chunk's wrapped offsets, and the terrain views (the equals key). |
| `GLTextureArray.c` (B02, not a pass) | Texture arrays (`GL_TEXTURE_2D_ARRAY`): direct OpenGL calls (decision W7), through the function pointers raylib's loader resolves (`rlGetProcAddress`), with their GL constants; private (`source/GLTextureArray.h`). |
| `GLReadback.c` (T37, not a pass) | Readbacks that do not wait: a pixel-pack buffer and a fence per copy, three in flight; direct OpenGL calls in `GLTextureArray`'s manner; private (`source/GLReadback.h`). |
| `GLDepth.c` (T39, not a pass) | Reversed depth (see "Render space"): whether the GPU has clip control, 32-bit float depth textures and renderbuffers, the switch to the reversed state and back; direct OpenGL calls in `GLTextureArray`'s manner; private (`source/GLDepth.h`). |
| `WorldRendererAtmosphere.c` | The atmosphere's tables (T15, see "Atmosphere"): their three builders and targets, the rebuild when the air changes, the sky view every frame (the moon's half while its sky shows; the far ground below the horizon); the haze's volume (T35, see "Haze"), its builder and target, the frame's haze and light pollution, the haze uniforms every reader gets (`WorldRendererHazeLocations`), the volume's unit and binds, the debug view; the atmosphere uniforms every reader gets (`WorldRendererAtmosphereLocations`), the tables' fixed units, the A/B, the status lines. Runs first among the GPU passes. |
| `WorldRendererSky.c` | The atmospheric sky with the moon, the moonlit sky, the Milky Way and the ground haze in front of it (the far ground and light pollution's light are the tables'); the sky textures the world names; the night sky values (the direct march's skyglow among them) and the moon's disc for the frame. |
| `WorldRendererStars.c` | The star field: the catalogue's mesh (rebuilt when the seed or density changes) and its draw (none behind the moon). |
| `WorldRendererPost.c` | Outlines, bloom, sun shafts and the tonemap. |
| `WorldRendererExposure.c` | The eye (T37, see "Eye adaptation"): the meter's shader, target and readback, the meter pass after the sun shafts, the eye's update from the newest blocks (`EyeAdaptation`) and the exposure the tonemap takes. |
| `WorldRendererCrisp.c` | The crisp overlay. |
| `WorldRendererCollision.c` | Debug wireframes of a collision world's shapes and (T27) its contact points (`WorldRenderer_SetCollisionDebug`), drawn in the scene pass after the light gizmos from its read-only debug lists ([WorldCollision.md](WorldCollision.md)). |
| `WorldVisibility.c` (with the cache, not a pass) | The frame's visibility lists (per pass, the entries it draws, and every entry's fades) and the dither's C mirror; see "Visibility". |
| `WorldLod.c` (with the cache, not a pass) | LOD selection and the cross-fade's state; the cache's `WorldRenderCache_UpdateLods` runs it per entry; see "LODs". |
| `WorldAtmosphere.c` (pure maths, not a pass) | The atmosphere's model: the air from the environment, the stale check and the tables' build record, the view height, the tables' layout (the C mirror of `common/atmosphere`); see "Atmosphere". |
| `WorldRenderSpace.c` (pure maths, not a pass) | The frame's origin and its rotation-only view and projection (`BuildFrame` calls it; T39: the reversed infinite projection and its inverse), the sky shader's ray in C, the cascades snapped to their texels and their grids from frame to frame (`WorldRendererShadows_PrepareFrame` calls it); see "Render space". |

The renderer keeps receiving features, so:

- **One pass, one place.** A new pass gets its own file (or a clearly separated part of the closest one), its
  own state struct in `WorldRendererInternal.h`, and its own `LoadShaders` / `CreateTargets` / `ReleaseTargets`
  hooks called from `EnsureShaders`, `WorldRendererScene_EnsureTargets` and `_ReleaseTargets`. Do not grow
  `WorldRenderer.c` beyond orchestration.
- **Decide once, read the frame.** Per-frame values and pass decisions go into `WorldRenderFrame`, built in
  `RenderToTarget`; passes read it instead of recomputing camera matrices or re-deciding.
- **Render space everywhere.** A pass, a uniform or a debug draw never takes a world position: it takes the render
  cache's render-space values, or converts a double position with `GameMath`'s relative functions against
  `frame->Origin` (see "Render space"). The frame holds no camera pointer, so there is no camera position to misuse.
- **Draw from the cache.** Passes iterate their visibility list over the render cache's entries
  (`WorldVisibility_GetModels` / `GetSprites` / `GetTerrainChunks` for their pass) and dither each draw by its fade;
  never walk the world or resolve assets by name in a pass.
- **Render reads, it never writes.** No writes to world objects or shared assets: per-draw material copies, the
  renderer's own location tables, and per-object render data in the cache.
- **Fixed texture units.** Renderer-owned samplers use `WorldRendererTextureUnit`. Point each sampler at its unit once
  at load and only bind textures per frame. Never leave a sampler on unit 0: OpenGL forbids samplers of different types
  on one unit. For 2D (`DrawTexturePro`) passes bind extra samplers with `SetShaderValueTexture` after
  `BeginShaderMode`; manual `rlActiveTextureSlot` binds only work for 3D `DrawMesh` draws. **The budget** (GL 3.3
  guarantees 16 units; `WORLD_RENDERER_MATERIAL_MAP_COUNT` must match raylib's `MAX_MATERIAL_MAPS`, see BUILDING.md):

  | Units | Who | What |
  |---|---|---|
  | 0–11 | raylib's `DrawMesh` | A drawn material's maps, map i on unit i (`GL_TEXTURE_2D`, cubes on 7–9); the 2D batch uses 0–4. Free in a program whose materials carry no maps. |
  | 7–8 | the sky pass and the stars (T15); the lit programs (models, sprites, terrain: T35) | In the sky pass and the stars 7 the transmittance table, 8 the sky-view table (`sampler2D`, `WorldRendererTextureUnit_Atmosphere*`). In the lit programs 8 the haze's volume (`sampler2D atmosphereVolume`, `WorldRendererTextureUnit_HazeVolume`, since T36 also for the sky's light by height in its last row, while the haze is off too: they read no table since T35; their transmittance sampler on 7 is declared and unused, so 7 is free there for a `sampler2D` of the frame). raylib binds only cube maps there (`MATERIAL_MAP_CUBEMAP`, `_IRRADIANCE`, filled by no material of ours) and the 2D batch only 0–4, so what a pass binds stays bound; the lit passes bind the volume on 8 with their frame textures, the sky pass and the stars the tables before their draws (the sky pass and the outlines take the volume through `SetShaderValueTexture`). Units chosen because they are free in every lit program, the terrain's included, with no capability check (units 16 and up would have needed GL_MAX_TEXTURE_IMAGE_UNITS above 16). |
  | 9–11 | the terrain program only (B02) | Its material arrays (`sampler2DArray`: albedo and height, normal, roughness and AO). The terrain draws with no raylib maps, and an array sampler reads only a unit's `GL_TEXTURE_2D_ARRAY` target, which nothing else binds, so the models' maps on the same units never meet them. |
  | 12–15 | every lit program | The frame's textures: the near and far cascades, the cube shadow, the screen's AO. Full. |

  T14 (the sky visibility map) and T18 (wetness and puddle masks) will need units in the lit programs too, and 12–15 are
  full (T15 took 7 and 8): a frame texture for the model programs takes a unit below 12 that no material map the models
  use binds (the PBR materials use maps 0, 1, 2 and 5: albedo, the packed ORM, normal, emission; raylib's glTF import
  also fills 3 (roughness) and 4 (occlusion) and its OBJ import 6 (height) when the file has those maps, so 10 and 11
  are the safe ones there), and the terrain program keeps 9–11 for its arrays (units 0–6 are free in it). Beyond that,
  units 16 and up exist on the hardware this runs on (32 a stage on the GTX 980 Ti and Mesa) but need a check of
  `GL_MAX_TEXTURE_IMAGE_UNITS` and a fallback. A sampler type different from what a unit's
  other users bind is safe only through a different target (2D, cube, 2D array); keep each new unit's users in this
  table.
- **Constants shared with GLSL.** `MAX_POINT_LIGHTS` in `pbr/fragment.frag` should equal
  `WORLD_MAX_FORWARD_LIGHTS`; the shader clamps its loop and the renderer checks the arrays at load (and logs if
  the shader holds fewer), so a mismatch costs lights, never memory safety. The bone palette's size
  (`SKINNING_MAX_BONES` in `common/skinning`) is read from each skinned program at load the same way
  (`WorldRendererCommon_PreparePalette`). The instanced twins' constants (the passes' values, the most bands, the solid
  share, the G-buffer's fade limit) are pinned to their C mirror by `tests/ShaderSourceTests.c`.
- **Every geometry pass skins (T29).** A pass that draws models has a skinned twin of its shader (the same fragment
  stage over a vertex stage that includes `common/skinning`), passes both to `WorldRendererGeometry_DrawModel`
  (`WorldRendererMeshShaders`) and sets every per-object uniform on both while the entry has a pose (dither, outline
  flag, forward lights; the PBR factors go to the program the mesh draws with). Since T30 a pass says whether it draws
  the camera's view (`WorldRendererMeshShaders.IsCameraView`: the G-buffer, the lit pass, the unlit fallback) or a
  light's (the shadow passes): the camera's draw each entry's `CameraPose`, which for the first-person object
  (`WorldRenderer_SetFirstPersonObject`) is the render cache's copy of its pose with the bones its world model's
  first_person block hides, and their subtrees, collapsed onto the hidden bone's joint; the lights' draw `Pose`, the
  whole body. A palette is uploaded once per pose per
  pass (`WorldRendererPalette`), and every frame starts by forgetting which pose each palette holds
  (`WorldRendererGeometry_ResetPalettes`). A new geometry pass follows the same pattern, or its skinned meshes draw in
  their bind pose.
- **Every geometry pass instances (B03).** A pass that draws models has an instanced twin of its shader (the same
  fragment stage over a vertex stage that includes `common/instancing`, its definition defining `WORLD_INSTANCED`) and
  draws the model instances' cells with it (`WorldRendererInstances_Draw*`), setting the twin's per-entry uniforms
  (`instance*`), its per-cell ones (outline flag, forward lights) and the LOD per draw; without its twin, or while
  instancing is off, the pass draws each copy on its own through its ordinary shader with the C mirror's values. A new
  geometry pass follows the same pattern, or the copies are missing from it.
- **Terrain never skins and is never instanced** (B01): its chunks draw with one rigid shader per pass and need no
  skinned or instanced twin.
- **Direct OpenGL calls** (B02, decision W7) live only in `source/GLTextureArray.{h,c}`: raylib has no texture arrays.
  Anything else that needs GL raylib lacks goes there or into a module like it (its function pointers through
  `rlGetProcAddress`, its constants defined in it, a missing function or a GL error degrading with one log line), never
  into a pass.
- **Vertex attributes (the budget).** GL 3.3 guarantees 16 attribute locations (0 to 15). raylib binds 0 to 12
  (`RL_DEFAULT_SHADER_ATTRIB_LOCATION_*` in `rlgl.h`): 0 position, 1 texcoord, 2 normal, 3 colour, 4 tangent,
  5 texcoord2, 7 bone indices, 8 bone weights, 9 to 12 the instance transform (a mat4 takes four). 6 is named for the
  indices but only marks the element buffer's slot: `rlLoadShaderProgram` binds no attribute there, so it is free too. A shader that declares its own attribute picks a location no mesh it draws fills otherwise:
  - **9 to 12: the terrain's weights** (B01): sixteen palette weights as four normalized `u8 x 4` attributes
    (`vertexWeights0..3` in `terrain/vertex.vert`, filled by `WorldRendererTerrain.c` with `rlSetVertexAttribute`). A
    terrain mesh is never instanced, so the instance transform's locations are free there.
  - **13: the terrain's vertex flags** (B02): a normalized `u8 x 4` (`vertexFlags`; x 1 on a face material's vertices,
    whose faces the rules leave alone; y, z, w 0 and free for later terrain flags). Shared with B03's instanced shaders,
    which never draw a terrain.
  - **9 to 13 in the instanced twins** (B03, `common/instancing`): a copy's record, read once per copy (divisor 1)
    from the cell's range of its entry's buffer: 9, 10 and 11 the three rows of its matrix (`vec4` each), 12 its tint
    (a normalized `u8 x 4`), 13 its random value (a `float`). They take raylib's instance transform's locations (9 to
    12, which the twins do not use) and the terrain's, which no model mesh fills; the attributes are taken off a mesh's
    vertex array after each instanced draw.
  - **14 and 15: free.** A new attribute takes a location here, or shares one with a kind of mesh that never meets it
    in one shader, and is written into this list.
- **Failures degrade loudly once.** A missing shader or target falls back with one log line, never silently
  and never per frame.

## Shader assets (vertex + fragment)

`source/ShaderDefinition.c` loads an optional vertex stage and an optional fragment stage
(`vertex_location` / `fragment_location`; a bare `location` is a back-compat alias for the fragment stage).
Either may be omitted to fall back to raylib's built-in stage. Both stages accept every shader-stage extension,
so a paired shader's two source files need DISTINCT stems: `world_pbr` uses `shaders/pbr/vertex.vert` +
`shaders/pbr/fragment.frag`, referenced as `"pbr/vertex"` and `"pbr/fragment"`. The `world_normal` G-buffer shader
is a second such pair. The four sprite shaders (`world_sprite`, `world_normal_sprite`, `world_sprite_depth`,
`world_sprite_cube_depth`) share one vertex stage, `sprite/vertex` (the quad, `uvRect`, a decal's lift, the shadow
lookups' light-facing quads), each with its own fragment stage in its own directory. The skinned twins (T29:
`world_pbr_skinned`, `world_normal_skinned`, `world_depth_skinned`, `world_cube_depth_skinned` and the unlit fallback's
`world_unlit_skinned`) sit beside their rigid shaders, each a `skinned.vert` with the rigid shader's fragment stage
(`unlit/` has its own, raylib's default). Each renderer shader lives in its own directory under `asset/shaders/`. GLSL shared between
shaders lives in `asset/shaders/common/` and is pulled in with `#include "common/<name>"` (no `#version` in shared
files; rules in GameAssets.md, "GameShader"): `common/math` (`PI`), `common/atmosphere` (the air's uniforms, the
tables' layout and lookups, their march (through the ground for the haze, with light pollution's light) and T34's direct
march of single scattering, the haze volume's layout, see "Atmosphere"; used by the table builders, the volume's,
`world_sky`, `world_stars` and the haze's readers; since T34 the direct march's 16 view steps grow with the
square of their index, short near the viewer where every ray meets the densest air, and each step's light is dimmed by
the air in front of it and half its own: equal steps, each dimmed by all of its own air, had made the sky about 30 % off
and the horizon a dark brown band), `common/haze` (T35: the volume's readers, the ground haze's closed forms and the
compositing, see "Haze"; used by `common/lighting`, `world_sky`, `world_stars` and `world_postfx`), `common/night_sky`
(air mass, extinction and the skyglow profile, used by `world_sky` and `world_stars`) and `common/night_vision` (the eye's night vision: the rods' share
and the blend into their grey; used by `world_sky` and `common/lighting`), `common/lighting` (the frame's lighting: its
uniforms, the BRDF and its wrapped variant, the cascades' shadow, point lights and the cube shadow, the screen AO, the
night vision and the haze through `common/haze`: `ApplyAerialPerspective`; used by `world_pbr`, `world_terrain` and
`world_sprite`), `common/distance_packing` (the cube shadow's
packed distance; used by `world_cube_depth`, `world_sprite_cube_depth` and `common/lighting`), `common/dither` (the
dithered fades: the draw's fade and its part of a LOD cross-fade; used by every world geometry shader, see
"Visibility") and `common/skinning` (T29: the bone attributes and the palette, and a vertex's blended rows; used by
every skinned vertex stage, [Animation.md](Animation.md)), and `common/instancing` (B03: a copy's record attributes, its
fade, LOD band and draw per pass, see "Model instances"; used by every instanced vertex stage). Put new shared lighting
or noise code there instead of copying it between shaders.

The atmosphere's table builders (T15: `world_atmosphere_transmittance`, `world_atmosphere_multiscatter`,
`world_atmosphere_skyview`) and the haze's volume builder (T35: `world_atmosphere_volume`) share one directory,
`shaders/atmosphere/` (`atmosphere_*.json` with the fragment stages `transmittance`, `multiscatter`, `skyview` and
`volume` over raylib's default vertex stage): full-screen passes into the tables' and the volume's targets.

The instanced twins (B03: `world_pbr_instanced`, `world_normal_instanced`, `world_depth_instanced`,
`world_cube_depth_instanced`) sit beside their rigid shaders as the skinned ones do, each an `instanced.vert` with the
rigid shader's fragment stage, and their definitions add `"defines": ["WORLD_INSTANCED"]`: a shader definition's
`defines` (at most 16 GLSL names) are written as `#define` lines after the main file's `#version` line in both stages
(`ShaderSource.h`), so one fragment stage serves both twins and shared files test the name with `#ifdef`
(`common/dither` reads the copy's flat values under it; `common/instancing` refuses to compile without it).

## WorldTestFrame

Concrete `GameFrame` (embeds `GameFrame` first). Owns a `World`, a `GameClock` ([GameClock.md](GameClock.md)), a
`GameCamera`, and a `WorldRenderer`. **The test world is a file** (T09; since T10 its model components name world models, `world_models/test_world/`):
`Create` loads `<base>/worlds/test.world.json`
(`compile/out/worlds/` in the repository) with `WorldFile_LoadJSON` ([WorldFile.md](WorldFile.md)) on `Services.Keys`,
`Services.Assets` and `Services.Logger`, so its sprite components bind their world sprites themselves. A missing or broken
file is logged once (`WorldTest: could not load the test world (...): <why>. The world is empty instead.`) and gives an
empty world with the debug grid on: visible, and the frame still starts. The file holds what the frame's
`BuildTestWorld` built in code until T09 (the shipped file was written from it, and the image is the same bit for
bit): a model component per cube and for the ground slab, two light components, and the sprites, which name world
sprites since T26 (`world_sprites/test_world/`, each with the look the sprite components used to hold): the test image
as a camera-facing billboard, 1.5 m wide at 1280 pixels per metre, and T06's placeholders: a blended tire-mark decal on
the ground in front of the camera, an axis-billboard tree that casts a shadow, a fixed sign behind it, a lit blended
smoke puff in front of an additive, unlit glow around the warm lamp, and an unlit neon sign. The tree stands a second
time, mirrored by its placement (`tree_mirrored`: `overrides_flip_x`), left of the first and farther back (T26). Its hierarchy (T08): a `desk` at (1.8, 0.05, -3.6) on the left of the start view,
an empty object turned 15 degrees that holds its parts (`desk_top` and two `desk_side_*`, scaled test cubes), a gold
`cup` and a `desk_lamp` (a thin post) whose child `desk_lamp_light` is a warm point light (intensity 3, reach 4, no
shadow) 0.35 lamp units above the post's centre (0.28 m: a child's position is in its parent's scaled space). Its
world models (T10): two cubes behind the centre row, `variant_cube_left` (red) and `variant_cube_right` (blue), show the
`painted_cube` world model's two variants; the crisp cube and the shadow tiers of the PBR demo cubes, the cup and the
lamp are placement overrides (`overrides_omit_pixelation`, `overrides_shadow_tier`). Its LODs (T11): a row of six
`lod_test` spheres (`lod_test_1` to `_6`, ids 25 to 30) along the left edge of the ground, at x = 8 from z = -6 to 9,
switching at 13 and 16 m, and beside it `lod_test_forced` (id 31, LOD 2 forced) and `lod_test_own_distances` (id 32, its
own switch distances 3 and 6 m). Its collision (T12, [WorldCollision.md](WorldCollision.md)): 34 colliders (the slab,
the cubes, the desk's parts, the LOD spheres' hulls, the PBR cube's mesh, the cup's and the tree trunk's cylinders, the
sign's plane), a `door` (id 33, the `test_door` world model with a `closed` and an `open` set; movable since T27, a
kinematic mover that stops when blocked) on the right of the start view, and a `desk_trigger_zone` (id 34, an own box,
a trigger) at the end of the desk's slide; the desk is movable. The T27 content (all own boxes, every crate pushable
and movable): a `pusher_wall` (id 35, kinematic) behind a row of five crates `row_crate_1` to `_5` (ids 36 to 40) on
z = -1.5, a static `row_stop` (id 41) at the row's far end, and a `door_crate` (id 42) pinned in the corner of two
static posts (`door_post_west`, `door_post_north`, ids 43 and 44) in the door's swing. Its animation (T29,
[Animation.md](Animation.md)): four placeholder figures in a row at z = 2, facing the start view, each animator playing
a clip of its own: `humanoid_showcase` (id 45, x = -1.9: idle with a 0.3 s fade, the one the 5 key steps),
`humanoid_walker` (id 46, x = -5.2: walk from a quarter of the way in), `humanoid_short_waver` (id 47, x = -2.8: the
short figure waving, looped by its placement's override) and `humanoid_runner` (id 48, x = -6.1: run at half speed).
Edit the file and restart to change the test world.
The frame owns a `WorldCollision` too, created after the world, and since T13 a `WorldCharacters` (the character
system, [Characters.md](Characters.md)) created after that, with its test characters (the private
`source/WorldTestCharacters.{h,c}`: walk mode, the test NPCs and their scripted brains, the player controller, the
character events and status lines). Its `Update` runs the tick in the order of
[WorldData.md](WorldData.md) ("Ready for things that move"): the test NPCs' brains set their characters' inputs, the
transform drivers' step (it slides the desk while J
has it sliding, around the X it was loaded at; swings the door toward open or closed and moves the pushing wall toward
its far end or home, each from where it actually is, since the collision step may have stopped it), then the
characters (`WorldCharacters_Update`, last among the drivers), the collision step
(`WorldCollision_Step`), the queued events of that step, logged and cleared (`WorldTest: trigger 34
("desk_trigger_zone") enter: object 16 ("desk_top").`, `WorldTest: contact begins: object 33 ("door") and object 42
("door_crate"), 1 shape pairs, impact 0.71 m/s.`, `WorldTest: object 33 ("door") is blocked by object 42
("door_crate"), which cannot get out of its way; it stops.`), then the animation system (`WorldAnimation_Update`, T29:
the animators' rows and clips; since T30 their graphs and actions; slowed to a quarter while the 7 key's slow motion is
on) and its events (T30: logged; a `push` from the door's action swings the door, a `release` sets the cup down on what
is under it), then advances the sprites of effectively enabled objects
(`World_GetComponents(world, WorldComponentType_Sprite)`), calls `World_UpdateTransforms` (the tick's
world-matrix step), so the renderer reads cached matrices, and last logs the characters' events (the player's use
starting or ending an action, T30) and updates the player's look target. **`PrepareRender`** (T30, G35; GameFrame.md),
once per shown frame: the pose step (`WorldAnimation_Pose`), `World_UpdateTransforms` again (what the poses moved: held
items), then, in walk mode, the camera on the player's eyes (its body's eyes socket as posed) and the player named as
the renderer's first-person object (`WorldRenderer_SetFirstPersonObject`; 0 out of walk mode).
**Game time:** the clock starts from the world environment's date and time of day (noon, 2026-10-18) at time scale
720 (a 2-minute day, as the test world had before T23; the clock's own default is 72). Every `Update` advances it,
logs a new day when it crosses midnight, and writes its time of day and date into the environment, so the sky shows
the clock.

All input runs in `Update` at the fixed timestep and comes from `Services.Input` ([GameInput.md](GameInput.md)), after
the UI test frame above had first refusal: movement through the actions (`move_*`, default WASD + SPACE/CTRL, `sprint`
default SHIFT), mouse-look (`GameInput_GetMouseDelta`, this tick's movement, only while the cursor is captured), the
mouse wheel (T34: the flycam's speed, up faster and down slower, three notches to a doubling from 0.375 to 6144 m/s,
6 m/s at start; the sprint key still multiplies it by 3; each change is logged and the I key's camera line shows it;
not while walking, and the UI takes it first while the pointer is over a widget) and the debug keys as raw keys. A key the UI test frame takes never arrives here: R (it rebuilds the menu), Delete, and while
it navigates Tab, the arrows, Enter and Escape. Give the world test none of them (every letter key, F1 to F11 and the
digits are taken already, minus since B01, equals since B02, the keypad's 1 to 3 since T15, 4 to 8 since T35 and 9
since T36; the keypad's 0 is free. Leave F12 free: raylib's `EndDrawing`
takes a screenshot on it (`SUPPORT_SCREEN_CAPTURE`), writing `screenshotNNN.png` into the working directory):

- **the clock** (each change is logged with the date and time, e.g. `WorldTest: dawn (paused): Sun 2026-10-18
  06:00:00.`): P pause/resume the clock; 1–4 jump to dawn/noon/dusk/midnight of the clock's day (06:00, 12:00, 18:00,
  00:00) and pause; `[` `]` scrub the time while held (a tenth of a day per real second, across midnights, logged when
  let go); `,` and `.` the previous and next day, `/` a week ahead (the day keys keep the time of day);
- **the atmosphere (T15, see "Atmosphere"):** keypad 1 switches the atmosphere's tables off and on (the A/B: off, the sky
  is marched per pixel as before T15, single scattering, and nothing is hazed; logged), keypad 2 the environment's
  `multiple_scattering` between 1 and 0, keypad 3 its `ozone` between 1 and 0 (each logged: compare dusk with and
  without the ozone layer);
- **the haze (T35, see "Haze"):** keypad 4 steps the haze's debug view (off, a slice's in-scatter, its transmittance),
  keypad 5 the slice it shows (wrapping from the whole ray back to the first; logged with its distance:
  `WorldTest: haze view: the volume's in-scatter at slice 20 (3420.0 m).`), keypad 6 the environment's
  `aerial_distance_scale` (1, 5, 10), keypad 7 a morning mist (`ground_haze_density` 0.02, `ground_haze_thickness` 15,
  or no ground haze), keypad 8 the haze strength (`fog_strength` 1 or 0: clear air); each logged;
- **the moon and the seasons:** F pins the moon full (`MoonPhaseSource` fixed at 0.5) and gives it back to the
  calendar; Y switches the seasons (`AreSeasonsEnabled`). Every clock and moon log line carries the moon: `WorldTest:
  next day: Mon 2026-10-19 12:00:00; first quarter (phase 0.27, 59% lit, from the calendar).`;
- **the renderer and the sky:** O post effects, B bloom, G sun shafts, C crisp overlay, V night vision (logged), K
  sprite lighting (off draws every sprite unlit, as before T06; logged), L light gizmos, N cycle the
  light pollution (0, 0.35, 0.7, 1; logged), X step the opacity of the centre cube (`test_object`) and the tree (1, 0.5,
  0; logged: a 50% dither on the pixel grid, then gone, their shadows with them), U frustum culling (off draws everything
  within its draw distance; the image must not change; logged), I log the renderer status (with the drawn, culled and
  fading counts), then the game time (`WorldTest: game time
  Sun 2026-10-18 12:00:00, running, 720 game seconds per real second (a day takes 2 real minutes); seasons off.`), the
  moon, the collision world's counts (`WorldTest: collision: 37 colliders (23 static, 14 movable), 293 shapes (61180
  mesh triangles, 0 infinite planes), 0 trigger pairs; 37 rebuilds and 6 moves in 3 syncs.`: since B01 the terrain is
  one static instance of 256 chunk shapes) and its last step's (T27:
  `WorldTest: the last collision step: 0 movers, 0 pushables awake, 0 pairs of shapes, 6 touching contacts (6 object
  pairs in contact), 0 solver iterations in 1 substeps (0 solved again), deepest overlap left 0.00 mm, 0 blocked, 0
  crushed.`), the atmosphere's tables (T15: on, off or unavailable, their sizes, how often they were built, the height
  the sky is seen from) and the frame times (`WorldTest: the last frame took 16.67 ms (60.0 a second; ...); its update
  0.41 ms and its render 2.10 ms on the CPU ...`: the frame time is the render's delta, the update and the render are
  WRStopwatch times);
- **the hierarchy (T08):** J starts and stops the desk sliding back and forth along X (1.2 m either way, a 6-second
  period of real time): its parts, the cup, the lamp and the lamp's light follow; H hides and shows the desk, and with it
  everything on it (the lamp's light goes off). Both logged;
- **the world models (T10):** E steps the two variant cubes through their variants: their own (red and blue), swapped,
  then none (`uses_variant` off: the model's own white), through the component's setters, so the cache resolves the
  variants again (logged); Z zooms the camera to a 15-degree view and back (logged): the zoom factor then lets the draw
  distances of models and (since T26) sprites, and the LOD distances (T11), reach about 4.4 times as far;
- **the LODs (T11):** Q toggles the LOD tinting (logged): every model takes its LOD's colour (0 white, 1 green, 2 yellow,
  3 red, coarser magenta; LODs past 0 glow faintly, for the night), to watch the switches along the `lod_test` row;
- **collision (T12, T27):** F7 shows and hides the collider wireframes (the collision world's debug list, drawn by the
  renderer: world grey, prop orange, triggers violet, static ones darker; logged); F8 the contact points (white crosses
  touching, grey near, cyan normals, red pushes; logged). J slides the desk through the trigger zone: its parts log
  enter and exit. F6 swings the door open (a quarter turn about its hinge at 60 degrees a second) and closed: it stops
  on the crate pinned in its way (blocked, logged), and closing pushes the crate along. F9 sends the pushing wall out
  2.5 m along +X at 0.6 m/s and back: it pushes the row of crates against the stop, where it is blocked and stops;
  home again, the crates are teleported back where they were. F10 teleports the door's crate 1.2 m aside, out of the
  door's way, and back into its corner (pushed out of the door if they overlap). Each logged, with the contact and
  motion events;
- **render space and double positions (T32, T33):** F11 steps the test world through the far shift's places
  (`FarShifts`): out to (20 000, 0, 20 000) m, 28 km from the world's origin, then to (100 000, 0, -100 000) m, 141 km
  out, then back to the start. Every root object, the player and the test NPCs (through the character system's
  teleport) and the camera move along (logged: `WorldTest: far shift 1 of 2: 38 root objects and the camera moved by
  (20000, 0, 20000) m, as teleports, and 0 characters placed again on their ground; ...`). The objects move as
  teleports (the collision step does not sweep 28 km), the collision world is synced before the characters are placed
  on their ground, and the frame's own places (the desk's slide, the pushing wall's and the crates' homes, the NPCs'
  circles) move along; the first press also logs the collision world's one-time warning for each static object it
  moves (static objects are not meant to move in play). Out there everything looks and plays as at the origin
  (positions are doubles: walking, jumping, crouching, the door, the crates and the NPCs), and a trip out and back
  returns everything to within a double's step (1.5e-11 m at 100 km); the debug grid stays at the world's origin. I
  adds the camera's position, its distance from the world's origin and the far shift;
- **the world file (T09):** F5 saves the live world next to the test world, as `worlds/test.saved.world.json` and
  `worlds/test.saved.world.ghdf` (atomically; each logged with its path, gitignored), for round-trip checks: saved at
  noon with nothing changed (2, then F5) the JSON is the shipped file byte for byte; otherwise its diff against
  `test.world.json` shows what changed in the game (the time of day once the clock ran, a slid desk, an opacity);
- **the characters (T13):** F1 switches walk mode on and off: walking creates a `player` object (a runtime object, never
  saved) with a character component of the test human below the flycam, dropped to the ground, and the camera then
  sits at its eyes (`PlayerController`: WASD walk relative to the view, SHIFT runs, SPACE jumps (the `jump` action),
  CTRL crouches while held (`crouch`), E uses what you look at (`use`, logged with what it hit; it takes the E press
  from the variants key while walking), the mouse looks around). F2 spawns and removes the test NPCs 3 m in front of
  the camera: a human walking a square and crouching every third side, a dog (brown) running circles and lying down, a
  pigeon (grey-violet) walking, taking off, flying a loop and landing: each a character driven by a scripted pattern
  through the same `CharacterInput` as the player; the dog and the pigeon wear a placeholder body of two test cubes
  (the trunk squashes when it crouches), the human (since T30) the animated placeholder humanoid. F3 lets the player fly
  and lands it (SPACE and CTRL climb and sink while flying), F4 turns the player into the next kind (human, dog, pigeon:
  the same object, another character asset; a human wears the humanoid, and what its hand held is set down when it stops
  being one). The log names what you look at whenever it changes and every character event (jumped, landed with its
  speed, left the ground, crouched, stood up, took off, stopped flying, used); I adds `WorldTest: you: a human
  (test_human), walking, standing, on object 2 ("ground"), 1.40 m/s, eyes 1.62 m up; looking at ...; in no action;
  ...`;
- **the bodies and their actions (T30, [Animation.md](Animation.md)):** as a human the player wears the humanoid and
  sees it in first person (looking down shows the chest, arms and feet, never the head; its shadow has one). E on an
  object that offers an action (its `tbp:action` text, on it or an ancestor) starts it on the player: the bench (49)
  `sit` (the player slides onto its seat and sits; E again stands up), the coffee cup on the desk (50) `pick_up` (the
  player slides in front of it, reaches, takes it in its right hand and holds it; refused while the hand is full) and
  the door (33) `open_door` (the player slides to it and pushes it: the frame swings it as F6 does). **9** puts down what
  the player holds (`put_down`: it leaves the hand at the reach and is set down on what is under it), **0** has the
  player (a human) and the NPC human wave with the right arm over whatever they do, **8** sends the NPC human to the
  bench: it walks to a point in front of it and starts the same `sit` (8 again gets it up). Every action's start, end or
  failure and every graph event is logged (`WorldTest: you: action "pick_up" starts (target 50 "coffee_cup").`,
  `WorldTest: you: "grab".`);
- **animation (T29, [Animation.md](Animation.md); the private `source/WorldTestAnimation.{h,c}`):** 5 steps the
  showcase figure (`humanoid_showcase`) through the placeholder's clips (idle, walk, run, crouch_idle, crouch_walk,
  jump, fall, land, wave, and since T30 sit_down, sit_idle, stand_up, pick_up, hold_r, put_down, open_door), each a
  cross-fade over its animator's 0.3 s (logged); 6 brings out a crowd of 100 figures
  (runtime objects, never saved; tall and short alternating) in a square in front of the camera, facing it, each
  walking at its own phase and a little faster or slower, and takes it away; 7 slows every animation to a quarter and
  back (logged). I adds `WorldTest: animation: 104 animators (104 posed), 104 clips playing, 1976 bones posed in 182 us;
  the showcase plays "idle" (5); crowd out (6); slow motion off (7).`;
- **the terrain (B01, [Terrain.md](Terrain.md)):** minus (`-`) shows and hides the chunk key (logged): each chunk takes a
  colour by its cell (neighbours always differ) mixed into its lit colour, and the wireframe of the chunks in view is
  drawn over it, a hair toward the camera. The status line (I) counts the chunks drawn and their triangles, and (B02)
  lists each terrain's materials and its arrays' sizes. **Equals** (`=`, B02) cycles what the terrain shows (logged):
  shaded, the painted weights (B01's debug colours, before the rules), the weights after the rules, the dominant material
  after height blending, then shaded again;
- **the model instances (B03, the private `source/WorldTestInstances.{h,c}`; see "Model instances"):** the test world's
  `grove` (object 52) is 400 copies of the placeholder tree on the terrain in front of the start view (3 LODs, at 30
  and 70 m; a third stretched upward, tinted, every 40th half transparent; written by
  `asset_source/scripts/placeholder_instances.py`), with a warm lamp casting a cube shadow in a clearing about 50 m
  ahead (`grove_lamp`, object 53: the cube shadow is the nearest shadow-casting lamp's, so it takes over from the slab's
  warm lamp once the camera is nearer to it). The **apostrophe** (`'`) shows and hides the **pebble field**
  (logged): 20 000 copies of the placeholder pebble scattered in front of the slab, made when it is first shown as a
  runtime object (never saved) at the terrain's object, each at the ground's height under it (a raycast); the
  **semicolon** (`;`) switches instanced drawing (logged): off, every copy is drawn on its own through the ordinary
  shaders, and the picture must not change (it may round differently by a level here and there). Q's LOD tinting
  colours the copies' LODs too, and the status line (I) gives the copies by LOD;
- T toggles the text sample.

M frees the cursor for the UI (and captures it again); a left click the UI did not take captures it again. An
unconsumed `pause` (Escape once the UI is not using it) quits through `GameFrameManager_RequestStop`. `Render` handles
no input. Captures the cursor in `Start`, releases it in `End`/`Destroy`. The world is loaded and the clock built in `Create`;
`BeginLoad` calls `WorldRenderer_PrepareWorld` (shaders and models), so the stepped load/unload slots are trivial.
`Create` builds everything up front so `Destroy` is always safe (even on forced teardown); it destroys the renderer,
then the collision world (it borrows the world, and the renderer borrowed it), then the world, the clock and the text
sample.

**Text sample** (shown by default, T toggles it): a `TextDocument` built from the `TEXT_SAMPLE_JSON` text component
JSON in `Create` (so the real game runs the codec), with `noto_sans` as its default font, laid out in `Update`
(`TextLayout_Refresh`) and drawn in `Render` over the final image, through a `RenderContext` on the frame target. It
shows Latvian and Cyrillic letters, colors, underline, strikethrough, a translate fallback, the `test_sprite`
animation inline, and one line in the `test` font (its missing letters draw as `?`, and its load logs which
codepoints the file lacks). A sample that fails to load is logged once and leaves the sample empty.

## UITestFrame

A debug `GameFrame` added on top of `WorldTestFrame` (`include/UITestFrame.h`, `source/UITestFrame.c`), so it gets the
input first. It owns a widget factory, a `UIScreen` (the window's size as its viewport, `UIScreenInput_CreateFromWindow`
as its input) and a few labels in `noto_sans`: a hint at the bottom left (input-disabled) and a focusable "menu" label at
the top right with three focusable item labels inside it. Hover and focus notifications tint the labels (yellow /
blue) and gaining focus plays a short size animation; Delete destroys the focused item, R rebuilds the menu. Tab or the
arrows start keyboard navigation, Enter opens the menu, Escape closes it and then lets go of the UI, after which Escape
reaches the world test (which quits). With the cursor captured (mouse look) the screen gets no pointer; M in the world
test frees it. It renders into its own transparent target. Known limitation: frames are composited with plain alpha
blending, so the antialiased edges of text on a transparent frame come out thinner than on an opaque one (fixing it
means premultiplied compositing in the frame manager).

## Bootstrap (main.c)

Order: base directory (`FileSystem_GetExecutableDirectory`) → Logger (`<base>/logs`) → if the executable's
directory was unavailable, warn and fall back to a copy of `GetWorkingDirectory()` → load config
(`<base>/config.json`) → load unicode DB (`<base>/asset/text/unicode_data.txt`, empty-DB fallback) →
`InitWindow` (fullscreen/vsync flags from the config) + `SetExitKey(KEY_NULL)` (Escape is the UI's `ui_back` and the
game's `pause` action, never raylib's quit key) → AssetManager (`Construct1` with the logger, `CreateStandardAssetTypes`,
cache directory `<base>/.wr_asset_cache`, absolute `<base>/asset` search root, `ReadDefinitions`) → GHDF pool → the
key registry (`KeyRegistry`, [NamespacedKey.md](NamespacedKey.md)) → `GameInput` (defaults, then `<base>/controls.json`
if present) → GameFrameManager (polls the input) → build `Services` (`Services.BaseDirectory`, `Services.Input`,
`Services.Keys`) → create + add `WorldTestFrame`, then `UITestFrame` on top → run loop.
Teardown (reverse, ordering matters): frame manager (releases assets + GPU targets, needs asset manager + GL alive;
its worlds borrow the key registry) → asset manager → JSON pool → GHDF pool → `CloseWindow` → key registry → input →
unicode → config → base-directory buffer → logger.

Every game file is resolved against the executable's directory (built via `Path_Append(base, relative)`), so
the game works no matter where it is started from.

## Asset files next to their definitions

Within a type's directory a `.json` file is a definition and never a resource, so a definition may share a stem
with the file it loads (`test.json` + `test.png`). A location matches files by their whole last name and the
loader's accepted extensions; more than one candidate is an error. See [AssetManager.md](AssetManager.md).

## Status

Builds clean under the project's warning set. Nothing that needs a GL context can be checked headlessly:
agents verify the cache, culling and world logic with the tests in `tests/`, and the owner runs the game to check
the rendering (start with the status line the renderer logs after its first frame).
