#pragma once
#include "TestFramework.h"


/**
 * Entry points of the test suites, one per test file, called in order by TestMain.c. To add a suite: create
 * "<Module>Tests.c" with a static TestCase array and a "<Module>Tests_Run" function that passes it to
 * TestRun_RunSuite, declare that function here, and call it from TestMain.c.
 */

/** @brief GameJSON value conversions (GameJSONTests.c). */
void GameJSONTests_Run(TestRun* run);
/** @brief WRFramework behaviours the game relies on: JSON insertion order and one-line scalar arrays, GHDF
 *  strings read in chunks and refused when they declare more than the data holds, pools freeing borrowed containers,
 *  truncated documents leaving nothing behind, atomic file replacement (WRFrameworkTests.c). */
void WRFrameworkTests_Run(TestRun* run);
/** @brief WRFramework's double-precision maths (WRVectorMath, 2D and 3D) and the GameMath bridge to raylib:
 *  every operation by hand, raymath's conventions on the same inputs (2D vectors, multiply order, rotations, points and
 *  directions, inversion), the column-major layout against MatrixToFloatV and raylib's Matrix fields, conversions
 *  relative to a far origin, inverses 10 000 km out, boxes around transformed boxes and the ray slab test against a
 *  face by face check (WRVectorMathTests.c). */
void WRVectorMathTests_Run(TestRun* run);
/** @brief GameGHDF: integers in the smallest type, read from any integer type in range, strings without NULs
 *  (GameGHDFTests.c). */
void GameGHDFTests_Run(TestRun* run);

/** @brief Config file loading and validation (ConfigTests.c). */
void ConfigTests_Run(TestRun* run);

/** @brief Asset definition scanning, file matching and the failed-load cache (AssetManagerTests.c). */
void AssetManagerTests_Run(TestRun* run);

/** @brief Shader #include expansion: line numbering, repeats, depth, errors, the shipped shaders (ShaderSourceTests.c;
 *  a private module of source/). */
void ShaderSourceTests_Run(TestRun* run);

/** @brief The texture asset type and the shared texture_properties parser: keys, defaults, rejections, the size limit
 *  and the shipped placeholder (TextureTests.c). */
void TextureTests_Run(TestRun* run);

/** @brief The packed occlusion/roughness/metallic map a glTF model's materials get: each map's red in its channel, a
 *  missing map as 255, the size and the resampling, the occlusion strength, the maps it refuses (ModelOrmMapTests.c; a
 *  private module of source/). */
void ModelOrmMapTests_Run(TestRun* run);

/** @brief A glTF model's material values the game reads itself: glTF's defaults, every factor, the emission strength
 *  extension, the occlusion strength, textures raylib skips, values out of range and files that are not glTF 2.0, and
 *  colours encoded as the renderer decodes them (ModelGltfMaterialsTests.c; a private module of source/). */
void ModelGltfMaterialsTests_Run(TestRun* run);

/** @brief The collision narrowphase: affine maths and poses, supports under any transform, GJK distances, EPA
 *  penetrations, contact manifolds and their feature ids, rays against every shape, linear and rotating casts (the
 *  fast thin cases that would tunnel with steps), convex hulls and their reduction, meshes with their active edges and
 *  BVH (CollisionGeometryTests.c; private modules of source/). */
void CollisionGeometryTests_Run(TestRun* run);

/** @brief Point and spot light reach culling: the cone against spheres, the ranking, the reaching count
 *  (WorldLightCullingTests.c). */
void WorldLightCullingTests_Run(TestRun* run);

/** @brief The light component (its defaults, the cone's rules, its setters, its JSON) and the flicker (deterministic,
 *  bounded, each kind's character) (WorldLightTests.c). */
void WorldLightTests_Run(TestRun* run);

/** @brief The local lights' shadows: which point light gets the cube and which spots get maps (their strength, the view,
 *  the hysteresis), a spot's bounding sphere and its map's projection and bias (WorldLocalShadowsTests.c; a private
 *  module of source/). */
void WorldLocalShadowsTests_Run(TestRun* run);

/** @brief Namespaced keys: the key characters, the length limit, the default namespace, interning, stable texts and
 *  prefix search (NamespacedKeyTests.c). */
void NamespacedKeyTests_Run(TestRun* run);

/** @brief Registries: registering, finding by id and text, getting, prefix search in byte order, active duplicates
 *  refused with both sources named, withdrawn keys reviving their index, a source's withdrawal, the element
 *  deconstructor run once per element, stable element addresses, the maximum, bad arguments, the revision, and the root
 *  registry (RegistryTests.c). */
void RegistryTests_Run(TestRun* run);

/** @brief Data values: equality, deep copies, homogeneous lists, the depth and size limits, compound names, validation,
 *  byte sizes, and the JSON and GHDF forms with hostile input (DataValueTests.c). */
void DataValueTests_Run(TestRun* run);

/** @brief Object tags and data: the data functions, tags, the data revision, the object and world limits, queries (each
 *  filter, the order, a brute-force comparison on a seeded random world), the tag index, and saving through the object
 *  table with hostile input (ObjectDataTests.c). */
void ObjectDataTests_Run(TestRun* run);

