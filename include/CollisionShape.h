#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// raylib.h is needed for Vector2, Vector3 and Matrix, which shapes hold and return by value; PropertyDescriptor.h for
// the shape types' names (a PropertyEnumTable).
#include "raylib/raylib.h"
#include "PropertyDescriptor.h"
#include "wr/WRError.h"

/** @brief A loaded model asset (GameModel.h); hulls and meshes can be built from its vertices. */
typedef struct GameModelStruct GameModel;


/**
 * @file CollisionShape.h
 * @brief Collision shapes: the primitives, convex hulls and triangle meshes a collider is made of, and the named sets
 *        (compounds) world models group them into.
 *
 * A CollisionShape is a plain value: a type, a centre and a rotation (Euler XYZ, radians) in its owner's space (a
 * world model's, or an object's for a collider's own shape), an optional part name that contacts report, and the sizes
 * its type uses. Seven types:
 * - SPHERE: a radius around the centre;
 * - CAPSULE: a segment of 2 x HalfHeight along local Y, swept by the radius;
 * - BOX: half extents, oriented by the rotation;
 * - CYLINDER: a radius and a half height, along local Y;
 * - CONVEX_HULL: a CollisionHull (built once from points or a model's vertices, owned by whoever built it);
 * - PLANE: its front is local +Y. Bounded (the default): a rectangle in local XZ of PlaneHalfExtents, solid Depth metres
 *   behind it within the rectangle (0: a flat rectangle). Unbounded: an infinite half-space, solid behind it;
 * - MESH: a CollisionMesh (triangles with a BVH, built once from a model), never pushed (static and kinematic
 *   colliders only).
 *
 * A CollisionSet is a named list of shapes: one compound "hitbox". A world model holds its sets (WorldModel.h); a door
 * has "closed" and "open", and a placement switches between them.
 *
 * Hulls and meshes are immutable once built and shared by every placement; build them with CollisionHull_Create /
 * CollisionMesh_Create (or from a model) and release them with their Destroy. Each is one allocation. Building a hull
 * or a mesh from a model reads the model's CPU-side vertices (raylib keeps them after upload); loading a model needs a
 * GL context, so headless tools can only use hulls built from points.
 */


// Macros.
/** @brief The most sets a world model may have. */
#define COLLISION_MAX_SETS ((size_t)32)
/** @brief The most shapes one set may have. */
#define COLLISION_MAX_SET_SHAPES ((size_t)128)
/** @brief The most shapes a world model may have over all its sets. */
#define COLLISION_MAX_MODEL_SHAPES ((size_t)1024)
/** @brief The most bytes a set's or a part's name may have (without the terminator). */
#define COLLISION_NAME_MAX_LENGTH ((size_t)128)
/** @brief The most points a convex hull is built from (a model's vertices included). */
#define COLLISION_HULL_MAX_INPUT_POINTS ((size_t)65536)
/** @brief The most vertices a built convex hull may keep; a finer hull is reduced to its maximum. */
#define COLLISION_HULL_MAX_VERTICES ((uint32_t)256)
/** @brief A convex hull's maximum vertex count when its definition does not say. */
#define COLLISION_HULL_DEFAULT_MAX_VERTICES ((uint32_t)64)
/** @brief The fewest vertices a built hull may be reduced to (a tetrahedron). */
#define COLLISION_HULL_MIN_VERTICES ((uint32_t)4)
/** @brief The most triangles a collision mesh may hold. */
#define COLLISION_MESH_MAX_TRIANGLES ((uint32_t)1U << 20U)
/** @brief The size every primitive starts at in CollisionShape_Create: a radius, half height or half extent, metres. */
#define COLLISION_SHAPE_DEFAULT_SIZE 0.5f
/** @brief A collider's mass when neither its world model nor its placement gives one, in kilograms (how pushes are
 *  shared; rigid bodies, not built yet, will use it too). */
#define COLLISION_DEFAULT_MASS 1.0f
/** @brief The largest mass a world model or a collider may give (above 0 up to this, kilograms): a ratio of masses past
 *  about 1e6 would lose the lighter one's push to float rounding. */
#define COLLISION_MAX_MASS 1.0e6f


