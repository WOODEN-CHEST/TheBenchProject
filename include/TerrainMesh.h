#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// raylib.h is needed for Vector3, the type of the mesh's positions and normals (the GPU's and the narrowphase's floats).
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "wr/WRVectorMath.h"

/** @brief The game's logger (Logger.h); the glTF import reports what it skipped through it. */
typedef struct LoggerStruct Logger;
/** @brief A collision mesh (CollisionShape.h): what TerrainMesh_CreateChunkCollisionMesh builds from a chunk. */
typedef struct CollisionMeshStruct CollisionMesh;


/**
 * @file TerrainMesh.h
 * @brief A terrain's mesh: one free-form triangle mesh (vertices anywhere, overhangs allowed) with painted material
 *        weights and vegetation densities per vertex and holes and face materials per triangle, cut into chunks
 *        of about 32 m for drawing and collision. Pure data with no GL: it loads headless.
 *
 * THE DATA. In the terrain's own space (metres, +Y up), within TERRAIN_MESH_EXTENT of its origin on every axis, where a
 * float keeps a millimetre:
 * - per vertex: a position, a normal, one weight byte per palette material (TERRAIN_MAX_MATERIALS bytes; a palette
 *   holds 1 to 16 materials, the bytes past its size are 0) and one byte per density channel (TERRAIN_MAX_DENSITY_CHANNELS
 *   bytes; 0 to 8 channels, the rest 0);
 * - per triangle: three vertex indices (counter-clockwise seen from its front), the hole flag (not drawn, no collision:
 *   cave mouths, basements) and the face material flag with its palette index (the face takes that material outright, a
 *   hard edge along its sides). Each triangle also keeps which of its edges are active for collision (below).
 *
 * POSITIONS ARE ON A GRID of TERRAIN_POSITION_QUANTUM (1/1024 m, a little under a millimetre): every position is rounded
 * to it when it comes in. A chunk keeps its positions relative to its centre (a point on the same grid), and on that grid
 * the subtraction is exact in float: a chunk's position plus its centre, added in double, is the terrain's position bit
 * for bit, so chunks meet exactly at their borders.
 *
 * NORMALS are smooth (TerrainNormalMode_Smooth: area-weighted over every triangle around a vertex, holes left out,
 * computed over the whole mesh, so both sides of a chunk border get the same normal) or imported (the file's: hard edges
 * modelled in Blender stay hard, as vertices with the same position and different normals; never recomputed).
 *
 * CHUNKS. A grid of square cells of the chunk size (8 to 256 m, 32 by default) in the terrain's XZ plane; each triangle
 * belongs to the cell of its centre, so an overhang and the ground under it share a column. Each non-empty cell is a
 * chunk (TerrainChunk), in row order (Z, then X). A chunk keeps its triangles (holes too), its centre (the middle of the
 * box around its triangles, on the position grid), and its surface:
 * its own vertex list (a vertex shared across a border is duplicated in each chunk; a face material splits its vertices
 * from the painted ones, so a renderer can give it a hard edge), positions relative to the centre, the triangles that
 * are drawn (holes left out) as indices into that list, and a bounding sphere around them.
 *
 * ACTIVE EDGES. Each triangle's edges are marked as a collision mesh marks its own (CollisionMesh_IsSharedEdgeActive): an
 * edge shared with one neighbour across a flat or concave fold is inactive, so a shape sliding over it takes the face's
 * normal and never catches. They are computed over the WHOLE terrain (vertices with one position are one vertex here):
 * an edge on a chunk's border keeps the activity its neighbour across the border gives it, so characters do not bump at
 * seams. Borders of the terrain, holes and non-manifold edges stay active.
 *
 * CHANGES (for the editor). The setters (TerrainMesh_SetVertexPositions, _SetVertexWeights, _SetVertexDensities,
 * _SetTriangleFlags) change the data and mark what it touches: the chunks whose triangles use a changed vertex, and for
 * a moved vertex or a hole the chunks around its neighbours (their normals and edges change).
 * TerrainMesh_RebuildDirtyChunks then recomputes those normals and edges and rebuilds those chunks, each with a new
 * revision (TerrainChunk_GetRevision): the renderer and the collision world rebuild what they made from a chunk when its
 * revision changes. A triangle stays in the chunk it was assigned to when the chunks were built: the editor's topology
 * edits (split, collapse, delete, fill) will rebuild the chunks, with a new layout revision
 * (TerrainMesh_GetLayoutRevision). Not thread-safe; main thread only.
 *
 * LIMITS (every file is hostile): at most TERRAIN_MESH_MAX_VERTICES vertices and TERRAIN_MESH_MAX_TRIANGLES triangles
 * filling at most TERRAIN_MESH_MAX_CHUNKS chunks, finite positions inside the extent, no edge longer than the extent,
 * indices in range and distinct in each triangle, known triangle flags, face materials and weights inside the palette,
 * densities inside the channels. A refusal names what and where.
 */