/** @brief The world's objects and components: ids, the per-type arrays, revisions, the sprite binding under the
 *  world's asset user, flags, and the one-transform-driver rule with test-only component types (WorldTests.c). */
void WorldTests_Run(TestRun* run);

/** @brief World models: the definition's rules and their messages, the limits, loading with the LOD models and the
 *  variants' textures as dependencies (and releasing them), variants by name and on one material slot, the model
 *  component's override flags and the settings they resolve to, and the shipped world models (WorldModelTests.c). */
void WorldModelTests_Run(TestRun* run);

/** @brief Screens: a world model's surfaces (every rule of the section and its message, LOD 0's triangles of each slot
 *  with their texture coordinates, the loads that fail), where a ray meets a placed surface (a quad, a turned, scaled,
 *  mirrored and far placement, a curved surface in facets, misses, a border off the picture), the screen component (its
 *  defaults, rules and saves), the screens system (what each screen resolves to and logs once, the nearest screen a ray
 *  points at, what hides one, following the components, removals between syncs; also 100 km out), the host of a
 *  screen's UI (the pointer, focus by use or a click, the keyboard taken after the UI reads it, typing a release key,
 *  letting go by Escape, pause or pointing away), the render cache's screens, the renderer's pictures for one render (only
 *  on their slot's draws, before the LOD glow) and its status's counts (WorldScreenTests.c). */
void WorldScreenTests_Run(TestRun* run);

/** @brief A CRT screen's maths: the curvature (the centre and the edges' middles kept, the corners in, the identity at
 *  0), the power transition's timing both ways, the power animation's shapes and their smoothness, the scanlines'
 *  levels, the detail's fade, the flicker and the defaults (CrtScreenTests.c). */
void CrtScreenTests_Run(TestRun* run);

/** @brief Colliders: the world model's collision section (every key, the "bounds" box under the import transform,
 *  each rule and its message, the limits, hulls and meshes from models as dependencies, a model that cannot give
 *  them), the collider component's defaults, the settings it resolves to from its override flags over the world
 *  model's defaults, its setters' checks, and its JSON and GHDF round trips (WorldColliderTests.c). */
void WorldColliderTests_Run(TestRun* run);

/** @brief The collision world: the sync (moves without rebuilds, set and world model switches, disabled objects and
 *  ancestors, a replaced collider at its predecessor's address), rotation modes, the collider's transform and uneven
 *  scales (ellipsoids), both trees against brute force on a seeded random scene that moves, filters (layers, triggers,
 *  ignored subtrees) and the layer rule of an object's contacts, mobility and static moves, trigger events (hysteresis,
 *  removal, the pairs that never form, their order), rays, continuous casts (fast, starting inside, turning),
 *  overlaps, manifolds and their local points, mesh triangles, planes, the debug list and the warnings given once
 *  (WorldCollisionTests.c). */
void WorldCollisionTests_Run(TestRun* run);

/** @brief Moving and pushing: no tunnelling at any speed (a thin wall against fast spheres and boxes, a fast fan
 *  blade, two fast spheres meeting), pushing many at once (a row and a grid against a wall, stopping or pushing through,
 *  crushed reports), mass shares, sliding, blocked movers, teleports, the write-back through a parent, carried
 *  colliders, contact events and their data, motion results (the ground, a moving platform), sleeping and determinism
 *  (WorldCollisionMotionTests.c). */
void WorldCollisionMotionTests_Run(TestRun* run);

/** @brief Characters: the character asset's and component's tables and overrides, the input, walking into walls,
 *  corners, steps, slopes, stairs and ledges, platforms, other characters, crouching, jumping, flying, tunnelling, the
 *  look ray and use (WorldCharacterTests.c). */
void WorldCharacterTests_Run(TestRun* run);

/** @brief World sprites: the definition's defaults, rules and messages (the rules shared with the sprite component's
 *  rows), loading with the animation as a dependency, the sprite component's override flags and the settings they
 *  resolve to (the draw distance from the placement, the world sprite or the kind), swapping the world sprite, and the
 *  shipped world sprites (WorldSpriteTests.c). */
void WorldSpriteTests_Run(TestRun* run);

/** @brief The transform hierarchy: World_SetParent's refusals (unknown ids, self, cycles, depth), world matrices down
 *  three levels, reparenting with and without keeping the world placement, dirty propagation with a recompute count,
 *  reads that never store, child order, subtree removal (components, arrays, hooks children first), effective flags,
 *  and the parent row through the table (WorldHierarchyTests.c). */
void WorldHierarchyTests_Run(TestRun* run);

/** @brief Transform maths: DrawModelEx's composition, the decomposition's round trips (mirrors, zero scales, gimbal
 *  lock, shear), local matrices under a parent and the parents that cannot be inverted, and the bound on a matrix's
 *  stretch (WorldTransformTests.c; a private module of source/). */
void WorldTransformTests_Run(TestRun* run);

/** @brief World files: JSON and GHDF round trips of a world using every property, the written form (defaults left out,
 *  table order, parents first), hostile input refused with nothing left (ids, parents, cycles, depth, counts, sizes,
 *  types, truncated and lying GHDF), unknown names skipped with one warning each, the next object id, files and memory
 *  buffers, atomic saves and the shipped test world (WorldFileTests.c). */
