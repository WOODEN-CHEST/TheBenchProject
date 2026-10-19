#pragma once
#include <stdbool.h>
#include "TestFramework.h"
#include "AssetManager.h"
#include "Logger.h"
#include "NamespacedKey.h"
#include "World.h"


/**
 * Helpers shared by the world suites (objects and components, object data and queries, the render cache): a world
 * needs the program's key registry, so a test builds one for each world it makes.
 */


// Functions.
/**
 * @brief Constructs a key registry, then a world on it (World_Construct2). On failure nothing is left to release.
 * @param assets The asset manager, or NULL for a world without assets.
 * @param logger The world's logger, or NULL.
 * @returns true on success (a failed check is recorded otherwise). Pair with TestWorld_Deconstruct.
 */
bool TestWorld_Construct(TestRun* run, World* world, KeyRegistry* keys, AssetManager* assets, Logger* logger);

/**
 * @brief Deconstructs a world built by TestWorld_Construct, then its key registry.
 */
void TestWorld_Deconstruct(TestRun* run, World* world, KeyRegistry* keys);
