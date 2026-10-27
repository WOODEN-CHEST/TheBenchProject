#include "TestSuites.h"
#include "GameMath.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"
#include "wr/WRRandom.h"
#include "wr/WRVectorMath.h"
#include <math.h>
#include <stddef.h>


/**
 * WRFramework's double-precision maths (WRVectorMath, for large worlds) and this project's bridge to
 * raylib's types (GameMath). The library has no tests of its own, so they live here: hand-checked values for every
 * operation, raymath's conventions on the same inputs, the column-major layout against MatrixToFloatV and raylib's
 * Matrix fields, the conversions relative to a far origin, inverses 10 000 km out, boxes around transformed boxes and
 * the ray slab test against a brute-force check.
 */


// Macros.
/** How far a double result may be from raymath's float result, relative to the larger entry or coordinate (at least
 *  1). */
#define FLOAT_TOLERANCE 1e-5
/** How close hand-checked results that are not exact in binary must be. */
#define HAND_TOLERANCE 1e-15
/** Ten thousand kilometres, in metres: how far out the precision cases go. */
#define FAR_DISTANCE 1e7
/** How close a matrix times its inverse must be to the identity: absolute on the linear part and the bottom row,
 *  relative to the translation's size on the translation (an absolute bound that small would be below a double's step
 *  10 000 km out, 1.9e-9 m). The cases below measure at most 1.8e-15 and 2.0e-15 (15 nm out there). */
#define INVERSE_TOLERANCE 1e-14
/** How many random cases the seeded tests try. */
#define RANDOM_CASE_COUNT 2000U
/** How many random rays the slab test meets, near the origin and far away each. */
#define RANDOM_RAY_COUNT 20000U
/** How far the brute-force ray check grows and shrinks a box, to stay clear of rounding on its faces. */
#define RAY_FACE_MARGIN 1e-6
/** How close the slab test's entry distance must be to the brute force's, relative to the distance (at least 1). */
#define RAY_DISTANCE_TOLERANCE 1e-8


// Static functions: helpers.
static double RandomDouble(Random* random, double minimum, double maximum)
{
    return minimum + ((maximum - minimum) * Random_NextDouble(random));
}

static Vector3Double RandomVector(Random* random, double minimum, double maximum)
{
    return Vector3Double_Create(RandomDouble(random, minimum, maximum), RandomDouble(random, minimum, maximum),
        RandomDouble(random, minimum, maximum));
}

/* A float, so raymath and the double maths start from the same value. */
static float RandomFloat(Random* random, float minimum, float maximum)
{
    return minimum + ((maximum - minimum) * (float)Random_NextDouble(random));
}

static Vector3 RandomFloatVector(Random* random, float minimum, float maximum)
{
    return (Vector3){ RandomFloat(random, minimum, maximum), RandomFloat(random, minimum, maximum),
        RandomFloat(random, minimum, maximum) };
}

/* Row @p row of column @p column. */
static double Entry(const Matrix4Double* matrix, size_t row, size_t column)
{
    return matrix->M[(column * 4U) + row];
}

/* A matrix written row by row, as on paper. */
static Matrix4Double FromRows(const double rows[4][4])
{
    Matrix4Double Result;
    for (size_t Row = 0U; Row < 4U; Row++)
    {
        for (size_t Column = 0U; Column < 4U; Column++)
        {
            Result.M[(Column * 4U) + Row] = rows[Row][Column];
        }
    }
    return Result;
}

/* A matrix whose every entry is @p value. */
static Matrix4Double Filled(double value)
{
    Matrix4Double Result;
    for (size_t Index = 0U; Index < MATRIX4_DOUBLE_ENTRY_COUNT; Index++)
    {
        Result.M[Index] = value;
    }
    return Result;
}

/* Whether a double matrix is raymath's float one within FLOAT_TOLERANCE of their largest entry (at least 1). */
static bool IsNearRaylibMatrix(const Matrix4Double* matrix, Matrix expected)
{
    float16 Expected = MatrixToFloatV(expected);
    double Largest = 1.0;
    for (size_t Index = 0U; Index < MATRIX4_DOUBLE_ENTRY_COUNT; Index++)
    {
        Largest = fmax(Largest, fmax(fabs(matrix->M[Index]), fabs((double)Expected.v[Index])));
    }
    for (size_t Index = 0U; Index < MATRIX4_DOUBLE_ENTRY_COUNT; Index++)
    {
        if (!(fabs(matrix->M[Index] - (double)Expected.v[Index]) <= (FLOAT_TOLERANCE * Largest)))
        {
            return false;
        }
    }
    return true;
}

/* Whether a double vector is raymath's float one within FLOAT_TOLERANCE of @p scale (at least 1, and at least their
 * largest coordinate): float sums and products round at their inputs' size, not at the result's. */
static bool IsNearRaylibVectorWithin(Vector3Double vector, Vector3 expected, double scale)
{
    Vector3Double Expected = GameMath_FromVector3(expected);
    double Largest = fmax(fmax(1.0, scale), fmax(fabs(vector.X), fmax(fabs(vector.Y), fabs(vector.Z))));
    Largest = fmax(Largest, fmax(fabs(Expected.X), fmax(fabs(Expected.Y), fabs(Expected.Z))));
    return Vector3Double_EqualsClose(vector, Expected, FLOAT_TOLERANCE * Largest);
}

/* Whether a double vector is raymath's float one within FLOAT_TOLERANCE of their largest coordinate (at least 1). */
static bool IsNearRaylibVector(Vector3Double vector, Vector3 expected)
{
    return IsNearRaylibVectorWithin(vector, expected, 1.0);
}

/* The same for a scalar. */
static bool IsNearRaylibScalar(double value, float expected, double scale)
{
    double Largest = fmax(fmax(1.0, scale), fmax(fabs(value), fabs((double)expected)));
    return fabs(value - (double)expected) <= (FLOAT_TOLERANCE * Largest);
}

/* A random scale on each axis in [0.2, 3] with a mirror now and then, a random turn and a translation up to @p reach
 * on each axis. */
static Matrix4Double RandomAffine(Random* random, double reach)
{
    Vector3Double Scale = RandomVector(random, 0.2, 3.0);
    if (Random_NextInt32InLimit(random, 4) == 0)
    {
        Scale.X = -Scale.X;
    }
    Matrix4Double ScaleMatrix = Matrix4Double_FromScale(Scale);
    Matrix4Double Rotation = Matrix4Double_FromRotationXYZ(RandomVector(random, -4.0, 4.0));
    Matrix4Double Translation = Matrix4Double_FromTranslation(RandomVector(random, -reach, reach));
    Matrix4Double ScaleRotation = Matrix4Double_Multiply(&ScaleMatrix, &Rotation);
    return Matrix4Double_Multiply(&ScaleRotation, &Translation);
}

/* Whether @p product is the identity within INVERSE_TOLERANCE: absolute except for the translation, which is measured
 * against @p translationSize (at least 1). */
static bool IsIdentityWithin(const Matrix4Double* product, double translationSize)
{
    double TranslationTolerance = INVERSE_TOLERANCE * fmax(1.0, translationSize);
    for (size_t Column = 0U; Column < 4U; Column++)
    {
        for (size_t Row = 0U; Row < 4U; Row++)
        {
            double Expected = (Row == Column) ? 1.0 : 0.0;
            bool IsTranslation = (Column == 3U) && (Row < 3U);
            double Tolerance = IsTranslation ? TranslationTolerance : INVERSE_TOLERANCE;
            if (!(fabs(Entry(product, Row, Column) - Expected) <= Tolerance))
            {
                return false;
            }
        }
    }
    return true;
}

/* The eight corners of a box. */
static void GetCorners(BoundingBoxDouble box, Vector3Double outCorners[8])
{
    for (size_t Index = 0U; Index < 8U; Index++)
    {
        outCorners[Index] = Vector3Double_Create(((Index & 1U) != 0U) ? box.Max.X : box.Min.X,
            ((Index & 2U) != 0U) ? box.Max.Y : box.Min.Y, ((Index & 4U) != 0U) ? box.Max.Z : box.Min.Z);
    }
}

/* A component of a vector by axis index. */
static double Component(Vector3Double vector, size_t axis)
{
    return (axis == 0U) ? vector.X : ((axis == 1U) ? vector.Y : vector.Z);
}

/* The ray against a box face by face instead of slab by slab: the origin in the box gives 0; otherwise the nearest t
 * where the ray crosses one of the six faces' planes at a point within that face (faces included). */
static bool IntersectByFaces(BoundingBoxDouble box, Vector3Double origin, Vector3Double direction, double maxDistance,
    double* outDistance)
{
    if (BoundingBoxDouble_ContainsPoint(box, origin))
    {
        *outDistance = 0.0;
        return true;
    }
    bool IsHit = false;
    double Nearest = INFINITY_POS_DOUBLE;
    for (size_t Axis = 0U; Axis < 3U; Axis++)
    {
        double Speed = Component(direction, Axis);
        if (Speed == 0.0)
        {
            continue; // running along these two faces' planes, it never crosses them
        }
        const double Planes[2] = { Component(box.Min, Axis), Component(box.Max, Axis) };
        for (size_t Side = 0U; Side < 2U; Side++)
        {
            double T = (Planes[Side] - Component(origin, Axis)) / Speed;
            if ((T < 0.0) || (T > maxDistance))
            {
                continue;
            }
            bool IsInFace = true;
            for (size_t Other = 0U; Other < 3U; Other++)
            {
                if (Other == Axis)
                {
                    continue;
                }
                double Coordinate = Component(origin, Other) + (T * Component(direction, Other));
                if ((Coordinate < Component(box.Min, Other)) || (Coordinate > Component(box.Max, Other)))
                {
                    IsInFace = false;
                }
            }
            if (IsInFace && (T < Nearest))
            {
                Nearest = T;
                IsHit = true;
            }
        }
    }
    *outDistance = Nearest;
    return IsHit;
}

/* Sets a vector's component by axis index. */
static void SetComponent(Vector3Double* vector, size_t axis, double value)
{
    if (axis == 0U)
    {
        vector->X = value;
    }
    else if (axis == 1U)
    {
        vector->Y = value;
    }
    else
    {
        vector->Z = value;
    }
}


// Static functions: vectors.
static void VectorsByHand(TestRun* run)
{
    Vector3Double A = Vector3Double_Create(1.0, -2.0, 3.5);
    Vector3Double B = Vector3Double_Create(-4.0, 0.5, 2.0);
    TEST_CHECK(run, (A.X == 1.0) && (A.Y == -2.0) && (A.Z == 3.5));
    Vector3Double Zero = Vector3Double_Zero();
    TEST_CHECK(run, (Zero.X == 0.0) && (Zero.Y == 0.0) && (Zero.Z == 0.0));

    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Add(A, B), Vector3Double_Create(-3.0, -1.5, 5.5), 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Subtract(A, B), Vector3Double_Create(5.0, -2.5, 1.5), 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Scale(A, -2.0), Vector3Double_Create(-2.0, 4.0, -7.0), 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Multiply(A, B), Vector3Double_Create(-4.0, -1.0, 7.0), 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_AddScaled(A, B, 2.0), Vector3Double_Create(-7.0, -1.0, 7.5),
        0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Negate(A), Vector3Double_Create(-1.0, 2.0, -3.5), 0.0));
    TEST_CHECK(run, Vector3Double_Dot(A, B) == 2.0);

    // The cross product is right-handed, and perpendicular to both.
    Vector3Double AxisX = Vector3Double_Create(1.0, 0.0, 0.0);
    Vector3Double AxisY = Vector3Double_Create(0.0, 1.0, 0.0);
    Vector3Double AxisZ = Vector3Double_Create(0.0, 0.0, 1.0);
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Cross(AxisX, AxisY), AxisZ, 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Cross(AxisY, AxisZ), AxisX, 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Cross(AxisZ, AxisX), AxisY, 0.0));
    Vector3Double Crossed = Vector3Double_Cross(A, B);
    TEST_CHECK(run, Vector3Double_EqualsClose(Crossed, Vector3Double_Create(-5.75, -16.0, -7.5), 0.0));
    TEST_CHECK(run, (Vector3Double_Dot(Crossed, A) == 0.0) && (Vector3Double_Dot(Crossed, B) == 0.0));

    // Lengths and distances.
    TEST_CHECK(run, Vector3Double_Length(Vector3Double_Create(2.0, -3.0, 6.0)) == 7.0);
    TEST_CHECK(run, Vector3Double_LengthSquared(Vector3Double_Create(3.0, 4.0, -12.0)) == 169.0);
    TEST_CHECK(run, Vector3Double_Length(Vector3Double_Create(3.0, 4.0, -12.0)) == 13.0);
    Vector3Double From = Vector3Double_Create(1.0, 2.0, 3.0);
    Vector3Double To = Vector3Double_Create(4.0, 6.0, 15.0);
    TEST_CHECK(run, Vector3Double_DistanceSquared(From, To) == 169.0);
    TEST_CHECK(run, (Vector3Double_Distance(From, To) == 13.0) && (Vector3Double_Distance(To, From) == 13.0));
    // 10 000 km out, a millimetre is still a millimetre (to within the doubles' 1.9e-9 m step there).
    Vector3Double Far = Vector3Double_Create(FAR_DISTANCE, -FAR_DISTANCE, FAR_DISTANCE);
    Vector3Double FarMoved = Vector3Double_Add(Far, Vector3Double_Create(0.0, 0.001, 0.0));
    TEST_CHECK(run, fabs(Vector3Double_Distance(Far, FarMoved) - 0.001) < 2e-9);

    // Lerp: the ends, the middle, and extrapolation either way.
    Vector3Double Start = Vector3Double_Create(0.0, 10.0, -4.0);
    Vector3Double End = Vector3Double_Create(2.0, 20.0, 4.0);
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Lerp(Start, End, 0.0), Start, 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Lerp(Start, End, 1.0), End, 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Lerp(Start, End, 0.5), Vector3Double_Create(1.0, 15.0, 0.0),
        0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Lerp(Start, End, 2.0), Vector3Double_Create(4.0, 30.0, 12.0),
        0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Lerp(Start, End, -1.0), Vector3Double_Create(-2.0, 0.0, -12.0),
        0.0));

    // Min and max per axis.
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Min(A, B), Vector3Double_Create(-4.0, -2.0, 2.0), 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Max(A, B), Vector3Double_Create(1.0, 0.5, 3.5), 0.0));

    // Nearly equal: within the margin on every axis, never for NaN.
    Vector3Double Near = Vector3Double_Create(1.0, 2.0, 3.0);
    TEST_CHECK(run, Vector3Double_EqualsClose(Near, Vector3Double_Create(1.1, 2.0, 3.0), 0.2));
    TEST_CHECK(run, !Vector3Double_EqualsClose(Near, Vector3Double_Create(1.1, 2.0, 3.0), 0.05));
    TEST_CHECK(run, !Vector3Double_EqualsClose(Near, Vector3Double_Create(1.0, 2.1, 3.0), 0.05));
    TEST_CHECK(run, !Vector3Double_EqualsClose(Near, Vector3Double_Create(1.0, 2.0, 2.9), 0.05));
    TEST_CHECK(run, !Vector3Double_EqualsClose(Vector3Double_Create(NAN_DOUBLE, 2.0, 3.0), Near, INFINITY_POS_DOUBLE));
    // Two equal infinities are not close either, as Math_EqualsCloseDouble compares: their difference is NaN.
    Vector3Double Infinite = Vector3Double_Create(INFINITY_POS_DOUBLE, 2.0, 3.0);
    TEST_CHECK(run, !Vector3Double_EqualsClose(Infinite, Infinite, INFINITY_POS_DOUBLE));

    // Finite: NaN and either infinity on any axis are not.
    TEST_CHECK(run, Vector3Double_IsFinite(A) && Vector3Double_IsFinite(Zero));
    const double Bad[3] = { NAN_DOUBLE, INFINITY_POS_DOUBLE, INFINITY_NEG_DOUBLE };
    for (size_t Value = 0U; Value < 3U; Value++)
    {
        for (size_t Axis = 0U; Axis < 3U; Axis++)
        {
            Vector3Double Broken = A;
            SetComponent(&Broken, Axis, Bad[Value]);
            TEST_CHECK(run, !Vector3Double_IsFinite(Broken));
        }
    }
}

