// The maths of world object transforms: composing, decomposing, relative matrices and a bound on the stretch. See
// WorldTransform.h.

#include <math.h>
#include <stddef.h>
#include "WorldTransform.h"
#include "raylib/raymath.h"


// Macros.
/** A column shorter than this is a zero scale: it gives no direction. */
#define DEGENERATE_LENGTH 1e-30
/** A matrix whose axes span less than this share of the box their lengths span (|determinant| over the product of the
 *  lengths) nearly lies in a plane, and is not inverted. */
#define MINIMUM_INVERTIBLE_VOLUME 1e-9


// Static functions: vectors of doubles.
static double Dot(const double a[3], const double b[3])
{
    return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]);
}

static void Cross(const double a[3], const double b[3], double out[3])
{
    out[0] = (a[1] * b[2]) - (a[2] * b[1]);
    out[1] = (a[2] * b[0]) - (a[0] * b[2]);
    out[2] = (a[0] * b[1]) - (a[1] * b[0]);
}

/* @p vector made unit, or false when it is zero (a zero scale) or not finite. */
static bool TryNormalize(const double vector[3], double out[3])
{
    double Length = sqrt(Dot(vector, vector));
    if (!(Length > DEGENERATE_LENGTH) || !isfinite(Length))
    {
        return false;
    }
    for (size_t Index = 0U; Index < 3U; Index++)
    {
        out[Index] = vector[Index] / Length;
    }
    return true;
}

/* Takes @p unit's part out of @p vector. */
static void RemovePart(double vector[3], const double unit[3])
{
    double Part = Dot(vector, unit);
    for (size_t Index = 0U; Index < 3U; Index++)
    {
        vector[Index] -= Part * unit[Index];
    }
}

/* A unit vector perpendicular to the unit @p unit: its cross product with the world axis least along it. */
static void GetPerpendicular(const double unit[3], double out[3])
{
    size_t LeastAxis = 0U;
    for (size_t Axis = 1U; Axis < 3U; Axis++)
    {
        if (fabs(unit[Axis]) < fabs(unit[LeastAxis]))
        {
            LeastAxis = Axis;
        }
    }
    double WorldAxis[3] = { 0.0, 0.0, 0.0 };
    WorldAxis[LeastAxis] = 1.0;
    double Crossed[3];
    Cross(unit, WorldAxis, Crossed);
    if (!TryNormalize(Crossed, out))
    {
        out[0] = 0.0; // only for a unit vector that is not finite
        out[1] = 1.0;
        out[2] = 0.0;
    }
}


// Static functions: matrices.
/* The axis columns of a matrix's linear part: column c is where the matrix takes the axis c. */
static void GetColumns(Matrix matrix, double outColumns[3][3])
{
    outColumns[0][0] = matrix.m0;
    outColumns[0][1] = matrix.m1;
    outColumns[0][2] = matrix.m2;
    outColumns[1][0] = matrix.m4;
    outColumns[1][1] = matrix.m5;
    outColumns[1][2] = matrix.m6;
    outColumns[2][0] = matrix.m8;
    outColumns[2][1] = matrix.m9;
    outColumns[2][2] = matrix.m10;
}

/* A right-handed orthonormal basis as close to the columns as it gets, trusting X, then Y, then Z: X normalized, Y
 * without its part along X, Z their cross product. Zero-length (or parallel) columns are completed from the others, so
 * a zero scale still gives a basis. */
static void Orthonormalize(const double columns[3][3], double outBasis[3][3])
{
    bool IsSet[3] = { false, false, false };
    IsSet[0] = TryNormalize(columns[0], outBasis[0]);
    double Y[3] = { columns[1][0], columns[1][1], columns[1][2] };
    if (IsSet[0])
    {
        RemovePart(Y, outBasis[0]);
    }
    IsSet[1] = TryNormalize(Y, outBasis[1]);
    if (IsSet[0] && IsSet[1])
    {
        Cross(outBasis[0], outBasis[1], outBasis[2]);
        return;
    }

    double Z[3] = { columns[2][0], columns[2][1], columns[2][2] };
    for (size_t Axis = 0U; Axis < 2U; Axis++)
    {
        if (IsSet[Axis])
        {
            RemovePart(Z, outBasis[Axis]);
        }
    }
    IsSet[2] = TryNormalize(Z, outBasis[2]);
    size_t SetCount = (IsSet[0] ? 1U : 0U) + (IsSet[1] ? 1U : 0U) + (IsSet[2] ? 1U : 0U);
    if (SetCount == 2U)
    {
        // X and Y are not both set (handled above), so Z is: the missing one completes the right-handed basis.
        if (IsSet[0])
        {
            Cross(outBasis[2], outBasis[0], outBasis[1]);
        }
        else
        {
            Cross(outBasis[1], outBasis[2], outBasis[0]);
        }
        return;
    }
    if (SetCount == 1U)
    {
        size_t Known = IsSet[0] ? 0U : (IsSet[1] ? 1U : 2U);
        size_t Next = (Known + 1U) % 3U;
        GetPerpendicular(outBasis[Known], outBasis[Next]);
        Cross(outBasis[Known], outBasis[Next], outBasis[(Known + 2U) % 3U]);
        return;
    }
    for (size_t Axis = 0U; Axis < 3U; Axis++)
    {
        for (size_t Component = 0U; Component < 3U; Component++)
        {
            outBasis[Axis][Component] = (Axis == Component) ? 1.0 : 0.0;
        }
    }
}

