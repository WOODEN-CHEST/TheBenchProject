#include "TestSuites.h"
#include "WorldTransform.h"
#include "raylib/raymath.h"
#include "wr/WRRandom.h"
#include <math.h>


// Macros.
/** How many random transforms the round-trip cases try. */
#define RANDOM_TRANSFORM_COUNT 2000U
/** How close two matrices must be, relative to their largest entry. */
#define MATRIX_TOLERANCE 2e-5f


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

/* The twelve entries of an affine matrix, for comparisons. */
static void GetEntries(Matrix matrix, float outEntries[12])
{
    const float Entries[12] = { matrix.m0, matrix.m1, matrix.m2, matrix.m4, matrix.m5, matrix.m6, matrix.m8, matrix.m9,
        matrix.m10, matrix.m12, matrix.m13, matrix.m14 };
    for (size_t Index = 0U; Index < 12U; Index++)
    {
        outEntries[Index] = Entries[Index];
    }
}

/* Whether two affine matrices agree within MATRIX_TOLERANCE of their largest entry (at least 1). */
static bool AreMatricesNear(Matrix a, Matrix b)
{
    float A[12];
    float B[12];
    GetEntries(a, A);
    GetEntries(b, B);
    float Largest = 1.0f;
    for (size_t Index = 0U; Index < 12U; Index++)
    {
        Largest = fmaxf(Largest, fmaxf(fabsf(A[Index]), fabsf(B[Index])));
    }
    for (size_t Index = 0U; Index < 12U; Index++)
    {
        if (!(fabsf(A[Index] - B[Index]) <= (MATRIX_TOLERANCE * Largest)))
        {
            return false;
        }
    }
    return true;
}

/* No sign hint: a mirror goes onto X. */
static const Vector3 NoSigns = { 1.0f, 1.0f, 1.0f };

/* Decomposes @p matrix with @p signs and checks that composing the parts gives it back. */
static bool RoundTripsWith(TestRun* run, Matrix matrix, Vector3 signs)
{
    Vector3 Position;
    Vector3 Rotation;
    Vector3 Scale;
    return TEST_CHECK(run, WorldTransform_Decompose(matrix, signs, &Position, &Rotation, &Scale))
        && AreMatricesNear(WorldTransform_Compose(Position, Rotation, Scale), matrix);
}

static bool RoundTrips(TestRun* run, Matrix matrix)
{
    return RoundTripsWith(run, matrix, NoSigns);
}

/* -1 or 1 for each axis: the signs of @p scale. */
static Vector3 SignsOf(Vector3 scale)
{
    return (Vector3){ (scale.x < 0.0f) ? -1.0f : 1.0f, (scale.y < 0.0f) ? -1.0f : 1.0f, (scale.z < 0.0f) ? -1.0f : 1.0f };
}

/* The largest stretch of a matrix's linear part, by brute force: the longest image of many unit directions. */
static float MeasureStretch(Matrix matrix)
{
    float Largest = 0.0f;
    const size_t Steps = 90U;
    for (size_t Theta = 0U; Theta <= Steps; Theta++)
    {
        for (size_t Phi = 0U; Phi < (2U * Steps); Phi++)
        {
            float T = PI * (float)Theta / (float)Steps;
            float P = PI * (float)Phi / (float)Steps;
            Vector3 Direction = { sinf(T) * cosf(P), cosf(T), sinf(T) * sinf(P) };
            Vector3 Stretched =
            {
                (matrix.m0 * Direction.x) + (matrix.m4 * Direction.y) + (matrix.m8 * Direction.z),
                (matrix.m1 * Direction.x) + (matrix.m5 * Direction.y) + (matrix.m9 * Direction.z),
                (matrix.m2 * Direction.x) + (matrix.m6 * Direction.y) + (matrix.m10 * Direction.z)
            };
            Largest = fmaxf(Largest, Vector3Length(Stretched));
        }
    }
    return Largest;
}