static void NormalizeFallsBackOnlyWithoutADirection(TestRun* run)
{
    Vector3Double Fallback = Vector3Double_Create(0.0, 1.0, 0.0);
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_NormalizeOr(Vector3Double_Create(3.0, 4.0, 0.0), Fallback),
        Vector3Double_Create(0.6, 0.8, 0.0), HAND_TOLERANCE));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_NormalizeOr(Vector3Double_Create(2.0, -3.0, 6.0), Fallback),
        Vector3Double_Create(2.0 / 7.0, -3.0 / 7.0, 6.0 / 7.0), HAND_TOLERANCE));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_NormalizeOr(Vector3Double_Create(0.0, 0.0, -5.0), Fallback),
        Vector3Double_Create(0.0, 0.0, -1.0), 0.0));

    // No direction: zero (either sign), NaN, an infinity. The fallback comes back as given.
    Vector3Double Odd = Vector3Double_Create(7.0, 8.0, 9.0);
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_NormalizeOr(Vector3Double_Zero(), Odd), Odd, 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_NormalizeOr(Vector3Double_Create(-0.0, 0.0, -0.0), Odd), Odd,
        0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_NormalizeOr(Vector3Double_Create(NAN_DOUBLE, 1.0, 0.0), Odd),
        Odd, 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_NormalizeOr(Vector3Double_Create(1.0, INFINITY_POS_DOUBLE,
        0.0), Odd), Odd, 0.0));

    // A direction however short or long: lengths that underflow to 0 or overflow to infinity still normalize.
    Vector3Double Tiny = Vector3Double_Create(1e-200, -1e-200, 0.0);
    TEST_CHECK(run, Vector3Double_Length(Tiny) == 0.0);
    double Half = sqrt(0.5);
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_NormalizeOr(Tiny, Fallback),
        Vector3Double_Create(Half, -Half, 0.0), HAND_TOLERANCE));
    Vector3Double Huge = Vector3Double_Create(1e300, 0.0, 1e300);
    TEST_CHECK(run, Vector3Double_Length(Huge) == INFINITY_POS_DOUBLE);
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_NormalizeOr(Huge, Fallback),
        Vector3Double_Create(Half, 0.0, Half), HAND_TOLERANCE));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_NormalizeOr(Vector3Double_Create(5e-324, 0.0, 0.0), Fallback),
        Vector3Double_Create(1.0, 0.0, 0.0), 0.0));

    // Unit length within rounding over the whole range of sizes.
    Random Generator;
    Random_Construct2(&Generator, (RandomState){ .Values = { 0x31AU, 0x1U, 0x2U, 0x3U } });
    for (size_t Index = 0U; Index < RANDOM_CASE_COUNT; Index++)
    {
        double Size = pow(10.0, RandomDouble(&Generator, -300.0, 300.0));
        Vector3Double Vector = Vector3Double_Scale(RandomVector(&Generator, -1.0, 1.0), Size);
        Vector3Double Unit = Vector3Double_NormalizeOr(Vector, Vector3Double_Zero());
        TEST_CHECK(run, fabs(Vector3Double_Length(Unit) - 1.0) < 1e-15);
        TEST_CHECK(run, Vector3Double_Dot(Unit, Vector) > 0.0);
    }
    Random_Deconstruct1(&Generator);
}

static void FloatConversionsRoundOnce(TestRun* run)
{
    const float Floats[3] = { 1.5f, -2.25f, 0.1f };
    Vector3Double Widened = Vector3Double_FromFloats(Floats);
    TEST_CHECK(run, (Widened.X == 1.5) && (Widened.Y == -2.25) && (Widened.Z == (double)0.1f));
    float Out[3];
    Vector3Double_ToFloats(Widened, Out);
    TEST_CHECK(run, Memory_IsEqual(Out, Floats, sizeof(Floats)));
    Vector3Double_ToFloats(Vector3Double_Create(0.1, 1e40, -1e40), Out);
    TEST_CHECK(run, (Out[0] == 0.1f) && (Out[1] == INFINITY) && (Out[2] == -INFINITY));

    Vector3Double_ToFloatsRelative(Vector3Double_Create(5.0, 7.0, -1.0), Vector3Double_Create(2.0, 10.0, -1.0), Out);
    TEST_CHECK(run, (Out[0] == 3.0f) && (Out[1] == -3.0f) && (Out[2] == 0.0f));

    // A point 10 000 km out and 0.123456789 m from an origin there: still 0.123456789 within float rounding.
    Vector3Double Point = Vector3Double_Create(FAR_DISTANCE + 0.123456789, FAR_DISTANCE - 0.987654321,
        -FAR_DISTANCE + 0.5);
    Vector3Double Origin = Vector3Double_Create(FAR_DISTANCE, FAR_DISTANCE, -FAR_DISTANCE);
    Vector3Double_ToFloatsRelative(Point, Origin, Out);
    float Expected = 0.123456789f;
    TEST_CHECK(run, fabsf(Out[0] - Expected) <= (nextafterf(Expected, INFINITY) - Expected));
    Expected = -0.987654321f;
    TEST_CHECK(run, fabsf(Out[1] - Expected) <= (Expected - nextafterf(Expected, -INFINITY)));
    TEST_CHECK(run, Out[2] == 0.5f);
    // Rounded to floats first, the offset is lost: a float's step is 1 m at 10 000 km.
    float Naive[3];
    float NaiveOrigin[3];
    Vector3Double_ToFloats(Point, Naive);
    Vector3Double_ToFloats(Origin, NaiveOrigin);
    TEST_CHECK(run, fabsf((Naive[0] - NaiveOrigin[0]) - 0.123456789f) > 0.1f);

    // Matrices: every entry rounded as it is, the translation relative to the origin and rounded once.
    Matrix4Double Placed = Matrix4Double_FromRotationXYZ(Vector3Double_Create(0.5, 1.0, -0.25));
    Matrix4Double_SetTranslation(&Placed, Point);
    float Entries[16];
    float Relative[16];
    Matrix4Double_ToFloats(&Placed, Entries);
    Matrix4Double_ToFloatsRelative(&Placed, Origin, Relative);
    for (size_t Index = 0U; Index < MATRIX4_DOUBLE_ENTRY_COUNT; Index++)
    {
        TEST_CHECK(run, Entries[Index] == (float)Placed.M[Index]);
        TEST_CHECK(run, ((Index >= 12U) && (Index <= 14U)) || (Relative[Index] == Entries[Index]));
    }
    TEST_CHECK(run, Memory_IsEqual(&Relative[12], Out, sizeof(Out)));
}


static void Vectors3DMoreByHand(TestRun* run)
{
    // Constants and per-axis arithmetic.
    Vector3Double V = Vector3Double_Create(1.0, -2.0, 3.0);
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_One(), Vector3Double_Create(1.0, 1.0, 1.0), 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_AddValue(V, 0.5), Vector3Double_Create(1.5, -1.5, 3.5), 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_SubtractValue(V, 0.5), Vector3Double_Create(0.5, -2.5, 2.5),
        0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Divide(Vector3Double_Create(3.0, -8.0, 1.0),
        Vector3Double_Create(2.0, 4.0, -4.0)), Vector3Double_Create(1.5, -2.0, -0.25), 0.0));
    Vector3Double ByZero = Vector3Double_Divide(Vector3Double_Create(1.0, 0.0, 2.0), Vector3Double_Create(0.0, 0.0, 1.0));
    TEST_CHECK(run, (ByZero.X == INFINITY_POS_DOUBLE) && Math_IsNaNDouble(ByZero.Y) && (ByZero.Z == 2.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Invert(Vector3Double_Create(2.0, -0.25, 1.0)),
        Vector3Double_Create(0.5, -4.0, 1.0), 0.0));
    TEST_CHECK(run, Vector3Double_Invert(Vector3Double_Create(0.0, 1.0, 1.0)).X == INFINITY_POS_DOUBLE);

    // Perpendicular: across the vector, from the axis it points least along, and long enough to build a basis on.
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Perpendicular(V), Vector3Double_Create(0.0, 3.0, 2.0), 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Perpendicular(Vector3Double_Zero()), Vector3Double_Zero(), 0.0));
    Random Generator;
    Random_Construct2(&Generator, (RandomState){ .Values = { 0x3D1U, 0x1U, 0x4U, 0x7U } });
    for (size_t Index = 0U; Index < RANDOM_CASE_COUNT; Index++)
    {
        Vector3Double Vector = RandomVector(&Generator, -10.0, 10.0);
        Vector3Double Across = Vector3Double_Perpendicular(Vector);
        double Length = Vector3Double_Length(Vector);
        TEST_CHECK(run, fabs(Vector3Double_Dot(Across, Vector)) <= (1e-14 * Length * Length));
        TEST_CHECK(run, Vector3Double_Length(Across) >= (sqrt(2.0 / 3.0) * Length * (1.0 - 1e-12)));
    }
    Random_Deconstruct1(&Generator);

    // Projection and rejection: onto any length, adding up to the vector, the rest across; nothing to project onto.
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Project(V, Vector3Double_Create(0.0, 0.0, 5.0)),
        Vector3Double_Create(0.0, 0.0, 3.0), 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Reject(V, Vector3Double_Create(0.0, 0.0, -0.001)),
        Vector3Double_Create(1.0, -2.0, 0.0), 0.0));
    Vector3Double Diagonal = Vector3Double_Create(1.0, 1.0, 1.0);
    Vector3Double Along = Vector3Double_Project(Vector3Double_Create(1.0, 2.0, 3.0), Diagonal);
    Vector3Double Across = Vector3Double_Reject(Vector3Double_Create(1.0, 2.0, 3.0), Diagonal);
    TEST_CHECK(run, Vector3Double_EqualsClose(Along, Vector3Double_Create(2.0, 2.0, 2.0), HAND_TOLERANCE * 4.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Across, Vector3Double_Create(-1.0, 0.0, 1.0), HAND_TOLERANCE * 4.0));
    TEST_CHECK(run, fabs(Vector3Double_Dot(Across, Diagonal)) < HAND_TOLERANCE * 4.0);
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Project(V, Vector3Double_Create(1e-200, 0.0, 0.0)),
        Vector3Double_Create(1.0, 0.0, 0.0), 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Project(V, Vector3Double_Zero()), Vector3Double_Zero(), 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Reject(V, Vector3Double_Create(NAN_DOUBLE, 0.0, 0.0)), V, 0.0));

    // Reflection off a floor (either side of its normal), and refraction: straight through head on, Snell's law at
    // 45 degrees from air into glass, and total internal reflection from glass into air at 60 degrees.
    Vector3Double Up = Vector3Double_Create(0.0, 1.0, 0.0);
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Reflect(V, Up), Vector3Double_Create(1.0, 2.0, 3.0), 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Reflect(V, Vector3Double_Negate(Up)),
        Vector3Double_Create(1.0, 2.0, 3.0), 0.0));
    Vector3Double Down = Vector3Double_Negate(Up);
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Refract(Down, Up, 1.0 / 1.5), Down, HAND_TOLERANCE));
    double Half = sqrt(0.5);
    Vector3Double Slanted = Vector3Double_Refract(Vector3Double_Create(Half, -Half, 0.0), Up, 1.0 / 1.5);
    double SineOut = Half / 1.5;
    TEST_CHECK(run, Vector3Double_EqualsClose(Slanted, Vector3Double_Create(SineOut, -sqrt(1.0 - (SineOut * SineOut)), 0.0),
        HAND_TOLERANCE * 4.0));
    Vector3Double Steep = Vector3Double_Create(sin(PI_DOUBLE / 3.0), -cos(PI_DOUBLE / 3.0), 0.0);
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Refract(Steep, Up, 1.5), Vector3Double_Zero(), 0.0));

    // Turning about an axis by the right-hand rule, as the matrices turn; any axis length; no axis; length kept.
    double Quarter = PI_DOUBLE / 2.0;
    Vector3Double AxisX = Vector3Double_Create(1.0, 0.0, 0.0);
    Vector3Double AxisY = Vector3Double_Create(0.0, 1.0, 0.0);
    Vector3Double AxisZ = Vector3Double_Create(0.0, 0.0, 1.0);
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_RotateByAxisAngle(AxisX, AxisZ, Quarter), AxisY,
        HAND_TOLERANCE));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_RotateByAxisAngle(AxisY, AxisX, Quarter), AxisZ,
        HAND_TOLERANCE));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_RotateByAxisAngle(AxisZ, Vector3Double_Create(0.0, 7.0, 0.0),
        Quarter), AxisX, HAND_TOLERANCE));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_RotateByAxisAngle(V, Vector3Double_Zero(), 1.0), V, 0.0));
    Matrix4Double TurnX = Matrix4Double_FromRotationX(0.7);
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_RotateByAxisAngle(V, AxisX, 0.7),
        Matrix4Double_TransformDirection(&TurnX, V), HAND_TOLERANCE * 8.0));
    TEST_CHECK(run, fabs(Vector3Double_Length(Vector3Double_RotateByAxisAngle(V, Vector3Double_Create(0.3, -1.0, 2.0),
        2.5)) - Vector3Double_Length(V)) < HAND_TOLERANCE * 8.0);

    // The angle between: unsigned, lengths do not matter, exact for tiny angles, a zero vector gives 0.
    TEST_CHECK(run, fabs(Vector3Double_Angle(AxisX, AxisY) - Quarter) < HAND_TOLERANCE);
    TEST_CHECK(run, fabs(Vector3Double_Angle(Vector3Double_Scale(AxisX, 1e-200), Vector3Double_Scale(AxisY, 1e300))
        - Quarter) < HAND_TOLERANCE);
    TEST_CHECK(run, Vector3Double_Angle(AxisX, Vector3Double_Negate(AxisX)) == PI_DOUBLE);
    TEST_CHECK(run, Vector3Double_Angle(V, V) == 0.0);
    TEST_CHECK(run, fabs(Vector3Double_Angle(Vector3Double_Create(1.0, 1.0, 0.0), AxisX) - (PI_DOUBLE / 4.0))
        < HAND_TOLERANCE);
    TEST_CHECK(run, fabs(Vector3Double_Angle(AxisX, Vector3Double_Create(1.0, 1e-10, 0.0)) - 1e-10) < 1e-24);
    TEST_CHECK(run, Vector3Double_Angle(Vector3Double_Zero(), AxisX) == 0.0);
    // Off the axes, huge and tiny vectors would overflow or underflow their dot and cross products.
    double Slope = atan2(2.0, 1.0);
    TEST_CHECK(run, fabs(Vector3Double_Angle(Vector3Double_Create(1e300, 2e300, 0.0), Vector3Double_Create(1e300, 0.0,
        0.0)) - Slope) < HAND_TOLERANCE * 4.0);
    TEST_CHECK(run, fabs(Vector3Double_Angle(Vector3Double_Create(1e-200, 2e-200, 0.0), Vector3Double_Create(1e-200, 0.0,
        0.0)) - Slope) < HAND_TOLERANCE * 4.0);

    // Moving towards a target: short of it, onto it, past it, away from it; 10 000 km out as at the origin.
    Vector3Double Target = Vector3Double_Create(2.0, 3.0, 6.0);
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_MoveTowards(Vector3Double_Zero(), Target, 1.0),
        Vector3Double_Create(2.0 / 7.0, 3.0 / 7.0, 6.0 / 7.0), HAND_TOLERANCE));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_MoveTowards(Vector3Double_Zero(), Target, 7.0), Target, 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_MoveTowards(Vector3Double_Zero(), Target, 70.0), Target, 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_MoveTowards(Vector3Double_Zero(), Target, -7.0),
        Vector3Double_Negate(Target), HAND_TOLERANCE * 8.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_MoveTowards(Target, Target, -1.0), Target, 0.0));
    Vector3Double FarStart = Vector3Double_Create(FAR_DISTANCE, -FAR_DISTANCE, FAR_DISTANCE);
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_MoveTowards(FarStart, Vector3Double_Add(FarStart, Target), 1.0),
        Vector3Double_Add(FarStart, Vector3Double_Create(2.0 / 7.0, 3.0 / 7.0, 6.0 / 7.0)), 4e-9));

    // Clamping per axis and by length; the zero vector keeps no length.
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_Clamp(Vector3Double_Create(5.0, -5.0, 0.5),
        Vector3Double_Create(-1.0, -2.0, 0.0), Vector3Double_Create(1.0, 2.0, 1.0)), Vector3Double_Create(1.0, -2.0, 0.5),
        0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_ClampLength(Target, 0.0, 3.5),
        Vector3Double_Create(1.0, 1.5, 3.0), HAND_TOLERANCE * 4.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_ClampLength(Target, 14.0, 20.0),
        Vector3Double_Create(4.0, 6.0, 12.0), HAND_TOLERANCE * 16.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_ClampLength(Target, 1.0, 10.0), Target, 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Vector3Double_ClampLength(Vector3Double_Zero(), 1.0, 2.0),
        Vector3Double_Zero(), 0.0));

    // Barycentric weights: the corners, a point inside and one above it (projected), one outside, 10 000 km out,
    // and a degenerate triangle refused.
    Vector3Double CornerA = Vector3Double_Zero();
    Vector3Double CornerB = Vector3Double_Create(4.0, 0.0, 0.0);
    Vector3Double CornerC = Vector3Double_Create(0.0, 4.0, 0.0);
    Vector3Double Weights = Vector3Double_Zero();
    TEST_CHECK(run, Vector3Double_TryGetBarycentric(CornerB, CornerA, CornerB, CornerC, &Weights)
        && Vector3Double_EqualsClose(Weights, Vector3Double_Create(0.0, 1.0, 0.0), 0.0));
    TEST_CHECK(run, Vector3Double_TryGetBarycentric(Vector3Double_Create(1.0, 1.0, 0.0), CornerA, CornerB, CornerC,
        &Weights) && Vector3Double_EqualsClose(Weights, Vector3Double_Create(0.5, 0.25, 0.25), 0.0));
    TEST_CHECK(run, Vector3Double_TryGetBarycentric(Vector3Double_Create(1.0, 1.0, 5.0), CornerA, CornerB, CornerC,
        &Weights) && Vector3Double_EqualsClose(Weights, Vector3Double_Create(0.5, 0.25, 0.25), 0.0));
    TEST_CHECK(run, Vector3Double_TryGetBarycentric(Vector3Double_Create(5.0, 5.0, 0.0), CornerA, CornerB, CornerC,
        &Weights) && (Weights.X == -1.5));
    Vector3Double FarA = Vector3Double_Add(FarStart, CornerA);
    Vector3Double FarB = Vector3Double_Add(FarStart, CornerB);
    Vector3Double FarC = Vector3Double_Add(FarStart, CornerC);
    TEST_CHECK(run, Vector3Double_TryGetBarycentric(Vector3Double_Add(FarStart, Vector3Double_Create(1.0, 1.0, 0.0)), FarA,
        FarB, FarC, &Weights) && Vector3Double_EqualsClose(Weights, Vector3Double_Create(0.5, 0.25, 0.25), 1e-12));
    Vector3Double Untouched = Vector3Double_Create(7.0, 8.0, 9.0);
    Weights = Untouched;
    TEST_CHECK(run, !Vector3Double_TryGetBarycentric(Vector3Double_One(), CornerA, Vector3Double_One(),
        Vector3Double_Create(2.0, 2.0, 2.0), &Weights) && Vector3Double_EqualsClose(Weights, Untouched, 0.0));
    TEST_CHECK(run, !Vector3Double_TryGetBarycentric(Vector3Double_Create(NAN_DOUBLE, 0.0, 0.0), CornerA, CornerB,
        CornerC, &Weights) && Vector3Double_EqualsClose(Weights, Untouched, 0.0));
}