void WorldFileTests_Run(TestRun* run);
/** @brief Property tables: the self-check (every table in the game), construction, validation, copies, JSON and GHDF
 *  round trips, unknown keys, access by key, migration, and the environment's table against a hand-written encoder
 *  (PropertyDescriptorTests.c). */
void PropertyDescriptorTests_Run(TestRun* run);

/** @brief The calendar: day numbers to dates and back over eight centuries, weekdays, leap years, month lengths,
 *  validation, names and the date text (GameCalendarTests.c). */
void GameCalendarTests_Run(TestRun* run);

/** @brief The game clock: the time scale, pausing, crossings, the largest step, fast-forwards over month and year
 *  ends, 1000 days of 480 Hz ticks without drift, validated jumps, the calendar's end, and saving through its table
 *  (GameClockTests.c). */
void GameClockTests_Run(TestRun* run);

/** @brief Environment validation, copying, the date and time the clock writes, and the celestial frame
 *  (WorldEnvironmentTests.c). */
void WorldEnvironmentTests_Run(TestRun* run);

/** @brief The climate asset: the shipped default climate, the defaults, every key, refused definitions and the
 *  quantities' rules (ClimateTests.c). */
void ClimateTests_Run(TestRun* run);

/** @brief The weather system: determinism, ranges and rates, rain and wetness, presets by weight, month and hour, the
 *  modes, locking, lightning, gusts, the forecast, saving and fast-forwards (WeatherSystemTests.c). */
void WeatherSystemTests_Run(TestRun* run);

/** @brief The weather's look on the renderer's side: the clouds' cover and deck, the flash, the wind and the fog's ground
 *  haze (WorldWeatherLightTests.c). */
void WorldWeatherLightTests_Run(TestRun* run);

/** @brief The clouds' noise: tileable gradient and Worley noise, and the three textures (determinism, spread, seams)
 *  (CloudNoiseTests.c; a private module of source/). */
void CloudNoiseTests_Run(TestRun* run);

/** @brief The clouds' frame: the layers from the environment, the patterns' drift and anchors, the ray's stretch through a
 *  layer and the shadow map's frame (WorldCloudsTests.c; a private module of source/). */
void WorldCloudsTests_Run(TestRun* run);

/** @brief The rain: its rate and mist, the streaks' drift and their copies around the camera, the rain map's box and
 *  lookups, the splashes' landing and grid, the puddles' coverage and threshold, the bolt (WorldRainTests.c; a private
 *  module of source/). */
void WorldRainTests_Run(TestRun* run);

/** @brief The screen-space reflections: the screen share by roughness, the direction's packing, the answers' fades, the
 *  distance a stored depth stands for and the march over synthetic scenes (WorldReflectionsTests.c; a private module of
 *  source/). */
void WorldReflectionsTests_Run(TestRun* run);

/** @brief Water: the water material's definitions (the defaults, every key, each rule refused, its maps as dependencies,
 *  a missing map), a world model's "water" section and its variants' swaps (the rules, the slots found by material slot,
 *  the materials as dependencies, a slot no mesh has, a material that does not load), and the C mirror of the water's
 *  shader (the drift with the wind and the flows, the maps anchored to the world far out, the waves' strength in the
 *  wind, the bottom's absorption and the foam) (WaterTests.c). */
void WaterTests_Run(TestRun* run);

/** @brief The night sky's maths: the star catalogue, limiting magnitude, air mass, star colours and the galaxy's
 *  orientation (NightSkyTests.c; a private module of source/). */
void NightSkyTests_Run(TestRun* run);

/** @brief The eye's night vision: its calibration on the game's light scale, untouched daylight, rods at night, the
 *  strength, the rods' grey (NightVisionTests.c; a private module of source/). */
void NightVisionTests_Run(TestRun* run);

/** @brief The eye's adaptation to the picture: the meter's centre-weighted mean, the estimate, the first
 *  measurement, the easing in stops, the window, the real scale, the partial adaptation's exposure and a room's bias
 *  on it (EyeAdaptationTests.c; a private module of source/). */
void EyeAdaptationTests_Run(TestRun* run);

/** @brief The renderer's per-object render cache (WorldRenderCacheTests.c; a private module of source/). */
void WorldRenderCacheTests_Run(TestRun* run);

/** @brief A sprite quad's maths: billboards facing the camera, axis billboards, fixed sprites, the pivot and flips,
 *  lights as viewers, size and bounds, the back-to-front order (SpriteTransformTests.c; a private module of source/). */
void SpriteTransformTests_Run(TestRun* run);

/** @brief What the renderer draws: frustum planes and the cascade box, draw distances and their fade band, opacity, the
 *  dither's C mirror, and the per-pass lists built from a code-built cache (WorldVisibilityTests.c; a private module of
 *  source/). */
void WorldVisibilityTests_Run(TestRun* run);

/** @brief Render space: the view a pure rotation wherever the camera is, points 50 km out within a hundredth of a
 *  scene pixel of a double-precision camera and near the origin where the world-space pipeline put them, the sky's ray
 *  exact at any distance, the shadow cascades' texels standing still while the camera moves (also for a light near
 *  straight up) and their grid turning about the camera while the light turns, near the origin and far out; each also
 *  shows the world-space maths failing far out (WorldRenderSpaceTests.c; a private module of source/). */
void WorldRenderSpaceTests_Run(TestRun* run);

