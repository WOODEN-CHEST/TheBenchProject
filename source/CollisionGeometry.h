#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "CollisionMath.h"
#include "CollisionShape.h"

/*
 * Private header (source/): the collision narrowphase, pure functions over shapes in world space, tested headless
 * (tests/CollisionGeometryTests.c). The collision world (WorldCollision) builds these views from its instances and runs
 * every query through them.
 *
 * CONVEX VIEWS. A CollisionConvex is one convex shape placed in the world: a kind (a point or a segment with a rounding
 * radius for spheres and capsules, a box, a cylinder, a hull, a triangle), its local sizes and its local-to-world
 * affine transform. Its support function maps a world direction d through the transform's transpose, takes the local
 * shape's farthest point along it, and maps that back (support(d) = A * s(A^T d) + t), so every shape takes any affine
 * transform exactly. Under a similarity (rotation, mirror, uniform scale) a sphere or capsule keeps its radius apart (its
 * CORE is a point or a segment, and GJK on cores stays exact and fast for shallow contacts, Box2D's trick); under an
 * uneven scale the rounding goes into the support (an ellipsoid).
 *
 * THE ALGORITHMS.
 * - GJK distance (Gilbert-Johnson-Keerthi, Johnson's sub-algorithm by Voronoi regions): the closest points of two convex
 *   shapes, their distance and the normal; with core radii a shallow overlap of spheres and capsules comes out exact.
 * - EPA (the expanding polytope): the penetration depth, normal and deepest points of overlapping shapes, from GJK's
 *   simplex (blown up to a tetrahedron when it ended flat).
 * - Manifolds: the two shapes' supporting faces along the normal, one clipped against the other's sides (Sutherland-
 *   Hodgman), the points' separations along the normal, at most COLLISION_MAX_CONTACT_POINTS kept (the deepest, then
 *   the largest area), each with a feature id (which faces, which vertex or clipping plane) that stays the same while the
 *   contact keeps its topology.
 * - Rays: analytic in each shape's own space (the ray goes through the inverse transform; its parameter is the same in
 *   both spaces), normals back through the inverse's transpose.
 * - Linear casts: the GJK ray cast (van den Bergen, 2004) of the moving shape against the other: the exact time of
 *   impact.
 * - Rotating casts: conservative advancement (Mirtich): step forward by the distance over a bound of how fast the
 *   shapes can close (the linear motion along the normal plus the turn times the moving shape's radius about its pose),
 *   so no hit is skipped whatever the speed.
 * - Planes: an unbounded plane is a half-space with its own closed forms; a bounded one is its solid slab as a box view
 *   with the one-sided rules applied (CollisionPlaneView).
 * - Triangles carry their mesh's active-edge bits: a contact on an inactive edge takes the face normal (no catching on
 *   seams), and a one-sided triangle ignores what lies behind it.
 *
 * Every normal of a contact or a cast points from A to B: moving B along it (or A against it) by the depth separates
 * them. Separations are signed: above 0 apart, below 0 penetrating.
 */


// Macros.
/** @brief The most points a contact manifold keeps. */
#define COLLISION_MAX_CONTACT_POINTS 4U
/** @brief The most points a supporting face may have (a hull face is cut down to it). */
#define COLLISION_MAX_FACE_POINTS 32U
/** @brief The points of a cylinder cap's polygon in manifolds. */
#define COLLISION_CYLINDER_CAP_POINTS 16U
/** @brief The most iterations of GJK, EPA and conservative advancement. */
#define COLLISION_GJK_MAX_ITERATIONS 64U
#define COLLISION_EPA_MAX_ITERATIONS 128U
#define COLLISION_TOI_MAX_ITERATIONS 64U
/** @brief GJK stops when a step improves the squared distance by less than this share (relative tolerance). */
#define COLLISION_GJK_RELATIVE_TOLERANCE 1e-5f
/** @brief Cores closer than this (metres) count as touching: GJK reports overlap and EPA takes over. */
#define COLLISION_GJK_OVERLAP_DISTANCE 1e-5f
/** @brief EPA stops when the support passes the closest face by less than this (metres). */
#define COLLISION_EPA_TOLERANCE 1e-4f
/** @brief A cast's time of impact is accurate to this distance (metres). */
#define COLLISION_CAST_TOLERANCE 1e-4f
/** @brief Conservative advancement stops this far before contact (metres): the shapes are left just apart. */
#define COLLISION_TOI_TARGET 5e-4f
/** A cast starting in an overlap no deeper than this (metres: a shape left where the last cast stopped, within rounding)
 *  is blocked only by a motion that goes deeper; moving away from the contact or along it is free. */