static void Vectors3DAgreeWithRaymath(TestRun* run)
{
    Random Generator;
    Random_Construct2(&Generator, (RandomState){ .Values = { 0x3D2U, 0x2U, 0x6U, 0x9U } });
    for (size_t Index = 0U; Index < RANDOM_CASE_COUNT; Index++)
    {
        // The same inputs on both sides: floats, widened exactly for the doubles.
        Vector3 AF = RandomFloatVector(&Generator, -50.0f, 50.0f);
        Vector3 BF = RandomFloatVector(&Generator, -50.0f, 50.0f);
        Vector3 CF = RandomFloatVector(&Generator, -50.0f, 50.0f);
        Vector3 PF = RandomFloatVector(&Generator, -50.0f, 50.0f);
        Vector3 LowF = Vector3Min(BF, CF);
        Vector3 HighF = Vector3Max(BF, CF);
        float ScaleF = RandomFloat(&Generator, -3.0f, 3.0f);
        float AmountF = RandomFloat(&Generator, -1.0f, 2.0f);
        float AngleF = RandomFloat(&Generator, -7.0f, 7.0f);
        float DistanceF = RandomFloat(&Generator, -10.0f, 120.0f);
        float LowLengthF = RandomFloat(&Generator, 0.0f, 40.0f);
        float HighLengthF = LowLengthF + RandomFloat(&Generator, 0.0f, 40.0f);
        float RatioF = RandomFloat(&Generator, 0.5f, 1.5f);
        Vector3Double A = GameMath_FromVector3(AF);
        Vector3Double B = GameMath_FromVector3(BF);
        Vector3Double C = GameMath_FromVector3(CF);
        Vector3Double P = GameMath_FromVector3(PF);
        Vector3Double Low = GameMath_FromVector3(LowF);
        Vector3Double High = GameMath_FromVector3(HighF);
        Vector3Double UnitA = Vector3Double_NormalizeOr(A, Vector3Double_Zero());
        Vector3Double UnitB = Vector3Double_NormalizeOr(B, Vector3Double_Zero());
        double Size = fmax(Vector3Double_Length(A), Vector3Double_Length(B));

        TEST_CHECK(run, IsNearRaylibVectorWithin(Vector3Double_Add(A, B), Vector3Add(AF, BF), Size));
        TEST_CHECK(run, IsNearRaylibVectorWithin(Vector3Double_Subtract(A, B), Vector3Subtract(AF, BF), Size));
        TEST_CHECK(run, IsNearRaylibVectorWithin(Vector3Double_Scale(A, ScaleF), Vector3Scale(AF, ScaleF), Size * 3.0));
        TEST_CHECK(run, IsNearRaylibVectorWithin(Vector3Double_Multiply(A, B), Vector3Multiply(AF, BF), Size * Size));
        TEST_CHECK(run, IsNearRaylibVectorWithin(Vector3Double_Negate(A), Vector3Negate(AF), Size));
        TEST_CHECK(run, IsNearRaylibVectorWithin(Vector3Double_Lerp(A, B, AmountF), Vector3Lerp(AF, BF, AmountF),
            Size * 3.0));
        TEST_CHECK(run, IsNearRaylibVectorWithin(Vector3Double_Min(A, B), Vector3Min(AF, BF), Size));
        TEST_CHECK(run, IsNearRaylibVectorWithin(Vector3Double_Max(A, B), Vector3Max(AF, BF), Size));
        TEST_CHECK(run, IsNearRaylibVector(UnitA, Vector3Normalize(AF)));
        TEST_CHECK(run, IsNearRaylibScalar(Vector3Double_Dot(A, B), Vector3DotProduct(AF, BF), Size * Size));
        TEST_CHECK(run, IsNearRaylibVectorWithin(Vector3Double_Cross(A, B), Vector3CrossProduct(AF, BF), Size * Size));
        TEST_CHECK(run, IsNearRaylibScalar(Vector3Double_Length(A), Vector3Length(AF), Size));
        TEST_CHECK(run, IsNearRaylibScalar(Vector3Double_LengthSquared(A), Vector3LengthSqr(AF), Size * Size));
        TEST_CHECK(run, IsNearRaylibScalar(Vector3Double_Distance(A, B), Vector3Distance(AF, BF), Size * 2.0));
        TEST_CHECK(run, IsNearRaylibScalar(Vector3Double_DistanceSquared(A, B), Vector3DistanceSqr(AF, BF),
            Size * Size * 4.0));

        TEST_CHECK(run, IsNearRaylibVectorWithin(Vector3Double_AddValue(A, ScaleF), Vector3AddValue(AF, ScaleF), Size));
        TEST_CHECK(run, IsNearRaylibVectorWithin(Vector3Double_SubtractValue(A, ScaleF), Vector3SubtractValue(AF, ScaleF),
            Size));
        TEST_CHECK(run, IsNearRaylibVector(Vector3Double_Divide(A, B), Vector3Divide(AF, BF)));
        TEST_CHECK(run, IsNearRaylibVector(Vector3Double_Invert(A), Vector3Invert(AF)));
        TEST_CHECK(run, IsNearRaylibVectorWithin(Vector3Double_Perpendicular(A), Vector3Perpendicular(AF), Size));
        TEST_CHECK(run, IsNearRaylibVectorWithin(Vector3Double_Project(A, B), Vector3Project(AF, BF), Size));
        TEST_CHECK(run, IsNearRaylibVectorWithin(Vector3Double_Reject(A, B), Vector3Reject(AF, BF), Size));
        TEST_CHECK(run, IsNearRaylibVectorWithin(Vector3Double_Reflect(A, UnitB), Vector3Reflect(AF, Vector3Normalize(BF)),
            Size));
        TEST_CHECK(run, IsNearRaylibVectorWithin(Vector3Double_RotateByAxisAngle(A, B, AngleF),
            Vector3RotateByAxisAngle(AF, BF, AngleF), Size));
        TEST_CHECK(run, IsNearRaylibScalar(Vector3Double_Angle(A, B), Vector3Angle(AF, BF), 1.0));
        TEST_CHECK(run, IsNearRaylibVectorWithin(Vector3Double_MoveTowards(A, B, DistanceF), Vector3MoveTowards(AF, BF,
            DistanceF), Size + 120.0));
        TEST_CHECK(run, IsNearRaylibVectorWithin(Vector3Double_Clamp(A, Low, High), Vector3Clamp(AF, LowF, HighF), Size));
        TEST_CHECK(run, IsNearRaylibVectorWithin(Vector3Double_ClampLength(A, LowLengthF, HighLengthF),
            Vector3ClampValue(AF, LowLengthF, HighLengthF), Size + 80.0));

        // Refraction, away from the edge of total internal reflection (where float and double may part).
        double Cosine = Vector3Double_Dot(UnitA, UnitB);
        double Root = 1.0 - ((double)RatioF * (double)RatioF * (1.0 - (Cosine * Cosine)));
        if (fabs(Root) > 1e-3)
        {
            TEST_CHECK(run, IsNearRaylibVector(Vector3Double_Refract(UnitA, UnitB, RatioF),
                Vector3Refract(Vector3Normalize(AF), Vector3Normalize(BF), RatioF)));
        }

        // Barycentric weights, on triangles whose edges are not nearly parallel (float weights there are poor); the
        // tolerance only needs to tell the weights' order and convention apart.
        Vector3Double Edge0 = Vector3Double_Subtract(B, A);
        Vector3Double Edge1 = Vector3Double_Subtract(C, A);
        double Area = Vector3Double_LengthSquared(Vector3Double_Cross(Edge0, Edge1));
        Vector3Double Weights = Vector3Double_Zero();
        if (Area > (0.1 * Vector3Double_LengthSquared(Edge0) * Vector3Double_LengthSquared(Edge1)))
        {
            Vector3 WeightsF = Vector3Barycenter(PF, AF, BF, CF);
            TEST_CHECK(run, Vector3Double_TryGetBarycentric(P, A, B, C, &Weights)
                && IsNearRaylibVectorWithin(Weights, WeightsF, 100.0));
        }
    }
    Random_Deconstruct1(&Generator);
}


// Static functions: 2D vectors.
/* Whether a double 2D vector is raymath's float one within FLOAT_TOLERANCE of @p scale (at least 1, and at least
 * their largest coordinate): float sums and products round at their inputs' size, not at the result's. */
static bool IsNearRaylibVector2(Vector2Double vector, Vector2 expected, double scale)
{
    Vector2Double Expected = Vector2Double_Create(expected.x, expected.y);
    double Largest = fmax(fmax(1.0, scale), fmax(fmax(fabs(vector.X), fabs(vector.Y)), fmax(fabs(Expected.X),
        fabs(Expected.Y))));
    return Vector2Double_EqualsClose(vector, Expected, FLOAT_TOLERANCE * Largest);
}

static void Vectors2DByHand(TestRun* run)
{
    Vector2Double A = Vector2Double_Create(1.0, -2.0);
    Vector2Double B = Vector2Double_Create(-4.0, 0.5);
    TEST_CHECK(run, (A.X == 1.0) && (A.Y == -2.0));
    Vector2Double Zero = Vector2Double_Zero();
    TEST_CHECK(run, (Zero.X == 0.0) && (Zero.Y == 0.0));

    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Add(A, B), Vector2Double_Create(-3.0, -1.5), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Subtract(A, B), Vector2Double_Create(5.0, -2.5), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Scale(A, -2.0), Vector2Double_Create(-2.0, 4.0), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Multiply(A, B), Vector2Double_Create(-4.0, -1.0), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_AddScaled(A, B, 2.0), Vector2Double_Create(-7.0, -1.0), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Negate(A), Vector2Double_Create(-1.0, 2.0), 0.0));
    TEST_CHECK(run, Vector2Double_Dot(A, B) == -5.0);

    // The 2D cross product: from X to Y is positive, back is negative, parallel vectors give zero; it is the Z of the
    // 3D cross product of the two in the XY plane.
    Vector2Double AxisX = Vector2Double_Create(1.0, 0.0);
    Vector2Double AxisY = Vector2Double_Create(0.0, 1.0);
    TEST_CHECK(run, (Vector2Double_Cross(AxisX, AxisY) == 1.0) && (Vector2Double_Cross(AxisY, AxisX) == -1.0));
    TEST_CHECK(run, Vector2Double_Cross(A, Vector2Double_Scale(A, 3.0)) == 0.0);
    TEST_CHECK(run, Vector2Double_Cross(A, B) == -7.5);
    Vector3Double InPlane = Vector3Double_Cross(Vector3Double_Create(A.X, A.Y, 0.0), Vector3Double_Create(B.X, B.Y, 0.0));
    TEST_CHECK(run, (InPlane.X == 0.0) && (InPlane.Y == 0.0) && (InPlane.Z == Vector2Double_Cross(A, B)));

    // Lengths and distances, also 10 000 km out.
    TEST_CHECK(run, Vector2Double_Length(Vector2Double_Create(3.0, -4.0)) == 5.0);
    TEST_CHECK(run, Vector2Double_LengthSquared(Vector2Double_Create(5.0, 12.0)) == 169.0);
    Vector2Double From = Vector2Double_Create(1.0, 2.0);
    Vector2Double To = Vector2Double_Create(6.0, 14.0);
    TEST_CHECK(run, Vector2Double_DistanceSquared(From, To) == 169.0);
    TEST_CHECK(run, (Vector2Double_Distance(From, To) == 13.0) && (Vector2Double_Distance(To, From) == 13.0));
    Vector2Double Far = Vector2Double_Create(FAR_DISTANCE, -FAR_DISTANCE);
    Vector2Double FarMoved = Vector2Double_Add(Far, Vector2Double_Create(0.0, 0.001));
    TEST_CHECK(run, fabs(Vector2Double_Distance(Far, FarMoved) - 0.001) < 2e-9);

    // Lerp, min, max.
    Vector2Double Start = Vector2Double_Create(0.0, 10.0);
    Vector2Double End = Vector2Double_Create(2.0, 20.0);
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Lerp(Start, End, 0.0), Start, 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Lerp(Start, End, 1.0), End, 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Lerp(Start, End, 0.5), Vector2Double_Create(1.0, 15.0), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Lerp(Start, End, 2.0), Vector2Double_Create(4.0, 30.0), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Lerp(Start, End, -1.0), Vector2Double_Create(-2.0, 0.0), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Min(A, B), Vector2Double_Create(-4.0, -2.0), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Max(A, B), Vector2Double_Create(1.0, 0.5), 0.0));

    // Nearly equal on both axes; never for NaN or two equal infinities. Finite on both axes.
    Vector2Double Near = Vector2Double_Create(1.0, 2.0);
    TEST_CHECK(run, Vector2Double_EqualsClose(Near, Vector2Double_Create(1.1, 2.0), 0.2));
    TEST_CHECK(run, !Vector2Double_EqualsClose(Near, Vector2Double_Create(1.1, 2.0), 0.05));
    TEST_CHECK(run, !Vector2Double_EqualsClose(Near, Vector2Double_Create(1.0, 2.1), 0.05));
    TEST_CHECK(run, !Vector2Double_EqualsClose(Vector2Double_Create(NAN_DOUBLE, 2.0), Near, INFINITY_POS_DOUBLE));
    Vector2Double Infinite = Vector2Double_Create(1.0, INFINITY_NEG_DOUBLE);
    TEST_CHECK(run, !Vector2Double_EqualsClose(Infinite, Infinite, INFINITY_POS_DOUBLE));
    TEST_CHECK(run, Vector2Double_IsFinite(A) && Vector2Double_IsFinite(Zero));
    const double Bad[3] = { NAN_DOUBLE, INFINITY_POS_DOUBLE, INFINITY_NEG_DOUBLE };
    for (size_t Value = 0U; Value < 3U; Value++)
    {
        TEST_CHECK(run, !Vector2Double_IsFinite(Vector2Double_Create(Bad[Value], 1.0)));
        TEST_CHECK(run, !Vector2Double_IsFinite(Vector2Double_Create(1.0, Bad[Value])));
    }

    // Normalizing: by hand, the vectors without a direction, and lengths that underflow or overflow.
    Vector2Double Fallback = Vector2Double_Create(7.0, 8.0);
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_NormalizeOr(Vector2Double_Create(3.0, 4.0), Fallback),
        Vector2Double_Create(0.6, 0.8), HAND_TOLERANCE));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_NormalizeOr(Vector2Double_Create(0.0, -5.0), Fallback),
        Vector2Double_Create(0.0, -1.0), 0.0));
    const Vector2Double Directionless[4] =
    {
        { .X = 0.0, .Y = 0.0 }, { .X = -0.0, .Y = -0.0 }, { .X = NAN_DOUBLE, .Y = 1.0 }, { .X = 1.0, .Y = INFINITY_POS_DOUBLE }
    };
    for (size_t Index = 0U; Index < 4U; Index++)
    {
        Vector2Double Normalized = Vector2Double_NormalizeOr(Directionless[Index], Fallback);
        TEST_CHECK(run, Memory_IsEqual(&Normalized, &Fallback, sizeof(Vector2Double)));
    }
    double Half = sqrt(0.5);
    Vector2Double Tiny = Vector2Double_Create(1e-200, -1e-200);
    TEST_CHECK(run, Vector2Double_Length(Tiny) == 0.0);
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_NormalizeOr(Tiny, Fallback), Vector2Double_Create(Half, -Half),
        HAND_TOLERANCE));
    Vector2Double Huge = Vector2Double_Create(1e300, 1e300);
    TEST_CHECK(run, Vector2Double_Length(Huge) == INFINITY_POS_DOUBLE);
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_NormalizeOr(Huge, Fallback), Vector2Double_Create(Half, Half),
        HAND_TOLERANCE));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_NormalizeOr(Vector2Double_Create(0.0, 5e-324), Fallback),
        Vector2Double_Create(0.0, 1.0), 0.0));
    Random Generator;
    Random_Construct2(&Generator, (RandomState){ .Values = { 0x2D1U, 0x4U, 0x5U, 0x6U } });
    for (size_t Index = 0U; Index < RANDOM_CASE_COUNT; Index++)
    {
        double Size = pow(10.0, RandomDouble(&Generator, -300.0, 300.0));
        Vector2Double Vector = Vector2Double_Scale(Vector2Double_Create(RandomDouble(&Generator, -1.0, 1.0),
            RandomDouble(&Generator, -1.0, 1.0)), Size);
        Vector2Double Unit = Vector2Double_NormalizeOr(Vector, Vector2Double_Zero());
        TEST_CHECK(run, fabs(Vector2Double_Length(Unit) - 1.0) < 1e-15);
        TEST_CHECK(run, Vector2Double_Dot(Unit, Vector) > 0.0);
    }
    Random_Deconstruct1(&Generator);

    // Floats: widened exactly, rounded once, and relative to a far origin.
    const float Floats[2] = { 1.5f, 0.1f };
    Vector2Double Widened = Vector2Double_FromFloats(Floats);
    TEST_CHECK(run, (Widened.X == 1.5) && (Widened.Y == (double)0.1f));
    float Out[2];
    Vector2Double_ToFloats(Widened, Out);
    TEST_CHECK(run, Memory_IsEqual(Out, Floats, sizeof(Floats)));
    Vector2Double_ToFloats(Vector2Double_Create(0.1, -1e40), Out);
    TEST_CHECK(run, (Out[0] == 0.1f) && (Out[1] == -INFINITY));
    Vector2Double_ToFloatsRelative(Vector2Double_Create(5.0, 7.0), Vector2Double_Create(2.0, 10.0), Out);
    TEST_CHECK(run, (Out[0] == 3.0f) && (Out[1] == -3.0f));
    Vector2Double_ToFloatsRelative(Vector2Double_Create(FAR_DISTANCE + 0.123456789, -FAR_DISTANCE + 0.5),
        Vector2Double_Create(FAR_DISTANCE, -FAR_DISTANCE), Out);
    float Expected = 0.123456789f;
    TEST_CHECK(run, (fabsf(Out[0] - Expected) <= (nextafterf(Expected, INFINITY) - Expected)) && (Out[1] == 0.5f));
}