/* The Euler XYZ angles of a rotation (a right-handed orthonormal basis, by columns), as MatrixRotateXYZ takes them. That
 * matrix is Rx(x) Ry(y) Rz(z) acting on column vectors: its first row is (cy cz, -cy sz, sy), its third column
 * (sy, -sx cy, cx cy). x comes from the third column, y from the first row; z is read with x turned back out
 * (Rx(-x) R = Ry(y) Rz(z), whose second row is (sz, cz, 0)), so the three always compose back to the basis, at gimbal
 * lock too (where x and z turn about one axis and only their sum is fixed). */
static Vector3 GetEulerXYZ(const double basis[3][3])
{
    double R00 = basis[0][0];
    double R01 = basis[1][0];
    double R02 = basis[2][0];
    double R10 = basis[0][1];
    double R11 = basis[1][1];
    double R12 = basis[2][1];
    double R20 = basis[0][2];
    double R21 = basis[1][2];
    double R22 = basis[2][2];
    double X = atan2(-R12, R22);
    double Y = atan2(R02, sqrt((R00 * R00) + (R01 * R01)));
    double SinX = sin(X);
    double CosX = cos(X);
    double Z = atan2((CosX * R10) + (SinX * R20), (CosX * R11) + (SinX * R21));
    return (Vector3){ (float)X, (float)Y, (float)Z };
}

/* -1 for a negative value, 1 otherwise (zero, -0 and NaN count as positive). */
static double SignOf(float value)
{
    return (value < 0.0f) ? -1.0 : 1.0;
}

static bool IsVectorFinite(Vector3 vector)
{
    return isfinite(vector.x) && isfinite(vector.y) && isfinite(vector.z);
}

static float DotFloat(Vector3 a, Vector3 b)
{
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}


// Functions (declared in WorldTransform.h).
Matrix WorldTransform_Compose(Vector3 position, Vector3 rotation, Vector3 scale)
{
    return MatrixMultiply(MatrixMultiply(MatrixScale(scale.x, scale.y, scale.z), MatrixRotateXYZ(rotation)),
        MatrixTranslate(position.x, position.y, position.z));
}

bool WorldTransform_Decompose(Matrix matrix, Vector3 scaleSigns, Vector3* outPosition, Vector3* outRotation,
    Vector3* outScale)
{
    double Columns[3][3];
    GetColumns(matrix, Columns);
    double Lengths[3];
    for (size_t Axis = 0U; Axis < 3U; Axis++)
    {
        Lengths[Axis] = sqrt(Dot(Columns[Axis], Columns[Axis]));
    }

    // The hint's signs, unless their product contradicts the determinant (whether the matrix mirrors): then X gives
    // way. Taking them out of the columns leaves a turn. A flat matrix (determinant 0) takes any signs.
    double Signs[3] = { SignOf(scaleSigns.x), SignOf(scaleSigns.y), SignOf(scaleSigns.z) };
    double YCrossZ[3];
    Cross(Columns[1], Columns[2], YCrossZ);
    double Determinant = Dot(Columns[0], YCrossZ);
    if ((Determinant != 0.0) && (((Signs[0] * Signs[1] * Signs[2]) < 0.0) != (Determinant < 0.0)))
    {
        Signs[0] = -Signs[0];
    }
    for (size_t Axis = 0U; Axis < 3U; Axis++)
    {
        for (size_t Component = 0U; Component < 3U; Component++)
        {
            Columns[Axis][Component] *= Signs[Axis];
        }
    }

    double Basis[3][3];
    Orthonormalize(Columns, Basis);
    *outPosition = (Vector3){ matrix.m12, matrix.m13, matrix.m14 };
    *outRotation = GetEulerXYZ(Basis);
    *outScale = (Vector3){ (float)(Signs[0] * Lengths[0]), (float)(Signs[1] * Lengths[1]),
        (float)(Signs[2] * Lengths[2]) };
    return IsVectorFinite(*outPosition) && IsVectorFinite(*outRotation) && IsVectorFinite(*outScale);
}