// Static functions: the cases.
static void ComposeIsDrawModelExsOrder(TestRun* run)
{
    Vector3 Position = { 1.0f, -2.0f, 3.5f };
    Vector3 Rotation = { 0.3f, -1.1f, 2.4f };
    Vector3 Scale = { 2.0f, 0.5f, -1.5f };
    Matrix Expected = MatrixMultiply(MatrixMultiply(MatrixScale(Scale.x, Scale.y, Scale.z), MatrixRotateXYZ(Rotation)),
        MatrixTranslate(Position.x, Position.y, Position.z));
    Matrix Composed = WorldTransform_Compose(Position, Rotation, Scale);
    TEST_CHECK(run, Memory_IsEqual(&Expected, &Composed, sizeof(Matrix)));

    // A point goes through the scale, then the rotation, then the translation.
    Vector3 Point = { 0.25f, 1.0f, -0.5f };
    Vector3 ByHand = Vector3Add(Vector3Transform(Vector3Multiply(Point, Scale), MatrixRotateXYZ(Rotation)), Position);
    Vector3 ByMatrix = Vector3Transform(Point, Composed);
    TEST_CHECK(run, Vector3Distance(ByHand, ByMatrix) < 1e-5f);
}

static void DecomposeRoundTripsRandomTransforms(TestRun* run)
{
    Random Generator;
    Random_Construct2(&Generator, (RandomState){ .Values = { 0x7AB5U, 0x0808U, 0x7E57U, 0x1U } });
    size_t RoundTripCount = 0U;
    size_t ExactCount = 0U;
    for (size_t Index = 0U; Index < RANDOM_TRANSFORM_COUNT; Index++)
    {
        Vector3 Position = RandomVector(&Generator, -50.0f, 50.0f);
        Vector3 Rotation = RandomVector(&Generator, -7.0f, 7.0f); // beyond +-pi too: any angle works
        Vector3 Scale = RandomScale(&Generator, true);
        Matrix Composed = WorldTransform_Compose(Position, Rotation, Scale);
        // Any hint gives the matrix back (the signs move between the scale and the turn).
        Vector3 AnySigns = { Random_NextBool(&Generator) ? 1.0f : -1.0f, Random_NextBool(&Generator) ? 1.0f : -1.0f,
            Random_NextBool(&Generator) ? 1.0f : -1.0f };
        RoundTripCount += (RoundTrips(run, Composed) && RoundTripsWith(run, Composed, AnySigns)) ? 1U : 0U;

        // With the scale's own signs as the hint, the parts come back themselves (the angles up to their turns).
        Vector3 OutPosition;
        Vector3 OutRotation;
        Vector3 OutScale;
        WorldTransform_Decompose(Composed, SignsOf(Scale), &OutPosition, &OutRotation, &OutScale);
        ExactCount += ((Vector3Distance(OutPosition, Position) == 0.0f)
            && (Vector3Distance(OutScale, Scale) < (1e-5f * Vector3Length(Scale)))
            && AreMatricesNear(MatrixRotateXYZ(OutRotation), MatrixRotateXYZ(Rotation))) ? 1U : 0U;
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
                TEST_CHECK(run, RoundTrips(run, WorldTransform_Compose((Vector3){ 1.0f, 2.0f, 3.0f }, Rotation,
                    (Vector3){ 1.0f, 2.0f, 0.5f })));
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
        Matrix Composed = WorldTransform_Compose((Vector3){ 4.0f, 5.0f, 6.0f }, Rotation, Scales[Index]);
        TEST_CHECK(run, RoundTrips(run, Composed));
    }

    // Without a hint a single mirror reads back as a negative X scale; the lengths stay the axes' lengths.
    Matrix Mirrored = WorldTransform_Compose(Vector3Zero(), Rotation, (Vector3){ 1.0f, -2.0f, 3.0f });
    Vector3 Position;
    Vector3 OutRotation;
    Vector3 Scale;
    WorldTransform_Decompose(Mirrored, NoSigns, &Position, &OutRotation, &Scale);
    TEST_CHECK(run, (fabsf(Scale.x + 1.0f) < 1e-5f) && (fabsf(Scale.y - 2.0f) < 1e-5f) && (fabsf(Scale.z - 3.0f) < 1e-5f));
    // With the hint it stays on Y, the turn unchanged; a hint that contradicts the mirror gives way on X.
    WorldTransform_Decompose(Mirrored, (Vector3){ 1.0f, -1.0f, 1.0f }, &Position, &OutRotation, &Scale);
    TEST_CHECK(run, (fabsf(Scale.x - 1.0f) < 1e-5f) && (fabsf(Scale.y + 2.0f) < 1e-5f)
        && AreMatricesNear(MatrixRotateXYZ(OutRotation), MatrixRotateXYZ(Rotation)));
    WorldTransform_Decompose(Mirrored, (Vector3){ -1.0f, -1.0f, 1.0f }, &Position, &OutRotation, &Scale);
    TEST_CHECK(run, (fabsf(Scale.x - 1.0f) < 1e-5f) && (fabsf(Scale.y + 2.0f) < 1e-5f));
    // Two mirrors are a half turn unless the hint keeps them.
    Matrix Twice = WorldTransform_Compose(Vector3Zero(), Vector3Zero(), (Vector3){ -1.0f, -1.0f, 1.0f });
    WorldTransform_Decompose(Twice, NoSigns, &Position, &OutRotation, &Scale);
    TEST_CHECK(run, (Scale.x > 0.0f) && (Scale.y > 0.0f) && (fabsf(fabsf(OutRotation.z) - PI) < 1e-5f));
    WorldTransform_Decompose(Twice, (Vector3){ -1.0f, -1.0f, 1.0f }, &Position, &OutRotation, &Scale);
    TEST_CHECK(run, (Scale.x == -1.0f) && (Scale.y == -1.0f) && (Vector3Length(OutRotation) < 1e-6f));
}