static void Vectors2DAgreeWithRaymath(TestRun* run)
{
    Random Generator;
    Random_Construct2(&Generator, (RandomState){ .Values = { 0x2D2U, 0x9U, 0x8U, 0x7U } });
    for (size_t Index = 0U; Index < RANDOM_CASE_COUNT; Index++)
    {
        // The same inputs on both sides: floats, widened exactly for the doubles.
        Vector2 AF = { RandomFloat(&Generator, -50.0f, 50.0f), RandomFloat(&Generator, -50.0f, 50.0f) };
        Vector2 BF = { RandomFloat(&Generator, -50.0f, 50.0f), RandomFloat(&Generator, -50.0f, 50.0f) };
        float ScaleF = RandomFloat(&Generator, -3.0f, 3.0f);
        float AmountF = RandomFloat(&Generator, -1.0f, 2.0f);
        float AngleF = RandomFloat(&Generator, -7.0f, 7.0f);
        float DistanceF = RandomFloat(&Generator, -10.0f, 120.0f);
        float LowLengthF = RandomFloat(&Generator, 0.0f, 40.0f);
        float HighLengthF = LowLengthF + RandomFloat(&Generator, 0.0f, 40.0f);
        float RatioF = RandomFloat(&Generator, 0.5f, 1.5f);
        Vector2 CF = { RandomFloat(&Generator, -50.0f, 50.0f), RandomFloat(&Generator, -50.0f, 50.0f) };
        Vector2 LowF = Vector2Min(BF, CF);
        Vector2 HighF = Vector2Max(BF, CF);
        Vector2Double A = Vector2Double_Create(AF.x, AF.y);
        Vector2Double B = Vector2Double_Create(BF.x, BF.y);
        Vector2Double Low = Vector2Double_Create(LowF.x, LowF.y);
        Vector2Double High = Vector2Double_Create(HighF.x, HighF.y);
        Vector2Double UnitA = Vector2Double_NormalizeOr(A, Vector2Double_Zero());
        Vector2Double UnitB = Vector2Double_NormalizeOr(B, Vector2Double_Zero());
        double Size = fmax(Vector2Double_Length(A), Vector2Double_Length(B));

        TEST_CHECK(run, IsNearRaylibVector2(Vector2Double_Add(A, B), Vector2Add(AF, BF), Size));
        TEST_CHECK(run, IsNearRaylibVector2(Vector2Double_Subtract(A, B), Vector2Subtract(AF, BF), Size));
        TEST_CHECK(run, IsNearRaylibVector2(Vector2Double_Scale(A, ScaleF), Vector2Scale(AF, ScaleF), Size * 3.0));
        TEST_CHECK(run, IsNearRaylibVector2(Vector2Double_Multiply(A, B), Vector2Multiply(AF, BF), Size * Size));
        TEST_CHECK(run, IsNearRaylibVector2(Vector2Double_Negate(A), Vector2Negate(AF), Size));
        TEST_CHECK(run, IsNearRaylibVector2(Vector2Double_Lerp(A, B, AmountF), Vector2Lerp(AF, BF, AmountF), Size * 3.0));
        TEST_CHECK(run, IsNearRaylibVector2(Vector2Double_Min(A, B), Vector2Min(AF, BF), Size));
        TEST_CHECK(run, IsNearRaylibVector2(Vector2Double_Max(A, B), Vector2Max(AF, BF), Size));
        TEST_CHECK(run, IsNearRaylibVector2(Vector2Double_NormalizeOr(A, Vector2Double_Zero()), Vector2Normalize(AF), 1.0));
        TEST_CHECK(run, IsNearRaylibScalar(Vector2Double_Dot(A, B), Vector2DotProduct(AF, BF), Size * Size));
        TEST_CHECK(run, IsNearRaylibScalar(Vector2Double_Cross(A, B), Vector2CrossProduct(AF, BF), Size * Size));
        TEST_CHECK(run, IsNearRaylibScalar(Vector2Double_Length(A), Vector2Length(AF), Size));
        TEST_CHECK(run, IsNearRaylibScalar(Vector2Double_LengthSquared(A), Vector2LengthSqr(AF), Size * Size));
        TEST_CHECK(run, IsNearRaylibScalar(Vector2Double_Distance(A, B), Vector2Distance(AF, BF), Size * 2.0));
        TEST_CHECK(run, IsNearRaylibScalar(Vector2Double_DistanceSquared(A, B), Vector2DistanceSqr(AF, BF),
            Size * Size * 4.0));

        TEST_CHECK(run, IsNearRaylibVector2(Vector2Double_AddValue(A, ScaleF), Vector2AddValue(AF, ScaleF), Size));
        TEST_CHECK(run, IsNearRaylibVector2(Vector2Double_SubtractValue(A, ScaleF), Vector2SubtractValue(AF, ScaleF),
            Size));
        TEST_CHECK(run, IsNearRaylibVector2(Vector2Double_Divide(A, B), Vector2Divide(AF, BF), 1.0));
        TEST_CHECK(run, IsNearRaylibVector2(Vector2Double_Invert(A), Vector2Invert(AF), 1.0));
        TEST_CHECK(run, IsNearRaylibVector2(Vector2Double_Reflect(A, UnitB), Vector2Reflect(AF, Vector2Normalize(BF)),
            Size));
        TEST_CHECK(run, IsNearRaylibVector2(Vector2Double_Rotate(A, AngleF), Vector2Rotate(AF, AngleF), Size));
        TEST_CHECK(run, IsNearRaylibVector2(Vector2Double_MoveTowards(A, B, DistanceF), Vector2MoveTowards(AF, BF,
            DistanceF), Size + 120.0));
        TEST_CHECK(run, IsNearRaylibVector2(Vector2Double_Clamp(A, Low, High), Vector2Clamp(AF, LowF, HighF), Size));
        TEST_CHECK(run, IsNearRaylibVector2(Vector2Double_ClampLength(A, LowLengthF, HighLengthF), Vector2ClampValue(AF,
            LowLengthF, HighLengthF), Size + 80.0));
        // The signed angle, modulo a whole turn (float and double may land on either side of +-pi); the heading of the
        // line from A to B, which raymath's Vector2LineAngle gives negated.
        TEST_CHECK(run, fabs(remainder(Vector2Double_Angle(A, B) - (double)Vector2Angle(AF, BF), TAU_DOUBLE))
            <= (FLOAT_TOLERANCE * PI_DOUBLE));
        TEST_CHECK(run, fabs(remainder(Vector2Double_GetAngle(Vector2Double_Subtract(B, A))
            + (double)Vector2LineAngle(AF, BF), TAU_DOUBLE)) <= (FLOAT_TOLERANCE * PI_DOUBLE));
        // A 2D point through a turn about Z and a move, as raymath moves it.
        Matrix TurnAndMoveR = MatrixMultiply(MatrixRotateZ(AngleF), MatrixTranslate(BF.x, BF.y, 0.0f));
        Matrix4Double TurnAndMove = GameMath_FromMatrix(TurnAndMoveR);
        TEST_CHECK(run, IsNearRaylibVector2(Matrix4Double_TransformPoint2D(&TurnAndMove, A), Vector2Transform(AF,
            TurnAndMoveR), Size * 2.0));
        // Refraction, away from the edge of total internal reflection (where float and double may part).
        double Cosine = Vector2Double_Dot(UnitA, UnitB);
        double Root = 1.0 - ((double)RatioF * (double)RatioF * (1.0 - (Cosine * Cosine)));
        if (fabs(Root) > 1e-3)
        {
            TEST_CHECK(run, IsNearRaylibVector2(Vector2Double_Refract(UnitA, UnitB, RatioF),
                Vector2Refract(Vector2Normalize(AF), Vector2Normalize(BF), RatioF), 1.0));
        }
    }
    Random_Deconstruct1(&Generator);
}

static void Vectors2DMoreByHand(TestRun* run)
{
    // Constants and per-axis arithmetic.
    Vector2Double V = Vector2Double_Create(1.0, -2.0);
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_One(), Vector2Double_Create(1.0, 1.0), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_AddValue(V, 0.5), Vector2Double_Create(1.5, -1.5), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_SubtractValue(V, 0.5), Vector2Double_Create(0.5, -2.5), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Divide(Vector2Double_Create(3.0, -8.0),
        Vector2Double_Create(2.0, 4.0)), Vector2Double_Create(1.5, -2.0), 0.0));
    Vector2Double ByZero = Vector2Double_Divide(Vector2Double_Create(1.0, 0.0), Vector2Double_Zero());
    TEST_CHECK(run, (ByZero.X == INFINITY_POS_DOUBLE) && Math_IsNaNDouble(ByZero.Y));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Invert(Vector2Double_Create(2.0, -0.25)),
        Vector2Double_Create(0.5, -4.0), 0.0));
    TEST_CHECK(run, Vector2Double_Invert(Vector2Double_Create(0.0, 1.0)).X == INFINITY_POS_DOUBLE);

    // A quarter turn: the X axis to the Y axis; the same length, at right angles, a positive cross product.
    Vector2Double AxisX = Vector2Double_Create(1.0, 0.0);
    Vector2Double AxisY = Vector2Double_Create(0.0, 1.0);
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Perpendicular(AxisX), AxisY, 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Perpendicular(AxisY), Vector2Double_Negate(AxisX), 0.0));
    Vector2Double W = Vector2Double_Create(3.0, 4.0);
    Vector2Double Across = Vector2Double_Perpendicular(W);
    TEST_CHECK(run, (Vector2Double_Dot(W, Across) == 0.0) && (Vector2Double_Cross(W, Across) == 25.0)
        && (Vector2Double_Length(Across) == 5.0));

    // Projection and rejection: onto any length, adding up to the vector; nothing to project onto.
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Project(W, Vector2Double_Create(10.0, 0.0)),
        Vector2Double_Create(3.0, 0.0), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Reject(W, Vector2Double_Create(10.0, 0.0)),
        Vector2Double_Create(0.0, 4.0), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Project(W, Vector2Double_Create(0.0, -0.001)),
        Vector2Double_Create(0.0, 4.0), 0.0));
    Vector2Double Diagonal = Vector2Double_One();
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Project(W, Diagonal), Vector2Double_Create(3.5, 3.5),
        HAND_TOLERANCE * 4.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Reject(W, Diagonal), Vector2Double_Create(-0.5, 0.5),
        HAND_TOLERANCE * 4.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Project(W, Vector2Double_Create(1e-200, 0.0)),
        Vector2Double_Create(3.0, 0.0), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Project(W, Vector2Double_Zero()), Vector2Double_Zero(), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Reject(W, Vector2Double_Create(NAN_DOUBLE, 0.0)), W, 0.0));

    // Reflection off a floor (either side of its normal) and off a 45 degree wall; refraction head on, by Snell's law
    // at 45 degrees from air into glass, and total internal reflection from glass into air at 60 degrees.
    Vector2Double Up = Vector2Double_Create(0.0, 1.0);
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Reflect(V, Up), Vector2Double_Create(1.0, 2.0), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Reflect(V, Vector2Double_Negate(Up)),
        Vector2Double_Create(1.0, 2.0), 0.0));
    double Half = sqrt(0.5);
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Reflect(W, Vector2Double_Create(Half, Half)),
        Vector2Double_Create(-4.0, -3.0), HAND_TOLERANCE * 8.0));
    Vector2Double Down = Vector2Double_Negate(Up);
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Refract(Down, Up, 1.0 / 1.5), Down, HAND_TOLERANCE));
    double SineOut = Half / 1.5;
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Refract(Vector2Double_Create(Half, -Half), Up, 1.0 / 1.5),
        Vector2Double_Create(SineOut, -sqrt(1.0 - (SineOut * SineOut))), HAND_TOLERANCE * 4.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Refract(Vector2Double_Create(sin(PI_DOUBLE / 3.0),
        -cos(PI_DOUBLE / 3.0)), Up, 1.5), Vector2Double_Zero(), 0.0));

    // Turning: quarter turns, turning back, the length kept.
    double Quarter = PI_DOUBLE / 2.0;
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Rotate(AxisX, Quarter), AxisY, HAND_TOLERANCE));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Rotate(AxisY, Quarter), Vector2Double_Negate(AxisX),
        HAND_TOLERANCE));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Rotate(Vector2Double_Rotate(W, 0.9), -0.9), W,
        HAND_TOLERANCE * 8.0));
    TEST_CHECK(run, fabs(Vector2Double_Length(Vector2Double_Rotate(W, 2.0)) - 5.0) < HAND_TOLERANCE * 8.0);

    // Signed angles: positive from X to Y, the lengths do not matter, opposite vectors pi, a zero vector 0; turning one
    // vector by its angle to another lines it up with it.
    TEST_CHECK(run, fabs(Vector2Double_Angle(AxisX, AxisY) - Quarter) < HAND_TOLERANCE);
    TEST_CHECK(run, fabs(Vector2Double_Angle(AxisY, AxisX) + Quarter) < HAND_TOLERANCE);
    TEST_CHECK(run, fabs(Vector2Double_Angle(AxisX, Vector2Double_Negate(AxisX))) == PI_DOUBLE);
    TEST_CHECK(run, fabs(Vector2Double_Angle(Vector2Double_Scale(AxisX, 1e300), Vector2Double_Scale(AxisY, 1e-300))
        - Quarter) < HAND_TOLERANCE);
    TEST_CHECK(run, (Vector2Double_Angle(Vector2Double_Zero(), AxisX) == 0.0) && (Vector2Double_Angle(W, W) == 0.0));
    // Off the axes, huge and tiny vectors would overflow or underflow their dot and cross products.
    double Slope = atan2(2.0, 1.0);
    TEST_CHECK(run, fabs(Vector2Double_Angle(Vector2Double_Create(1e300, 2e300), Vector2Double_Create(1e300, 0.0)) + Slope)
        < HAND_TOLERANCE * 4.0);
    TEST_CHECK(run, fabs(Vector2Double_Angle(Vector2Double_Create(1e-200, 2e-200), Vector2Double_Create(1e-200, 0.0))
        + Slope) < HAND_TOLERANCE * 4.0);
    Vector2Double From = Vector2Double_Create(3.0, -1.0);
    Vector2Double To = Vector2Double_Create(-2.0, 5.0);
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_NormalizeOr(Vector2Double_Rotate(From,
        Vector2Double_Angle(From, To)), Vector2Double_Zero()), Vector2Double_NormalizeOr(To, Vector2Double_Zero()),
        HAND_TOLERANCE * 8.0));

    // Headings: FromAngle and GetAngle undo each other; the axes; the line from one point to another.
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_FromAngle(0.0), AxisX, 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_FromAngle(Quarter), AxisY, HAND_TOLERANCE));
    TEST_CHECK(run, (Vector2Double_GetAngle(Vector2Double_Create(0.0, 5.0)) == Quarter)
        && (Vector2Double_GetAngle(Vector2Double_Create(-1.0, 0.0)) == PI_DOUBLE)
        && (Vector2Double_GetAngle(Vector2Double_Create(0.0, -2.0)) == -Quarter));
    for (double Angle = -3.0; Angle <= 3.0; Angle += 0.25)
    {
        TEST_CHECK(run, fabs(Vector2Double_GetAngle(Vector2Double_FromAngle(Angle)) - Angle) < HAND_TOLERANCE * 4.0);
    }
    TEST_CHECK(run, Vector2Double_GetAngle(Vector2Double_Subtract(Vector2Double_Create(1.0, 3.0),
        Vector2Double_Create(1.0, 1.0))) == Quarter);
    TEST_CHECK(run, Vector2Double_GetAngle(Vector2Double_Zero()) == 0.0);

    // Moving towards a target: short of it, onto it, past it, away from it; 10 000 km out as at the origin.
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_MoveTowards(Vector2Double_Zero(), W, 1.0),
        Vector2Double_Create(0.6, 0.8), HAND_TOLERANCE));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_MoveTowards(Vector2Double_Zero(), W, 5.0), W, 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_MoveTowards(Vector2Double_Zero(), W, 50.0), W, 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_MoveTowards(Vector2Double_Zero(), W, -5.0),
        Vector2Double_Negate(W), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_MoveTowards(W, W, -1.0), W, 0.0));
    Vector2Double FarStart = Vector2Double_Create(FAR_DISTANCE, -FAR_DISTANCE);
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_MoveTowards(FarStart, Vector2Double_Add(FarStart, W), 1.0),
        Vector2Double_Add(FarStart, Vector2Double_Create(0.6, 0.8)), 4e-9));

    // Clamping per axis and by length; the zero vector keeps no length.
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Clamp(Vector2Double_Create(5.0, -5.0),
        Vector2Double_Create(-1.0, -2.0), Vector2Double_Create(1.0, 2.0)), Vector2Double_Create(1.0, -2.0), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_Clamp(Vector2Double_Create(0.5, 0.0),
        Vector2Double_Create(-1.0, -2.0), Vector2Double_Create(1.0, 2.0)), Vector2Double_Create(0.5, 0.0), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_ClampLength(W, 0.0, 2.0), Vector2Double_Create(1.2, 1.6),
        HAND_TOLERANCE * 4.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_ClampLength(W, 10.0, 20.0), Vector2Double_Create(6.0, 8.0),
        0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_ClampLength(W, 1.0, 10.0), W, 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Vector2Double_ClampLength(Vector2Double_Zero(), 1.0, 2.0),
        Vector2Double_Zero(), 0.0));

    // Barycentric weights: a corner, a point inside, one outside, 10 000 km out, rebuilding random points, and a
    // degenerate triangle and NaN refused.
    Vector2Double CornerA = Vector2Double_Zero();
    Vector2Double CornerB = Vector2Double_Create(4.0, 0.0);
    Vector2Double CornerC = Vector2Double_Create(0.0, 4.0);
    Vector3Double Weights = Vector3Double_Zero();
    TEST_CHECK(run, Vector2Double_TryGetBarycentric(CornerB, CornerA, CornerB, CornerC, &Weights)
        && Vector3Double_EqualsClose(Weights, Vector3Double_Create(0.0, 1.0, 0.0), 0.0));
    TEST_CHECK(run, Vector2Double_TryGetBarycentric(Vector2Double_One(), CornerA, CornerB, CornerC, &Weights)
        && Vector3Double_EqualsClose(Weights, Vector3Double_Create(0.5, 0.25, 0.25), 0.0));
    TEST_CHECK(run, Vector2Double_TryGetBarycentric(Vector2Double_Create(5.0, 5.0), CornerA, CornerB, CornerC, &Weights)
        && (Weights.X == -1.5));
    TEST_CHECK(run, Vector2Double_TryGetBarycentric(Vector2Double_Add(FarStart, Vector2Double_One()), Vector2Double_Add(
        FarStart, CornerA), Vector2Double_Add(FarStart, CornerB), Vector2Double_Add(FarStart, CornerC), &Weights)
        && Vector3Double_EqualsClose(Weights, Vector3Double_Create(0.5, 0.25, 0.25), 1e-12));
    Random Generator;
    Random_Construct2(&Generator, (RandomState){ .Values = { 0x2D3U, 0x3U, 0x2U, 0x1U } });
    for (size_t Index = 0U; Index < RANDOM_CASE_COUNT; Index++)
    {
        Vector2Double Corners[3] =
        {
            Vector2Double_Create(RandomDouble(&Generator, -10.0, 10.0), RandomDouble(&Generator, -10.0, 10.0)),
            Vector2Double_Create(RandomDouble(&Generator, -10.0, 10.0), RandomDouble(&Generator, -10.0, 10.0)),
            Vector2Double_Create(RandomDouble(&Generator, -10.0, 10.0), RandomDouble(&Generator, -10.0, 10.0))
        };
        Vector2Double Point = Vector2Double_Create(RandomDouble(&Generator, -10.0, 10.0), RandomDouble(&Generator, -10.0,
            10.0));
        if (fabs(Vector2Double_Cross(Vector2Double_Subtract(Corners[1], Corners[0]), Vector2Double_Subtract(Corners[2],
            Corners[0]))) < 1.0)
        {
            continue; // too thin to rebuild the point to 1e-9
        }
        if (!TEST_CHECK(run, Vector2Double_TryGetBarycentric(Point, Corners[0], Corners[1], Corners[2], &Weights)))
        {
            continue;
        }
        Vector2Double Rebuilt = Vector2Double_Add(Vector2Double_Add(Vector2Double_Scale(Corners[0], Weights.X),
            Vector2Double_Scale(Corners[1], Weights.Y)), Vector2Double_Scale(Corners[2], Weights.Z));
        TEST_CHECK(run, Vector2Double_EqualsClose(Rebuilt, Point, 1e-9));
        TEST_CHECK(run, fabs((Weights.X + Weights.Y + Weights.Z) - 1.0) < 1e-12);
    }
    Random_Deconstruct1(&Generator);
    Vector3Double Untouched = Vector3Double_Create(7.0, 8.0, 9.0);
    Weights = Untouched;
    TEST_CHECK(run, !Vector2Double_TryGetBarycentric(Vector2Double_One(), CornerA, Vector2Double_One(),
        Vector2Double_Create(2.0, 2.0), &Weights) && Vector3Double_EqualsClose(Weights, Untouched, 0.0));
    TEST_CHECK(run, !Vector2Double_TryGetBarycentric(Vector2Double_Create(NAN_DOUBLE, 0.0), CornerA, CornerB, CornerC,
        &Weights) && Vector3Double_EqualsClose(Weights, Untouched, 0.0));

    // 2D points and directions through a 4x4 matrix: a quarter turn about Z, then a move by (10, 20).
    const double TurnRows[4][4] = { { 0, -1, 0, 10 }, { 1, 0, 0, 20 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } };
    Matrix4Double Turn = FromRows(TurnRows);
    TEST_CHECK(run, Vector2Double_EqualsClose(Matrix4Double_TransformPoint2D(&Turn, AxisX), Vector2Double_Create(10.0,
        21.0), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Matrix4Double_TransformPoint2D(&Turn, Vector2Double_Create(0.0, 2.0)),
        Vector2Double_Create(8.0, 20.0), 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Matrix4Double_TransformDirection2D(&Turn, AxisX), AxisY, 0.0));
    Vector3Double Moved = Matrix4Double_TransformPoint(&Turn, Vector3Double_Create(W.X, W.Y, 0.0));
    TEST_CHECK(run, Vector2Double_EqualsClose(Matrix4Double_TransformPoint2D(&Turn, W), Vector2Double_Create(Moved.X,
        Moved.Y), 0.0));
}