// Macros.
/** @brief The most vertices a terrain mesh may have. */
#define TERRAIN_MESH_MAX_VERTICES ((uint32_t)1U << 21U)
/** @brief The most triangles a terrain mesh may have. */
#define TERRAIN_MESH_MAX_TRIANGLES ((uint32_t)1U << 22U)
/** @brief The most chunks a terrain mesh may have (each costs the renderer GPU meshes and the collision world a shape):
 *  128 x 128 cells, 4 km square at the default 32 m, the whole extent at 128 m. */
#define TERRAIN_MESH_MAX_CHUNKS 16384U
/** @brief The most materials a terrain's palette may hold: one weight byte per vertex each. */
#define TERRAIN_MAX_MATERIALS 16U
/** @brief The most density channels a terrain may have: one byte per vertex each. */
#define TERRAIN_MAX_DENSITY_CHANNELS 8U
/** @brief How far from the terrain's origin a position may lie on any axis, in metres (a float keeps a millimetre there);
 *  also the longest edge a triangle may have. */
#define TERRAIN_MESH_EXTENT 8192.0
/** @brief The grid every position lies on, in metres (1/1024 m): chunk positions relative to their centres are exact in
 *  float on it. */
#define TERRAIN_POSITION_QUANTUM (1.0 / 1024.0)
/** @brief The smallest, largest and default chunk sizes, in metres. */
#define TERRAIN_MIN_CHUNK_SIZE 8.0f
#define TERRAIN_MAX_CHUNK_SIZE 256.0f
#define TERRAIN_DEFAULT_CHUNK_SIZE 32.0f
/** @brief Triangle flags: a hole (not drawn, no collision). */
#define TERRAIN_TRIANGLE_FLAG_HOLE ((uint8_t)0x1U)
/** @brief Triangle flags: the face takes the palette material in its material byte outright (a hard edge). */
#define TERRAIN_TRIANGLE_FLAG_OVERRIDES_MATERIAL ((uint8_t)0x2U)
/** @brief Every triangle flag there is (others are refused). */
#define TERRAIN_TRIANGLE_FLAGS_KNOWN ((uint8_t)0x3U)
/** @brief A chunk vertex's material when it carries the painted weights (not a face material). */
#define TERRAIN_CHUNK_BLEND_MATERIAL ((uint8_t)0xFFU)
/** @brief A triangle's active edges with every edge active: bit 0 for (v0, v1), bit 1 for (v1, v2), bit 2 for (v2, v0). */
#define TERRAIN_ALL_EDGES_ACTIVE ((uint8_t)0x7U)
/** @brief The largest .glb or .gltf a terrain is imported from, in bytes; its buffers together may hold no more. */
#define TERRAIN_GLTF_MAX_FILE_BYTES ((size_t)256U * 1024U * 1024U)
/** @brief The largest binary form (TerrainMesh_ReadGHDF) read, in bytes: a terrain at the mesh's limits fits. */
#define TERRAIN_FILE_MAX_BYTES ((size_t)192U * 1024U * 1024U)
/** @brief The most memory the binary form's parsed tree may take, as the GHDF reader estimates it while it reads. */
#define TERRAIN_FILE_MAX_TREE_BYTES ((size_t)256U * 1024U * 1024U)


// Types.
/**
 * @brief How a terrain's normals are made. Saved by value in the terrain's binary form: never renumber.
 */
typedef enum TerrainNormalModeEnum
{
    /** @brief Computed: area-weighted over the triangles around each vertex (holes left out), over the whole mesh. */
    TerrainNormalMode_Smooth = 0,
    /** @brief The source's own (a .glb's NORMAL attribute), kept as they are: hard edges stay hard. */
    TerrainNormalMode_Imported = 1
} TerrainNormalMode;

/**
 * @brief One triangle of a terrain mesh. Read-only: change it through the mesh's setters.
 */
typedef struct TerrainTriangleStruct
{
    /** @brief Its vertices, counter-clockwise seen from its front. */
    uint32_t _indices[3];
    /** @brief TERRAIN_TRIANGLE_FLAG_HOLE and TERRAIN_TRIANGLE_FLAG_OVERRIDES_MATERIAL. */
    uint8_t _flags;
    /** @brief The palette index of its face material while TERRAIN_TRIANGLE_FLAG_OVERRIDES_MATERIAL is set (0
     *  otherwise). */
    uint8_t _material;
    /** @brief Which of its edges are active for collision (see the file comment): bit 0 for (v0, v1), bit 1 for
     *  (v1, v2), bit 2 for (v2, v0), as CollisionMeshTriangle's. */
    uint8_t _activeEdges;
    /** @brief Padding (always 0). */
    uint8_t _reserved;
} TerrainTriangle;

/**
 * @brief One chunk of a terrain mesh: a cell of the chunk grid, its triangles and its surface. Read-only, owned by its
 *        mesh; valid until the chunks are rebuilt (TerrainMesh_RebuildDirtyChunks may move its arrays).
 */
