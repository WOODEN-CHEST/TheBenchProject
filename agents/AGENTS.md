This is for AI agents:
The project uses the exact same development tech stack and project guidelines as the main library it uses.
The library is WRFramework. In the same directory as this file ("agents" directory under root directory)
exists WRFramework.md.
That file is a copy-paste of the AGENTS.md file from the WRFramework library.
You MUST read that file before doing anything further.

This project follows the same style guide and tech stack as that framework, so use that file as reference.
You **must** look at that library's functions when deciding to implement a feature to see if it already has
some library functions to help implement it, the library has been added here so you wouldn't need to duplicate
code for each feature.

If you require to write code which requires platform-specific code AND the library doesn't provide a cross-platform way
of doing that, stop what you're doing and let that be know. This project should be cross-platform to windows and linux,
and all the cross-platform code should be in the WRFramework library. So if you require such absent functions,
let it be known so they can be added to WRFramework. If you are told to continue anyway, make sure that the code
you write works for all targeted platforms (same platforms as WRFramework).

When writing code, keep it modular and clean, do not cram everything into main.c.
You are a senior developer.

Try to keep memory allocation count to a minimum by reusing buffers or passing reusable buffer pools
throughout the application, this is a game after all. Performance matters.

The references/ directory contains various reference files for various things. It contains:
* ghdf.txt - The full specification of the binary file format GHDF.
* unicode_data.txt The unicode data loaded into the game at runtime.
* asset_structure.md - A file which describes the JSON asset structure that assets must have.
* text_component_structure.md - The JSON structure of text components.
* controls_structure.md - The JSON structure of the player's controls file (action bindings).
* world_structure.md - The structure of world files: JSON (authored worlds) and GHDF (saves, the network), their
  loading rules and what the writer writes.

BUILDING.md (repository root) records the exact raylib and WRFramework versions and the required raylib
configuration: raylib is built with SUPPORT_CUSTOM_FRAME_CONTROL, so EndDrawing neither swaps buffers nor polls
input (the game does both itself). Read it before touching frame timing, input polling or buffer swapping.

For storing BINARY game data, use the GHDF module, for human-readable game data, JSON module.

When you implement a new module, create an md file in the agents directory describing the module and
its usage, then in this file, in the modules list, add the module, the md file reference and a short
description of what the module is for.

The docs in agents/ (this file too), references/ and BUILDING.md, and every comment in the code (source/, include/,
tests/, the shaders, the asset scripts) describe the project as it is now, not how it got there: what a module or a
piece of code is, how to use it, its rules and pitfalls, and why, where a choice is not obvious. Leave the history
out: no dates of decisions or changes, no task, decision or plan ids (T15, G41, B02, fix plan phases), no "the owner
decided ...", no "since T27", "as before", "used to" or "the old ...", and no paths into handoff/ (it is gitignored, so
such references work on one machine only). Log messages follow the same rule. When a change makes a doc or a comment
wrong, rewrite the text it affects to describe the new state instead of appending to it; a test that guards against a
past bug says what it guards, not when the bug was found. What stays: the reasons behind choices (as reasons, not as
a decision record), rules (as rules), intended behaviour that looks like a bug, open questions for the owner, and
features that are not built yet (named, not numbered). The history itself lives in git and in handoff/.

Current module list:
* Config (Config.md) - Loads the game's JSON config file (window/render settings) into a plain, expandable GameConfig struct.
* Logger (Logger.md) - Writes timestamped, level-tagged messages to <base>/logs/latest.log and stdout, and rotates the previous log into <base>/logs/archived/ (keeping the newest 30).
* AssetManager (AssetManager.md) - Loads, shares and unloads assets built from JSON definitions in prioritized search
  roots; hands out borrowed pointers held per AssetUserID (presence-based, unloaded at once on the last release),
  caches failed loads, registers the fifteen standard types, and reads a texture asset's image file as CPU pixels
  without loading it (AssetManager_LoadTexturePixels). Read its lifetime rules before touching assets.
* Game asset wrappers (GameAssets.md) - GameFont, GameModel, GameShader, GameSound and GameTexture: the manager-owned
  wrappers for the standard font, model, shader, sound and texture types, with their ownership rules, accessors and
  pitfalls; a skinned model carries its GameSkeleton (Animation.md). GameModel.h also holds GameMaterialFactors
  (source/GameModel.c): the PBR factors a material override sets,
  shared by the model type's material_overrides and world model variants (one parser, one apply). A glTF model's
  occlusion, roughness and metallic maps, which raylib's importer leaves apart, are packed into the one map the PBR
  shader reads (the private source/ModelOrmMap.{h,c}, pure: tested headless); its first material is slot 1. GameFont (source/GameFont.c) adds sorted glyph lookup with the '?' fallback; fonts load at a
  definition-chosen size and codepoint set (source/FontCodepoints.c, private) and warn once about glyphs their file
  lacks. Shader stages may `#include "common/..."` shared GLSL from shaders/common/, expanded with #line directives so
  compiler errors name the right file (source/ShaderSource.c, private); a shader definition's `defines` are
  written in after the #version line, so one source serves a shader and its variants (the instanced twins). GameTexture is a standalone texture (sky
  panoramas, the moon, noise, masks) with filtering, wrap, optional mipmaps and an sRGB/linear flag.
* World models (WorldModels.md) - The world_model asset type (a standard type, JSON in world_models/): the reusable
  definition of a placeable thing, kept apart from its placements. WorldModel (include/WorldModel.h, source/WorldModel.c)
  holds its LOD models and their switch distances (drawn by distance with dithered cross-fades), its draw
  distance and whether it is fixed against the quality
  setting, the default render settings of its placements, its material variants, its bounds, its
  collision sets and its animation libraries, graph, sockets and first-person block (the sockets in
  source/WorldModelSocketDefinition.c); its LOD models, its variants' textures, libraries, graph and the models its
  hulls and meshes are built from load as its dependencies. The definition (source/WorldModelDefinition.c) refuses unknown keys and
  checks every rule when it is read. Variants apply to the renderer's per-draw material copies, never to an asset. The
  model component names a world model and overrides its settings per placement with explicit flags.