/** @brief Animation libraries: the placeholder files' bones and clips, clip settings (names, loops, speeds),
 *  sampling (the ends held, the last key exact, step, linear, cubic splines, the shorter arc, normalized integers),
 *  node matrices, unsorted parents, skipped clips, hostile files and limits, the definition's rules and the asset
 *  loading its file (AnimationLibraryTests.c). */
void AnimationLibraryTests_Run(TestRun* run);

/** @brief Animation graphs: a graph using every part of the format read back record by record (parameters and
 *  their indices, layers with masks, blends, clips, empty states, events with their effects, transitions from any state
 *  and from lists, every kind of condition, look-at, actions with anchors), the defaults, the block copied as bytes, the
 *  asset type, every rule's refusal naming its reason, and the limits (AnimationGraphTests.c). */
void AnimationGraphTests_Run(TestRun* run);

/** @brief The graph player: bindings (clips with their natural speeds, masks, look bones, what is missing), the
 *  playback's start, blends in step matching the speed within their rate limits, transitions (from any state, exit
 *  times, priority, one a tick), timed events (by phase and time, at the start, once each, "set"), layers over the body
 *  by mask and weight, socket conditions, look-at's easing, limits, weight and import rotation, cross-fades, full
 *  layers and missing clips (AnimationGraphPlayerTests.c). */
void AnimationGraphPlayerTests_Run(TestRun* run);

/** @brief Skeletal animation's runtime maths: transforms composed and taken apart (a non-uniform scale under a turn
 *  too), skeletons from raylib models (rest and bind poses, order, names, broken ones, meshes the GPU cannot skin, the
 *  same bones), the rest pose's palette and bounds, blends, pose bounds holding every skinned vertex, bindings by name
 *  (unmatched bones, folded ancestors, the height ratio, a shorter figure keeping its bone lengths, a clip on its own
 *  skeleton as authored) and the mixer (cross-fades keeping the weights' sum, loops and ends, replays, weights alone, a
 *  full mixer, the blended pose) (SkeletalAnimationTests.c). */
void SkeletalAnimationTests_Run(TestRun* run);

/** @brief The animation system: animators bound on demand to their world models' skeletons and libraries, the
 *  clip row (phase, cross-fades, speed, pause, loop override), clips played from code over the rows, the libraries'
 *  override, rebinding, sockets in double (also 173 km out), disabled objects, one-time reports and the animator's rows
 *  (WorldAnimationTests.c). */
void WorldAnimationTests_Run(TestRun* run);

/** @brief The animation system's graphs and actions: a graph from the world model following its character (its
 *  speeds, ground and events), parameters, triggers and states from code, clips from code over the graph, sockets
 *  carrying their children with the pose (and the arm layer's socket condition), actions attaching and detaching their
 *  target, an anchored action sliding the character onto a bench's seat and ended by its driver, what actions refuse,
 *  and a rebind or the system's end freeing the character; also 173 km out (WorldAnimationGraphTests.c). */
void WorldAnimationGraphTests_Run(TestRun* run);

/** @brief LODs: the selection by distance with hysteresis, the scale, the zoom and a forced LOD; the cross-fade's state
 *  (its start, progress, snapping, a fade time of 0, held frames); the two LODs' complementary dither masks
 *  (WorldLodTests.c; a private module of source/). */
void WorldLodTests_Run(TestRun* run);

/** @brief The sun's and the moon's shadow levels: their sizes and texels, the levels the reach needs, which level a point
 *  reads with the bands between them and the reach's fade, the size cut, the LODs far levels pick, the update schedule
 *  (staggered periods, the turn and the move that render a kept level at once, new settings), and kept levels read as
 *  they were rendered, near the origin and 10 000 km out (WorldShadowLevelsTests.c; a private module of source/). */
void WorldShadowLevelsTests_Run(TestRun* run);

/** @brief The terrain's shadows at any distance: the nested maps' sizes and their frame on the coarsest texel grid, the
 *  maps drawn from above, the light's two disc edges, the sweep's passes and their schedule across frames, the map a
 *  point reads with its height lowered by the curve, the disc's share, and the sweep's doubling against a brute-force
 *  march, ridges' shadows with the exclusion and a ray continued in the coarser map (WorldFarShadowsTests.c; a private
 *  module of source/). */
void WorldFarShadowsTests_Run(TestRun* run);

/** @brief Instanced drawing's pure maths: cells (every copy in one, grid order, records composing back to the copies,
 *  spheres holding them, stable random values), LOD bands (around the switch distances, stopping at the middle,
 *  complementary and continuous, the zoom and the scales, a forced LOD), fades by the models' rule, the passes' rules
 *  and the cells' LOD masks (WorldInstancingTests.c; a private module of source/). */
void WorldInstancingTests_Run(TestRun* run);

/** @brief The atmosphere's pure maths: the air's coefficients from the environment (the defaults, the settings,
 *  clamping), when its tables are stale, the height the sky is seen from, heights and distances along rays without
 *  cancellation, and the tables' mappings (the transmittance's, the multiple scattering's, the sky view's: round trips,
 *  their corners, rows dense near the ground and the horizon, the azimuth from the light, the halves)
 *  (WorldAtmosphereTests.c; a private module of source/). */