// Types.
/**
 * @brief The shape types. Saved by value (a collider's own shape): never renumber.
 */
typedef enum CollisionShapeTypeEnum
{
    /** @brief A sphere. */
    CollisionShapeType_Sphere = 0,
    /** @brief A capsule along local Y. */
    CollisionShapeType_Capsule = 1,
    /** @brief An oriented box. */
    CollisionShapeType_Box = 2,
    /** @brief A cylinder along local Y. */
    CollisionShapeType_Cylinder = 3,
    /** @brief A convex hull (CollisionHull). */
    CollisionShapeType_ConvexHull = 4,
    /** @brief A plane: a bounded rectangle (solid behind it, to a depth) or an infinite half-space. */
    CollisionShapeType_Plane = 5,
    /** @brief A triangle mesh (CollisionMesh). */
    CollisionShapeType_Mesh = 6,
    /** @brief The number of types (not a type). */
    CollisionShapeType_Count = 7
} CollisionShapeType;

/**
 * @brief One face of a convex hull: its plane and its vertices.
 */
typedef struct CollisionHullFaceStruct
{
    /** @brief The outward unit normal. */
    Vector3 _normal;
    /** @brief The plane's offset: points x of the face have dot(_normal, x) == _offset. */
    float _offset;
    /** @brief The face's first entry in the hull's face vertex indices. */
    uint32_t _firstIndex;
    /** @brief How many vertices the face has (at least 3), counter-clockwise seen from outside. */
    uint32_t _vertexCount;
} CollisionHullFace;

/**
 * @brief A convex hull, in its owner's local space (before the shape's centre and rotation).
 *
 * Built by CollisionHull_Create (quickhull, with coplanar faces merged into polygons) and released with
 * CollisionHull_Destroy; immutable and read-only (use the accessors). One allocation holds the struct and its arrays.
 */
typedef struct CollisionHullStruct
{
    /** @brief The vertices (4 to COLLISION_HULL_MAX_VERTICES). */
    const Vector3* _vertices;
    uint32_t _vertexCount;
    /** @brief The faces. */
    const CollisionHullFace* _faces;
    uint32_t _faceCount;
    /** @brief Every face's vertex indices, face after face. */
    const uint32_t* _faceVertexIndices;
    uint32_t _faceVertexIndexCount;
    /** @brief The edges, two vertex indices each (for debug drawing). */
    const uint32_t* _edgeVertexIndices;
    uint32_t _edgeCount;
    /** @brief The box around the vertices. */
    Vector3 _boundsMin;
    Vector3 _boundsMax;
    /** @brief The mean of the vertices (inside the hull). */
    Vector3 _centroid;
} CollisionHull;

/**
 * @brief One triangle of a collision mesh.
 */
typedef struct CollisionMeshTriangleStruct
{
    /** @brief Its vertices' indices in the mesh. */
    uint32_t _indices[3];
    /** @brief The model's mesh (raylib's mesh index) it came from. */
    uint32_t _part;
    /** @brief Its index among that mesh's own triangles (with the part, it finds the model's UVs, for surface picking,
     *  not built yet). */
    uint32_t _sourceIndex;
    /** @brief Which edges are active: bit 0 for (v0, v1), bit 1 for (v1, v2), bit 2 for (v2, v0). An inactive edge is
     *  shared with a neighbour across a flat or concave seam: contacts on it take the face normal, so shapes slide over
     *  seams without catching. */
    uint8_t _activeEdges;
} CollisionMeshTriangle;

/**
 * @brief A node of a collision mesh's bounding volume hierarchy.
 */
typedef struct CollisionMeshNodeStruct
{
    /** @brief The box around everything under the node. */
    Vector3 _min;
    Vector3 _max;
    /** @brief A leaf: its first triangle. An inner node: its second child's index (the first child is the next node). */
    uint32_t _start;
    /** @brief A leaf: how many triangles it holds (at least 1). An inner node: 0. */
    uint32_t _count;
} CollisionMeshNode;

/**
 * @brief A triangle mesh with a bounding volume hierarchy, in its owner's local space.
 *
 * Built by CollisionMesh_Create: vertices welded (equal positions become one), degenerate triangles skipped, each
 * edge marked active or not, and a BVH over the triangles. Released with CollisionMesh_Destroy; immutable and
 * read-only (use the accessors). One allocation holds the struct and its arrays.
 */
