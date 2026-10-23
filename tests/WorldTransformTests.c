#include "TestSuites.h"
#include "GameMath.h"
#include "WorldTransform.h"
#include "raylib/raymath.h"
#include "wr/WRRandom.h"
#include <math.h>


// Macros.
/** How many random transforms the round-trip cases try. */
#define RANDOM_TRANSFORM_COUNT 2000U
/** How close two matrices must be, relative to their largest entry: the decomposition rounds its rotation and scale to
 *  floats. */
#define MATRIX_TOLERANCE 2e-5
/** How close two matrices computed entirely in double must be, relative to their largest entry. */
#define DOUBLE_MATRIX_TOLERANCE 1e-12
/** 10 000 km: the largest position an object may hold (WORLD_POSITION_LIMIT), where a double's step is 1.9 nm. */
#define FAR_DISTANCE 1.0e7


// Static functions: helpers.
static float RandomFloat(Random* random, float minimum, float maximum)
{
    return minimum + ((maximum - minimum) * (float)Random_NextDouble(random));
}

static Vector3 RandomVector(Random* random, float minimum, float maximum)
{
    return (Vector3){ RandomFloat(random, minimum, maximum), RandomFloat(random, minimum, maximum),
        RandomFloat(random, minimum, maximum) };
}

static Vector3Double RandomPosition(Random* random, double minimum, double maximum)
{
    return Vector3Double_Create(minimum + ((maximum - minimum) * Random_NextDouble(random)),
        minimum + ((maximum - minimum) * Random_NextDouble(random)),
        minimum + ((maximum - minimum) * Random_NextDouble(random)));
}

/* A scale with each axis in [0.2, 3], sometimes negative. */
static Vector3 RandomScale(Random* random, bool isMirrorAllowed)
{
    Vector3 Scale = RandomVector(random, 0.2f, 3.0f);
    if (isMirrorAllowed && (Random_NextInt32InLimit(random, 4) == 0))
    {
        Scale.x = -Scale.x;
    }
    if (isMirrorAllowed && (Random_NextInt32InLimit(random, 4) == 0))
    {
        Scale.z = -Scale.z;
    }
    return Scale;
}

static Vector3Double Widen(Vector3 vector)
{
    return GameMath_FromVector3(vector);
}

/* The twelve entries of an affine matrix (its axes, then its translation), for comparisons. */
static void GetEntries(const Matrix4Double* matrix, double outEntries[12])
{
    const size_t Indices[12] = { 0U, 1U, 2U, 4U, 5U, 6U, 8U, 9U, 10U, 12U, 13U, 14U };
    for (size_t Index = 0U; Index < 12U; Index++)
    {
        outEntries[Index] = matrix->M[Indices[Index]];
    }
}

/* Whether two affine matrices agree within @p tolerance of their largest entry (at least 1). */
static bool AreMatricesWithin(const Matrix4Double* a, const Matrix4Double* b, double tolerance)
{
    double A[12];
    double B[12];
    GetEntries(a, A);
    GetEntries(b, B);
    double Largest = 1.0;
    for (size_t Index = 0U; Index < 12U; Index++)
    {
        Largest = fmax(Largest, fmax(fabs(A[Index]), fabs(B[Index])));
    }
    for (size_t Index = 0U; Index < 12U; Index++)
    {
        if (!(fabs(A[Index] - B[Index]) <= (tolerance * Largest)))
        {
            return false;
        }
    }
    return true;
}

static bool AreMatricesNear(const Matrix4Double* a, const Matrix4Double* b)
{
    return AreMatricesWithin(a, b, MATRIX_TOLERANCE);
}

/* The rotation matrix of Euler XYZ angles, in double. */
static Matrix4Double GetTurn(Vector3 rotation)
{
    return Matrix4Double_FromRotationXYZ(Widen(rotation));
}

/* No sign hint: a mirror goes onto X. */
static const Vector3 NoSigns = { 1.0f, 1.0f, 1.0f };

/* Decomposes @p matrix with @p signs and checks that composing the parts gives it back. */
static bool RoundTripsWith(TestRun* run, const Matrix4Double* matrix, Vector3 signs)
{
    Vector3Double Position;
    Vector3 Rotation;
    Vector3 Scale;
    if (!TEST_CHECK(run, WorldTransform_Decompose(matrix, signs, &Position, &Rotation, &Scale)))
    {
        return false;
    }
    Matrix4Double Recomposed = WorldTransform_Compose(Position, Rotation, Scale);
    return AreMatricesNear(&Recomposed, matrix);
}

