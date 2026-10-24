# Terrain

The ground of a world (B01): **one free-form triangle mesh** (vertices anywhere, overhangs and caves allowed) with
painted material weights and vegetation densities per vertex, and holes and hard per-face materials per triangle. It
is a `terrain` asset that a world's terrain object names, imported from a Blender `.glb`, cut into chunks of about 32 m
that draw in every render pass and collide as triangle meshes. In B01 the weights show as flat debug colours; B02
blends real materials from them.

The owner's decisions behind it (`handoff/2026-10_world_building/DECISIONS.md`): **W1**, a free vertex mesh rather
than height-grid tiles (terrain editing will be mesh editing; LOD only if measured, B04); **W5**, the mesh lives in a
**`terrain` asset** in a resource pack's `terrains/`, so stories set in one town share it and a story's pack overrides
it; **W6**, it is **authored in Blender and imported** (the in-game editor will write the asset's binary form later).
W7 (texture arrays for the materials) is B02's.

| Module | Files | What it is |
|---|---|---|
| TerrainMesh | `include/TerrainMesh.h`; `source/TerrainMesh.c` (checks, the grid, vertex groups, normals, active edges, the setters), `source/TerrainMeshChunks.c` (the chunks and their rebuilds), `source/TerrainMeshFile.c` (the binary form), `source/TerrainMeshGltf.c` (the glTF import), `source/TerrainMeshCollision.c` (a chunk's surface in parts and its collision mesh); private `source/TerrainMeshInternal.h` | The mesh as pure data with no GL: it loads headless. |
| GltfChecks (private) | `source/GltfChecks.{h,c}` | The structure checks every glTF reader runs before cgltf trusts a number, the buffer-file callbacks and the budget of what cgltf may allocate; shared by the animation library's reader and the terrain's (moved out of `source/AnimationLibraryGltf.c` in B01; the budget came with B01's review). |
| TerrainAsset | `include/TerrainAsset.h`, `source/TerrainAsset.c`, `source/TerrainDefinition.c` (the asset type) | The `terrain` asset: the mesh, the palette's names, the density channels' names, and the default render and collision settings of its placements. |
| WorldTerrainComponent | `include/WorldTerrainComponent.h`, `source/WorldTerrainComponent.c` | Component type 6 (`terrain`): a terrain by asset name and the settings this placement overrides. |
| The collision world's terrains | `source/WorldCollision.c` (and its query, step and event files) | One instance per terrain object, one mesh shape per chunk ([WorldCollision.md](WorldCollision.md), "Terrain"). |
| The renderer's terrain | `source/WorldRendererTerrain.c`; the render cache's terrain entries and chunk lists (`source/WorldRenderCache.{h,c}`); the visibility's chunk lists (`source/WorldVisibility.{h,c}`); `shaders/terrain/` (`world_terrain`) | The chunks' GPU meshes, the lit terrain shader and every pass ([WorldRuntime.md](WorldRuntime.md)). |
| Test content | `asset_source/scripts/placeholder_terrain.py`, `compile/out/asset/terrains/test_world/`, object 51 of the test world | The placeholder terrain around the test world's slab. |

The formats: `references/asset_structure.md` ("Terrains": every key, the import's rules and the Blender 5 export
steps) and `references/world_structure.md` ("The terrain block").

## The mesh (`TerrainMesh.h`)

**The data**, in the terrain's own space (metres, +Y up):
- per vertex: a position, a normal, 16 weight bytes (one per palette material; a palette holds 1 to 16, the bytes past
  its size are 0) and 8 density bytes (one per density channel, 0 to 8 of them);
- per triangle: three vertex indices (counter-clockwise seen from its front), its flags (`TERRAIN_TRIANGLE_FLAG_HOLE`:
  not drawn, no collision; `TERRAIN_TRIANGLE_FLAG_OVERRIDES_MATERIAL` with a palette index: the face takes that material
  outright, a hard edge along its sides) and its active edges (below).

**Positions lie on a grid** of 1/1024 m (`TERRAIN_POSITION_QUANTUM`, a little under a millimetre), within 8192 m of the
origin on each axis (`TERRAIN_MESH_EXTENT`): every position is rounded onto it when it comes in. A chunk keeps its
positions relative to its centre, a point on the same grid, and on that grid the float subtraction is exact (the
grid needs at most 23 bits there): a chunk's position plus its centre, added in double, is the terrain's position bit
for bit, so chunks meet exactly and stay exact far from the world's origin.

**Vertex groups.** Vertices at one position are one vertex for the normals and the edges (a hard edge from Blender is
vertices at one position with different normals; they still join their neighbours).

**Normals** are smooth (`TerrainNormalMode_Smooth`: area-weighted over every triangle around a group, holes left out,
over the whole mesh, so both sides of a chunk border agree) or imported (`TerrainNormalMode_Imported`: the file's, never
recomputed).

**Chunks.** A grid of square cells of the chunk size (8 to 256 m, 32 by default) in the terrain's X and Z; each triangle
goes to the cell of its centre, so an overhang and the ground under it share a column. Each non-empty cell is a chunk
(`TerrainChunk`), in row order (Z, then X; `TerrainMesh_TryFindChunk` finds one by its cell, `TerrainMesh_GetCell` gives
a point's cell). A chunk keeps:
- its triangles (holes too) and its cell;
- its centre (`TerrainChunk_GetCenter`, a `Vector3Double`: the middle of the box around its triangles, on the grid);
- its **surface**, what the renderer and the collision world build from: its own vertex list (a vertex shared across a
  border is duplicated in each chunk; a face material's vertices are split from the painted ones, so a renderer can
  give it a hard edge), each surface vertex's terrain vertex (`TerrainChunk_GetVertexSources`) and its material
  (`TerrainChunk_GetVertexMaterials`: `TERRAIN_CHUNK_BLEND_MATERIAL` for the painted weights, or the face material),
  positions relative to the centre, the drawn triangles (holes left out) as indices into that list with each one's
  terrain triangle (`TerrainChunk_GetSurfaceTriangles`), and its box and bounding sphere.

`TerrainMesh_SplitChunkSurface` cuts a chunk's surface into parts of at most a given number of vertices, the
surface's triangles in order (each part's own vertex list and indices): the renderer and the collision world both cut
at 65536, since raylib's and the collision mesh's indices are unsigned shorts.

**Active edges.** Each triangle's three edges are marked as a collision mesh marks its own
(`CollisionMesh_IsSharedEdgeActive`): an edge shared with one neighbour across a flat or concave fold is inactive, so a
shape sliding over it takes the face's normal and never catches. They are computed over the **whole terrain**, so an
edge on a chunk's border keeps the activity its neighbour across the border gives it: characters do not bump at seams.
Borders of the terrain, holes and non-manifold edges stay active. `TerrainMesh_CreateChunkCollisionMesh` passes them
(and each triangle's terrain index) into the chunk's collision mesh.

**Changes, for the editor.** `TerrainMesh_SetVertexPositions`, `_SetVertexWeights`, `_SetVertexDensities` and
`_SetTriangleFlags` change the data (every value checked first: all or nothing) and mark what it touches: a moved vertex
or a hole changes the triangles around its ring (their normals and edges), new weights, densities or a face material
only the chunks that draw them. `TerrainMesh_RebuildDirtyChunks` recomputes those normals and edges and rebuilds those
chunks. **Revisions** tell the consumers what to rebuild:
- each chunk's (`TerrainChunk_GetRevision`, from 1) changes when it is rebuilt;
- the mesh's (`TerrainMesh_GetRevision`) changes whenever any chunk was (built or rebuilt), so a consumer looks at the
  chunks only then;
- the layout's (`TerrainMesh_GetLayoutRevision`) changes when the chunks are cut again. A triangle stays in the chunk it
  was cut into; the editor's topology edits (split, collapse, delete, fill) will cut the chunks again, with a new layout
  revision.

The renderer and the collision world watch them ("Rendering", "Collision" below). Not thread-safe; main thread only.

**The binary form** (`TerrainMesh_WriteGHDF` / `_ReadGHDF`, the files `_SaveGHDF` / `_LoadGHDF`): one GHDF document, a
root compound of the counts (format version 1, vertices, triangles, palette size, channels, normal mode) and one array
per kind of data (positions; normals only when imported; weights and densities at their counts per vertex; indices,
flags and face materials per triangle); the chunks, groups, smooth normals and active edges are computed again when it
is read, bit for bit as before. Saves are atomic (a `.tmp` beside the file, then `FileSystem_ReplaceEntry`); reads are
of hostile files: at most 192 MiB, the parsed tree at most 256 MiB, arrays of exactly their type and length, then every
limit of the mesh.

**The glTF import** (`TerrainMesh_CreateFromGltf`, `_CreateFromGltfFile`; `TerrainGltfOptions`): the shared structure
checks (`GltfChecks`; cgltf's own allocations within a budget, 64 MiB beyond the file's size for its parse), then two
passes over the default scene's nodes (an explicit stack, at most 256 levels): the first counts and checks every
primitive (triangles only, no Draco, float positions, the slot, every mapped attribute of a supported shape, and every
accessor it reads stored in the file: one without a buffer view would be zeros of any count, memory the file never paid
for), the second reads. Each position goes through its node's world transform in double (glTF's T x R x S, the parents'
after) and is rounded once onto the grid; normals go through the inverse transpose, and a mirroring node turns its
triangles' winding back. A primitive's material is its slot: the blend slot (`blend` by default, or no material) is the
painted weights, `hole` makes holes, a palette material's name makes face materials, anything else is refused (so no
palette material may be named `hole` or as the blend slot). The weights and densities come from the mapped attributes
(floats clamped to 0..1, or normalized bytes or shorts; scalar, RGB or RGBA). Then vertices at one grid position (with
imported normals, at one normal too) are welded: their weights and densities are averaged where they differ (a seam in
the file: Blender splits a vertex wherever a face corner's attribute differs), triangles left with two corners at one
position are dropped, each logged once with counts. The weld finds every input vertex's welded vertex first, then sizes
its sums by the welded count (refused past the limit before they are allocated). `TerrainMesh_Create` then checks the
mesh's own limits and cuts the chunks.

**Limits** (every file is hostile; each refusal names what and where): at most 2^21 vertices and 2^22 triangles
(`TERRAIN_MESH_MAX_VERTICES`, `_MAX_TRIANGLES`; the import takes at most four times the vertices before welding),
filling at most 16 384 chunks (`TERRAIN_MESH_MAX_CHUNKS`: each costs the renderer GPU meshes and the collision world a
shape; 128 x 128 cells, 4 km square at the default 32 m, the whole extent at 128 m), finite positions inside the extent,
no edge longer than the extent, indices in range and distinct within a triangle, known flags, face materials and weights
inside the palette, densities inside the channels, a `.glb` of at most 256 MiB (`TERRAIN_GLTF_MAX_FILE_BYTES`).

## The asset (`TerrainAsset.h`, the `terrain` type)

The thirteenth standard asset type ([AssetManager.md](AssetManager.md)): JSON definitions in `terrains/`, loaded with
`AssetManager_LoadTerrain`. `source` picks the mesh's origin explicitly: `model` (a `.glb` imported when it loads, with
the `weights` and `densities` mappings, `normals` and `blend_slot`) or `data` (the binary form). `materials` (1 to 16
names) is the palette, `density_channels` (up to 8) the channels, `chunk_size` the cells, and `render` and `collision`
the defaults of its placements (`TerrainSettings`: outline, shadow tier, casts shadows, a 4 km draw distance kept fixed
against the quality setting; colliding on `world` with the layer's default mask unless the definition gives one).
Unknown keys are refused everywhere and every rule is checked when the definitions are read; the file when it loads. The
asset owns its mesh and its names and has no dependencies (B02 makes the palette's materials its dependencies). The
renderer and the collision world read the mesh and never write it; the editor (later) changes it through the setters.

## The component (`WorldTerrainComponent.h`, type 6)

`terrain` names the asset (none places nothing; changing the name swaps the terrain and keeps the object). Each default
of the asset a placement may change is two rows, as the model component's ([WorldModels.md](WorldModels.md)): an
override flag and the placement's own value, kept (and saved) while the flag is off; `draw_distance_multiplier`
multiplies whichever draw distance applies. `WorldTerrainComponent_ResolveSettings` gives the settings in effect (a mask
neither the asset nor the placement gives becomes the default mask of the layer in effect). The object's transform
places the terrain's space; it is static by default (moving a terrain works: the collision world moves its chunks, with
its warning for a static object that moved). Not a transform driver.

The table, `terrain_component` (version 1):

| Key | Wire | Type | Default |
|---|---|---|---|
| `terrain` | 1 | AssetName (at most 128 bytes) | none |
| `overrides_has_outline`, `has_outline` | 2, 3 | Bool, Bool | false, true |
| `overrides_shadow_tier`, `shadow_tier` | 4, 5 | Bool, Enum (`near`, `far`, `both`) | false, `both` |
| `overrides_casts_shadows`, `casts_shadows` | 6, 7 | Bool, Bool | false, true |
| `overrides_draw_distance`, `draw_distance` | 8, 9 | Bool, Float > 0 | false, 4000 |
| `draw_distance_multiplier` | 10 | Float > 0 | 1 |
| `overrides_is_draw_distance_fixed`, `is_draw_distance_fixed` | 11, 12 | Bool, Bool | false, true |
| `overrides_is_collision_enabled`, `is_collision_enabled` | 13, 14 | Bool, Bool | false, true |
| `overrides_collision_layer`, `collision_layer` | 15, 16 | Bool, Enum (the layers) | false, `world` |
| `overrides_collides_with`, `collides_with` | 17, 18 | Bool, EnumSet (the layers) | false, world's default mask |

Its next free wire id is 19.

## Collision

The collision world's second source ([WorldCollision.md](WorldCollision.md), "Terrain"): one instance per terrain
object without a collider (with both, the collider wins, warned once), one one-sided mesh shape per chunk at the chunk's
centre, its border edges active only where the terrain has an edge, hits naming the chunk (`MeshPart`) and the
terrain's triangle (`SourceTriangle`); rebuilt only for changed chunks (their revisions), all of them for a new layout,
placed again when the object moves. Characters stand and walk on it as on any mesh (T13's step and slope limits).

## Rendering

- **The cache** lists every chunk with a surface of every shown terrain each frame (`WorldRenderTerrainChunk`): the
  chunk's world matrix (the object's world matrix times the translation to the chunk's centre) and its sphere, in
  double and in render space, the settings in effect and the object's tint ([WorldRuntime.md](WorldRuntime.md),
  "Per-component render cache").
- **Visibility** puts the chunks in every pass's list as models: the camera's frustum, each cascade's box by shadow
  tier, the cube's reach, the draw distance with the zoom, and the fade.
- **GPU meshes** (`WorldRendererTerrain.c`) are the renderer's own, one record per terrain asset with one mesh per chunk,
  built from the chunk's surface in parts of at most 65536 vertices: positions (relative to the chunk's centre),
  normals (the terrain vertices'), and the 16 weights as four normalized `u8 x 4` attributes at locations 9 to 12 (the
  painted weights, or for a face material's vertices that material's weight alone, 255: one-hot, so its faces take its
  colour outright). The indices stay on the CPU beside the GPU copy: raylib's `DrawMesh` draws a mesh whose index
  array is gone as if it had none. A mesh is uploaded when its chunk is listed and its revision differs; all of a
  terrain's records are cut again when its layout changes, and the records of terrains no entry resolves any more are
  unloaded (a hidden terrain's are kept: it may show again).
- **The passes:** the lit pass draws each chunk with the `world_terrain` shader (`shaders/terrain/`): its vertex stage
  is rigid; its fragment stage mixes a debug colour per palette entry by the weights in linear light (B02 replaces them
  with the materials) and lights it as `world_pbr` lights a dielectric of roughness 0.9 (the sun and the moon with the
  cascades' shadow, the cube-shadow light, up to 8 forward lights per chunk by its sphere, the ambient with the screen's
  AO, the night vision and the fog), dithered by the chunk's fade, before the sprites (decals lie on it). Without the
  shader the chunks draw unlit and solid through raylib's default shader. The G-buffer (`world_normal`, the outline
  flag in effect), the cascades (`world_depth`, back faces: a hill's far side shades what lies behind it) and the cube
  shadow (`world_cube_depth`) draw the chunks with the shared shaders. Terrain is never crisp, never skinned, never
  instanced.
- **The chunk key** (minus in the test frame, `WorldRenderer_SetTerrainKeyEnabled`): each chunk mixes a colour by its
  cell (X and Z each modulo 3: neighbours always differ) into its lit colour, and the wireframe of the chunks in view is
  drawn over it, scaled 0.999 toward the camera so it lies on the surface.
- **The status** (`WorldRenderer_GetStatus`, the I key) counts the terrains placed and those without a terrain, the
  chunks drawn of all listed and their triangles, and the "terrain shading" feature.

## The test terrain

`asset_source/scripts/placeholder_terrain.py` (the Python standard library only) writes
`terrains/test_world/placeholder_terrain.glb` and its definition (`placeholder_terrain`), one primitive per material
slot as Blender exports them; object 51 of the test world places it at the origin. A ring around the 20 m slab out to
248 m:
- a flat apron at the slab's top (y = 0.05) for 3 m around it, so a character walks off the slab onto it, then rolling
  hills;
- a valley along Z to the east (x 60 to 120 m), its floor 6 m below the slab;
- a cliff to the north, its foot at z = -30 m between x = -18 and 6, rising 10 m over four 1 m rows whose top lip leans
  4 m out over its foot (the overhang: no heightfield could hold it), a plateau behind it falling back to the ground;
- a cave mouth in the cliff's foot (x -8 to -5) of `hole` faces, with a 6 m tunnel behind it, its faces facing in;
- a footpath east of the slab (dirt weights), and a 6 m patch south-west of it (x -30 to -24, z 14 to 20) in the
  `dirt` slot: a face material with hard edges;
- 1 m quads out to 40 m, then 2, 4 and 8 m ones; the outer edge slopes down to y = 0;
- `COLOR_0` paints grass, dirt and rock (rock on the steep, dirt on the path and the valley floor), `_DENSITY_0` the
  trees (on gentle hills).

It loads as 30 976 vertices and 61 174 triangles (6 of them holes, 72 the dirt patch) in 256 chunks; the start view
draws about 90 of them.

## Tests

- `tests/TerrainMeshTests.c`: the grid, the chunks (every triangle in one chunk, exact positions back from the centre,
  bounds), smooth normals over the whole mesh, holes, face materials, the limits and their messages, active edges over
  the whole terrain, the setters and the rebuild of only what they touch, the revisions, the parts and the collision
  meshes, and the binary form (bit-for-bit round trips, hostile documents).
- `tests/TerrainGltfTests.c`: the import (node transforms and mirrors, welding and averaged seams, every attribute
  kind, slots into flags, imported normals keeping hard edges, hostile files refused by the shared checks, the buffer
  limit, an import written and read back bit for bit); `tests/TestTerrain.{h,c}` write the test files.
- `tests/TerrainAssetTests.c`: definitions (the defaults, every key, each rule refused with its message), loading the
  `.glb` and the binary form through the asset manager, the component's pairs and their resolution, a terrain object in
  a world file, and the shipped terrain's counts.
- `tests/WorldTerrainCollisionTests.c`: [WorldCollision.md](WorldCollision.md), "Terrain"; run again 173 km out.
- The render cache's and the visibility's terrain entries in `tests/WorldRenderCacheTests.c` and
  `tests/WorldVisibilityTests.c`; the shader's stages in `tests/ShaderSourceTests.c`. The GPU side was checked with the
  windowless harness (`handoff/2026-10_graphics_and_world/glcheck/`), including 32 renderer and shader mutations.

## Known limits and what comes next

- **Debug colours only** until B02's materials (W7: texture arrays); weights need not add up to 1 (the mix divides by
  their sum).
- **No LOD** (B04, W9): every chunk draws all its triangles out to its 4 km draw distance.
- **Densities are kept, unused** until the scattering (B09).
- **One-sided collision:** a shape whose centre ends up under the surface gets no contact from it and falls through
  (as from any one-sided mesh); the tunnel's faces face in, so its walls and roof hold from inside.
- **A whole chunk is rebuilt** (GPU mesh and collision mesh) when anything in it changes: cheap for 32 m chunks, the
  editor's brushes will show whether it needs finer updates.
- **A chunk with more than 2^20 triangles** gets no collision (warned once).
- **Walking:** on slopes of about 26 degrees and more, the test human (T13's `ground_snap_distance` 0, the owner's
  "falls off every step") leaves the ground for a few ticks at a time going downhill; with a snap distance of 0.2 m it
  stays down (B01's status block).
