#pragma once
#include "TestFramework.h"


/**
 * Entry points of the test suites, one per test file, called in order by TestMain.c. To add a suite: create
 * "<Module>Tests.c" with a static TestCase array and a "<Module>Tests_Run" function that passes it to
 * TestRun_RunSuite, declare that function here, and call it from TestMain.c.
 */

/** @brief GameJSON value conversions (GameJSONTests.c). */
void GameJSONTests_Run(TestRun* run);
/** @brief WRFramework behaviours the game relies on since T09: JSON insertion order and one-line scalar arrays, GHDF
 *  strings read in chunks and refused when they declare more than the data holds, pools freeing borrowed containers,
 *  truncated documents leaving nothing behind, atomic file replacement (WRFrameworkTests.c). */
void WRFrameworkTests_Run(TestRun* run);
/** @brief WRFramework's double-precision maths (WRVectorMath, T31; 2D vectors since) and the GameMath bridge to raylib:
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

/** @brief The collision narrowphase: affine maths and poses, supports under any transform, GJK distances, EPA
 *  penetrations, contact manifolds and their feature ids, rays against every shape, linear and rotating casts (the
 *  fast thin cases that would tunnel with steps), convex hulls and their reduction, meshes with their active edges and
 *  BVH (CollisionGeometryTests.c; private modules of source/). */
void CollisionGeometryTests_Run(TestRun* run);

/** @brief Point-light reach culling (WorldLightCullingTests.c). */
void WorldLightCullingTests_Run(TestRun* run);

/** @brief Namespaced keys: the key characters, the length limit, the default namespace, interning, stable texts and
 *  prefix search (NamespacedKeyTests.c). */
void NamespacedKeyTests_Run(TestRun* run);

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

/** @brief Moving and pushing (T27): no tunnelling at any speed (a thin wall against fast spheres and boxes, a fast fan
 *  blade, two fast spheres meeting), pushing many at once (a row and a grid against a wall, stopping or pushing through,
 *  crushed reports), mass shares, sliding, blocked movers, teleports, the write-back through a parent, carried
 *  colliders, contact events and their data, motion results (the ground, a moving platform), sleeping and determinism
 *  (WorldCollisionMotionTests.c). */
void WorldCollisionMotionTests_Run(TestRun* run);

/** @brief Characters (T13): the character asset's and component's tables and overrides, the input, walking into walls,
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
 *  round trips, unknown keys, access by key, migration, and the environment's table against the old encoder
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

/** @brief The night sky's maths: the star catalogue, limiting magnitude, air mass, star colours and the galaxy's
 *  orientation (NightSkyTests.c; a private module of source/). */
void NightSkyTests_Run(TestRun* run);

/** @brief The eye's night vision: its calibration on the game's light scale, untouched daylight, rods at night, the
 *  strength, the rods' grey (NightVisionTests.c; a private module of source/). */
void NightVisionTests_Run(TestRun* run);

/** @brief The renderer's per-object render cache (WorldRenderCacheTests.c; a private module of source/). */
void WorldRenderCacheTests_Run(TestRun* run);

/** @brief A sprite quad's maths: billboards facing the camera, axis billboards, fixed sprites, the pivot and flips,
 *  lights as viewers, size and bounds, the back-to-front order (SpriteTransformTests.c; a private module of source/). */
void SpriteTransformTests_Run(TestRun* run);

/** @brief What the renderer draws: frustum planes and the cascade box, draw distances and their fade band, opacity, the
 *  dither's C mirror, and the per-pass lists built from a code-built cache (WorldVisibilityTests.c; a private module of
 *  source/). */
void WorldVisibilityTests_Run(TestRun* run);

/** @brief Render space (T32): the view a pure rotation wherever the camera is, points 50 km out within a hundredth of a
 *  scene pixel of a double-precision camera and near the origin where the world-space pipeline put them, the sky's ray
 *  exact at any distance, the shadow cascades' texels standing still while the camera moves (also for a light near
 *  straight up) and their grid turning about the camera while the light turns, near the origin and far out; each also
 *  shows the maths it replaced failing (WorldRenderSpaceTests.c; a private module of source/). */
