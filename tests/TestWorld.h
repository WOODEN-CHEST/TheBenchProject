#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "TestFramework.h"
#include "AssetManager.h"
#include "Logger.h"
#include "NamespacedKey.h"
#include "World.h"
#include "raylib/raylib.h"
#include "wr/WRVectorMath.h"


/**
 * Helpers shared by the world suites (objects and components, object data and queries, the render cache): a world
 * needs the program's key registry, so a test builds one for each world it makes.
 *
 * THE FAR RUNS. The hierarchy, world file, collision, motion and character suites run twice (TestWorld_RunSuite):
 * as they are, then with every scene shifted by TEST_WORLD_FAR_OFFSET, 100 km along each axis (173 km from the world's
 * origin), where a float's step is 8 mm: the test that finds a forgotten float. The offset is off the float grid on
 * every axis: a whole number of float steps would shift scene values on a 1/128 m grid (0.5, 2.5, ...) without any
 * rounding, and hide the bug. A test describes its scene near the origin, in floats, and places it in the world through
 * TestWorld_At (root objects, query origins, rays, casts, teleports); what the world reports goes back through
 * TestWorld_ToScene before it is compared, with the same tolerances in both runs. Local positions (children's,
 * colliders' own) are not shifted. A test about one place (a far placement of its own, a fixture file) leaves the
 * offset out.
 */


// Macros.
/** The far runs' offset, metres: about 100 km along each axis, off the float grid (the file comment). */
#define TEST_WORLD_FAR_OFFSET_X 100000.37
#define TEST_WORLD_FAR_OFFSET_Y 100000.21
#define TEST_WORLD_FAR_OFFSET_Z (-100000.53)


// Functions.
/**
 * @brief Runs a world suite twice: under @p suiteName with no offset, then under @p farSuiteName with every scene
 *        shifted by TEST_WORLD_FAR_OFFSET (the file comment).
 */
void TestWorld_RunSuite(TestRun* run, const char* suiteName, const char* farSuiteName, const TestCase* cases,
    size_t caseCount);

/** @brief The offset the running suite shifts its scenes by: zero, or TEST_WORLD_FAR_OFFSET in its far run. */
Vector3Double TestWorld_GetOffset(void);

/**
 * @brief Shifts the scenes by @p offset from now on, for a test that compares two places itself (the same scene at the
 *        origin and far away); it puts back the offset this returns before it ends.
 */
Vector3Double TestWorld_SetOffset(Vector3Double offset);

/** @brief A point of the test's scene in the world: @p point plus the offset, in double. */
Vector3Double TestWorld_At(Vector3 point);

/** @brief A point of the world in the test's scene: @p point minus the offset, rounded to floats there. */
Vector3 TestWorld_ToScene(Vector3Double point);

/** @brief A world matrix in the test's scene: its translation minus the offset, rounded to floats there. */
Matrix TestWorld_ToSceneMatrix(const Matrix4Double* matrix);


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
