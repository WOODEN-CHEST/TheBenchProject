#include "TestWorld.h"


// Public functions.
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