static void ShearGivesTheNearestTransform(TestRun* run)
{
    // A child turned 45 degrees about Y inside a parent stretched along X: the child's world axes are no longer at
    // right angles, which no position, rotation and scale can hold.
    Matrix Parent = WorldTransform_Compose(Vector3Zero(), Vector3Zero(), (Vector3){ 2.0f, 1.0f, 1.0f });
    Matrix Child = WorldTransform_Compose((Vector3){ 1.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, PI / 4.0f, 0.0f },
        (Vector3){ 1.0f, 1.0f, 1.0f });
    Matrix World = MatrixMultiply(Child, Parent);
    Vector3 Position;
    Vector3 Rotation;
    Vector3 Scale;
    TEST_CHECK(run, WorldTransform_Decompose(World, NoSigns, &Position, &Rotation, &Scale));
    TEST_CHECK(run, (Position.x == World.m12) && (Position.y == World.m13) && (Position.z == World.m14));
    // The scale is each axis's length; the rotation keeps the X axis's direction exactly.
    TEST_CHECK(run, fabsf(Scale.x - Vector3Length((Vector3){ World.m0, World.m1, World.m2 })) < 1e-5f);
    TEST_CHECK(run, fabsf(Scale.z - Vector3Length((Vector3){ World.m8, World.m9, World.m10 })) < 1e-5f);
    Matrix Turned = MatrixRotateXYZ(Rotation);
    Vector3 AxisX = Vector3Normalize((Vector3){ World.m0, World.m1, World.m2 });
    TEST_CHECK(run, Vector3Distance(AxisX, (Vector3){ Turned.m0, Turned.m1, Turned.m2 }) < 1e-5f);
    TEST_CHECK(run, !AreMatricesNear(WorldTransform_Compose(Position, Rotation, Scale), World));
}

static void LocalMatrixUndoesTheParent(TestRun* run)
{
    Random Generator;
    Random_Construct2(&Generator, (RandomState){ .Values = { 0x10CA1U, 0x2U, 0x3U, 0x4U } });
    size_t Agreements = 0U;
    for (size_t Index = 0U; Index < RANDOM_TRANSFORM_COUNT; Index++)
    {
        Matrix Parent = WorldTransform_Compose(RandomVector(&Generator, -20.0f, 20.0f),
            RandomVector(&Generator, -4.0f, 4.0f), RandomScale(&Generator, true));
        Matrix World = WorldTransform_Compose(RandomVector(&Generator, -20.0f, 20.0f),
            RandomVector(&Generator, -4.0f, 4.0f), RandomScale(&Generator, true));
        Matrix Local = MatrixIdentity();
        if (TEST_CHECK(run, WorldTransform_TryGetLocalMatrix(World, Parent, &Local)))
        {
            Agreements += AreMatricesNear(MatrixMultiply(Local, Parent), World) ? 1U : 0U;
        }
    }
    TEST_CHECK(run, Agreements == RANDOM_TRANSFORM_COUNT);
    Random_Deconstruct1(&Generator);
}

