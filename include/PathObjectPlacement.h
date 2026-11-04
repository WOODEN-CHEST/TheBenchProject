#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "wr/WRVectorMath.h"
#include "PathCurve.h"
#include "PathObject.h"
#include "PathObjectLayout.h"
#include "PropertyDescriptor.h"


/**
 * @file PathObjectPlacement.h
 * @brief What a laid-out piece turns into, as pure maths: how a piece's mesh is measured, where a rigid piece's copy stands,
 *        and how a bent piece's slices lie and its mesh follows them. No world, no assets, no GL: meshes come in as arrays, and
 *        the ground comes in as points the caller already snapped.
 *
 * THE MESH'S BODY. A piece's mesh runs along its own +X, stands on its Y = 0 and is centred on its Z = 0, with its +Z to the
 * right
 * of the path's direction. Its BODY is its size along X less its entry depths (what reaches into its neighbours), or a length the
 * setting gives; the middle of the body lands on the middle of the piece. A stretched piece scales along X by the piece's length
 * over the body's.
 *
 * A RIGID COPY (PathObjectPlacement_MakeCopy) takes its two ends and the slope's rule: `shear` keeps the up as it is and lets
 * the length climb (no vertical moves out of vertical: PropertyInstanceEntry.Shear), `tilt` turns it with the slope, `step`
 * keeps it
 * level at the height the anchor says. A chord steeper than PATH_OBJECT_MAX_SHEAR_SLOPE cannot shear: it tilts. A turned-round
 * piece
 * (a flip, the left side) is the same piece turned 180 degrees about its up through its middle, so its faces never mirror. A
 * rotation
 * offset and a yaw jitter turn it through its middle too: its body's middle stays where the rest says, wherever the mesh's
 * origin is.
 *
 * A BENT PIECE (PathObjectBend_AppendMesh) is cut at the planes of its slices (PathObjectBendSlice), each a position and three
 * axes:
 * a point (x, y, z) of the mesh between two slices goes to the lerp of their positions by x, plus y and z along the lerp of their
 * axes, so neighbouring slabs meet exactly. What reaches past the first or the last slice (an entry depth) carries on straight
 * along
 * that slice's X axis. Normals and tangents go through the same frame (cofactors, so a shear and a scale keep them right).
 * Triangles come out unindexed: the vertices are the clipped polygons'.
 */


// Macros.
/** @brief How much a chord may climb per metre across and still shear (80 degrees). */
#define PATH_OBJECT_MAX_SHEAR_SLOPE 5.671f
/** @brief The most slices one bent piece takes. */
#define PATH_OBJECT_MAX_SLICES ((size_t)4096)


// Types.
/** @brief A world model's mesh as a path object measures it: LOD 0's box under its import transform. A plain value type. */
typedef struct PathObjectMeasureStruct
{
    /** @brief The box, in the object's own space. */
    Vector3 Min;
    Vector3 Max;
} PathObjectMeasure;

/** @brief Where one end of a piece lies: a position already off to its side and snapped to the ground, and the frame there. */
typedef struct PathObjectPieceEndStruct
{
    /** @brief The position, in the space the copy's position is in. */
    Vector3Double Position;
    /** @brief The unit tangent, up and right there (PathCurvePoint). */
    Vector3 Tangent;
    Vector3 Up;
    Vector3 Right;
    /** @brief The node's scale and width factors there. */
    float Scale;
    float Width;
} PathObjectPieceEnd;

/** @brief What a rigid piece's copy is made from. */
typedef struct PathObjectCopyInputStruct
{
    /** @brief Its two ends (equal for a joint or a piece of no length). */
    PathObjectPieceEnd Start;
    PathObjectPieceEnd End;
    /** @brief The mesh's measure and the piece's settings in effect. */
    PathObjectMeasure Measure;
    const PathObjectSlotRecord* Slot;
    /** @brief Whether it is turned end for end (its flip, or the left side). */
    bool IsTurned;
    /** @brief Whether it keeps its own length along its X whatever its chord's run: a joint, or a level piece that does not
     * stretch,
     *  so a post stays as thick on a slope. Otherwise its X spans the chord, so its ends meet what comes before and after it. */
    bool KeepsLength;
    /** @brief Random values in [0, 1) for its jitter: position along, up and right, yaw, scale. */
    float Jitter[5];
    /** @brief The model's tint. */
    Color Tint;
} PathObjectCopyInput;

/** @brief One slice of a bent piece: where a plane of the mesh's X lands. Everything is in the space its mesh is built in. */
typedef struct PathObjectBendSliceStruct
{
    /** @brief The plane: this X of the mesh (under its import transform). Slices rise along it. */
    float MeshX;
    /** @brief Where the mesh's (MeshX, 0, 0) lands. */
    Vector3 Position;
    /** @brief The images of the mesh's unit X (for normals, and past the ends), Y and Z. */
    Vector3 AxisX;
    Vector3 AxisY;
    Vector3 AxisZ;
} PathObjectBendSlice;

/** @brief A mesh to bend: raylib's arrays as a Mesh holds them. Any but the positions may be NULL. */
typedef struct PathObjectSourceMeshStruct
{
    /** @brief Three floats a vertex. */
    const float* Positions;
    const float* Normals;
    /** @brief Four floats a vertex. */
    const float* Tangents;
    /** @brief Two floats a vertex. */
    const float* Texcoords;
    const float* Texcoords2;
    /** @brief Four bytes a vertex. */
    const unsigned char* Colors;
    uint32_t VertexCount;
    /** @brief Three a triangle, or NULL: every three vertices are a triangle. */
    const unsigned short* Indices;
    uint32_t TriangleCount;
    /** @brief The LOD's import transform: applied before anything else. */
    Matrix Transform;
} PathObjectSourceMesh;

