#include <math.h>
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

/* A spot at @p position shining along @p direction (unit) with an outer angle of @p outerDegrees. */
static WorldCullingLight MakeSpot(Vector3 position, Vector3 direction, float intensity, float range, float outerDegrees)
{
    return (WorldCullingLight){ .Position = position, .Range = range, .Intensity = intensity, .IsSpot = true,
        .Direction = direction, .CosOuter = cosf(outerDegrees * (3.14159265f / 180.0f)) };
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
        (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, Selected, CULLING_TEST_BUFFER_CAPACITY, NULL);
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
        CULLING_TEST_BUFFER_CAPACITY, NULL);
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
        CULLING_TEST_BUFFER_CAPACITY, NULL) == 0);
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
        CULLING_TEST_BUFFER_CAPACITY, NULL) == (size_t)WORLD_MAX_FORWARD_LIGHTS);
    if (TEST_CHECK(run, WorldLightCulling_SelectForSphere(Lights, LightCount, Origin, 1.0f, Selected, 1U, NULL) == 1U))
    {
        TEST_CHECK(run, Selected[0] == 0);
    }
    TEST_CHECK(run, WorldLightCulling_SelectForSphere(Lights, LightCount, Origin, 1.0f, Selected, 0U, NULL) == 0U);
}

static void EmptyListSelectsNothing(TestRun* run)
{
    size_t Selected[CULLING_TEST_BUFFER_CAPACITY];
    TEST_CHECK(run, WorldLightCulling_SelectForSphere(NULL, 0, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, Selected,
        CULLING_TEST_BUFFER_CAPACITY, NULL) == 0);
}


/* A spot pointing down from 4 m with a 35-degree cone lights a sphere on the ground under it and one at the edge of its
 * pool, not one beside the pool, nor one above or behind its head within its range (a point light there reaches all
 * of them). */
static void SpotReachesOnlyWhatLiesInItsCone(TestRun* run)
{
    Vector3 Lamp = { 0.0f, 4.0f, 0.0f };
    WorldCullingLight Spot = MakeSpot(Lamp, (Vector3){ 0.0f, -1.0f, 0.0f }, 1.0f, 10.0f, 35.0f);
    WorldCullingLight Point = MakeLight(Lamp, 1.0f, 10.0f);
    // The pool's edge on the ground: 4 tan(35) = 2.80 m from the foot of the lamp.
    const Vector3 Below = { 0.0f, 0.0f, 0.0f };
    const Vector3 AtTheEdge = { 3.0f, 0.0f, 0.0f };   // 0.5 m spheres reach 0.2 m past the edge into the pool
    const Vector3 Beside = { 4.5f, 0.0f, 0.0f };
    const Vector3 Above = { 0.0f, 7.0f, 0.0f };
    const Vector3 Behind = { 1.0f, 4.6f, 0.0f };     // just above the lamp's plane, beside it
    TEST_CHECK(run, WorldLightCulling_DoesLightReachSphere(&Spot, Below, 0.5f));
    TEST_CHECK(run, WorldLightCulling_DoesLightReachSphere(&Spot, AtTheEdge, 0.5f));
    TEST_CHECK(run, !WorldLightCulling_DoesLightReachSphere(&Spot, Beside, 0.5f));
    TEST_CHECK(run, !WorldLightCulling_DoesLightReachSphere(&Spot, Above, 0.5f));
    TEST_CHECK(run, !WorldLightCulling_DoesLightReachSphere(&Spot, Behind, 0.5f));
    // Straight behind a narrow spot near its apex the infinite cone's other side lies within the sphere's radius, but
    // nothing behind the apex's plane is lit.
    WorldCullingLight Narrow = MakeSpot(Lamp, (Vector3){ 0.0f, -1.0f, 0.0f }, 1.0f, 10.0f, 15.0f);
    TEST_CHECK(run, !WorldLightCulling_DoesLightReachSphere(&Narrow, (Vector3){ 0.0f, 6.0f, 0.0f }, 1.0f));
    const Vector3 Places[] = { Below, AtTheEdge, Beside, Above, Behind };
    for (size_t Index = 0; Index < (sizeof(Places) / sizeof(Places[0])); Index++)
    {
        TEST_CHECK(run, WorldLightCulling_DoesLightReachSphere(&Point, Places[Index], 0.5f));
    }
    // A sphere around the lamp itself, and a large one reaching from behind into the cone, are lit.
    TEST_CHECK(run, WorldLightCulling_DoesLightReachSphere(&Spot, Lamp, 0.1f));
    TEST_CHECK(run, WorldLightCulling_DoesLightReachSphere(&Spot, (Vector3){ 0.0f, 5.0f, 0.0f }, 1.5f));
    // Past the range nothing is lit, in the cone or not; a light with no range lights nothing.
    TEST_CHECK(run, !WorldLightCulling_DoesLightReachSphere(&Spot, (Vector3){ 0.0f, -7.0f, 0.0f }, 0.5f));
    WorldCullingLight Rangeless = Spot;
    Rangeless.Range = 0.0f;
    TEST_CHECK(run, !WorldLightCulling_DoesLightReachSphere(&Rangeless, Below, 0.5f));

    // The selection keeps the spot for the sphere in front of it and not for the one behind it.
    size_t Selected[CULLING_TEST_BUFFER_CAPACITY];
    TEST_CHECK(run, WorldLightCulling_SelectForSphere(&Spot, 1, Below, 0.5f, Selected, CULLING_TEST_BUFFER_CAPACITY, NULL)
        == 1U);
    TEST_CHECK(run, WorldLightCulling_SelectForSphere(&Spot, 1, Behind, 0.5f, Selected, CULLING_TEST_BUFFER_CAPACITY,
        NULL) == 0U);
}