#define COLLISION_SHALLOW_START_DEPTH COLLISION_CAST_TOLERANCE


// Types.
/** The kinds of convex views. */
typedef enum CollisionConvexKindEnum
{
    /** A point with Radius: a sphere. */
    CollisionConvexKind_Point,
    /** A segment from (0, -HalfHeight, 0) to (0, HalfHeight, 0) with Radius: a capsule. */
    CollisionConvexKind_Segment,
    /** A box of HalfExtents around the origin (a plane's slab is one too). */
    CollisionConvexKind_Box,
    /** A cylinder of CylinderRadius and HalfHeight along local Y. */
    CollisionConvexKind_Cylinder,
    /** A convex hull. */
    CollisionConvexKind_Hull,
    /** A triangle given in world space (Transform is the identity). */
    CollisionConvexKind_Triangle
} CollisionConvexKind;

/** One convex shape placed in the world. Fill the kind, sizes and Transform, then call CollisionConvex_Prepare. */
typedef struct CollisionConvexStruct
{
    CollisionConvexKind Kind;
    /** Local to world. */
    CollisionAffine Transform;
    /** Point and Segment: the local rounding radius. 0 for the other kinds. */
    float Radius;
    /** Box: the half extents. */
    Vector3 HalfExtents;
    /** Segment and Cylinder: the half height. */
    float HalfHeight;
    /** Cylinder: the radius. */
    float CylinderRadius;
    /** Hull: the hull (borrowed). */
    const CollisionHull* Hull;
    /** Triangle: its world corners, counter-clockwise seen from its front. */
    Vector3 Vertices[3];
    /** Triangle: the active-edge bits (CollisionMeshTriangle) and whether it collides from its front only. */
    uint8_t ActiveEdges;
    bool IsOneSided;

    /* Filled by CollisionConvex_Prepare. */
    /** World to local, for the faces' normals and the rays. */
    CollisionAffine Inverse;
    /** Whether Transform is a similarity: the rounding is then kept apart as CoreRadius. */
    bool IsSimilarity;
    /** World: Radius times the uniform scale under a similarity, else 0 (the rounding is in the support). */
    float CoreRadius;
    /** World: a point inside the shape (its centre). */
    Vector3 Center;
    /** World: the largest distance of the shape's points from Transform's origin (for rotating casts). */
    float BoundingRadius;
    /** Triangle: the unit front normal. */
    Vector3 TriangleNormal;
    /** Whether the view could be prepared: false for a transform that flattens the shape. */
    bool IsValid;
} CollisionConvex;

/** One vertex of a GJK simplex: the support points of A and B and their difference. */
typedef struct CollisionSimplexVertexStruct
{
    Vector3 A;
    Vector3 B;
    Vector3 W; /* A - B */
} CollisionSimplexVertex;

/** A GJK simplex: 1 to 4 vertices with the barycentric weights of the closest point. */
typedef struct CollisionSimplexStruct
{
    CollisionSimplexVertex Vertices[4];
    float Weights[4];
    uint32_t Count;
} CollisionSimplex;

/** The result of CollisionGeometry_GetDistance. */
typedef struct CollisionDistanceStruct
{
    /** The cores are closer than COLLISION_GJK_OVERLAP_DISTANCE: Distance, the points and the normal are not valid; use
     *  CollisionGeometry_GetPenetration. */
    bool AreCoresOverlapping;
    /** Between the full shapes: the cores' distance less both core radii (below 0 for a shallow overlap of rounded
     *  shapes, whose normal and points are still exact). */
    float Distance;
    /** The closest points on the full shapes (for a shallow overlap: the deepest points). */
    Vector3 PointA;
    Vector3 PointB;
    /** Unit, from A to B. */
    Vector3 Normal;
    /** The final simplex (on the cores). */
    CollisionSimplex Simplex;
} CollisionDistance;

/** The result of CollisionGeometry_GetPenetration. */
typedef struct CollisionPenetrationStruct
{
    /** Unit, from A to B: moving B along it by Depth separates them. */
    Vector3 Normal;
    /** At least 0. */
    float Depth;
    /** The deepest point of A inside B (on A's surface) and the matching point on B's surface. */
    Vector3 PointA;
    Vector3 PointB;
} CollisionPenetration;

