#pragma once
#include "raylib/raylib.h" // Vector3 and Matrix: this module is the bridge to raylib's types.
#include "wr/WRVectorMath.h"


/**
 * GameMath: the bridge between WRFramework's double-precision maths (WRVectorMath: Vector3Double, Matrix4Double,
 * BoundingBoxDouble) and raylib's float Vector3 and Matrix. Header-only; owns nothing.
 *
 * Positions far from the world's origin stay doubles (Vector3Double, Matrix4Double) and become raylib floats only
 * relative to a nearby origin: GameMath_ToVector3Relative and GameMath_ToMatrixRelative subtract the origin in double
 * and round once. The plain GameMath_ToVector3 and GameMath_ToMatrix round the absolute values: use them for values
 * that are not positions (directions, angles, sizes, rotations) or are known to be near the origin.
 *
 * raylib's Matrix declares its fields row by row (m0, m4, m8, m12, then m1, m5, ...) although its maths, like
 * WRVectorMath's, is column-major (mN is entry N of MatrixToFloatV), so its memory is not the column-major order: every
 * conversion here goes field by field, and none may use a memcpy.
 */


// Functions: vectors.
/**
 * @brief Widens raylib's float vector to a double vector, exactly.
 * @param vector The vector.
 * @returns The same vector in double.
 */
static inline Vector3Double GameMath_FromVector3(Vector3 vector)
{
    return Vector3Double_Create(vector.x, vector.y, vector.z);
}

/**
 * @brief Rounds a double vector to raylib's float vector, each component to the nearest float.
 *
 * For a position far from the origin, use GameMath_ToVector3Relative.
 * @param vector The vector.
 * @returns The rounded vector.
 */
static inline Vector3 GameMath_ToVector3(Vector3Double vector)
{
    float Values[3];
    Vector3Double_ToFloats(vector, Values);
    return (Vector3){ .x = Values[0], .y = Values[1], .z = Values[2] };
}

/**
 * @brief A double vector's offset from an origin as raylib's float vector: subtracted in double, then rounded once
 *        (Vector3Double_ToFloatsRelative).
 * @param vector The vector, typically a world position.
 * @param origin The origin, typically the camera's position.
 * @returns vector - origin, rounded to floats.
 */
static inline Vector3 GameMath_ToVector3Relative(Vector3Double vector, Vector3Double origin)
{
    float Values[3];
    Vector3Double_ToFloatsRelative(vector, origin, Values);
    return (Vector3){ .x = Values[0], .y = Values[1], .z = Values[2] };
}


// Functions: matrices.
/**
 * @brief Builds raylib's Matrix from 16 floats in column-major order: the inverse of raymath's MatrixToFloatV.
 *
 * values[N] becomes the field mN, field by field.
 * @param values The entries: values[c * 4 + r] is row r of column c. Must not be NULL and must hold 16 floats.
 * @returns The matrix.
 */
static inline Matrix GameMath_MatrixFromFloats(const float values[16])
{
    return (Matrix)
    {
        .m0 = values[0], .m1 = values[1], .m2 = values[2], .m3 = values[3],
        .m4 = values[4], .m5 = values[5], .m6 = values[6], .m7 = values[7],
        .m8 = values[8], .m9 = values[9], .m10 = values[10], .m11 = values[11],
        .m12 = values[12], .m13 = values[13], .m14 = values[14], .m15 = values[15]
    };
}

/**
 * @brief Widens raylib's Matrix to a double matrix, exactly: the field mN becomes the entry M[N].
 * @param matrix The matrix.
 * @returns The same matrix in double.
 */
static inline Matrix4Double GameMath_FromMatrix(Matrix matrix)
{
    const float Values[16] =
    {
        matrix.m0, matrix.m1, matrix.m2, matrix.m3, matrix.m4, matrix.m5, matrix.m6, matrix.m7,
        matrix.m8, matrix.m9, matrix.m10, matrix.m11, matrix.m12, matrix.m13, matrix.m14, matrix.m15
    };
    return Matrix4Double_FromFloats(Values);
}

/**
 * @brief Rounds a double matrix to raylib's Matrix, each entry to the nearest float: M[N] becomes the field mN.
 *
 * For a matrix that places something far from the origin, use GameMath_ToMatrixRelative.
 * @param matrix The matrix. Must not be NULL.
 * @returns The rounded matrix.
 */
static inline Matrix GameMath_ToMatrix(const Matrix4Double* matrix)
{
    float Values[16];
    Matrix4Double_ToFloats(matrix, Values);
    return GameMath_MatrixFromFloats(Values);
}

/**
 * @brief A double matrix as raylib's Matrix with its translation taken relative to an origin: the translation minus
 *        the origin subtracted in double and rounded once, every other entry rounded as it is
 *        (Matrix4Double_ToFloatsRelative).
 *
 * For an affine world matrix, the result places the same thing relative to the origin: the matrix a camera-relative
 * renderer draws with when the origin is the camera's position.
 * @param matrix The matrix, typically a world matrix. Must not be NULL.
 * @param origin The origin, typically the camera's position.
 * @returns The relative matrix, rounded to floats.
 */
static inline Matrix GameMath_ToMatrixRelative(const Matrix4Double* matrix, Vector3Double origin)
{
    float Values[16];
    Matrix4Double_ToFloatsRelative(matrix, origin, Values);
    return GameMath_MatrixFromFloats(Values);
}