void WorldAtmosphereTests_Run(TestRun* run);

/** @brief The sky's light on the surfaces: the exact transmittance against a long double integral, the light
 *  setting over its disc, the C tables against the exact light, the sky's and light pollution's irradiance against fine
 *  integrals, the frame's light from its parts and the settings, broken values, the shadow light's hand-over at dusk
 *  through the air, the light falling continuously into the night, the exposure, and the night vision's anchors on this
 *  scale (WorldSkyLightTests.c; a private module of source/). */
void WorldSkyLightTests_Run(TestRun* run);

/** @brief Interior lighting's pure maths: the ambient zone component's defaults and rules, zones placed by their
 *  objects (turned, scaled, a million metres out, collapsed), their weights (the fade, surfaces facing in, the camera),
 *  priorities laid over each other, the draws' choice of at most four, the haze inside them, the sky's visibility map on
 *  its texel grid, the taps and their reference heights (flat, upright, sloped, facing down), the visibility on stand-in
 *  heights (open, a room, a ceiling, a facade under eaves, a slope, a curb) and its fade toward the map's edge, the
 *  share's floor, and the camera's easing and light (WorldInteriorTests.c; a private module of source/). */
void WorldInteriorTests_Run(TestRun* run);


/** @brief Log rotation, pruning and formatted output under a base directory (LoggerTests.c). */
void LoggerTests_Run(TestRun* run);

/** @brief The text component tree: ownership, depth limit, owned strings, revisions, clone, style (TextComponentTests.c). */
void TextComponentTests_Run(TestRun* run);

/** @brief The shared JSON/GHDF text codec: round trips, shorthands, errors, depth limit (TextComponentCodecTests.c). */
void TextComponentCodecTests_Run(TestRun* run);

/** @brief Text documents: tree ownership, font and animation bindings, fallbacks, ticking (TextDocumentTests.c). */
void TextDocumentTests_Run(TestRun* run);

/** @brief Text layout: lines, wrapping, shrinking, alignment, runs and staleness; carets: offsets, hits, lines, ranges (TextLayoutTests.c). */
void TextLayoutTests_Run(TestRun* run);

/** @brief The text piece: what comes in through the limits, editing, words, codepoint boundaries, and the grouped undo with its budget,
 *  its step cap and its mark (UITextEditTests.c). */
void UITextEditTests_Run(TestRun* run);

/** @brief Fonts: glyph lookup, the "codepoints" key, font definitions and the shipped fonts (FontTests.c). */
void FontTests_Run(TestRun* run);

/** @brief The label widget's text, layout properties and construction (LabelWidgetTests.c). */
void LabelWidgetTests_Run(TestRun* run);

/** @brief Keys, the per-tick input with consumption, actions and controls files (GameInputTests.c). */
void GameInputTests_Run(TestRun* run);

/** @brief The input_actions asset type: its reader's rules and its registration with the input (InputActionsTests.c). */
void InputActionsTests_Run(TestRun* run);

/** @brief The value piece: its rules, the drag range, normalizing, conversions (custom ones checked), steps, a scrub's rate and a coarse
 *  snap, text that reads back and shown digits, hostile typed and pasted text, the functions by id, changes and the clipboard
 *  (UIValueTests.c). */
void UIValueTests_Run(TestRun* run);

/** @brief The slider on the value piece: Shift as a flag on a drag's rate in every order, the thumb under the pointer past an end, a
 *  slow drag over a step, upright sliders, fine steps, a middle click, value lists, log sliders, its context menu (UISliderTests.c). */
void UISliderTests_Run(TestRun* run);

/** @brief The property box: what it shows, a press becoming a scrub, scrubbing with and without a range, Shift and Ctrl, soft and hard
 *  limits, cancelling, typing (good, bad, out of range, Enter, Escape, Tab both ways), far doubles through the text, mixed, strings, the
 *  wheel, resetting, the clipboard, its menus, disabling (UIPropertyBoxTests.c). */
void UIPropertyBoxTests_Run(TestRun* run);

/** @brief The compounds: a labelled element across and stacked, a form's labels lined up, a compound slider's parts in step (code on
 *  either, a drag, typing, settings, mixed, enabled) with its events its own, its scale and hover text, a row of fields' indexed events,
 *  Tab along it and mixed per axis (UICompoundTests.c). */
void UICompoundTests_Run(TestRun* run);

/** @brief The list: the selection's runs, a hundred thousand rows costing the rows in view with no allocation while idle, the selection by
 *  clicks with Ctrl and Shift, a press waiting for its release, the modes, the keys only once entered, Enter and the double click, the
 *  context menu of a row, rows inserted, removed and counted anew under the selection, toggle cells, the wheel, disabled, where a point
 *  falls (UIListTests.c). */
void UIListTests_Run(TestRun* run);

/** @brief The tree: the walk of what is expanded (and its depth limit), a hundred thousand nodes costing the rows in view, the expander and
 *  the side keys, selected nodes outliving a collapse but not a removal, events naming their node whole, where a drop would go
 *  (UITreeTests.c). */
void UITreeTests_Run(TestRun* run);

/** @brief The modal dialog: Enter and Escape answering from an entered field in one press (the field's own events first), Enter entering a
 *  field only focused, the focus moving in and coming back and kept inside, a destroyed owner, buttons by the mouse and the backdrop, no
 *  cancel, a typed value committed before the answer, options refused (UIDialogTests.c). */