/** One point of a contact. */
typedef struct CollisionContactPointStruct
{
    /** World positions on A's and on B's surface. */
    Vector3 PositionA;
    Vector3 PositionB;
    /** Along the contact's normal: (PositionB - PositionA) . Normal. Below 0 penetrating. */
    float Separation;
    /** Which features made the point (stable while the contact keeps its topology). */
    uint32_t FeatureId;
} CollisionContactPoint;

/** A contact manifold between two shapes. */
typedef struct CollisionContactStruct
{
    /** Unit, from A to B. */
    Vector3 Normal;
    /** The smallest of the points' separations. */
    float Separation;
    /** 0 (no contact within the asked separation) to COLLISION_MAX_CONTACT_POINTS. */
    uint32_t PointCount;
    CollisionContactPoint Points[COLLISION_MAX_CONTACT_POINTS];
} CollisionContact;

/** The result of a ray against one shape, in world terms. */
typedef struct CollisionRayHitStruct
{
    /** The ray's parameter at the hit: origin + T * direction. */
    float T;
    /** The surface's unit normal, facing the ray (for a start inside: against the ray). */
    Vector3 Normal;
    /** The ray started inside a solid shape (T is 0). */
    bool IsStartInside;
    /** Meshes: the triangle hit (its index in the mesh) and the barycentric weights of its corners. */
    uint32_t TriangleIndex;
    Vector3 Barycentric;
} CollisionRayHit;

/** The result of a cast of A against B. */
typedef struct CollisionCastHitStruct
{
    /** The motion's fraction at the time of impact, in [0, 1]. */
    float Fraction;
    /** Unit, from A to B at the impact (the hit surface faces along its negation). */
    Vector3 Normal;
    /** The touching points at the impact, on A and on B. */
    Vector3 PointA;
    Vector3 PointB;
    /** A already overlapped B at the start: Fraction 0, Normal and PenetrationDepth from EPA (the push-out). */
    bool IsStartPenetrating;
    float PenetrationDepth;
} CollisionCastHit;

/** A plane in world space: its front normal and surface, and for a bounded one the slab behind its rectangle. */
typedef struct CollisionPlaneViewStruct
{
    /** The unit front normal. */
    Vector3 Normal;
    /** A point on the surface (the rectangle's centre). */
    Vector3 Origin;
    /** Whether it is a rectangle (with the slab below) or a half-space. */
    bool IsBounded;
    /** Bounded: the solid slab behind the rectangle (a box view; at depth 0 a flat box). */
    CollisionConvex Slab;
    /** Bounded: the rectangle's corners, counter-clockwise seen from the front (clockwise under a mirror). */
    Vector3 Corners[4];
    /** Bounded: how far behind the surface the slab's back face lies, along the normal (world metres, at least 0). A
     *  shape whose centre lies behind the back face does not touch the plane: rooms below a floor stay free, and at
     *  depth 0 the plane is one-way (crossed from behind, it pushes to the front once the centre is through). */
    float BackDistance;
} CollisionPlaneView;


// Functions: views.
/* Fills a view's derived fields; IsValid false when the transform flattens it (no inverse). */
void CollisionConvex_Prepare(CollisionConvex* self);

/* The farthest point of the core along @p direction (the rounding kept apart under a similarity). */
Vector3 CollisionConvex_SupportCore(const CollisionConvex* self, Vector3 direction);

/* The farthest point of the full shape along @p direction. */
Vector3 CollisionConvex_SupportFull(const CollisionConvex* self, Vector3 direction);

/* The view of a shape (not a plane or a mesh) under @p shapeToWorld; false when the shape type has no convex view or
 * the transform flattens it. */
bool CollisionConvex_FromShape(const CollisionShape* shape, const CollisionAffine* shapeToWorld,
    CollisionConvex* outConvex);

/* A triangle view from a mesh's triangle under @p meshToWorld. */
void CollisionConvex_FromMeshTriangle(const CollisionMesh* mesh, uint32_t triangle, const CollisionAffine* meshToWorld,
    bool isTwoSided, CollisionConvex* outConvex);

/* The view of a plane shape under @p shapeToWorld; false when the transform flattens it. */
bool CollisionPlaneView_FromShape(const CollisionShape* shape, const CollisionAffine* shapeToWorld,
    CollisionPlaneView* outPlane);

/* Whether @p convex is a one-sided triangle with @p point behind its plane: a shape centred there never touches it
 * (the contacts' rule, which the casts share). */