void WorldRenderSpaceTests_Run(TestRun* run);

/** @brief Animation libraries (T29): the placeholder files' bones and clips, clip settings (names, loops, speeds),
 *  sampling (the ends held, the last key exact, step, linear, cubic splines, the shorter arc, normalized integers),
 *  node matrices, unsorted parents, skipped clips, hostile files and limits, the definition's rules and the asset
 *  loading its file (AnimationLibraryTests.c). */
void AnimationLibraryTests_Run(TestRun* run);

/** @brief Animation graphs (T30): a graph using every part of the format read back record by record (parameters and
 *  their indices, layers with masks, blends, clips, empty states, events with their effects, transitions from any state
 *  and from lists, every kind of condition, look-at, actions with anchors), the defaults, the block copied as bytes, the
 *  asset type, every rule's refusal naming its reason, and the limits (AnimationGraphTests.c). */
void AnimationGraphTests_Run(TestRun* run);

/** @brief The graph player (T30): bindings (clips with their natural speeds, masks, look bones, what is missing), the
 *  playback's start, blends in step matching the speed within their rate limits, transitions (from any state, exit
 *  times, priority, one a tick), timed events (by phase and time, at the start, once each, "set"), layers over the body
 *  by mask and weight, socket conditions, look-at's easing, limits, weight and import rotation, cross-fades, full
 *  layers and missing clips (AnimationGraphPlayerTests.c). */
void AnimationGraphPlayerTests_Run(TestRun* run);

/** @brief Skeletal animation's runtime maths (T29): transforms composed and taken apart (a non-uniform scale under a turn
 *  too), skeletons from raylib models (rest and bind poses, order, names, broken ones, meshes the GPU cannot skin, the
 *  same bones), the rest pose's palette and bounds, blends, pose bounds holding every skinned vertex, bindings by name
 *  (unmatched bones, folded ancestors, the height ratio, a shorter figure keeping its bone lengths, a clip on its own
 *  skeleton as authored) and the mixer (cross-fades keeping the weights' sum, loops and ends, replays, weights alone, a
 *  full mixer, the blended pose) (SkeletalAnimationTests.c). */
void SkeletalAnimationTests_Run(TestRun* run);

/** @brief The animation system (T29): animators bound on demand to their world models' skeletons and libraries, the
 *  clip row (phase, cross-fades, speed, pause, loop override), clips played from code over the rows, the libraries'
 *  override, rebinding, sockets in double (also 173 km out), disabled objects, one-time reports and the animator's rows
 *  (WorldAnimationTests.c). */
void WorldAnimationTests_Run(TestRun* run);

/** @brief The animation system's graphs and actions (T30): a graph from the world model following its character (its
 *  speeds, ground and events), parameters, triggers and states from code, clips from code over the graph, sockets
 *  carrying their children with the pose (and the arm layer's socket condition), actions attaching and detaching their
 *  target, an anchored action sliding the character onto a bench's seat and ended by its driver, what actions refuse,
 *  and a rebind or the system's end freeing the character; also 173 km out (WorldAnimationGraphTests.c). */
void WorldAnimationGraphTests_Run(TestRun* run);

/** @brief LODs: the selection by distance with hysteresis, the scale, the zoom and a forced LOD; the cross-fade's state
 *  (its start, progress, snapping, a fade time of 0, held frames); the two LODs' complementary dither masks
 *  (WorldLodTests.c; a private module of source/). */
void WorldLodTests_Run(TestRun* run);

/** @brief Log rotation, pruning and formatted output under a base directory (LoggerTests.c). */
void LoggerTests_Run(TestRun* run);

/** @brief The text component tree: ownership, depth limit, owned strings, revisions, clone, style (TextComponentTests.c). */
void TextComponentTests_Run(TestRun* run);

/** @brief The shared JSON/GHDF text codec: round trips, shorthands, errors, depth limit (TextComponentCodecTests.c). */
void TextComponentCodecTests_Run(TestRun* run);

/** @brief Text documents: tree ownership, font and animation bindings, fallbacks, ticking (TextDocumentTests.c). */
void TextDocumentTests_Run(TestRun* run);