bool WorldTransform_TryGetLocalMatrix(Matrix world, Matrix parentWorld, Matrix* outLocal)
{
    // The parent's linear part A (columns a, b, c) inverts by its adjugate: the rows of A^-1 are b x c, c x a and a x b,
    // each over det(A) = a . (b x c).
    double Parent[3][3];
    GetColumns(parentWorld, Parent);
    double InverseRows[3][3];
    Cross(Parent[1], Parent[2], InverseRows[0]);
    Cross(Parent[2], Parent[0], InverseRows[1]);
    Cross(Parent[0], Parent[1], InverseRows[2]);
    double Determinant = Dot(Parent[0], InverseRows[0]);
    double Box = sqrt(Dot(Parent[0], Parent[0])) * sqrt(Dot(Parent[1], Parent[1])) * sqrt(Dot(Parent[2], Parent[2]));
    if (!(Box > 0.0) || !isfinite(Box) || !isfinite(Determinant) || !(fabs(Determinant) >= (MINIMUM_INVERTIBLE_VOLUME * Box)))
    {
        return false;
    }

    // local = parent^-1 world: its axes are A^-1 times the world's axes, its translation A^-1 times the world's
    // translation less the parent's.
    double World[3][3];
    GetColumns(world, World);
    double Offset[3] = { (double)world.m12 - (double)parentWorld.m12, (double)world.m13 - (double)parentWorld.m13,
        (double)world.m14 - (double)parentWorld.m14 };
    double Local[3][3];
    double Translation[3];
    for (size_t Row = 0U; Row < 3U; Row++)
    {
        for (size_t Column = 0U; Column < 3U; Column++)
        {
            Local[Column][Row] = Dot(InverseRows[Row], World[Column]) / Determinant;
        }
        Translation[Row] = Dot(InverseRows[Row], Offset) / Determinant;
    }

    Matrix Result =
    {
        .m0 = (float)Local[0][0], .m4 = (float)Local[1][0], .m8 = (float)Local[2][0], .m12 = (float)Translation[0],
        .m1 = (float)Local[0][1], .m5 = (float)Local[1][1], .m9 = (float)Local[2][1], .m13 = (float)Translation[1],
        .m2 = (float)Local[0][2], .m6 = (float)Local[1][2], .m10 = (float)Local[2][2], .m14 = (float)Translation[2],
        .m3 = 0.0f, .m7 = 0.0f, .m11 = 0.0f, .m15 = 1.0f
    };
    const float* Values[] = { &Result.m0, &Result.m1, &Result.m2, &Result.m4, &Result.m5, &Result.m6, &Result.m8,
        &Result.m9, &Result.m10, &Result.m12, &Result.m13, &Result.m14 };
    for (size_t Index = 0U; Index < (sizeof(Values) / sizeof(Values[0])); Index++)
    {
        if (!isfinite(*Values[Index]))
        {
            return false;
        }
    }
    *outLocal = Result;
    return true;
}

float WorldTransform_GetMaxScale(Matrix matrix)
{
    // The stretch is at most the square root of the largest eigenvalue of the columns' Gram matrix, which Gershgorin
    // bounds by each row's diagonal plus its off-diagonal magnitudes. Orthogonal columns leave only the diagonal: the
    // squared lengths, summed as raymath's Vector3Length sums them.
    const Vector3 Columns[3] =
    {
        { matrix.m0, matrix.m1, matrix.m2 },
        { matrix.m4, matrix.m5, matrix.m6 },
        { matrix.m8, matrix.m9, matrix.m10 }
    };
    float Largest = 0.0f;
    for (size_t Row = 0U; Row < 3U; Row++)
    {
        float Bound = DotFloat(Columns[Row], Columns[Row]);
        for (size_t Column = 0U; Column < 3U; Column++)
        {
            if (Column != Row)
            {
                Bound += fabsf(DotFloat(Columns[Row], Columns[Column]));
            }
        }
        Largest = fmaxf(Largest, Bound);
    }
    return sqrtf(Largest);
}