typedef struct TerrainChunkStruct
{
    /** @brief Its cell: X and Z cell indices (the cell spans [x, x + 1) x chunk size along X, likewise Z). */
    int32_t _cellX;
    int32_t _cellZ;
    /** @brief Its centre in the terrain's space: the middle of the box around its triangles' vertices, on the position
     *  grid (so every surface position relative to it is within the extent, and exact). Recomputed whenever the chunk is
     *  rebuilt. */
    Vector3Double _center;
    /** @brief Its triangles (terrain triangle indices, holes too, ascending): _triangleCount entries from
     *  _firstTriangle in the mesh's chunk triangle list (TerrainMesh_GetChunkTriangles). */
    uint32_t _firstTriangle;
    uint32_t _triangleCount;
    /** @brief The surface's vertices: their positions relative to _center, the terrain vertex each copies, and its
     *  material (TERRAIN_CHUNK_BLEND_MATERIAL for the painted weights, else a face material's palette index). */
    Vector3* _positions;
    uint32_t* _vertexSources;
    uint8_t* _vertexMaterials;
    uint32_t _vertexCount;
    /** @brief The surface's triangles (its triangles that are not holes): three indices into the surface's vertices
     *  each, counter-clockwise, and the terrain triangle each one is. */
    uint32_t* _indices;
    uint32_t* _surfaceTriangles;
    uint32_t _surfaceTriangleCount;
    /** @brief The box and the sphere around the surface's vertices, relative to _center (all zero without a
     *  surface). */
    Vector3 _boundsMin;
    Vector3 _boundsMax;
    Vector3 _sphereCenter;
    float _sphereRadius;
    /** @brief Advances (from 1) every time the chunk is rebuilt. */
    uint32_t _revision;
} TerrainChunk;

/**
 * @brief A terrain mesh: see the file comment.
 *
 * Built with TerrainMesh_Create (from arrays), TerrainMesh_CreateFromGltf (a .glb) or TerrainMesh_ReadGHDF (its binary
 * form); released with TerrainMesh_Destroy. Fields are read-only outside source/TerrainMesh*.c: use the accessors.
 */
typedef struct TerrainMeshStruct
{
    /** @brief The vertices: positions (terrain space, on the position grid), unit normals, weights
     *  (TERRAIN_MAX_MATERIALS bytes each) and densities (TERRAIN_MAX_DENSITY_CHANNELS bytes each). */
    Vector3* _positions;
    Vector3* _normals;
    uint8_t* _weights;
    uint8_t* _densities;
    uint32_t _vertexCount;
    /** @brief The triangles. */
    TerrainTriangle* _triangles;
    uint32_t _triangleCount;
    /** @brief The palette's size (1 to TERRAIN_MAX_MATERIALS) and the number of density channels (0 to
     *  TERRAIN_MAX_DENSITY_CHANNELS). */
    uint32_t _materialCount;
    uint32_t _densityChannelCount;
    /** @brief How the normals are made. */
    TerrainNormalMode _normalMode;
    /** @brief The chunk grid's cell size, in metres. */
    float _chunkSize;
    /** @brief The chunks, in row order (Z, then X), and every chunk's triangles one after another (each chunk's range in
     *  it is its _firstTriangle and _triangleCount). */
    TerrainChunk* _chunks;
    uint32_t _chunkCount;
    uint32_t* _chunkTriangles;
    /** @brief Each triangle's chunk. */
    uint32_t* _triangleChunks;
    /** @brief Each vertex's group: the first vertex with the same position (vertices with one position are one vertex
     *  for normals and edges), and the next vertex of its group (UINT32_MAX: the last). */
    uint32_t* _groups;
    uint32_t* _nextInGroup;
    /** @brief Every group's triangles, ascending: group g's are _groupTriangles[_groupTriangleStarts[g] ..
     *  _groupTriangleStarts[g + 1]) (empty for a vertex that is not its group's first). */
    uint32_t* _groupTriangleStarts;
    uint32_t* _groupTriangles;
    /** @brief What the setters marked for the next rebuild: per group, triangle and chunk a flag, and the lists of the
     *  marked ones (uint32_t). */
    uint8_t* _isGroupDirty;
    uint8_t* _isTriangleDirty;
    uint8_t* _isChunkDirty;
    GenericBuffer _dirtyGroups;
    GenericBuffer _dirtyTriangles;
    GenericBuffer _dirtyChunks;
    /** @brief Advances (from 1) whenever the set of chunks changes. */
    uint32_t _layoutRevision;
    /** @brief Advances (from 1) whenever the set of chunks changes or a chunk is rebuilt: one comparison tells a reader
     *  whether any chunk's revision moved. */
    uint32_t _revision;
} TerrainMesh;

/**
 * @brief What TerrainMesh_Create builds a mesh from: borrowed arrays the caller fills (copied by the build).
 */
