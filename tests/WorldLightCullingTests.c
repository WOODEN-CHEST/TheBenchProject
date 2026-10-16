#include "TestSuites.h"
#include "World.h"
#include "WorldLight.h"
#include "WorldLightCulling.h"
#include "WorldObject.h"


// Macros.
/** Capacity of the selection buffers; larger than WORLD_MAX_FORWARD_LIGHTS to exercise the clamp. */
#define CULLING_TEST_BUFFER_CAPACITY ((size_t)16)


// Static functions.
/* Creates a point light, configures it and hands it to the world. Returns the (world-owned) light, or NULL
 * with a failed check if any step failed (the light is destroyed then, since the world never took it). */
static WorldLight* AddLight(TestRun* run, World* world, const char* name, Vector3 position, float intensity,
    float size)
{
    WorldLight* Light = NULL;
    if (!TEST_CHECK_SUCCESS(run, WorldLight_Create((const unsigned char*)name, &Light)))
    {
        return NULL;
    }

    WorldObject* Object = WorldLight_AsObject(Light);
    bool IsAdded = TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, position))
        && TEST_CHECK_SUCCESS(run, WorldLight_SetIntensity(Light, intensity))
        && TEST_CHECK_SUCCESS(run, WorldLight_SetSize(Light, size))
        && TEST_CHECK_SUCCESS(run, World_AddObject(world, Object));
    if (!IsAdded)
    {
        WorldObject_Destroy(Object);
        return NULL;
    }
    return Light;
}

static void SelectsReachingLightsStrongestFirst(TestRun* run)
{
    World TestWorld;
    if (!TEST_CHECK_SUCCESS(run, World_Construct(&TestWorld)))
    {
        return;
    }

    // Against a unit sphere at the origin: Near's reach penetrates deepest, Far's less, Distant's not at all.
    WorldLight* Far = AddLight(run, &TestWorld, "far", (Vector3){ 0.0f, 0.0f, 5.0f }, 1.0f, 10.0f);
    WorldLight* Near = AddLight(run, &TestWorld, "near", (Vector3){ 0.0f, 0.0f, 2.0f }, 1.0f, 10.0f);
    AddLight(run, &TestWorld, "distant", (Vector3){ 0.0f, 0.0f, 50.0f }, 1.0f, 10.0f);

    const WorldLight* Selected[CULLING_TEST_BUFFER_CAPACITY];
    size_t Count = WorldLightCulling_SelectForSphere(&TestWorld, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, Selected,
        CULLING_TEST_BUFFER_CAPACITY);
    if (TEST_CHECK(run, Count == 2))
    {
        TEST_CHECK(run, Selected[0] == Near);
        TEST_CHECK(run, Selected[1] == Far);
    }
    TEST_CHECK_SUCCESS(run, World_Deconstruct(&TestWorld));
}

static void ExcludesLightsWithoutIntensityOrSize(TestRun* run)
{
    World TestWorld;
    if (!TEST_CHECK_SUCCESS(run, World_Construct(&TestWorld)))
    {
        return;
    }

    AddLight(run, &TestWorld, "dark", (Vector3){ 0.0f, 0.0f, 2.0f }, 0.0f, 10.0f);
    AddLight(run, &TestWorld, "pointless", (Vector3){ 0.0f, 0.0f, 2.0f }, 1.0f, 0.0f);

    const WorldLight* Selected[CULLING_TEST_BUFFER_CAPACITY];
    TEST_CHECK(run, WorldLightCulling_SelectForSphere(&TestWorld, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, Selected,
        CULLING_TEST_BUFFER_CAPACITY) == 0);
    TEST_CHECK_SUCCESS(run, World_Deconstruct(&TestWorld));
}

static void RespectsAndClampsCapacity(TestRun* run)
{
    World TestWorld;
    if (!TEST_CHECK_SUCCESS(run, World_Construct(&TestWorld)))
    {
        return;
    }

    // More lights than the shader supports, all reaching the sphere; the strongest is the closest.
    WorldLight* Strongest = NULL;
    for (size_t Index = 0; Index < (size_t)WORLD_MAX_FORWARD_LIGHTS + 4U; Index++)
    {
        WorldLight* Light = AddLight(run, &TestWorld, "light",
            (Vector3){ 0.0f, 0.0f, 2.0f + (float)Index }, 1.0f, 20.0f);
        if (Index == 0)
        {
            Strongest = Light;
        }
    }

    const WorldLight* Selected[CULLING_TEST_BUFFER_CAPACITY];
    Vector3 Origin = { 0.0f, 0.0f, 0.0f };
    TEST_CHECK(run, WorldLightCulling_SelectForSphere(&TestWorld, Origin, 1.0f, Selected, CULLING_TEST_BUFFER_CAPACITY)
        == (size_t)WORLD_MAX_FORWARD_LIGHTS);
    if (TEST_CHECK(run, WorldLightCulling_SelectForSphere(&TestWorld, Origin, 1.0f, Selected, 1U) == 1U))
    {
        TEST_CHECK(run, Selected[0] == Strongest);
    }
    TEST_CHECK(run, WorldLightCulling_SelectForSphere(&TestWorld, Origin, 1.0f, Selected, 0U) == 0U);
    TEST_CHECK_SUCCESS(run, World_Deconstruct(&TestWorld));
}

static void NullWorldSelectsNothing(TestRun* run)
{
    const WorldLight* Selected[CULLING_TEST_BUFFER_CAPACITY];
    TEST_CHECK(run, WorldLightCulling_SelectForSphere(NULL, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, Selected,
        CULLING_TEST_BUFFER_CAPACITY) == 0);
}


// Public functions.
void WorldLightCullingTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "selects_reaching_lights_strongest_first", SelectsReachingLightsStrongestFirst },
        { "excludes_lights_without_intensity_or_size", ExcludesLightsWithoutIntensityOrSize },
        { "respects_and_clamps_capacity", RespectsAndClampsCapacity },
        { "null_world_selects_nothing", NullWorldSelectsNothing },
    };
    TestRun_RunSuite(run, "WorldLightCulling", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