static void ParentsThatCannotBeInvertedAreRefused(TestRun* run)
{
    Matrix World = WorldTransform_Compose((Vector3){ 1.0f, 2.0f, 3.0f }, Vector3Zero(), (Vector3){ 1.0f, 1.0f, 1.0f });
    Matrix Local = MatrixIdentity();
    Matrix Flat = WorldTransform_Compose(Vector3Zero(), (Vector3){ 0.2f, 0.3f, 0.4f }, (Vector3){ 1.0f, 0.0f, 1.0f });
    TEST_CHECK(run, !WorldTransform_TryGetLocalMatrix(World, Flat, &Local));
    // Two axes almost along each other span almost no volume.
    Matrix Sheared = MatrixIdentity();
    Sheared.m4 = 1.0f;
    Sheared.m5 = 1e-12f;
    TEST_CHECK(run, !WorldTransform_TryGetLocalMatrix(World, Sheared, &Local));
    Matrix Identity = MatrixIdentity();
    TEST_CHECK(run, Memory_IsEqual(&Local, &Identity, sizeof(Matrix))); // untouched on a refusal
    // A thin but upright parent is fine.
    Matrix Thin = WorldTransform_Compose(Vector3Zero(), Vector3Zero(), (Vector3){ 1.0f, 1e-4f, 1.0f });
    TEST_CHECK(run, WorldTransform_TryGetLocalMatrix(World, Thin, &Local) && AreMatricesNear(MatrixMultiply(Local, Thin),
        World));
}

static void MaxScaleIsTheLongestAxisAndBoundsShear(TestRun* run)
{
    // Upright matrices: the longest axis, bit for bit as the renderer always measured it; turned ones within rounding.
    const Vector3 Scales[] = { { 1.0f, 1.0f, 1.0f }, { 40.0f, 0.2f, 40.0f }, { 2.0f, 4.0f, 2.0f }, { 0.1f, 0.8f, 0.1f } };
    for (size_t Index = 0U; Index < (sizeof(Scales) / sizeof(Scales[0])); Index++)
    {
        Matrix Upright = WorldTransform_Compose((Vector3){ 3.0f, 1.0f, 2.0f }, Vector3Zero(), Scales[Index]);
        float Longest = fmaxf(Vector3Length((Vector3){ Upright.m0, Upright.m1, Upright.m2 }),
            fmaxf(Vector3Length((Vector3){ Upright.m4, Upright.m5, Upright.m6 }),
                Vector3Length((Vector3){ Upright.m8, Upright.m9, Upright.m10 })));
        TEST_CHECK(run, WorldTransform_GetMaxScale(Upright) == Longest);
        Matrix Turned = WorldTransform_Compose(Vector3Zero(), (Vector3){ 0.5f, 1.2f, -0.3f }, Scales[Index]);
        float LongestScale = fmaxf(fabsf(Scales[Index].x), fmaxf(fabsf(Scales[Index].y), fabsf(Scales[Index].z)));
        TEST_CHECK(run, fabsf(WorldTransform_GetMaxScale(Turned) - LongestScale) < (1e-4f * LongestScale));
    }

    // Sheared: never below the true stretch.
    Random Generator;
    Random_Construct2(&Generator, (RandomState){ .Values = { 0x5EEAU, 0x9U, 0x8U, 0x7U } });
    for (size_t Index = 0U; Index < 40U; Index++)
    {
        Matrix Parent = WorldTransform_Compose(Vector3Zero(), RandomVector(&Generator, -3.0f, 3.0f),
            RandomScale(&Generator, true));
        Matrix Child = WorldTransform_Compose(Vector3Zero(), RandomVector(&Generator, -3.0f, 3.0f),
            RandomScale(&Generator, true));
        Matrix World = MatrixMultiply(Child, Parent);
        TEST_CHECK(run, WorldTransform_GetMaxScale(World) >= MeasureStretch(World));
    }
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
    };
    TestRun_RunSuite(run, "WorldTransform", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