typedef struct TerrainMeshDataStruct
{
    /** @brief VertexCount positions in the terrain's space (rounded to the position grid). */
    const Vector3* Positions;
    /** @brief With TerrainNormalMode_Imported, VertexCount normals (normalized by the build; a zero one is refused);
     *  ignored (may be NULL) with TerrainNormalMode_Smooth. */
    const Vector3* Normals;
    /** @brief VertexCount x TERRAIN_MAX_MATERIALS weights, or NULL for none (all 0: the shaders then show the first
     *  material). */
    const uint8_t* Weights;
    /** @brief VertexCount x TERRAIN_MAX_DENSITY_CHANNELS densities, or NULL for none (all 0). */
    const uint8_t* Densities;
    /** @brief How many vertices (1 to TERRAIN_MESH_MAX_VERTICES). */
    uint32_t VertexCount;
    /** @brief Three vertex indices per triangle. */
    const uint32_t* Indices;
    /** @brief One flag byte per triangle (TERRAIN_TRIANGLE_FLAG_*), or NULL for none. */
    const uint8_t* TriangleFlags;
    /** @brief One face material per triangle (used where its flag is set), or NULL for 0. */
    const uint8_t* TriangleMaterials;
    /** @brief How many triangles (1 to TERRAIN_MESH_MAX_TRIANGLES). */
    uint32_t TriangleCount;
    /** @brief The palette's size (1 to TERRAIN_MAX_MATERIALS). */
    uint32_t MaterialCount;
    /** @brief The number of density channels (0 to TERRAIN_MAX_DENSITY_CHANNELS). */
    uint32_t DensityChannelCount;
    /** @brief How the normals are made. */
    TerrainNormalMode NormalMode;
    /** @brief The chunk size, in metres (TERRAIN_MIN_CHUNK_SIZE to TERRAIN_MAX_CHUNK_SIZE). */
    float ChunkSize;
} TerrainMeshData;

/**
 * @brief One attribute of a .glb and what its channels feed: palette materials (a terrain's weights) or density channels.
 *
 * A plain value type the caller fills; the strings are borrowed for the import.
 */
typedef struct TerrainAttributeMappingStruct
{
    /** @brief The attribute's name in the file ("COLOR_0", "_DENSITY_0"). */
    const unsigned char* Attribute;
    /** @brief Per channel (r, g, b, a): the palette index (weights) or density channel it feeds, or -1 for none. */
    int32_t Targets[4];
} TerrainAttributeMapping;

/**
 * @brief One part of a chunk's surface cut by TerrainMesh_SplitChunkSurface: a run of the surface's triangles and the
 *        run of the part's own vertices. A plain value type.
 */
typedef struct TerrainChunkPartStruct
{
    /** @brief Its first surface triangle and how many follow it (the parts take the surface's triangles in order). */
    uint32_t FirstTriangle;
    uint32_t TriangleCount;
    /** @brief Its first vertex in the split's vertex list and how many it has (at most the split's maximum). */
    uint32_t FirstVertex;
    uint32_t VertexCount;
} TerrainChunkPart;

/**
 * @brief How TerrainMesh_CreateFromGltf reads a .glb. A plain value type the caller fills; pointers are borrowed for the
 *        call.
 */
typedef struct TerrainGltfOptionsStruct
{
    /** @brief The attributes carrying the material weights, and how many. */
    const TerrainAttributeMapping* Weights;
    size_t WeightMappingCount;
    /** @brief The attributes carrying the densities, and how many. */
    const TerrainAttributeMapping* Densities;
    size_t DensityMappingCount;
    /** @brief The palette's material names (1 to TERRAIN_MAX_MATERIALS), asset names: a primitive whose material slot
     *  names one of them takes it outright on all its faces. Slot names and these are compared as asset names, each
     *  in full ("dirt" is "tbp:dirt"), after the blend slot and "hole" (which a slot names exactly). */
    const unsigned char* const* MaterialNames;
    uint32_t MaterialCount;
    /** @brief The number of density channels (0 to TERRAIN_MAX_DENSITY_CHANNELS). */
    uint32_t DensityChannelCount;
    /** @brief The material name whose faces use the painted weights ("blend"); a primitive without a material does too.
     *  The name "hole" makes holes; any other name is refused. */
    const unsigned char* BlendSlotName;
    /** @brief Smooth (computed) or the file's normals. */
    TerrainNormalMode NormalMode;
    /** @brief The chunk size, in metres. */
    float ChunkSize;
    /** @brief The file's path, for buffers kept in files beside it (NULL: the .glb's own buffer only). */
    const unsigned char* FilePath;
    /** @brief Hears what the import skipped (degenerate triangles, welded seams with different weights), once each; may
     *  be NULL. */
    Logger* Logger;
    /** @brief What error messages call the terrain ("Terrain \"hills\""), or NULL for "The terrain". */
    const unsigned char* Description;
} TerrainGltfOptions;


// Functions: building and releasing.
/**
 * @brief Builds a terrain mesh from arrays: checks every limit, copies the data (positions rounded to the position grid,
 *        imported normals normalized), computes the smooth normals and the active edges, and cuts the chunks.
 * @param data The arrays; must not be NULL. Borrowed: nothing is kept.
 * @param description What messages call the terrain ("Terrain \"hills\""), or NULL for "The terrain".
 * @param outMesh [out] Receives the mesh (owned by the caller: TerrainMesh_Destroy); NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments; ErrorCode_InvalidAssetData naming the broken limit and
 *          where (a count, a vertex outside the extent or not finite, an index out of range or used twice in one
 *          triangle, an edge longer than the extent, an unknown flag, a material or weight outside the palette, a
 *          density outside the channels, a zero imported normal, a chunk size out of range, more chunks than
 *          TERRAIN_MESH_MAX_CHUNKS).
 */