// Static functions: matrices.
static void MatricesByHand(TestRun* run)
{
    Matrix4Double Identity = Matrix4Double_Identity();
    for (size_t Index = 0U; Index < MATRIX4_DOUBLE_ENTRY_COUNT; Index++)
    {
        TEST_CHECK(run, Identity.M[Index] == (((Index % 5U) == 0U) ? 1.0 : 0.0));
    }

    // Translation and scale, and what they do to points and directions.
    Matrix4Double Translation = Matrix4Double_FromTranslation(Vector3Double_Create(1.0, 2.0, 3.0));
    const double TranslationRows[4][4] = { { 1, 0, 0, 1 }, { 0, 1, 0, 2 }, { 0, 0, 1, 3 }, { 0, 0, 0, 1 } };
    Matrix4Double ExpectedTranslation = FromRows(TranslationRows);
    TEST_CHECK(run, Matrix4Double_EqualsClose(&Translation, &ExpectedTranslation, 0.0));
    TEST_CHECK(run, (Translation.M[12] == 1.0) && (Translation.M[13] == 2.0) && (Translation.M[14] == 3.0));
    Matrix4Double Scale = Matrix4Double_FromScale(Vector3Double_Create(2.0, 3.0, -4.0));
    const double ScaleRows[4][4] = { { 2, 0, 0, 0 }, { 0, 3, 0, 0 }, { 0, 0, -4, 0 }, { 0, 0, 0, 1 } };
    Matrix4Double ExpectedScale = FromRows(ScaleRows);
    TEST_CHECK(run, Matrix4Double_EqualsClose(&Scale, &ExpectedScale, 0.0));
    Vector3Double Ones = Vector3Double_Create(1.0, 1.0, 1.0);
    TEST_CHECK(run, Vector3Double_EqualsClose(Matrix4Double_TransformPoint(&Translation, Ones),
        Vector3Double_Create(2.0, 3.0, 4.0), 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Matrix4Double_TransformDirection(&Translation, Ones), Ones, 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Matrix4Double_TransformPoint(&Scale, Ones),
        Vector3Double_Create(2.0, 3.0, -4.0), 0.0));

    // Quarter turns about each axis, by the right-hand rule.
    double Quarter = PI_DOUBLE / 2.0;
    Vector3Double AxisX = Vector3Double_Create(1.0, 0.0, 0.0);
    Vector3Double AxisY = Vector3Double_Create(0.0, 1.0, 0.0);
    Vector3Double AxisZ = Vector3Double_Create(0.0, 0.0, 1.0);
    Matrix4Double TurnX = Matrix4Double_FromRotationX(Quarter);
    Matrix4Double TurnY = Matrix4Double_FromRotationY(Quarter);
    Matrix4Double TurnZ = Matrix4Double_FromRotationZ(Quarter);
    TEST_CHECK(run, Vector3Double_EqualsClose(Matrix4Double_TransformDirection(&TurnX, AxisY), AxisZ, HAND_TOLERANCE));
    TEST_CHECK(run, Vector3Double_EqualsClose(Matrix4Double_TransformDirection(&TurnX, AxisZ), Vector3Double_Negate(AxisY),
        HAND_TOLERANCE));
    TEST_CHECK(run, Vector3Double_EqualsClose(Matrix4Double_TransformDirection(&TurnY, AxisZ), AxisX, HAND_TOLERANCE));
    TEST_CHECK(run, Vector3Double_EqualsClose(Matrix4Double_TransformDirection(&TurnY, AxisX), Vector3Double_Negate(AxisZ),
        HAND_TOLERANCE));
    TEST_CHECK(run, Vector3Double_EqualsClose(Matrix4Double_TransformDirection(&TurnZ, AxisX), AxisY, HAND_TOLERANCE));
    TEST_CHECK(run, Vector3Double_EqualsClose(Matrix4Double_TransformDirection(&TurnZ, AxisY), Vector3Double_Negate(AxisX),
        HAND_TOLERANCE));

    // XYZ with one angle is that axis's rotation; with three it is X(x) Y(y) Z(z) on column vectors: Z turns first.
    double Angle = 0.7;
    Matrix4Double OnlyX = Matrix4Double_FromRotationXYZ(Vector3Double_Create(Angle, 0.0, 0.0));
    Matrix4Double OnlyY = Matrix4Double_FromRotationXYZ(Vector3Double_Create(0.0, Angle, 0.0));
    Matrix4Double OnlyZ = Matrix4Double_FromRotationXYZ(Vector3Double_Create(0.0, 0.0, Angle));
    Matrix4Double RotationX = Matrix4Double_FromRotationX(Angle);
    Matrix4Double RotationY = Matrix4Double_FromRotationY(Angle);
    Matrix4Double RotationZ = Matrix4Double_FromRotationZ(Angle);
    TEST_CHECK(run, Matrix4Double_EqualsClose(&OnlyX, &RotationX, HAND_TOLERANCE));
    TEST_CHECK(run, Matrix4Double_EqualsClose(&OnlyY, &RotationY, HAND_TOLERANCE));
    TEST_CHECK(run, Matrix4Double_EqualsClose(&OnlyZ, &RotationZ, HAND_TOLERANCE));
    Vector3Double Angles = Vector3Double_Create(0.3, -1.1, 2.4);
    Matrix4Double Combined = Matrix4Double_FromRotationXYZ(Angles);
    Matrix4Double PartX = Matrix4Double_FromRotationX(Angles.X);
    Matrix4Double PartY = Matrix4Double_FromRotationY(Angles.Y);
    Matrix4Double PartZ = Matrix4Double_FromRotationZ(Angles.Z);
    Matrix4Double ZThenY = Matrix4Double_Multiply(&PartZ, &PartY);
    Matrix4Double ZThenYThenX = Matrix4Double_Multiply(&ZThenY, &PartX);
    TEST_CHECK(run, Matrix4Double_EqualsClose(&Combined, &ZThenYThenX, HAND_TOLERANCE));
    double CosY = cos(Angles.Y);
    TEST_CHECK(run, fabs(Entry(&Combined, 0U, 0U) - (CosY * cos(Angles.Z))) < HAND_TOLERANCE);
    TEST_CHECK(run, fabs(Entry(&Combined, 0U, 1U) + (CosY * sin(Angles.Z))) < HAND_TOLERANCE);
    TEST_CHECK(run, fabs(Entry(&Combined, 0U, 2U) - sin(Angles.Y)) < HAND_TOLERANCE);
    TEST_CHECK(run, fabs(Entry(&Combined, 1U, 2U) + (sin(Angles.X) * CosY)) < HAND_TOLERANCE);
    TEST_CHECK(run, fabs(Entry(&Combined, 2U, 2U) - (cos(Angles.X) * CosY)) < HAND_TOLERANCE);

    // Multiply(a, b) applies a first: translate then scale, or scale then translate.
    Matrix4Double Move = Matrix4Double_FromTranslation(AxisX);
    Matrix4Double Doubling = Matrix4Double_FromScale(Vector3Double_Create(2.0, 2.0, 2.0));
    Matrix4Double MoveThenDouble = Matrix4Double_Multiply(&Move, &Doubling);
    Matrix4Double DoubleThenMove = Matrix4Double_Multiply(&Doubling, &Move);
    TEST_CHECK(run, Vector3Double_EqualsClose(Matrix4Double_TransformPoint(&MoveThenDouble, AxisX),
        Vector3Double_Create(4.0, 0.0, 0.0), 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Matrix4Double_TransformPoint(&DoubleThenMove, AxisX),
        Vector3Double_Create(3.0, 0.0, 0.0), 0.0));

    // A full product against the definition (b * a)(r, c) = sum over k of b(r, k) a(k, c).
    const double RowsA[4][4] = { { 1, 2, 3, 4 }, { -1, 0, 5, 2 }, { 3, -2, 1, 0 }, { 0.5, 1, -1, 2 } };
    const double RowsB[4][4] = { { 2, -1, 0, 3 }, { 1, 4, -2, 0 }, { 0, 1, 1, -1 }, { 3, 0, 2, 1 } };
    Matrix4Double MatrixA = FromRows(RowsA);
    Matrix4Double MatrixB = FromRows(RowsB);
    Matrix4Double Product = Matrix4Double_Multiply(&MatrixA, &MatrixB);
    for (size_t Row = 0U; Row < 4U; Row++)
    {
        for (size_t Column = 0U; Column < 4U; Column++)
        {
            double Sum = 0.0;
            for (size_t K = 0U; K < 4U; K++)
            {
                Sum += RowsB[Row][K] * RowsA[K][Column];
            }
            TEST_CHECK(run, Entry(&Product, Row, Column) == Sum);
        }
    }

    // Transpose swaps rows and columns, twice gives the matrix back.
    Matrix4Double Transposed = Matrix4Double_Transpose(&MatrixA);
    for (size_t Row = 0U; Row < 4U; Row++)
    {
        for (size_t Column = 0U; Column < 4U; Column++)
        {
            TEST_CHECK(run, Entry(&Transposed, Row, Column) == RowsA[Column][Row]);
        }
    }
    Matrix4Double TwiceTransposed = Matrix4Double_Transpose(&Transposed);
    TEST_CHECK(run, Memory_IsEqual(&TwiceTransposed, &MatrixA, sizeof(Matrix4Double)));

    // Determinants: a scale, a mirror, a swap of two axes, a turn, a translation, a block matrix, a singular one.
    const double DiagonalRows[4][4] = { { 2, 0, 0, 1 }, { 0, 3, 0, 2 }, { 0, 0, 4, 3 }, { 0, 0, 0, 1 } };
    Matrix4Double Diagonal = FromRows(DiagonalRows);
    TEST_CHECK(run, Matrix4Double_GetDeterminant(&Diagonal) == 24.0);
    TEST_CHECK(run, Matrix4Double_GetDeterminant(&Scale) == -24.0);
    const double SwapRows[4][4] = { { 0, 1, 0, 0 }, { 1, 0, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } };
    Matrix4Double Swap = FromRows(SwapRows);
    TEST_CHECK(run, Matrix4Double_GetDeterminant(&Swap) == -1.0);
    TEST_CHECK(run, fabs(Matrix4Double_GetDeterminant(&Combined) - 1.0) < HAND_TOLERANCE);
    TEST_CHECK(run, Matrix4Double_GetDeterminant(&Translation) == 1.0);
    const double BlockRows[4][4] = { { 1, 2, 0, 0 }, { 3, 4, 0, 0 }, { 0, 0, 5, 6 }, { 0, 0, 7, 8 } };
    Matrix4Double Block = FromRows(BlockRows);
    TEST_CHECK(run, Matrix4Double_GetDeterminant(&Block) == 4.0);
    const double CountingRows[4][4] = { { 1, 2, 3, 4 }, { 5, 6, 7, 8 }, { 9, 10, 11, 12 }, { 13, 14, 15, 16 } };
    Matrix4Double Counting = FromRows(CountingRows);
    TEST_CHECK(run, Matrix4Double_GetDeterminant(&Counting) == 0.0);

    // The translation, read and replaced; nothing else changes.
    Matrix4Double Placed = Combined;
    TEST_CHECK(run, Vector3Double_EqualsClose(Matrix4Double_GetTranslation(&Placed), Vector3Double_Zero(), 0.0));
    Matrix4Double_SetTranslation(&Placed, Vector3Double_Create(7.0, -8.0, 9.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Matrix4Double_GetTranslation(&Placed), Vector3Double_Create(7.0, -8.0, 9.0),
        0.0));
    TEST_CHECK(run, (Placed.M[12] == 7.0) && (Placed.M[13] == -8.0) && (Placed.M[14] == 9.0));
    for (size_t Index = 0U; Index < 12U; Index++)
    {
        TEST_CHECK(run, Placed.M[Index] == Combined.M[Index]);
    }
    TEST_CHECK(run, Placed.M[15] == 1.0);

    // Nearly equal and finite.
    Matrix4Double Nudged = Identity;
    Nudged.M[9] += 1e-9;
    TEST_CHECK(run, Matrix4Double_EqualsClose(&Identity, &Nudged, 1e-8));
    TEST_CHECK(run, !Matrix4Double_EqualsClose(&Identity, &Nudged, 1e-10));
    TEST_CHECK(run, Matrix4Double_IsFinite(&Identity) && Matrix4Double_IsFinite(&Counting));
    const size_t BadIndices[3] = { 0U, 3U, 14U };
    const double BadValues[3] = { NAN_DOUBLE, INFINITY_POS_DOUBLE, INFINITY_NEG_DOUBLE };
    for (size_t Index = 0U; Index < 3U; Index++)
    {
        Matrix4Double Broken = Identity;
        Broken.M[BadIndices[Index]] = BadValues[Index];
        TEST_CHECK(run, !Matrix4Double_IsFinite(&Broken));
    }
    Matrix4Double WithNaN = Identity;
    WithNaN.M[6] = NAN_DOUBLE;
    TEST_CHECK(run, !Matrix4Double_EqualsClose(&WithNaN, &WithNaN, INFINITY_POS_DOUBLE));
}

