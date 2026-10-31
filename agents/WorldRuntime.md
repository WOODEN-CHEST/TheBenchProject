# World runtime layer (camera, renderer, services, bootstrap)

The runtime pieces that turn the (rendering-independent) world data layer into something you can see and
fly around: the camera, the world renderer, the services bundle, the test frame and the bootstrap in `main.c`.
See [WorldData.md](WorldData.md) for the data model it draws.

## Modules

| Module | Files | What it is |
|---|---|---|
| GameCamera | `include/GameCamera.h`, `source/GameCamera.c` | The 3D camera (a double position, fov/yaw/pitch/roll), converted to raylib's render camera at the origin only at render time. |
| Services | `include/Services.h`, `source/Services.c` | A bundle of BORROWED pointers to the shared game services (logger, config, unicode, asset manager + standard type ids, frame manager, per-tick input, GHDF pool, the key registry, the root registry, base directory). |
| WorldRenderer | `include/WorldRenderer.h`, `source/WorldRenderer*.c`, `source/WorldRendererInternal.h` | Renders a World through a GameCamera; fully separate from the world data. Split by render pass, see "Renderer structure". |
| WorldRenderCache | `source/WorldRenderCache.h`, `source/WorldRenderCache.c` (private) | The renderer's per-component cache and per-frame draw lists (models with their resolved world models, their LOD state, the variant, the settings in effect and their pose; the camera's pose, the first-person object's with its hidden bones collapsed; sprites with their world sprites' settings in effect; lights; terrains and their chunks; the model instances components with their cells), built from the world's per-type component arrays. |
| WorldInstancing | `source/WorldInstancing.h`, `source/WorldInstancing.c` (private) | Instanced drawing as pure maths (see "Model instances"): a component's copies sorted into cells with their records, the copies' LOD bands, fades and per-pass draws, and the LODs a cell can show: the C mirror of `shaders/common/instancing.glsl`. |
| WorldLod | `source/WorldLod.h`, `source/WorldLod.c` (private) | Levels of detail as pure maths (see "LODs"): the LOD selection by distance with hysteresis, the scale and a forced LOD, and the cross-fade's state and its two draws. |
| WorldAtmosphere | `source/WorldAtmosphere.h`, `source/WorldAtmosphere.c` (private) | The atmosphere as pure maths (see "Atmosphere"): the air's coefficients from the environment, the height the sky is seen from, heights and distances along rays without cancellation, and the lookup tables' layout: the C mirror of `shaders/common/atmosphere.glsl`'s mappings. |
| WorldRenderSpace | `source/WorldRenderSpace.h`, `source/WorldRenderSpace.c` (private) | Render space as pure maths (see "Render space"): the frame's origin and its rotation-only view and projection, the C mirror of the sky shader's ray, the shadow levels' boxes snapped to their texel grids, each grid anchored to its last box, and a kept box rebuilt for a new origin. |
| WorldShadowLevels | `source/WorldShadowLevels.h`, `source/WorldShadowLevels.c` (private) | The sun's and the moon's shadow levels as pure maths (see "Sun and moon shadows"): their sizes and texels, the levels the reach needs, which level a point reads (the C mirror of `common/lighting`'s choice), the size cut, the LODs far levels pick, and which levels render each frame with the light frame each kept one is read with. |
| WorldVisibility | `source/WorldVisibility.h`, `source/WorldVisibility.c` (private) | What each pass draws this frame (see "Visibility"): frustum planes and sphere tests, draw distances and their fade band, opacity, the dither's C mirror, and the per-pass lists built from the render cache. |
| SpriteTransform | `source/SpriteTransform.h`, `source/SpriteTransform.c` (private) | A sprite quad's pure maths: its matrix for a viewer (billboard, axis billboard, fixed; the camera or a light), its world size, its bounds, and the back-to-front order of a transparent list (see "Sprites"). |
| NightVision | `source/NightVision.h`, `source/NightVision.c` (private) | The eye's dim-light vision as pure maths: its calibration on the game's light scale, the rods' share, the rods' grey (see "Night vision"). |
| WorldWeatherLight | `source/WorldWeatherLight.h`, `source/WorldWeatherLight.c` (private) | The weather's look as pure maths (see "Weather"): the clouds' deck from the environment's weather values (their cover, the light they let through, their transmission), its light added to the sky's light at the camera, a lightning flash's light and direction, the wetness and the wind. |
| WorldInterior | `source/WorldInterior.h`, `source/WorldInterior.c` (private) | Interior lighting as pure maths (see "Interior lighting"): ambient zones in render space, their weights, the draws' choice of zones and their composition, the haze inside them, the sky's occupancy volume (its map, slices and window) and the march through it, the camera's interior and the light the eye is in: the C mirror of `shaders/common/interior.glsl`. |
| WorldTestFrame | `include/WorldTestFrame.h`, `source/WorldTestFrame.c`, `source/WorldTestCharacters.{h,c}` (private: walk mode and the test NPCs), `source/WorldTestAnimation.{h,c}` (private: the animation system and its keys), `source/WorldTestInstances.{h,c}` (private: the pebble field and the instancing A/B) | A concrete GameFrame: a WASD + mouse-look flycam over a test world, and walk mode (the player as a character) and test NPCs. |

`source/main.c` is the composition root (bootstrap).

## GameCamera

Coordinate convention: **+Y up, +Z forward** (yaw=pitch=roll=0 looks toward +Z). Plain value type (no
Construct/Deconstruct — like ProgramTime); build with `GameCamera_Create`, mutate public fields directly.
`Position` is a `Vector3Double` (far from the world's origin a float's steps would shake the view); the
angles, the field of view and the directions stay floats. Code that moves the camera works in double (the flycam
adds its float movement to the double position; the player controller puts it at the character's eyes, a double).
`GameCamera_GetForward` derives the view dir from yaw/pitch; `GetRight` is the horizontal strafe axis
(`cross(worldUp, forward)`, roll-independent); `GetUp` rotates world up around forward by roll.
`GameCamera_ToRenderCamera` is the single place a raylib `Camera3D` is produced: the camera in render space, at
(0, 0, 0) with forward as its target (see "Render space"), so `BeginMode3D` loads a view matrix that is a pure
rotation. Instance/runtime data — never saved to a world. With +Z forward and +Y up in raylib's
right-handed pipeline, world +X renders on the camera's LEFT; this is intended.

## Render space

The renderer draws in **render space**: the world's orientation with the camera at (0, 0, 0). A float holds a world
position only to millimetres or worse far from the world's origin (its step is 0.24 mm at 2–4 km, 1 mm past 8 km),
which would make the world and the sky shake there; an offset from the camera keeps a float's full precision near the
camera, however far out it is. The maths is the private `source/WorldRenderSpace.{h,c}` (tested in
`tests/WorldRenderSpaceTests.c`).
- **The origin.** Each frame takes the camera's position, in double, as its origin (`WorldRenderFrame.Origin`). Every
  position the GPU sees is that position minus the origin, subtracted in double and rounded once
  (`GameMath_ToVector3Relative`, `GameMath_ToMatrixRelative`, [GameMath.md](GameMath.md)). Never round a far position
  to float and subtract after: the float keeps the absolute position's error.
- **One place converts.** `WorldRenderCache_Sync` takes the frame's origin: every entry keeps its world placement in
  double (`WorldMatrix`, `Center`, the sprites' and lights' `Position`) and the same placement in render space
  (`RenderMatrix`, `RenderCenter`, the sprites' `Shape` and `FixedTransform`, the lights' `RenderPosition`). The passes,
  the visibility, the LOD selection, the light culling and the uniforms read only the render-space values. The
  world's positions and world matrices are doubles ([WorldData.md](WorldData.md), "Positions are doubles"), and the
  sync reads them as they are. Debug draws convert the same way (the collision world's double transforms and contact
  points, the grid around the world's origin).
- **The view** (`WorldRenderSpace_BuildView`): raylib's camera at the origin looking along the camera's forward, so
  the view matrix is a pure rotation and the view-projection and its inverse hold no large numbers. The sky's ray is
  `normalize(invViewProj × the point at clip depth 1)` (the far plane's, or with reversed depth the near plane's: the
  same direction; the C mirror is `WorldRenderSpace_GetSkyRay`), the stars and the sun's
  place on screen (the sun shafts) are directions projected with w = 0, the outlines' sun is the sun's direction turned
  by the view. Measured against a double-precision camera 50 km out: points 0.6 m to 30 m away land within 2e-5 scene
  pixels and the sky's rays within 3e-5 (world-space floats would miss by 0.9 and 23 pixels).
- **Reversed depth** (`source/GLDepth.{h,c}`, `WorldRenderSpace_GetReversedProjection`): where the
  GPU has clip control (OpenGL 4.5 or `GL_ARB_clip_control`), the camera's passes (the scene, the G-buffer, the crisp
  overlay) draw with a projection that has no far plane: x and y as raylib's, depth 1 at the 5 cm near plane and
  near / distance beyond it, toward 0 at infinity, into 32-bit float depth buffers cleared to 0 and tested `GL_GEQUAL`.
  A float's exponent spreads its precision evenly over the orders of magnitude, so a hill 20 km away is resolved to about
  a ten-millionth of its distance, 2 mm (raylib's 24-bit depth steps about 19 m at its fixed 4 km far plane, which
  nothing beyond passes). The frame's projection (`WorldRenderFrame.Projection`, its inverse in closed form) is the reversed one; each
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
- **Stable shadow levels** (`WorldRenderSpace_GetCascade`, `WorldRenderSpace_StepCascade`; the levels are "Sun and moon
  shadows"): each level is a fixed-size box around the camera along the shadow light, its centre kept on the level's
  texel grid (1.6 cm for the first, four times that for each one after it) in the light's frame, in double. Each frame
  a level renders, its centre moves from where it was (its anchor, which `WorldRenderSpace_StepCascade` keeps in the
  level's `WorldRenderCascadeGrid`) to the camera by whole texels along the light's right and up axes; a frame that
  keeps a level's map builds its matrices again from that anchor and the light it was drawn with
  (`WorldRenderSpace_GetCascadeAt`), relative to the new origin. While the light holds still the grid stands still in the world, so shadow edges do not
  crawl while the camera moves (a point keeps its texel; when the centre steps, the map's contents move by one whole
  texel). While the sun or the moon turns, the grid turns about the anchor, within half a texel of the camera, so edges
  near the camera slide as they would under a box centred on the camera, wherever the camera is: a point 4 m from a
  still camera moves 0.24 texels a frame on the first level's map with a 2-minute day, 0.016 with a 24-minute day,
  0.0004 with a 24-hour day, from 7 m to 28 km out. (A grid fixed to the world's origin would turn about that origin: 0.38–0.48 texels
  a frame of shimmer far out, whatever the clock's speed.) Stepping the shadow direction's updates is not done.
- **The rule for every shader:** "world space" in a shader means render space. `fragPosition` is the fragment's
  position relative to the camera; light positions (`pointLightPositions`, `pointShadowLightPos`, the cube depth
  shaders' `lightPosition`) and the shadow matrices (`lightVP`, `lightVPFar`) are in render space; the camera sits at
  the origin, so there is no camera uniform: the view vector is
  `-fragPosition`, the haze's distance `length(fragPosition)`, a decal's side `dot(normal, -position)`. Directions (the
  sun, the moon, normals) are the world's. The haze, specular and `PerturbNormal` (`dFdx` of the position) need no
  change for it and have no large-coordinate error. The haze's heights come from the origin's Y in double (the
  camera's height above the ground haze's base and above the atmosphere's ground), never from a render-space position.
- **Absolute coordinates in a shader** (world-anchored noise, rain streaks wrapping around the camera, wave phases,
  puddle masks): a shader never receives the origin as floats. It gets
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
  longest axis falls short), and `IsDrawn`: its world model resolved and its object
  effectively enabled and visible (`WorldObject_IsEffectivelyEnabled` / `_IsEffectivelyVisible`: a disabled or hidden
  ancestor counts). The world matrix and the sphere's centre are kept in double (`WorldMatrix`, `Center`) and in render
  space (`RenderMatrix`, `RenderCenter`: relative to the frame's origin, subtracted in double). Passes draw only the
  `IsDrawn` entries; a hidden object keeps its entry and its world model. Every
  pass draws a model one LOD at a time through `WorldRendererGeometry_DrawModel`, which applies the entry's variant to
  the per-draw material copy (`WorldModel_ApplyVariant`) before the lit pass uploads the copy's factors. An entry
  also takes its object's **pose** (`Pose`: the animator's, when it was made for the entry's world model,
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
- **Terrains** ([Terrain.md](Terrain.md)): one entry per terrain component (`WorldRenderTerrain`), kept across
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
- **Model instances** (see "Model instances"): one entry per `model_instances` component
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
whole arrays. The lists hold indices into the cache's models, sprites and terrain chunks, ascending (so the
world's order is kept), and are rebuilt every frame without allocating once grown; each entry also gets its fades. Everything here is in render
space: the cache's render-space centres, the camera at (0, 0, 0), the frustum, the shadow levels' boxes and the cube's
lamp all relative to the frame's origin.
- **Frustum culling.** The camera's frustum is extracted once per frame from its view-projection (Gribb and Hartmann, six
  unit inward planes; `WorldVisibility_ExtractFrustum`; with reversed depth, `WorldVisibility_ExtractReversedFrustum`:
  the same sides and near plane, and a far plane everything passes). The scene, the G-buffer and the crisp overlay draw
  only what touches it (`WorldVisibility_IsSphereInFrustum`). A model's sphere is its bounding sphere (around `RenderCenter`); a
  sprite's is the sphere around its pivot that holds its quad in every orientation (`BoundsRadius`: half its diagonal
  around its centre for the default centre pivot), so one sphere serves the camera and the lights.
- **Shadow casters are never culled by the camera:** a caster off screen can still shadow what is on it. Each shadow
  level that renders this frame takes the casters in its own orthographic box (its planes, from the light matrices
  `WorldRendererShadows_PrepareFrame` builds once per frame) whose sphere is at least its size cut
  (`WorldVisibilityShadowLevel.MinRadius`) and reaches within the shadow reach; past the first level, not those lying
  inside the level before's sides by more than their radius and the level's inner inset (its ring:
  `WorldVisibilityShadowLevel.InnerInset`, see "Sun and moon shadows"); a kept level gets no list. The cube shadow takes
  the casters touching its lamp's reach (the shadow reach does not limit it). A model casts when it has
  `casts_shadows` and its shadow fade is below 1; a sprite when it is a cutout sprite that is not a decal and casts
  (`WorldRenderSprite_CastsShadows`). A shadow-only entry (`is_shadow_only`) is in every shadow list and the sky's
  volume, never in the camera's.
- **Shadow distance.** Each caster's shadow fade is the larger of its fade and the fade of its shadow distance, measured
  as its draw distance is (`WorldVisibility_ScaleShadowDistance`: its own times the player's `shadow_distance_scale`
  unless fixed; without one, its draw distance times its shadow distance multiplier, scaled as the draw distance is and
  then by the shadow scale; never beyond its scaled draw distance). Beyond it an entry casts into no shadow pass.
- **Draw distance.** Each model and each sprite has its own ([WorldModels.md](WorldModels.md),
  [WorldSprites.md](WorldSprites.md)): its world model's or world sprite's
  `draw_distance` (a world sprite without one: its kind's, 150 m or 60 m for a decal, decided per placement), or its
  placement's own while `overrides_draw_distance` is on, times the placement's `draw_distance_multiplier` (the cache
  resolves it); then times the config's `draw_distance_scale` (default 1, [0, 16]) unless the asset or the placement
  keeps it fixed (`WorldVisibility_ScaleDrawDistance`). The distance measured is the camera's to the nearest point of
  the entry's sphere (0 inside it), so a large object does not fade while its near end is close, multiplied by the
  **zoom factor**, `WorldVisibility_GetZoomFactor(fovY)` = tan(fovY / 2) / tan(30 degrees)
  (`WorldVisibilityView.ZoomFactor`, from the camera's field of view): exactly 1 at the default 60 degrees, below 1 when
  zoomed in, so a zoomed view (binoculars, a camera) keeps far models and sprites instead of fading them. The LOD
  selection uses the same factor. Beyond its draw distance an entry is in no list (its shadow goes too); over the last
  stretch (10% of the distance, at least 3 m, at most all of it) its distance fade rises linearly from 0 to 1.
- **Opacity.** The tint's opacity (the alpha byte of the final tint) asks for a fade of 1 - opacity, and a draw's fade is
  the larger of that and the distance fade. An entry whose fade is 1 is in no list. Models do not use alpha blending:
  the PBR output alpha is 1 and their opacity is the dither (`// TODO:` in the PBR shader: alpha-tested material
  textures, leaves on a model, need a material alpha mode).
- **How a fade shows.** Models and cutout sprites dither by their fade, in every pass that draws them (the scene, the
  crisp overlay, every shadow level and the cube shadow: there by their shadow fade), so a fading object's shadow thins
  with it. Blended and additive
  sprites are transparent already: their draw-distance fade scales their alpha (their opacity is in it already) and they
  do not dither. The G-buffer never dithers (holes would break the outline into noise and speckle the AO): it draws an
  entry solid while its fade is below 0.5 (`WORLD_VISIBILITY_GBUFFER_FADE_LIMIT`) and skips it from there on, so the
  outline and the AO switch off once, at mid-fade. The outline pass reads the G-buffer's depth for the same reason.
- **The dither** (`shaders/common/dither.glsl`, included by every world geometry shader: `world_pbr`, `world_normal`,
  `world_depth`, `world_cube_depth`, `world_sprite`, `world_normal_sprite`, `world_sprite_depth`,
  `world_sprite_cube_depth`, `world_terrain`; each tests `DitherDiscard()` once). Uniforms `ditherFade` (this draw's fade: 0 solid .. 1
  gone), `ditherLodProgress` and `ditherIsIncomingLod` (its part of a LOD cross-fade: the share of the pixels the
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
  each of its pixels drawn by one LOD. (The incoming LOD is not a mirrored pattern at fade 1 - p: computing 1 - p in
  floats would draw a few pixels twice right next to the pattern's thresholds, so both compare the progress itself.)
  In the crisp overlay every window pixel of one scene pixel gets that scene pixel's cell, so its dither sits on the same
  pixel grid as the rest.
  Shaders that take derivatives or sample mipmapped textures test it after those (the PBR shader last), so they stay
  defined for the pixels that are drawn.
- **Terrain chunks** enter every list as models do, each by its own sphere: the camera's frustum, each
  shadow level as a caster (when it casts, within its shadow distance), the cube's reach, the draw distance (4 km and fixed by
  default, so the quality setting never thins the hills; the zoom factor applies) and the fade, which their draws
  dither (and the G-buffer's limit) like a model's. Their lists are their own
  (`WorldVisibility_GetTerrainChunks(visibility, cache, pass)`, the fades `WorldVisibility_GetTerrainChunkFade`), and
  their counts too: a chunk is not counted as a model.
- **Instance cells** enter every list as models do, each by its sphere around all its copies: the camera's
  frustum, each shadow level as a caster (when it casts, within its shadow distance), the cube's reach, the draw distance
  measured to the sphere's nearest point (the quality setting scales it unless fixed; the zoom applies) and the opacity
  (the object's: each copy's own alpha fades only that copy, in the vertex stage). A cell no copy of which can be seen
  leaves every list; inside one, every copy fades and picks its LOD on its own (see "Model instances"). Their lists and
  fades are their own (`WorldVisibility_GetInstanceCells`, `WorldVisibility_GetInstanceCellFade`), and their counts too
  (`DrawnInstanceCellCount`, `DrawnInstanceCount`: the copies of the drawn cells, `InstanceCellsOutsideViewCount`,
  `InstanceCellsFadedOutCount`).
- **A/B.** `WorldRenderer_SetCullingEnabled` (U in the test frame) switches the camera's frustum and the shadow levels'
  boxes and rings off: everything within its draw distance is drawn, and the image must not change. The
  draw distances, fades, the size cut, the shadow reach and the cube's reach stay.
- **Status.** The status line (after the first render, the I key) gives what the camera's passes drew (models, sprites,
  how many of them fading; the models by LOD and how many cross-fade), how many entries lay outside the view, and how
  many faded out (beyond their draw distance or fully transparent); the terrains placed (and those without a
  terrain), the chunks drawn of all listed and their triangles, and whether the chunk key is on; a line of
  its own for the model instances: the components (and those without a world model), their copies and cells, the cells
  drawn with their copies and how (instanced, or one at a time), the cells outside the view and faded out, and the
  camera's copies by the LOD they show (the larger share), how many sit in a band and how many faded out, worked out
  with the C mirror when the line is logged (never per frame).
- **Known limits.** The cube shadow of a fading object is grainy: its 20-tap PCF spreads over about 17 texels and samples
  the dither pattern unevenly (the levels' bilinear compare weighs the 2 x 2 texels around each point of the pattern, so
  their shadows thin smoothly). Without reversed depth the far plane is extracted from a float matrix, about 10 m uncertain at
  4 km.

### LODs

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
  (`WorldLodState_GetSolidLod`: the incoming one from half the fade on), solid, as it draws a fading object
  solid; the first shadow level and the cube shadow draw the LOD the object heads to (`WorldLodState_GetShadowLod`),
  solid (only the object's own shadow fade dithers them), so a shadow changes once per change and never shimmers; a
  level past the first at least the LOD the object would show at the level's inner edge; an object with its own
  `shadow_lod` casts with it in every shadow pass. Without the PBR
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

### Model instances

A `model_instances` component ([WorldData.md](WorldData.md)) places many copies of one world model, each with its own
position (double, relative to the object), rotation, scale and tint; it shares the model component's settings (every
copy has them) except `omit_pixelation` (copies are never crisp). The renderer draws all the copies of a **cell** at
once, each mesh of each LOD in one instanced draw, so tens of thousands of copies cost a few dozen draws.

- **Cells** (`WorldInstancing.h`, built by the render cache): the copies are sorted into squares of
  `WORLD_INSTANCE_CELL_SIZE` (32 m) of the object's own X and Z. A cell's centre is the middle of its square, and the
  middle of its copies' heights; each copy's **record** (`WorldInstanceRecord`, 56 bytes) holds the first three rows
  of its matrix relative to that centre (scale, the Euler XYZ rotation, the position minus the centre: composed in
  double, rounded once), its tint and a stable random value (a hash of the object's id and the copy's index, for
  wind, not built yet). The cell's sphere holds every copy's sphere (the world model's under the copy's matrix). Drawing a cell takes
  one matrix, the object's world matrix times the translation to the cell's centre in render space, so the camera's
  movement never touches the records: 141 km out the copies stand where they stand at the origin, to the pixel.
- **GPU buffers** (`WorldRendererInstances_PrepareFrame`): one vertex buffer per build of an entry's cells, uploaded
  once (`rlLoadVertexBuffer`) and kept while the entry holds that build, hidden or not; a cell is a range of it. A new
  build replaces the buffer; buffers no entry holds are unloaded, all of them at `PrepareWorld`.
- **Per copy, in the vertex stage** (`shaders/common/instancing.glsl`, whose C mirror is `WorldInstancing.c`): a copy is
  measured as an object is, its LOD by the camera's distance to its sphere's centre and its fade by the distance to the
  sphere's nearest point, both times the zoom factor. Its fade is an object's (the last band of the draw distance in effect)
  against 1 - its opacity (the object's times its tint's alpha). **LOD changes are distance bands**, not an object's timed
  cross-fades (100 000 copies cannot keep a state each): around each switch distance s (times `lod_distance_scale`), from
  s(1 - h) to s(1 + h) with h the world model's `lod_hysteresis`, the incoming LOD's share rises linearly
  (`WorldInstancing_GetLodBands`; a band stops at the middle between two switch distances, so bands never overlap), drawn
  with the cross-fade's complementary dither. A standing camera never changes the picture. The fade and the band reach the fragment
  stage as flat varyings: with `WORLD_INSTANCED` defined (the twins' definitions), `common/dither` reads them instead of
  its uniforms, through the same formula.
- **The passes' rules, per copy** (`WorldInstancing_GetDraw`, as for objects): the lit pass draws
  both LODs of a band, each with its share of the dither, and the copy's fade; the G-buffer the LOD with the larger
  share (the incoming one from `WORLD_LOD_SOLID_SHARE` of the band), solid, until mid-fade; the shadow passes that LOD,
  dithered by the copy's own shadow fade (`WorldInstancing_EvaluateShadow`: the larger of its fade and its shadow
  distance's, the component's shadow distance in effect; in a shadow level, none for a copy smaller than the level's
  size cut, and at least the LOD it would show at the level's inner edge, or the component's own shadow LOD:
  `WorldInstancing_GetShadowLodMask` picks the cell's LODs for that); the unlit fallback that LOD, solid. A copy with a
  fade of 1 is drawn by none.
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
  textures alike, and only `GLTextureArray` and the modules like it may call GL themselves); the copies then draw from a buffer without storage. **The picture is the same up to float rounding**: the instanced
  path places a vertex by the record's rows and then the cell's matrix, the other by their product, and normals by
  cofactors against an inverse transpose; without shadows and post effects some views are bit-identical and others
  differ in a few pixels where two triangles meet; with them, shadow tests and AO round differently (most pixels by 1
  of 255); a copy tint other than white also rounds (an integer multiply into the material colour, against a float
  one in the shader).
- **Costs** (GTX 980 Ti, the start of the pebble field, a 1280 x 720 frame to a `glFinish`): 3.85 ms without
  copies, 4.98 ms with the grove's 400 trees, 6.40 ms with the 20 000 pebbles and a lamp casting a cube shadow among
  them; one at a time, 212.6 ms. The instanced lit pass's own GPU time is 0.67 ms there.
- **Not built**: GPU culling (a cell is all or nothing for the frustum), collision for copies (the repeat and
  place-along tools and scattering will add them as a static source), scattering rules, grass and wind (the random
  value is in every record and unused). Copies never skin (a skinned world model's copies draw rigid, in the bind pose) and never draw crisp.

### Pipeline (one `WorldRenderer_RenderToTarget`)

1. Sync the cache for the frame's origin (the camera's position: every entry in render space); upload the terrain
   chunks that are new or changed (`WorldRendererTerrain_PrepareFrame`) and the copies' new builds of cells
   (`WorldRendererInstances_PrepareFrame`); make sure the targets match the window size and pixelation setting
   (`WorldRendererScene_EnsureTargets`).
2. Build the frame context (`WorldRenderFrame`): the origin, the render camera, the zoom factor, the scene's
   rotation-only view and projection matrices (`WorldRenderSpace_BuildView`: the reversed infinite projection the camera's
   passes load in place of `BeginMode3D`'s where the GPU has clip control (see "Render space"), otherwise the one
   `BeginMode3D` builds for the scene target; shared by every pass that reconstructs positions or rays) and the camera's
   frustum from them (without a far plane when reversed), the
   sky
   (`WorldEnvironment_GetSkyLights`: the sun's and the moon's directions, daylight, the moon's phase and lit fraction,
   the two lights' intensities above the air; the weather's frame, `WorldWeatherLight_GetFrame`, see "Weather"), the
   stars' rotation (`StarsToWorld`), the render
   clock (`TimeSeconds`), sizes, the camera's interior (`WorldRendererInterior_PrepareCamera`, see "Interior
   lighting": whether it runs, the camera's openness and the ambient zones at it, eased), the night sky values (`WorldRendererSky_PrepareFrame`: skyglow, the moonlit sky's
   strength and the moon's disc; after the sky's light, `WorldRendererSky_PrepareNightSky`: the limiting magnitude
   and the Milky Way's radiance; first the star catalogue is rebuilt if the
   world's seed or density changed, and the sky textures reloaded if the world names others), the atmosphere's
   (`WorldRendererAtmosphere_PrepareFrame`: the air's coefficients, the height the sky is seen from, whether the
   tables draw the sky; the frame's haze, light pollution's light in the air, whether the volume hazes the frame),
   the sky's light (`WorldRendererLighting_PrepareSkyLight`, see "Sky's light on surfaces": the C mirror's tables
   kept on the air, the light at the camera (under the weather's clouds, with their deck's light: "Weather"), the light
   the air is lit by, the sun's disc, the scene luminance, the light
   the eye is in (the sky's as the camera's interior leaves it, plus the rooms' own), night vision's field and
   calibration, and the shadow light chosen from the light through the air), every model's LOD and
   its cross-fade (`WorldRenderCache_UpdateLods`, see "LODs"), and every
   pass decision:
   first which shadow passes run (`DecideShadows`, with the shadow levels' settings: the player's quality and the
   world's reach) and their light matrices (`WorldRendererShadows_PrepareFrame`: the levels' maps made for the config,
   the levels used, which render this frame, each one's view, projection and box, snapped to its texels, or kept from its
   last render; the cube shadow's lamp and far range) and the sky's
   occupancy volume's map and window (`WorldRendererSkyVisibility_PrepareFrame`), then the frame's visibility (what every
   pass draws, see "Visibility"), then the rest (`DecidePasses`; the crisp overlay runs only for crisp objects in view).
3. Lighting values for the frame (`WorldRendererLighting_PrepareFrame`): the shadow strength and the forward-light
   candidates (every light except the cube-shadow light) with their linear radiance.
   Then the eye's night vision for the frame (`DecideNightVision`: its strength, the field luminance, the rods' grey),
   and each sprite's matrices (`WorldRendererSprites_PrepareFrame`: facing the camera, facing the shadow levels' light
   and the cube shadow's lamp while those run) with the blended and additive sprites sorted back to front.
4. The atmosphere's tables (`WorldRendererAtmosphere_Render`, see "Atmosphere"): the transmittance and multiple
   scattering tables when the air changed, the sky's irradiance table after them, then the sky view (the sun's
   half, and the moon's while the moonlit sky shows), then the aerial-perspective volume (see "Haze") with the
   sky's light by height in its last row (only that row while the haze is off); the tables left bound on units 7
   and 8. Then the shadow levels of the shadow light (the sun or the moon) that render this frame, each into its
   layer of the maps with depth clamping, then the point-light cube shadow, each drawing its visibility list (the
   casters in its box, large enough for its texels, within the reach; within the light's reach). Models (the LOD they
   show or fade to, at least the level's), the model instances' copies (each the LOD with the larger share, at least the
   level's, dithered by its own shadow fade, left out of a level they are too small for), terrain chunks (their back
   faces in the levels, as the models') and cutout sprites cast; sprites turned toward the light; each caster dithered
   by its shadow fade. Then the sky's occupancy volume (see "Interior lighting"): what keeps the sky out in its
   map's box, drawn from above into height slices, and its pyramid.
5. The normal/flag G-buffer (the camera's models, the LOD with the larger share, the copies the same way, terrain
   chunks and cutout sprites, solid below half their fade), when the AO, the contact shadows, the sky's visibility or
   the outlines run; then the ambient pre-pass and its blur (the AO, the contact shadows and the sky's visibility per
   pixel, see "G-buffer, AO and outlines"), when any but the outlines run.
6. The scene pass: clear, atmospheric sky (the sky-view table's glow, with the moonlit sky and light pollution's
   light, and the far ground below the horizon; the weather's cloud deck and a lightning flash's glow above it; the
   ground haze in front of it all; the moon's disc, the Milky Way), the
   stars (none behind the moon), debug grid, the camera's PBR models (each dithered by its fade, both LODs
   of a cross-fade with complementary dithers; the sun and the moon; per object: up to
   `WORLD_MAX_FORWARD_LIGHTS` reach-culled point lights; per material: PBR factors and map flags), the camera's
   instance cells (lit like the models, the lights per cell, both LODs of a copy's band), the camera's terrain
   chunks (the `world_terrain` shader, lit as the models, with up to `WORLD_MAX_FORWARD_LIGHTS` lights per chunk;
   before the sprites, so decals lie on it), the camera's lit sprites
   (the cutout ones, then decals, then blended and additive ones back to front), light gizmos, the ambient zones'
   wireframes while that debug view is chosen, and collider wireframes and contact points while a collision world is
   set. The sky's glow and the models' and lit sprites' reflected light go through the
   eye's night vision; every lit surface is seen through the haze in front of it (and through interior lighting's debug
   view while one is chosen). Then, while one is chosen, the haze's
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
(one `DirectionalLight` function; their light through the air at the fragment's height), the sky's own
light as the ambient (the sky's irradiance from above, the ground's bounce from below, the moonless night's own
light; see "Sky's light on surfaces"), forward point lights, per-material metallic/roughness/occlusion/emissive factors and
optional albedo / normal / packed ORM (in the METALNESS slot) / emissive maps from the model file (a glTF file's
occlusion, roughness and metallic maps packed when it loads: [GameAssets.md](GameAssets.md)) or the model definition's
`material_overrides`, derivative-based normal mapping (OpenGL's convention: green toward the image's top), and the
haze in front of every surface (the aerial
perspective and ground haze, see "Haze": `ApplyAerialPerspective`). The lighting
itself (the frame's uniforms, the BRDF, the sky's lights with the shadow levels' shadow, point lights with the cube shadow,
the AO, the night vision and the haze) lives in
`shaders/common/lighting.glsl`, which the sprite shader includes too: the renderer prepares and feeds every lit shader
the same way (`WorldRendererLighting_PrepareLitShader`, `WorldRendererLighting_UploadFrame`). Without it models draw
unlit with raylib's default shader.

**Terrain materials** ([Terrain.md](Terrain.md)). `world_terrain` blends up to sixteen terrain materials per pixel:
the painted weights (a face material's faces outright), the terrain's rules (slope and height in the world, noise in the
terrain's space), height blending, and the maps sampled from texture arrays (planar, or triplanar on steep ground)
in the terrain's own space, the normal maps reoriented onto the surface without mesh tangents and turned into the world;
lit as `world_pbr`'s dielectric with the map's roughness and AO, through the night vision. The arrays are made once per
terrain from the materials' pixels and sit on units 9 to 11; without them (no GL support, logged once) each material
shows its mean colour. The C mirror (`TerrainBlend.h`) gives the same answer on the CPU. The equals key cycles the views
(shaded, painted weights, rule weights, the dominant material).

**Sprites** (their settings are their world sprite's, each overridable per placement:
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
  sprites stay as they are. So a tree's shadow does not turn with the camera. The matrices (camera, the shadow levels' light,
  cube's lamp) are made once per frame and shared by every pass.
- **Lighting** (`world_sprite`, `#include "common/lighting"`): a rough dielectric (roughness 0.9, metallic 0, occlusion
  1) lit by the sun, the moon, the forward point lights (reach-culled per sprite on the sphere around its pivot), the
  cube-shadow lamp, the ambient and the haze, its reflected light through the eye's night vision. Fixed sprites and decals
  are two-sided cards lit with their normal turned toward the viewer. Billboards and axis billboards face the camera
  and stand in for round things, so their diffuse light wraps: (n.l + w) / (1 + w) with w their `light_wrap` (default 1:
  n.l x 0.5 + 0.5), half lit from the side and dark only from straight behind; the specular keeps n.l.
- **No self-shadowing.** A billboard's shadow-map entry faces the light while the visible quad faces the camera; the two
  cross at the pivot, and half of the visible quad would lie in its own shadow. So the shadow levels and the cube are read on
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
- **Shadows.** Cutout sprites that are not decals and have `casts_shadows` draw into the shadow levels (those they are
  large enough for, within their shadow distance) and the cube shadow through alpha-tested depth shaders
  (`world_sprite_depth`, `world_sprite_cube_depth`), so the shadow has the sprite's shape. An `is_shadow_only` sprite
  casts but is not drawn.
- **G-buffer.** Cutout sprites that are not decals (`world_normal_sprite`: alpha-tested at their cutoff, their quad's
  view normal turned toward the camera) get outlines and AO like models; decals, blended and additive sprites stay out.
- **Crisp.** `omit_pixelation` draws a cutout sprite that is not a decal in the crisp overlay; decals and blended or
  additive sprites stay pixelated (the overlay composites opaque surfaces with depth only).
- **A/B.** `WorldRenderer_SetSpriteLightingEnabled` (K in the test frame) draws every sprite unlit at its full
  colours. Without the sprite shader or the quad no sprite is drawn (logged once; status
  "sprite shading" unavailable).

**Sky.** `world_sky`: the atmosphere (`shaders/common/atmosphere.glsl`) lit by the sun and, at night, by the moon: the
sky-view table's glow by direction (single and multiple scattering, with light pollution's skyglow; see
"Atmosphere"), or, with the tables off (the A/B, the fallback), the direct march of single scattering per pixel; the sun disc
(dimmed and reddened by the transmittance table: orange as it sets; at a radiance of its own,
`SUN_DISC_RADIANCE` 22 x `sun_color` x `sun_intensity`, scaled against the exposure: "Sky's light on surfaces"), the
moon's disc, the Milky Way, drawn as
a full-screen pass before the geometry (it writes no depth). Each pixel's ray comes from the inverse of the frame's view-projection,
so the sky lines up with the 3D camera; in render space that matrix is a rotation and a projection, so the ray is exact
at any distance from the world's origin (see "Render space"). Falls back to a CPU gradient clear. Its textures are the ones the world's
environment names (`MilkyWayTexture`, default `tbp:sky_milky_way`; `MoonTexture`, default `tbp:sky_moon`): each sky texture
slot holds its texture under its own asset user, loads it when the environment names it (at `PrepareWorld` or the
first render after a change) and releases it when the environment names another; a name that fails is logged once
and not retried until the name changes (`WorldRendererSky_SyncTextures`).

**Atmosphere** (`source/WorldRendererAtmosphere.c`, the C mirror `source/WorldAtmosphere.h`,
the GLSL `shaders/common/atmosphere.glsl`). The sky's light is computed once per frame into lookup tables (Hillaire, "A
Scalable and Production Ready Sky and Atmosphere Rendering Technique", EGSR 2020) that the sky pass, the stars and,
through the aerial-perspective volume built from them ("Haze"), every lit surface read.
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
    sky shows (`MoonSkyIntensity` above 0), each lit by its light above the air on the surfaces' scale
    (`ATMOSPHERE_SUN_LIGHT` x `sun_intensity` x `sun_color`); azimuth from the half's light across (the sine of half the angle:
    0.6 degrees a texel next to the light), zenith angle up, quadratic toward the horizon (from 1.7 m the two rows next
    to it are 0.008 degrees from it); 32 steps growing with the square of their index, each step's light integrated
    exactly over its own air, the light through the transmittance table and the planet's shadow (soft over the sun's
    disc), plus `multiple_scattering` of the multiple scattering table's glow; the in-scatter tinted by `sky_tint`.
    **Below the horizon** a ray ends on the *far ground*: the planet's surface at the atmosphere's ground
    (`horizon_ground_height`), seen through the haze in front of it (that path scaled by the frame's haze like every
    surface's, through the ground where the scale takes it past it). The ground is `sky_ground_albedo` lit as the lit
    shaders light a dielectric facing up: the half's light through the air at the far point of the curved planet,
    at the light's zenith cosine there (the transmittance with its disc's share above the horizon), times the cosine to
    it, plus the sky's irradiance at the ground (the irradiance table, with light pollution's in the sun's half) times
    the ambient's tint, plus the moonless night's own light in the sun's half. So a far point the sun has set on is lit
    by the sky alone, and the far ground meets a terrain's edge without a seam.
  - *irradiance*, 65 x 32: the sky's irradiance on a surface facing up per unit of a light above the air, by the
    light's elevation and the height, and light pollution's in its last column ("Sky's light on surfaces").
  The transmittance and multiple scattering tables are built again only when their air changes
  (`WorldAtmosphere_UpdateTableRecord`, with `WorldAtmosphere_AreTablesStale`: not for `sun_glow` or
  `multiple_scattering`, which only the sky view and the irradiance table use; the irradiance table follows those too:
  `WorldAtmosphere_UpdateIrradianceRecord`). A rebuild costs 0.47 ms on a GTX 980 Ti, so whatever moves the
  air every frame should step it (the weather does not move the air: its clouds dim the lights instead, see
  "Weather"). A light far brighter than the sun saturates the sky view at its half floats' largest value (65504) instead of turning infinite there; a haze too thick to see through gives no light, never
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
  direct march (with a darkened mirror of the sky below the horizon and the skyglow profile of "Night sky"), which is
  also what runs when a table shader or target is missing (logged once); nothing is hazed then (the volume is built from the tables). The status's
  "atmosphere tables" feature says which; `WorldRendererAtmosphere_LogStatus` logs the sizes, the build count, the height
  the sky is seen from and the render's CPU time. TODO in the code: remove the switch and the march once approved.
- **Accuracy and cost**: the sky-view table, sampled as the shaders sample it, is within 0.2 to 0.4 % on average of single scattering with 1024 steps and an exact light path at
  the default haze with the sun above 4 degrees (multiple scattering off; 0.41 % over every sun height from -4 to 85
  degrees against a soft-shadowed reference, 7.6 % at worst at the moment of sunset, where a darkened mirror below the
  horizon would give 19 %), the transmittance table within 0.5 % of an exact optical depth; the sky pass costs 0.024 ms
  by day against the direct march's 0.215, the tables 0.06 ms (0.11 at night with the moon's half).

**Haze** (the pure maths in `source/WorldAtmosphere.{h,c}`, the readers' GLSL
`shaders/common/haze.glsl`, the volume's builder `shaders/atmosphere/volume.frag`). A surface's light reaches the camera
dimmed by the air in front of it (the transmittance) plus the light that air scatters toward the camera (the
in-scatter), from the same atmosphere as the sky along the real path: at the horizon that is exactly the sky, so far
things still dissolve into it; nearer, a hill ends as a flat, pale silhouette, never see-through; dark things turn
blue, bright ones warm.
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
  sky's multiple scattering and light pollution's; also the sky's light by height every lit surface reads:
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
  read from far away). Every lit draw sets `hazeMultiplier` and its ambient zones (`WorldRendererLighting_SetDrawPlace`),
  as the uniforms keep the last draw's values. The part of the line of sight inside the draw's ambient zones is hazed by
  their own haze (`InteriorHazeDistance`, see "Interior lighting": a room's still air has none).
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
  the Milky Way and the stars (`hazeGroundTransmittance`, per star) are dimmed by it. Off by default (density 0). The
  weather's ground fog joins it in the same layer (`fog_density` added to the density at the base, the thickness the
  mean of `ground_haze_thickness` and `fog_height` weighed by the two densities: either alone is exactly itself;
  `WorldAtmosphere_GetHaze`); the rain's own mist is not built yet.
- **The outlines** fade by the haze's transmittance per colour at the object's own multiplier: an outlined surface's
  G-buffer alpha holds its haze (`OUTLINE_HAZE_BASE` 0.76 at 0 to 1 at 1; above 1 counts as 1), and `world_postfx` reads
  the volume's transmittance and the ground haze's at the pixel ("G-buffer, AO and outlines").
- **Light pollution** is the town's light scattered by the air ("Night sky").
- **The debug view** (`WorldRenderer_SetHazeView`; keypad 4 and 5 in the test frame): one slice's in-scatter or
  transmittance stretched over the scene in its place, as the readers sample it (`WorldRendererAtmosphere_DrawView`).
- **Status**: the "haze" feature (active while the volume is built; inactive while the tables are off or the haze's
  scale is 0; unavailable without the tables or the volume's shader or target), and a status log line with the volume,
  the scale, the ground haze and the debug view.
- **Units**: the in-scatter is on the surfaces' scale, so a dark surface hazes at the
  real rate: from 300 m up the terrain 400 to 600 m away changes 0.5 % in brightness (the air takes about 1.8 % of the
  grass's light over 500 m and gives back the sky's, nearly as bright; with the sky at 22 times the surfaces' sun it
  would change 28.7 %), black things still vanish at the real visibility (about 100 km at turbidity 3), and a morning mist toward
  a low sun glows instead of blowing out to white.
- **Cost** (GTX 980 Ti): the volume and its use 0.085 ms by day and 0.13 ms at night (the moon's
  march); a morning mist adds about 0.18 ms in a view full of terrain.

**Sky's light on surfaces** (the C mirror `source/WorldSkyLight.{h,c}`, tested in
`tests/WorldSkyLightTests.c`; the layouts in `source/WorldAtmosphere.h`; the lit shaders' part in
`shaders/common/lighting.glsl`). The sky, the haze, the far ground and every surface are lit by the same sun and moon,
on the surfaces' scale, the one lamps, emissives, the bloom threshold, night vision's candelas and the night sky's
radiances are calibrated on.
- **One scale.** The sun above the air lights a surface facing it with `ATMOSPHERE_SUN_LIGHT` (1.078; C twin
  `WORLD_ATMOSPHERE_SUN_LIGHT`, pinned by the shader source tests) x `sun_intensity` x `sun_color` (linear; white by
  default: the air reddens it by itself): the sun 60 degrees up lights the ground in the default air at a luminance of
  0.911. The air is lit by the same light. The moon above the air is
  `moon_intensity` x its phase's brightness x (1 - the daylight factor) x the sun's light, in `moon_color`, for the
  surfaces and the air alike.
- **Through the air, at each surface's own height.** The sun's and the moon's light on a surface is their light above
  the air times `atmosphereLightTransmittance` toward them at the surface's height (the transmittance with the share of
  the light's disc above the planet's horizon there): near white at noon, golden then red as the sun sets, gone once the
  disc has set there. So from 5 km at dusk the ground lies in the planet's shadow while the camera still sees the sun, and
  a hill keeps the sun longer than the valley.
- **The ambient** is the sky's own light: its irradiance on a surface facing up (the sky's model: single scattering
  through the transmittance with the phase functions, plus `multiple_scattering` of the multiple scattering table's
  glow, lit by the sun, the moon and light pollution's light in the air, times `sky_tint`) and the ground's bounce on one
  facing down (`sky_ground_albedo` x the sun's and the moon's light on the ground and the sky's irradiance). A surface
  with normal n takes (sky (1 + n.y) / 2 + ground (1 - n.y) / 2) / pi x `ambient_skylight_color` x
  `ambient_skylight_intensity` + the moonless night's own light (`night_light_color` x `night_light_intensity`), times its
  albedo and AO (`AmbientLight` in `common/lighting`; under roofs and in rooms all three take the surface's share of the
  sky and the ambient zones add their own light: see "Interior lighting"). At a clear noon shade is about a tenth
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
  inverse weighted by Cornette and Shanks's (6 rings x 6 azimuths: in the default 20 km haze 4 x 4 would miss the
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
  step runs while the time so far and the last step's fit in 1 ms, the first always. So whatever moves the air (a
  story's script) can move it every frame. The GPU's light at the camera matches it within 0.4 % (2.5 % for the sun at
  the moment of sunset: the
  GPU's transmittance table against the exact transmittance). The moon's light counts while the moonlit sky shows
  (`WorldSkyLight_GetMoonGate`: the sun below about 3 degrees, the moon above about -6), for the surfaces and the air
  alike.
- **The scene luminance** (`WorldSkyLight_GetSceneLuminance`): the ambient's luminance on a white surface facing up
  plus 0.40 (`WORLD_SKY_LIGHT_DIRECTIONAL_ADAPT_WEIGHT`) of the sun's and the moon's light, 0.371 at a clear noon in
  the default haze. The eye adapts to the picture ("Eye adaptation"): this, as interior lighting leaves it at the
  camera (the light the eye is in), sets its anchor, and its split into the daylight's share and the night lights'
  (`WorldSkyLight_GetDayLuminance`) is how the eye and night vision read the game's light on the real scale
  (`NightVision_GetFieldLuminance`, "Night vision").
- **The sun's disc** keeps its look on screen whatever the exposure: its radiance is `SUN_DISC_RADIANCE` (22,
  `world_sky`; C twin `WORLD_ATMOSPHERE_SUN_DISC_RADIANCE`) x `sun_color` x `sun_intensity` x 1.08 / the exposure (the
  eye's, before the frame's own is set; `SUN_DISC_SCREEN_EXPOSURE`), so the setting disc stays orange while the
  exposure rises through dusk (at most 65504 in all). It does not bloom (the bloom masks the sky) unless the scene's depth cannot be sampled.
- **The sun shafts'** strength is multiplied by 8 / the exposure (`SUNSHAFT_SKY_SCREEN_SCALE`): their light on screen is
  8 times the sky's pixels they gather toward the sun, mostly the disc, so it falls as the exposure rises with the disc;
  tuned on pictures with the sun 30, 15 and 5 degrees up.
- **Status**: a log line with the light at the camera (the sun's and the moon's light, the sky's irradiance, the
  ground's bounce, the night's light, the scene luminance, the eye's level and whether it was measured, the exposure,
  the C tables' builds) and the same in `WorldRendererStatus`.
- **Cost** (GTX 980 Ti): the values row 0.004 ms with the haze on, 0.0125 ms alone; the lit terrain
  about 0.06 ms more (5 to 8 %); the irradiance table 0.235 ms when the air changes; the C mirror 2.3 microseconds a
  frame.

**Weather** (the pure maths `source/WorldWeatherLight.{h,c}`, tested in `tests/WorldWeatherLightTests.c`; the values
are the environment's "Weather" rows ([WorldData.md](WorldData.md)), which the weather system moves
([WeatherSystem.md](WeatherSystem.md))). The weather's look is the sky's light under clouds on the one light scale: no
factor of its own dims the sun or the shadows. Until clouds are drawn, the clouds are one even deck over the whole sky.
- **The deck** (`WorldWeatherLight_GetFrame`, from `BuildSky`): a cloud's optical depth is 64 x `cloud_density`
  (`WORLD_WEATHER_CLOUD_DEPTH_PER_DENSITY`: density 1 is a storm cloud's), so the share of the sky the clouds hide, the
  cloud cover, is `cloud_coverage` x (1 - e^-depth), and the rest of the sun's and the moon's light comes through
  (`DirectShare`, 1 - the cover). The covered share passes down, diffused, the light falling on the clouds' top times
  their transmission (two-stream: 1 / (1 + 0.75 (1 - g) depth), their droplets' g 0.85: a thin deck passes most of
  it, a storm's about an eighth). `cloud_type` does not change the look (it decides where rain and lightning are
  possible).
- **The light under it** (`WorldRendererLighting_PrepareSkyLight`): under clouds the C mirror's frame light is taken
  for the clear sky at the clouds' height (what falls on the deck's top: a whiter sun than the ground's, less sky above)
  and for the camera with the lights above the air times `DirectShare`, so the sun's and the moon's light on surfaces,
  the sky's light from the air they light and the ground's bounce dim alike; `WorldWeatherLight_AddDeck` then adds the
  deck's irradiance on a level surface (the deck top's sun and moon on a level surface and its sky's irradiance, times
  the cover and the transmission) to the sky's irradiance, and the ground's albedo times it to the ground's bounce. The sun's part of it (from the sun's light and the sky's light from the sun) joins the daylight's
  share (`SunSky`), so the eye and night vision read an overcast day as day. As the clouds thicken the light shifts
  from the sun to the deck: shadows pale (the sun brings less of the light) and are gone under a full storm deck, and
  the scene darkens. The scene luminance at noon in the test world: the clear sky 0.371; thin clouds (coverage 0.1,
  density 0.3) 0.344; cloudy (0.55, 0.5) 0.205; overcast (0.9, 0.7) 0.086; rain (1, 0.9) 0.045; a storm (0.95, 1)
  0.058. The start view's exposure, as the eye meters it, rises from 1.18 to 1.23, 1.73, 3.18, 5.54 and 4.75: the eye
  opens 2.2 of the 3.0 stops the light falls, so a rainy noon reads dull, not dark.
- **The air and the far ground**: the sky-view table and the volume are built with the dimmed lights, so the clear
  sky's glow and the haze dim with the sun. In the sun's half the deck also lights the air below it from all around:
  `atmosphereScatteredLight` takes an even `ambientRadiance` below a height (`cloudHeight`, the deck's: each point
  scatters (sigma_R + sigma_M) x it toward the eye, whatever the phase; a march step reaching past the deck takes its
  share below it, so the sky stays smooth across directions), the deck's mean radiance over its half of the sky (two
  thirds of its zenith's) and the ground's bounce of it below (`cloudAmbientRadiance`; the ground haze's ambient takes
  it too). The air above the deck takes none of it (lit too, it brightened the overcast sky 25 degrees up by a
  twentieth; near the horizon a ray stays below the deck for all the way the eye sees). The far ground and the
  volume's values row (the light on surfaces by height) take the deck's irradiance (`cloudSkyIrradiance`) on top of the
  sky's. So far hills fade into a grey haze that meets the deck at the horizon.
- **The sky** (`world_sky`): above the horizon it adds the deck on top of the clear sky the clouds leave, an overcast
  sky's radiance (CIE: the zenith's 9 / (7 pi) of the deck's irradiance, `cloudZenithRadiance`, falling as (1 + 2 cos
  zenith) / 3 to a third at the horizon), seen through the air below the clouds' height (`WORLD_WEATHER_CLOUD_HEIGHT`,
  1500 m: the transmittance to the top from the camera over the one from where the ray meets that height, in the form
  that does not cancel; the fallback's own transmittance while the tables are off). The sun's and the moon's discs, the
  Milky Way and the stars are dimmed by `DirectShare`: an overcast night hides the stars.
- **Lightning** (`lightning_flash`, `lightning_direction`, `lightning_distance`, runtime rows): a strike lights the
  clouds over a wide area, so its light on a level surface falls off as 1 / distance: 0.1 at a kilometre
  (`WORLD_WEATHER_FLASH_IRRADIANCE`), at most 0.5, the nearest counted 200 m, blue-white, times the flash (1 at the
  strike, then decaying). It joins the sky's irradiance and the ground's bounce on surfaces (the fallback uniforms and the
  values row), the far ground's and the air's light with the deck's, and the sky glows with it, most in a broad lobe
  (power 6, three times the rest) toward the clouds over the strike (`FlashDirection`: the clouds' height over the
  strike's distance). The C mirror's frame light leaves it out, so the eye's anchor and night vision's field do not
  follow it, and the meter, easing over seconds, barely does: a flash of a third of a second lights the scene.
- **Wetness** (`surfaceWetness`, `common/lighting`'s `ApplyWetness`): wet = `wetness` x clamp(0.5 + 0.5 n.y) by the
  surface's own normal (most facing up, half upright, none facing down); the albedo x (1 - 0.7 x porosity x wet) (water
  in the pores traps the light) and the roughness x (1 - 0.5 x wet), at least 0.045 (a film of water reflects like a
  polished surface: wet slopes glint in the sun, wet ground mirrors lamps). Models take porosity 0.5 x (1 - metallic)
  (`WET_DEFAULT_POROSITY`: a metal only turns glossier), terrain its materials' `porosity` blended as their other values
  (not in the terrain's debug views), cards (decals among them: they lie on the wet surface) and cutout billboards 0.5,
  a billboard as a thing standing upright (half wet, whatever the camera's pitch); blended billboards (smoke, glass) and
  additive sprites (light) never. Dry, the shaders skip it. Every surface is wet alike, under roofs too.
- **Fog** (`fog_density`, `fog_height`): the frame's haze (`WorldAtmosphere_GetHaze`) joins it with the world's ground
  haze in one layer: the densities add up at the layer's base, and its thickness is the mean of the two weighed by their
  densities, so either alone is exactly itself (see "Haze").
- **Wind** (`WindDirection`: the level unit direction it blows toward, (sin, 0, cos) of `wind_direction`;
  `WindSpeed`; `WindGust`) is in the frame for what sways in it; nothing reads it yet.
- **Without weather** (no clouds, no flash, dry: the environment's defaults) the frame light is the clear sky's bit for
  bit and every weather term is 0 or skipped, so the picture is the clear sky's.
- **Status**: `WorldRendererStatus.CloudCover`, `DirectShare`, `CloudTransmission`, `DeckIrradiance`,
  `FlashIrradiance`, `Wetness`, and a log line (a storm: `WorldRenderer: weather: the clouds hide 0.95 of the sky (the
  sun and the moon 0.05 of their light; they pass 0.122 of what falls on them), their deck's light 0.1085 0.1031 0.1021
  on a level surface, a flash's 0 0 0, wetness 1.`).
- **Cost** (GTX 980 Ti, the far noon view): a storm's frame within the frame times' noise of a clear one's (5.37 to
  5.58 ms against 5.37 to 5.42); the C side adds one more frame light (2.3 microseconds) under clouds.

**Interior lighting** (the pure maths `source/WorldInterior.{h,c}`, tested in `tests/WorldInteriorTests.c`; the
renderer's parts `source/WorldRendererSkyVisibility.c` and `source/WorldRendererInterior.c`; the lit shaders' part
`shaders/common/interior.glsl`, which `common/lighting` includes). Roofs keep the sky's light out, rooms light
themselves, and the eye adapts indoors. **Off by default**: the renderer starts with it switched off (every surface
takes the whole sky's light, no zone applies, no volume is drawn), and `WorldRenderer_SetInteriorLightingEnabled`
switches it on (keypad 9 in the test frame).
- **The sky's occupancy volume**: around the camera, on a map 1024 x 1024 texels over 96 m (9.4 cm texels), its centre
  snapped to whole texels in double so it stands still in the world while the camera moves; each texel a column of 128
  height slices of 0.25 m, one bit each (an RGBA32UI texture), in a window from 12 m below the camera to 20 m above it
  (snapped to whole slices). After the shadows it draws what keeps the sky out in the map's box (its visibility list:
  models and instance cells whose `occludes_sky` is on, a world model's render setting that placements override
  ([WorldModels.md](WorldModels.md)), and every terrain chunk, whether they cast shadows or not, each dithered by its
  fade) from above, both faces, with no depth test (`world_sky_slices`, its skinned and instanced twins) and a logic
  operation in place of blending (`GLSliceMap.h`'s direct GL calls): first XOR, each fragment toggling the slices whose
  centre lies below it, so a slice is solid where an odd number of surfaces lie above it (inside a wall under a roof,
  not in the air under it); then OR, each model's fragment setting the slice it passes through (a slab thinner than a
  slice is not lost; a fragment above the window sets the top slice: a column solid at the window's top goes on up).
  Two things that overlap would cancel each other's parity, so the models go to 4 layers by their boxes (each to the
  layer where the fewest models placed before it overlap it), each layer's parity drawn into a scratch volume over the
  layer's footprint on the map and ORed into the volume; the terrain's sheet goes straight in, the instance cells take a
  layer of their own. An OR pyramid follows in the texture's 8 levels (`world_sky_reduce`: each level the union of
  2 x 2 texels of the one below). The ambient pre-pass and the lit programs read it (the lit programs on unit 7).
- **A surface's share of the sky.** A surface marches through the volume in directions around it, from 0.14 m out
  (past its own texel) to 24 m, through every texel the direction crosses, one after another (no wall falls between two
  reads), at the level whose texels are 0.35 of the distance wide (finer near, coarser far). Each texel stands for the
  distances the direction spends in it and tests 5 bands of elevation, 15 degrees each from the horizon, against the
  slices their rays pass there: what it finds solid hides the elevations from its bottom (seen from the farthest of
  those distances) to its top (seen from the nearest), and a band keeps open what its blockers leave below and above
  them. The zenith's cap above the bands is open while nothing lies straight above the surface (read in its own column;
  an upright surface's a texel and a half in front of it, not in its own wall). The bands start above the surface,
  0.05 m for an upright one to 0.3 m for one facing up, placed by its geometric normal (never a normal map's, which
  would jitter it), so its own slices are no roof, nor are curbs and bumps; a surface facing up more than half goes on
  around itself as its plane (a slope's own ground is no roof; where that plane rises into the window's top slice
  nothing is known there); what is solid in the surface's own slice alone, solid below it too, is the top of what it
  stands over (the ground at a wall's foot), no wall. The visibility is the bands' and the cap's open share, each
  weighed by its solid angle and the surface's cosine to it: a pebble hides a sliver of the sky, a wall half of it at
  its foot, a roof all of it; a doorway lets in the sky below its lintel, a carport the sky below its roof's edge, a
  cave's mouth the sky before it; a surface facing down is covered by what lies above it, itself too. The ambient
  pre-pass marches 2 directions a pixel, turned by its 4 x 4 pattern, and blurs them (32 directions over every block);
  models and the terrain take its value at their pixel, and so does a card sprite it saw (a cutout card, a decal).
  Billboards (which stand for a thing seen from all round: the pre-pass saw their quad facing the camera), blended
  sprites, the crisp overlay and a frame without the pre-pass read only what lies straight above them, covered or open
  (`InteriorSkyOverhead`). The sky's ambient, the ground's bounce and the night's own light take 1 - 0.85 (1 -
  visibility) of themselves (`INTERIOR_SKY_FLOOR`, 0.15: the light doors and windows let in beyond what the march
  sees). The sun and the moon are the shadows' to keep out. The debug view shows the share on every lit surface.
- **What the volume cannot tell.** Its coarser levels hold anything in a texel: what stands a few metres off counts as
  if it filled its texel across the direction, so open ground among scattered things loses a little of its sky (1 to 2
  levels of the picture across the test world's floor at noon, more beside what stands near), a crate's side facing
  open ground darkens by things off to its side, and under a carport on thin posts less sky shows below the roof's edge
  than should. The slices are 0.25 m: what lies within a slice of a surface's lift counts or not by the slice. The
  window moves with the camera: from more than about 10 m above the ground (the flycam) the ground lies below it and
  its darkening fades out; a closed thing wholly above the window is a roof only straight below it. The terrain sets no
  slice of its own (its parity holds it), so a terrain lip thinner than a slice is lost. Toward the map's edge, from 33
  to 45 m from the camera (horizontally), and toward the window's top and bottom (over 2 m) the visibility fades to
  open, so no hard edge sweeps over shaded ground as the volume moves with the camera; outside it everything is open.
- **Ambient zones** (`ambient_zone` components, [WorldData.md](WorldData.md)): boxes or spheres that set a room's own
  light. The render cache lists, in render space, those of shown objects that have an inside. A lit draw takes at most
  4 (`MAX_AMBIENT_ZONES`), those whose inside reaches its sphere, the highest priorities first, then the nearest; per
  fragment they are laid over each other by priority, each group of equal priority blended by its members' weights and
  covering what lies under it by its strongest member's. A zone's weight rises from 0 at its surface to 1 at its blend
  distance inward (0.1 m by default), and a surface on or inside it that faces into it takes it whole; more than 1 cm
  beyond its surface nothing does. A room's zone that reaches its blend distance into the walls, floor and ceiling (the
  walls' middle) lights everything in the room whole and none of the walls' outside faces; one that ends at the walls'
  inner faces still lights the walls (they face into it), but what stands within its blend distance of them takes only
  the fade (a desk against a wall). Inside, a surface takes the zone's own light (its colour, made linear, times its
  intensity, per unit of albedo, times the AO) and the sky's ambient (after the visibility) times its sky scale.
- **The default light**: 0.02 (`WORLD_AMBIENT_ZONE_DEFAULT_INTENSITY`), about the 500 lux of a lit office: in the
  default warm colour a white surface shows 0.013, where a clear noon's sky gives one facing up 0.07 and the sun 0.25
  more, a lamp 0.06 at 2 m and a moonless night 0.005, so a lit room glows from the street at night and the street
  looks dark from inside.
- **The haze inside**: the part of the line of sight inside a draw's zones is hazed by their haze (0, a room's still
  air: nothing in front of a wall across the room). A zone counts for a fragment only when its draw takes it; zones
  that overlap on the line of sight each take their stretch off, whatever their priorities; a haze above 1 adds the
  outside air's light (the volume's in-scatter), not the room's; and the outlines fade by the whole distance's haze
  (the outline pass knows no zones), which shows only in thick air (a mist, a long `aerial_distance_scale`).
- **The eye indoors.** The game measures how open the camera's surroundings are to the sky
  (`WorldCollision_GetSkyOpenness`: 13 rays spread like the sky's light on a surface facing up; the test frame casts
  them 30 m against the world and prop layers in `PrepareRender`) and hands it to `WorldRenderer_SetCameraOpenness`
  (without it the camera counts as open). The renderer eases it and the zones at the camera (4 a second; the first frame
  of a world sets them) into the light the eye is in (`WorldInterior_GetCameraLuminance`): the sky's light at the
  camera, its ambient taken as a surface's by its visibility (the openness) and the zones' sky scale, the sun's and the
  moon's by the openness, plus the zones' own light, which counts as daylight on the real scale (lamp light). The eye's
  anchor and real scale and night vision's field follow it, and the zones' exposure bias at the camera multiplies the
  exposure (`EyeAdaptation_GetBiasedExposure`). Inside the test room at noon the eye's light is 0.024 against the sky's
  0.371, and looking out of its doorway the metered exposure is 7 (1.2 in the open start view): the room is seen well
  lit and the doorway blows out, as for an eye or a camera. At night the room's 0.014 is above the street's 0.006.
- **On, off and debug**: `WorldRenderer_SetInteriorLightingEnabled` (keypad 9 in the test frame; off at the start):
  off, every surface takes the whole sky's light, no zone applies, no volume is drawn and the eye takes the sky's light
  at the camera, the picture of the open sky's lighting exactly. `WorldRenderer_SetInteriorView` (keypad 0) shows the zones' wireframes (in
  their light's hue), or the sky's share on every lit surface (white the whole sky, the floor's share dark grey) with the
  zones' light over it.
- **Far from the origin** the map's snapping in double and the march in render space keep it steady: the test room
  and the cube 141 km out shade as at the origin, within a few levels where the map's texels meet the world at another
  phase.
- **Status**: a log line (whether it runs, the volume and how many things keep the sky out in it, the zones, the
  camera's openness and zone weight, the exposure bias, the eye's light against the sky's) and in
  `WorldRendererStatus`: `IsInteriorLightingEnabled`, `AmbientZoneCount`, `SkyOccluderCount`, `CameraOpenness`,
  `CameraZoneWeight`, `CameraSceneLuminance`, `ExposureBias` and `InteriorView`; the features "sky visibility" and
  "ambient zones".
- **Cost** (GTX 980 Ti, 1280 x 720, the test world): the volume 0.58 to 0.78 ms (in the room: the terrain's parity
  0.13, the models' layers 0.14, their slices 0.20, the pyramid 0.15); the march in the ambient pre-pass 0.6 to 1.15 ms,
  growing with the share of the screen near things (0.6 at the start view, 0.76 in the room, 1.15 from above and
  looking down at the cube); the lit passes march nothing (they read the pre-pass), the zones cost them up to 0.02 ms.

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
  with reversed depth: the cleared sky's, so whatever is drawn hides it; without, just inside the far plane) and
  widens it to exactly one scene pixel, so a star lights one pixel at a steady brightness and
  never flickers. Drawn right after the sky: additive (ONE, ONE) with alpha 0 (the scene's alpha reaches the frame
  target), depth-tested, writing no depth (the bloom prefilter and the sun shafts read the cleared depth as sky),
  culling off.
- **What shows** (by the sky's brightness behind the stars). The limiting magnitude
  (`NightSky_GetLimitingMagnitude`, one value per frame) is the naked eye's threshold against the sky's luminance
  overhead: Schaefer's ("Astronomy and the limits of vision", 1993), the rods' and the cones' thresholds alike, the
  lower counting (`NightSky_GetThresholdMagnitude`: about 6.5 against a dark site's 2.2e-4 cd/m^2, 3.2 against 1
  cd/m^2, none by day). The background is the night's plus the twilight's:
  - the night's: the background that gives the tuned night limit (`NightSky_GetNightLimitingMagnitude`,
    `NightSky_GetThresholdBackground`): 6.5 at a dark site, minus 3.5 x `LightPollution`, minus the moonlit sky's share
    (a high full moon at the default moonlight takes 1.75, so the faint stars and most of the Milky Way go; it grows with
    the logarithm of the moon's light, so a quarter moon still takes about 0.6, and it shrinks with light pollution,
    which already brightened the sky);
  - the twilight's: the sky's light from the sun at the camera (the C mirror's, `WorldSkyLight_GetTwilightLuminance`),
    as the sky's mean luminance on the real scale (`WORLD_ATMOSPHERE_SUN_LIGHT_CANDELAS`: the game's sun above the air
    stands for the real sun's 128 klux; night vision's scale is the eye's adaptation level, not the sky's radiance).
  In the default air: no star with the sun 4 degrees up or higher, the brightest at sunset (0.2), 1.7 at -3 degrees,
  3.5 at -6, 4.1 at -9, 4.7 at -12, the night's by -14 (5.28 with the default light pollution; 6.5 at a dark site by
  -15). Stars fade out over the last 0.5 magnitude, so at dusk the brightest appear first. The moon's disc hides the
  stars behind it (the star shader drops them). Near the horizon stars and the Milky Way dim by extinction (Kasten-Young air mass, 0.25
  magnitudes per air mass; by the transmittance table relative to the zenith's, which reddens them too, while
  the atmosphere's tables run); below it nothing is drawn. Stars twinkle by `StarTwinkle` x noise, more near the horizon,
  on `WorldRenderFrame.TimeSeconds` (the renderer's own clock, wrapped hourly).
- **Milky Way.** The texture the world names (`MilkyWayTexture`, default `tbp:sky_milky_way`: sRGB, trilinear with
  mipmaps), sampled once per pixel in galactic coordinates: `NightSky_GetCelestialToGalactic` places the galaxy as on
  Earth (plane 62.9 degrees to the equator, centre at declination -29) in the star frame, which the date turns (above).
  The panorama's longitude grows to the left, as in astronomical all-sky maps. Explicit mip level from the pixel's angular size
  (automatic derivatives would draw a seam line where the longitude wraps). Brightness: `MilkyWayBrightness` x full
  at a limiting magnitude of 6 or more, gone at 4.5 or less x extinction: it comes in
  with the sun between about 11.5 and 14 degrees down in the default world (half strength at night under the default
  light pollution), between 11 and 13 at a dark site. A missing texture is logged once and the
  sky draws no Milky Way.
- **Light pollution.** At night the town's light fills the air: it is what it physically is, light scattered
  by the air, so it grows with the haze. The air below about `WORLD_ATMOSPHERE_LIGHT_POLLUTION_HEIGHT` (500 m) and within
  about `WORLD_ATMOSPHERE_LIGHT_POLLUTION_REACH` (10 km) of the camera holds `LightPollutionColor` x `LightPollution` x
  `WORLD_ATMOSPHERE_LIGHT_POLLUTION_RADIANCE` (0.049), faded out by daylight (`WorldAtmosphere_GetLightPollutionRadiance`),
  scattered like any light (the air's scattering per metre x that, `WorldAtmosphere_GetLightPollutionShare`): in the
  sky-view table's sun half, in the volume (the haze over the town glows) and in the ground haze's light. The radiance is
  calibrated so the glow at the horizon is the direct march's skyglow (0.012 x the colour at `LightPollution` 1: 99 % of
  it in green), a little bluer (Rayleigh scattering), 5.5 % of it overhead (the direct march's: 6 %); doubling
  `sky_turbidity` brightens it about 1.4 times at the horizon. The direct march (the A/B) draws the skyglow as a profile
  (`SKYGLOW_BASE_RADIANCE`). The town's light reaches surfaces as part of the sky's irradiance (the irradiance table's
  last column: "Sky's light on surfaces"), and the moonless night's own light is the environment's `night_light_color` x
  `night_light_intensity`.
- **Brightness knobs** (tuned for the night's exposure, about 24, which the eye keeps for an open field:
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
  (1 - illumination)). The face is the texture the world names (`MoonTexture`, default `tbp:sky_moon`, point-filtered for
  crisp pixel art; a flat grey without it), tinted by `MoonColor`, at `MOON_DISC_BASE_RADIANCE` (the "MOON DISC KNOB") x
  `MoonBrightness`. The disc covers what lies behind it (the sun disc, the Milky Way, the stars) and is dimmed by the
  view ray's transmittance (the transmittance table; the direct march's out parameter while the tables are
  off), with the sky's in-scatter added in front: so
  by day it is pale (a quarter moon is findable in the afternoon), at night bright, and reddened low over the horizon.
  It fades out within 6 to 12 degrees of the sun (lost in the glare; this also keeps the virtual moon from eclipsing
  the sun) and is skipped once below the horizon.
- **The moonlit sky.** The same atmosphere lit by the moon's light above the air (all of it, the moonlight
  the surfaces get): a dark blue sky with a soft halo at full moon, faded out
  while the sun is above about 3 degrees and the moon below about -3 to -6 (outside that it is skipped: the
  sky-view table's moon half is built only then, with the moon as its light). The haze's volume is lit by the moon then too, so
  hazed silhouettes match the sky behind them.
- **Moonlight.** The second directional light of the PBR shader, with the sun's BRDF: `MoonIntensity` (default 0.04 of
  the sun, `WORLD_ENVIRONMENT_DEFAULT_MOON_INTENSITY`: tuned on rendered frames so a full-moon night's ground
  is clearly lighter than a new-moon night and still night) x the phase's
  brightness (`WorldEnvironment_GetMoonBrightness`, the opposition-surge fit of Krisciunas and Schaefer: a half moon
  gives about a tenth of the full moon's light) x (1 - daylight), in `MoonColor`, above the air; it reaches a
  surface through the air at the surface's height like the sun (reddened and dimmed low over the horizon, gone once
  set), its fill is the moonlit sky's own irradiance, and the eye adaptation counts it like the sunlight.

**Night vision.** The eye's dim-light vision: in dim light the rods take over from the cones and colour fades into a
grey with a faint blue cast. A low moon's sky is as orange as a sunset's (moonlight is sunlight, scattered by the same
air; a long exposure shows it), but the eye sees it grey-blue, and a moonlit field loses its greens. The maths is the
private `source/NightVision.{h,c}` (tested in `tests/NightVisionTests.c`; its header explains the model), the GLSL
`shaders/common/night_vision.glsl`.
- **The model.** Hunt's rod sensitivity as Krawczyk, Myszkowski and Seidel use it (2005): the rods' share
  sigma = 0.04 / (0.04 + L) for a luminance L in cd/m^2, half and half at 0.04 (a bright moonlit scene). The game's
  light scale is compressed (its noon, a scene luminance of 0.371 in the default 20 km haze, stands
  for 5000 cd/m^2, its moonless night, 0.00602, for 0.001 cd/m^2), so a game luminance Y is read as
  5000 x (Y / 0.371)^3.74 cd/m^2, which makes sigma = 1 / (1 + (Y / 0.0161)^3.74). A pixel is blended by sigma toward its luminance in the rods' grey: white shifted
  toward the night blue (CIE x, y = 0.25, 0.25, Jensen et al. 2001) by the world's `NightVisionBlueShift` (default
  0.5; the full blue looks like a film's day-for-night filter, Krawczyk's own tint reads violet), at the same luminance,
  so the night's tuned brightness does not change.
- **Which luminance.** The larger of the pixel's own and the field luminance: the light the sky gives the whole scene
  (the sun, the moon, the ambient; lamps do not enter it), as interior lighting leaves it at the camera, plus the
  rooms' own light (the light the eye is in: see "Interior lighting"), which counts as daylight (lamp light, real light):
  a lit room at night is seen in colour. It is read in two parts
  (`NightVision_GetFieldLuminance`, the frame's `NightVisionField`): the daylight (the sun's and its sky's light) is on the
  real scale, linearly (the noon's 0.371 stands for 5000 cd/m^2), while the night's lights (the moon, the town, the
  moonless night's own light) are raised for play and go through the power law; their real luminances add. So the
  twilight keeps its colours until the sun is about 4 degrees down, loses them through the nautical twilight (half at
  about -8 degrees) and is the rods' by -12 degrees, as for the eye; without the split the night's own light alone would
  grey the sunset. By day the field is far above 0.103, where sigma is below 1/1024, and the shaders return the colour
  untouched, bit for bit. At night each pixel is seen by its own light: a lamp's bright pool keeps its
  colour, the dark beyond it greys. Lamps lower the exposure (by their share of the view) but do not raise the
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
- **Not modelled.** The rods' slow dark adaptation (minutes; the vision follows the light at once) and the loss of
  sharpness in the dark.

**Sun and moon shadows** (the pure maths `source/WorldShadowLevels.{h,c}`, tested in `tests/WorldShadowLevelsTests.c`;
the pass `source/WorldRendererShadows.c`; the maps `source/GLShadowArray.{h,c}`, direct GL calls). The shadow light casts
into nested **levels** around the camera (a clipmap):
- **The levels.** Level k is a square box in the light's view, 32 m x 4^k wide (32, 128, 512, 2048 and 8192 m by
  default: the player's `shadow_level_count` and `shadow_level_factor`, [Config.md](Config.md)), each map
  `shadow_map_size` texels a side (2048: texels of 1.6 cm, 6.3 cm, 25 cm, 1 m and 4 m). Each is centred on the camera
  and snapped to its own texel grid (see "Render space"), so its shadow edges stand still while the camera moves. At a
  640 x 360 picture a scene pixel at distance d is about 0.0032 d wide, so at each level's inner edge a texel is about a
  scene pixel: the shadows' quality falls with distance no faster than the picture's. Boxes centred on the camera, not
  slices of its view: turning the camera renders nothing again, and a level's half behind the camera still holds the
  casters that shade the view.
- **The maps.** One texture array of 32-bit float depth, a layer per level the reach uses (`GLShadowArray`: made at the
  first frame that runs the levels, for the config's map size and the levels used, again when either changes; a failure
  is logged once and leaves the sun and the moon without shadows), on texture unit 12 (`sampler2DArrayShadow shadowLevels`), read with the
  hardware's depth comparison and linear filtering: one lookup weighs the four texels around a point bilinearly, a soft
  one-texel edge (no staircase at a coarse level's 1 m or 4 m texels). Every level's depth runs the reach both ways from
  the camera along the light (times 1.05), so whatever the shadows reach lies inside every level; casters nearer the
  light than a box (a tower toward a low sun, a hill behind the camera) are drawn onto its near plane with depth clamping
  instead of being cut off, so a box's near plane culls no caster.
- **The reach.** The world's `shadow_reach` (4 km by default, at most 20 km: the levels' depth precision, 2.5 mm there
  against the first level's 8 mm bias at 2048 texels; the bias never falls below two of the depth's steps, 2^-23 of the
  depth range, which finer maps at long reaches would ask for; 0 turns them off). The levels used are the first ones up to the one whose half
  size holds it; the shadows fade out over its last tenth (at least 3 m) by the distance to the camera, and only casters
  whose sphere reaches within it are drawn into the levels.
- **Which level a point reads** (`ComputeShadow` in `common/lighting`, its C mirror `WorldShadowLevels_GetLookup`): the
  smallest that holds it, by the larger of its light clip x and y in each level's own matrix against the level's edge (a
  margin of 4 texels for the filter, the lookup's move off its surface and the centre's snapping); over the outer 15 % of
  that the shadow blends linearly into the next level's (two lookups only there), and the last level's fades to none, so
  there is no line at a level's edge or at the end of the shadows.
- **What a level draws** (its visibility list, see "Visibility"): the casters in its box, large enough for its texels
  (a bounding sphere at least two of its texels wide: a cup casts into the first level only, a tree up to the 1 m level, a
  house and the terrain into every one; per copy for model instances, in the vertex stage), within their shadow
  distance; models, model instances' copies and terrain chunks with their faces turned away from the light (both faces
  for a two-sided caster), cutout sprites turned toward the light. A level past the first draws a caster at the LOD it
  would show at the level's inner edge (`WorldShadowLevels_GetLodDistance`, with the zoom) unless it shows a coarser one,
  or its own shadow LOD while it has one ([WorldModels.md](WorldModels.md)). Each caster is dithered by its shadow fade
  (the larger of its fade and its shadow distance's). And a level past the first draws only its **ring**: the light's
  view is orthographic, so a caster shades only what lies under it there, and one deep inside the level before's box
  shades only points that read the level before alone. A caster whose sphere lies inside that box's sides by more than
  `WorldShadowLevels_GetInnerInset` stays out, a bound on everything that can happen before the level renders again:
  the band (where both levels are read), the camera's movement while either level keeps its map (2 % of the level's half
  size and twice of the level before's), both levels' snapping, 4 of the level's texels around a lookup (its move off
  the surface, the bilinear tap) and the light's turns while a map is kept (up to three turn limits for the level
  before's light, carried over a caster's depth and, through the box's axes, over its place in the box; two for the
  level's own, over the depth from the caster to what it shades: 10 to 24 m with the defaults). So the 128 m level leaves
  out almost nothing, the 512 m level what lies within about 34 m of the camera, the 2048 m one 168 m and the 8192 m one
  700 m: from the slab the 8192 m level draws none of the test terrain's chunks. The pictures are those of every level
  drawing everything in its box, bit for bit (still, walking, flying past the far field).
- **The draw.** Every depth pass (the levels, the cube shadow, the sky's volume) draws its models' and its chunks'
  meshes through `WorldRendererCommon_DrawDepthMesh`: raylib's `DrawMesh` with the same matrices multiplied in the same
  order (the pictures are the same bit for bit), but without what no depth stage reads (the material's colours and maps,
  the normal matrix) and without unbinding the program and the vertex array after each mesh; the program is bound once
  for all of the terrain's chunks and at each model's first mesh (and where a model's next mesh takes the other of its
  rigid and skinned shaders), and a caster's dither uploaded only when it changes (`WorldRendererCommon_SetRunDither`). `DrawMesh`'s resets cost about 1.6 microseconds a mesh, which made a level of the
  test terrain's 256 chunks cost 0.4 ms; drawn this way they cost about 0.05 ms.
- **The objects' settings** ([WorldModels.md](WorldModels.md), [WorldSprites.md](WorldSprites.md),
  [Terrain.md](Terrain.md)): `casts_shadows`; `shadow_distance`, how far from the camera a thing still casts (its own,
  its asset's, or its draw distance when neither gives one; times its multiplier; times the player's
  `shadow_distance_scale` unless fixed; never beyond its draw distance), measured and faded as a draw distance is (to its
  sphere's nearest point, times the zoom; over its last tenth, at least 3 m); `is_shadow_only`, cast but not drawn (out of
  the camera's passes, in every shadow pass and the sky's volume); `has_two_sided_shadows`; `shadow_lod`. The point
  light's cube shadow takes them too.
- **When a level renders** (`WorldShadowLevels_BeginFrame`): the far levels less often, staggered so that no two of
  them render in one frame (`shadow_update_periods`: 1, 1, 2, 4, 8 by default; a period P renders when the frame count is
  P / 2 - 1 past a multiple of P), and at once on a level's first frame, when the maps or the settings they were drawn for
  change (the size, the factor, the reach), when the light has turned by half a texel at the box's corner since (the
  same angle for every level: about 0.02 degrees at 2048; with the test frame's 2-minute day the sun turns more than
  that every frame, with the game's 20-minute day every fourth) or across the up vector's switch near straight up
  (every box turns about the light there), when the camera has moved by 2 % of its half size (the box would no longer
  hold what it is read for), and after frames without the levels or for another world (the renderer forgets every map
  then, and when the frame's visibility cannot be built after the levels were told to render). A level that does not render keeps its map and the light frame it
  was drawn with (its centre in the world, in double, and the light's direction); its matrices are built again from them
  for each frame's origin (`WorldRenderSpace_GetCascadeAt`), so its lookups read its map as it was drawn. A moving
  thing's shadow in a level of period P lags by up to P - 1 frames of its motion.
- **The shadow light** is the frame's (`WorldEnvironment_ChooseShadowLight`, from the light the sun and the moon give
  the camera's surroundings through the air): the brighter of the sun and the moon by luminance, once it stands above
  0.05 (the sine of its elevation; full shadows from 0.15), faded out while the two are about as bright (0 where they are
  equal, full once one is 4 times the other) and for a light too dim for a shadow to show (a thin crescent). So dusk
  with the moon up goes from sun shadows through none to moon shadows without a jump. The levels darken only that light's
  direct term (`sunShadowFade` / `moonShadowFade`; both 0, and no map read, while the levels do not run).
- **Off the surface.** The maps hold the casters' faces turned away from the light (front faces culled), so a lit
  surface never meets itself there. Models and the terrain read them off the triangle's own face (`FaceNormal`: from the
  position's derivatives, turned to the interpolated normal's side): a face turned away from the light takes the whole
  shadow, the map holding that very face (no sunlit specks inside a cave or on a cliff's shaded side); a face toward it is
  looked up off itself along that normal by a texel and a half at grazing incidence (`sin` of the angle to the light: none
  facing it) with a depth bias of half a texel, both in texels of the level read (0.8 cm and 2.3 cm in the first,
  `SHADOW_DEPTH_BIAS_TEXELS`, `SHADOW_NORMAL_OFFSET_TEXELS` in `WorldRendererShadows.c`), so a shadow reaches the foot of
  what casts it and a roof's shadow the top of the wall under it. A smooth low-poly model's terminator follows its faces.
  A sprite is read on its quad facing the light, off that face.
- **The debug view** (`WorldRenderer_SetShadowLevelViewEnabled`, keypad * in the test frame): every lit surface tinted by
  the level its shadow comes from (red, green, blue, yellow, magenta, cyan, orange, white from the first out), mixed
  across the bands, dark beyond the levels or the reach. The status line (I) lists the levels used with their widths,
  which rendered this frame and their casters, the maps' size and the reach.
- **Cost** (a GTX 980 Ti at 1417 MHz, the windowless harness at 1280 x 720 with the defaults; the GPU's timestamps read
  frames later and the CPU's clock, averaged over 1500 frames). The start view: the levels 0.46 ms of GPU time and
  0.61 ms of CPU a frame (per render the 32 m level 0.05 ms, the 128 m 0.13, the 512 m 0.46 every second frame, the
  2048 m 0.14 every fourth, the 8192 m 0.012 every eighth), the lookup 0.1 ms in the lit passes. Over a forest of 4000
  instanced trees from 30 m up: 0.8 ms of GPU and 1.0 ms of CPU a frame, about 1.0 ms in the frames that render the
  512 m level, which holds most of the forest's cells (their instanced draws' calls, on the CPU). Over the far field from 80 m up the lookups over the whole lit plain cost about 0.5 ms more than
  shadows that end at 80 m would. Shorter periods, more levels or larger maps cost more.
- Active when shadows are enabled, the strength is above 0, the reach is above 0 and there is a shadow light.

**Contact shadows.** The seam the maps' texels and filter still leave at a caster's foot, and the hairline where a roof
meets a wall, the ambient pre-pass closes: from each pixel it marches the depth toward the shadow light over 0.25 m (a
pixel a step, at most 32; every depth sample taken 0.25 m thick, so a thin thing casts no endless shadow), and the lit
shaders take the larger of the shadow levels' shadow and that one for the shadow light's direct term
(`FaceShadowAmount`). Only what the camera sees casts it, so it only adds to the levels. It runs with the levels;
`WorldRenderer_SetContactShadowsEnabled` (keypad . in the test frame) switches it, and the status has it as the feature
"contact shadows".

**Point-light shadow.** One cube shadow for the shadow-casting light nearest the camera (`CastsShadows`): six
90° faces of packed linear distance (RGBA8 cube, `common/distance_packing`), sampled with 20-tap PCF. The faces are
written with colour blending off: the packed value's alpha byte is a digit of the distance, not a coverage (the
default blending would mix every texel with the cleared "far", so casters beyond half the light's reach would leave no
shadow and nearer ones would be stored too far away). Models and cutout sprites (turned toward the light) cast. That light is
shaded separately and is excluded from the forward candidates, so an object can still get eight other point lights.

**G-buffer, AO and outlines.** `world_normal` (models) and `world_normal_sprite` (cutout sprites, alpha-tested) write
view-space normals (RGB) and a surface/outline flag (A: 0 = nothing, 0.5 = surface, 0.76 to 1 = surface with outline,
its haze multiplier over that range)
into a low-res 8-bit target with blending disabled. The ambient pre-pass (`world_ao`, over the G-buffer's depth and
flags) writes per pixel the AO multiplier (R), the sky's visibility (G, see "Interior lighting") and the contact
shadows' light (B, see "Contact shadows"). The AO is horizon-based (ground-truth AO): one slice through the view vector
a pixel, turned by a 4 x 4 pattern on the pixel grid, 6 steps each way over 0.5 m (at most 64 pixels on screen; none
below a pixel), the horizons measured against the surface's tangent plane and clamped at the view direction, the
slice's cosine-weighted visibility in closed form, normalized per pixel so open ground is exactly 1; occluders fade out
over the outer 60 % of the radius (no halos behind silhouettes), the normal comes from the depth along each axis from
the neighbour whose depth steps less (the contact is the darkest place, never a light line), and the darkening is at
most 0.6 (`AO_MAX`). `world_ao_blur` then averages R and G over 5 x 5 pixels whose outer row and column weigh a half
(every cell of the pattern once: the pattern is gone) among the neighbours on the same surface (within 2 cm a metre of
the centre's plane, their normals within about 37 degrees of its), and passes B through. The lit passes apply the AO
to their ambient term only (so AO never dims direct light). A draw the G-buffer left out (past half its fade, per draw
through `WorldRendererLighting_SetDrawPlace`; a copy fading on its own inside a cell still counts as its cell) takes
none of the pre-pass's values, which belong to what lies behind it: no AO, no contact shadow, and the sky read only
straight above itself. `world_postfx` draws outlines after the scene:
depth/surface silhouettes darken, view-normal creases are recoloured sun-aware; the sky is never outlined. A
line fades with the haze as the surface under it does: its strength is scaled, per colour, by the haze's
transmittance to the pixel at the object's own haze multiplier (the volume's and the ground haze's, `hazeTransmittance`;
the world's up in view space, `worldUpView`, gives the ray's climb), so a hill the haze has all but hidden keeps no dark
rim, and a sign with `haze` 0 keeps its lines. It reads the
G-buffer's depth (the scene's only where the G-buffer's is not samplable), which matches its normals and flags: a fading
object is solid in the G-buffer but has the dither's holes in the scene's depth. (So the debug light gizmos,
which write the scene's depth but are not in the G-buffer, are not darkened as if they were the rim of the surface
behind them.) The AO and the outlines are under the post-effects toggle (`WorldRenderer_SetPostEffectsEnabled`, O key in
the test frame); the contact shadows and the sky's visibility are lighting and stay. Cost (GTX 980 Ti, 1280 x 720): the
pre-pass's AO with its blur 0.36 to 0.42 ms, the contact shadows up to 0.23 ms more (none while the shadow levels do
not run), the sky's visibility as "Interior lighting" says.

**Bloom and sun shafts.** Half-resolution HDR passes added by the tonemap: a depth-masked bright pass (the sky
does not bloom) with a separable blur, and a radial blur of the unoccluded sky toward the sun's screen position
that fades as the sun nears the screen margin or the horizon; the shafts' strength is divided by the exposure
(times `SUNSHAFT_SKY_SCREEN_SCALE`), so they follow the sky's light on screen and do not glare at dusk ("Sky's light
on surfaces"). The march toward the sun ends at the screen's edge (beyond it nothing is known; clamped, the samples
would repeat the edge row into streaks), and each sample adds at most `SUNSHAFT_SOURCE_SCREEN_LIMIT` (1.0) on screen (its
luminance times the last exposure), about the sky's glow near the sun: the disc's few pixels, far brighter, would smear
their square shape into a blocky cross. The scene's colour texture clamps at its edges (raylib makes colour textures
repeat, and a read at an edge would take the opposite edge's pixels: the town at the bottom of the screen as white and
aqua streaks at its top).

**Eye adaptation** (the eye's model `source/EyeAdaptation.{h,c}`, tested in
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
- **The anchor.** 0.11 (`EYE_ADAPTATION_ANCHOR_SHARE`) of the scene luminance at the camera (the light the eye is in:
  the sky's in the open, a room's indoors, see "Interior lighting"): the log-average an open view has under the sky's
  light (measured on the test world by day and by night). The eye starts there, and stays
  there where the meter is unavailable. A dark interior opens the eye at most 6 stops, a bright view closes it at most
  2 (closing it further would grey a white platform or a lamp-lit square at night); a black corner, the sun's disc or a bulb count no
  further. A window around the eye's own level would chase a black sky down without end.
- **Easing.** In stops: 0.6 a second, faster by a quarter per stop of the step, five times faster while brightening, at
  most a tenth of a second of a frame counted. The first measurement sets the eye at once.
- **The curve: partial adaptation.** The eye's level is read on the real light scale (night vision's: daylight linear,
  the night's raised lights through its power law, at the camera's mix, `EyeAdaptation_GetRealScale`; at least the
  moonless night's ratio, so a darker night or a world lit by lamps alone is read as a dark night) as R cd/m^2, and the
  screen shows it at `EYE_ADAPTATION_KEY` (0.0873) x (R / 5000)^`EYE_ADAPTATION_POWER` (0.0596): a sunny day looks a
  little brighter than a dull one, the dusk darkens steadily, the night keeps its look. The exposure is that over the
  level, within 1/64 and 512 (guards only). Calibrated on rendered frames so an open field's exposure is 2.2 at noon
  and 24 at night; through dusk it rises to about 40 with the sun 3 degrees down and is
  back at the night's by 12 degrees down, so the twilight stays on screen while its real light fades. The picture as
  seen (its mean tonemapped sRGB) on the open field: noon 0.33, sunset 0.33, 2 degrees down 0.24, 6 down 0.18, 10 down
  0.15, night 0.15.
- **Lamps** count by what they light on screen. Beside the grove's lamp by day the view keeps the day's look, and
  near it at night the grove shows as any night's view.
- **Bright views and moonlit nights**: a view of the white test platform by day gets about 40 % less exposure than an
  open view under the same light (the meter sees a bright view; the picture as seen about a fifth darker); a
  full-moon night is darkened toward the moonless night's look (the eye adapts to the moonlight: the moon shows in its
  shadows, sky and colours more than in the overall brightness).
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
  "moon shadows" are the shadow levels cast by either light (unavailable without their depth shader or the GL functions
  of their maps); "moon" is the disc (inactive below the horizon, near the sun or
  switched off; drawn flat grey without its texture), "moonlight" the moon's light on the world, "night vision" the
  eye's night vision (active while it changes something: dusk and night, with a strength above 0), "sprite shading" the
  sprites at all (unavailable without the sprite shader or quad: then no sprite is drawn), "sprite lighting" the lit
  sprites (inactive while switched off with the K key's toggle), "sprite shadows" the shadows of cutout sprites
  (active while one casts into a running shadow; unavailable without the sprite depth shaders), "skinning" the posed
  models' skinned meshes (available with the skinned twin of the shader models draw with, active while a shown
  model has a pose; the status line counts the posed models), "terrain shading" the lit terrain shader (unavailable
  without `world_terrain`, when the chunks draw unlit; the status counts the terrains placed, those without a terrain,
  the chunks drawn of all listed and their triangles, and whether the chunk key is on), "terrain materials" the terrains'
  texture arrays (active while a drawn terrain has them, unavailable without texture arrays or the terrain shader;
  the status log adds a line per terrain with its materials and its arrays' sizes, and the status names a debug view
  while one shows), "atmosphere tables" the atmosphere's tables (unavailable without their four shaders or targets,
  inactive while switched off, when the sky is marched per pixel; the status adds the build count, the height the sky is
  seen from, the render's CPU time and the frame time the caller gave), "haze" the aerial-perspective volume
  (unavailable without the tables or the volume's shader or target, inactive while the tables are off or the haze's scale
  is 0; the status adds the scale, the ground haze's density and the debug view), "reversed depth" the camera's reversed
  depth (see "Render space": unavailable without clip control, logged once, when the camera keeps raylib's
  projection and its 4 km; a camera pass's 32-bit depth that cannot be made is logged once and the pass's target fails
  as any target does), "sky visibility" the sky's visibility map (unavailable without the depth shader or its
  target, inactive while interior lighting is off), "ambient zones" the zones' light (active while interior lighting
  runs and a zone is listed). It also counts what the camera's passes
  drew and what culling and fading removed (see "Visibility"). The renderer logs the status once after its first
  render; `WorldRenderer_LogStatus` logs it again (the I key in the test frame), with a line for the atmosphere's tables,
  one for the haze and one for interior lighting.

### Renderer structure (keep it clean)

| File | What it holds |
|---|---|
| `WorldRenderer.c` | Public API: create/destroy, `PrepareWorld`, `RenderToTarget` (frame context, pass decisions, the visibility build, pass order), status. Orchestration only. |
| `WorldRendererInternal.h` | The renderer state (one state struct per pass), `WorldRenderFrame`, fixed texture units, shared constants, and every internal function, documented. |
| `WorldRendererCommon.c` | Config multipliers, colour conversion, shader loading, one-time warnings, render-target constructors; the camera passes' depth (their depth buffers, the reversed state around them, the projection they load, the depth convention's uniform). |
| `WorldRendererScene.c` | Scene target size, HDR tiers and recreation on resize (asks each pass to recreate its targets); the scene pass. |
| `WorldRendererGeometry.c` | Model meshes one LOD at a time with per-draw materials (both LODs of a cross-fade in the lit passes; `WorldRendererGeometry_PrepareMaterial` builds each, the instanced draws use it too), each mesh with its pass's rigid shader or, posed and skinned, its skinned twin after the pose's palette; the unlit fallback's skinned shader; the LOD tinting, light gizmos, the pixelation filter. |
| `WorldRendererInstances.c` | The model instances' copies (see "Model instances"): each entry's records buffer, the G-buffer's and the shadows' instanced twins (the lit one is the lighting file's), the instanced draw that mirrors `DrawMesh`, each pass's cells instanced or one at a time (the A/B and every fallback), the status line. |
| `WorldRendererSprites.c` | The sprites: the unit quad, the lit sprite shader, each frame's sprite matrices (camera, the shadow levels' light, cube's lamp) and transparent draw order, the lit cutout / decal / sorted transparent draws. The G-buffer and shadow passes draw sprites with this file's quad and matrices. |
| `WorldRendererLighting.c` | The PBR programs (rigid, skinned, instanced: `WorldRendererPbrProgram`) and their location tables, the lit shaders' frame uniforms (`PrepareLitShader`, `UploadFrame`: every PBR program, the sprite shader and the terrain shader, the night vision's and interior lighting's among them), forward point lights, each lit draw's place (`WorldRendererLighting_SetDrawPlace`: its haze multiplier and its ambient zones), per-material factors. |
| `WorldRendererShadows.c` | The shadow levels of the sun or the moon (their settings from the config and the world, their maps through `GLShadowArray`, each frame's levels from `WorldShadowLevels`, the levels' passes with depth clamping, the maps' bind, the status line), and the point-light cube shadow (models, model instances, terrain chunks, and cutout sprites through alpha-tested depth shaders). |
| `GLShadowArray.c` (not a pass) | The shadow levels' maps: a texture array of 32-bit float depth with the hardware's comparison, its layers attached to a framebuffer one at a time, depth clamping; direct OpenGL calls in `GLTextureArray`'s manner; private (`source/GLShadowArray.h`). |
| `WorldShadowLevels.c` (pure maths, not a pass) | The levels' sizes, the reach's levels, the lookup's C mirror, the size cut, the LOD distances, the update schedule and the kept levels' light frames; see "Sun and moon shadows". |
| `WorldRendererSkyVisibility.c` | The sky's occupancy volume (see "Interior lighting"): its integer texture with its levels and their framebuffers, the scratch volume, its frame (the map's matrices and box, the window), its pass (the models' layers, the parity and the slices from above, both faces, the pyramid) and its bind on unit 7. |
| `WorldRendererInterior.c` | Interior lighting's other parts: the lit shaders' interior uniforms (`WorldRendererInteriorLocations`), the camera's interior for the eye (its openness and the ambient zones at it, eased), each lit draw's zones (through `WorldRendererLighting_SetDrawPlace`), the zones' wireframes (the debug view), the status line. |
| `WorldRendererGBuffer.c` | The normal/flag G-buffer (models, terrain chunks and cutout sprites) and the ambient pre-pass (the AO, the contact shadows, the sky's visibility) with its blur. |
| `WorldRendererTerrain.c` | The terrain's chunks ([Terrain.md](Terrain.md)): their GPU meshes, kept by the renderer per terrain asset and chunk and uploaded again when a chunk's revision changes (all of them when the chunk layout changes; those of terrains no entry resolves any more unloaded); the `world_terrain` shader and its lit draw (per-chunk lights and dither; the unlit fallback with raylib's default shader); the G-buffer's and the shadow passes' draws with their shared shaders; the chunk key (the minus key: a colour per chunk and the wireframe); each terrain's materials as texture arrays (made once per terrain through `GLTextureArray`, bound on units 9 to 11), its materials' and rules' uniforms, each chunk's wrapped offsets, and the terrain views (the equals key). |
| `GLTextureArray.c` (not a pass) | Texture arrays (`GL_TEXTURE_2D_ARRAY`): direct OpenGL calls, through the function pointers raylib's loader resolves (`rlGetProcAddress`), with their GL constants; private (`source/GLTextureArray.h`). |
| `GLReadback.c` (not a pass) | Readbacks that do not wait: a pixel-pack buffer and a fence per copy, three in flight; direct OpenGL calls in `GLTextureArray`'s manner; private (`source/GLReadback.h`). |
| `GLDepth.c` (not a pass) | Reversed depth (see "Render space"): whether the GPU has clip control, 32-bit float depth textures and renderbuffers, the switch to the reversed state and back; direct OpenGL calls in `GLTextureArray`'s manner; private (`source/GLDepth.h`). |
| `GLSliceMap.c` (not a pass) | Slice maps (the sky's occupancy volume): RGBA32UI textures with their levels, their integer clear, the logic operations (XOR, OR) and per-level reads for the pyramid; direct OpenGL calls in `GLTextureArray`'s manner; private (`source/GLSliceMap.h`). |
| `WorldRendererAtmosphere.c` | The atmosphere's tables (see "Atmosphere"): their four builders and targets, the rebuild when the air changes, the sky view every frame (the moon's half while its sky shows; the far ground below the horizon); the haze's volume (see "Haze"), its builder and target, the frame's haze and light pollution, the haze uniforms every reader gets (`WorldRendererHazeLocations`), the volume's unit and binds, the debug view; the atmosphere uniforms every reader gets (`WorldRendererAtmosphereLocations`), the tables' fixed units, the A/B, the status lines. Runs first among the GPU passes. |
| `WorldRendererSky.c` | The atmospheric sky with the moon, the moonlit sky, the Milky Way and the ground haze in front of it (the far ground and light pollution's light are the tables'); the sky textures the world names; the night sky values (the direct march's skyglow among them) and the moon's disc for the frame. |
| `WorldRendererStars.c` | The star field: the catalogue's mesh (rebuilt when the seed or density changes) and its draw (none behind the moon). |
| `WorldRendererPost.c` | Outlines, bloom, sun shafts and the tonemap. |
| `WorldRendererExposure.c` | The eye (see "Eye adaptation"): the meter's shader, target and readback, the meter pass after the sun shafts, the eye's update from the newest blocks (`EyeAdaptation`) and the exposure the tonemap takes. |
| `WorldRendererCrisp.c` | The crisp overlay. |
| `WorldRendererCollision.c` | Debug wireframes of a collision world's shapes and its contact points (`WorldRenderer_SetCollisionDebug`), drawn in the scene pass after the light gizmos from its read-only debug lists ([WorldCollision.md](WorldCollision.md)). |
| `WorldVisibility.c` (with the cache, not a pass) | The frame's visibility lists (per pass, the entries it draws, and every entry's fades) and the dither's C mirror; see "Visibility". |
| `WorldLod.c` (with the cache, not a pass) | LOD selection and the cross-fade's state; the cache's `WorldRenderCache_UpdateLods` runs it per entry; see "LODs". |
| `WorldAtmosphere.c` (pure maths, not a pass) | The atmosphere's model: the air from the environment, the stale check and the tables' build record, the view height, the tables' layout (the C mirror of `common/atmosphere`); see "Atmosphere". |
| `WorldInterior.c` (pure maths, not a pass) | Interior lighting's model: zones in render space, their weights, the draws' choice and the composition, the haze inside, the sky's occupancy volume (its map, slices and window), the march through it and the read straight above, the camera's interior and the light the eye is in (the C mirror of `common/interior`); see "Interior lighting". |
| `WorldRenderSpace.c` (pure maths, not a pass) | The frame's origin and its rotation-only view and projection (`BuildFrame` calls it; also the reversed infinite projection and its inverse), the sky shader's ray in C, the shadow levels' boxes snapped to their texels and their grids from frame to frame, and a kept box for a new origin (`WorldShadowLevels_BeginFrame` calls it); see "Render space". |

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
  | 7–8 | the sky pass and the stars; the lit programs (models, sprites, terrain) | In the sky pass and the stars 7 the transmittance table, 8 the sky-view table (`sampler2D`, `WorldRendererTextureUnit_Atmosphere*`). In the lit programs 7 the sky's occupancy volume (`usampler2D skyVolume`, an integer texture whose levels are its pyramid, `WorldRendererTextureUnit_SkyVisibility`; their transmittance sampler there is declared and unused, so it never meets the volume's: they read no table) and 8 the haze's volume (`sampler2D atmosphereVolume`, `WorldRendererTextureUnit_HazeVolume`, also for the sky's light by height in its last row, while the haze is off too). raylib binds only cube maps there (`MATERIAL_MAP_CUBEMAP`, `_IRRADIANCE`, filled by no material of ours) and the 2D batch only 0–4, so what a pass binds stays bound; the lit passes bind the occupancy volume on 7 and the haze's volume on 8 with their frame textures, the sky pass and the stars the tables before their draws (the sky pass and the outlines take the volume through `SetShaderValueTexture`). Units chosen because they are free in every lit program, the terrain's included, with no capability check (units 16 and up would have needed GL_MAX_TEXTURE_IMAGE_UNITS above 16). |
  | 9–11 | the terrain program only | Its material arrays (`sampler2DArray`: albedo and height, normal, roughness and AO). The terrain draws with no raylib maps, and an array sampler reads only a unit's `GL_TEXTURE_2D_ARRAY` target, which nothing else binds, so the models' maps on the same units never meet them. |
  | 12, 14–15 | every lit program | The frame's textures: 12 the shadow levels' maps (`sampler2DArrayShadow shadowLevels`, the array's `GL_TEXTURE_2D_ARRAY` target), 14 the cube shadow, 15 the ambient pre-pass's map (the AO, the sky's visibility, the contact shadows). |
  | 13 | nobody | Free in every lit program. |

  The wetness and puddle masks (rain and wetness) will need units in the lit programs too: 13 is free, and 7, 8, 12,
  14 and 15 are taken; beyond 13, a frame texture for the model programs takes a unit below 12 that no material map the models
  use binds (the PBR materials use maps 0, 1, 2 and 5: albedo, the packed ORM, normal, emission; raylib's importers
  also fill 3 (roughness) and 4 (occlusion) in some formats (a glTF file's are packed into 1 and emptied when the model
  loads) and its OBJ import 6 (height) when the file has those maps, so 10 and 11 are the safe ones there), and the
  terrain program keeps 9–11 for its arrays (units 0–6 are free in it). Beyond that,
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
- **Every geometry pass skins.** A pass that draws models has a skinned twin of its shader (the same fragment
  stage over a vertex stage that includes `common/skinning`), passes both to `WorldRendererGeometry_DrawModel`
  (`WorldRendererMeshShaders`) and sets every per-object uniform on both while the entry has a pose (dither, outline
  flag, forward lights; the PBR factors go to the program the mesh draws with). A pass says whether it draws
  the camera's view (`WorldRendererMeshShaders.IsCameraView`: the G-buffer, the lit pass, the unlit fallback) or a
  light's (the shadow passes): the camera's draw each entry's `CameraPose`, which for the first-person object
  (`WorldRenderer_SetFirstPersonObject`) is the render cache's copy of its pose with the bones its world model's
  first_person block hides, and their subtrees, collapsed onto the hidden bone's joint; the lights' draw `Pose`, the
  whole body. A palette is uploaded once per pose per
  pass (`WorldRendererPalette`), and every frame starts by forgetting which pose each palette holds
  (`WorldRendererGeometry_ResetPalettes`). A new geometry pass follows the same pattern, or its skinned meshes draw in
  their bind pose.
- **Every geometry pass instances.** A pass that draws models has an instanced twin of its shader (the same
  fragment stage over a vertex stage that includes `common/instancing`, its definition defining `WORLD_INSTANCED`) and
  draws the model instances' cells with it (`WorldRendererInstances_Draw*`), setting the twin's per-entry uniforms
  (`instance*`), its per-cell ones (outline flag, forward lights) and the LOD per draw; without its twin, or while
  instancing is off, the pass draws each copy on its own through its ordinary shader with the C mirror's values. A new
  geometry pass follows the same pattern, or the copies are missing from it.
- **Terrain never skins and is never instanced**: its chunks draw with one rigid shader per pass and need no
  skinned or instanced twin.
- **Direct OpenGL calls** live only in `source/GLTextureArray.{h,c}` (raylib has no texture arrays) and the modules
  like it, `source/GLReadback.{h,c}` and `source/GLDepth.{h,c}`. Anything else that needs GL raylib lacks goes there or into a module like it (its function pointers through
  `rlGetProcAddress`, its constants defined in it, a missing function or a GL error degrading with one log line), never
  into a pass.
- **Vertex attributes (the budget).** GL 3.3 guarantees 16 attribute locations (0 to 15). raylib binds 0 to 12
  (`RL_DEFAULT_SHADER_ATTRIB_LOCATION_*` in `rlgl.h`): 0 position, 1 texcoord, 2 normal, 3 colour, 4 tangent,
  5 texcoord2, 7 bone indices, 8 bone weights, 9 to 12 the instance transform (a mat4 takes four). 6 is named for the
  indices but only marks the element buffer's slot: `rlLoadShaderProgram` binds no attribute there, so it is free too. A shader that declares its own attribute picks a location no mesh it draws fills otherwise:
  - **9 to 12: the terrain's weights**: sixteen palette weights as four normalized `u8 x 4` attributes
    (`vertexWeights0..3` in `terrain/vertex.vert`, filled by `WorldRendererTerrain.c` with `rlSetVertexAttribute`). A
    terrain mesh is never instanced, so the instance transform's locations are free there.
  - **13: the terrain's vertex flags**: a normalized `u8 x 4` (`vertexFlags`; x 1 on a face material's vertices,
    whose faces the rules leave alone; y, z, w 0 and free for later terrain flags). Shared with the instanced shaders,
    which never draw a terrain.
  - **9 to 13 in the instanced twins** (`common/instancing`): a copy's record, read once per copy (divisor 1)
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
lookups' light-facing quads), each with its own fragment stage in its own directory. The skinned twins
(`world_pbr_skinned`, `world_normal_skinned`, `world_depth_skinned`, `world_cube_depth_skinned` and the unlit fallback's
`world_unlit_skinned`) sit beside their rigid shaders, each a `skinned.vert` with the rigid shader's fragment stage
(`unlit/` has its own, raylib's default). Each renderer shader lives in its own directory under `asset/shaders/`. GLSL shared between
shaders lives in `asset/shaders/common/` and is pulled in with `#include "common/<name>"` (no `#version` in shared
files; rules in GameAssets.md, "GameShader"): `common/math` (`PI`), `common/atmosphere` (the air's uniforms, the
tables' layout and lookups, their march (through the ground for the haze, with light pollution's light) and the direct
march of single scattering, the haze volume's layout, see "Atmosphere"; used by the table builders, the volume's,
`world_sky`, `world_stars` and the haze's readers; the direct march's 16 view steps grow with the
square of their index, short near the viewer where every ray meets the densest air, and each step's light is dimmed by
the air in front of it and half its own: equal steps, each dimmed by all of its own air, would make the sky about 30 %
off and the horizon a dark brown band), `common/haze` (the volume's readers, the ground haze's closed forms and the
compositing, see "Haze"; used by `common/lighting`, `world_sky`, `world_stars` and `world_postfx`), `common/night_sky`
(air mass, extinction and the skyglow profile, used by `world_sky` and `world_stars`) and `common/night_vision` (the eye's night vision: the rods' share
and the blend into their grey; used by `world_sky` and `common/lighting`), `common/lighting` (the frame's lighting: its
uniforms, the BRDF and its wrapped variant, the shadow levels' shadow off the face with the contact shadows, point lights
and the cube shadow, the pre-pass's AO and sky visibility, the
night vision, the haze through `common/haze`: `ApplyAerialPerspective`, and interior lighting through
`common/interior`; used by `world_pbr`, `world_terrain` and `world_sprite`), `common/interior` (interior lighting: the
ambient zones a draw takes, the occupancy volume's march and the read straight above, the haze inside zones and the
debug view, see "Interior lighting"; used by `common/lighting` and `world_ao`), `common/distance_packing` (the cube shadow's
packed distance; used by `world_cube_depth`, `world_sprite_cube_depth` and `common/lighting`), `common/dither` (the
dithered fades: the draw's fade and its part of a LOD cross-fade; used by every world geometry shader, see
"Visibility") and `common/skinning` (the bone attributes and the palette, and a vertex's blended rows; used by
every skinned vertex stage, [Animation.md](Animation.md)), and `common/instancing` (a copy's record attributes, its
fade, LOD band and draw per pass, see "Model instances"; used by every instanced vertex stage). Put new shared lighting
or noise code there instead of copying it between shaders.

The atmosphere's table builders (`world_atmosphere_transmittance`, `world_atmosphere_multiscatter`,
`world_atmosphere_skyview`, `world_atmosphere_irradiance`) and the haze's volume builder (`world_atmosphere_volume`)
share one directory, `shaders/atmosphere/` (`atmosphere_*.json` with the fragment stages `transmittance`,
`multiscatter`, `skyview`, `irradiance` and `volume` over raylib's default vertex stage): full-screen passes into the tables' and the volume's targets.

The sky's occupancy volume draws with `world_sky_slices` (`shaders/sky_slices/`: its fragment stage over raylib's
default vertex stage; its twins `world_sky_slices_skinned` and `world_sky_slices_instanced` take the depth shaders'
`depth/skinned` and `depth/instanced` vertex stages) and builds its pyramid with `world_sky_reduce`; the ambient
pre-pass is `world_ao` and its blur `world_ao_blur`, full-screen passes over the G-buffer.

The instanced twins (`world_pbr_instanced`, `world_normal_instanced`, `world_depth_instanced`,
`world_cube_depth_instanced`) sit beside their rigid shaders as the skinned ones do, each an `instanced.vert` with the
rigid shader's fragment stage, and their definitions add `"defines": ["WORLD_INSTANCED"]`: a shader definition's
`defines` (at most 16 GLSL names) are written as `#define` lines after the main file's `#version` line in both stages
(`ShaderSource.h`), so one fragment stage serves both twins and shared files test the name with `#ifdef`
(`common/dither` reads the copy's flat values under it; `common/instancing` refuses to compile without it).

## WorldTestFrame

Concrete `GameFrame` (embeds `GameFrame` first). Owns a `World`, a `GameClock` ([GameClock.md](GameClock.md)), a
`GameCamera`, and a `WorldRenderer`. **The test world is a file** (its model components name world models, `world_models/test_world/`):
`Create` loads `<base>/worlds/test.world.json`
(`compile/out/worlds/` in the repository) with `WorldFile_LoadJSON` ([WorldFile.md](WorldFile.md)) on `Services.Keys`,
`Services.Assets` and `Services.Logger`, so its sprite components bind their world sprites themselves. A missing or broken
file is logged once (`WorldTest: could not load the test world (...): <why>. The world is empty instead.`) and gives an
empty world with the debug grid on: visible, and the frame still starts. The file holds a model component per cube
and for the ground slab, two light components (`warm_light`, `cool_light`), and the sprites, which name world
sprites (`world_sprites/test_world/`): the test image
as a camera-facing billboard, 1.5 m wide at 1280 pixels per metre, and placeholders: a blended tire-mark decal on
the ground in front of the camera, an axis-billboard tree that casts a shadow, a fixed sign behind it, a lit blended
smoke puff in front of an additive, unlit glow around the warm lamp, and an unlit neon sign. The tree stands a second
time, mirrored by its placement (`tree_mirrored`: `overrides_flip_x`), left of the first and farther back. Its hierarchy: a `desk` at (1.8, 0.05, -3.6) on the left of the start view,
an empty object turned 15 degrees that holds its parts (`desk_top` and two `desk_side_*`, scaled test cubes), a gold
`cup` and a `desk_lamp` (a thin post) whose child `desk_lamp_light` is a warm point light (intensity 3, reach 4, no
shadow) 0.35 lamp units above the post's centre (0.28 m: a child's position is in its parent's scaled space). Its
world models: two cubes behind the centre row, `variant_cube_left` (red) and `variant_cube_right` (blue), show the
`painted_cube` world model's two variants; the crisp cube and the 16 m shadow distances of the PBR demo cubes, the cup
and the lamp are placement overrides (`overrides_omit_pixelation`, `overrides_shadow_distance`). Its LODs: a row of six
`lod_test` spheres (`lod_test_1` to `_6`, ids 25 to 30) along the left edge of the ground, at x = 8 from z = -6 to 9,
switching at 13 and 16 m, and beside it `lod_test_forced` (id 31, LOD 2 forced) and `lod_test_own_distances` (id 32, its
own switch distances 3 and 6 m). Its collision ([WorldCollision.md](WorldCollision.md)): 44 colliders (the slab,
the cubes, the desk's parts, the LOD spheres' hulls, the PBR cube's mesh, the cup's, the tree trunk's and the trash
can's cylinders, the sign's plane, the test room's walls and roof), a `door` (id 33, the `test_door` world model with
a `closed` and an `open` set; movable, a
kinematic mover that stops when blocked) on the right of the start view, and a `desk_trigger_zone` (id 34, an own box,
a trigger) at the end of the desk's slide; the desk is movable. Its moving and pushing content (boxes, the `test` world model's `bounds`, every
crate pushable and movable): a `pusher_wall` (id 35, kinematic) behind a row of five crates `row_crate_1` to `_5` (ids 36 to 40) on
z = -1.5, a static `row_stop` (id 41) at the row's far end, and a `door_crate` (id 42) pinned in the corner of two
static posts (`door_post_west`, `door_post_north`, ids 43 and 44) in the door's swing. Its animation
([Animation.md](Animation.md)): four placeholder figures in a row at z = 2, facing the start view, each animator playing
a clip of its own: `humanoid_showcase` (id 45, x = -1.9: idle with a 0.3 s fade, the one the 5 key steps),
`humanoid_walker` (id 46, x = -5.2: walk from a quarter of the way in), `humanoid_short_waver` (id 47, x = -2.8: the
short figure waving, looped by its placement's override) and `humanoid_runner` (id 48, x = -6.1: run at half speed).
Its interior lighting (see "Interior lighting"): a `test_room` (id 54) 15 m in front of the start view, a pivot at
(1, 0.05, 8) whose children are four walls with a doorway facing the start view and a flat roof (`test` cubes with
colliders, ids 55 to 61), around its ambient zone (`room_zone`, id 62: to the walls' middle and 0.1 m past the floor and
the ceiling, still air, the default light).
Its two-sided shadows: two `test_card`s (a one-sided 1 m square facing up, `models/placeholder_props/card.obj`), scaled
to 1.4 m and floating 1.6 m above the ground right of the test room, `card_one_sided` (id 63) and `card_two_sided` (id 64,
`overrides_has_two_sided_shadows`): the shadow levels hold the faces turned away from the light, so in daylight the first
casts nothing (it has no face turned away) and the second a square shadow.
An asset made in Blender: a `trash_can` (id 65, the `trash_can_lined` world model: a slatted steel bin with a black
liner, four LODs from .glb files with their own maps, [WorldModels.md](WorldModels.md)) stands in the start view at
(0.3, 0.05, -2.7), in front of the crates; it is movable and pushable (30 kg).
Edit the file and restart to change the test world.
The frame owns a `WorldCollision` too, created after the world, and a `WorldCharacters` (the character
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
("door_crate"), which cannot get out of its way; it stops.`), then the animation system (`WorldAnimation_Update`:
the animators' rows and clips, their graphs and actions; slowed to a quarter while the 7 key's slow motion is
on) and its events (logged; a `push` from the door's action swings the door, a `release` sets the cup down on what
is under it), then advances the sprites of effectively enabled objects
(`World_GetComponents(world, WorldComponentType_Sprite)`), calls `World_UpdateTransforms` (the tick's
world-matrix step), so the renderer reads cached matrices, and last logs the characters' events (the player's use
starting or ending an action) and updates the player's look target. **`PrepareRender`** (GameFrame.md),
once per shown frame: the pose step (`WorldAnimation_Pose`), `World_UpdateTransforms` again (what the poses moved: held
items), then, in walk mode, the camera on the player's eyes (its body's eyes socket as posed) and the player named as
the renderer's first-person object (`WorldRenderer_SetFirstPersonObject`; 0 out of walk mode); last how open the
camera's surroundings are to the sky (`WorldCollision_GetSkyOpenness`, 30 m against the world and prop layers,
triggers left out) for interior lighting (`WorldRenderer_SetCameraOpenness`).
**Game time:** the clock starts from the world environment's date and time of day (noon, 2026-10-18) at time scale
720 (a 2-minute day; the clock's own default is 72). Every `Update` advances it,
logs a new day when it crosses midnight, and writes its time of day and date into the environment, so the sky shows
the clock. Then the weather moves (`source/WorldTestWeather.{h,c}`, [WeatherSystem.md](WeatherSystem.md)): a weather
system runs the climate the test world names (the default one) with the game seconds the clock moved and the tick's
real seconds, so the clouds, the rain, the wetness and the fog follow the clock and its presets change as the hours
pass. A held clock holds them; the gusts, the lightning and a flash's fading run on in real time.

All input runs in `Update` at the fixed timestep and comes from `Services.Input` ([GameInput.md](GameInput.md)), after
the UI test frame above had first refusal: movement through the actions (`move_*`, default WASD + SPACE/CTRL, `sprint`
default SHIFT), mouse-look (`GameInput_GetMouseDelta`, this tick's movement, only while the cursor is captured), the
mouse wheel (the flycam's speed, up faster and down slower, three notches to a doubling from 0.375 to 6144 m/s,
6 m/s at start; the sprint key still multiplies it by 3; each change is logged and the I key's camera line shows it;
not while walking, and the UI takes it first while the pointer is over a widget) and the debug keys as raw keys. A key the UI test frame takes never arrives here: R (it rebuilds the menu), Delete, and while
it navigates Tab, the arrows, Enter and Escape. Give the world test none of them (every letter key, F1 to F11, the
digits, minus, equals, `'`, `;`, `[`, `]`, `,`, `.`, `/`, the backslash, Insert, Home, End, Page Up, Page Down, the
keypad's digits, its `.`, `+` and `*` are taken already; the keypad's other operators are free. Leave F12 free:
raylib's `EndDrawing` takes a screenshot on it (`SUPPORT_SCREEN_CAPTURE`), writing `screenshotNNN.png` into the working directory):

- **the clock** (each change is logged with the date and time, e.g. `WorldTest: dawn (paused): Sun 2026-10-18
  06:00:00.`): P pause/resume the clock; 1–4 jump to dawn/noon/dusk/midnight of the clock's day (06:00, 12:00, 18:00,
  00:00) and pause; `[` `]` scrub the time while held (a tenth of a day per real second, across midnights, logged when
  let go); `,` and `.` the previous and next day, `/` a week ahead (the day keys keep the time of day);
- **the atmosphere (see "Atmosphere"):** keypad 1 switches the atmosphere's tables off and on (the A/B: off, the sky
  is marched per pixel, single scattering, and nothing is hazed; logged), keypad 2 the environment's
  `multiple_scattering` between 1 and 0, keypad 3 its `ozone` between 1 and 0 (each logged: compare dusk with and
  without the ozone layer);
- **the haze (see "Haze"):** keypad 4 steps the haze's debug view (off, a slice's in-scatter, its transmittance),
  keypad 5 the slice it shows (wrapping from the whole ray back to the first; logged with its distance:
  `WorldTest: haze view: the volume's in-scatter at slice 20 (3420.0 m).`), keypad 6 the environment's
  `aerial_distance_scale` (1, 5, 10), keypad 7 a morning mist (`ground_haze_density` 0.02, `ground_haze_thickness` 15,
  or no ground haze), keypad 8 the haze strength (`fog_strength` 1 or 0: clear air); each logged;
- **interior lighting (see "Interior lighting"):** keypad 9 switches it on and off (it starts off: every surface takes
  the whole sky's light, no ambient zone applies and the eye takes the sky's light as in the open), keypad 0 steps its
  debug view (off, the ambient zones' wireframes, the sky's share on every lit surface); each logged;
- **the moon and the seasons:** F pins the moon full (`MoonPhaseSource` fixed at 0.5) and gives it back to the
  calendar; Y switches the seasons (`AreSeasonsEnabled`). Every clock and moon log line carries the moon: `WorldTest:
  next day: Mon 2026-10-19 12:00:00; first quarter (phase 0.27, 59% lit, from the calendar).`;
- **the shadows (see "Sun and moon shadows"; `source/WorldTestShadows.{h,c}`, test content):** keypad + shows and hides
  the far field (made when first shown, not saved with the world: a 12 km plain just under the slab and 24 towers of the
  test cube, 6 x 40 m at 300 m, 15 x 100 m at 1 km and 40 x 250 m at 3 km in eight directions, every ring a little
  turned, all seen to 20 km, around the terrain's object), keypad * the shadow levels' debug view; each logged, and the
  I key's status says both;
- **the weather (see "Weather"; [WeatherSystem.md](WeatherSystem.md), "The test frame's weather"):** Insert moves into
  the climate's next preset in the file's order with its values at once (while the clock is held too: P and the
  number keys hold the rest of the weather, the wetness with it), Home locks and unlocks the weather,
  End steps its speed (x1, x10, x100 the climate's), Page Up strikes lightning now, Page Down steps the mode (simulated,
  fixed, scripted, off: off gives the world file's weather back), the backslash logs a forecast of the next 24 game
  hours; each logged, every strike too (its distance and direction), and the I key's status adds the weather's line
  (the preset, its hours left, the values). At the 720x clock a storm lasts 1.5 to 7.5 real seconds: Home holds it,
  Page Up shows its lightning at once;
- **the renderer and the sky:** O post effects, B bloom, G sun shafts, keypad . contact shadows, C crisp overlay, V
  night vision (logged), K
  sprite lighting (off draws every sprite unlit; logged), L light gizmos, N cycle the
  light pollution (0, 0.35, 0.7, 1; logged), X step the opacity of the centre cube (`test_object`) and the tree (1, 0.5,
  0; logged: a 50% dither on the pixel grid, then gone, their shadows with them), U frustum culling (off draws everything
  within its draw distance; the image must not change; logged), I log the renderer status (with the drawn, culled and
  fading counts), then the game time (`WorldTest: game time
  Sun 2026-10-18 12:00:00, running, 720 game seconds per real second (a day takes 2 real minutes); seasons off.`), the
  moon, the collision world's counts (`WorldTest: collision: 37 colliders (23 static, 14 movable), 293 shapes (61180
  mesh triangles, 0 infinite planes), 0 trigger pairs; 37 rebuilds and 6 moves in 3 syncs.`: the terrain is
  one static instance of 256 chunk shapes) and its last step's
  (`WorldTest: the last collision step: 0 movers, 0 pushables awake, 0 pairs of shapes, 6 touching contacts (6 object
  pairs in contact), 0 solver iterations in 1 substeps (0 solved again), deepest overlap left 0.00 mm, 0 blocked, 0
  crushed.`), the atmosphere's tables (on, off or unavailable, their sizes, how often they were built, the height
  the sky is seen from) and the frame times (`WorldTest: the last frame took 16.67 ms (60.0 a second; ...); its update
  0.41 ms and its render 2.10 ms on the CPU ...`: the frame time is the render's delta, the update and the render are
  WRStopwatch times);
- **the hierarchy:** J starts and stops the desk sliding back and forth along X (1.2 m either way, a 6-second
  period of real time): its parts, the cup, the lamp and the lamp's light follow; H hides and shows the desk, and with it
  everything on it (the lamp's light goes off). Both logged;
- **the world models:** E steps the two variant cubes through their variants: their own (red and blue), swapped,
  then none (`uses_variant` off: the model's own white), through the component's setters, so the cache resolves the
  variants again (logged); Z zooms the camera to a 15-degree view and back (logged): the zoom factor then lets the draw
  distances of models and sprites, and the LOD distances, reach about 4.4 times as far;
- **the LODs:** Q toggles the LOD tinting (logged): every model takes its LOD's colour (0 white, 1 green, 2 yellow,
  3 red, coarser magenta; LODs past 0 glow faintly, for the night), to watch the switches along the `lod_test` row;
- **collision:** F7 shows and hides the collider wireframes (the collision world's debug list, drawn by the
  renderer: world grey, prop orange, triggers violet, static ones darker; logged); F8 the contact points (white crosses
  touching, grey near, cyan normals, red pushes; logged). J slides the desk through the trigger zone: its parts log
  enter and exit. F6 swings the door open (a quarter turn about its hinge at 60 degrees a second) and closed: it stops
  on the crate pinned in its way (blocked, logged), and closing pushes the crate along. F9 sends the pushing wall out
  2.5 m along +X at 0.6 m/s and back: it pushes the row of crates against the stop, where it is blocked and stops;
  home again, the crates are teleported back where they were. F10 teleports the door's crate 1.2 m aside, out of the
  door's way, and back into its corner (pushed out of the door if they overlap). Each logged, with the contact and
  motion events;
- **render space and double positions:** F11 steps the test world through the far shift's places
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
- **the world file:** F5 saves the live world next to the test world, as `worlds/test.saved.world.json` and
  `worlds/test.saved.world.ghdf` (atomically; each logged with its path, gitignored), for round-trip checks: its diff
  against `test.world.json` shows what changed in the game. Saved at noon right after the start (2, then F5), only what
  the first ticks moved: the crates settling and the weather's values (the climate runs from the first tick); later the
  time of day once the clock ran, a slid desk, an opacity;
- **the characters:** F1 switches walk mode on and off: walking creates a `player` object (a runtime object, never
  saved) with a character component of the test human below the flycam, dropped to the ground, and the camera then
  sits at its eyes (`PlayerController`: WASD walk relative to the view, SHIFT runs, SPACE jumps (the `jump` action),
  CTRL crouches while held (`crouch`), E uses what you look at (`use`, logged with what it hit; it takes the E press
  from the variants key while walking), the mouse looks around). F2 spawns and removes the test NPCs 3 m in front of
  the camera: a human walking a square and crouching every third side, a dog (brown) running circles and lying down, a
  pigeon (grey-violet) walking, taking off, flying a loop and landing: each a character driven by a scripted pattern
  through the same `CharacterInput` as the player; the dog and the pigeon wear a placeholder body of two test cubes
  (the trunk squashes when it crouches), the human wears the animated placeholder humanoid. F3 lets the player fly
  and lands it (SPACE and CTRL climb and sink while flying), F4 turns the player into the next kind (human, dog, pigeon:
  the same object, another character asset; a human wears the humanoid, and what its hand held is set down when it stops
  being one). The log names what you look at whenever it changes and every character event (jumped, landed with its
  speed, left the ground, crouched, stood up, took off, stopped flying, used); I adds `WorldTest: you: a human
  (test_human), walking, standing, on object 2 ("ground"), 1.40 m/s, eyes 1.62 m up; looking at ...; in no action;
  ...`;
- **the bodies and their actions ([Animation.md](Animation.md)):** as a human the player wears the humanoid and
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
- **animation ([Animation.md](Animation.md); the private `source/WorldTestAnimation.{h,c}`):** 5 steps the
  showcase figure (`humanoid_showcase`) through the placeholder's clips (idle, walk, run, crouch_idle, crouch_walk,
  jump, fall, land, wave, sit_down, sit_idle, stand_up, pick_up, hold_r, put_down, open_door), each a
  cross-fade over its animator's 0.3 s (logged); 6 brings out a crowd of 100 figures
  (runtime objects, never saved; tall and short alternating) in a square in front of the camera, facing it, each
  walking at its own phase and a little faster or slower, and takes it away; 7 slows every animation to a quarter and
  back (logged). I adds `WorldTest: animation: 104 animators (104 posed), 104 clips playing, 1976 bones posed in 182 us;
  the showcase plays "idle" (5); crowd out (6); slow motion off (7).`;
- **the terrain ([Terrain.md](Terrain.md)):** minus (`-`) shows and hides the chunk key (logged): each chunk takes a
  colour by its cell (neighbours always differ) mixed into its lit colour, and the wireframe of the chunks in view is
  drawn over it, a hair toward the camera. The status line (I) counts the chunks drawn and their triangles, and
  lists each terrain's materials and its arrays' sizes. **Equals** (`=`) cycles what the terrain shows (logged):
  shaded, the painted weights (their debug colours, before the rules), the weights after the rules, the dominant material
  after height blending, then shaded again;
- **the model instances (the private `source/WorldTestInstances.{h,c}`; see "Model instances"):** the test world's
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
key registry (`KeyRegistry`, [NamespacedKey.md](NamespacedKey.md)) → the root registry (`RegistryRoot`,
[Registry.md](Registry.md)) → `GameInput` (defaults, then `<base>/controls.json` if present) → one log line listing the
root's registries → GameFrameManager (polls the input) → build `Services` (`Services.BaseDirectory`, `Services.Input`,
`Services.Keys`, `Services.Registries`) → create + add `WorldTestFrame`, then `UITestFrame` on top → run loop.
Teardown (reverse, ordering matters): frame manager (releases assets + GPU targets, needs asset manager + GL alive;
its worlds borrow the key registry) → asset manager → JSON pool → GHDF pool → `CloseWindow` → input → root registry
(after every system withdrew its registries) → key registry (the root borrows it) → unicode → config → base-directory
buffer → logger.

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
