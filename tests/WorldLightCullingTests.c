#include "TestSuites.h"
#include "WorldLightCulling.h"


// Macros.
/** Capacity of the selection buffers; larger than WORLD_MAX_FORWARD_LIGHTS to exercise the clamp. */
#define CULLING_TEST_BUFFER_CAPACITY ((size_t)16)


// Static functions.
static WorldCullingLight MakeLight(Vector3 position, float intensity, float range)
{
    return (WorldCullingLight){ .Position = position, .Range = range, .Intensity = intensity };
}

static void SelectsReachingLightsStrongestFirst(TestRun* run)
{
    // Against a unit sphere at the origin: Near's reach penetrates deepest, Far's less, Distant's not at all.
    const WorldCullingLight Lights[] =
    {
        MakeLight((Vector3){ 0.0f, 0.0f, 5.0f }, 1.0f, 10.0f),  // 0: far
        MakeLight((Vector3){ 0.0f, 0.0f, 2.0f }, 1.0f, 10.0f),  // 1: near
        MakeLight((Vector3){ 0.0f, 0.0f, 50.0f }, 1.0f, 10.0f), // 2: distant
    };

    size_t Selected[CULLING_TEST_BUFFER_CAPACITY];
    size_t Count = WorldLightCulling_SelectForSphere(Lights, sizeof(Lights) / sizeof(Lights[0]),
        (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, Selected, CULLING_TEST_BUFFER_CAPACITY);
    if (TEST_CHECK(run, Count == 2))
    {
        TEST_CHECK(run, Selected[0] == 1);
        TEST_CHECK(run, Selected[1] == 0);
    }
}

static void BrighterLightOutranksCloserDimmerLight(TestRun* run)
{
    const WorldCullingLight Lights[] =
    {
        MakeLight((Vector3){ 0.0f, 0.0f, 2.0f }, 1.0f, 10.0f), // close, dim
        MakeLight((Vector3){ 0.0f, 0.0f, 4.0f }, 5.0f, 10.0f), // further, much brighter
    };

    size_t Selected[CULLING_TEST_BUFFER_CAPACITY];
    size_t Count = WorldLightCulling_SelectForSphere(Lights, 2, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, Selected,
        CULLING_TEST_BUFFER_CAPACITY);
    if (TEST_CHECK(run, Count == 2))
    {
        TEST_CHECK(run, Selected[0] == 1);
    }
}

static void ExcludesLightsWithoutIntensityOrRange(TestRun* run)
{
    const WorldCullingLight Lights[] =
    {
        MakeLight((Vector3){ 0.0f, 0.0f, 2.0f }, 0.0f, 10.0f), // dark
        MakeLight((Vector3){ 0.0f, 0.0f, 2.0f }, 1.0f, 0.0f),  // pointless
    };

    size_t Selected[CULLING_TEST_BUFFER_CAPACITY];
    TEST_CHECK(run, WorldLightCulling_SelectForSphere(Lights, 2, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, Selected,
        CULLING_TEST_BUFFER_CAPACITY) == 0);
}

static void RespectsAndClampsCapacity(TestRun* run)
{
    // More lights than the shader supports, all reaching the sphere; the strongest is the closest (index 0).
    WorldCullingLight Lights[WORLD_MAX_FORWARD_LIGHTS + 4];
    size_t LightCount = sizeof(Lights) / sizeof(Lights[0]);
    for (size_t Index = 0; Index < LightCount; Index++)
    {
        Lights[Index] = MakeLight((Vector3){ 0.0f, 0.0f, 2.0f + (float)Index }, 1.0f, 20.0f);
    }

    size_t Selected[CULLING_TEST_BUFFER_CAPACITY];
    Vector3 Origin = { 0.0f, 0.0f, 0.0f };
    TEST_CHECK(run, WorldLightCulling_SelectForSphere(Lights, LightCount, Origin, 1.0f, Selected,
        CULLING_TEST_BUFFER_CAPACITY) == (size_t)WORLD_MAX_FORWARD_LIGHTS);
    if (TEST_CHECK(run, WorldLightCulling_SelectForSphere(Lights, LightCount, Origin, 1.0f, Selected, 1U) == 1U))
    {
        TEST_CHECK(run, Selected[0] == 0);
    }
    TEST_CHECK(run, WorldLightCulling_SelectForSphere(Lights, LightCount, Origin, 1.0f, Selected, 0U) == 0U);
}

static void EmptyListSelectsNothing(TestRun* run)
{
    size_t Selected[CULLING_TEST_BUFFER_CAPACITY];
    TEST_CHECK(run, WorldLightCulling_SelectForSphere(NULL, 0, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, Selected,
        CULLING_TEST_BUFFER_CAPACITY) == 0);
}


// Public functions.
void WorldLightCullingTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "selects_reaching_lights_strongest_first", SelectsReachingLightsStrongestFirst },
        { "brighter_light_outranks_closer_dimmer_light", BrighterLightOutranksCloserDimmerLight },
        { "excludes_lights_without_intensity_or_range", ExcludesLightsWithoutIntensityOrRange },
        { "respects_and_clamps_capacity", RespectsAndClampsCapacity },
        { "empty_list_selects_nothing", EmptyListSelectsNothing },
    };
    TestRun_RunSuite(run, "WorldLightCulling", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