static void InversionByHandAndRefusals(TestRun* run)
{
    // Scale by (2, 4, 8), then move by (1, 2, 3): the inverse is exact in binary.
    Matrix4Double Scale = Matrix4Double_FromScale(Vector3Double_Create(2.0, 4.0, 8.0));
    Matrix4Double Move = Matrix4Double_FromTranslation(Vector3Double_Create(1.0, 2.0, 3.0));
    Matrix4Double Placed = Matrix4Double_Multiply(&Scale, &Move);
    const double InverseRows[4][4] =
    {
        { 0.5, 0, 0, -0.5 }, { 0, 0.25, 0, -0.5 }, { 0, 0, 0.125, -0.375 }, { 0, 0, 0, 1 }
    };
    Matrix4Double Expected = FromRows(InverseRows);
    Matrix4Double Inverse;
    Matrix4Double AffineInverse;
    TEST_CHECK(run, Matrix4Double_TryInvert(&Placed, &Inverse) && Matrix4Double_EqualsClose(&Inverse, &Expected, 0.0));
    TEST_CHECK(run, Matrix4Double_TryInvertAffine(&Placed, &AffineInverse)
        && Matrix4Double_EqualsClose(&AffineInverse, &Expected, 0.0));

    // A turn's inverse is its transpose.
    Matrix4Double Turn = Matrix4Double_FromRotationXYZ(Vector3Double_Create(0.3, -1.1, 2.4));
    Matrix4Double TurnTransposed = Matrix4Double_Transpose(&Turn);
    TEST_CHECK(run, Matrix4Double_TryInvert(&Turn, &Inverse)
        && Matrix4Double_EqualsClose(&Inverse, &TurnTransposed, HAND_TOLERANCE));
    TEST_CHECK(run, Matrix4Double_TryInvertAffine(&Turn, &AffineInverse)
        && Matrix4Double_EqualsClose(&AffineInverse, &TurnTransposed, HAND_TOLERANCE));

    // A matrix that is not affine: two 2x2 blocks, each inverted on its own.
    const double BlockRows[4][4] = { { 1, 2, 0, 0 }, { 3, 4, 0, 0 }, { 0, 0, 5, 6 }, { 0, 0, 7, 8 } };
    const double BlockInverseRows[4][4] =
    {
        { -2, 1, 0, 0 }, { 1.5, -0.5, 0, 0 }, { 0, 0, -4, 3 }, { 0, 0, 3.5, -2.5 }
    };
    Matrix4Double Block = FromRows(BlockRows);
    Matrix4Double BlockInverse = FromRows(BlockInverseRows);
    TEST_CHECK(run, Matrix4Double_TryInvert(&Block, &Inverse) && Matrix4Double_EqualsClose(&Inverse, &BlockInverse, 0.0));

    // The affine inverse ignores the bottom row and writes (0, 0, 0, 1).
    Matrix4Double Skewed = Placed;
    Skewed.M[3] = 5.0;
    Skewed.M[11] = -2.0;
    Skewed.M[15] = 3.0;
    TEST_CHECK(run, Matrix4Double_TryInvertAffine(&Skewed, &AffineInverse)
        && Matrix4Double_EqualsClose(&AffineInverse, &Expected, 0.0));

    // Either may write over its input.
    Matrix4Double InPlace = Placed;
    TEST_CHECK(run, Matrix4Double_TryInvert(&InPlace, &InPlace) && Matrix4Double_EqualsClose(&InPlace, &Expected, 0.0));
    InPlace = Placed;
    TEST_CHECK(run, Matrix4Double_TryInvertAffine(&InPlace, &InPlace) && Matrix4Double_EqualsClose(&InPlace, &Expected,
        0.0));

    // Refused: a zero scale on any axis (also turned and moved), no axes at all, two equal axes, a singular matrix
    // that is not affine, NaN and infinite entries. The output stays untouched.
    Matrix4Double Refused[9];
    size_t RefusedCount = 0U;
    for (size_t Axis = 0U; Axis < 3U; Axis++)
    {
        Vector3Double Flat = Vector3Double_Create(1.0, 1.0, 1.0);
        SetComponent(&Flat, Axis, 0.0);
        Matrix4Double FlatScale = Matrix4Double_FromScale(Flat);
        Matrix4Double FlatTurned = Matrix4Double_Multiply(&FlatScale, &Turn);
        Refused[RefusedCount++] = Matrix4Double_Multiply(&FlatTurned, &Move);
    }
    Matrix4Double FlattenY = Matrix4Double_FromScale(Vector3Double_Create(1.0, 0.0, 1.0));
    Refused[RefusedCount++] = Matrix4Double_Multiply(&Turn, &FlattenY); // turned, then flattened onto y = 0: a zero row
    Refused[RefusedCount++] = Filled(0.0);
    const double EqualAxesRows[4][4] = { { 1, 4, 1, 0 }, { 2, 5, 2, 0 }, { 3, 6, 3, 0 }, { 0, 0, 0, 1 } };
    Refused[RefusedCount++] = FromRows(EqualAxesRows);
    Refused[RefusedCount] = Placed;
    Refused[RefusedCount++].M[5] = NAN_DOUBLE;
    Refused[RefusedCount] = Placed;
    Refused[RefusedCount++].M[8] = INFINITY_POS_DOUBLE;
    Refused[RefusedCount] = Placed;
    Refused[RefusedCount++].M[13] = NAN_DOUBLE; // in the translation: both refuse, the affine one by its result
    for (size_t Index = 0U; Index < RefusedCount; Index++)
    {
        Matrix4Double Untouched = Filled(7.0);
        Matrix4Double Out = Untouched;
        TEST_CHECK(run, !Matrix4Double_TryInvert(&Refused[Index], &Out));
        TEST_CHECK(run, !Matrix4Double_TryInvertAffine(&Refused[Index], &Out));
        TEST_CHECK(run, Memory_IsEqual(&Out, &Untouched, sizeof(Matrix4Double)));
    }
    const double CountingRows[4][4] = { { 1, 2, 3, 4 }, { 5, 6, 7, 8 }, { 9, 10, 11, 12 }, { 13, 14, 15, 16 } };
    Matrix4Double Counting = FromRows(CountingRows);
    TEST_CHECK(run, !Matrix4Double_TryInvert(&Counting, &Inverse));

    // A tiny scale still inverts; one whose determinant underflows to zero is refused, and so is one whose determinant
    // is not zero but too small to divide by (a subnormal: its reciprocal is infinite).
    Matrix4Double Small = Matrix4Double_FromScale(Vector3Double_Create(1e-100, 1e-100, 1e-100));
    TEST_CHECK(run, Matrix4Double_TryInvert(&Small, &Inverse) && (fabs(Inverse.M[0] - 1e100) < 1e85));
    TEST_CHECK(run, Matrix4Double_TryInvertAffine(&Small, &AffineInverse) && (fabs(AffineInverse.M[10] - 1e100) < 1e85));
    Matrix4Double Underflowing = Matrix4Double_FromScale(Vector3Double_Create(1e-120, 1e-120, 1e-120));
    TEST_CHECK(run, Matrix4Double_GetDeterminant(&Underflowing) == 0.0);
    TEST_CHECK(run, !Matrix4Double_TryInvert(&Underflowing, &Inverse));
    TEST_CHECK(run, !Matrix4Double_TryInvertAffine(&Underflowing, &AffineInverse));
    Matrix4Double Subnormal = Matrix4Double_FromScale(Vector3Double_Create(1e-160, 1e-160, 1.0));
    TEST_CHECK(run, (Matrix4Double_GetDeterminant(&Subnormal) > 0.0) && (Matrix4Double_GetDeterminant(&Subnormal) < 1e-310));
    Matrix4Double Untouched = Filled(7.0);
    Matrix4Double Out = Untouched;
    TEST_CHECK(run, !Matrix4Double_TryInvert(&Subnormal, &Out) && !Matrix4Double_TryInvertAffine(&Subnormal, &Out));
    TEST_CHECK(run, Memory_IsEqual(&Out, &Untouched, sizeof(Matrix4Double)));
    // The documented limit the other way: a determinant that overflows is refused, though the inverse would fit.
    Matrix4Double Huge = Matrix4Double_FromScale(Vector3Double_Create(1e103, 1e103, 1e103));
    TEST_CHECK(run, Matrix4Double_GetDeterminant(&Huge) == INFINITY_POS_DOUBLE);
    TEST_CHECK(run, !Matrix4Double_TryInvert(&Huge, &Out) && !Matrix4Double_TryInvertAffine(&Huge, &Out));
}

static void RaymathConventionsHold(TestRun* run)
{
    Random Generator;
    Random_Construct2(&Generator, (RandomState){ .Values = { 0x7A11U, 0x31U, 0x3U, 0x9U } });
    for (size_t Index = 0U; Index < RANDOM_CASE_COUNT; Index++)
    {
        // The same inputs on both sides: floats, widened exactly for the doubles.
        Vector3 AnglesF = RandomFloatVector(&Generator, -4.0f, 4.0f);
        Vector3 ScaleF = RandomFloatVector(&Generator, 0.25f, 3.0f);
        if (Random_NextBool(&Generator))
        {
            ScaleF.y = -ScaleF.y;
        }
        Vector3 TranslationF = RandomFloatVector(&Generator, -50.0f, 50.0f);
        Vector3 PointF = RandomFloatVector(&Generator, -10.0f, 10.0f);

        Matrix RotationR = MatrixRotateXYZ(AnglesF);
        Matrix ScaleR = MatrixScale(ScaleF.x, ScaleF.y, ScaleF.z);
        Matrix TranslationR = MatrixTranslate(TranslationF.x, TranslationF.y, TranslationF.z);
        Matrix PlacedR = MatrixMultiply(MatrixMultiply(ScaleR, RotationR), TranslationR);

        Matrix4Double Rotation = Matrix4Double_FromRotationXYZ(GameMath_FromVector3(AnglesF));
        Matrix4Double Scale = Matrix4Double_FromScale(GameMath_FromVector3(ScaleF));
        Matrix4Double Translation = Matrix4Double_FromTranslation(GameMath_FromVector3(TranslationF));
        Matrix4Double ScaleRotation = Matrix4Double_Multiply(&Scale, &Rotation);
        Matrix4Double Placed = Matrix4Double_Multiply(&ScaleRotation, &Translation);

        // Rotations, the multiply order, transpose and determinant.
        TEST_CHECK(run, IsNearRaylibMatrix(&Rotation, RotationR));
        Matrix4Double TurnX = Matrix4Double_FromRotationX(AnglesF.x);
        Matrix4Double TurnY = Matrix4Double_FromRotationY(AnglesF.y);
        Matrix4Double TurnZ = Matrix4Double_FromRotationZ(AnglesF.z);
        TEST_CHECK(run, IsNearRaylibMatrix(&TurnX, MatrixRotateX(AnglesF.x)));
        TEST_CHECK(run, IsNearRaylibMatrix(&TurnY, MatrixRotateY(AnglesF.y)));
        TEST_CHECK(run, IsNearRaylibMatrix(&TurnZ, MatrixRotateZ(AnglesF.z)));
        TEST_CHECK(run, IsNearRaylibMatrix(&Placed, PlacedR));
        Matrix4Double Reversed = Matrix4Double_Multiply(&Translation, &ScaleRotation);
        TEST_CHECK(run, IsNearRaylibMatrix(&Reversed, MatrixMultiply(TranslationR, MatrixMultiply(ScaleR, RotationR))));
        Matrix4Double Transposed = Matrix4Double_Transpose(&Placed);
        TEST_CHECK(run, IsNearRaylibMatrix(&Transposed, MatrixTranspose(PlacedR)));
        double Determinant = Matrix4Double_GetDeterminant(&Placed);
        TEST_CHECK(run, fabs(Determinant - (double)MatrixDeterminant(PlacedR)) <= (FLOAT_TOLERANCE * fmax(1.0,
            fabs(Determinant))));

        // Points and directions.
        TEST_CHECK(run, IsNearRaylibVector(Matrix4Double_TransformPoint(&Placed, GameMath_FromVector3(PointF)),
            Vector3Transform(PointF, PlacedR)));
        Matrix LinearR = PlacedR;
        LinearR.m12 = 0.0f;
        LinearR.m13 = 0.0f;
        LinearR.m14 = 0.0f;
        TEST_CHECK(run, IsNearRaylibVector(Matrix4Double_TransformDirection(&Placed, GameMath_FromVector3(PointF)),
            Vector3Transform(PointF, LinearR)));

        // Inversion, both ways.
        Matrix4Double Inverse;
        Matrix InverseR = MatrixInvert(PlacedR);
        TEST_CHECK(run, Matrix4Double_TryInvert(&Placed, &Inverse) && IsNearRaylibMatrix(&Inverse, InverseR));
        TEST_CHECK(run, Matrix4Double_TryInvertAffine(&Placed, &Inverse) && IsNearRaylibMatrix(&Inverse, InverseR));
    }
    Random_Deconstruct1(&Generator);

    // A projective matrix (a camera's view and projection) inverts like raymath's too.
    Matrix ViewR = MatrixLookAt((Vector3){ 3.0f, 2.0f, -5.0f }, (Vector3){ 0.5f, 1.0f, 0.0f }, (Vector3){ 0.0f, 1.0f, 0.0f });
    Matrix ProjectionR = MatrixPerspective(1.0, 16.0 / 9.0, 0.1, 100.0);
    Matrix ViewProjectionR = MatrixMultiply(ViewR, ProjectionR);
    Matrix4Double ViewProjection = GameMath_FromMatrix(ViewProjectionR);
    Matrix4Double Inverse;
    TEST_CHECK(run, Matrix4Double_TryInvert(&ViewProjection, &Inverse)
        && IsNearRaylibMatrix(&Inverse, MatrixInvert(ViewProjectionR)));
}

static void LayoutMatchesRaylib(TestRun* run)
{
    // A raylib Matrix whose every field holds its own number plus one: mN = N + 1.
    Matrix Numbered =
    {
        .m0 = 1.0f, .m1 = 2.0f, .m2 = 3.0f, .m3 = 4.0f, .m4 = 5.0f, .m5 = 6.0f, .m6 = 7.0f, .m7 = 8.0f,
        .m8 = 9.0f, .m9 = 10.0f, .m10 = 11.0f, .m11 = 12.0f, .m12 = 13.0f, .m13 = 14.0f, .m14 = 15.0f, .m15 = 16.0f
    };
    float16 Floats = MatrixToFloatV(Numbered);
    Matrix4Double Widened = GameMath_FromMatrix(Numbered);
    float Out[16];
    Matrix4Double_ToFloats(&Widened, Out);
    for (size_t Index = 0U; Index < MATRIX4_DOUBLE_ENTRY_COUNT; Index++)
    {
        TEST_CHECK(run, Floats.v[Index] == (float)(Index + 1U));
        TEST_CHECK(run, Widened.M[Index] == (double)(Index + 1U));
        TEST_CHECK(run, Out[Index] == Floats.v[Index]);
    }
    Matrix Back = GameMath_ToMatrix(&Widened);
    TEST_CHECK(run, Memory_IsEqual(&Back, &Numbered, sizeof(Matrix)));
    Matrix FromFloats = GameMath_MatrixFromFloats(Floats.v);
    TEST_CHECK(run, Memory_IsEqual(&FromFloats, &Numbered, sizeof(Matrix)));

    // Why field by field: raylib lays Matrix out row by row, so its memory is not the column-major order.
    float Memory[16];
    Memory_Copy(&Numbered, Memory, sizeof(Memory));
    TEST_CHECK(run, (Memory[1] == 5.0f) && (Memory[4] == 2.0f) && (Memory[3] == 13.0f));

    // Translations and scales land in the same fields as raymath's, bit for bit; a placed matrix agrees with
    // MatrixToFloatV of the one raymath builds.
    Matrix4Double Translation = Matrix4Double_FromTranslation(Vector3Double_Create(5.0, 6.0, 7.0));
    Matrix TranslationR = MatrixTranslate(5.0f, 6.0f, 7.0f);
    Matrix Converted = GameMath_ToMatrix(&Translation);
    TEST_CHECK(run, Memory_IsEqual(&Converted, &TranslationR, sizeof(Matrix)));
    TEST_CHECK(run, (Converted.m12 == 5.0f) && (Converted.m13 == 6.0f) && (Converted.m14 == 7.0f));
    Matrix4Double Scale = Matrix4Double_FromScale(Vector3Double_Create(2.0, 4.0, 8.0));
    Matrix ScaleR = MatrixScale(2.0f, 4.0f, 8.0f);
    Converted = GameMath_ToMatrix(&Scale);
    TEST_CHECK(run, Memory_IsEqual(&Converted, &ScaleR, sizeof(Matrix)));
    Matrix4Double Placed = Matrix4Double_Multiply(&Scale, &Translation);
    Matrix4Double_ToFloats(&Placed, Out);
    float16 PlacedFloats = MatrixToFloatV(MatrixMultiply(ScaleR, TranslationR));
    TEST_CHECK(run, Memory_IsEqual(Out, PlacedFloats.v, sizeof(Out)));
    Vector3 Angles = { 0.4f, -2.0f, 1.3f };
    Matrix4Double Turned = Matrix4Double_FromRotationXYZ(GameMath_FromVector3(Angles));
    Matrix4Double TurnedPlaced = Matrix4Double_Multiply(&Turned, &Translation);
    Matrix4Double_ToFloats(&TurnedPlaced, Out);
    float16 TurnedFloats = MatrixToFloatV(MatrixMultiply(MatrixRotateXYZ(Angles), TranslationR));
    for (size_t Index = 0U; Index < MATRIX4_DOUBLE_ENTRY_COUNT; Index++)
    {
        TEST_CHECK(run, fabsf(Out[Index] - TurnedFloats.v[Index]) < 1e-5f);
    }
}