static bool RoundTrips(TestRun* run, const Matrix4Double* matrix)
{
    return RoundTripsWith(run, matrix, NoSigns);
}

/* -1 or 1 for each axis: the signs of @p scale. */
static Vector3 SignsOf(Vector3 scale)
{
    return (Vector3){ (scale.x < 0.0f) ? -1.0f : 1.0f, (scale.y < 0.0f) ? -1.0f : 1.0f, (scale.z < 0.0f) ? -1.0f : 1.0f };
}

/* The length of a matrix's axis column. */
static double GetAxisLength(const Matrix4Double* matrix, size_t axis)
{
    return Vector3Double_Length(Vector3Double_Create(matrix->M[axis * 4U], matrix->M[(axis * 4U) + 1U],
        matrix->M[(axis * 4U) + 2U]));
}

/* The largest stretch of a matrix's linear part, by brute force: the longest image of many unit directions. */
static double MeasureStretch(const Matrix4Double* matrix)
{
    double Largest = 0.0;
    const size_t Steps = 90U;
    for (size_t Theta = 0U; Theta <= Steps; Theta++)
    {
        for (size_t Phi = 0U; Phi < (2U * Steps); Phi++)
        {
            double T = PI * (double)Theta / (double)Steps;
            double P = PI * (double)Phi / (double)Steps;
            Vector3Double Direction = Vector3Double_Create(sin(T) * cos(P), cos(T), sin(T) * sin(P));
            Largest = fmax(Largest, Vector3Double_Length(Matrix4Double_TransformDirection(matrix, Direction)));
        }
    }
    return Largest;
}


// Static functions: the cases.
static void ComposeIsDrawModelExsOrder(TestRun* run)
{
    Vector3Double Position = Vector3Double_Create(1.0, -2.0, 3.5);
    Vector3 Rotation = { 0.3f, -1.1f, 2.4f };
    Vector3 Scale = { 2.0f, 0.5f, -1.5f };
    Matrix4Double Composed = WorldTransform_Compose(Position, Rotation, Scale);

    // The same products in double, operand for operand, give the same matrix bit for bit.
    Matrix4Double ScaleMatrix = Matrix4Double_FromScale(Widen(Scale));
    Matrix4Double Turn = GetTurn(Rotation);
    Matrix4Double Translation = Matrix4Double_FromTranslation(Position);
    Matrix4Double ScaledTurn = Matrix4Double_Multiply(&ScaleMatrix, &Turn);
    Matrix4Double Expected = Matrix4Double_Multiply(&ScaledTurn, &Translation);
    TEST_CHECK(run, Memory_IsEqual(&Expected, &Composed, sizeof(Matrix4Double)));

    // raylib's DrawModelEx composition in float agrees within float rounding.
    Matrix Raylib = MatrixMultiply(MatrixMultiply(MatrixScale(Scale.x, Scale.y, Scale.z), MatrixRotateXYZ(Rotation)),
        MatrixTranslate((float)Position.X, (float)Position.Y, (float)Position.Z));
    Matrix4Double RaylibWide = GameMath_FromMatrix(Raylib);
    TEST_CHECK(run, AreMatricesWithin(&RaylibWide, &Composed, 1e-6));

    // A point goes through the scale, then the rotation, then the translation; the translation is the position exactly.
    Vector3Double Point = Vector3Double_Create(0.25, 1.0, -0.5);
    Vector3Double ByHand = Vector3Double_Add(Matrix4Double_TransformDirection(&Turn,
        Vector3Double_Multiply(Point, Widen(Scale))), Position);
    Vector3Double ByMatrix = Matrix4Double_TransformPoint(&Composed, Point);
    TEST_CHECK(run, Vector3Double_Distance(ByHand, ByMatrix) < 1e-12);
    Vector3Double Translated = Matrix4Double_GetTranslation(&Composed);
    TEST_CHECK(run, (Translated.X == Position.X) && (Translated.Y == Position.Y) && (Translated.Z == Position.Z));
}