Error TerrainMesh_Create(const TerrainMeshData* data, const unsigned char* description, TerrainMesh** outMesh);

/**
 * @brief Reads a terrain mesh from a binary glTF (.glb) or a .gltf: every triangle primitive of every node of the default
 *        scene through its node's world transform (composed in double, rounded once), its positions, its indices, the
 *        attributes the options map (normalized unsigned bytes or shorts, or floats; scalars, RGB or RGBA; clamped to
 *        0..1), its normals (TerrainNormalMode_Imported) and its material's name (the blend slot, "hole", or a palette
 *        material).
 *
 * The file is hostile: its structure is checked before cgltf trusts a number (source/GltfChecks.h), and accessors the
 * terrain reads must store their data in the file (no accessor without a buffer view). Vertices with exactly the same
 * position (after the grid) are welded into one (with imported normals, the same position and normal): where a weld
 * joins different weights or densities (a seam in the file), they are averaged; triangles left with a repeated vertex
 * are dropped. Both are logged once, with counts.
 * @param data The file's bytes; borrowed. Must not be NULL unless @p byteCount is 0.
 * @param byteCount How many (at most TERRAIN_GLTF_MAX_FILE_BYTES).
 * @param options How to read it; must not be NULL.
 * @param outMesh [out] Receives the mesh (owned by the caller); NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments; ErrorCode_BufferTooLarge for a file past the limit;
 *          ErrorCode_InvalidAssetData for a file that is not valid glTF or breaks the terrain's rules (a primitive that
 *          is not triangles, naming its mesh; an unknown material slot, naming it; an attribute the options name that a
 *          primitive lacks or holds as another shape; the mesh's limits); ErrorCode_FileNotFound / ErrorCode_IO for a
 *          buffer file that is missing or unreadable.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TerrainMesh_CreateFromGltf(const void* data, size_t byteCount, const TerrainGltfOptions* options,
    TerrainMesh** outMesh);

/**
 * @brief Reads a .glb or .gltf file (at most TERRAIN_GLTF_MAX_FILE_BYTES) and imports it (TerrainMesh_CreateFromGltf;
 *        buffers in files are found beside it).
 * @param path The file; must not be NULL.
 * @param options As for TerrainMesh_CreateFromGltf (its FilePath, when NULL, becomes @p path).
 * @param outMesh [out] Receives the mesh; NULL on failure. Must not be NULL.
 * @returns As TerrainMesh_CreateFromGltf, and the file's read errors.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TerrainMesh_CreateFromGltfFile(const unsigned char* path, const TerrainGltfOptions* options,
    TerrainMesh** outMesh);

/**
 * @brief Releases a terrain mesh and everything it owns.
 * @param self The mesh; NULL does nothing.
 */
void TerrainMesh_Destroy(TerrainMesh* self);


// Functions: the binary form (source/TerrainMeshFile.c).
/**
 * @brief Appends the mesh's binary form (a GHDF document: the counts, the normal mode, the positions, the normals while
 *        imported, the weights of the palette's materials, the densities of its channels, the triangles and their flags)
 *        to @p outBytes. Nothing is appended on failure. Reading it back (TerrainMesh_ReadGHDF) gives the same mesh, bit
 *        for bit.
 * @param self The mesh; must not be NULL.
 * @param outBytes [out] A byte buffer the document is appended to. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TerrainMesh_WriteGHDF(const TerrainMesh* self, GenericBuffer* outBytes);

/**
 * @brief Reads a mesh from its binary form, treating it as hostile: the document's size (at most
 *        TERRAIN_FILE_MAX_BYTES) and its parsed tree (at most TERRAIN_FILE_MAX_TREE_BYTES) are limited, every entry
 *        must have its type and length, and the mesh's limits apply (TerrainMesh_Create).
 * @param bytes The document; borrowed. Must not be NULL unless @p byteCount is 0.
 * @param byteCount Its size.
 * @param chunkSize The chunk size to cut it into (the terrain's definition says).
 * @param description What messages call the terrain, or NULL.
 * @param outMesh [out] Receives the mesh; NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments; ErrorCode_BufferTooLarge past the size limit;
 *          ErrorCode_Deserialize for a document that is not a terrain (a missing or mistyped entry, a newer format, an
 *          array of the wrong length) or whose tree is too large; ErrorCode_InvalidAssetData for the mesh's limits.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TerrainMesh_ReadGHDF(const unsigned char* bytes, size_t byteCount, float chunkSize,
    const unsigned char* description, TerrainMesh** outMesh);

/**
 * @brief Writes the mesh's binary form to a file atomically: to "<path>.tmp" next to it, which then replaces it
 *        (FileSystem_ReplaceEntry); missing parent folders are made.
 * @param self The mesh; must not be NULL.
 * @param path The file; must not be NULL.
 * @returns Success, or the write's error (the file is then unchanged).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TerrainMesh_SaveGHDF(const TerrainMesh* self, const unsigned char* path);

/**
 * @brief Reads a file of at most TERRAIN_FILE_MAX_BYTES (checked before it is read) as TerrainMesh_ReadGHDF does.
 * @param path The file; must not be NULL.
 * @param chunkSize The chunk size to cut it into.
 * @param description What messages call the terrain, or NULL.
 * @param outMesh [out] Receives the mesh; NULL on failure. Must not be NULL.
 * @returns As TerrainMesh_ReadGHDF, and the file's read errors.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TerrainMesh_LoadGHDF(const unsigned char* path, float chunkSize, const unsigned char* description,
    TerrainMesh** outMesh);


// Functions: changes (for the editor).
/**
 * @brief Moves vertices (each rounded to the position grid) and marks for the next rebuild the chunks whose triangles use
 *        them or their neighbours (normals and edges change around a moved vertex). All or nothing.
 *
 * The vertex groups (one per position) stay as they were built: move every vertex at a position together, and never
 * onto another group's position, or the normals and edges there go on following the old grouping (a TODO for the
 * editor's topology edits).
 * @param self The mesh; must not be NULL.
 * @param vertices The vertices' indices; borrowed. Must not be NULL unless @p count is 0.
 * @param positions Their new positions (terrain space); borrowed.
 * @param count How many.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments; ErrorCode_ArgumentOutOfRange for an index out of range,
 *          a position that is not finite or outside the extent, or one that would give a triangle an edge longer than
 *          the extent (nothing is changed then).
 */