/** @brief Text layout: lines, wrapping, shrinking, alignment, runs and staleness (TextLayoutTests.c). */
void TextLayoutTests_Run(TestRun* run);

/** @brief Fonts: glyph lookup, the "codepoints" key, font definitions and the shipped fonts (FontTests.c). */
void FontTests_Run(TestRun* run);

/** @brief The label widget's text, layout properties and construction (LabelWidgetTests.c). */
void LabelWidgetTests_Run(TestRun* run);

/** @brief Keys, the per-tick input with consumption, actions and controls files (GameInputTests.c). */
void GameInputTests_Run(TestRun* run);

/** @brief Easing, keyframe validation and evaluation of typed values (UIAnimationTests.c). */
void UIAnimationTests_Run(TestRun* run);

/** @brief The UI screen: ids, deferred destruction, focus, navigation, input routing, drawing order, animations and
 *  notifications (UIScreenTests.c). */
void UIScreenTests_Run(TestRun* run);

/** @brief The UI test frame run headless: how its menu shares input with the world below (UITestFrameTests.c). */
void UITestFrameTests_Run(TestRun* run);

/** @brief The frame manager's PrepareRender slot (T30, G35): its order and which frames it skips
 *  (GameFrameManagerTests.c). */
void GameFrameManagerTests_Run(TestRun* run);

/** @brief Terrain meshes (B01): the position grid, the chunks (each triangle by its centre, exact positions relative to a
 *  centre, bounds), smooth normals over the whole mesh, holes, face materials, the limits and their messages, active
 *  edges over the whole terrain, the setters and the rebuild of only what they touch, and the binary form (bit for bit
 *  round trips, hostile documents) (TerrainMeshTests.c). */
void TerrainMeshTests_Run(TestRun* run);

/** @brief The terrain's glTF import (B01): positions through the node's transform (mirrors turn the winding), welding and
 *  averaged seams, colour attributes of every kind (bytes, shorts, floats; scalars, RGB, RGBA) into weights and
 *  densities, material slots into face flags, imported normals keeping hard edges, hostile files refused by the shared
 *  checks, and an import written and read back bit for bit (TerrainGltfTests.c). */
void TerrainGltfTests_Run(TestRun* run);

/** @brief The terrain asset and component (B01): definitions (the defaults, every key, each rule refused with its
 *  message), loading the .glb and the binary form through the asset manager (counts checked against the definition,
 *  broken files named), the component's override flags next to their values, the settings they resolve (the mask
 *  falling back to the layer's default) and a terrain object in a world file (TerrainAssetTests.c). */
void TerrainAssetTests_Run(TestRun* run);

/** @brief Terrain materials (B02): definitions read through their property table (the defaults, every key, each rule
 *  refused with its message), the maps packed as the texture arrays take them (the height in the albedo's alpha), sizes
 *  checked (the height the albedo's, the limit), missing and code-built textures naming the slot, a texture's pixels
 *  read as RGBA from any image, and the height sampled bilinearly with repeat (TerrainMaterialTests.c). */
void TerrainMaterialTests_Run(TestRun* run);

/** @brief The terrain's material blend in C (B02; the terrain shader's mirror): weights from their bytes and normalized
 *  (none: the first material), rule ranges and their ramps, the slope, the value noise against an independent
 *  implementation (periodic, deterministic), wrapping, rule strengths, rules in order with only_on, height blending's
 *  shares (transitions, ties, a single material, monotonic), the dominant material, triplanar weights and each side's
 *  coordinates, texture coordinates and noise standing still 100 km out, a point of a terrain (painted, ruled, face
 *  material) and the shaders' constants (TerrainBlendTests.c). */
void TerrainBlendTests_Run(TestRun* run);

/** @brief Terrain in the collision world (B01): rays and casts hit each chunk where it is, naming the chunk and the
 *  terrain's triangle, and pass through holes and from below; seams between chunks do not catch (a ball's contacts, a
 *  walk compared with one on a box); T13's slope limit; only changed chunks rebuilt; a collider wins over a terrain on
 *  one object; problems warned once; run again 173 km out (WorldTerrainCollisionTests.c). */
void WorldTerrainCollisionTests_Run(TestRun* run);
