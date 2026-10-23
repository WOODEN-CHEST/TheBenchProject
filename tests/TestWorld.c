#include "TestWorld.h"
#include "GameMath.h"


// Fields.
/* The offset the running suite shifts its scenes by (TestWorld_RunSuite). */
static Vector3Double CurrentOffset = { .X = 0.0, .Y = 0.0, .Z = 0.0 };


// Public functions.
void TestWorld_RunSuite(TestRun* run, const char* suiteName, const char* farSuiteName, const TestCase* cases,
    size_t caseCount)
{
    CurrentOffset = Vector3Double_Zero();
    TestRun_RunSuite(run, suiteName, cases, caseCount);
    CurrentOffset = Vector3Double_Create(TEST_WORLD_FAR_OFFSET_X, TEST_WORLD_FAR_OFFSET_Y, TEST_WORLD_FAR_OFFSET_Z);
    TestRun_RunSuite(run, farSuiteName, cases, caseCount);
    CurrentOffset = Vector3Double_Zero();
}

Vector3Double TestWorld_GetOffset(void)
{
    return CurrentOffset;
}

Vector3Double TestWorld_SetOffset(Vector3Double offset)
{
    Vector3Double Previous = CurrentOffset;
    CurrentOffset = offset;
    return Previous;
}

Vector3Double TestWorld_At(Vector3 point)
{
    return Vector3Double_Add(GameMath_FromVector3(point), CurrentOffset);
}

Vector3 TestWorld_ToScene(Vector3Double point)
{
    return GameMath_ToVector3Relative(point, CurrentOffset);
}

Matrix TestWorld_ToSceneMatrix(const Matrix4Double* matrix)
{
    return GameMath_ToMatrixRelative(matrix, CurrentOffset);
}


bool TestWorld_Construct(TestRun* run, World* world, KeyRegistry* keys, AssetManager* assets, Logger* logger)
{
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(keys)))
    {
        return false;
    }
    if (!TEST_CHECK_SUCCESS(run, World_Construct2(world, keys, assets, logger)))
    {
        KeyRegistry_Deconstruct(keys);
        return false;
    }
    return true;
}

void TestWorld_Deconstruct(TestRun* run, World* world, KeyRegistry* keys)
{
    TEST_CHECK_SUCCESS(run, World_Deconstruct(world));
    KeyRegistry_Deconstruct(keys);
}