Error TerrainMesh_SetVertexPositions(TerrainMesh* self, const uint32_t* vertices, const Vector3* positions,
    size_t count);

/**
 * @brief Sets vertices' material weights and marks the chunks whose triangles use them. All or nothing.
 * @param self The mesh; must not be NULL.
 * @param vertices The vertices' indices; borrowed.
 * @param weights TERRAIN_MAX_MATERIALS bytes per vertex (those past the palette must be 0); borrowed.
 * @param count How many vertices.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments; ErrorCode_ArgumentOutOfRange for an index out of range
 *          or a weight past the palette.
 */
Error TerrainMesh_SetVertexWeights(TerrainMesh* self, const uint32_t* vertices, const uint8_t* weights, size_t count);

/**
 * @brief Sets vertices' densities and marks the chunks whose triangles use them. All or nothing.
 * @param self The mesh; must not be NULL.
 * @param vertices The vertices' indices; borrowed.
 * @param densities TERRAIN_MAX_DENSITY_CHANNELS bytes per vertex (those past the channels must be 0); borrowed.
 * @param count How many vertices.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments; ErrorCode_ArgumentOutOfRange for an index out of range
 *          or a density past the channels.
 */
Error TerrainMesh_SetVertexDensities(TerrainMesh* self, const uint32_t* vertices, const uint8_t* densities,
    size_t count);

/**
 * @brief Sets triangles' flags and face materials and marks their chunks (and, where a hole opens or closes, the chunks
 *        around its vertices: their normals and edges change). All or nothing.
 * @param self The mesh; must not be NULL.
 * @param triangles The triangles' indices; borrowed.
 * @param flags One flag byte per triangle (TERRAIN_TRIANGLE_FLAG_*); borrowed.
 * @param materials One face material per triangle (used where its flag is set); borrowed.
 * @param count How many triangles.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments; ErrorCode_ArgumentOutOfRange for an index out of range,
 *          an unknown flag or a face material past the palette.
 */
Error TerrainMesh_SetTriangleFlags(TerrainMesh* self, const uint32_t* triangles, const uint8_t* flags,
    const uint8_t* materials, size_t count);

/**
 * @brief Recomputes what the setters marked (the smooth normals and the active edges around the changes) and rebuilds
 *        the marked chunks' surfaces and bounds, each with a new revision. Cheap when nothing is marked.
 * @param self The mesh; must not be NULL.
 * @returns How many chunks were rebuilt.
 */
uint32_t TerrainMesh_RebuildDirtyChunks(TerrainMesh* self);


// Functions: parts and collision (source/TerrainMeshCollision.c).
/**
 * @brief Cuts a chunk's surface into parts of at most @p maxVertices vertices each, for consumers whose indices are
 *        narrower than the surface's (raylib's meshes and collision mesh parts take unsigned shorts): the surface's
 *        triangles in order, a part closing when the next triangle's new vertices would not fit in it. A vertex two
 *        parts share is in both.
 * @param self The mesh; must not be NULL.
 * @param chunk The chunk.
 * @param maxVertices The most vertices a part may have: at least 3.
 * @param outParts [out] A buffer of TerrainChunkPart (cleared first): the parts, in order; none for a chunk without a
 *        surface. Must not be NULL.
 * @param outVertices [out] A buffer of uint32_t (cleared first): each part's vertices, part after part, as the surface
 *        vertices they are (TerrainChunk_GetPositions' and _GetVertexSources' indices). Must not be NULL.
 * @param outIndices [out] A buffer of uint32_t (cleared first): three per surface triangle, in the surface's order, each
 *        an index among its part's own vertices (below its VertexCount). Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments; ErrorCode_ArgumentOutOfRange for a chunk past the
 *          chunks or fewer than 3 vertices a part; ErrorCode_InvalidOperation when a buffer could not grow (they are then
 *          incomplete).
 */
