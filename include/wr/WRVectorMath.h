#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "WRMath.h"


/**
 * Double-precision 2D and 3D vectors (Vector2Double, Vector3Double), 4x4 matrices (Matrix4Double) and axis-aligned
 * boxes (BoundingBoxDouble), for worlds large enough that float positions lose precision: a float's step is 1 mm at
 * 8 km from the origin and 8 mm at 100 km, a double's is 2 nm at 10 000 km.
 *
 * The conventions are those of raymath (raylib's maths library), in double, so float code written with raymath ports
 * operand for operand:
 *   - Matrices are column-major, the order of OpenGL and of raymath's MatrixToFloatV: row r of column c is
 *     M[c * 4 + r], the translation is M[12], M[13] and M[14], and points are column vectors (a matrix times a point).
 *   - Matrix4Double_Multiply(a, b) applies a first, then b: it is the product b * a, as raymath's MatrixMultiply(a, b)
 *     and C#'s System.Numerics Matrix4x4.Multiply(a, b) are. An object's world matrix is Multiply(its local matrix,
 *     its parent's world matrix).
 *   - Angles are in radians. Matrix4Double_FromRotationXYZ is raymath's MatrixRotateXYZ.
 *   - Points, directions and boxes are moved as by an affine matrix: the bottom row is ignored and there is no
 *     perspective divide, as in raymath's Vector3Transform.
 *
 * Large worlds keep their positions in double and make floats of them only relative to a nearby origin: the camera
 * for rendering, a query's start for collision. Vector3Double_ToFloatsRelative and Matrix4Double_ToFloatsRelative
 * subtract the origin in double and round once, so a point 0.1 m from a camera 10 000 km away is still 0.1 m to
 * within float rounding. Rounding to float first and subtracting after keeps the float error of the absolute
 * position, which is what makes a far-away world shake.
 *
 * Every function is a pure function of its arguments: no state, nothing allocated, safe from any thread. Vectors and
 * boxes are passed and returned by value; matrices (128 bytes) are passed by pointer and returned by value. The
 * fallible operations (inversion, barycentric weights) return a bool and write their result through an out pointer.
 * Small functions are inline; the rest are in WRVectorMath.c: normalization and what builds on it (projections,
 * angles, length clamps, moves towards a target), refraction, the rotations, barycentric weights, the determinant,
 * inversion, the box transform and the ray test. Angles are in radians and follow the right-hand rule: in 2D a
 * positive angle turns the X axis towards the Y axis. NaN and infinite components are not checked unless a function
 * says so: test input that may be bad with the IsFinite functions.
 */


// Macros.
/** @brief The number of entries of a Matrix4Double: 4 rows times 4 columns. */
#define MATRIX4_DOUBLE_ENTRY_COUNT ((size_t)16)


// Types.
/**
 * @brief A 2D vector of doubles: a position, a direction, an offset or a size in a plane.
 *
 * A plain value type; copy it freely, it owns nothing. Build one with Vector2Double_Create, or from floats with
 * Vector2Double_FromFloats. Its functions are Vector3Double's in two dimensions, with the same conventions, plus the
 * 2D cross product.
 */
typedef struct Vector2DoubleStruct
{
    /** @brief The X component. */
    double X;
    /** @brief The Y component. */
    double Y;
} Vector2Double;

/**
 * @brief A 3D vector of doubles: a position, a direction, an offset or a size.
 *
 * A plain value type; copy it freely, it owns nothing. Build one with Vector3Double_Create, or from floats with
 * Vector3Double_FromFloats.
 */
typedef struct Vector3DoubleStruct
{
    /** @brief The X component. */
    double X;
    /** @brief The Y component. */
    double Y;
    /** @brief The Z component. */
    double Z;
} Vector3Double;

/**
 * @brief A 4x4 matrix of doubles, stored column by column.
 *
 * Row r of column c is M[c * 4 + r], and points are column vectors: a point p goes to the matrix times p. In an
 * affine matrix (a rotation, scale or shear followed by a translation) the first three columns are where the X, Y and
 * Z axes go, M[12], M[13] and M[14] are the translation, and the bottom row (M[3], M[7], M[11], M[15]) is
 * (0, 0, 0, 1). A plain value type; copy it freely, it owns nothing. Build one with Matrix4Double_Identity or the
 * Matrix4Double_From* functions.
 */
typedef struct Matrix4DoubleStruct
{
    /** @brief The entries, column-major: M[c * 4 + r] is row r of column c, the order of raymath's MatrixToFloatV. */
    double M[MATRIX4_DOUBLE_ENTRY_COUNT];
} Matrix4Double;

/**
 * @brief An axis-aligned box of doubles: every point from Min to Max on all three axes, its faces included.
 *
 * A plain value type; copy it freely, it owns nothing. Min is expected to be at most Max on every axis:
 * BoundingBoxDouble_FromCorners makes sure of it, and the union and transform functions keep it. A box whose Min
 * equals its Max on an axis is flat (a point when it is so on all three). Infinite bounds (a slab, a half-space) work
 * with the unions, Expand, the containment and overlap tests, BoundingBoxDouble_Transform and
 * BoundingBoxDouble_IntersectsRay; the measures (centre, extents, size, surface area) need finite bounds. NaN bounds
 * are not allowed.
 */
typedef struct BoundingBoxDoubleStruct
{
    /** @brief The smallest corner: the least X, Y and Z of the box. */
    Vector3Double Min;
    /** @brief The largest corner: the greatest X, Y and Z of the box. */
    Vector3Double Max;
} BoundingBoxDouble;


// Functions: 2D vectors.
/**
 * @brief Builds a 2D vector from its two components.
 * @param x The X component.
 * @param y The Y component.
 * @returns The vector (x, y).
 */
static inline Vector2Double Vector2Double_Create(double x, double y)
{
    return (Vector2Double){ .X = x, .Y = y };
}

/**
 * @brief Returns the zero 2D vector.
 * @returns (0, 0).
 */
static inline Vector2Double Vector2Double_Zero(void)
{
    return (Vector2Double){ .X = 0.0, .Y = 0.0 };
}

/**
 * @brief Builds a 2D vector from two floats, each widened to double exactly.
 * @param values The components in X, Y order. Must not be NULL and must hold 2 floats.
 * @returns The vector.
 */
static inline Vector2Double Vector2Double_FromFloats(const float values[2])
{
    return (Vector2Double){ .X = values[0], .Y = values[1] };
}

/**
 * @brief Rounds a 2D vector to two floats, each component to the nearest float.
 *
 * A float keeps only about seven significant digits (its step is 1 mm at 8 km), so for a position far from the
 * origin use Vector2Double_ToFloatsRelative instead.
 * @param self The vector.
 * @param out [out] Receives the components in X, Y order; a component beyond the float range becomes an infinity.
 *        Must not be NULL and must hold 2 floats.
 */
static inline void Vector2Double_ToFloats(Vector2Double self, float out[2])
{
    out[0] = (float)self.X;
    out[1] = (float)self.Y;
}

/**
 * @brief Rounds a 2D vector's offset from an origin to two floats: the difference is taken in double, then rounded
 *        once to the nearest float.
 *
 * This is how a far-away position becomes a float: relative to a nearby origin, so the float keeps the precision of
 * the offset instead of the absolute position's (see Vector3Double_ToFloatsRelative).
 * @param self The vector, typically a position.
 * @param origin The origin to subtract, typically the camera's position.
 * @param out [out] Receives self - origin in X, Y order. Must not be NULL and must hold 2 floats.
 */
static inline void Vector2Double_ToFloatsRelative(Vector2Double self, Vector2Double origin, float out[2])
{
    out[0] = (float)(self.X - origin.X);
    out[1] = (float)(self.Y - origin.Y);
}

/**
 * @brief Adds two 2D vectors component by component.
 * @param a The first vector.
 * @param b The second vector.
 * @returns a + b.
 */
static inline Vector2Double Vector2Double_Add(Vector2Double a, Vector2Double b)
{
    return (Vector2Double){ .X = a.X + b.X, .Y = a.Y + b.Y };
}

/**
 * @brief Subtracts one 2D vector from another component by component.
 * @param a The vector to subtract from.
 * @param b The vector to subtract.
 * @returns a - b: for two points, the offset from b to a.
 */
static inline Vector2Double Vector2Double_Subtract(Vector2Double a, Vector2Double b)
{
    return (Vector2Double){ .X = a.X - b.X, .Y = a.Y - b.Y };
}

/**
 * @brief Multiplies both components of a 2D vector by one number.
 * @param self The vector.
 * @param scale The factor.
 * @returns self * scale.
 */
static inline Vector2Double Vector2Double_Scale(Vector2Double self, double scale)
{
    return (Vector2Double){ .X = self.X * scale, .Y = self.Y * scale };
}