void UIDialogTests_Run(TestRun* run);

/** @brief The colour picker: every colour back through HSV, hex read and written, dragging changing and closing committing, Escape putting
 *  the colour back, the hex field and a bad hex, a channel field, mixed, a RenderColor's brightness and opacity, disabled and a destroyed
 *  swatch (UIColorPickerTests.c). */
void UIColorPickerTests_Run(TestRun* run);

/** @brief Drag and drop: the distance a press moves before it drags, a target that takes it and one that refuses (the pointer's shape, what
 *  shows the drag), the drop to both and its record, Escape, a target and a source going, a drag across a scroll area into a list that
 *  scrolls, a plain widget as a source, a payload's text cut (UIDragDropTests.c). */
void UIDragDropTests_Run(TestRun* run);

/** @brief The tool bar: its items laid out in a row and a column, its buttons' clicks as its events (a button, a toggle, a choice of a
 *  group, a disabled item, the keyboard), states set by calling code without events, its names as hover text, bad items refused
 *  (UIToolbarTests.c). */
void UIToolbarTests_Run(TestRun* run);

/** @brief Windows: the title bar moving one, every edge and corner resizing it within its least size and the workspace, its least size
 *  from its content per axis, staying reachable as the surface shrinks (every corner, odd scales, a window of its own scale) and coming back
 *  as it grows, minimize, maximize (one at a time, the workspace's strips), a double click, closing and its query, a window destroyed
 *  mid-drag, a press on the title bar acting on nothing inside and the focus coming back (entered again if it was), raising within a layer,
 *  its scale, a press on what keeps the focus, the workspace, refused options, the arithmetic, a window on a world screen
 *  (UIWindowTests.c). */
void UIWindowTests_Run(TestRun* run);

/** @brief The dock tree: tabs added, removed, moved and selected, leaves split on each side, empty leaves collapsed, ratios, the limits,
 *  arranging with minimums at many sizes (whole units, no gaps or overlaps), the ratio a splitter's drag sets, drop zones, broken trees
 *  refused, random operation sequences keeping it whole (UIDockTreeTests.c). */
void UIDockTreeTests_Run(TestRun* run);

/** @brief Layouts: names checked and made up, a layout checked whole and compared by meaning, round trips through the document, unknown
 *  window types and second windows of a single type dropped, bad documents changing nothing, docks deeper or larger than a tree refused,
 *  thousands of mutated documents never breaking it, the file read, written atomically and put aside when bad, a folder that cannot be
 *  written keeping its file, saving once the changes settle (UILayoutsTests.c). */
void UILayoutsTests_Run(TestRun* run);

/** @brief The workspace: its bars and the strips they keep from windows, docking as tabs and splits, a press on a tab selecting it and
 *  focusing its window, windows in tabs not selected neither shown nor updated, closing tabs, a tab dragged out carrying its window on to a
 *  drop (the same press), Escape putting it back, reordering along a strip, drop guides and floating elsewhere, an empty dock, a window
 *  destroyed mid-drag, what a window holds kept through docking, maximizing out of the dock and back, splitters within the windows' least
 *  sizes and through a surface that changes size, a tab's context menu, its tabs' names and hover text, hiding it, single types
 *  (UIWorkspaceTests.c). */
void UIWorkspaceTests_Run(TestRun* run);

/** @brief The render view: its record through a tick (presses and releases told once, the wheel focused or not, hover and the pointer's
 *  place, a lost press, a hidden-cursor drag), its kept keys, Tab and Enter, its size following a splitter and an edge, shown only while its
 *  window shows, a title bar press reaching no record, a drop, its texture, what is refused (UIRenderViewTests.c). */
void UIRenderViewTests_Run(TestRun* run);

/** @brief The editor's fly camera as pure maths: the speed's steps by the wheel, looking, flying along where it looks, across and up, the
 *  fast key, nothing without flying or time, far away in double (EditorCameraTests.c). */
void EditorCameraTests_Run(TestRun* run);

/** @brief The editor frame with no window: an empty world without a name, a named world and bad names, a tick syncing what moved and
 *  flying, the keys only while flying in the entered view, looking, the wheel, the world's window closed and reopened, the layout kept
 *  across a restart, its services (EditorFrameTests.c). */
void EditorFrameTests_Run(TestRun* run);

/** @brief Window types: registering and finding them (through the root too), refused definitions and keys, withdrawing and reviving,
 *  opening one filled by its type, centred and stepped, a single type opening once (restored, in front, focused, found anywhere), what
 *  cannot open, a closed window's place remembered, the Window menu's items (UIWindowTypesTests.c). */
void UIWindowTypesTests_Run(TestRun* run);

/** @brief Easing, keyframe validation and evaluation of typed values (UIAnimationTests.c). */
void UIAnimationTests_Run(TestRun* run);

/** @brief The UI screen: ids, deferred destruction, focus, navigation, input routing, drawing order, animations and
 *  notifications (UIScreenTests.c). */
void UIScreenTests_Run(TestRun* run);

/** @brief What each scale policy answers: the monitor's quarters, the window's height, the player's scale on top, the limits and
 *  broken numbers (UIScalePolicyTests.c). */