Error TerrainMesh_SplitChunkSurface(const TerrainMesh* self, uint32_t chunk, uint32_t maxVertices, GenericBuffer* outParts,
    GenericBuffer* outVertices, GenericBuffer* outIndices);

/**
 * @brief Builds the collision mesh of one chunk: its surface triangles (holes left out) at their positions
 *        relative to the chunk's centre (the caller places the mesh at the centre). Each triangle keeps its terrain
 *        triangle's active edges (computed over the whole terrain, so seams between chunks do not catch) and reports
 *        its terrain triangle as its source index (CollisionMeshTriangle._sourceIndex). The surface is cut into parts of
 *        at most 65536 vertices (a part's indices are unsigned shorts), which the mesh welds again.
 * @param self The mesh; must not be NULL.
 * @param chunk The chunk.
 * @param outMesh [out] Receives the mesh (owned by the caller: CollisionMesh_Destroy), or NULL for a chunk without a
 *        surface (every triangle a hole). Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments; ErrorCode_ArgumentOutOfRange for a chunk past the
 *          chunks; ErrorCode_InvalidAssetData when CollisionMesh_Create refuses it (more than
 *          COLLISION_MESH_MAX_TRIANGLES triangles, or none with an area).
 */
Error TerrainMesh_CreateChunkCollisionMesh(const TerrainMesh* self, uint32_t chunk, CollisionMesh** outMesh);


// Functions: reading.
/**
 * @brief Finds the chunk of a cell.
 * @param self The mesh; must not be NULL.
 * @param cellX The cell's X index.
 * @param cellZ The cell's Z index.
 * @param outIndex [out] Receives the chunk's index. Must not be NULL.
 * @returns false when no triangle lies in that cell.
 */
bool TerrainMesh_TryFindChunk(const TerrainMesh* self, int32_t cellX, int32_t cellZ, uint32_t* outIndex);

/**
 * @brief The cell a point of the terrain's space lies in.
 * @param self The mesh; must not be NULL.
 * @param point The point.
 * @param outCellX [out] Receives the X index. Must not be NULL.
 * @param outCellZ [out] Receives the Z index. Must not be NULL.
 */
void TerrainMesh_GetCell(const TerrainMesh* self, Vector3Double point, int32_t* outCellX, int32_t* outCellZ);

/** @brief The vertex count. */
static inline uint32_t TerrainMesh_GetVertexCount(const TerrainMesh* self)
{
    return self->_vertexCount;
}

/** @brief The triangle count. */
static inline uint32_t TerrainMesh_GetTriangleCount(const TerrainMesh* self)
{
    return self->_triangleCount;
}

/** @brief The palette's size (1 to TERRAIN_MAX_MATERIALS). */
static inline uint32_t TerrainMesh_GetMaterialCount(const TerrainMesh* self)
{
    return self->_materialCount;
}

/** @brief The number of density channels (0 to TERRAIN_MAX_DENSITY_CHANNELS). */
static inline uint32_t TerrainMesh_GetDensityChannelCount(const TerrainMesh* self)
{
    return self->_densityChannelCount;
}

/** @brief How the normals are made. */
static inline TerrainNormalMode TerrainMesh_GetNormalMode(const TerrainMesh* self)
{
    return self->_normalMode;
}

/** @brief The chunk size, in metres. */
static inline float TerrainMesh_GetChunkSize(const TerrainMesh* self)
{
    return self->_chunkSize;
}

/**
 * @brief Returns a vertex's position (terrain space).
 * @param self The mesh; must not be NULL.
 * @param vertex The vertex; must be below the vertex count.
 * @returns The position.
 */
static inline Vector3 TerrainMesh_GetPosition(const TerrainMesh* self, uint32_t vertex)
{
    return self->_positions[vertex];
}

/** @brief Returns a vertex's unit normal (@p vertex below the vertex count). */
static inline Vector3 TerrainMesh_GetNormal(const TerrainMesh* self, uint32_t vertex)
{
    return self->_normals[vertex];
}

/** @brief Returns a vertex's TERRAIN_MAX_MATERIALS weights (borrowed; @p vertex below the vertex count). */
static inline const uint8_t* TerrainMesh_GetWeights(const TerrainMesh* self, uint32_t vertex)
{
    return &self->_weights[(size_t)vertex * TERRAIN_MAX_MATERIALS];
}

/** @brief Returns a vertex's TERRAIN_MAX_DENSITY_CHANNELS densities (borrowed; @p vertex below the vertex count). */
static inline const uint8_t* TerrainMesh_GetDensities(const TerrainMesh* self, uint32_t vertex)
{
    return &self->_densities[(size_t)vertex * TERRAIN_MAX_DENSITY_CHANNELS];
}

/** @brief Returns a triangle (borrowed; @p triangle below the triangle count). */
static inline const TerrainTriangle* TerrainMesh_GetTriangle(const TerrainMesh* self, uint32_t triangle)
{
    return &self->_triangles[triangle];
}

