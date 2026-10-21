#pragma once
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include "raylib/raylib.h"
#include "raylib/raymath.h"

/*
 * Private header (source/): the small maths every collision module shares (tests/CollisionGeometryTests.c).
 *
 * CollisionAffine is an affine transform kept as its three axis columns and a translation: a shape's own space to the
 * world, through the object's world matrix (which may scale unevenly and, under parents, shear), the collider's own
 * transform and the shape's centre and rotation. The collision maths never assumes it is a rotation: support functions
 * map directions through its transpose, rays through its inverse and normals through the inverse's transpose, so an
 * unevenly scaled sphere is exactly an ellipsoid and a sheared box exactly a parallelepiped.
 *
 * CollisionPose is a rigid pose (a position and a unit quaternion) for motions: a rotating cast interpolates between
 * two of them (the position linearly, the rotation by slerp about the pose's own origin).
 */


// Macros.
/** Lengths below this count as zero when a direction is normalised. */
#define COLLISION_MATH_EPSILON 1e-9f
/** A transform is a similarity (a rotation, a mirror and one uniform scale) when its columns agree within this share of
 *  their length, in length and in orthogonality. */
#define COLLISION_SIMILARITY_TOLERANCE 1e-4f


// Types.
/** An affine transform: point p goes to X * p.x + Y * p.y + Z * p.z + Translation. */
typedef struct CollisionAffineStruct
{
    /** The images of the local X, Y and Z axes (the linear part's columns). */
    Vector3 X;
    Vector3 Y;
    Vector3 Z;
    /** Where the local origin goes. */
    Vector3 Translation;
} CollisionAffine;

/** A rigid pose: a position and a unit rotation. */
typedef struct CollisionPoseStruct
{
    Vector3 Position;
    Quaternion Rotation;
} CollisionPose;


// Functions: vectors.
static inline Vector3 CollisionMath_Vector(float x, float y, float z)
{
    return (Vector3){ .x = x, .y = y, .z = z };
}