void UIScalePolicyTests_Run(TestRun* run);

/** @brief What is remembered about the window: the state file's text and file, hostile values, the fit to the monitors, the watch
 *  that follows a window (GameWindowStateTests.c). */
void GameWindowStateTests_Run(TestRun* run);

/** @brief The game window with no real window: its mode and size at start, the toggle's keys and their consumption, saving
 *  (GameWindowTests.c). */
void GameWindowTests_Run(TestRun* run);

/** @brief The full-window UI's host: the viewport and the scale following a sample that changes the window and the monitor
 *  (UIWindowHostTests.c). */
void UIWindowHostTests_Run(TestRun* run);

/** Input routing: a held press past the surface, the mouse arguments, a release over another widget, a lost press, handing a press on,
 *  moving a widget, the wheel up the tree, layers, the hidden-cursor drag, hover keys, cursor shapes (UIRoutingTests.c). */
void UIRoutingTests_Run(TestRun* run);

/** @brief Entering elements: the key rule for an edit element outside and inside, entering by Enter, a press or code, leaving
 *  by Escape, Enter, Tab and focus moving, a held Tab carrying editing on, kept keys, hooks, the entered state shown
 *  (UIEnteringTests.c). */
void UIEnteringTests_Run(TestRun* run);

/** @brief UI pixels: placements, the scale, preferred sizes and their cache, stacks and their children's hints, labels in UI
 *  pixels (UILayoutTests.c). */
void UILayoutTests_Run(TestRun* run);

/** @brief The render transform: its arithmetic against raymath, the renderer's transform stack and clips, hit tests and coordinates
 *  through turned and nested widgets, a drag along a turned slider, what rendering pushes and pops, the hit test hook, the typed
 *  properties (UITransformTests.c). */
void UITransformTests_Run(TestRun* run);

/** @brief Per-element scale: the design's table of size against scale, nesting, a fixed child in a scaled parent, the standard looks
 *  scaled whole, wrapped labels settling under a scale, hostile scales, an animated scale, whole pixels at odd scales
 *  (UIScaleTests.c). */
void UIScaleTests_Run(TestRun* run);

/** @brief Looks: the look an element gets (its options, its theme, the standard one), an unknown name, two looks of a kind on one
 *  screen, looks of every composed kind, cues and their cost while nobody listens, icons as content (UILookTests.c). */
void UILookTests_Run(TestRun* run);

/** @brief Hover text: its delay, a press and moving off, falling back up the tree, a widget gone, a provider, keeping on the
 *  surface, a screen with nothing to show it with (UITooltipTests.c). */
void UITooltipTests_Run(TestRun* run);

/** @brief The scrollable piece's arithmetic, the scroll bar's thumb and paging, the scroll area: its content at its offset, bars by mode,
 *  measuring at the view's width, the wheel up the tree at an end, Shift, focus scrolled into view (nested too), page keys, an idle area
 *  measuring nothing (UIScrollTests.c). */
void UIScrollTests_Run(TestRun* run);

/** @brief The text box: entering and leaving with what each gives, the keys, undo by the keys, the clipboard, the mouse, the short list, the
 *  context menu, read-only and masked boxes, style ranges, component trees, scrolling, events (UITextBoxTests.c). */
void UITextBoxTests_Run(TestRun* run);

/** @brief The popup layer: the placement's arithmetic, opening above everything and following the anchor, closing by a press outside (going
 *  on to the widget behind), by Escape, by the owner going away, nested popups from the inside out, modal popups and their backdrop, an
 *  owner that counts as inside, navigation kept in the topmost popup, refusals (UIPopupTests.c). */
void UIPopupTests_Run(TestRun* run);

/** @brief The choice piece, the row recycler's visible range, and the dropdown: opening below or above by the room or forced, a row chosen by
 *  a click or a press-drag-release, a long list's strips, bar and wheel, keys passing the head and working in the list at once, a letter, a
 *  provider's 100 000 options costing the rows in view, Ctrl with the wheel, mixed, disabled (UIDropdownTests.c). */
void UIDropdownTests_Run(TestRun* run);

/** @brief Menus: items, separators, disabled and checked items, keys, submenus by resting, Right and a click, provided submenus, a handler
 *  destroying the owner, checks; context requests walking up to a provider (right click, the menu key, none found); the menu bar (opening,
 *  switching, press-drag-release, Left and Right); the status bar's message and hover text (UIMenuTests.c). */
void UIMenuTests_Run(TestRun* run);

/** @brief The ui_theme asset type: every key, each refusal with its message, state fallbacks, variants, hostile sizes and
 *  counts, a screen's theme swap and teardown; box styles; the nine-slice's quads (UIThemeTests.c). */
void UIThemeTests_Run(TestRun* run);

/** @brief The kit and its elements: the panel, the label, the button, the toggle and the slider, their input, states,
 *  events and refusals; a second widget type behind a capability (UIElementTests.c). */
void UIElementTests_Run(TestRun* run);

/** @brief The UI test frame run headless: how its menu shares input with the world below (UITestFrameTests.c). */
void UITestFrameTests_Run(TestRun* run);

/** @brief The frame manager's PrepareRender slot: its order and which frames it skips
 *  (GameFrameManagerTests.c). */
