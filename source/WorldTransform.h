#pragma once
#include <stdbool.h>
#include "raylib/raylib.h"

/*
 * Private header (source/): the pure maths of world object transforms, tested headless (tests/WorldTransformTests.c).
 *
 * An object stores its transform as a position, an Euler XYZ rotation in radians (what raylib's MatrixRotateXYZ takes)
 * and a per-axis scale, relative to its parent. The world turns them into a matrix (WorldTransform_Compose: the scale,
 * then the rotation, then the translation, raylib's DrawModelEx order) and multiplies those down its hierarchy.
 * Matrices are raylib's: MatrixMultiply(a, b) applies a first, then b, so an object's world matrix is
 * MatrixMultiply(its local matrix, its parent's world matrix), and Vector3Transform moves a point by one.
 *
 * WorldTransform_Decompose is the one way back from a matrix to a position, rotation and scale (an object's world
 * rotation, reparenting that keeps an object's world placement). A matrix multiplied down a hierarchy can hold a shear
 * (a child turned inside a parent scaled unevenly), which those three vectors cannot hold, so it gives the nearest
 * transform without one. WorldTransform_GetMaxScale bounds how far a matrix stretches anything (bounding spheres).
 */


// Functions.
/**
 * @brief The matrix of a transform: @p scale, then the Euler XYZ @p rotation, then the translation to @p position.
 *
 * Exactly raylib's DrawModelEx composition (the renderer's model transforms always used it).
 * @param position The translation.
 * @param rotation Euler angles in radians, as MatrixRotateXYZ takes them.
 * @param scale The per-axis scale.
 * @returns The matrix.
 */
Matrix WorldTransform_Compose(Vector3 position, Vector3 rotation, Vector3 scale);

/**
 * @brief Splits a matrix into the position, Euler XYZ rotation and scale that WorldTransform_Compose turns back into
 *        it (within float precision).
 *
 * The position is the translation. The scale is the length of each axis column, with the signs @p scaleSigns asks for
 * where the matrix allows them. Signs are not fixed by a matrix: a scale of (-1, -1, 1) and a half turn about Z give
 * the same one. Only their product is fixed, the sign of the determinant, which says whether the matrix mirrors. When
 * the hint's product disagrees with it, the X sign gives way. The rotation is the columns, with those signs taken out,
 * made orthonormal (X, then Y without its part along X, then Z as their cross product; a zero-length or parallel
 * column is completed from the others), read as Euler XYZ angles in (-pi, pi], with no loss at gimbal lock (Y at
 * +-90 degrees). A sheared matrix has no exact answer: it gives that nearest one. The maths runs in double.
 * @param matrix The matrix (an affine one: the last row is ignored).
 * @param scaleSigns The signs the scale should have, one per axis: each component's sign (0 counts as positive).
 *        (1, 1, 1) puts a mirror on X. An object passes the product of the scale signs along its chain, its own and
 *        its ancestors' (see WorldObject_GetWorldTransform), so its world scale keeps the signs its scales give it.
 * @param outPosition [out] Receives the position. Must not be NULL.
 * @param outRotation [out] Receives the Euler angles in radians. Must not be NULL.
 * @param outScale [out] Receives the scale. Must not be NULL.
 * @returns true when every result is finite (always, for a finite matrix).
 */
bool WorldTransform_Decompose(Matrix matrix, Vector3 scaleSigns, Vector3* outPosition, Vector3* outRotation,
    Vector3* outScale);

/**
 * @brief The local matrix that, under a parent's world matrix, gives @p world: the parent's inverse applied to it.
 *
 * Refuses a parent that cannot be inverted: a zero scale on an axis, or axes so sheared that they nearly lie in one
 * plane. The maths runs in double.
 * @param world The world matrix to keep.
 * @param parentWorld The parent's world matrix.
 * @param outLocal [out] Receives the local matrix (unchanged on failure). Must not be NULL.
 * @returns true on success; false when @p parentWorld cannot be inverted or the result is not finite.
 */
bool WorldTransform_TryGetLocalMatrix(Matrix world, Matrix parentWorld, Matrix* outLocal);

/**
 * @brief An upper bound of how much a matrix's linear part lengthens any vector: a bounding sphere's radius times it
 *        holds the transformed sphere.
 *
 * Gershgorin's bound on the columns' dot products, which never falls below the true stretch. When the columns are
 * exactly at right angles (an unturned matrix with any scale) it is bit for bit the longest column's length, which
 * the renderer used before. A turned matrix's dot products come out a rounding error from zero, so it can be an ulp or
 * so above that. A sheared one (a child turned inside an unevenly scaled parent) is bounded correctly, where the
 * longest column would fall short.
 * @param matrix The matrix.
 * @returns The bound (>= 0).
 */
float WorldTransform_GetMaxScale(Matrix matrix);