static inline float CollisionMath_Dot(Vector3 a, Vector3 b)
{
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

static inline Vector3 CollisionMath_Cross(Vector3 a, Vector3 b)
{
    return (Vector3){ .x = (a.y * b.z) - (a.z * b.y), .y = (a.z * b.x) - (a.x * b.z), .z = (a.x * b.y) - (a.y * b.x) };
}

static inline Vector3 CollisionMath_Add(Vector3 a, Vector3 b)
{
    return (Vector3){ .x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z };
}

static inline Vector3 CollisionMath_Subtract(Vector3 a, Vector3 b)
{
    return (Vector3){ .x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z };
}

static inline Vector3 CollisionMath_Scale(Vector3 v, float scale)
{
    return (Vector3){ .x = v.x * scale, .y = v.y * scale, .z = v.z * scale };
}

/* a + b * scale. */
static inline Vector3 CollisionMath_AddScaled(Vector3 a, Vector3 b, float scale)
{
    return (Vector3){ .x = a.x + (b.x * scale), .y = a.y + (b.y * scale), .z = a.z + (b.z * scale) };
}

static inline Vector3 CollisionMath_Negate(Vector3 v)
{
    return (Vector3){ .x = -v.x, .y = -v.y, .z = -v.z };
}

static inline float CollisionMath_LengthSquared(Vector3 v)
{
    return CollisionMath_Dot(v, v);
}

static inline float CollisionMath_Length(Vector3 v)
{
    return sqrtf(CollisionMath_Dot(v, v));
}

/* The unit vector along @p v, or @p fallback when @p v is (nearly) zero. */
static inline Vector3 CollisionMath_NormalizeOr(Vector3 v, Vector3 fallback)
{
    float Length = CollisionMath_Length(v);
    return (Length > COLLISION_MATH_EPSILON) ? CollisionMath_Scale(v, 1.0f / Length) : fallback;
}

static inline Vector3 CollisionMath_Min(Vector3 a, Vector3 b)
{
    return (Vector3){ .x = fminf(a.x, b.x), .y = fminf(a.y, b.y), .z = fminf(a.z, b.z) };
}

static inline Vector3 CollisionMath_Max(Vector3 a, Vector3 b)
{
    return (Vector3){ .x = fmaxf(a.x, b.x), .y = fmaxf(a.y, b.y), .z = fmaxf(a.z, b.z) };
}

static inline Vector3 CollisionMath_Lerp(Vector3 a, Vector3 b, float t)
{
    return CollisionMath_AddScaled(a, CollisionMath_Subtract(b, a), t);
}

static inline bool CollisionMath_IsFinite(Vector3 v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

/* The largest of the three components' absolute values. */
static inline float CollisionMath_MaxAbsComponent(Vector3 v)
{
    return fmaxf(fabsf(v.x), fmaxf(fabsf(v.y), fabsf(v.z)));
}

/* Two unit vectors that complete the unit vector @p normal into a right-handed orthonormal basis (u, v, normal). */
void CollisionMath_GetBasis(Vector3 normal, Vector3* outU, Vector3* outV);

/* The point of segment [a, b] nearest to @p point, as a parameter in [0, 1]. A zero-length segment gives 0. */
float CollisionMath_GetClosestSegmentParameter(Vector3 a, Vector3 b, Vector3 point);

/* The closest points of segments [p1, q1] and [p2, q2], as parameters in [0, 1] (Ericson 5.1.9; parallel segments give
 * one of the closest pairs). */
void CollisionMath_GetClosestSegmentParameters(Vector3 p1, Vector3 q1, Vector3 p2, Vector3 q2, float* outS,
    float* outT);

/* The point of triangle (a, b, c) nearest to @p point, with its barycentric weights (u, v, w) for (a, b, c), and the
 * region it lies in: 0 the face, 1..3 the edges ab, bc, ca, 4..6 the vertices a, b, c (Ericson 5.1.5). A degenerate
 * triangle falls back to its nearest edge. */
Vector3 CollisionMath_GetClosestTrianglePoint(Vector3 a, Vector3 b, Vector3 c, Vector3 point, Vector3* outWeights,
    uint32_t* outRegion);


// Functions: affine transforms.
static inline CollisionAffine CollisionAffine_Identity(void)
{
    return (CollisionAffine)
    {
        .X = { 1.0f, 0.0f, 0.0f }, .Y = { 0.0f, 1.0f, 0.0f }, .Z = { 0.0f, 0.0f, 1.0f }, .Translation = { 0.0f, 0.0f, 0.0f }
    };
}

/* The linear part applied to @p v (a direction or an offset: no translation). */
static inline Vector3 CollisionAffine_TransformVector(const CollisionAffine* self, Vector3 v)
{
    return (Vector3)
    {
        .x = (self->X.x * v.x) + (self->Y.x * v.y) + (self->Z.x * v.z),
        .y = (self->X.y * v.x) + (self->Y.y * v.y) + (self->Z.y * v.z),
        .z = (self->X.z * v.x) + (self->Y.z * v.y) + (self->Z.z * v.z)
    };
}

static inline Vector3 CollisionAffine_TransformPoint(const CollisionAffine* self, Vector3 p)
{
    return CollisionMath_Add(CollisionAffine_TransformVector(self, p), self->Translation);
}

/* The linear part's transpose applied to @p d: how a world direction reads in the local space for support functions
 * (support_world(d) = A * support_local(A^T d) + t). */
static inline Vector3 CollisionAffine_TransposeTransformVector(const CollisionAffine* self, Vector3 d)
{
    return (Vector3){ .x = CollisionMath_Dot(self->X, d), .y = CollisionMath_Dot(self->Y, d),
        .z = CollisionMath_Dot(self->Z, d) };
}

/* outer after inner: a point goes through @p inner first, then through @p outer. */
CollisionAffine CollisionAffine_Multiply(const CollisionAffine* outer, const CollisionAffine* inner);

/* The determinant of the linear part (negative for a mirror, 0 for a flattened space). */
float CollisionAffine_GetDeterminant(const CollisionAffine* self);

/* The inverse transform; false (and identity) when the linear part is singular or nearly so (a zero scale on an
 * axis). */
bool CollisionAffine_TryInvert(const CollisionAffine* self, CollisionAffine* outInverse);

/* Whether the linear part is a rotation (or a mirror) times one uniform scale, within COLLISION_SIMILARITY_TOLERANCE;
 * *outScale receives that scale (the mean column length) either way. */
bool CollisionAffine_IsSimilarity(const CollisionAffine* self, float* outScale);

/* The largest length any unit vector gets from the linear part (an upper bound: the Frobenius norm, tight for
 * similarities). */
float CollisionAffine_GetMaxStretch(const CollisionAffine* self);

/* A raylib matrix (MatrixMultiply order, Vector3Transform convention) as an affine transform, and back. */
CollisionAffine CollisionAffine_FromMatrix(Matrix matrix);
Matrix CollisionAffine_ToMatrix(const CollisionAffine* self);

/* The transform of a centre and an Euler XYZ rotation in radians (raylib's MatrixRotateXYZ), no scale. */
CollisionAffine CollisionAffine_FromCenterRotation(Vector3 center, Vector3 rotation);

/* The world box around the local box [@p min, @p max] under @p self. */
void CollisionAffine_TransformBounds(const CollisionAffine* self, Vector3 min, Vector3 max, Vector3* outMin,
    Vector3* outMax);


// Functions: poses.
static inline CollisionPose CollisionPose_Identity(void)
{
    return (CollisionPose){ .Position = { 0.0f, 0.0f, 0.0f }, .Rotation = { 0.0f, 0.0f, 0.0f, 1.0f } };
}

/* The pose's transform (rotation, then translation). */
CollisionAffine CollisionPose_ToAffine(const CollisionPose* self);

/* The pose at @p t of the motion from @p from to @p to: the position linearly, the rotation by slerp along the shorter
 * arc. */
CollisionPose CollisionPose_Interpolate(const CollisionPose* from, const CollisionPose* to, float t);

/* The angle, in radians in [0, pi], the motion from @p from to @p to turns through. */
float CollisionPose_GetRotationAngle(const CollisionPose* from, const CollisionPose* to);