* World collision (WorldCollision.md) - Collision for the 3D world: shapes, layers, the collider component
  and the collision world with its step (moving and pushing). Covers:
  * CollisionShape - Collision shapes as plain values (sphere, capsule, box, cylinder, convex hull, plane bounded or
    infinite, triangle mesh) placed by a centre and a rotation, named sets of them (compounds), convex hulls (quickhull,
    merged coplanar faces, reduced to a vertex budget) and meshes (welded, active edges, a BVH), built from points or
    from a model's vertices.
  * CollisionLayer - The collision layers (world, prop, character, vehicle, dynamic, trigger, interaction), their
    default masks and the symmetric blocking rule.
  * The narrowphase (private, source/CollisionGeometry.h and source/Collision*.c) - Support functions under any affine
    transform (uneven scales exact: an ellipsoid), GJK distances, EPA penetrations, contact manifolds of up to 4 points
    with feature ids, rays against every shape, linear casts (exact time of impact) and rotating casts (conservative
    advancement: no contact is ever skipped).
  * WorldColliderComponent - Component type 3: which of its world model's collision sets (or one primitive of its own),
    the layer, mask, trigger flag and mass (override flags over the world model's defaults), a local offset, rotation
    and scale, a rotation mode (full, yaw only, none), and how it moves (pushable or kinematic, what a blocked
    mover does, swept or not, persisting contact reports).
  * WorldCollision - The collision world: every enabled collider in world space, synced once a tick (rebuilt or moved
    only when something changed; static and movable dynamic AABB trees, infinite planes in a list), queries with layer,
    trigger and ignore filters (rays, casts with rotation, overlaps, contact manifolds with local points and triangle
    data, a point's openness to the sky), trigger enter/exit events with hysteresis, counts and a debug list the renderer draws as wireframes. Its
    step (WorldCollision_Step, the tick's collision step; source/WorldCollisionStep.c, the solver in
    source/WorldCollisionSolver.c, contacts and events in source/WorldCollisionEvents.c) sweeps every collider that
    moved (nothing tunnels: speculative contacts, then exact casts of every pushed motion), pushes everything that
    overlaps apart at once (projected Gauss-Seidel shared by mass; pushables slide, kinematic movers push, static
    colliders hold), stops or pushes through blocked movers, writes the pushes into the objects, and reports contacts
    (with relative velocities, pushes and ages), begin/persist/end contact events, blocked and crushed events and
    each object's motion result (push, contacts, ground); teleports skip the sweep and pushables at rest sleep.
  The world model's `collision` section (sets and their defaults, or "bounds") is parsed and loaded by
  source/WorldModelCollisionDefinition.c (World models).
* Characters (Characters.md) - The player and NPCs: what makes an object walk, run, crouch, jump, fly, look
  around and use things, the same for the player and every NPC, whether a human, a dog or a pigeon. Covers:
  * CharacterAsset - The character asset type (a standard type, JSON in characters/): the defaults of a kind of
    character (CharacterSettings: an upright capsule, eye height, mass, walking, turning, crouching, jumping, falling,
    flying, reach), described by a property table whose rows the component shares (the private
    source/CharacterInternal.h); the definition (source/CharacterDefinition.c) refuses unknown keys and checks the
    heights against the radius.
  * WorldCharacterComponent - Component type 4, the first transform driver: a character asset by name, every setting
    overridable per placement with explicit flags, the CharacterInput its driver fills (move direction, run, crouch,
    jump, fly, use, look yaw and pitch, how the body faces) and the state it shows (mode, stance, ground, velocity,
    facing, eyes, crouch amount, time in the air).
  * WorldCharacters - The character system: once a tick, after the other drivers, it moves every character (modes and
    stances, platforms, velocity, move and slide, steps, ground following, facing, eyes), keeps each one's capsule
    collider in step, queues events (jumped, landed, left the ground, crouched, stood up, took off, stopped flying, used)
    and answers what a character looks at; its capsule motion is the private source/WorldCharacterMotion.c
    (source/WorldCharacterInternal.h).
  * PlayerController - The player's side: GameInput actions and the mouse into a character's input, the camera at its
    eyes (its body's eyes socket as posed), what the player looks at.
* Animation (Animation.md) - Skeletal animation: skinned models posed per placed object, clips from separate
  libraries retargeted onto every model of a skeleton layout, cross-fades, sockets, and skinning in every render pass;
  animation graphs (states, transitions, layers, look-at), special actions with anchors for the player and
  NPCs alike, held items on sockets, first person on the body's eyes, and posing once per shown frame. Covers:
  * AnimationLibrary - The animation_library asset type (a standard type, JSON in animation_libraries/, a .glb or .gltf
    file): bones and clips read by our own glTF reader (cgltf, the file treated as hostile;
    source/AnimationLibraryGltf.c), clip settings (name, source, loop, speed) from the definition
    (source/AnimationLibraryDefinition.c), sampling as glTF says (STEP, LINEAR, CUBICSPLINE).
  * GameSkeleton - A skinned model's skeleton, built when the model loads: evaluation order, bind and inverse bind
    matrices, rest poses, which meshes the GPU skins, each bone's vertex box.
  * SkeletalAnimation - The runtime maths: binding a library to a skeleton by bone name (rotations from the clip,
    translations scaled by the height ratio, unmatched ancestors folded), blending, the mixer (8 clips, cross-fades,
    weights, loops), bone matrices, the skinning palette, the pose's bounds; transforms taken apart by columns.
  * AnimationGraph - The animation_graph asset type (a standard type, JSON in animation_graphs/): parameters,
    events, layers of states and transitions, actions, look-at, read from a hostile definition into one immutable block
    of indices (source/AnimationGraphReader.c; the asset type in source/AnimationGraphDefinition.c).
  * AnimationGraphPlayer - Playing a graph on one skeleton, pure functions over caller memory: binding (clips,
    masks, look bones), the instance (parameters, events, per-layer cross-fades), the tick's advance (transitions,
    synced blends, matched speeds, timed events) and the pose (layers by mask, look-at).
  * WorldAnimatorComponent - Component type 5 (`animator`): the clip row with phase, speed, pause, fade, a loop override,
    the placement's own libraries and graph, and the pose the system publishes for the renderer and sockets
    (with the sockets' frames and the eyes socket).
  * WorldAnimation - The animation system: once a tick, after the collision step's events, it binds every animator to
    its world model's skeleton, libraries and graph, applies its rows, runs its graph and action and advances its
    clips; once per shown frame (WorldAnimation_Pose, from the frame's PrepareRender) it poses them and publishes their
    sockets; code plays clips, sets parameters and starts actions through it, and its events queue for the game; a
    bone's world matrix in double. Graphs are source/WorldAnimationGraph.c, actions (anchors, attach, detach)
    source/WorldAnimationActions.c, its per-animator state the private source/WorldAnimationInternal.h.
* Terrain (Terrain.md) - Terrain of any shape: one free-form triangle mesh with painted
  material weights and densities, holes and face materials, imported from a Blender .glb, cut into 32 m chunks that
  draw in every pass and collide as triangle meshes, dressed in up to sixteen terrain materials blended
  per pixel (painted weights, face materials, rules by slope, height and noise, height blending, triplanar mapping)
  through texture arrays. Covers:
  * TerrainMesh - The mesh as pure data with no GL (include/TerrainMesh.h): the 1/1024 m position grid, the limits,
    smooth or imported normals, the chunks (each triangle in the cell of its centre; positions relative to an exact
    centre), active edges over the whole terrain, the editor's setters and the rebuild of only the chunks they touch
    (source/TerrainMesh.c, source/TerrainMeshChunks.c), the binary GHDF form (source/TerrainMeshFile.c), the glTF
    import (source/TerrainMeshGltf.c: node transforms in double, welding, colour attributes into weights and
    densities, material slots into holes and face materials), and a chunk's surface in parts and its collision mesh
    (source/TerrainMeshCollision.c); private source/TerrainMeshInternal.h.
  * GltfChecks - The structure checks every glTF reader runs before cgltf trusts a number (private,
    source/GltfChecks.h): buffers, views, accessors, sparse data, alignment, and buffer files beside the file, shared
    by the animation library's reader and the terrain's.
  * TerrainAsset - The terrain asset type (a standard type, JSON in terrains/; include/TerrainAsset.h,
    source/TerrainAsset.c, the definition in source/TerrainDefinition.c): the mesh from a .glb (source model) or its
    binary form (source data), the palette's names, the density channels' names, the chunk size, and its placements'
    default render and collision settings; unknown keys refused everywhere.
  * WorldTerrainComponent - Component type 6 (`terrain`): a terrain asset by name and the settings this placement
    overrides (explicit flags next to its own values: outline, casts shadows, shadow distance, fixed shadow distance,
    draw distance, fixed draw distance, collision enabled, layer, mask; draw and shadow distance multipliers), with the
    pure function that resolves the settings in effect. The collision world makes each chunk a one-sided mesh shape (WorldCollision.md, "Terrain");
    the renderer draws the chunks in every pass (source/WorldRendererTerrain.c, WorldRuntime.md).
  * TerrainMaterial - The terrain_material asset type (a standard type, JSON in terrain_materials/;
    include/TerrainMaterial.h, source/TerrainMaterial.c, the definition in source/TerrainMaterialDefinition.c): one
    palette material's settings (read through a property table: tile size, tint, roughness, normal strength, height
    blend strength, triplanar mode and slope, surface key, puddles, porosity) and its maps' pixels, read from texture
    assets' image files and packed as the texture arrays take them (albedo with height, normal, roughness with AO); a
    terrain loads its palette's materials as its dependencies.
  * TerrainBlend - The terrain's material blend in C (include/TerrainBlend.h): the terrain shader's steps mirrored
    (weights, rules with their periodic value noise, height blending, triplanar projections) and the material at a point
    of a terrain (a triangle and barycentrics: footsteps, the scattering's limits).
  * GLTextureArray - Texture arrays (private, source/GLTextureArray.{h,c}): direct OpenGL calls (the
    project's only ones with GLReadback, GLDepth, GLSliceMap and GLShadowArray), through the function pointers raylib's rlGetProcAddress resolves
    (no raylib rebuild), with their GL constants; used by the renderer's terrain pass. Anything else that needs GL raylib
    lacks belongs in a module like it, never in a pass (BUILDING.md, "Direct OpenGL calls").
  * GLReadback - Reading a small target back without waiting for the GPU (private, source/GLReadback.{h,c}): a
    pixel-pack buffer and a fence per copy, three in flight, the newest finished copy handed
    over a frame or two later; direct OpenGL calls in GLTextureArray's manner. Used by the eye's meter
    (WorldRendererExposure.c).
  * GLDepth - Reversed depth (private, source/GLDepth.{h,c}): whether the GPU has clip
    control (OpenGL 4.5 or GL_ARB_clip_control), 32-bit float depth textures and renderbuffers, and the switch of GL's
    depth state to reversed (0-to-1 clip depth, GL_GEQUAL, cleared to 0) and back to raylib's; direct OpenGL calls in
    GLTextureArray's manner. Used by the renderer's camera passes (WorldRendererCommon.c's camera depth helpers), so
    the camera draws at any distance; without clip control the renderer keeps raylib's projection and its 4 km.
  * GLSliceMap - Slice maps (private, source/GLSliceMap.{h,c}): RGBA32UI textures with their mipmap levels, their
    integer clear, the logic operations (XOR, OR) that draw them and the per-level reads that build their pyramid;
    direct OpenGL calls in GLTextureArray's manner. Used by the sky's occupancy volume (WorldRendererSkyVisibility.c,
    interior lighting); without them the sky's visibility stays open.
  * GLShadowArray - The sun's and the moon's shadow maps (private, source/GLShadowArray.{h,c}): one 2D array texture of
    32-bit float depth, a layer per shadow level, with the hardware's depth comparison and linear filtering, a
    framebuffer without colour that each layer is attached to in turn, and depth clamping around a level's draw; direct
    OpenGL calls in GLTextureArray's manner. Used by the shadow pass (WorldRendererShadows.c); without it the sun and
    the moon cast no shadows.
* World sprites (WorldSprites.md) - The world_sprite asset type (a standard type, JSON in world_sprites/): how a sprite
  animation stands, is lit and is seen in the world, kept apart from its placements. WorldSprite (include/WorldSprite.h,
  source/WorldSprite.c) holds the animation it plays (loaded as its dependency) and the defaults of its placements
  (WorldSpriteSettings: orientation, pivot, pixels per unit, flips, transparency, lighting, decal, render flags, draw
  distance or its kind's, fixed), described by two property tables whose rows the sprite component's table shares
  (the private source/WorldSpriteInternal.h): one copy of every key, default and rule. The definition
  (source/WorldSpriteDefinition.c) reads them through those tables and refuses unknown keys. The sprite component names
  a world sprite and may override every setting per placement with explicit flags.
* SpriteSheet (SpriteSheet.md) - Texture atlas with named pixel regions; the asset loader shelf-packs padded source
  images into one texture (padding at most 64 px, atlas at most 8192 px).
* SpriteAnimation (SpriteAnimation.md) - 2D frame animations: SpriteAnimation (shared asset data: frames plus default
  playback) and SpriteAnimationInstance (per-consumer playback state, Update, end/loop events; borrows its source and
  must never be moved or copied by value).
* Renderer (Renderer.md) - RenderContext, the low-level aspect-fitted 2D drawing device (pixel/relative/fitted units,
  RenderColor, textures, text, primitives, model/mesh passthrough) used by the UI and text renderers; not the world
  renderer.
* ProgramTime (ProgramTime.md) - Tiny value type carrying a tick's total time and passed (delta) time, in seconds.
* Input (GameInput.md) - How the game reads input. Covers two modules:
  * GameKey - The game-wide key identity (keyboard keys and mouse buttons, room for gamepad buttons) with stable id
    strings for binding files and display names; the raylib codes stay private to the input layer.
  * GameInput - The per-tick input snapshot (keys, mouse, wheel, typed text drained once), polled by the frame manager
    right after PollInputEvents, with consumption (a consumed key reads as not pressed while held) and the action layer
    (GameAction: named actions bound to keys with modifiers; JSON controls file load/save).
* GameFrame (GameFrame.md) - Abstract base for a game "scene"/section (menu, level, loading screen), with stepped load/unload, logic lifecycle, per-target rendering and an optional PrepareRender slot: the update side's last step before a render (posing skeletons once per shown frame).
* GameFrameManager (GameFrameManager.md) - Owns a z-stack of game frames, drives their lifecycle and update loop (polls the input, then updates frames top-down so the top frame gets input first), runs their PrepareRender before each render, and composites them to the screen; crashes gracefully on frame errors.
* NamespacedKey (NamespacedKey.md) - Namespaced keys (`namespace:path`, Minecraft's character set, at most 128 bytes,
  a default namespace for keys written without one) and the KeyRegistry that interns them to uint32 ids (one per
  program: main creates it, Services.Keys carries it, every World borrows it), with stable texts and prefix search for
  autocomplete.
* Object data (ObjectData.md) - Tags and typed custom data on world objects, and queries over them. Covers:
  * DataValue - One typed value (tag, bool, int, float, string, key, object, vector3, color, date_time, list,
    compound) for object data, global variables, selectors, the editor and saves: the storable part of the future
    scripting VM's values. Deep copy, equality, release, depth (16) and size limits, validation, and its JSON and GHDF
    forms (source/DataValueJSON.c, source/DataValueGHDF.c, private source/DataValueInternal.h).
  * DataMap - Key id -> DataValue (a tag is an entry with no value): one object's tags and data, later the global
    variables. Byte accounting, owner-given limits, JSON (tags and values as two keys) and GHDF.
  * World data (source/WorldData.c) - The world's data functions (World_SetData and the tag sugar), the object and
    world limits, the data revision, and the tag index queries use.
  * WorldQuery - Searches over a world's objects by tags, data comparisons, components, flags, distance and box, ordered
    by id or distance, with a limit: the engine side of selectors.
* World data layer (WorldData.md) - The rendering-independent 3D world model: one object type made of components, in a
  hierarchy. Covers nine modules (plus the private source/WorldInternal.h they share):
  * WorldObject - The one object type: id, name, parent, transform (relative to the parent), tint, enabled, visible,
    mobility (static/movable), a revision, its tags and data (a DataMap, with a data revision), and one slot per
    component type; created by the World only; validating setters and a property table (the parent row is ReadOnly:
    World_SetParent changes it). Its world placement (a cached world matrix, read without writing) and its effective
    flags come through its parents.
  * WorldComponent - The component base (type, owner, revision, and a serial no other component of the world ever had),
    the component types, and the registry entry that describes each (name, size, table, transform-driver flag,
    construct/deconstruct/attach/detach/property-set hooks). At most one component of each type per object, at most one
    transform driver.
  * WorldModelComponent - A world model by asset name (WorldModels.md), the variant drawn, and the settings this
    placement overrides: each an explicit flag (overrides_<key>) next to the placement's own value (outline, crisp, shadow
    tier, casts shadows, haze, keeps the sky out, draw distance, fixed draw distance, LOD switch distances), plus a draw
    distance multiplier, a LOD
    distance multiplier and a forced LOD; pure functions resolve the settings and the LOD settings in effect.
  * WorldModelInstancesComponent - Component type 7 (`model_instances`): many copies of one world model, each a
    double position, a rotation, a scale and a tint (an InstanceList, at most 262 144), with the model component's
    settings shared row for row through the private source/WorldModelComponentInternal.h (all but omit_pixelation:
    copies are never crisp) and an instances revision; pure functions resolve the settings and the LOD settings in
    effect. The renderer draws the copies a cell at a time, instanced, in every pass (WorldRuntime.md, "Model
    instances").
  * WorldSpriteComponent - A world sprite by asset name (WorldSprites.md) with its animation's playback instance, bound
    under the world's asset user on attach and whenever the name changes (also through the table), and every setting
    this placement overrides: each an explicit flag (overrides_<key>) next to the placement's own value (orientation,
    pivot, pixels per unit, flips, alpha mode and cutoff, lit, emission, light wrap, casts shadows, decal and its lift,
    outline, crisp, draw distance, fixed draw distance), plus a draw distance multiplier; a pure function resolves the
    settings in effect (the draw distance from the placement, the world sprite or the kind).
  * WorldLightComponent - A point light (colour, intensity, range, casts shadows).
  * WorldAmbientZoneComponent - Component type 8 (`ambient_zone`): a box or a sphere around its object that sets a
    room's own light (its colour and intensity, the sky's share inside, the eye's exposure bias, its priority where
    zones overlap, its air's haze), fading in from its surface (WorldData.md; the renderer's side in WorldRuntime.md,
    "Interior lighting").
  * WorldColliderComponent - How the object collides (World collision, WorldCollision.md).
  * WorldTerrainComponent - Where a terrain lies and how it draws and collides (Terrain, Terrain.md).
  * WorldEnvironment - Per-world sky/sun/moon/night-sky/haze/effect settings, described by its property table
    (defaults, validation, copy, JSON and GHDF all come from it; see PropertyDescriptors.md; it owns its sky texture
    names). Its date and time of day are what the sky shows: the game clock writes them every tick, the environment
    never advances time itself. Pure functions turn them into the sky: the sun (optionally following the seasons), the
    moon (phase from the calendar, position, lit fraction, moonlight above the air), the stars' daily turn and yearly
    drift, the two lights above the air (WorldEnvironment_GetSkyLights) and which of them casts the shadows from the
    light each gives through the air (WorldEnvironment_ChooseShadowLight).
  * World - Owns the objects and their components, mints the uint64 ids (an id map for lookups), keeps a dense array
    of components per type for systems to iterate, holds the environment, borrows the key registry (the keys of its
    objects' tags and data) and keeps their tag index; with an asset manager (World_Construct2) it owns an asset user
    its components load under; object-set and component-set versions. The hierarchy (source/WorldHierarchy.c):
    World_SetParent (no cycles, at most 64 levels, optionally keeping the world placement), children in attach order,
    cached world matrices marked dirty down a subtree and brought up to date by World_UpdateTransforms once a tick,
    effective enabled/visible flags, and subtree removal (children first). Saved and loaded by WorldFile.
  * WorldTransform - Transform maths (private, source/WorldTransform.h): compose a position, Euler XYZ rotation and
    scale in DrawModelEx's order, the one decomposition back (mirrors, zero scales, gimbal lock; the nearest answer
    for a sheared matrix), a local matrix under a parent, and a bound on a matrix's stretch for bounding spheres.
* WorldFile (WorldFile.md) - Worlds saved and loaded as JSON (authored, diffable) and GHDF (saves, the network), from
  files or memory, through the property tables: the format version, name, next id, environment, and each object's id,
  table and components (references/world_structure.md). Loading is all or nothing into caller memory and treats input
  as hostile (limits on the input and on its parsed tree, ids, parents through the world's checks, the data limit, keys
  given back on failure); unknown
  names are skipped with one warning per name; objects are written parents first and saves are atomic. The private
  source/WorldFileInternal.h holds the rules both formats share.
* World runtime layer (WorldRuntime.md) - Camera, renderer, services bundle and bootstrap for viewing a world:
  * GameCamera - 3D camera (a double position, fov/yaw/pitch/roll; +Y up, +Z forward), converted at render time to
    raylib's render camera, which sits at the origin and only turns (render space).
  * Services - Bundle of borrowed shared services (logger, config, unicode, asset manager, frame manager, input, GHDF
    pool, key registry).
  * WorldRenderer - Renders a World through a GameCamera (PBR lit by the sun and the moon, HDR, sky with the moon,
    stars, Milky Way and light pollution, the eye's night vision, sun and moon shadows, AO, outlines, bloom, sun
    shafts, crisp overlay, lit sprites with decals, sorted transparency and shadows that face the light; frustum
    culling, draw distances, and dithered fades and opacity; LODs with dithered cross-fades; skinned models posed by
    their animators in every pass; terrain chunks in every pass with their own GPU meshes; model instances drawn a cell
    at a time with instanced twins of every geometry pass, each copy fading and changing LOD on its own
    (source/WorldRendererInstances.c); the atmosphere's lookup tables (source/WorldRendererAtmosphere.c):
    transmittance, multiple scattering and the sky view, built first in the frame, which the sky and the stars read;
    the haze (source/WorldRendererAtmosphere.c and shaders/common/haze.glsl): the aerial-perspective volume built
    from them every frame, the far ground below the horizon, the ground haze and light pollution's light in the air,
    which every lit surface, the sky, the stars and the outlines take; one light scale for the sky and the surfaces:
    the sun and the moon through the air at each surface's height and the sky's own light as the ambient, from the
    sky's irradiance table and the volume's last row; the eye adapting to the picture (source/WorldRendererExposure.c):
    a meter pass over the HDR picture read back a frame or two later; interior lighting
    (source/WorldRendererSkyVisibility.c, source/WorldRendererInterior.c and shaders/common/interior.glsl): a top-down
    map of what keeps the sky out around the camera, which every lit surface reads for its share of the sky's light,
    ambient zones that light rooms, and the eye adapting to the light at the camera indoors; the camera's passes in
    reversed 32-bit float
    depth with no far plane where the GPU has clip control (GLDepth), so whatever has the draw distance draws at any
    distance), in render space: the world's orientation with the camera at the origin, every position relative to the
    camera (subtracted in double), shadow levels snapped to their texels; separate from the world data and never
    writes to it. Split by render pass into source/WorldRenderer*.c behind the private source/WorldRendererInternal.h;
    read "Renderer structure" in WorldRuntime.md before adding to it. WorldRenderer_GetStatus reports which features
    run.
  * WorldRenderCache - The renderer's private per-component cache (source/WorldRenderCache.h): one walk over the
    world's model/sprite/light/terrain/model instances/ambient zone component arrays per frame builds the lists (every shown
    terrain's chunks too, and every shown model instances component's cells, built again only when its copies or its
    world model change) with the objects' world placements (in double,
    and in render space for the frame's origin: the one place a world position becomes the floats the GPU sees;
    effectively disabled and hidden objects not drawn); world models are resolved once per component and again only
    when the name changes, variants to an index, and the settings in effect (models' and sprites') every frame; each
    model's LOD state is kept across frames and updated once per frame (WorldRenderCache_UpdateLods).
  * WorldAtmosphere - The atmosphere as pure maths (private, source/WorldAtmosphere.h): the air's coefficients from
    the environment (the air, the haze, the ozone layer), when the tables must be built again (the stale check and the
    tables' build record), the height the sky is seen from, heights and distances along rays without the cancellation of a float radius, and the lookup tables' layout
    (transmittance, multiple scattering, sky view, the sky's irradiance table and the sky's light by height in the
    volume's last row): the C mirror of shaders/common/atmosphere.glsl's mappings.
  * WorldSkyLight - The sky's light on the world's surfaces as pure maths (private, source/WorldSkyLight.h): the exact transmittance toward a light with its disc's share above the horizon, the C mirror's own
    transmittance and multiple scattering tables and the GPU's irradiance table built from them by the GPU's quadrature
    (whole, or in steps over frames: WorldSkyLightBuilder keeps two sets and swaps in a rebuild when the air changes),
    the sky's and light pollution's irradiance, the frame's light at the camera (the sun's and the moon's through the
    air, the sky's irradiance, the ground's bounce, the ambient's tint, the night's own light; every value finite), the
    moon's gate (while the moonlit sky shows), and the scene luminance on that scale with its daylight's share. The
    renderer takes the camera's light from it for the eye's anchor and real scale, night vision, the shadow light, the
    status line and the fallback uniforms; the tests check the GPU's quadrature through it (WorldRuntime.md, "Sky's
    light on surfaces").
  * WorldRenderSpace - Render space as pure maths (private, source/WorldRenderSpace.h): the frame's origin (the
    camera's double position) with its rotation-only view and projection (also the reversed infinite projection and
    its closed-form inverse), the C mirror of the sky shader's ray, and the
    shadow levels' boxes with their centres snapped to their texel grids in double, each grid kept from frame to frame
    (anchored to the last frame's box), and a kept level's box built again from its stored centre and light.
    WorldRuntime.md's "Render space" has the rule every pass and shader follows.
  * WorldShadowLevels - The sun's and the moon's shadow levels as pure maths (private, source/WorldShadowLevels.h): the
    nested boxes around the camera (their sizes and texels from the player's quality settings, the levels the world's
    reach needs, their depth range), which level a point reads and the blend over its band (the C mirror of
    shaders/common/lighting.glsl's choice), the reach's fade, what a level draws (the size cut, the LOD of its inner edge,
    its ring) and which levels render each frame (the staggered periods, the turn and move triggers) with the light
    frame each kept level is read with (WorldRuntime.md, "Sun and moon shadows").
  * WorldInstancing - Instanced drawing as pure maths (private, source/WorldInstancing.h): a component's copies
    sorted into 32 m cells of its object's space with their 56-byte records (each matrix relative to its cell's centre,
    composed in double, rounded once), the copies' LOD bands around the switch distances, their fades, which pass draws
    which LOD of a copy and how it dithers, and the LODs a cell can show: the C mirror of shaders/common/instancing.glsl
    (tests, the one-at-a-time A/B, the status's copies by LOD).
  * WorldLod - Levels of detail as pure maths (private, source/WorldLod.h): the LOD a distance picks (the zoom, the
    config's lod_distance_scale, the placement's switch distances, hysteresis, a forced LOD) and the cross-fade's state
    across frames (progress over the world model's fade time, turning back without a pop, a third LOD from the larger
    share, the two draws with complementary dithers, the LOD the G-buffer and the shadows draw solid).
  * WorldVisibility - What each pass draws this frame (private, source/WorldVisibility.h): frustum planes and sphere
    tests (the camera's frustum, without a far plane for reversed depth; each shadow level's box, size cut, reach and
    ring), draw distances (each model's and sprite's own, times the
    config's draw_distance_scale unless fixed, against its distance times the camera's zoom factor) and shadow
    distances (scaled by the config's shadow_distance_scale unless fixed, never past the draw distance) with their
    fade band, the opacity's fade, the dither's C mirror (shaders/common/dither.glsl: the draw's fade and the LOD
    cross-fade's complementary masks), and
    the per-pass lists of cache entries with their fades, built every frame from the render cache; shadow-only entries
    are in the shadow lists alone.
  * SpriteTransform - A sprite quad's pure maths (private, source/SpriteTransform.h): its matrix for a viewer (the
    camera or a light; billboard, axis billboard, fixed), its world size from pixels per unit, its bounds and the
    back-to-front order of the blended sprites; the sprite pass and the shadow passes draw with it.
  * NightSky - The night sky's pure maths (private, source/NightSky.h): the seeded star catalogue, the limiting
    magnitude (the naked eye's threshold, Schaefer's, against the sky's luminance: the twilight's from the
    C mirror's sky light on the real scale, light pollution's and the moonlit sky's as the background that gives their
    tuned limits), the air mass, star colours and the galaxy's place among the stars; used by the sky and star passes
    (WorldRendererSky.c, WorldRendererStars.c).
  * NightVision - The eye's dim-light vision as pure maths (private, source/NightVision.h): Hunt's rod share calibrated
    on the game's light scale (the one scale's anchors, and the field luminance from the daylight on the real
    scale and the night's raised lights through the power law) and the rods' blue-grey; the sky and PBR shaders apply it
    (shaders/common/night_vision).
  * EyeAdaptation - The eye's adaptation to the picture as pure maths (private, source/EyeAdaptation.h): the meter's centre-weighted log-average of its blocks, the anchor and the window around it (the sky's
    floor at the night's own light), the eye eased in stops, the partial adaptation's curve over night vision's real
    light scale that gives the exposure, and a room's bias on it; the renderer's meter pass and readback feed it
    (WorldRuntime.md, "Eye adaptation").
  * WorldWeatherLight - The weather's look as pure maths (private, source/WorldWeatherLight.h): the clouds as one even
    deck (their cover and what it leaves of the sun and the moon, their two-stream transmission, the deck's light on
    surfaces, in the air and on the sky as an overcast sky shows it), a lightning flash's light and glow, the wetness and
    the wind, from the environment's weather values each frame (WorldRuntime.md, "Weather").
  * WorldInterior - Interior lighting as pure maths (private, source/WorldInterior.h): ambient zones in render space
    (placed by their objects' world matrices), their weights (the fade from the surface, surfaces facing in), the at
    most four a draw takes and their composition by priority, the haze inside them, the sky's visibility map on its
    texel grid, a surface's taps and their reference heights, the visibility and the sky's share, and the camera's
    interior (its openness and zones, eased) with the light the eye is in: the C mirror of
    shaders/common/interior.glsl (WorldRuntime.md, "Interior lighting").
  * WorldTestFrame - Concrete GameFrame: WASD + mouse-look flycam over a test world whose sky follows the frame's game
    clock and whose colliders it steps every tick (trigger, contact and motion events logged; F7 shows the colliders,
    F8 the contact points, F6 swings the door, F9 sends a wall through a row of crates, F10 moves the door's crate),
    with debug keys and a text sample (T toggles it) drawn with TextDocument + TextLayout over the world. Its
    characters move every tick: F1 walk mode (the player as a character), F2 test NPCs (a human, a dog, a pigeon), F3
    flight, F4 the player's kind (the private source/WorldTestCharacters.{h,c}). It ticks the animation system: 5 steps
    the showcase figure through the clips, 6 a crowd of 100 walking figures, 7 slow motion (the private
    source/WorldTestAnimation.{h,c}); it poses them in its PrepareRender, the humans wear the animated humanoid (the
    player in first person), E starts the action the object looked at offers (the bench's sit, the coffee cup's
    pick-up, the door's push), 8 sends the NPC human to the bench, 9 puts down, 0 waves. Its world has a terrain around
    the slab in four placeholder materials (the minus key shows the terrain's chunk key, the equals key cycles its
    views: shaded, painted weights, rule weights, the dominant material) and a grove of 400 instanced trees; the
    apostrophe shows a field of 20 000 instanced pebbles (a runtime object) and the semicolon switches instanced
    drawing for the A/B (the private source/WorldTestInstances.{h,c}). The keypad's 1 switches the atmosphere's tables
    (the A/B against the direct march), 2 the multiple scattering and 3 the ozone, and I adds the frame times; the
    keypad's 4 to 8 drive the haze (its debug view and slice, the distance scale, a morning mist, the haze strength),
    the keypad's 9 switches interior lighting (the A/B) and 0 steps its debug view (the zones, the sky's share). Its
    world has a test room with an ambient zone, and its PrepareRender measures how open the camera is to the sky. The
    keypad's + shows a far field of towers out to 3 km on a 12 km plain (a runtime object) and * the shadow levels'
    debug view (the private source/WorldTestShadows.{h,c}). It runs the weather on its clock (the private
    source/WorldTestWeather.{h,c}): Insert the next preset, Home the lock, End the speed, Page Up a strike, Page Down the
    mode, the backslash a forecast.
* PropertyDescriptor (PropertyDescriptors.md) - Property tables: one static table per data type describes each field
  once (key, GHDF wire id, type, default, rules, flags, editor text), and generic functions driven by it do defaults,
  validation (errors name the table, key and rule), deep copy, equality, JSON and GHDF read (all or nothing, unknown
  keys by mode, migration hook) and write (defaults left out by default, integers in GHDF's smallest type), and get/set
  by key. The one module allowed to write `_` fields by offset. Clients: WorldEnvironment, GameClock, the world objects
  and their components, the world sprite's settings, the character asset's settings; new data types get a table, not
  hand-written codecs. A FloatList
  type (PropertyFloatList: an owned list of floats with a maximum count, per-value bounds and an optional rising rule)
  carries lists such as a placement's LOD distances; an EnumSet type (a uint32 mask of an enum's entries, JSON an
  array of names) carries a collider's layer mask; an AssetNameList type (PropertyAssetNameList: an owned list of
  asset names, JSON an array of strings) carries an animator's own animation libraries; an InstanceList type
  (PropertyInstanceList: an owned list of copies, each a double position, a rotation, a scale and a tint; JSON an array
  of objects with the defaults left out, GHDF parallel arrays) carries a model instances component's copies. A setting a placement overrides
  is two rows, a Bool
  `overrides_<key>` and the value `<key>` (the table check enforces the pairing). A DataMap property (tags and typed
  data) is written as two JSON keys.
* GameCalendar (GameCalendar.md) - The game's calendar: pure functions over day numbers (days since 0001-01-01, an
  int64) and dates of the proleptic Gregorian calendar, years 1 to 9999: conversions, weekday (Monday = 0), day of the
  year, leap years, month lengths, validation, English names, the "yyyy-mm-dd" text, and date-times (int64
  microseconds, "yyyy-mm-ddThh:mm:ss[.ffffff]"). Every calendar rule lives here, so a custom calendar would replace
  only this module.
* GameClock (GameClock.md) - The game's one clock: a day number plus double seconds of the day (compensated
  summation, no drift), a time scale (default 72, a 20-minute day), pause, a largest step per tick, fast-forwards and
  validated jumps, reports of what each move passed (game seconds, midnights, hours) for the weather and the scheduler,
  and a property table so it saves. Everything that needs game time reads it.
* Weather (WeatherSystem.md) - The weather as continuous values (cloud coverage, type and density, rain, wetness, the
  wind and its gusts, a ground fog, the temperature, a lightning flash) in the world's environment, moved over game time
  by the climate the world names. Covers:
  * Climate - The climate asset type (a standard type, JSON in climates/; source/ClimateDefinition.c the type): presets
    (targets as numbers or ranges drawn per visit, durations, weighted links, month weights, start hours, transitions,
    their own rates, gusts, lightning) and the settings (mode, seed, first preset, speed, rates, wetting and drying, the
    clouds' ranges for rain and storms, the forecast's accuracy, the fixed state), read from a hostile definition into
    one block of plain data; WeatherQuantity (the values presets target) and WeatherMode.
  * WeatherSystem - Once a tick it moves the environment's weather values by the clock's game seconds (presets chosen by
    weight, month and hour, values toward their targets at their rates, rain only from dense clouds, wetness rising and
    drying) and by the real seconds of play (smooth gusts, lightning strikes with their flash and return strokes, a queue
    of strikes for thunder), in four modes (simulated, fixed, scripted, off), with the story's API (presets, values,
    lock, seed, speed, climate, a strike), a forecast that runs a copy ahead (blurred by the climate's accuracy, stable
    across calls) and a property table that saves it.
* World light culling (WorldLightCulling.md) - Reach-culls a list of point lights to the strongest few (up to WORLD_MAX_FORWARD_LIGHTS) overlapping an object's bounding sphere, ranked by influence, for the renderer's per-object forward shading.
* UI framework (UIFramework.md) - The in-game UI, on any surface (the window or a texture in the world: the host passes
  the pointer, the keyboard and the viewport). A screen holds a tree of widgets with slot + generation ids, routes the
  tick's GameInput (the focused widget gets every key first, consumed input does not reach the game), drives keyboard
  navigation through actions, runs typed property animations, queues notifications to the end of its update, lays
  widgets out in update and draws them with nested clipping. Widget_Destroy is deferred and takes the whole subtree.
  Covers these modules (UIScreen is split by job behind the private source/UIInternal.h):
  * UIInput - The argument structs handed to a widget's input hooks (keys and buttons are GameKeys).
  * UIAnimation - Typed property values (UIPropertyValue), easing methods, keyframes, keyframe validation and
    evaluation. Pure data + math.
  * UIRenderContext - The widget-local drawing device: wraps the renderer's RenderContext with the widget's absolute box + accumulated tint, and maps widget-local [0;1] draws onto the screen.
  * UIWidgetFactory - Registers capabilities and widget types (a descriptor: struct size, vtable, constructor,
    capabilities, typed properties, focusability), owns per-type pools, creates widgets (base first, id reserved,
    constructor, then alive: a failed constructor leaves nothing) and counts live widgets. One per program.
  * UIWidget - The abstract widget base (vtable incl. Layout and keyboard first refusal, geometry/flags/state, subwidgets
    with a separate draw order, capabilities, typed properties, deferred destroy); concrete widgets embed it first.
  * UIScreen - The context: id registry, roots, focus, input routing, keyboard navigation + outlines, animations,
    notifications (subscriptions; queued, delivered at the end of UIScreen_Update), layout pass and rendering.
  * UITestFrame - A debug game frame over the world test that exercises the UI and the input sharing.
  * LabelWidget - The first concrete widget: displays a (possibly multi-line) rich text with per-line alignment and bound handling (cut/wrap/resize/wrap-then-resize/wrap-then-cut). Embeds a TextDocument and a TextLayout (TextDocument.md), lays out in Update, draws in Render. Auto-fits its widget box to the rendered text.
  Also adds 2D rectangle/outline/line primitives to the Renderer module.
* Text components (TextComponents.md) - Renderer-independent, Minecraft-style rich text as a STRICT TREE: a parent owns
  its children, a tree has at most 64 levels, components own copies of their strings, and fonts and sprite animations
  are named, never held. Every change gives the component and its ancestors a new revision. Covers three modules (plus
  the private source/TextComponentInternal.h):
  * TextComponent - The base type, the four concrete types (String, Translate (a key plus a fallback, styled like a
    string), Sprite, Empty), tree operations (add/insert a root, detach, destroy) and validating property setters.
  * TextComponentFactory - Pooled create/clone/return of components and their strings and child lists; number,
    codepoint and special-character shorthands.
  * TextStyle - Optional, presence-guarded style properties applied to a component by type.
* Text documents and layout (TextDocument.md) - Showing a component tree. Covers two modules:
  * TextDocument - The single owner of one rich text: the tree, its own asset user, the name->asset bindings (each
    failing name logged once and remembered), one playback instance per sprite-animation name (TextDocument_Update
    advances them), the default font name and the translation hook (no string tables yet).
  * TextLayout - Measures and arranges a document once (lines, wrap at spaces, shrink to width, alignment,
    bottom-aligned runs, tabs) into cached glyph and sprite runs; TextLayout_Refresh rebuilds only when the document,
    its tree or the options changed; TextLayout_Draw draws the runs (shadows first) through RenderContext_RenderGlyph.
* GameJSON (GameJSON.md) - Shared parsers and writers for the game's common JSON value shapes (numbers, booleans,
  strings, colors, render colors, vectors); the asset-definition helpers, the text component codec and the property
  tables all use it so the value conventions live in one place. Raises ErrorCode_InvalidJSON for every bad value.
* GameGHDF (GameGHDF.md) - The GHDF counterpart: integers written in the smallest GHDF type that holds them and read
  from any integer type whose value fits (arrays refused), and strings without NULs inside. Used by the property
  tables, the data values and the world files.
* GameMath (GameMath.md) - The bridge between WRFramework's double-precision maths (WRVectorMath: Vector3Double,
  Matrix4Double, BoundingBoxDouble, for large worlds) and raylib's Vector3 and Matrix: conversions both
  ways, field by field (raylib declares Matrix's fields row by row), and relative to an origin (subtracted in double,
  rounded once), which is how a far-away position becomes the floats raylib and the GPU take. Header-only. GameMath.md
  also sums up WRVectorMath's conventions (raymath's, in double) and its precision far from the origin.
* Text component serialization (TextComponentSerialization.md) - Text component trees to and from the game's three
  formats. JSON (references/text_component_structure.md) and GHDF share ONE codec (source/TextComponentCodec.c,
  private) that lists every type's fields once, validates every value, names the key and the component's path in
  errors (InvalidJSON / Deserialize) and limits nesting to 64 levels. Structured forms convert to/from the format's
  value tree, not bytes. Covers three modules:
  * TextComponentText - Component -> plain UTF-8 string (no deserializer).
  * TextComponentJSON - Component <-> WRJSON value tree (writes shorthands and only non-default keys).
  * TextComponentGHDF - Component <-> GHDF compound tree (explicit type wire ids).
* SoundEngine (SoundEngine.md) - Custom audio mixer (track tree, effects, scheduled commands) on raylib's audio
  stream. DISABLED: AudioEngine_Construct1 always fails until its threading redesign; SoundEngine.md lists the
  defects and is the brief for that redesign (ask the owner its open decisions before starting).

Logging:
Use the Logger module for all program output. Do NOT print to the standard streams directly (no
printf / fprintf(stdout|stderr, ...) or other direct stdout/stderr writes) in game code. Route
messages through a Logger (passed by pointer) at the appropriate level instead. The only allowed
exception is the bootstrap fallback in main when the logger itself fails to initialize and therefore
cannot be used.

Project rules:
1. Asset lifetime: whatever keeps asset data owns its own AssetUserID, and nothing stores asset pointers or by-value
   copies (GameFont, Model, Texture2D, sprite animation sources) past that user. See AssetManager.md. Text follows it
   through TextDocument (text components only name their assets).
2. Never hold a Widget* or text component pointer across a call that can run hooks, events or user callbacks;
   re-resolve it by id afterwards (UIScreen_TryGetWidgetById). Destroying widgets from hooks and notification handlers
   is safe: Widget_Destroy is deferred to the end of the screen's update.
3. Render reads state, it never writes it: no mutating world objects, widgets or shared assets from render code.
   Per-object render data belongs in a renderer-side cache (the WorldRenderer's is WorldRenderCache), and meshes draw
   with per-draw Material copies, never by writing shaders or tints into a model asset's materials.
4. Switches over project enums that should handle every value have no `default:`. -Wswitch (on through -Wall, an
   error under -Werror) then fails the build wherever a newly added value is not handled. Handle corrupted values
   after the switch with an explicit error or neutral return, not unreachable().
5. Every recursive walk over data that came from a file or the network has a depth limit.
6. Never log per tick from the update loop, and never log from a non-main thread (the Logger is not thread-safe).
7. A frame returns an error only when an invariant is broken: that error shuts the game down. A recoverable failure
   (missing asset, bad keyframe) changes state so it is not retried every tick (mark it failed, stop the animation,
   fall back) and is logged once, when it happens.
8. Mark every intentionally incomplete piece with `// TODO:`.
9. Read input only through GameInput (Services.Input) and only in Update; never call raylib input functions
   (IsKeyPressed, GetMouseDelta, GetCharPressed, ...) above the input layer. Ask for actions rather than keys for
   anything a player could rebind, and respect consumption: frames are updated top-down and the UI reads first.

Testing and tooling:
* Unlike what WRFramework.md says, this project has a test step. `sh tests/run_linux.sh` builds and runs the headless
  tests in tests/ (add `--sanitize` for AddressSanitizer + UndefinedBehaviorSanitizer). Add or extend tests for any
  logic that does not need a window or GL context, and run them before finishing a feature. tests/TestFramework.h
  explains how to write a suite. Tests are tooling, not game code, so they may print to stdout.
* `compile/compile_linux_debug.sh` then `compile/run_linux_x11_debug.sh` build and run the game with the same
  sanitizers. Use it after changes to lifetime-sensitive code (assets, UI, text components, audio).
* The build uses `-Wconversion -Wshadow` on top of `-Wall -Wextra -Wpedantic -Werror`; keep the code clean under them.
* Game files (config, assets, logs, the asset cache) are resolved against the executable's directory, never the
  working directory: use `Services.BaseDirectory`.

If you cannot compile because a WRFramework or other library header file exists, but there is no such defined function
in the library file, then this is an error on my part and should be flagged.
The headers are correct, the function missing from the library is just a version mismatch on my end.

You should not edit the WRFramework.md file, it is regularly pulled from the actual framework repository when
changes are made to it, so making changes to this file is useless as they will be overwritten. If more info
is needed, make changes to either a module file or this file.

**IMPORTANT: I try to architecture the project myself as much as I can, but the reality is that using Claude to
write all code leads to me not fully understanding the codebase. If when I request a feature you see a roadblock or
potential required refactor, please let me know before proceeding and say what is needed, you can even ask questions so you
can plan out what the refactor should be (but don't instantly perform it, I need to know first and decide myself).
The codebase should be kept clean, so try to avoid band-aid patches. Refactors are fine and expected, just don't pile on already
broken code.**


This is pretty much all that I can write here, read WRFramework.md for the rest.