/**
 * @brief What bending has made of one mesh so far: unindexed vertices, three a triangle. Construct, append pieces, read or take
 *        the arrays, clear or deconstruct.
 */
typedef struct PathObjectBentMeshStruct
{
    /** @brief floats: three a vertex. */
    GenericBuffer Positions;
    /** @brief floats: three a vertex. */
    GenericBuffer Normals;
    /** @brief floats: four a vertex, while the source has them. */
    GenericBuffer Tangents;
    /** @brief floats: two a vertex, while the source has them. */
    GenericBuffer Texcoords;
    GenericBuffer Texcoords2;
    /** @brief bytes: four a vertex, while the source has them. */
    GenericBuffer Colors;
    /** @brief The box of its positions; meaningless while it has none. */
    Vector3 BoxMin;
    Vector3 BoxMax;
} PathObjectBentMesh;


// Functions: measuring.
/**
 * @brief A mesh's box under its import transform.
 * @param positions Three floats a vertex; may be NULL when @p vertexCount is 0.
 * @param vertexCount How many vertices.
 * @param transform The import transform.
 * @param measure [in,out] Grown to hold them. Start it with PathObjectMeasure_CreateEmpty.
 */
void PathObjectMeasure_AddVertices(PathObjectMeasure* measure, const float* positions, size_t vertexCount, Matrix transform);

/**
 * @brief A box's eight corners under an import transform, for a model with no vertices at hand.
 * @param measure [in,out] Grown to hold them.
 * @param box The box.
 * @param transform The import transform.
 */
void PathObjectMeasure_AddBox(PathObjectMeasure* measure, BoundingBox box, Matrix transform);

/** @brief A measure holding nothing yet. @returns The measure. */
static inline PathObjectMeasure PathObjectMeasure_CreateEmpty(void)
{
    return (PathObjectMeasure){ .Min = { 1.0e30f, 1.0e30f, 1.0e30f }, .Max = { -1.0e30f, -1.0e30f, -1.0e30f } };
}

/**
 * @brief The length a slot's piece takes along the path: its setting while it overrides it, otherwise the mesh's size along X
 *        less its entry depths, at least 0.
 * @param measure The mesh's measure; must not be NULL.
 * @param slot The slot's settings in effect; must not be NULL.
 * @returns The length in metres (0 for a measure holding nothing).
 */
float PathObjectMeasure_GetLength(const PathObjectMeasure* measure, const PathObjectSlotRecord* slot);

/**
 * @brief The mesh's X that lands on the middle of the piece: the middle of its body.
 * @param measure The mesh's measure; must not be NULL.
 * @param slot The slot's settings in effect; must not be NULL.
 * @returns The X (0 for a measure holding nothing).
 */
float PathObjectMeasure_GetBodyCenter(const PathObjectMeasure* measure, const PathObjectSlotRecord* slot);


// Functions: rigid copies.
/**
 * @brief The copy of a rigid piece.
 * @param input What it is made from; must not be NULL.
 * @param outCopy [out] Receives the copy, its position in the space of the input's. Must not be NULL.
 * @returns true; false for an input with anything not finite (the copy is then left alone).
 */
bool PathObjectPlacement_MakeCopy(const PathObjectCopyInput* input, PropertyInstanceEntry* outCopy);


// Functions: bent pieces.
/**
 * @brief Initializes an empty bent mesh.
 * @param self The mesh; must not be NULL.
 */
void PathObjectBentMesh_Construct(PathObjectBentMesh* self);

/**
 * @brief Releases its buffers.
 * @param self The mesh, or NULL.
 */
void PathObjectBentMesh_Deconstruct(PathObjectBentMesh* self);

/**
 * @brief Empties it, keeping its buffers.
 * @param self The mesh; must not be NULL.
 */
void PathObjectBentMesh_Clear(PathObjectBentMesh* self);

/**
 * @brief How many vertices it holds.
 * @param self The mesh; must not be NULL.
 * @returns The count: three a triangle.
 */
static inline size_t PathObjectBentMesh_GetVertexCount(const PathObjectBentMesh* self)
{
    return self->Positions._count / 3U;
}

/**
 * @brief Bends one mesh along its slices and appends what comes out.
 *
 * Every triangle of the mesh with a part between @p cutMinX and @p cutMaxX comes out cut at the slices' planes. A mesh whose X
 * reaches past the slices carries on straight (see the file comment). A triangle flattened by a slice's axes stays, with what
 * normal its corners have.
 * @param self The mesh to append to; must not be NULL. Tangents, texture coordinates and colours are kept only while every
 *        source appended so far has them.
 * @param source The mesh; must not be NULL.
 * @param slices The slices, rising along their MeshX; must not be NULL.
 * @param sliceCount How many, at least 1, at most PATH_OBJECT_MAX_SLICES.
 * @param cutMinX What lies below this X of the mesh is dropped; -INFINITY for nothing.
 * @param cutMaxX What lies above this X is dropped; +INFINITY for nothing.
 * @param maxVertexCount The most vertices @p self may hold afterwards.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments or a slice count out of range; ErrorCode_ArgumentOutOfRange
 *          when @p maxVertexCount would be passed; ErrorCode_BufferTooSmall when a buffer could not grow. On a failure the mesh
 *          holds what was appended before it.
 */
Error PathObjectBend_AppendMesh(PathObjectBentMesh* self, const PathObjectSourceMesh* source,
    const PathObjectBendSlice* slices, size_t sliceCount, float cutMinX, float cutMaxX, size_t maxVertexCount);