static void DecomposeRoundTripsRandomTransforms(TestRun* run)
{
    Random Generator;
    Random_Construct2(&Generator, (RandomState){ .Values = { 0x7AB5U, 0x0808U, 0x7E57U, 0x1U } });
    size_t RoundTripCount = 0U;
    size_t ExactCount = 0U;
    for (size_t Index = 0U; Index < RANDOM_TRANSFORM_COUNT; Index++)
    {
        Vector3Double Position = Widen(RandomVector(&Generator, -50.0f, 50.0f));
        Vector3 Rotation = RandomVector(&Generator, -7.0f, 7.0f); // beyond +-pi too: any angle works
        Vector3 Scale = RandomScale(&Generator, true);
        Matrix4Double Composed = WorldTransform_Compose(Position, Rotation, Scale);
        // Any hint gives the matrix back (the signs move between the scale and the turn).
        Vector3 AnySigns = { Random_NextBool(&Generator) ? 1.0f : -1.0f, Random_NextBool(&Generator) ? 1.0f : -1.0f,
            Random_NextBool(&Generator) ? 1.0f : -1.0f };
        RoundTripCount += (RoundTrips(run, &Composed) && RoundTripsWith(run, &Composed, AnySigns)) ? 1U : 0U;

        // With the scale's own signs as the hint, the parts come back themselves (the angles up to their turns).
        Vector3Double OutPosition;
        Vector3 OutRotation;
        Vector3 OutScale;
        WorldTransform_Decompose(&Composed, SignsOf(Scale), &OutPosition, &OutRotation, &OutScale);
        Matrix4Double OutTurn = GetTurn(OutRotation);
        Matrix4Double Turn = GetTurn(Rotation);
        ExactCount += ((Vector3Double_Distance(OutPosition, Position) == 0.0)
            && (Vector3Distance(OutScale, Scale) < (1e-5f * Vector3Length(Scale)))
            && AreMatricesNear(&OutTurn, &Turn)) ? 1U : 0U;
    }
    TEST_CHECK(run, RoundTripCount == RANDOM_TRANSFORM_COUNT);
    TEST_CHECK(run, ExactCount == RANDOM_TRANSFORM_COUNT);
    Random_Deconstruct1(&Generator);
}

static void DecomposeHoldsAtGimbalLock(TestRun* run)
{
    const float Turns[] = { -2.5f, -1.0f, 0.0f, 0.4f, 1.7f, 3.0f };
    const float Pitches[] = { PI / 2.0f, -PI / 2.0f, (PI / 2.0f) - 1e-4f, (-PI / 2.0f) + 1e-6f };
    for (size_t Pitch = 0U; Pitch < (sizeof(Pitches) / sizeof(Pitches[0])); Pitch++)
    {
        for (size_t X = 0U; X < (sizeof(Turns) / sizeof(Turns[0])); X++)
        {
            for (size_t Z = 0U; Z < (sizeof(Turns) / sizeof(Turns[0])); Z++)
            {
                Vector3 Rotation = { Turns[X], Pitches[Pitch], Turns[Z] };
                Matrix4Double Composed = WorldTransform_Compose(Vector3Double_Create(1.0, 2.0, 3.0), Rotation,
                    (Vector3){ 1.0f, 2.0f, 0.5f });
                TEST_CHECK(run, RoundTrips(run, &Composed));
            }
        }
    }
}

