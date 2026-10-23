// The maths of world object transforms: composing, decomposing, relative matrices and a bound on the stretch. See
// WorldTransform.h.

#include <math.h>
#include <stddef.h>
#include "WorldTransform.h"


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
static void GetColumns(const Matrix4Double* matrix, double outColumns[3][3])
{
    for (size_t Column = 0U; Column < 3U; Column++)
    {
        for (size_t Row = 0U; Row < 3U; Row++)
        {
            outColumns[Column][Row] = matrix->M[(Column * 4U) + Row];
        }
    }
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

// Functions (declared in WorldTransform.h).
Matrix4Double WorldTransform_Compose(Vector3Double position, Vector3 rotation, Vector3 scale)
{
    Matrix4Double Scale = Matrix4Double_FromScale(Vector3Double_Create(scale.x, scale.y, scale.z));
    Matrix4Double Turn = Matrix4Double_FromRotationXYZ(Vector3Double_Create(rotation.x, rotation.y, rotation.z));
    Matrix4Double Translation = Matrix4Double_FromTranslation(position);
    Matrix4Double ScaledTurn = Matrix4Double_Multiply(&Scale, &Turn);
    return Matrix4Double_Multiply(&ScaledTurn, &Translation);
}

bool WorldTransform_Decompose(const Matrix4Double* matrix, Vector3 scaleSigns, Vector3Double* outPosition,
    Vector3* outRotation, Vector3* outScale)
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
    *outPosition = Matrix4Double_GetTranslation(matrix);
    *outRotation = GetEulerXYZ(Basis);
    *outScale = (Vector3){ (float)(Signs[0] * Lengths[0]), (float)(Signs[1] * Lengths[1]),
        (float)(Signs[2] * Lengths[2]) };
    return Vector3Double_IsFinite(*outPosition) && IsVectorFinite(*outRotation) && IsVectorFinite(*outScale);
}

bool WorldTransform_TryGetLocalMatrix(const Matrix4Double* world, const Matrix4Double* parentWorld,
    Matrix4Double* outLocal)
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
    // translation less the parent's (subtracted first: far out, the difference is exact where a product with the whole
    // inverse would cancel terms as large as the translation).
    double World[3][3];
    GetColumns(world, World);
    double Offset[3] = { world->M[12] - parentWorld->M[12], world->M[13] - parentWorld->M[13],
        world->M[14] - parentWorld->M[14] };
    Matrix4Double Result = Matrix4Double_Identity();
    for (size_t Row = 0U; Row < 3U; Row++)
    {
        for (size_t Column = 0U; Column < 3U; Column++)
        {
            Result.M[(Column * 4U) + Row] = Dot(InverseRows[Row], World[Column]) / Determinant;
        }
        Result.M[12U + Row] = Dot(InverseRows[Row], Offset) / Determinant;
    }
    if (!Matrix4Double_IsFinite(&Result))
    {
        return false;
    }
    *outLocal = Result;
    return true;
}

double WorldTransform_GetMaxScale(const Matrix4Double* matrix)
{
    // The stretch is at most the square root of the largest eigenvalue of the columns' Gram matrix, which Gershgorin
    // bounds by each row's diagonal plus its off-diagonal magnitudes. Orthogonal columns leave only the diagonal: the
    // squared lengths.
    double Columns[3][3];
    GetColumns(matrix, Columns);
    double Largest = 0.0;
    for (size_t Row = 0U; Row < 3U; Row++)
    {
        double Bound = Dot(Columns[Row], Columns[Row]);
        for (size_t Column = 0U; Column < 3U; Column++)
        {
            if (Column != Row)
            {
                Bound += fabs(Dot(Columns[Row], Columns[Column]));
            }
        }
        Largest = fmax(Largest, Bound);
    }
    return sqrt(Largest);
}