static void GameMathBridgesRaylib(TestRun* run)
{
    Vector3 Floats = { 1.5f, -2.0f, 0.1f };
    Vector3Double Widened = GameMath_FromVector3(Floats);
    TEST_CHECK(run, (Widened.X == 1.5) && (Widened.Y == -2.0) && (Widened.Z == (double)0.1f));
    Vector3 Rounded = GameMath_ToVector3(Vector3Double_Create(1.5, -2.0, 0.1));
    TEST_CHECK(run, Memory_IsEqual(&Rounded, &Floats, sizeof(Vector3)));
    Vector3 Relative = GameMath_ToVector3Relative(Vector3Double_Create(FAR_DISTANCE + 0.25, -FAR_DISTANCE - 0.5,
        FAR_DISTANCE), Vector3Double_Create(FAR_DISTANCE, -FAR_DISTANCE, FAR_DISTANCE + 0.125));
    TEST_CHECK(run, (Relative.x == 0.25f) && (Relative.y == -0.5f) && (Relative.z == -0.125f));

    // A world matrix 10 000 km out (scaled and turned), and a camera a metre or so from it.
    Matrix4Double Scale = Matrix4Double_FromScale(Vector3Double_Create(0.5, 2.0, 1.25));
    Matrix4Double Rotation = Matrix4Double_FromRotationXYZ(Vector3Double_Create(0.3, -1.1, 2.4));
    Vector3Double Position = Vector3Double_Create(FAR_DISTANCE + 0.3125, -FAR_DISTANCE + 2.75, FAR_DISTANCE - 0.125);
    Matrix4Double Translation = Matrix4Double_FromTranslation(Position);
    Matrix4Double ScaleRotation = Matrix4Double_Multiply(&Scale, &Rotation);
    Matrix4Double World = Matrix4Double_Multiply(&ScaleRotation, &Translation);
    Vector3Double Origin = Vector3Double_Add(Position, Vector3Double_Create(0.75, -0.5, 1.25));
    Matrix RelativeMatrix = GameMath_ToMatrixRelative(&World, Origin);
    Matrix RoundedMatrix = GameMath_ToMatrix(&World);

    // Only the translation differs from the plain rounding, and it is the small offset, exactly (rounded first, each
    // axis would be a whole number of metres instead).
    float16 RelativeFloats = MatrixToFloatV(RelativeMatrix);
    float16 RoundedFloats = MatrixToFloatV(RoundedMatrix);
    TEST_CHECK(run, Memory_IsEqual(RelativeFloats.v, RoundedFloats.v, 12U * sizeof(float)));
    TEST_CHECK(run, RelativeFloats.v[15] == RoundedFloats.v[15]);
    TEST_CHECK(run, (RelativeMatrix.m12 == -0.75f) && (RelativeMatrix.m13 == 0.5f) && (RelativeMatrix.m14 == -1.25f));

    // A point on the object, drawn relative to the camera, is where the doubles put it to within float rounding.
    Vector3 Local = { 0.4f, -0.7f, 0.25f };
    Vector3Double Exact = Vector3Double_Subtract(Matrix4Double_TransformPoint(&World, GameMath_FromVector3(Local)),
        Origin);
    Vector3 Drawn = Vector3Transform(Local, RelativeMatrix);
    TEST_CHECK(run, Vector3Double_EqualsClose(GameMath_FromVector3(Drawn), Exact, 1e-6));
    // Rounded first and moved by the camera after, it is off by up to half a float step out there (0.5 m).
    Vector3 Naive = Vector3Subtract(Vector3Transform(Local, RoundedMatrix), GameMath_ToVector3(Origin));
    TEST_CHECK(run, Vector3Distance(Naive, Drawn) > 0.01f);
}

static void InversesArePreciseFarAway(TestRun* run)
{
    Random Generator;
    Random_Construct2(&Generator, (RandomState){ .Values = { 0x1E7U, 0x10U, 0xCAFEU, 0x5U } });
    double WorstFloat = 0.0;
    for (size_t Index = 0U; Index < RANDOM_CASE_COUNT; Index++)
    {
        Matrix4Double Placed = RandomAffine(&Generator, FAR_DISTANCE);
        Vector3Double Translation = Matrix4Double_GetTranslation(&Placed);
        double TranslationSize = fmax(fabs(Translation.X), fmax(fabs(Translation.Y), fabs(Translation.Z)));
        Matrix4Double Inverse;
        Matrix4Double AffineInverse;
        if (!TEST_CHECK(run, Matrix4Double_TryInvert(&Placed, &Inverse)
            && Matrix4Double_TryInvertAffine(&Placed, &AffineInverse)))
        {
            continue;
        }
        // Both orders. (For the affine inverse, ThenBack's translation cancels exactly: it sums the same products in
        // the same order as the inverse's translation did. BackThen measures it.)
        const Matrix4Double* Inverses[2] = { &Inverse, &AffineInverse };
        for (size_t Which = 0U; Which < 2U; Which++)
        {
            Matrix4Double ThenBack = Matrix4Double_Multiply(&Placed, Inverses[Which]);
            Matrix4Double BackThen = Matrix4Double_Multiply(Inverses[Which], &Placed);
            TEST_CHECK(run, IsIdentityWithin(&ThenBack, TranslationSize));
            TEST_CHECK(run, IsIdentityWithin(&BackThen, TranslationSize));
        }

        // The same in raymath's floats is off by about a float step at that size: the test above tells them apart.
        Matrix PlacedR = GameMath_ToMatrix(&Placed);
        Matrix4Double FloatProduct = GameMath_FromMatrix(MatrixMultiply(PlacedR, MatrixInvert(PlacedR)));
        for (size_t Row = 0U; Row < 3U; Row++)
        {
            WorstFloat = fmax(WorstFloat, fabs(Entry(&FloatProduct, Row, 3U)) / fmax(1.0, TranslationSize));
        }
    }
    TEST_CHECK(run, WorstFloat > (1000.0 * INVERSE_TOLERANCE));
    Random_Deconstruct1(&Generator);
}