typedef struct CollisionMeshStruct
{
    /** @brief The welded vertices. */
    const Vector3* _vertices;
    uint32_t _vertexCount;
    /** @brief The triangles, in the BVH's leaf order (1 to COLLISION_MESH_MAX_TRIANGLES). */
    const CollisionMeshTriangle* _triangles;
    uint32_t _triangleCount;
    /** @brief The BVH's nodes, the root first. */
    const CollisionMeshNode* _nodes;
    uint32_t _nodeCount;
    /** @brief Each part's material index (raylib's meshMaterial; -1 when the source had none). */
    const int32_t* _partMaterials;
    uint32_t _partCount;
    /** @brief The box around the vertices. */
    Vector3 _boundsMin;
    Vector3 _boundsMax;
} CollisionMesh;

/**
 * @brief One part of a collision mesh's source: a vertex array and, optionally, its triangles' indices.
 *
 * A plain value type the caller fills; CollisionMesh_Create copies what it needs.
 */
typedef struct CollisionMeshPartStruct
{
    /** @brief The vertices, three floats (x, y, z) each; borrowed. */
    const float* Vertices;
    /** @brief The number of vertices. */
    uint32_t VertexCount;
    /** @brief Three indices per triangle; borrowed. NULL: every three vertices in a row are a triangle (raylib's OBJ
     *  meshes). */
    const unsigned short* Indices;
    /** @brief The number of triangles (with Indices); ignored without them (VertexCount / 3). */
    uint32_t TriangleCount;
    /** @brief The part's material index, reported by ray hits (-1 for none). */
    int32_t MaterialIndex;
    /** @brief One byte per triangle: its active edges as the caller decided them (CollisionMeshTriangle's bits; a part
     *  cut from a larger mesh, a terrain chunk, keeps the activity its neighbours across the cut give its border edges),
     *  or NULL: computed from the mesh's own triangles. Borrowed. */
    const uint8_t* ActiveEdges;
    /** @brief One index per triangle: the source index its hits report (a terrain's own triangle), or NULL: its index
     *  among the part's triangles. Borrowed. */
    const uint32_t* SourceIndices;
} CollisionMeshPart;

/**
 * @brief A collision shape: a plain value type (see the file comment).
 *
 * Build one with CollisionShape_Create and set what the type uses. A hull or mesh shape borrows its CollisionHull or
 * CollisionMesh, which must outlive every use of the shape; the name is borrowed too.
 */
typedef struct CollisionShapeStruct
{
    /** @brief The type. */
    CollisionShapeType Type;
    /** @brief Where the shape's own origin sits in its owner's space. */
    Vector3 Center;
    /** @brief The shape's rotation in its owner's space: Euler XYZ in radians (raylib's MatrixRotateXYZ). */
    Vector3 Rotation;
    /** @brief The part's name contacts report ([a-z][a-z0-9_.-]*, at most COLLISION_NAME_MAX_LENGTH bytes), or NULL;
     *  borrowed. */
    const unsigned char* Name;
    /** @brief Sphere, capsule and cylinder: the radius, above 0. */
    float Radius;
    /** @brief Capsule: half the segment between the two sphere centres, at least 0. Cylinder: half its height, above
     *  0. */
    float HalfHeight;
    /** @brief Box: its half extents, each above 0. */
    Vector3 HalfExtents;
    /** @brief Plane: whether it is a rectangle (true) or an infinite half-space (false). */
    bool IsBounded;
    /** @brief Plane, while bounded: the rectangle's half extents along local X and Z, each above 0. */
    Vector2 PlaneHalfExtents;
    /** @brief Plane, while bounded: how far behind the rectangle it is solid, at least 0 (0: a flat rectangle). */
    float Depth;
    /** @brief Convex hull: the hull (borrowed). */
    const CollisionHull* Hull;
    /** @brief Mesh: the mesh (borrowed). */
    const CollisionMesh* Mesh;
    /** @brief Mesh: whether its triangles collide from both sides (true) or only from the front (counter-clockwise). */
    bool IsTwoSided;
} CollisionShape;