static void DecomposeHandlesZeroScalesAndMirrors(TestRun* run)
{
    Vector3 Rotation = { 0.7f, -0.4f, 1.9f };
    const Vector3 Scales[] =
    {
        { 0.0f, 1.0f, 2.0f }, { 1.0f, 0.0f, 2.0f }, { 1.0f, 2.0f, 0.0f }, { 0.0f, 0.0f, 3.0f }, { 0.0f, 2.0f, 0.0f },
        { 2.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { -1.0f, 1.0f, 1.0f }, { 1.0f, -1.0f, 1.0f },
        { -1.0f, -2.0f, -3.0f }, { -2.0f, 0.0f, 1.0f }
    };
    for (size_t Index = 0U; Index < (sizeof(Scales) / sizeof(Scales[0])); Index++)
    {
        Matrix4Double Composed = WorldTransform_Compose(Vector3Double_Create(4.0, 5.0, 6.0), Rotation, Scales[Index]);
        TEST_CHECK(run, RoundTrips(run, &Composed));
    }

    // Without a hint a single mirror reads back as a negative X scale; the lengths stay the axes' lengths.
    Matrix4Double Mirrored = WorldTransform_Compose(Vector3Double_Zero(), Rotation, (Vector3){ 1.0f, -2.0f, 3.0f });
    Vector3Double Position;
    Vector3 OutRotation;
    Vector3 Scale;
    WorldTransform_Decompose(&Mirrored, NoSigns, &Position, &OutRotation, &Scale);
    TEST_CHECK(run, (fabsf(Scale.x + 1.0f) < 1e-5f) && (fabsf(Scale.y - 2.0f) < 1e-5f) && (fabsf(Scale.z - 3.0f) < 1e-5f));
    // With the hint it stays on Y, the turn unchanged; a hint that contradicts the mirror gives way on X.
    WorldTransform_Decompose(&Mirrored, (Vector3){ 1.0f, -1.0f, 1.0f }, &Position, &OutRotation, &Scale);
    Matrix4Double OutTurn = GetTurn(OutRotation);
    Matrix4Double Turn = GetTurn(Rotation);
    TEST_CHECK(run, (fabsf(Scale.x - 1.0f) < 1e-5f) && (fabsf(Scale.y + 2.0f) < 1e-5f)
        && AreMatricesNear(&OutTurn, &Turn));
    WorldTransform_Decompose(&Mirrored, (Vector3){ -1.0f, -1.0f, 1.0f }, &Position, &OutRotation, &Scale);
    TEST_CHECK(run, (fabsf(Scale.x - 1.0f) < 1e-5f) && (fabsf(Scale.y + 2.0f) < 1e-5f));
    // Two mirrors are a half turn unless the hint keeps them.
    Matrix4Double Twice = WorldTransform_Compose(Vector3Double_Zero(), Vector3Zero(), (Vector3){ -1.0f, -1.0f, 1.0f });
    WorldTransform_Decompose(&Twice, NoSigns, &Position, &OutRotation, &Scale);
    TEST_CHECK(run, (Scale.x > 0.0f) && (Scale.y > 0.0f) && (fabsf(fabsf(OutRotation.z) - PI) < 1e-5f));
    WorldTransform_Decompose(&Twice, (Vector3){ -1.0f, -1.0f, 1.0f }, &Position, &OutRotation, &Scale);
    TEST_CHECK(run, (Scale.x == -1.0f) && (Scale.y == -1.0f) && (Vector3Length(OutRotation) < 1e-6f));
}

static void ShearGivesTheNearestTransform(TestRun* run)
{
    // A child turned 45 degrees about Y inside a parent stretched along X: the child's world axes are no longer at
    // right angles, which no position, rotation and scale can hold.
    Matrix4Double Parent = WorldTransform_Compose(Vector3Double_Zero(), Vector3Zero(), (Vector3){ 2.0f, 1.0f, 1.0f });
    Matrix4Double Child = WorldTransform_Compose(Vector3Double_Create(1.0, 0.0, 0.0),
        (Vector3){ 0.0f, PI / 4.0f, 0.0f }, (Vector3){ 1.0f, 1.0f, 1.0f });
    Matrix4Double World = Matrix4Double_Multiply(&Child, &Parent);
    Vector3Double Position;
    Vector3 Rotation;
    Vector3 Scale;
    TEST_CHECK(run, WorldTransform_Decompose(&World, NoSigns, &Position, &Rotation, &Scale));
    TEST_CHECK(run, (Position.X == World.M[12]) && (Position.Y == World.M[13]) && (Position.Z == World.M[14]));
    // The scale is each axis's length; the rotation keeps the X axis's direction exactly.
    TEST_CHECK(run, fabs((double)Scale.x - GetAxisLength(&World, 0U)) < 1e-6);
    TEST_CHECK(run, fabs((double)Scale.z - GetAxisLength(&World, 2U)) < 1e-6);
    Matrix4Double Turned = GetTurn(Rotation);
    Vector3Double AxisX = Vector3Double_NormalizeOr(Vector3Double_Create(World.M[0], World.M[1], World.M[2]),
        Vector3Double_Zero());
    TEST_CHECK(run, Vector3Double_Distance(AxisX, Vector3Double_Create(Turned.M[0], Turned.M[1], Turned.M[2])) < 1e-6);
    Matrix4Double Recomposed = WorldTransform_Compose(Position, Rotation, Scale);
    TEST_CHECK(run, !AreMatricesNear(&Recomposed, &World));
}

static void LocalMatrixUndoesTheParent(TestRun* run)
{
    Random Generator;
    Random_Construct2(&Generator, (RandomState){ .Values = { 0x10CA1U, 0x2U, 0x3U, 0x4U } });
    size_t Agreements = 0U;
    for (size_t Index = 0U; Index < RANDOM_TRANSFORM_COUNT; Index++)
    {
        Matrix4Double Parent = WorldTransform_Compose(Widen(RandomVector(&Generator, -20.0f, 20.0f)),
            RandomVector(&Generator, -4.0f, 4.0f), RandomScale(&Generator, true));
        Matrix4Double World = WorldTransform_Compose(Widen(RandomVector(&Generator, -20.0f, 20.0f)),
            RandomVector(&Generator, -4.0f, 4.0f), RandomScale(&Generator, true));
        Matrix4Double Local = Matrix4Double_Identity();
        if (TEST_CHECK(run, WorldTransform_TryGetLocalMatrix(&World, &Parent, &Local)))
        {
            // All in double: the product gives the world matrix back far beyond float precision.
            Matrix4Double Product = Matrix4Double_Multiply(&Local, &Parent);
            Agreements += AreMatricesWithin(&Product, &World, DOUBLE_MATRIX_TOLERANCE) ? 1U : 0U;
        }
    }
    TEST_CHECK(run, Agreements == RANDOM_TRANSFORM_COUNT);
    Random_Deconstruct1(&Generator);
}

static void ParentsThatCannotBeInvertedAreRefused(TestRun* run)
{
    Matrix4Double World = WorldTransform_Compose(Vector3Double_Create(1.0, 2.0, 3.0), Vector3Zero(),
        (Vector3){ 1.0f, 1.0f, 1.0f });
    Matrix4Double Local = Matrix4Double_Identity();
    Matrix4Double Flat = WorldTransform_Compose(Vector3Double_Zero(), (Vector3){ 0.2f, 0.3f, 0.4f },
        (Vector3){ 1.0f, 0.0f, 1.0f });
    TEST_CHECK(run, !WorldTransform_TryGetLocalMatrix(&World, &Flat, &Local));
    // Two axes almost along each other span almost no volume.
    Matrix4Double Sheared = Matrix4Double_Identity();
    Sheared.M[4] = 1.0;
    Sheared.M[5] = 1e-12;
    TEST_CHECK(run, !WorldTransform_TryGetLocalMatrix(&World, &Sheared, &Local));
    Matrix4Double Identity = Matrix4Double_Identity();
    TEST_CHECK(run, Memory_IsEqual(&Local, &Identity, sizeof(Matrix4Double))); // untouched on a refusal
    // A thin but upright parent is fine.
    Matrix4Double Thin = WorldTransform_Compose(Vector3Double_Zero(), Vector3Zero(), (Vector3){ 1.0f, 1e-4f, 1.0f });
    TEST_CHECK(run, WorldTransform_TryGetLocalMatrix(&World, &Thin, &Local));
    Matrix4Double Product = Matrix4Double_Multiply(&Local, &Thin);
    TEST_CHECK(run, AreMatricesNear(&Product, &World));
}

static void MaxScaleIsTheLongestAxisAndBoundsShear(TestRun* run)
{
    // Upright matrices: the longest axis; turned ones within rounding.
    const Vector3 Scales[] = { { 1.0f, 1.0f, 1.0f }, { 40.0f, 0.2f, 40.0f }, { 2.0f, 4.0f, 2.0f }, { 0.1f, 0.8f, 0.1f } };
    for (size_t Index = 0U; Index < (sizeof(Scales) / sizeof(Scales[0])); Index++)
    {
        Matrix4Double Upright = WorldTransform_Compose(Vector3Double_Create(3.0, 1.0, 2.0), Vector3Zero(),
            Scales[Index]);
        double Longest = fmax(GetAxisLength(&Upright, 0U), fmax(GetAxisLength(&Upright, 1U),
            GetAxisLength(&Upright, 2U)));
        TEST_CHECK(run, WorldTransform_GetMaxScale(&Upright) == Longest);
        Matrix4Double Turned = WorldTransform_Compose(Vector3Double_Zero(), (Vector3){ 0.5f, 1.2f, -0.3f },
            Scales[Index]);
        double LongestScale = fmax(fabs((double)Scales[Index].x), fmax(fabs((double)Scales[Index].y),
            fabs((double)Scales[Index].z)));
        TEST_CHECK(run, fabs(WorldTransform_GetMaxScale(&Turned) - LongestScale) < (1e-9 * LongestScale));
        // The translation does not matter, however far out.
        Matrix4Double Far = WorldTransform_Compose(Vector3Double_Create(FAR_DISTANCE, -FAR_DISTANCE, FAR_DISTANCE),
            (Vector3){ 0.5f, 1.2f, -0.3f }, Scales[Index]);
        TEST_CHECK(run, WorldTransform_GetMaxScale(&Far) == WorldTransform_GetMaxScale(&Turned));
    }

    // Sheared: never below the true stretch.
    Random Generator;
    Random_Construct2(&Generator, (RandomState){ .Values = { 0x5EEAU, 0x9U, 0x8U, 0x7U } });
    for (size_t Index = 0U; Index < 40U; Index++)
    {
        Matrix4Double Parent = WorldTransform_Compose(Vector3Double_Zero(), RandomVector(&Generator, -3.0f, 3.0f),
            RandomScale(&Generator, true));
        Matrix4Double Child = WorldTransform_Compose(Vector3Double_Zero(), RandomVector(&Generator, -3.0f, 3.0f),
            RandomScale(&Generator, true));
        Matrix4Double World = Matrix4Double_Multiply(&Child, &Parent);
        TEST_CHECK(run, WorldTransform_GetMaxScale(&World) >= MeasureStretch(&World));
    }
    Random_Deconstruct1(&Generator);
}

/* T33: far from the world's origin the positions keep their precision. A double's step at 10 000 km is 1.9 nm: every
 * offset below is kept to a few steps, where floats would round it to a metre. */
static void FarPlacementsKeepTheirPrecision(TestRun* run)
{
    // A child 0.5 mm from its parent, both turned and scaled, at 10 000 km: the world matrix keeps the 0.5 mm.
    Vector3Double ParentPosition = Vector3Double_Create(FAR_DISTANCE, 0.123456789, -FAR_DISTANCE);
    Vector3 ParentRotation = { 0.3f, -1.1f, 2.4f };
    Vector3 ParentScale = { 1.0f, 1.0f, 1.0f };
    Matrix4Double Parent = WorldTransform_Compose(ParentPosition, ParentRotation, ParentScale);
    Vector3Double Offset = Vector3Double_Create(0.0005, 0.0, 0.0);
    Matrix4Double ChildLocal = WorldTransform_Compose(Offset, (Vector3){ 0.0f, 0.2f, 0.0f },
        (Vector3){ 1.0f, 1.0f, 1.0f });
    Matrix4Double World = Matrix4Double_Multiply(&ChildLocal, &Parent);
    Vector3Double ChildWorld = Matrix4Double_GetTranslation(&World);
    TEST_CHECK(run, fabs(Vector3Double_Distance(ChildWorld, ParentPosition) - 0.0005) < 5e-9);
    Matrix4Double Turn = GetTurn(ParentRotation);
    Vector3Double Expected = Vector3Double_Add(ParentPosition, Matrix4Double_TransformDirection(&Turn, Offset));
    TEST_CHECK(run, Vector3Double_Distance(ChildWorld, Expected) < 5e-9);

    // Its world placement decomposes back to that position exactly (the translation is not rounded).
    Vector3Double Position;
    Vector3 Rotation;
    Vector3 Scale;
    TEST_CHECK(run, WorldTransform_Decompose(&World, NoSigns, &Position, &Rotation, &Scale));
    TEST_CHECK(run, (Position.X == ChildWorld.X) && (Position.Y == ChildWorld.Y) && (Position.Z == ChildWorld.Z));

    // Re-parented under another far parent and back, keeping the world placement: exact to a few double steps at
    // 10 000 km, and to 1e-9 m at 100 km (a step there is 1.5e-11 m).
    const double Distances[] = { FAR_DISTANCE, 1.0e5 };
    const double Tolerances[] = { 5e-9, 1e-9 };
    for (size_t Index = 0U; Index < 2U; Index++)
    {
        Vector3Double FarParent = Vector3Double_Create(-Distances[Index], Distances[Index] * 0.5, Distances[Index]);
        Matrix4Double First = WorldTransform_Compose(FarParent, (Vector3){ -0.7f, 0.4f, 1.3f },
            (Vector3){ 1.0f, 1.0f, 1.0f });
        Vector3Double Placed = Vector3Double_Add(FarParent, Vector3Double_Create(0.0123, -0.0004, 2.5));
        Matrix4Double Wanted = WorldTransform_Compose(Placed, (Vector3){ 0.1f, 0.2f, 0.3f },
            (Vector3){ 1.0f, 1.0f, 1.0f });
        Matrix4Double Local;
        TEST_CHECK(run, WorldTransform_TryGetLocalMatrix(&Wanted, &First, &Local));
        // Its local offset is small: under the parent's inverse it reads 2.5 m, not 10 000 km.
        TEST_CHECK(run, Vector3Double_Length(Matrix4Double_GetTranslation(&Local)) < 3.0);
        Matrix4Double Back = Matrix4Double_Multiply(&Local, &First);
        Vector3Double BackPosition = Matrix4Double_GetTranslation(&Back);
        TEST_CHECK(run, Vector3Double_Distance(BackPosition, Placed) < Tolerances[Index]);
    }

    // Random parents anywhere within 10 000 km, turned and scaled, and placements up to 10 m from them: the local
    // matrix puts each back within a few double steps.
    Random Generator;
    Random_Construct2(&Generator, (RandomState){ .Values = { 0xFA2U, 0x33U, 0x1U, 0x9U } });
    size_t ExactCount = 0U;
    for (size_t Index = 0U; Index < RANDOM_TRANSFORM_COUNT; Index++)
    {
        Vector3Double ParentAt = RandomPosition(&Generator, -FAR_DISTANCE, FAR_DISTANCE);
        Matrix4Double RandomParent = WorldTransform_Compose(ParentAt, RandomVector(&Generator, -4.0f, 4.0f),
            RandomScale(&Generator, true));
        Vector3Double ChildAt = Vector3Double_Add(ParentAt, RandomPosition(&Generator, -10.0, 10.0));
        Matrix4Double RandomChild = WorldTransform_Compose(ChildAt, RandomVector(&Generator, -4.0f, 4.0f),
            RandomScale(&Generator, true));
        Matrix4Double Local;
        if (!TEST_CHECK(run, WorldTransform_TryGetLocalMatrix(&RandomChild, &RandomParent, &Local)))
        {
            continue;
        }
        Matrix4Double Back = Matrix4Double_Multiply(&Local, &RandomParent);
        ExactCount += (Vector3Double_Distance(Matrix4Double_GetTranslation(&Back), ChildAt) < 5e-9) ? 1U : 0U;
    }
    TEST_CHECK(run, ExactCount == RANDOM_TRANSFORM_COUNT);
    Random_Deconstruct1(&Generator);
}


// Public functions.
void WorldTransformTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "compose_is_draw_model_exs_order", ComposeIsDrawModelExsOrder },
        { "decompose_round_trips_random_transforms", DecomposeRoundTripsRandomTransforms },
        { "decompose_holds_at_gimbal_lock", DecomposeHoldsAtGimbalLock },
        { "decompose_handles_zero_scales_and_mirrors", DecomposeHandlesZeroScalesAndMirrors },
        { "shear_gives_the_nearest_transform", ShearGivesTheNearestTransform },
        { "local_matrix_undoes_the_parent", LocalMatrixUndoesTheParent },
        { "parents_that_cannot_be_inverted_are_refused", ParentsThatCannotBeInvertedAreRefused },
        { "max_scale_is_the_longest_axis_and_bounds_shear", MaxScaleIsTheLongestAxisAndBoundsShear },
        { "far_placements_keep_their_precision", FarPlacementsKeepTheirPrecision },
    };
    TestRun_RunSuite(run, "WorldTransform", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