/**
 * @brief Multiplies two 2D vectors component by component (a per-axis scale).
 * @param a The first vector.
 * @param b The second vector.
 * @returns (a.X * b.X, a.Y * b.Y).
 */
static inline Vector2Double Vector2Double_Multiply(Vector2Double a, Vector2Double b)
{
    return (Vector2Double){ .X = a.X * b.X, .Y = a.Y * b.Y };
}

/**
 * @brief Adds a scaled 2D vector to another: a point moved along a direction, such as a ray's point at a distance.
 *
 * Each component is a + (b * scale), rounded after the product and after the sum.
 * @param a The vector to add to (the start).
 * @param b The vector to scale and add (the direction).
 * @param scale The factor for @p b (the distance along it).
 * @returns a + b * scale.
 */
static inline Vector2Double Vector2Double_AddScaled(Vector2Double a, Vector2Double b, double scale)
{
    return (Vector2Double){ .X = a.X + (b.X * scale), .Y = a.Y + (b.Y * scale) };
}

/**
 * @brief Negates both components of a 2D vector.
 * @param self The vector.
 * @returns -self.
 */
static inline Vector2Double Vector2Double_Negate(Vector2Double self)
{
    return (Vector2Double){ .X = -self.X, .Y = -self.Y };
}

/**
 * @brief The dot product of two 2D vectors.
 * @param a The first vector.
 * @param b The second vector.
 * @returns a.X * b.X + a.Y * b.Y.
 */
static inline double Vector2Double_Dot(Vector2Double a, Vector2Double b)
{
    return (a.X * b.X) + (a.Y * b.Y);
}

/**
 * @brief The 2D cross product of two vectors (raymath's Vector2CrossProduct): the Z component of the 3D cross product
 *        of (a.X, a.Y, 0) and (b.X, b.Y, 0).
 *
 * Positive when the turn from a to b goes the way the X axis turns to the Y axis, negative the other way, zero when
 * the vectors are parallel (or one is zero). Its magnitude is the area of the parallelogram they span: the sine of the
 * angle between them times their lengths.
 * @param a The first vector.
 * @param b The second vector.
 * @returns a.X * b.Y - a.Y * b.X.
 */
static inline double Vector2Double_Cross(Vector2Double a, Vector2Double b)
{
    return (a.X * b.Y) - (a.Y * b.X);
}

/**
 * @brief The squared length of a 2D vector: its dot product with itself. Cheaper than the length; compare squared
 *        lengths against squared limits.
 * @param self The vector.
 * @returns The squared length (>= 0, or NaN for a NaN component).
 */
static inline double Vector2Double_LengthSquared(Vector2Double self)
{
    return Vector2Double_Dot(self, self);
}

/**
 * @brief The length of a 2D vector.
 *
 * The square root of the squared length, so a vector whose components pass about 1e154 overflows to infinity, one
 * shorter than about 1e-154 loses precision (its square is subnormal), and one whose components are both below about
 * 1e-162 reads as 0. Vector2Double_NormalizeOr has none of these limits.
 * @param self The vector.
 * @returns The length (>= 0, or NaN for a NaN component).
 */
static inline double Vector2Double_Length(Vector2Double self)
{
    return Math_SqrtDouble(Vector2Double_Dot(self, self));
}

/**
 * @brief The squared distance between two 2D points.
 * @param a The first point.
 * @param b The second point.
 * @returns The squared length of b - a.
 */
static inline double Vector2Double_DistanceSquared(Vector2Double a, Vector2Double b)
{
    return Vector2Double_LengthSquared(Vector2Double_Subtract(b, a));
}

/**
 * @brief The distance between two 2D points.
 *
 * The points are subtracted first, so two points 10 000 km from the origin and 1 mm apart are 1 mm apart to within
 * the doubles' own precision.
 * @param a The first point.
 * @param b The second point.
 * @returns The length of b - a.
 */
static inline double Vector2Double_Distance(Vector2Double a, Vector2Double b)
{
    return Vector2Double_Length(Vector2Double_Subtract(b, a));
}

/**
 * @brief The unit vector in a 2D vector's direction, or a fallback when the vector has no direction.
 *
 * The fallback is returned for the zero vector and for a vector with a NaN or infinite component. Every other vector
 * has a direction, however short or long: it is divided by its larger component's magnitude before its length is
 * taken, so the length can neither underflow nor overflow. Within a few rounding errors the result's length is 1.
 * @param self The vector to normalize.
 * @param fallback The vector to return when @p self has no direction; returned as given (it need not be a unit
 *        vector).
 * @returns The unit vector along @p self, or @p fallback.
 */
Vector2Double Vector2Double_NormalizeOr(Vector2Double self, Vector2Double fallback);

/**
 * @brief Interpolates linearly between two 2D vectors: a + (b - a) * amount, component by component.
 *
 * The amount is not clamped: 0 gives a, 1 gives b (to within rounding), and amounts outside [0, 1] extrapolate.
 * @param a The vector at amount 0.
 * @param b The vector at amount 1.
 * @param amount The interpolation factor.
 * @returns The interpolated vector.
 */
static inline Vector2Double Vector2Double_Lerp(Vector2Double a, Vector2Double b, double amount)
{
    return (Vector2Double){ .X = Math_LerpDouble(a.X, b.X, amount), .Y = Math_LerpDouble(a.Y, b.Y, amount) };
}

/**
 * @brief The smaller of two 2D vectors' components, axis by axis.
 *
 * NaN components are not special-cased (see Math_MinDouble).
 * @param a The first vector.
 * @param b The second vector.
 * @returns (min(a.X, b.X), min(a.Y, b.Y)).
 */
static inline Vector2Double Vector2Double_Min(Vector2Double a, Vector2Double b)
{
    return (Vector2Double){ .X = Math_MinDouble(a.X, b.X), .Y = Math_MinDouble(a.Y, b.Y) };
}

/**
 * @brief The larger of two 2D vectors' components, axis by axis.
 *
 * NaN components are not special-cased (see Math_MaxDouble).
 * @param a The first vector.
 * @param b The second vector.
 * @returns (max(a.X, b.X), max(a.Y, b.Y)).
 */
static inline Vector2Double Vector2Double_Max(Vector2Double a, Vector2Double b)
{
    return (Vector2Double){ .X = Math_MaxDouble(a.X, b.X), .Y = Math_MaxDouble(a.Y, b.Y) };
}

/**
 * @brief Whether two 2D vectors are equal within an absolute margin on both axes.
 *
 * Each component is compared as Math_EqualsCloseDouble compares, by |a - b| <= marginOfError, so a NaN component
 * never compares close, and neither do two equal infinities (their difference is NaN).
 * @param a The first vector.
 * @param b The second vector.
 * @param marginOfError The largest difference allowed per component (>= 0).
 * @returns true when |a - b| <= marginOfError on both axes.
 */
static inline bool Vector2Double_EqualsClose(Vector2Double a, Vector2Double b, double marginOfError)
{
    return Math_EqualsCloseDouble(a.X, b.X, marginOfError) && Math_EqualsCloseDouble(a.Y, b.Y, marginOfError);
}

/**
 * @brief Whether both components of a 2D vector are finite: neither NaN nor an infinity.
 * @param self The vector.
 * @returns true when both components are finite.
 */