/** @brief Returns the chunk count. */
static inline uint32_t TerrainMesh_GetChunkCount(const TerrainMesh* self)
{
    return self->_chunkCount;
}

/** @brief Returns a chunk (borrowed; @p chunk below the chunk count). */
static inline const TerrainChunk* TerrainMesh_GetChunk(const TerrainMesh* self, uint32_t chunk)
{
    return &self->_chunks[chunk];
}

/** @brief Returns the chunk a triangle belongs to (@p triangle below the triangle count). */
static inline uint32_t TerrainMesh_GetTriangleChunk(const TerrainMesh* self, uint32_t triangle)
{
    return self->_triangleChunks[triangle];
}

/**
 * @brief Returns a chunk's triangles (terrain triangle indices, holes too, ascending).
 * @param self The mesh; must not be NULL.
 * @param chunk The chunk; must be below the chunk count.
 * @param outCount [out] Receives how many. Must not be NULL.
 * @returns The indices (borrowed until the chunks are rebuilt).
 */
static inline const uint32_t* TerrainMesh_GetChunkTriangles(const TerrainMesh* self, uint32_t chunk, uint32_t* outCount)
{
    *outCount = self->_chunks[chunk]._triangleCount;
    return &self->_chunkTriangles[self->_chunks[chunk]._firstTriangle];
}

/** @brief Returns a number that advances (from 1) whenever the set of chunks changes. */
static inline uint32_t TerrainMesh_GetLayoutRevision(const TerrainMesh* self)
{
    return self->_layoutRevision;
}

/** @brief Returns a number that advances (from 1) whenever the set of chunks changes or a chunk is rebuilt (its own
 *  revision then advances too). */
static inline uint32_t TerrainMesh_GetRevision(const TerrainMesh* self)
{
    return self->_revision;
}

/** @brief Whether the setters marked something TerrainMesh_RebuildDirtyChunks has not rebuilt yet. */
static inline bool TerrainMesh_HasDirtyChunks(const TerrainMesh* self)
{
    return (self->_dirtyChunks._count > 0U) || (self->_dirtyGroups._count > 0U) || (self->_dirtyTriangles._count > 0U);
}

/** @brief A chunk's centre in the terrain's space. */
static inline Vector3Double TerrainChunk_GetCenter(const TerrainChunk* self)
{
    return self->_center;
}

/** @brief A chunk's revision: advances (from 1) every time it is rebuilt. */
static inline uint32_t TerrainChunk_GetRevision(const TerrainChunk* self)
{
    return self->_revision;
}

/** @brief How many vertices a chunk's surface has. */
static inline uint32_t TerrainChunk_GetVertexCount(const TerrainChunk* self)
{
    return self->_vertexCount;
}

/** @brief How many triangles a chunk's surface has (its triangles that are not holes). */
static inline uint32_t TerrainChunk_GetSurfaceTriangleCount(const TerrainChunk* self)
{
    return self->_surfaceTriangleCount;
}

/** @brief A chunk's cell (its X and Z indices in the chunk grid). */
static inline void TerrainChunk_GetCell(const TerrainChunk* self, int32_t* outCellX, int32_t* outCellZ)
{
    *outCellX = self->_cellX;
    *outCellZ = self->_cellZ;
}

/** @brief A chunk's surface positions, relative to its centre (TerrainChunk_GetVertexCount of them; NULL without a
 *  surface). */
static inline const Vector3* TerrainChunk_GetPositions(const TerrainChunk* self)
{
    return self->_positions;
}

/** @brief The terrain vertex each surface vertex copies (its normal, weights and densities are that vertex's). */
static inline const uint32_t* TerrainChunk_GetVertexSources(const TerrainChunk* self)
{
    return self->_vertexSources;
}

/** @brief Each surface vertex's material: TERRAIN_CHUNK_BLEND_MATERIAL for the painted weights, else a face material's
 *  palette index. */
static inline const uint8_t* TerrainChunk_GetVertexMaterials(const TerrainChunk* self)
{
    return self->_vertexMaterials;
}

/** @brief The surface's triangles: three surface vertex indices each, counter-clockwise, in the corner order of the
 *  terrain triangle each one is (so a terrain triangle's edge bits apply to it as they are). */
static inline const uint32_t* TerrainChunk_GetIndices(const TerrainChunk* self)
{
    return self->_indices;
}

/** @brief The terrain triangle each surface triangle is. */
static inline const uint32_t* TerrainChunk_GetSurfaceTriangles(const TerrainChunk* self)
{
    return self->_surfaceTriangles;
}

/** @brief The box around a chunk's surface, relative to its centre (zero without a surface). */
static inline void TerrainChunk_GetBounds(const TerrainChunk* self, Vector3* outMin, Vector3* outMax)
{
    *outMin = self->_boundsMin;
    *outMax = self->_boundsMax;
}

/** @brief The sphere around a chunk's surface: its centre relative to the chunk's centre, and its radius (zero without
 *  a surface). */
static inline void TerrainChunk_GetSphere(const TerrainChunk* self, Vector3* outCenter, float* outRadius)
{
    *outCenter = self->_sphereCenter;
    *outRadius = self->_sphereRadius;
}