/**
 * @brief A named set of shapes: one compound "hitbox" of a world model. Read-only, owned by the world model.
 */
typedef struct CollisionSetStruct
{
    /** @brief The set's name ([a-z][a-z0-9_.-]*, at most COLLISION_NAME_MAX_LENGTH bytes), unique in its world model. */
    const unsigned char* _name;
    /** @brief Its shapes (1 to COLLISION_MAX_SET_SHAPES). */
    const CollisionShape* _shapes;
    uint32_t _shapeCount;
} CollisionSet;


// Fields.
/** @brief The names of every shape type ("sphere", "capsule", "box", "cylinder", "convex_hull", "plane", "mesh"): the
 *  world model definition reads them. */
extern const PropertyEnumTable COLLISION_SHAPE_TYPE_ENUM;
/** @brief The names of the primitive types a collider's own shape may have ("sphere", "capsule", "box", "cylinder",
 *  "plane"): the collider's shape_type row reads them. */
extern const PropertyEnumTable COLLISION_PRIMITIVE_TYPE_ENUM;


// Functions: shapes.
/**
 * @brief A shape of @p type with the default sizes: at the origin, not turned, unnamed, every size
 *        COLLISION_SHAPE_DEFAULT_SIZE, a bounded flat plane, a two-sided mesh, no hull or mesh yet.
 * @param type The type.
 * @returns The shape.
 */
CollisionShape CollisionShape_Create(CollisionShapeType type);

/**
 * @brief Checks a shape's values: a known type, a finite centre and rotation, the sizes its type uses finite and in
 *        range, a hull or a mesh where the type needs one, a valid name or none.
 * @param self The shape; must not be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange naming the broken value.
 */
Error CollisionShape_Validate(const CollisionShape* self);

/**
 * @brief The shape's transform in its owner's space: its rotation, then its centre.
 * @param self The shape; must not be NULL.
 * @returns The matrix (raylib's convention).
 */
Matrix CollisionShape_GetTransform(const CollisionShape* self);

/**
 * @brief The box around the shape in its own space (before its centre and rotation).
 * @param self The shape; must not be NULL.
 * @param outMin [out] Receives the box's minimum corner. Must not be NULL.
 * @param outMax [out] Receives the box's maximum corner. Must not be NULL.
 * @returns false for an unbounded plane (it has no box; both corners are then zero).
 */
bool CollisionShape_TryGetLocalBounds(const CollisionShape* self, Vector3* outMin, Vector3* outMax);

/**
 * @brief Whether a name is a valid set or part name: [a-z][a-z0-9_.-]*, 1 to COLLISION_NAME_MAX_LENGTH bytes.
 * @param name The name, NUL-terminated; NULL is not valid.
 * @returns true when it is valid.
 */
bool CollisionShape_IsValidName(const unsigned char* name);

/**
 * @brief Returns a shape type's name ("convex_hull").
 * @param type The type.
 * @returns The name (static), or NULL for a value that is not a type.
 */
const unsigned char* CollisionShapeType_GetName(CollisionShapeType type);


// Functions: convex hulls.
/**
 * @brief Builds the convex hull of a set of points.
 *
 * Quickhull in double precision; points within a small tolerance of a face (relative to the points' size) count as on
 * it, and coplanar triangles are merged into polygon faces. A hull with more than @p maxVertices vertices is reduced:
 * the points farthest along a spread of directions are kept and the hull is rebuilt from them, so the extremes stay.
 * @param points The points; borrowed. Must not be NULL unless @p count is 0.
 * @param count How many (4 to COLLISION_HULL_MAX_INPUT_POINTS).
 * @param maxVertices The most vertices the hull may keep (COLLISION_HULL_MIN_VERTICES to COLLISION_HULL_MAX_VERTICES).
 * @param outHull [out] Receives the hull (owned by the caller: CollisionHull_Destroy). NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange for too few or too many points, a bad maximum or a point that is not
 *          finite; ErrorCode_InvalidOperation when the points span no volume (they lie on a plane, a line or a point).
 */
Error CollisionHull_Create(const Vector3* points, size_t count, uint32_t maxVertices, CollisionHull** outHull);