void GameFrameManagerTests_Run(TestRun* run);

/** @brief Terrain meshes: the position grid, the chunks (each triangle by its centre, exact positions relative to a
 *  centre, bounds), smooth normals over the whole mesh, holes, face materials, the limits and their messages, active
 *  edges over the whole terrain, the setters and the rebuild of only what they touch, and the binary form (bit for bit
 *  round trips, hostile documents) (TerrainMeshTests.c). */
void TerrainMeshTests_Run(TestRun* run);

/** @brief The terrain's glTF import: positions through the node's transform (mirrors turn the winding), welding and
 *  averaged seams, colour attributes of every kind (bytes, shorts, floats; scalars, RGB, RGBA) into weights and
 *  densities, material slots into face flags, imported normals keeping hard edges, hostile files refused by the shared
 *  checks, and an import written and read back bit for bit (TerrainGltfTests.c). */
void TerrainGltfTests_Run(TestRun* run);

/** @brief The terrain asset and component: definitions (the defaults, every key, each rule refused with its
 *  message), loading the .glb and the binary form through the asset manager (counts checked against the definition,
 *  broken files named), the component's override flags next to their values, the settings they resolve (the mask
 *  falling back to the layer's default) and a terrain object in a world file (TerrainAssetTests.c). */
void TerrainAssetTests_Run(TestRun* run);

/** @brief Terrain materials: definitions read through their property table (the defaults, every key, each rule
 *  refused with its message), the maps packed as the texture arrays take them (the height in the albedo's alpha), sizes
 *  checked (the height the albedo's, the limit), missing and code-built textures naming the slot, a texture's pixels
 *  read as RGBA from any image, and the height sampled bilinearly with repeat (TerrainMaterialTests.c). */
void TerrainMaterialTests_Run(TestRun* run);

/** @brief The terrain's material blend in C (the terrain shader's mirror): weights from their bytes and normalized
 *  (none: the first material), rule ranges and their ramps, the slope, the value noise against an independent
 *  implementation (periodic, deterministic), wrapping, rule strengths, rules in order with only_on, height blending's
 *  shares (transitions, ties, a single material, monotonic), the dominant material, triplanar weights and each side's
 *  coordinates, texture coordinates and noise standing still 100 km out, a point of a terrain (painted, ruled, face
 *  material) and the shaders' constants (TerrainBlendTests.c). */
void TerrainBlendTests_Run(TestRun* run);

/** @brief Terrain in the collision world: rays and casts hit each chunk where it is, naming the chunk and the terrain's
 *  triangle, and pass through holes and from below; seams between chunks do not catch (a ball's contacts, a walk
 *  compared with one on a box); the characters' slope limit; only changed chunks rebuilt; a collider wins over a
 *  terrain on one object; problems warned once; run again 173 km out (WorldTerrainCollisionTests.c). */
void WorldTerrainCollisionTests_Run(TestRun* run);

/** @brief Paths as pure maths: every pair of node modes (against independent centripetal Catmull-Rom and de Casteljau
 *  evaluations), arc length, closing and wrapping, sections, samples by spacing and curvature, the nearest point against a
 *  dense walk, upright and rotation-minimizing frames, values without overshoot, degenerate and hostile input, regions
 *  (PathCurveTests.c). */
void PathCurveTests_Run(TestRun* run);

/** @brief The path component: the curve follows every change, by setter or table; a reader finds the nearest path above
 *  it and watches it; points and regions in the world, in double; queries by region; ground projection (terrain only, chosen
 *  layers, a surface it starts inside, a tunnel, what collides); world files in both formats; run again 173 km out
 *  (WorldPathTests.c). */
void WorldPathTests_Run(TestRun* run);

/** @brief Where a path object's pieces lie, as pure maths: the fits and alignments against their closed forms on a line, chords and
 *  arcs on a circle, caps, separators, joints, skipped sections, closed runs, too-short spans, picks, limits, fuzz
 *  (PathObjectLayoutTests.c). */
void PathObjectLayoutTests_Run(TestRun* run);

/** @brief The path object's asset and its pure maths: the tables, what is in effect (defaults, asset, placement), the definition's
 *  checks, measuring a mesh, rigid copies (their ends, the slope's rules, turned pieces, offsets, jitter), bending (surface kept,
 *  following slices, what reaches past them, cuts, indexed meshes, limits), a copy's shear (PathObjectTests.c). */
void PathObjectTests_Run(TestRun* run);

/** @brief The path objects system: copies and generated models on runtime children, reading a path above them, live rebuilds that
 *  write only what changed, placements' overrides, world files, the ground and its changes, the slots' collision, the render
 *  cache's entries; run again 173 km out (WorldPathObjectTests.c). */
void WorldPathObjectTests_Run(TestRun* run);
/** @brief A prefab file's rules, each refused clearly, what it declares, values by their property's type, its lists' limits, any byte of
 *  a file changed (PrefabTests.c). */
void PrefabTests_Run(TestRun* run);
/** @brief A prefab's copies: objects under the root, ids from the root and the name, values, enums undoing each other, overrides, stale
 *  names, nesting, pass-through, cycles, budgets, world files, rebuilds, selection; run again 173 km out (WorldPrefabTests.c). */
void WorldPrefabTests_Run(TestRun* run);