/* The cone's test is the sphere's distance to the cone: a sphere grazing a narrow cone from the side is kept, the same
 * sphere a little farther out is not, along any axis. */
static void SpotsConeEdgeIsWhereItsAngleSays(TestRun* run)
{
    // A spot at the origin along +Z with a 20-degree cone: at 5 m along its axis its edge lies 5 tan(20) = 1.82 m out,
    // and a sphere of radius r there touches the cone while its centre is within 1.82 + r / cos(20) of the axis.
    WorldCullingLight Spot = MakeSpot((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 1.0f }, 1.0f, 20.0f, 20.0f);
    float Edge = 5.0f * tanf(20.0f * (3.14159265f / 180.0f)) + (0.3f / cosf(20.0f * (3.14159265f / 180.0f)));
    TEST_CHECK(run, WorldLightCulling_DoesLightReachSphere(&Spot, (Vector3){ Edge - 0.02f, 0.0f, 5.0f }, 0.3f));
    TEST_CHECK(run, !WorldLightCulling_DoesLightReachSphere(&Spot, (Vector3){ Edge + 0.02f, 0.0f, 5.0f }, 0.3f));
    TEST_CHECK(run, WorldLightCulling_DoesLightReachSphere(&Spot, (Vector3){ 0.0f, -(Edge - 0.02f), 5.0f }, 0.3f));
    TEST_CHECK(run, !WorldLightCulling_DoesLightReachSphere(&Spot, (Vector3){ 0.0f, -(Edge + 0.02f), 5.0f }, 0.3f));
    // The same along a slanted axis.
    float Half = sqrtf(0.5f);
    WorldCullingLight Slanted = MakeSpot((Vector3){ 1.0f, 2.0f, 3.0f }, (Vector3){ Half, 0.0f, Half }, 1.0f, 20.0f, 20.0f);
    Vector3 OnAxis = { 1.0f + (5.0f * Half), 2.0f, 3.0f + (5.0f * Half) };
    TEST_CHECK(run, WorldLightCulling_DoesLightReachSphere(&Slanted, (Vector3){ OnAxis.x, OnAxis.y + Edge - 0.02f,
        OnAxis.z }, 0.3f));
    TEST_CHECK(run, !WorldLightCulling_DoesLightReachSphere(&Slanted, (Vector3){ OnAxis.x, OnAxis.y + Edge + 0.02f,
        OnAxis.z }, 0.3f));
}

/* The reaching count counts every light that reaches the sphere, also those past the capacity, and none that does not. */
static void CountsEveryReachingLight(TestRun* run)
{
    WorldCullingLight Lights[WORLD_MAX_FORWARD_LIGHTS + 4];
    size_t LightCount = sizeof(Lights) / sizeof(Lights[0]);
    for (size_t Index = 0; Index < LightCount; Index++)
    {
        Lights[Index] = MakeLight((Vector3){ 0.0f, 0.0f, 2.0f + (float)Index }, 1.0f, 20.0f);
    }
    Lights[1].Intensity = 0.0f;                                                    // dark
    Lights[2] = MakeSpot((Vector3){ 0.0f, 0.0f, 4.0f }, (Vector3){ 0.0f, 0.0f, 1.0f }, 1.0f, 20.0f, 30.0f); // away

    size_t Selected[CULLING_TEST_BUFFER_CAPACITY];
    size_t Reaching = 99U;
    size_t Count = WorldLightCulling_SelectForSphere(Lights, LightCount, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, Selected,
        CULLING_TEST_BUFFER_CAPACITY, &Reaching);
    TEST_CHECK(run, (Count == (size_t)WORLD_MAX_FORWARD_LIGHTS) && (Reaching == (LightCount - 2U)));
    TEST_CHECK(run, (WorldLightCulling_SelectForSphere(Lights, LightCount, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, Selected,
        0U, &Reaching) == 0U) && (Reaching == (LightCount - 2U)));
    TEST_CHECK(run, (WorldLightCulling_SelectForSphere(Lights, LightCount, (Vector3){ 0.0f, 0.0f, -100.0f }, 1.0f,
        Selected, CULLING_TEST_BUFFER_CAPACITY, &Reaching) == 0U) && (Reaching == 0U));
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
        { "spot_reaches_only_what_lies_in_its_cone", SpotReachesOnlyWhatLiesInItsCone },
        { "spots_cone_edge_is_where_its_angle_says", SpotsConeEdgeIsWhereItsAngleSays },
        { "counts_every_reaching_light", CountsEveryReachingLight },
    };
    TestRun_RunSuite(run, "WorldLightCulling", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