bool CollisionConvex_IsBehindOneSided(const CollisionConvex* convex, Vector3 point);


// Functions: distance and penetration.
void CollisionGeometry_GetDistance(const CollisionConvex* a, const CollisionConvex* b, CollisionDistance* outDistance);

/* The penetration of two overlapping shapes, from the simplex GJK left (on the full shapes); false when it could not
 * be found (the shapes only touch: Depth 0 and a fallback normal are written). */
bool CollisionGeometry_GetPenetration(const CollisionConvex* a, const CollisionConvex* b,
    CollisionPenetration* outPenetration);


// Functions: contacts.
/* The contact manifold of two convex shapes, its points out to @p maxSeparation; false (no points) when they are
 * farther apart. Triangles apply their active edges and sides. */
bool CollisionGeometry_Collide(const CollisionConvex* a, const CollisionConvex* b, float maxSeparation,
    CollisionContact* outContact);

/* The contact of a plane (A) and a convex shape (B), out to @p maxSeparation. Within the rectangle (or anywhere, for a
 * half-space) the normal is the plane's front: whatever lies behind is pushed to the front. At a bounded plane's edges
 * the contact is the slab's own. */
bool CollisionGeometry_CollidePlane(const CollisionPlaneView* plane, const CollisionConvex* b, float maxSeparation,
    CollisionContact* outContact);


// Functions: rays (direction need not be unit; T is the parameter along it).
bool CollisionGeometry_RaycastShape(const CollisionShape* shape, const CollisionAffine* shapeToWorld,
    const CollisionAffine* worldToShape, Vector3 origin, Vector3 direction, float maxT, CollisionRayHit* outHit);

/* A ray against a world triangle; two-sided or front only. */
bool CollisionGeometry_RaycastTriangle(Vector3 origin, Vector3 direction, Vector3 a, Vector3 b, Vector3 c,
    bool isTwoSided, float maxT, CollisionRayHit* outHit);


// Functions: casts.
/* A cast hit on a one-sided triangle @p target whose normal (from the cast shape into the triangle) points along the
 * triangle's front is turned to face it: such a triangle is only ever touched from its front. */
void CollisionConvex_FaceOneSided(const CollisionConvex* target, CollisionCastHit* hit);

/* A moved by @p translation against B: the first time they touch (within COLLISION_CAST_TOLERANCE). A start already
 * that close is a hit at fraction 0 only when the motion closes on B; overlapping at the start is a start hit with the
 * push-out (a shallow one, COLLISION_SHALLOW_START_DEPTH, only when the motion goes deeper). A one-sided triangle B is
 * passed from behind. */
bool CollisionGeometry_Cast(const CollisionConvex* a, Vector3 translation, const CollisionConvex* b,
    CollisionCastHit* outHit);

/* A moved by @p translation against a plane: the first time they touch (a plane is crossed only from its front). */
bool CollisionGeometry_CastPlane(const CollisionConvex* a, Vector3 translation, const CollisionPlaneView* plane,
    CollisionCastHit* outHit);

/* A (its Transform relative to the pose) moving from pose @p from to pose @p to, rotation included, against B (at
 * rest): the first time they come within COLLISION_TOI_TARGET, by conservative advancement (a start closer than that
 * stops at half its own gap, and only when the motion closes on B); overlapping at the start is a start hit. */
bool CollisionGeometry_CastMotion(const CollisionConvex* aInPose, const CollisionPose* from, const CollisionPose* to,
    const CollisionConvex* b, CollisionCastHit* outHit);

/* The same against a plane. */
bool CollisionGeometry_CastMotionPlane(const CollisionConvex* aInPose, const CollisionPose* from,
    const CollisionPose* to, const CollisionPlaneView* plane, CollisionCastHit* outHit);


// Functions: meshes.
/* Calls @p visit for every triangle of @p mesh whose box overlaps the local box [@p min, @p max]; stops early when it
 * returns false. */
typedef bool (*CollisionTriangleVisitor)(void* context, uint32_t triangle);
void CollisionGeometry_QueryMesh(const CollisionMesh* mesh, Vector3 min, Vector3 max, CollisionTriangleVisitor visit,
    void* context);

/* The nearest hit of a ray (in the mesh's own space) against a mesh's triangles. */
bool CollisionGeometry_RaycastMesh(const CollisionMesh* mesh, Vector3 origin, Vector3 direction, float maxT,
    bool isTwoSided, CollisionRayHit* outHit);