static inline bool Vector2Double_IsFinite(Vector2Double self)
{
    const double Components[2] = { self.X, self.Y };
    for (size_t Index = 0U; Index < 2U; Index++)
    {
        if (Math_IsNaNDouble(Components[Index]) || Math_IsInfinityDouble(Components[Index]))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Returns the 2D vector with both components 1.
 * @returns (1, 1).
 */
static inline Vector2Double Vector2Double_One(void)
{
    return (Vector2Double){ .X = 1.0, .Y = 1.0 };
}

/**
 * @brief Adds one number to both components of a 2D vector (raymath's Vector2AddValue).
 * @param self The vector.
 * @param value The number to add.
 * @returns (X + value, Y + value).
 */
static inline Vector2Double Vector2Double_AddValue(Vector2Double self, double value)
{
    return (Vector2Double){ .X = self.X + value, .Y = self.Y + value };
}

/**
 * @brief Subtracts one number from both components of a 2D vector (raymath's Vector2SubtractValue).
 * @param self The vector.
 * @param value The number to subtract.
 * @returns (X - value, Y - value).
 */
static inline Vector2Double Vector2Double_SubtractValue(Vector2Double self, double value)
{
    return (Vector2Double){ .X = self.X - value, .Y = self.Y - value };
}

/**
 * @brief Divides two 2D vectors component by component (a per-axis division, raymath's Vector2Divide).
 *
 * A zero component of @p b gives an infinity, or NaN for 0 / 0, as a division does.
 * @param a The vector to divide.
 * @param b The divisors.
 * @returns (a.X / b.X, a.Y / b.Y).
 */
static inline Vector2Double Vector2Double_Divide(Vector2Double a, Vector2Double b)
{
    return (Vector2Double){ .X = a.X / b.X, .Y = a.Y / b.Y };
}

/**
 * @brief The reciprocal of each component of a 2D vector (raymath's Vector2Invert): a zero component becomes an
 *        infinity.
 * @param self The vector.
 * @returns (1 / X, 1 / Y).
 */
static inline Vector2Double Vector2Double_Invert(Vector2Double self)
{
    return (Vector2Double){ .X = 1.0 / self.X, .Y = 1.0 / self.Y };
}

/**
 * @brief A 2D vector turned a quarter turn, from the X axis towards the Y axis: (-Y, X).
 *
 * The same length, at right angles: the X axis gives the Y axis, and the 2D cross product of the vector with it is
 * its squared length (positive). The other perpendicular is its negation.
 * @param self The vector.
 * @returns (-Y, X).
 */
static inline Vector2Double Vector2Double_Perpendicular(Vector2Double self)
{
    return (Vector2Double){ .X = -self.Y, .Y = self.X };
}

/**
 * @brief The part of a 2D vector along another: @p onto's direction times the vector's component along it.
 *
 * @p onto need not be a unit vector, and may be however short or long (it is normalized with
 * Vector2Double_NormalizeOr). An @p onto without a direction (zero, NaN or infinite) gives the zero vector.
 * @param self The vector to project.
 * @param onto The vector whose direction it is projected onto.
 * @returns The projection; Vector2Double_Reject gives the rest, so the two add up to @p self.
 */
Vector2Double Vector2Double_Project(Vector2Double self, Vector2Double onto);

/**
 * @brief The part of a 2D vector across another: the vector minus its projection onto @p onto.
 *
 * What is left of a motion along a wall once its part into the wall's normal is taken out. An @p onto without a
 * direction (zero, NaN or infinite) gives @p self unchanged.
 * @param self The vector.
 * @param onto The vector whose direction is taken out (need not be a unit vector).
 * @returns self - Vector2Double_Project(self, onto), at right angles to @p onto within rounding.
 */
Vector2Double Vector2Double_Reject(Vector2Double self, Vector2Double onto);

/**
 * @brief A 2D vector mirrored off a surface with the given normal: self - 2 (self . normal) normal (raymath's
 *        Vector2Reflect).
 *
 * A ball's velocity after it bounces off a wall. The normal must be a unit vector (either side of the surface gives
 * the same result); a longer one scales the reflection.
 * @param self The incoming vector.
 * @param normal The surface's unit normal.
 * @returns The reflected vector.
 */
static inline Vector2Double Vector2Double_Reflect(Vector2Double self, Vector2Double normal)
{
    double Twice = 2.0 * Vector2Double_Dot(self, normal);
    return (Vector2Double){ .X = self.X - (normal.X * Twice), .Y = self.Y - (normal.Y * Twice) };
}

/**
 * @brief The direction of a ray refracted through a surface, by Snell's law (raymath's Vector2Refract, GLSL's
 *        refract).
 * @param self The incoming ray's unit direction.
 * @param normal The surface's unit normal, facing the side the ray comes from (against @p self).
 * @param ratio The refractive index of the ray's medium over that of the medium it enters (1 / 1.33 from air into
 *        water).
 * @returns The refracted unit direction; the zero vector when the ray is reflected back entirely (total internal
 *          reflection).
 */
Vector2Double Vector2Double_Refract(Vector2Double self, Vector2Double normal, double ratio);

/**
 * @brief A 2D vector turned by an angle about the origin (raymath's Vector2Rotate).
 * @param self The vector.
 * @param angle The angle in radians: positive turns the X axis towards the Y axis.
 * @returns The turned vector, the same length.
 */
Vector2Double Vector2Double_Rotate(Vector2Double self, double angle);

/**
 * @brief The signed angle from one 2D vector to another (raymath's Vector2Angle): atan2 of their cross and dot
 *        products.
 *
 * Positive when the turn from @p a to @p b goes the way the X axis turns to the Y axis, as Vector2Double_Cross is. The
 * vectors' lengths do not matter (they are normalized first, so no length overflows); a zero vector gives 0.
 * Vector2Double_Rotate(a, Vector2Double_Angle(a, b)) points along b.
 * @param a The vector to measure from.
 * @param b The vector to measure to.
 * @returns The angle in radians, in [-pi, pi].
 */
double Vector2Double_Angle(Vector2Double a, Vector2Double b);

/**
 * @brief The unit 2D vector at an angle from the X axis: (cos angle, sin angle).
 * @param angle The angle in radians: positive turns from the X axis towards the Y axis.
 * @returns The unit vector; the inverse of Vector2Double_GetAngle.
 */
Vector2Double Vector2Double_FromAngle(double angle);

/**
 * @brief The angle of a 2D vector from the X axis: atan2(Y, X), a heading.
 *
 * Positive towards the Y axis. The vector's length does not matter; the zero vector gives atan2's value for zeros (0
 * for positive zeros). The angle of the line from p to q is the angle of q - p (raymath's Vector2LineAngle returns its
 * negative).
 * @param self The vector.
 * @returns The angle in radians, in [-pi, pi]; the inverse of Vector2Double_FromAngle.
 */
double Vector2Double_GetAngle(Vector2Double self);

/**
 * @brief A 2D point moved towards a target by at most a distance (raymath's Vector2MoveTowards).
 *
 * The target itself when it is no farther than @p maxDistance. A negative @p maxDistance moves the point away from
 * the target by that much (and a point on the target stays there).
 * @param self The point.
 * @param target Where it moves to.
 * @param maxDistance The largest distance it moves.
 * @returns The moved point.
 */
Vector2Double Vector2Double_MoveTowards(Vector2Double self, Vector2Double target, double maxDistance);

/**
 * @brief Each component of a 2D vector clamped to its axis's range (raymath's Vector2Clamp).
 * @param self The vector.
 * @param minimum The least value of each axis.
 * @param maximum The greatest value of each axis; at least @p minimum on each axis (otherwise the minimum wins).
 * @returns The clamped vector.
 */
static inline Vector2Double Vector2Double_Clamp(Vector2Double self, Vector2Double minimum, Vector2Double maximum)
{
    return (Vector2Double)
    {
        .X = Math_ClampDouble(self.X, minimum.X, maximum.X),
        .Y = Math_ClampDouble(self.Y, minimum.Y, maximum.Y)
    };
}

/**
 * @brief A 2D vector scaled so that its length lies in a range, its direction kept (raymath's Vector2ClampValue).
 *
 * A speed limit for a velocity. The zero vector stays zero: it has no direction to give a length to.
 * @param self The vector.
 * @param minimumLength The shortest length (>= 0).
 * @param maximumLength The longest length (at least @p minimumLength).
 * @returns The vector, lengthened or shortened if its length was outside the range.
 */
Vector2Double Vector2Double_ClampLength(Vector2Double self, double minimumLength, double maximumLength);

/**
 * @brief The barycentric weights of a 2D point for a triangle: the (u, v, w) for which u a + v b + w c is the point.
 *
 * u + v + w is 1, and the point is in the triangle exactly when all three are in [0, 1] (its edges included, within
 * rounding). The corners are taken relative to @p a, so a triangle far from the origin keeps its precision. A
 * degenerate triangle (its corners on one line) is refused; there is no tolerance, so a nearly degenerate one gives
 * large weights.
 * @param point The point.
 * @param a The first corner (weight u).
 * @param b The second corner (weight v).
 * @param c The third corner (weight w).
 * @param outWeights [out] Receives (u, v, w); left unchanged when refused. Must not be NULL.
 * @returns true on success; false for a degenerate triangle or non-finite input.
 */
bool Vector2Double_TryGetBarycentric(Vector2Double point, Vector2Double a, Vector2Double b, Vector2Double c,
    Vector3Double* outWeights);


// Functions: 3D vectors.
/**
 * @brief Builds a vector from its three components.
 * @param x The X component.
 * @param y The Y component.
 * @param z The Z component.
 * @returns The vector (x, y, z).
 */
static inline Vector3Double Vector3Double_Create(double x, double y, double z)
{
    return (Vector3Double){ .X = x, .Y = y, .Z = z };
}

/**
 * @brief Returns the zero vector.
 * @returns (0, 0, 0).
 */
static inline Vector3Double Vector3Double_Zero(void)
{
    return (Vector3Double){ .X = 0.0, .Y = 0.0, .Z = 0.0 };
}

/**
 * @brief Builds a vector from three floats, each widened to double exactly.
 * @param values The components in X, Y, Z order. Must not be NULL and must hold 3 floats.
 * @returns The vector.
 */
static inline Vector3Double Vector3Double_FromFloats(const float values[3])
{
    return (Vector3Double){ .X = values[0], .Y = values[1], .Z = values[2] };
}

/**
 * @brief Rounds a vector to three floats, each component to the nearest float.
 *
 * A float keeps only about seven significant digits (its step is 1 mm at 8 km), so for a position far from the
 * origin use Vector3Double_ToFloatsRelative instead.
 * @param self The vector.
 * @param out [out] Receives the components in X, Y, Z order; a component beyond the float range becomes an infinity.
 *        Must not be NULL and must hold 3 floats.
 */
static inline void Vector3Double_ToFloats(Vector3Double self, float out[3])
{
    out[0] = (float)self.X;
    out[1] = (float)self.Y;
    out[2] = (float)self.Z;
}

/**
 * @brief Rounds a vector's offset from an origin to three floats: the difference is taken in double, then rounded
 *        once to the nearest float.
 *
 * This is how a large-world position becomes a float: relative to a nearby origin (the camera, a query's start), so
 * the float keeps the precision of the offset instead of the absolute position's. When a component and the origin's
 * are within a factor of two of each other, the double subtraction is exact (Sterbenz's lemma), so the float is the
 * nearest float to the exact offset between the two doubles.
 * @param self The vector, typically a world position.
 * @param origin The origin to subtract, typically the camera's position.
 * @param out [out] Receives self - origin in X, Y, Z order. Must not be NULL and must hold 3 floats.
 */
static inline void Vector3Double_ToFloatsRelative(Vector3Double self, Vector3Double origin, float out[3])
{
    out[0] = (float)(self.X - origin.X);
    out[1] = (float)(self.Y - origin.Y);
    out[2] = (float)(self.Z - origin.Z);
}

/**
 * @brief Adds two vectors component by component.
 * @param a The first vector.
 * @param b The second vector.
 * @returns a + b.
 */
static inline Vector3Double Vector3Double_Add(Vector3Double a, Vector3Double b)
{
    return (Vector3Double){ .X = a.X + b.X, .Y = a.Y + b.Y, .Z = a.Z + b.Z };
}

/**
 * @brief Subtracts one vector from another component by component.
 * @param a The vector to subtract from.
 * @param b The vector to subtract.
 * @returns a - b: for two points, the offset from b to a.
 */
static inline Vector3Double Vector3Double_Subtract(Vector3Double a, Vector3Double b)
{
    return (Vector3Double){ .X = a.X - b.X, .Y = a.Y - b.Y, .Z = a.Z - b.Z };
}

/**
 * @brief Multiplies every component of a vector by one number.
 * @param self The vector.
 * @param scale The factor.
 * @returns self * scale.
 */
static inline Vector3Double Vector3Double_Scale(Vector3Double self, double scale)
{
    return (Vector3Double){ .X = self.X * scale, .Y = self.Y * scale, .Z = self.Z * scale };
}

/**
 * @brief Multiplies two vectors component by component (a per-axis scale).
 * @param a The first vector.
 * @param b The second vector.
 * @returns (a.X * b.X, a.Y * b.Y, a.Z * b.Z).
 */
static inline Vector3Double Vector3Double_Multiply(Vector3Double a, Vector3Double b)
{
    return (Vector3Double){ .X = a.X * b.X, .Y = a.Y * b.Y, .Z = a.Z * b.Z };
}

/**
 * @brief Adds a scaled vector to another: a point moved along a direction, such as a ray's point at a distance.
 *
 * Each component is a + (b * scale), rounded after the product and after the sum.
 * @param a The vector to add to (the start).
 * @param b The vector to scale and add (the direction).
 * @param scale The factor for @p b (the distance along it).
 * @returns a + b * scale.
 */
static inline Vector3Double Vector3Double_AddScaled(Vector3Double a, Vector3Double b, double scale)
{
    return (Vector3Double){ .X = a.X + (b.X * scale), .Y = a.Y + (b.Y * scale), .Z = a.Z + (b.Z * scale) };
}

/**
 * @brief Negates every component of a vector.
 * @param self The vector.
 * @returns -self.
 */
static inline Vector3Double Vector3Double_Negate(Vector3Double self)
{
    return (Vector3Double){ .X = -self.X, .Y = -self.Y, .Z = -self.Z };
}

/**
 * @brief The dot product of two vectors.
 * @param a The first vector.
 * @param b The second vector.
 * @returns a.X * b.X + a.Y * b.Y + a.Z * b.Z.
 */
static inline double Vector3Double_Dot(Vector3Double a, Vector3Double b)
{
    return (a.X * b.X) + (a.Y * b.Y) + (a.Z * b.Z);
}

/**
 * @brief The cross product of two vectors, right-handed: the X axis crossed with the Y axis is the Z axis.
 * @param a The first vector.
 * @param b The second vector.
 * @returns a x b: perpendicular to both, as long as the area of the parallelogram they span.
 */
static inline Vector3Double Vector3Double_Cross(Vector3Double a, Vector3Double b)
{
    return (Vector3Double)
    {
        .X = (a.Y * b.Z) - (a.Z * b.Y),
        .Y = (a.Z * b.X) - (a.X * b.Z),
        .Z = (a.X * b.Y) - (a.Y * b.X)
    };
}

/**
 * @brief The squared length of a vector: its dot product with itself. Cheaper than the length; compare squared
 *        lengths against squared limits.
 * @param self The vector.
 * @returns The squared length (>= 0, or NaN for a NaN component).
 */
static inline double Vector3Double_LengthSquared(Vector3Double self)
{
    return Vector3Double_Dot(self, self);
}

/**
 * @brief The length of a vector.
 *
 * The square root of the squared length, so a vector whose components pass about 1e154 overflows to infinity, one
 * shorter than about 1e-154 loses precision (its square is subnormal), and one whose components are all below about
 * 1e-162 reads as 0. Vector3Double_NormalizeOr has none of these limits.
 * @param self The vector.
 * @returns The length (>= 0, or NaN for a NaN component).
 */
static inline double Vector3Double_Length(Vector3Double self)
{
    return Math_SqrtDouble(Vector3Double_Dot(self, self));
}

/**
 * @brief The squared distance between two points.
 * @param a The first point.
 * @param b The second point.
 * @returns The squared length of b - a.
 */
static inline double Vector3Double_DistanceSquared(Vector3Double a, Vector3Double b)
{
    return Vector3Double_LengthSquared(Vector3Double_Subtract(b, a));
}

/**
 * @brief The distance between two points.
 *
 * The points are subtracted first, so two points 10 000 km from the origin and 1 mm apart are 1 mm apart to within
 * the doubles' own precision.
 * @param a The first point.
 * @param b The second point.
 * @returns The length of b - a.
 */
static inline double Vector3Double_Distance(Vector3Double a, Vector3Double b)
{
    return Vector3Double_Length(Vector3Double_Subtract(b, a));
}

/**
 * @brief The unit vector in a vector's direction, or a fallback when the vector has no direction.
 *
 * The fallback is returned for the zero vector and for a vector with a NaN or infinite component. Every other vector
 * has a direction, however short or long: it is divided by its largest component's magnitude before its length is
 * taken, so the length can neither underflow nor overflow. Within a few rounding errors the result's length is 1.
 * @param self The vector to normalize.
 * @param fallback The vector to return when @p self has no direction; returned as given (it need not be a unit
 *        vector).
 * @returns The unit vector along @p self, or @p fallback.
 */
Vector3Double Vector3Double_NormalizeOr(Vector3Double self, Vector3Double fallback);

/**
 * @brief Interpolates linearly between two vectors: a + (b - a) * amount, component by component.
 *
 * The amount is not clamped: 0 gives a, 1 gives b (to within rounding), and amounts outside [0, 1] extrapolate.
 * @param a The vector at amount 0.
 * @param b The vector at amount 1.
 * @param amount The interpolation factor.
 * @returns The interpolated vector.
 */
static inline Vector3Double Vector3Double_Lerp(Vector3Double a, Vector3Double b, double amount)
{
    return (Vector3Double)
    {
        .X = Math_LerpDouble(a.X, b.X, amount),
        .Y = Math_LerpDouble(a.Y, b.Y, amount),
        .Z = Math_LerpDouble(a.Z, b.Z, amount)
    };
}

/**
 * @brief The smaller of two vectors' components, axis by axis.
 *
 * NaN components are not special-cased (see Math_MinDouble).
 * @param a The first vector.
 * @param b The second vector.
 * @returns (min(a.X, b.X), min(a.Y, b.Y), min(a.Z, b.Z)).
 */
static inline Vector3Double Vector3Double_Min(Vector3Double a, Vector3Double b)
{
    return (Vector3Double){ .X = Math_MinDouble(a.X, b.X), .Y = Math_MinDouble(a.Y, b.Y), .Z = Math_MinDouble(a.Z, b.Z) };
}

/**
 * @brief The larger of two vectors' components, axis by axis.
 *
 * NaN components are not special-cased (see Math_MaxDouble).
 * @param a The first vector.
 * @param b The second vector.
 * @returns (max(a.X, b.X), max(a.Y, b.Y), max(a.Z, b.Z)).
 */
static inline Vector3Double Vector3Double_Max(Vector3Double a, Vector3Double b)
{
    return (Vector3Double){ .X = Math_MaxDouble(a.X, b.X), .Y = Math_MaxDouble(a.Y, b.Y), .Z = Math_MaxDouble(a.Z, b.Z) };
}

/**
 * @brief Whether two vectors are equal within an absolute margin on every axis.
 *
 * Each component is compared as Math_EqualsCloseDouble compares, by |a - b| <= marginOfError, so a NaN component
 * never compares close, and neither do two equal infinities (their difference is NaN).
 * @param a The first vector.
 * @param b The second vector.
 * @param marginOfError The largest difference allowed per component (>= 0).
 * @returns true when |a - b| <= marginOfError on all three axes.
 */
static inline bool Vector3Double_EqualsClose(Vector3Double a, Vector3Double b, double marginOfError)
{
    return Math_EqualsCloseDouble(a.X, b.X, marginOfError) && Math_EqualsCloseDouble(a.Y, b.Y, marginOfError)
        && Math_EqualsCloseDouble(a.Z, b.Z, marginOfError);
}

/**
 * @brief Whether every component of a vector is finite: neither NaN nor an infinity.
 * @param self The vector.
 * @returns true when all three components are finite.
 */
static inline bool Vector3Double_IsFinite(Vector3Double self)
{
    const double Components[3] = { self.X, self.Y, self.Z };
    for (size_t Index = 0U; Index < 3U; Index++)
    {
        if (Math_IsNaNDouble(Components[Index]) || Math_IsInfinityDouble(Components[Index]))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Returns the vector with every component 1.
 * @returns (1, 1, 1).
 */
static inline Vector3Double Vector3Double_One(void)
{
    return (Vector3Double){ .X = 1.0, .Y = 1.0, .Z = 1.0 };
}

/**
 * @brief Adds one number to every component of a vector (raymath's Vector3AddValue).
 * @param self The vector.
 * @param value The number to add.
 * @returns (X + value, Y + value, Z + value).
 */
static inline Vector3Double Vector3Double_AddValue(Vector3Double self, double value)
{
    return (Vector3Double){ .X = self.X + value, .Y = self.Y + value, .Z = self.Z + value };
}

/**
 * @brief Subtracts one number from every component of a vector (raymath's Vector3SubtractValue).
 * @param self The vector.
 * @param value The number to subtract.
 * @returns (X - value, Y - value, Z - value).
 */
static inline Vector3Double Vector3Double_SubtractValue(Vector3Double self, double value)
{
    return (Vector3Double){ .X = self.X - value, .Y = self.Y - value, .Z = self.Z - value };
}

/**
 * @brief Divides two vectors component by component (a per-axis division, raymath's Vector3Divide).
 *
 * A zero component of @p b gives an infinity, or NaN for 0 / 0, as a division does.
 * @param a The vector to divide.
 * @param b The divisors.
 * @returns (a.X / b.X, a.Y / b.Y, a.Z / b.Z).
 */
static inline Vector3Double Vector3Double_Divide(Vector3Double a, Vector3Double b)
{
    return (Vector3Double){ .X = a.X / b.X, .Y = a.Y / b.Y, .Z = a.Z / b.Z };
}

/**
 * @brief The reciprocal of each component of a vector (raymath's Vector3Invert): a zero component becomes an
 *        infinity.
 * @param self The vector.
 * @returns (1 / X, 1 / Y, 1 / Z).
 */
static inline Vector3Double Vector3Double_Invert(Vector3Double self)
{
    return (Vector3Double){ .X = 1.0 / self.X, .Y = 1.0 / self.Y, .Z = 1.0 / self.Z };
}

/**
 * @brief A vector at right angles to the given one (raymath's Vector3Perpendicular): its cross product with the
 *        coordinate axis it points least along.
 *
 * Not normalized: never shorter than sqrt(2/3) of the vector's length, and zero for the zero vector. Of the many
 * perpendiculars, it is the one that suits building a basis around a direction.
 * @param self The vector.
 * @returns A vector perpendicular to @p self.
 */
Vector3Double Vector3Double_Perpendicular(Vector3Double self);

/**
 * @brief The part of a vector along another: @p onto's direction times the vector's component along it (raymath's
 *        Vector3Project).
 *
 * @p onto need not be a unit vector, and may be however short or long (it is normalized with
 * Vector3Double_NormalizeOr). An @p onto without a direction (zero, NaN or infinite) gives the zero vector.
 * @param self The vector to project.
 * @param onto The vector whose direction it is projected onto.
 * @returns The projection; Vector3Double_Reject gives the rest, so the two add up to @p self.
 */
Vector3Double Vector3Double_Project(Vector3Double self, Vector3Double onto);

/**
 * @brief The part of a vector across another: the vector minus its projection onto @p onto (raymath's
 *        Vector3Reject).
 *
 * What is left of a motion along a wall or a floor once its part into the surface's normal is taken out. An @p onto
 * without a direction (zero, NaN or infinite) gives @p self unchanged.
 * @param self The vector.
 * @param onto The vector whose direction is taken out (need not be a unit vector).
 * @returns self - Vector3Double_Project(self, onto), at right angles to @p onto within rounding.
 */
Vector3Double Vector3Double_Reject(Vector3Double self, Vector3Double onto);

/**
 * @brief A vector mirrored off a surface with the given normal: self - 2 (self . normal) normal (raymath's
 *        Vector3Reflect).
 *
 * A ball's velocity after it bounces off a wall. The normal must be a unit vector (either side of the surface gives
 * the same result); a longer one scales the reflection.
 * @param self The incoming vector.
 * @param normal The surface's unit normal.
 * @returns The reflected vector.
 */
static inline Vector3Double Vector3Double_Reflect(Vector3Double self, Vector3Double normal)
{
    double Twice = 2.0 * Vector3Double_Dot(self, normal);
    return (Vector3Double)
    {
        .X = self.X - (normal.X * Twice),
        .Y = self.Y - (normal.Y * Twice),
        .Z = self.Z - (normal.Z * Twice)
    };
}

/**
 * @brief The direction of a ray refracted through a surface, by Snell's law (raymath's Vector3Refract, GLSL's
 *        refract).
 * @param self The incoming ray's unit direction.
 * @param normal The surface's unit normal, facing the side the ray comes from (against @p self).
 * @param ratio The refractive index of the ray's medium over that of the medium it enters (1 / 1.33 from air into
 *        water).
 * @returns The refracted unit direction; the zero vector when the ray is reflected back entirely (total internal
 *          reflection).
 */
Vector3Double Vector3Double_Refract(Vector3Double self, Vector3Double normal, double ratio);

/**
 * @brief A vector turned about an axis through the origin by an angle (raymath's Vector3RotateByAxisAngle: the
 *        Euler-Rodrigues formula).
 *
 * The turn follows the right-hand rule: with the thumb along the axis, a positive angle turns the way the fingers
 * curl (about the Z axis, X towards Y). The axis need not be a unit vector; a zero (or non-finite) axis leaves the
 * vector as it is.
 * @param self The vector.
 * @param axis The axis's direction.
 * @param angle The angle in radians.
 * @returns The turned vector, the same length.
 */
Vector3Double Vector3Double_RotateByAxisAngle(Vector3Double self, Vector3Double axis, double angle);

/**
 * @brief The angle between two vectors (raymath's Vector3Angle): atan2 of the length of their cross product and their
 *        dot product, accurate for small angles and for nearly opposite vectors.
 *
 * Unsigned: 3D has no side to measure it from (for a signed angle about an axis, project both onto the plane across
 * it). The vectors' lengths do not matter (they are normalized first, so no length overflows); a zero vector gives 0.
 * @param a The first vector.
 * @param b The second vector.
 * @returns The angle in radians, in [0, pi].
 */
double Vector3Double_Angle(Vector3Double a, Vector3Double b);

/**
 * @brief A point moved towards a target by at most a distance (raymath's Vector3MoveTowards).
 *
 * The target itself when it is no farther than @p maxDistance. A negative @p maxDistance moves the point away from
 * the target by that much (and a point on the target stays there).
 * @param self The point.
 * @param target Where it moves to.
 * @param maxDistance The largest distance it moves.
 * @returns The moved point.
 */
Vector3Double Vector3Double_MoveTowards(Vector3Double self, Vector3Double target, double maxDistance);

/**
 * @brief Each component of a vector clamped to its axis's range (raymath's Vector3Clamp).
 * @param self The vector.
 * @param minimum The least value of each axis.
 * @param maximum The greatest value of each axis; at least @p minimum on each axis (otherwise the minimum wins).
 * @returns The clamped vector.
 */
static inline Vector3Double Vector3Double_Clamp(Vector3Double self, Vector3Double minimum, Vector3Double maximum)
{
    return (Vector3Double)
    {
        .X = Math_ClampDouble(self.X, minimum.X, maximum.X),
        .Y = Math_ClampDouble(self.Y, minimum.Y, maximum.Y),
        .Z = Math_ClampDouble(self.Z, minimum.Z, maximum.Z)
    };
}

/**
 * @brief A vector scaled so that its length lies in a range, its direction kept (raymath's Vector3ClampValue).
 *
 * A speed limit for a velocity. The zero vector stays zero: it has no direction to give a length to.
 * @param self The vector.
 * @param minimumLength The shortest length (>= 0).
 * @param maximumLength The longest length (at least @p minimumLength).
 * @returns The vector, lengthened or shortened if its length was outside the range.
 */
Vector3Double Vector3Double_ClampLength(Vector3Double self, double minimumLength, double maximumLength);

/**
 * @brief The barycentric weights of a point for a triangle (raymath's Vector3Barycenter): the (u, v, w) for which
 *        u a + v b + w c is the point, or its projection onto the triangle's plane for a point off it.
 *
 * u + v + w is 1, and the projected point is in the triangle exactly when all three are in [0, 1] (its edges included,
 * within rounding): what a picked point's texture coordinates are interpolated with. The corners are taken relative to
 * @p a, so a triangle far from the origin keeps its precision. A degenerate triangle (its corners on one line) is
 * refused; there is no tolerance, so a nearly degenerate one gives large weights.
 * @param point The point.
 * @param a The first corner (weight u).
 * @param b The second corner (weight v).
 * @param c The third corner (weight w).
 * @param outWeights [out] Receives (u, v, w); left unchanged when refused. Must not be NULL.
 * @returns true on success; false for a degenerate triangle or non-finite input.
 */
bool Vector3Double_TryGetBarycentric(Vector3Double point, Vector3Double a, Vector3Double b, Vector3Double c,
    Vector3Double* outWeights);


// Functions: matrices.
/**
 * @brief Returns the identity matrix, which leaves every point where it is.
 * @returns The identity.
 */
static inline Matrix4Double Matrix4Double_Identity(void)
{
    Matrix4Double Result = { .M = { 0.0 } };
    Result.M[0] = 1.0;
    Result.M[5] = 1.0;
    Result.M[10] = 1.0;
    Result.M[15] = 1.0;
    return Result;
}

/**
 * @brief Builds a matrix from 16 floats in column-major order (raymath's MatrixToFloatV order), each widened to
 *        double exactly.
 * @param values The entries: values[c * 4 + r] is row r of column c. Must not be NULL and must hold 16 floats.
 * @returns The matrix.
 */
static inline Matrix4Double Matrix4Double_FromFloats(const float values[16])
{
    Matrix4Double Result;
    for (size_t Index = 0U; Index < MATRIX4_DOUBLE_ENTRY_COUNT; Index++)
    {
        Result.M[Index] = values[Index];
    }
    return Result;
}

/**
 * @brief Rounds a matrix to 16 floats in column-major order (raymath's MatrixToFloatV order, which OpenGL takes),
 *        each entry to the nearest float.
 *
 * For a matrix that places something far from the origin, use Matrix4Double_ToFloatsRelative instead: the float
 * translation keeps only about seven significant digits.
 * @param self The matrix. Must not be NULL.
 * @param out [out] Receives the entries: out[c * 4 + r] is row r of column c. Must not be NULL and must hold 16
 *        floats.
 */
static inline void Matrix4Double_ToFloats(const Matrix4Double* self, float out[16])
{
    for (size_t Index = 0U; Index < MATRIX4_DOUBLE_ENTRY_COUNT; Index++)
    {
        out[Index] = (float)self->M[Index];
    }
}

/**
 * @brief Rounds a matrix to 16 floats with its translation taken relative to an origin: the translation minus the
 *        origin is computed in double and rounded once; every other entry is rounded as it is.
 *
 * For an affine matrix that places something in the world (a model or world matrix), the result places it relative to
 * the origin: it is the matrix followed by a move by -origin. This is how such a matrix becomes floats in a large
 * world, relative to a nearby origin (the camera for rendering, a query's start for collision): the floats then hold
 * the offset's precision instead of the absolute position's. The bottom row is rounded unchanged.
 * @param self The matrix. Must not be NULL.
 * @param origin The origin to take the translation relative to.
 * @param out [out] Receives the entries, column-major (out[c * 4 + r] is row r of column c), with out[12], out[13]
 *        and out[14] holding the translation minus the origin. Must not be NULL and must hold 16 floats.
 */
static inline void Matrix4Double_ToFloatsRelative(const Matrix4Double* self, Vector3Double origin, float out[16])
{
    Matrix4Double_ToFloats(self, out);
    out[12] = (float)(self->M[12] - origin.X);
    out[13] = (float)(self->M[13] - origin.Y);
    out[14] = (float)(self->M[14] - origin.Z);
}

/**
 * @brief Builds the matrix that moves every point by a translation.
 * @param translation The move.
 * @returns The identity with M[12], M[13] and M[14] set to the translation (raymath's MatrixTranslate).
 */
static inline Matrix4Double Matrix4Double_FromTranslation(Vector3Double translation)
{
    Matrix4Double Result = Matrix4Double_Identity();
    Result.M[12] = translation.X;
    Result.M[13] = translation.Y;
    Result.M[14] = translation.Z;
    return Result;
}

/**
 * @brief Builds the matrix that scales every point about the origin, axis by axis.
 * @param scale The factor on each axis; a negative factor mirrors that axis.
 * @returns The diagonal matrix (scale.X, scale.Y, scale.Z, 1) (raymath's MatrixScale).
 */
static inline Matrix4Double Matrix4Double_FromScale(Vector3Double scale)
{
    Matrix4Double Result = Matrix4Double_Identity();
    Result.M[0] = scale.X;
    Result.M[5] = scale.Y;
    Result.M[10] = scale.Z;
    return Result;
}

/**
 * @brief Builds the matrix that turns every point about the X axis (raymath's MatrixRotateX).
 * @param angle The angle in radians; positive turns the Y axis towards the Z axis.
 * @returns The rotation.
 */
Matrix4Double Matrix4Double_FromRotationX(double angle);

/**
 * @brief Builds the matrix that turns every point about the Y axis (raymath's MatrixRotateY).
 * @param angle The angle in radians; positive turns the Z axis towards the X axis.
 * @returns The rotation.
 */
Matrix4Double Matrix4Double_FromRotationY(double angle);

/**
 * @brief Builds the matrix that turns every point about the Z axis (raymath's MatrixRotateZ).
 * @param angle The angle in radians; positive turns the X axis towards the Y axis.
 * @returns The rotation.
 */
Matrix4Double Matrix4Double_FromRotationZ(double angle);

/**
 * @brief Builds the rotation of three Euler angles, exactly as raymath's MatrixRotateXYZ does, in double.
 *
 * The matrix is RotationX(angles.X) * RotationY(angles.Y) * RotationZ(angles.Z) acting on column vectors: a point is
 * turned about Z first, then about Y, then about X. Its first row is (cos y cos z, -cos y sin z, sin y) and its third
 * column (sin y, -sin x cos y, cos x cos y).
 * @param angles The angles in radians: X, Y and Z.
 * @returns The rotation.
 */
Matrix4Double Matrix4Double_FromRotationXYZ(Vector3Double angles);

/**
 * @brief Multiplies two matrices so that the result applies @p a first, then @p b (raymath's MatrixMultiply(a, b)).
 *
 * The result is the product b * a for column vectors: a point p goes to b * (a * p). For example, a scale, then a
 * rotation, then a translation is Multiply(Multiply(scale, rotation), translation), and a child's world matrix is
 * Multiply(child's local matrix, parent's world matrix). Each entry sums its four products in order. Either pointer
 * may point to the variable the result is assigned to.
 * @param a The matrix applied first. Must not be NULL.
 * @param b The matrix applied second. Must not be NULL.
 * @returns The product b * a.
 */
static inline Matrix4Double Matrix4Double_Multiply(const Matrix4Double* a, const Matrix4Double* b)
{
    Matrix4Double Result;
    for (size_t Column = 0U; Column < 4U; Column++)
    {
        const double* AColumn = &a->M[Column * 4U];
        for (size_t Row = 0U; Row < 4U; Row++)
        {
            Result.M[(Column * 4U) + Row] = (b->M[Row] * AColumn[0]) + (b->M[4U + Row] * AColumn[1])
                + (b->M[8U + Row] * AColumn[2]) + (b->M[12U + Row] * AColumn[3]);
        }
    }
    return Result;
}

/**
 * @brief Moves a point by a matrix: its linear part, then its translation (raymath's Vector3Transform).
 *
 * The matrix is taken as affine: its bottom row is ignored and there is no perspective divide. Each coordinate sums
 * the three products in order and adds the translation last; BoundingBoxDouble_Transform relies on that order.
 * @param self The matrix. Must not be NULL.
 * @param point The point.
 * @returns The moved point.
 */
static inline Vector3Double Matrix4Double_TransformPoint(const Matrix4Double* self, Vector3Double point)
{
    const double* M = self->M;
    return (Vector3Double)
    {
        .X = (((M[0] * point.X) + (M[4] * point.Y)) + (M[8] * point.Z)) + M[12],
        .Y = (((M[1] * point.X) + (M[5] * point.Y)) + (M[9] * point.Z)) + M[13],
        .Z = (((M[2] * point.X) + (M[6] * point.Y)) + (M[10] * point.Z)) + M[14]
    };
}

/**
 * @brief Moves a direction (or an offset) by a matrix's linear part only: the translation does not apply.
 *
 * The bottom row is ignored. A direction moved by a matrix that scales or shears is not normalized again, and a
 * surface normal needs the inverse's transpose instead when the matrix scales unevenly.
 * @param self The matrix. Must not be NULL.
 * @param direction The direction or offset.
 * @returns The moved direction.
 */
static inline Vector3Double Matrix4Double_TransformDirection(const Matrix4Double* self, Vector3Double direction)
{
    const double* M = self->M;
    return (Vector3Double)
    {
        .X = ((M[0] * direction.X) + (M[4] * direction.Y)) + (M[8] * direction.Z),
        .Y = ((M[1] * direction.X) + (M[5] * direction.Y)) + (M[9] * direction.Z),
        .Z = ((M[2] * direction.X) + (M[6] * direction.Y)) + (M[10] * direction.Z)
    };
}

/**
 * @brief Moves a 2D point by a matrix: the point (X, Y, 0) through the matrix's linear part and translation, keeping
 *        the result's X and Y (raymath's Vector2Transform).
 *
 * For 2D work on the XY plane with a 4x4 matrix (there is no 3x3 type): a matrix built from FromTranslation,
 * FromScale and FromRotationZ moves 2D points as a 2D transform would. The matrix is taken as affine, as in
 * Matrix4Double_TransformPoint.
 * @param self The matrix. Must not be NULL.
 * @param point The point.
 * @returns The moved point's X and Y.
 */
static inline Vector2Double Matrix4Double_TransformPoint2D(const Matrix4Double* self, Vector2Double point)
{
    const double* M = self->M;
    return (Vector2Double)
    {
        .X = ((M[0] * point.X) + (M[4] * point.Y)) + M[12],
        .Y = ((M[1] * point.X) + (M[5] * point.Y)) + M[13]
    };
}

/**
 * @brief Moves a 2D direction (or offset) by a matrix's linear part only: the direction (X, Y, 0), keeping the
 *        result's X and Y. The translation does not apply.
 * @param self The matrix. Must not be NULL.
 * @param direction The direction or offset.
 * @returns The moved direction's X and Y.
 */
static inline Vector2Double Matrix4Double_TransformDirection2D(const Matrix4Double* self, Vector2Double direction)
{
    const double* M = self->M;
    return (Vector2Double)
    {
        .X = (M[0] * direction.X) + (M[4] * direction.Y),
        .Y = (M[1] * direction.X) + (M[5] * direction.Y)
    };
}

/**
 * @brief Swaps a matrix's rows and columns (raymath's MatrixTranspose). The transpose of a rotation is its inverse.
 * @param self The matrix. Must not be NULL.
 * @returns The transpose: row r of column c becomes row c of column r.
 */
static inline Matrix4Double Matrix4Double_Transpose(const Matrix4Double* self)
{
    Matrix4Double Result;
    for (size_t Column = 0U; Column < 4U; Column++)
    {
        for (size_t Row = 0U; Row < 4U; Row++)
        {
            Result.M[(Column * 4U) + Row] = self->M[(Row * 4U) + Column];
        }
    }
    return Result;
}

/**
 * @brief The determinant of the whole 4x4 matrix; for an affine matrix, that of its linear part.
 *
 * Negative for a matrix that mirrors, zero for one that flattens space onto a plane, a line or a point. Computed from
 * the same 2x2 minors as Matrix4Double_TryInvert, which refuses every matrix for which this returns zero, NaN or an
 * infinity, and also those whose inverse would not be finite (a determinant too small to divide by).
 * @param self The matrix. Must not be NULL.
 * @returns The determinant.
 */
double Matrix4Double_GetDeterminant(const Matrix4Double* self);

/**
 * @brief Inverts any 4x4 matrix, refusing a singular one.
 *
 * Uses the cofactors of raymath's MatrixInvert, in double: the cofactors times the determinant's reciprocal. A matrix
 * is refused when its determinant is zero, NaN or infinite, when the reciprocal is not finite, or when an entry of the
 * inverse would not be finite. Beyond the singular matrices, that refuses a few of extreme scale: a determinant that
 * overflows, or one so small that its reciprocal does (beyond about 1.8e308, or below about 5.6e-309: a uniform scale
 * past about 5.6e102, or below about 1.8e-103). There is no tolerance: a matrix that is nearly singular, but not
 * exactly, inverts to large entries. A caller that needs a tolerance compares Matrix4Double_GetDeterminant against a
 * scale it knows. For an affine matrix, Matrix4Double_TryInvertAffine is faster.
 * @param self The matrix to invert. Must not be NULL.
 * @param outInverse [out] Receives the inverse; left unchanged when the matrix is refused. Must not be NULL; may point
 *        to @p self.
 * @returns true on success; false when the matrix is singular, or of an extreme scale as above.
 */
bool Matrix4Double_TryInvert(const Matrix4Double* self, Matrix4Double* outInverse);

/**
 * @brief Inverts an affine matrix, faster than Matrix4Double_TryInvert, refusing a singular one the same way.
 *
 * The matrix is taken as affine whatever its bottom row holds: its linear part (the first three columns' upper three
 * rows) is inverted by its adjugate times the determinant's reciprocal, and the inverse's translation is minus that
 * inverse applied to the translation. The result's bottom row is (0, 0, 0, 1). A matrix is refused as by
 * Matrix4Double_TryInvert, from its linear part's determinant: zero, NaN or infinite, a reciprocal that is not finite,
 * or an inverse entry that would not be (so also a few of extreme scale). There is no tolerance.
 *
 * Far from the origin, the matrix times its inverse cancels terms as large as the translation: the result is the
 * identity to within a few of the doubles' steps at that size (a step is 1.9 nm at 10 000 km), not exactly. To place
 * one object relative to another, subtract their translations first and apply the inverse's linear part to the
 * difference.
 * @param self The matrix to invert. Must not be NULL.
 * @param outInverse [out] Receives the inverse; left unchanged when the matrix is refused. Must not be NULL; may point
 *        to @p self.
 * @returns true on success; false when the linear part is singular, or of an extreme scale as above.
 */
bool Matrix4Double_TryInvertAffine(const Matrix4Double* self, Matrix4Double* outInverse);

/**
 * @brief The translation of a matrix: where it moves the origin to (for an affine matrix).
 * @param self The matrix. Must not be NULL.
 * @returns (M[12], M[13], M[14]).
 */
static inline Vector3Double Matrix4Double_GetTranslation(const Matrix4Double* self)
{
    return (Vector3Double){ .X = self->M[12], .Y = self->M[13], .Z = self->M[14] };
}

/**
 * @brief Replaces the translation of a matrix, leaving every other entry as it is.
 * @param self The matrix to change. Must not be NULL.
 * @param translation The new translation, written to M[12], M[13] and M[14].
 */
static inline void Matrix4Double_SetTranslation(Matrix4Double* self, Vector3Double translation)
{
    self->M[12] = translation.X;
    self->M[13] = translation.Y;
    self->M[14] = translation.Z;
}

/**
 * @brief Whether two matrices are equal within an absolute margin on every entry.
 *
 * Each entry is compared as Math_EqualsCloseDouble compares, by |a - b| <= marginOfError, so a NaN entry never
 * compares close, and neither do two equal infinities (their difference is NaN).
 * @param a The first matrix. Must not be NULL.
 * @param b The second matrix. Must not be NULL.
 * @param marginOfError The largest difference allowed per entry (>= 0).
 * @returns true when every pair of entries differs by at most marginOfError.
 */
static inline bool Matrix4Double_EqualsClose(const Matrix4Double* a, const Matrix4Double* b, double marginOfError)
{
    for (size_t Index = 0U; Index < MATRIX4_DOUBLE_ENTRY_COUNT; Index++)
    {
        if (!Math_EqualsCloseDouble(a->M[Index], b->M[Index], marginOfError))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Whether every entry of a matrix is finite: neither NaN nor an infinity.
 * @param self The matrix. Must not be NULL.
 * @returns true when all 16 entries are finite.
 */
static inline bool Matrix4Double_IsFinite(const Matrix4Double* self)
{
    for (size_t Index = 0U; Index < MATRIX4_DOUBLE_ENTRY_COUNT; Index++)
    {
        if (Math_IsNaNDouble(self->M[Index]) || Math_IsInfinityDouble(self->M[Index]))
        {
            return false;
        }
    }
    return true;
}


// Functions: boxes.
/**
 * @brief Builds the box with two given points as opposite corners, in either order.
 * @param a One corner.
 * @param b The opposite corner.
 * @returns The box from the smaller to the larger of the two on every axis.
 */
static inline BoundingBoxDouble BoundingBoxDouble_FromCorners(Vector3Double a, Vector3Double b)
{
    return (BoundingBoxDouble){ .Min = Vector3Double_Min(a, b), .Max = Vector3Double_Max(a, b) };
}

/**
 * @brief The smallest box that holds two boxes.
 * @param a The first box.
 * @param b The second box.
 * @returns The box from the smaller of the two Mins to the larger of the two Maxes, on every axis.
 */
static inline BoundingBoxDouble BoundingBoxDouble_Union(BoundingBoxDouble a, BoundingBoxDouble b)
{
    return (BoundingBoxDouble){ .Min = Vector3Double_Min(a.Min, b.Min), .Max = Vector3Double_Max(a.Max, b.Max) };
}

/**
 * @brief The smallest box that holds a box and a point.
 * @param self The box.
 * @param point The point.
 * @returns The box grown to reach the point; the box itself when the point is already in it.
 */
static inline BoundingBoxDouble BoundingBoxDouble_UnionPoint(BoundingBoxDouble self, Vector3Double point)
{
    return (BoundingBoxDouble){ .Min = Vector3Double_Min(self.Min, point), .Max = Vector3Double_Max(self.Max, point) };
}

/**
 * @brief Grows a box by a margin on every side: Min moves down by it and Max up by it on every axis.
 *
 * A negative margin shrinks the box; one larger than half the box's size on an axis inverts it there (Min above Max),
 * which the other box functions do not expect.
 * @param self The box.
 * @param margin How far each face moves outwards.
 * @returns The grown box.
 */
static inline BoundingBoxDouble BoundingBoxDouble_Expand(BoundingBoxDouble self, double margin)
{
    return (BoundingBoxDouble)
    {
        .Min = { .X = self.Min.X - margin, .Y = self.Min.Y - margin, .Z = self.Min.Z - margin },
        .Max = { .X = self.Max.X + margin, .Y = self.Max.Y + margin, .Z = self.Max.Z + margin }
    };
}

/**
 * @brief Whether a point is in a box, its faces included.
 * @param self The box.
 * @param point The point.
 * @returns true when Min <= point <= Max on every axis; false otherwise, and always for a NaN coordinate.
 */
static inline bool BoundingBoxDouble_ContainsPoint(BoundingBoxDouble self, Vector3Double point)
{
    return (self.Min.X <= point.X) && (point.X <= self.Max.X) && (self.Min.Y <= point.Y) && (point.Y <= self.Max.Y)
        && (self.Min.Z <= point.Z) && (point.Z <= self.Max.Z);
}

/**
 * @brief Whether a box holds another box entirely, faces included (a box contains itself).
 *
 * A bounding volume tree asks this to decide whether a moved box still fits in the fattened box it keeps.
 * @param self The outer box.
 * @param box The inner box.
 * @returns true when @p box lies within @p self on every axis.
 */
static inline bool BoundingBoxDouble_ContainsBox(BoundingBoxDouble self, BoundingBoxDouble box)
{
    return (self.Min.X <= box.Min.X) && (box.Max.X <= self.Max.X) && (self.Min.Y <= box.Min.Y)
        && (box.Max.Y <= self.Max.Y) && (self.Min.Z <= box.Min.Z) && (box.Max.Z <= self.Max.Z);
}

/**
 * @brief Whether two boxes share at least one point; boxes that only touch, face to face, overlap.
 * @param a The first box.
 * @param b The second box.
 * @returns true when the boxes' ranges overlap on every axis.
 */
static inline bool BoundingBoxDouble_Overlaps(BoundingBoxDouble a, BoundingBoxDouble b)
{
    return (a.Min.X <= b.Max.X) && (b.Min.X <= a.Max.X) && (a.Min.Y <= b.Max.Y) && (b.Min.Y <= a.Max.Y)
        && (a.Min.Z <= b.Max.Z) && (b.Min.Z <= a.Max.Z);
}

/**
 * @brief The centre of a box.
 *
 * Computed as Min / 2 + Max / 2: for normal bounds both halvings are exact, so the centre is rounded once and never
 * overflows, even for bounds near the largest double. (A subnormal bound, below about 2.2e-308, can lose its last bit
 * when halved.) The bounds must be finite.
 * @param self The box.
 * @returns The point halfway between Min and Max.
 */
static inline Vector3Double BoundingBoxDouble_GetCenter(BoundingBoxDouble self)
{
    return (Vector3Double)
    {
        .X = (self.Min.X * 0.5) + (self.Max.X * 0.5),
        .Y = (self.Min.Y * 0.5) + (self.Max.Y * 0.5),
        .Z = (self.Min.Z * 0.5) + (self.Max.Z * 0.5)
    };
}

/**
 * @brief The extents of a box: half its size on every axis, from the centre to a face.
 * @param self The box.
 * @returns (Max - Min) / 2.
 */
static inline Vector3Double BoundingBoxDouble_GetExtents(BoundingBoxDouble self)
{
    return Vector3Double_Scale(Vector3Double_Subtract(self.Max, self.Min), 0.5);
}

/**
 * @brief The size of a box on every axis, from face to face.
 * @param self The box.
 * @returns Max - Min.
 */
static inline Vector3Double BoundingBoxDouble_GetSize(BoundingBoxDouble self)
{
    return Vector3Double_Subtract(self.Max, self.Min);
}

/**
 * @brief The surface area of a box, 2 (xy + yz + zx) of its size: the cost a bounding volume tree minimizes when it
 *        inserts a box (the chance that a random ray or small box meets a box grows with it).
 * @param self The box.
 * @returns The surface area (0 for a point; a flat box counts both of its sides).
 */
static inline double BoundingBoxDouble_GetSurfaceArea(BoundingBoxDouble self)
{
    Vector3Double Size = BoundingBoxDouble_GetSize(self);
    return 2.0 * ((Size.X * Size.Y) + (Size.Y * Size.Z) + (Size.Z * Size.X));
}

/**
 * @brief The tightest box around a box moved by an affine matrix: the box around its eight moved corners.
 *
 * Arvo's method: on each axis, every entry of the matrix's linear part is multiplied by both of the box's bounds, and
 * the smaller products are summed into the new Min, the larger into the new Max. The products are summed in the order
 * Matrix4Double_TransformPoint sums them, and rounding is monotonic, so the result holds every corner exactly as that
 * function moves it, and its faces touch the outermost corners exactly: nothing is lost to rounding. That needs a
 * compiler that fuses neither function's products into multiply-adds, as GCC does not in ISO C mode (-std=c2x) even
 * with FMA instructions enabled. A zero entry adds nothing, so infinite bounds work: an infinite slab stays infinite
 * only along the output axes it reaches. The bottom row of the matrix is ignored. Far from the origin the result is as
 * precise as the corners themselves.
 * @param self The box, in the matrix's input space (for example a shape's own space).
 * @param matrix The affine matrix that moves it (for example a world matrix). Must not be NULL.
 * @returns The box around the moved box, in the matrix's output space.
 */
BoundingBoxDouble BoundingBoxDouble_Transform(BoundingBoxDouble self, const Matrix4Double* matrix);

/**
 * @brief Whether a ray reaches a box: the slab test on the ray origin + t * direction, for t from 0 to maxDistance.
 *
 * The ray is clipped against each axis's slab (the range between the box's two faces on that axis). A direction
 * component may be zero: the ray then runs parallel to that slab and is inside it everywhere or nowhere. A zero
 * direction only tests whether the origin is in the box. The origin is a double, so a ray far from the world's origin
 * is as exact as one near it. Faces count: a ray that touches a face, an edge or a corner, or that ends exactly on the
 * box, reaches it.
 *
 * A non-finite origin or direction, a negative or NaN maxDistance, and a box with a NaN bound or with Min above Max on
 * an axis never intersect. maxDistance may be infinite (an unbounded ray), and the box's bounds may be infinite.
 * @param self The box.
 * @param origin Where the ray starts.
 * @param direction Where it goes; need not be a unit vector (t is then measured in its lengths).
 * @param maxDistance The largest t: the ray's length for a unit direction.
 * @param outDistance [out] Receives the t where the ray enters the box, 0 when the origin is in it: a distance along
 *        a unit direction. May be NULL; left unchanged when the ray misses.
 * @returns true when the ray reaches the box within maxDistance.
 */
bool BoundingBoxDouble_IntersectsRay(BoundingBoxDouble self, Vector3Double origin, Vector3Double direction,
    double maxDistance, double* outDistance);