// Static functions: boxes.
static void BoxesByHand(TestRun* run)
{
    // Two corners in either order.
    BoundingBoxDouble Box = BoundingBoxDouble_FromCorners(Vector3Double_Create(1.0, 5.0, -2.0),
        Vector3Double_Create(3.0, -1.0, 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Box.Min, Vector3Double_Create(1.0, -1.0, -2.0), 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Box.Max, Vector3Double_Create(3.0, 5.0, 0.0), 0.0));
    BoundingBoxDouble Swapped = BoundingBoxDouble_FromCorners(Vector3Double_Create(3.0, -1.0, 0.0),
        Vector3Double_Create(1.0, 5.0, -2.0));
    TEST_CHECK(run, Memory_IsEqual(&Box, &Swapped, sizeof(BoundingBoxDouble)));

    // Unions with a box and with a point.
    BoundingBoxDouble Unit = BoundingBoxDouble_FromCorners(Vector3Double_Zero(), Vector3Double_Create(1.0, 1.0, 1.0));
    BoundingBoxDouble Other = BoundingBoxDouble_FromCorners(Vector3Double_Create(2.0, -1.0, 0.5),
        Vector3Double_Create(3.0, 0.0, 4.0));
    BoundingBoxDouble Union = BoundingBoxDouble_Union(Unit, Other);
    TEST_CHECK(run, Vector3Double_EqualsClose(Union.Min, Vector3Double_Create(0.0, -1.0, 0.0), 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Union.Max, Vector3Double_Create(3.0, 1.0, 4.0), 0.0));
    BoundingBoxDouble WithPoint = BoundingBoxDouble_UnionPoint(Unit, Vector3Double_Create(2.0, 0.5, -1.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(WithPoint.Min, Vector3Double_Create(0.0, 0.0, -1.0), 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(WithPoint.Max, Vector3Double_Create(2.0, 1.0, 1.0), 0.0));
    BoundingBoxDouble WithInside = BoundingBoxDouble_UnionPoint(Unit, Vector3Double_Create(0.5, 0.5, 0.5));
    TEST_CHECK(run, Memory_IsEqual(&WithInside, &Unit, sizeof(BoundingBoxDouble)));

    // Expanded by a margin, and by none.
    BoundingBoxDouble Grown = BoundingBoxDouble_Expand(Unit, 0.5);
    TEST_CHECK(run, Vector3Double_EqualsClose(Grown.Min, Vector3Double_Create(-0.5, -0.5, -0.5), 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Grown.Max, Vector3Double_Create(1.5, 1.5, 1.5), 0.0));
    BoundingBoxDouble Same = BoundingBoxDouble_Expand(Unit, 0.0);
    TEST_CHECK(run, Memory_IsEqual(&Same, &Unit, sizeof(BoundingBoxDouble)));

    // Points: inside, on a face, an edge and a corner (all in), just outside on each axis, NaN.
    TEST_CHECK(run, BoundingBoxDouble_ContainsPoint(Unit, Vector3Double_Create(0.5, 0.5, 0.5)));
    TEST_CHECK(run, BoundingBoxDouble_ContainsPoint(Unit, Vector3Double_Create(1.0, 0.5, 0.5)));
    TEST_CHECK(run, BoundingBoxDouble_ContainsPoint(Unit, Vector3Double_Create(1.0, 1.0, 0.5)));
    TEST_CHECK(run, BoundingBoxDouble_ContainsPoint(Unit, Vector3Double_Zero()));
    for (size_t Axis = 0U; Axis < 3U; Axis++)
    {
        Vector3Double Above = Vector3Double_Create(0.5, 0.5, 0.5);
        Vector3Double Below = Above;
        SetComponent(&Above, Axis, 1.0000001);
        SetComponent(&Below, Axis, -0.0000001);
        TEST_CHECK(run, !BoundingBoxDouble_ContainsPoint(Unit, Above) && !BoundingBoxDouble_ContainsPoint(Unit, Below));
    }
    TEST_CHECK(run, !BoundingBoxDouble_ContainsPoint(Unit, Vector3Double_Create(0.5, NAN_DOUBLE, 0.5)));

    // Boxes in boxes.
    TEST_CHECK(run, BoundingBoxDouble_ContainsBox(Unit, Unit));
    TEST_CHECK(run, BoundingBoxDouble_ContainsBox(Grown, Unit) && !BoundingBoxDouble_ContainsBox(Unit, Grown));
    BoundingBoxDouble OnAFace = BoundingBoxDouble_FromCorners(Vector3Double_Create(0.5, 0.5, 0.5),
        Vector3Double_Create(1.0, 1.0, 1.0));
    TEST_CHECK(run, BoundingBoxDouble_ContainsBox(Unit, OnAFace));
    BoundingBoxDouble PokingOut = BoundingBoxDouble_FromCorners(Vector3Double_Create(0.5, 0.5, 0.5),
        Vector3Double_Create(1.0, 1.25, 1.0));
    TEST_CHECK(run, !BoundingBoxDouble_ContainsBox(Unit, PokingOut));

    // Overlaps: apart on each axis, touching face to face or at a corner, one in the other; both ways round.
    for (size_t Axis = 0U; Axis < 3U; Axis++)
    {
        Vector3Double Offset = Vector3Double_Zero();
        SetComponent(&Offset, Axis, 1.5);
        BoundingBoxDouble Apart = { .Min = Vector3Double_Add(Unit.Min, Offset), .Max = Vector3Double_Add(Unit.Max, Offset) };
        TEST_CHECK(run, !BoundingBoxDouble_Overlaps(Unit, Apart) && !BoundingBoxDouble_Overlaps(Apart, Unit));
        SetComponent(&Offset, Axis, 1.0);
        BoundingBoxDouble Touching = { .Min = Vector3Double_Add(Unit.Min, Offset),
            .Max = Vector3Double_Add(Unit.Max, Offset) };
        TEST_CHECK(run, BoundingBoxDouble_Overlaps(Unit, Touching) && BoundingBoxDouble_Overlaps(Touching, Unit));
    }
    BoundingBoxDouble Corner = BoundingBoxDouble_FromCorners(Vector3Double_Create(1.0, 1.0, 1.0),
        Vector3Double_Create(2.0, 2.0, 2.0));
    TEST_CHECK(run, BoundingBoxDouble_Overlaps(Unit, Corner));
    TEST_CHECK(run, BoundingBoxDouble_Overlaps(Grown, Unit) && BoundingBoxDouble_Overlaps(Unit, Grown));

    // Centre, extents, size, surface area.
    BoundingBoxDouble Offcentre = BoundingBoxDouble_FromCorners(Vector3Double_Create(-1.0, 2.0, -6.0),
        Vector3Double_Create(3.0, 4.0, -2.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(BoundingBoxDouble_GetCenter(Offcentre), Vector3Double_Create(1.0, 3.0, -4.0),
        0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(BoundingBoxDouble_GetExtents(Offcentre), Vector3Double_Create(2.0, 1.0, 2.0),
        0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(BoundingBoxDouble_GetSize(Offcentre), Vector3Double_Create(4.0, 2.0, 4.0),
        0.0));
    BoundingBoxDouble Brick = BoundingBoxDouble_FromCorners(Vector3Double_Zero(), Vector3Double_Create(1.0, 2.0, 3.0));
    TEST_CHECK(run, BoundingBoxDouble_GetSurfaceArea(Brick) == 22.0);
    BoundingBoxDouble Sheet = BoundingBoxDouble_FromCorners(Vector3Double_Zero(), Vector3Double_Create(2.0, 3.0, 0.0));
    TEST_CHECK(run, BoundingBoxDouble_GetSurfaceArea(Sheet) == 12.0);
    BoundingBoxDouble Point = BoundingBoxDouble_FromCorners(Vector3Double_Create(4.0, 5.0, 6.0),
        Vector3Double_Create(4.0, 5.0, 6.0));
    TEST_CHECK(run, BoundingBoxDouble_GetSurfaceArea(Point) == 0.0);
    // Far out the centre is exact, and near the largest double it does not overflow.
    BoundingBoxDouble Far = BoundingBoxDouble_FromCorners(Vector3Double_Create(FAR_DISTANCE, -FAR_DISTANCE, FAR_DISTANCE),
        Vector3Double_Create(FAR_DISTANCE + 1.0, -FAR_DISTANCE + 0.25, FAR_DISTANCE + 2.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(BoundingBoxDouble_GetCenter(Far),
        Vector3Double_Create(FAR_DISTANCE + 0.5, -FAR_DISTANCE + 0.125, FAR_DISTANCE + 1.0), 0.0));
    BoundingBoxDouble Enormous = BoundingBoxDouble_FromCorners(Vector3Double_Create(MAX_DOUBLE / 2.0, 0.0, 0.0),
        Vector3Double_Create(MAX_DOUBLE, 0.0, 0.0));
    TEST_CHECK(run, Vector3Double_IsFinite(BoundingBoxDouble_GetCenter(Enormous)));

    // Transformed by a quarter turn about Z (X goes to Y, Y to -X), then moved by (10, 20, 30).
    const double TurnRows[4][4] = { { 0, -1, 0, 10 }, { 1, 0, 0, 20 }, { 0, 0, 1, 30 }, { 0, 0, 0, 1 } };
    Matrix4Double Turn = FromRows(TurnRows);
    BoundingBoxDouble Turned = BoundingBoxDouble_Transform(Brick, &Turn);
    TEST_CHECK(run, Vector3Double_EqualsClose(Turned.Min, Vector3Double_Create(8.0, 20.0, 30.0), 0.0));
    TEST_CHECK(run, Vector3Double_EqualsClose(Turned.Max, Vector3Double_Create(10.0, 21.0, 33.0), 0.0));
    // A mirror keeps Min below Max.
    Matrix4Double Mirror = Matrix4Double_FromScale(Vector3Double_Create(-2.0, 1.0, 1.0));
    BoundingBoxDouble Mirrored = BoundingBoxDouble_Transform(BoundingBoxDouble_FromCorners(Vector3Double_Create(1.0, 0.0,
        0.0), Vector3Double_Create(2.0, 1.0, 1.0)), &Mirror);
    TEST_CHECK(run, (Mirrored.Min.X == -4.0) && (Mirrored.Max.X == -2.0));

    // Infinite bounds: a floor slab stays itself under the identity, and a quarter turn takes its one finite axis
    // (Y, to -X) and leaves the others infinite; a half-space stays a half-space. No 0 times infinity.
    BoundingBoxDouble Slab =
    {
        .Min = Vector3Double_Create(INFINITY_NEG_DOUBLE, -1.0, INFINITY_NEG_DOUBLE),
        .Max = Vector3Double_Create(INFINITY_POS_DOUBLE, 0.0, INFINITY_POS_DOUBLE)
    };
    Matrix4Double Identity = Matrix4Double_Identity();
    BoundingBoxDouble SlabUnmoved = BoundingBoxDouble_Transform(Slab, &Identity);
    TEST_CHECK(run, Memory_IsEqual(&SlabUnmoved.Min, &Slab.Min, sizeof(Vector3Double))
        && Memory_IsEqual(&SlabUnmoved.Max, &Slab.Max, sizeof(Vector3Double)));
    BoundingBoxDouble Wall = BoundingBoxDouble_Transform(Slab, &Turn);
    TEST_CHECK(run, (Wall.Min.X == 10.0) && (Wall.Max.X == 11.0));
    TEST_CHECK(run, (Wall.Min.Y == INFINITY_NEG_DOUBLE) && (Wall.Max.Y == INFINITY_POS_DOUBLE));
    TEST_CHECK(run, (Wall.Min.Z == INFINITY_NEG_DOUBLE) && (Wall.Max.Z == INFINITY_POS_DOUBLE));
    BoundingBoxDouble HalfSpace = { .Min = Vector3Double_Create(INFINITY_NEG_DOUBLE, 0.0, 0.0),
        .Max = Vector3Double_Create(1.0, 1.0, 1.0) };
    BoundingBoxDouble MovedHalfSpace = BoundingBoxDouble_Transform(HalfSpace, &Turn);
    TEST_CHECK(run, (MovedHalfSpace.Min.X == 9.0) && (MovedHalfSpace.Min.Y == INFINITY_NEG_DOUBLE)
        && (MovedHalfSpace.Min.Z == 30.0));
    TEST_CHECK(run, (MovedHalfSpace.Max.X == 10.0) && (MovedHalfSpace.Max.Y == 21.0) && (MovedHalfSpace.Max.Z == 31.0));
}

static void TransformedBoxesHoldEveryCorner(TestRun* run)
{
    Random Generator;
    Random_Construct2(&Generator, (RandomState){ .Values = { 0xB0EU, 0x8U, 0x11U, 0x2U } });
    for (size_t Index = 0U; Index < RANDOM_CASE_COUNT; Index++)
    {
        // Half the boxes 10 000 km out, some flat on an axis; matrices with mirrors and (multiplied) shears.
        double Reach = ((Index % 2U) == 0U) ? 100.0 : FAR_DISTANCE;
        Vector3Double Center = RandomVector(&Generator, -Reach, Reach);
        Vector3Double Extents = RandomVector(&Generator, 0.0, 5.0);
        if (Random_NextInt32InLimit(&Generator, 4) == 0)
        {
            SetComponent(&Extents, (size_t)Random_NextInt32InLimit(&Generator, 3), 0.0);
        }
        BoundingBoxDouble Box = { .Min = Vector3Double_Subtract(Center, Extents), .Max = Vector3Double_Add(Center, Extents) };
        Matrix4Double Inner = RandomAffine(&Generator, 0.0);
        Matrix4Double Outer = RandomAffine(&Generator, Reach);
        Matrix4Double Placement = Matrix4Double_Multiply(&Inner, &Outer);

        BoundingBoxDouble Moved = BoundingBoxDouble_Transform(Box, &Placement);
        Vector3Double Corners[8];
        GetCorners(Box, Corners);
        Vector3Double Lowest = Matrix4Double_TransformPoint(&Placement, Corners[0]);
        Vector3Double Highest = Lowest;
        for (size_t CornerIndex = 0U; CornerIndex < 8U; CornerIndex++)
        {
            Vector3Double MovedCorner = Matrix4Double_TransformPoint(&Placement, Corners[CornerIndex]);
            TEST_CHECK(run, BoundingBoxDouble_ContainsPoint(Moved, MovedCorner));
            Lowest = Vector3Double_Min(Lowest, MovedCorner);
            Highest = Vector3Double_Max(Highest, MovedCorner);
        }
        // Tight: the faces touch the outermost corners exactly.
        TEST_CHECK(run, Vector3Double_EqualsClose(Moved.Min, Lowest, 0.0) && Vector3Double_EqualsClose(Moved.Max, Highest,
            0.0));
    }
    Random_Deconstruct1(&Generator);
}

static void RaySlabTestByHand(TestRun* run)
{
    BoundingBoxDouble Unit = BoundingBoxDouble_FromCorners(Vector3Double_Zero(), Vector3Double_Create(1.0, 1.0, 1.0));
    Vector3Double AlongX = Vector3Double_Create(1.0, 0.0, 0.0);
    Vector3Double Before = Vector3Double_Create(-1.0, 0.5, 0.5);
    double Distance = 42.0;

    // Into a face; stopping short; ending exactly on it; the box behind; the origin inside or on a face.
    TEST_CHECK(run, BoundingBoxDouble_IntersectsRay(Unit, Before, AlongX, 10.0, &Distance) && (Distance == 1.0));
    Distance = 42.0;
    TEST_CHECK(run, !BoundingBoxDouble_IntersectsRay(Unit, Before, AlongX, 0.5, &Distance) && (Distance == 42.0));
    TEST_CHECK(run, BoundingBoxDouble_IntersectsRay(Unit, Before, AlongX, 1.0, &Distance) && (Distance == 1.0));
    TEST_CHECK(run, !BoundingBoxDouble_IntersectsRay(Unit, Vector3Double_Create(2.0, 0.5, 0.5), AlongX, 10.0, NULL));
    TEST_CHECK(run, BoundingBoxDouble_IntersectsRay(Unit, Vector3Double_Create(0.5, 0.5, 0.5),
        Vector3Double_Create(-0.3, 0.9, 0.1), 10.0, &Distance) && (Distance == 0.0));
    TEST_CHECK(run, BoundingBoxDouble_IntersectsRay(Unit, Vector3Double_Create(0.0, 0.5, 0.5),
        Vector3Double_Negate(AlongX), 10.0, &Distance) && (Distance == 0.0));

    // Parallel to a slab: along the top face's plane (it grazes the face), just above it, and with two axes zero.
    TEST_CHECK(run, BoundingBoxDouble_IntersectsRay(Unit, Vector3Double_Create(-1.0, 1.0, 0.5), AlongX, 10.0, &Distance)
        && (Distance == 1.0));
    TEST_CHECK(run, !BoundingBoxDouble_IntersectsRay(Unit, Vector3Double_Create(-1.0, 1.0000001, 0.5), AlongX, 10.0,
        NULL));
    TEST_CHECK(run, !BoundingBoxDouble_IntersectsRay(Unit, Vector3Double_Create(-1.0, 0.5, 1.5), AlongX, 10.0, NULL));
    // Rising off the top face's plane by a subnormal slope: above the box for every t > 0. Its slab is divided, never
    // multiplied by the infinite reciprocal (0 times infinity is NaN, which would drop the face).
    TEST_CHECK(run, !BoundingBoxDouble_IntersectsRay(Unit, Vector3Double_Create(-1.0, 1.0, 0.5),
        Vector3Double_Create(1.0, 1e-320, 0.0), 10.0, NULL));
    TEST_CHECK(run, BoundingBoxDouble_IntersectsRay(Unit, Vector3Double_Create(-1.0, 1.0, 0.5),
        Vector3Double_Create(1.0, -1e-320, 0.0), 10.0, &Distance) && (Distance == 1.0));

    // In through an edge; touching only an edge, or only a corner, on the way past.
    TEST_CHECK(run, BoundingBoxDouble_IntersectsRay(Unit, Vector3Double_Create(-1.0, -1.0, 0.5),
        Vector3Double_Create(1.0, 1.0, 0.0), 10.0, &Distance) && (Distance == 1.0));
    TEST_CHECK(run, BoundingBoxDouble_IntersectsRay(Unit, Vector3Double_Create(-1.0, 0.0, 0.5),
        Vector3Double_Create(1.0, 1.0, 0.0), 10.0, &Distance) && (Distance == 1.0));
    TEST_CHECK(run, BoundingBoxDouble_IntersectsRay(Unit, Vector3Double_Create(-1.0, 0.0, 0.0),
        Vector3Double_Create(1.0, 1.0, 1.0), 10.0, &Distance) && (Distance == 1.0));
    TEST_CHECK(run, !BoundingBoxDouble_IntersectsRay(Unit, Vector3Double_Create(-1.0, 0.0, 0.0),
        Vector3Double_Create(1.0, 1.0000001, 1.0), 10.0, NULL));

    // A zero direction tests the origin; a longer direction measures t in its lengths; an unbounded ray.
    TEST_CHECK(run, BoundingBoxDouble_IntersectsRay(Unit, Vector3Double_Create(0.5, 0.5, 0.5), Vector3Double_Zero(), 10.0,
        &Distance) && (Distance == 0.0));
    TEST_CHECK(run, !BoundingBoxDouble_IntersectsRay(Unit, Before, Vector3Double_Zero(), 10.0, NULL));
    TEST_CHECK(run, BoundingBoxDouble_IntersectsRay(Unit, Before, Vector3Double_Create(2.0, 0.0, 0.0), 10.0, &Distance)
        && (Distance == 0.5));
    TEST_CHECK(run, BoundingBoxDouble_IntersectsRay(Unit, Vector3Double_Create(-1e300, 0.5, 0.5), AlongX,
        INFINITY_POS_DOUBLE, &Distance) && (Distance == 1e300));

    // Bad input never hits: a NaN or infinite origin or direction, a negative or NaN length, an inverted or NaN box.
    TEST_CHECK(run, !BoundingBoxDouble_IntersectsRay(Unit, Vector3Double_Create(NAN_DOUBLE, 0.5, 0.5), AlongX, 10.0,
        NULL));
    TEST_CHECK(run, !BoundingBoxDouble_IntersectsRay(Unit, Vector3Double_Create(0.5, 0.5, 0.5),
        Vector3Double_Create(INFINITY_POS_DOUBLE, 0.0, 0.0), 10.0, NULL));
    TEST_CHECK(run, !BoundingBoxDouble_IntersectsRay(Unit, Vector3Double_Create(0.5, 0.5, 0.5), AlongX, -1.0, NULL));
    TEST_CHECK(run, !BoundingBoxDouble_IntersectsRay(Unit, Vector3Double_Create(0.5, 0.5, 0.5), AlongX, NAN_DOUBLE, NULL));
    BoundingBoxDouble Inverted = { .Min = Vector3Double_Create(1.0, 0.0, 0.0), .Max = Vector3Double_Create(0.0, 1.0, 1.0) };
    TEST_CHECK(run, !BoundingBoxDouble_IntersectsRay(Inverted, Before, AlongX, 10.0, NULL));
    BoundingBoxDouble Broken = Unit;
    Broken.Max.Y = NAN_DOUBLE;
    TEST_CHECK(run, !BoundingBoxDouble_IntersectsRay(Broken, Before, AlongX, 10.0, NULL));

    // 10 000 km out, as exact as at the origin.
    BoundingBoxDouble Far = BoundingBoxDouble_FromCorners(Vector3Double_Create(FAR_DISTANCE, 0.0, -FAR_DISTANCE),
        Vector3Double_Create(FAR_DISTANCE + 1.0, 1.0, -FAR_DISTANCE + 1.0));
    TEST_CHECK(run, BoundingBoxDouble_IntersectsRay(Far, Vector3Double_Create(FAR_DISTANCE - 0.25, 0.5,
        -FAR_DISTANCE + 0.5), AlongX, 10.0, &Distance) && (Distance == 0.25));
}

static void RaySlabTestMatchesBruteForce(TestRun* run)
{
    Random Generator;
    Random_Construct2(&Generator, (RandomState){ .Values = { 0x5AB5U, 0x7U, 0x3U, 0x1U } });
    const Vector3Double Places[2] =
    {
        Vector3Double_Zero(), Vector3Double_Create(FAR_DISTANCE, -FAR_DISTANCE, FAR_DISTANCE)
    };
    for (size_t Place = 0U; Place < 2U; Place++)
    {
        size_t HitCount = 0U;
        for (size_t Index = 0U; Index < RANDOM_RAY_COUNT; Index++)
        {
            Vector3Double Center = Vector3Double_Add(Places[Place], RandomVector(&Generator, -10.0, 10.0));
            Vector3Double Extents = RandomVector(&Generator, 0.05, 2.5);
            BoundingBoxDouble Box = { .Min = Vector3Double_Subtract(Center, Extents),
                .Max = Vector3Double_Add(Center, Extents) };
            Vector3Double Origin = Vector3Double_Add(Center, Vector3Double_Multiply(RandomVector(&Generator, -3.0, 3.0),
                Extents));
            // Half the rays aim at a point of the box grown by a tenth, so they hit or just miss near its edges.
            Vector3Double Direction = RandomVector(&Generator, -1.0, 1.0);
            if (Random_NextBool(&Generator))
            {
                Vector3Double Target = Vector3Double_Add(Center, Vector3Double_Multiply(RandomVector(&Generator, -1.1,
                    1.1), Extents));
                Direction = Vector3Double_Subtract(Target, Origin);
            }

            // Now and then one or two direction components are zero, and the origin sits on a face's plane there.
            int32_t Choice = Random_NextInt32InLimit(&Generator, 8);
            if (Choice < 3)
            {
                size_t Axis = (size_t)Random_NextInt32InLimit(&Generator, 3);
                SetComponent(&Direction, Axis, 0.0);
                if (Choice == 0)
                {
                    SetComponent(&Direction, (Axis + 1U) % 3U, 0.0);
                }
                if (Choice == 1)
                {
                    SetComponent(&Origin, Axis, Random_NextBool(&Generator) ? Component(Box.Min, Axis)
                        : Component(Box.Max, Axis));
                }
            }
            double MaxDistance = (Random_NextInt32InLimit(&Generator, 5) == 0) ? INFINITY_POS_DOUBLE
                : RandomDouble(&Generator, 0.0, 25.0);

            double SlabDistance = 0.0;
            bool IsSlabHit = BoundingBoxDouble_IntersectsRay(Box, Origin, Direction, MaxDistance, &SlabDistance);
            // The face by face check on the box grown and shrunk a little: a slab hit must hit the grown box no later,
            // and a hit on the shrunk box must be a slab hit no later.
            double OuterDistance = 0.0;
            double InnerDistance = 0.0;
            bool IsOuterHit = IntersectByFaces(BoundingBoxDouble_Expand(Box, RAY_FACE_MARGIN), Origin, Direction,
                MaxDistance, &OuterDistance);
            bool IsInnerHit = IntersectByFaces(BoundingBoxDouble_Expand(Box, -RAY_FACE_MARGIN), Origin, Direction,
                MaxDistance, &InnerDistance);
            if (IsSlabHit)
            {
                HitCount++;
                TEST_CHECK(run, IsOuterHit && (OuterDistance <= (SlabDistance + (RAY_DISTANCE_TOLERANCE
                    * fmax(1.0, SlabDistance)))));
            }
            if (IsInnerHit)
            {
                TEST_CHECK(run, IsSlabHit && (SlabDistance <= (InnerDistance + (RAY_DISTANCE_TOLERANCE
                    * fmax(1.0, InnerDistance)))));
            }
        }
        // Enough of both for the comparison to mean something.
        TEST_CHECK(run, (HitCount > (RANDOM_RAY_COUNT / 5U)) && (HitCount < ((RANDOM_RAY_COUNT * 4U) / 5U)));
    }
    Random_Deconstruct1(&Generator);
}


// Public functions.
void WRVectorMathTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "vectors_by_hand", VectorsByHand },
        { "normalize_falls_back_only_without_a_direction", NormalizeFallsBackOnlyWithoutADirection },
        { "float_conversions_round_once", FloatConversionsRoundOnce },
        { "vectors_3d_more_by_hand", Vectors3DMoreByHand },
        { "vectors_3d_agree_with_raymath", Vectors3DAgreeWithRaymath },
        { "vectors_2d_by_hand", Vectors2DByHand },
        { "vectors_2d_more_by_hand", Vectors2DMoreByHand },
        { "vectors_2d_agree_with_raymath", Vectors2DAgreeWithRaymath },
        { "matrices_by_hand", MatricesByHand },
        { "inversion_by_hand_and_refusals", InversionByHandAndRefusals },
        { "raymath_conventions_hold", RaymathConventionsHold },
        { "layout_matches_raylib", LayoutMatchesRaylib },
        { "game_math_bridges_raylib", GameMathBridgesRaylib },
        { "inverses_are_precise_far_away", InversesArePreciseFarAway },
        { "boxes_by_hand", BoxesByHand },
        { "transformed_boxes_hold_every_corner", TransformedBoxesHoldEveryCorner },
        { "ray_slab_test_by_hand", RaySlabTestByHand },
        { "ray_slab_test_matches_brute_force", RaySlabTestMatchesBruteForce },
    };
    TestRun_RunSuite(run, "WRVectorMath", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