/**
 * @brief Builds the convex hull of a model's vertices after its import transform (every mesh of it).
 * @param model The model (its CPU-side vertices); must not be NULL.
 * @param maxVertices As for CollisionHull_Create.
 * @param outHull [out] Receives the hull (owned by the caller). NULL on failure. Must not be NULL.
 * @returns Success, or ErrorCode_InvalidAssetData for a model without vertices or with more than
 *          COLLISION_HULL_MAX_INPUT_POINTS of them.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error CollisionHull_CreateFromModel(const GameModel* model, uint32_t maxVertices, CollisionHull** outHull);

/**
 * @brief Releases a hull.
 * @param self The hull; NULL does nothing.
 */
void CollisionHull_Destroy(CollisionHull* self);


// Functions: meshes.
/**
 * @brief Whether an edge two triangles share is active (see CollisionMeshTriangle): it is, unless the two lie flat
 *        across it (their normals within 5 degrees) or fold concave seen from the first one's front. The rule every
 *        collision mesh marks its edges by; a terrain marks its own with it over the whole terrain (TerrainMesh.h).
 *        The triangles must share the edge with consistent winding (it runs opposite ways in the two); an edge that
 *        does not is active, which the caller decides.
 * @param normal The first triangle's unit normal.
 * @param otherNormal The second triangle's unit normal.
 * @param edgePoint A point of the edge.
 * @param otherOpposite The second triangle's corner that is not on the edge.
 * @returns true when the edge is active.
 */
bool CollisionMesh_IsSharedEdgeActive(Vector3 normal, Vector3 otherNormal, Vector3 edgePoint, Vector3 otherOpposite);

/**
 * @brief Builds a collision mesh from parts, each transformed by @p transform.
 *
 * Welds vertices with the same position, skips triangles with a repeated vertex or no area, marks each edge active or
 * not (see CollisionMeshTriangle; a part that gives its own activity keeps it) and builds the BVH.
 * @param parts The parts; borrowed. Must not be NULL unless @p partCount is 0.
 * @param partCount How many.
 * @param transform Applied to every vertex (a model's import transform; MatrixIdentity() for none).
 * @param outMesh [out] Receives the mesh (owned by the caller: CollisionMesh_Destroy). NULL on failure. Must not be
 *        NULL.
 * @returns Success; ErrorCode_InvalidAssetData for no triangle with an area, more than COLLISION_MESH_MAX_TRIANGLES
 *          triangles, an index out of range or a vertex that is not finite.
 */
Error CollisionMesh_Create(const CollisionMeshPart* parts, size_t partCount, Matrix transform, CollisionMesh** outMesh);

/**
 * @brief Builds a collision mesh from every mesh of a model, after its import transform.
 * @param model The model (its CPU-side vertices); must not be NULL.
 * @param outMesh [out] Receives the mesh (owned by the caller). NULL on failure. Must not be NULL.
 * @returns As CollisionMesh_Create.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error CollisionMesh_CreateFromModel(const GameModel* model, CollisionMesh** outMesh);

/**
 * @brief Releases a mesh.
 * @param self The mesh; NULL does nothing.
 */
void CollisionMesh_Destroy(CollisionMesh* self);


// Functions: sets.
/**
 * @brief Returns a set's name.
 * @param self The set; must not be NULL.
 * @returns The name (borrowed: valid while the set's owner is).
 */
static inline const unsigned char* CollisionSet_GetName(const CollisionSet* self)
{
    return self->_name;
}

/**
 * @brief Returns how many shapes a set has.
 * @param self The set; must not be NULL.
 * @returns 1 to COLLISION_MAX_SET_SHAPES.
 */
static inline uint32_t CollisionSet_GetShapeCount(const CollisionSet* self)
{
    return self->_shapeCount;
}

/**
 * @brief Returns one of a set's shapes.
 * @param self The set; must not be NULL.
 * @param index The shape's index.
 * @returns The shape (borrowed: valid while the set's owner is), or NULL for an index past the last shape.
 */
static inline const CollisionShape* CollisionSet_GetShape(const CollisionSet* self, uint32_t index)
{
    return (index < self->_shapeCount) ? &self->_shapes[index] : NULL;
}
