#include <math.h>
#include <stdint.h>
#include "TestSuites.h"
#include "WorldLocalShadows.h"
#include "raylib/raymath.h"


// Macros.
/** Degrees to radians, for readable angles. */
#define DEGREES(degrees) ((float)((degrees) * 3.14159265358979323846 / 180.0))


// Static functions.
/* A light of the render cache as the choice reads it: on, casting shadows, at @p position, shining along @p direction. */
static WorldRenderLight MakeLight(WorldLightType type, Vector3 position, Vector3 direction, float intensity,
    float range, uint64_t objectId)
{
    return (WorldRenderLight){ .ObjectId = objectId, .RenderPosition = position, .Type = type,
        .Direction = direction, .Cone = { .InnerAngle = DEGREES(25.0), .OuterAngle = DEGREES(35.0) }, .Range = range,
        .Intensity = intensity, .CastsShadows = true, .IsOn = true };
}

/* The camera's frustum looking along +Z from the origin, 60 degrees high. */
static WorldFrustum ForwardFrustum(void)
{
    Matrix View = MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 1.0f }, (Vector3){ 0.0f, 1.0f, 0.0f });
    Matrix Projection = MatrixPerspective(DEGREES(60.0), 16.0 / 9.0, 0.1, 1000.0);
    return WorldVisibility_ExtractFrustum(MatrixMultiply(View, Projection));
}

/* The cube shadow goes to the point light nearest the camera that is on and casts shadows; spots, lights that are off,
 * cast nothing or reach nothing never take it, and none does while the cube shadow is not wanted. */
static void CubeShadowGoesToTheNearestCastingPoint(TestRun* run)
{
    const Vector3 Down = { 0.0f, -1.0f, 0.0f };
    WorldRenderLight Lights[6] =
    {
        MakeLight(WorldLightType_Point, (Vector3){ 0.0f, 2.0f, 9.0f }, Down, 1.0f, 10.0f, 1U),
        MakeLight(WorldLightType_Spot, (Vector3){ 0.0f, 2.0f, 1.0f }, Down, 1.0f, 10.0f, 2U),
        MakeLight(WorldLightType_Point, (Vector3){ 0.0f, 2.0f, 2.0f }, Down, 1.0f, 10.0f, 3U),
        MakeLight(WorldLightType_Point, (Vector3){ 0.0f, 2.0f, 3.0f }, Down, 1.0f, 10.0f, 4U),
        MakeLight(WorldLightType_Point, (Vector3){ 0.0f, 1.0f, 1.0f }, Down, 1.0f, 0.0f, 5U),
        MakeLight(WorldLightType_Point, (Vector3){ 0.0f, 1.0f, 0.5f }, Down, 1.0f, 10.0f, 6U),
    };
    Lights[2].IsOn = false;
    Lights[5].CastsShadows = false;
    WorldLocalShadowChoice Choice = WorldLocalShadows_Choose(Lights, 6U, NULL, NULL, 0U, true);
    TEST_CHECK(run, (Choice.PointLight == 3U) && (Choice.SpotCount == 0U));
    Choice = WorldLocalShadows_Choose(Lights, 6U, NULL, NULL, 2U, false);
    TEST_CHECK(run, (Choice.PointLight == SIZE_MAX) && (Choice.SpotCount == 1U) && (Choice.Spots[0] == 1U));
}

/* The spots that cast are the strongest by intensity over their squared distance (at least 1 m), strongest first, at
 * most the limit; a spot that is off, casts nothing or whose light cannot reach the view casts nothing. */
static void SpotShadowsGoToTheStrongestSpotsInView(TestRun* run)
{
    const Vector3 Down = { 0.0f, -1.0f, 0.0f };
    WorldRenderLight Lights[7] =
    {
        MakeLight(WorldLightType_Spot, (Vector3){ 0.0f, 4.0f, 20.0f }, Down, 4.0f, 10.0f, 1U),  // 4 / 416
        MakeLight(WorldLightType_Spot, (Vector3){ 0.0f, 4.0f, 6.0f }, Down, 1.0f, 10.0f, 2U),   // 1 / 52
        MakeLight(WorldLightType_Spot, (Vector3){ 0.0f, 0.5f, 0.5f }, Down, 0.5f, 10.0f, 3U),   // 0.5 / 1
        MakeLight(WorldLightType_Spot, (Vector3){ 0.0f, 4.0f, 2.0f }, Down, 9.0f, 10.0f, 4U),   // off
        MakeLight(WorldLightType_Spot, (Vector3){ 0.0f, 4.0f, 3.0f }, Down, 9.0f, 10.0f, 5U),   // casts nothing
        MakeLight(WorldLightType_Spot, (Vector3){ 0.0f, 4.0f, -30.0f }, Down, 90.0f, 10.0f, 6U), // behind the camera
        MakeLight(WorldLightType_Spot, (Vector3){ 0.0f, 4.0f, 4.0f }, Down, 9.0f, 0.0f, 7U),    // reaches nothing
    };
    Lights[3].IsOn = false;
    Lights[4].CastsShadows = false;
    WorldFrustum Frustum = ForwardFrustum();
    WorldLocalShadowChoice Choice = WorldLocalShadows_Choose(Lights, 7U, &Frustum, NULL, 2U, true);
    TEST_CHECK(run, (Choice.SpotCount == 2U) && (Choice.Spots[0] == 2U) && (Choice.Spots[1] == 1U)
        && (Choice.PointLight == SIZE_MAX));
    Choice = WorldLocalShadows_Choose(Lights, 7U, &Frustum, NULL, 1U, true);
    TEST_CHECK(run, (Choice.SpotCount == 1U) && (Choice.Spots[0] == 2U));
    Choice = WorldLocalShadows_Choose(Lights, 7U, &Frustum, NULL, 9U, true);
    TEST_CHECK(run, (Choice.SpotCount == WORLD_LOCAL_SHADOWS_MAX_SPOTS) && (Choice.Spots[0] == 2U));
    // Without a frustum the bright lamp behind the camera (90 / 916) takes part, and outranks the nearer dim one.
    Choice = WorldLocalShadows_Choose(Lights, 7U, NULL, NULL, 2U, true);
    TEST_CHECK(run, (Choice.SpotCount == 2U) && (Choice.Spots[0] == 2U) && (Choice.Spots[1] == 5U));
    // A spot behind the camera whose cone reaches into the view still casts: its light shows.
    Lights[5].RenderPosition = (Vector3){ 0.0f, 4.0f, -1.0f };
    Lights[5].Direction = (Vector3){ 0.0f, -0.5f, 0.8660254f };
    Choice = WorldLocalShadows_Choose(Lights, 7U, &Frustum, NULL, 2U, true);
    TEST_CHECK(run, (Choice.SpotCount == 2U) && (Choice.Spots[0] == 5U));
}

/* A spot that cast last frame counts a quarter stronger: of two equal lamps the one that had the shadow keeps it until the
 * other is a quarter stronger. */
static void SpotShadowsKeepTheirLampsAcrossFrames(TestRun* run)
{
    const Vector3 Down = { 0.0f, -1.0f, 0.0f };
    WorldRenderLight Lights[2] =
    {
        MakeLight(WorldLightType_Spot, (Vector3){ 0.0f, 4.0f, 10.0f }, Down, 1.0f, 10.0f, 11U),
        MakeLight(WorldLightType_Spot, (Vector3){ 0.0f, 4.0f, 9.0f }, Down, 1.0f, 10.0f, 12U),
    };
    uint64_t Previous[WORLD_LOCAL_SHADOWS_MAX_SPOTS] = { 11U, 0U };
    WorldLocalShadowChoice Choice = WorldLocalShadows_Choose(Lights, 2U, NULL, Previous, 1U, true);
    TEST_CHECK(run, (Choice.SpotCount == 1U) && (Choice.Spots[0] == 0U)); // 116 / 97 is below 1.25
    Choice = WorldLocalShadows_Choose(Lights, 2U, NULL, NULL, 1U, true);
    TEST_CHECK(run, (Choice.SpotCount == 1U) && (Choice.Spots[0] == 1U)); // without it the nearer wins
    Lights[1].RenderPosition = (Vector3){ 0.0f, 4.0f, 7.0f };              // 116 / 65 is above 1.25
    Choice = WorldLocalShadows_Choose(Lights, 2U, NULL, Previous, 1U, true);
    TEST_CHECK(run, (Choice.SpotCount == 1U) && (Choice.Spots[0] == 1U));
}

/* The bounding sphere of a spot's reach holds its apex, its rim and its cap's tip, for narrow and wide cones, and is the
 * smallest that does: the apex (narrow) or the rim (wide) lies on it. */
static void SpotBoundsHoldTheReach(TestRun* run)
{
    const float Angles[] = { 10.0f, 30.0f, 44.0f, 46.0f, 60.0f, 85.0f };
    Vector3 Axis = Vector3Normalize((Vector3){ 0.3f, -1.0f, 0.2f });
    Vector3 Side = Vector3Normalize(Vector3CrossProduct(Axis, (Vector3){ 0.0f, 0.0f, 1.0f }));
    Vector3 Apex = { 1.0f, 5.0f, -2.0f };
    for (size_t Index = 0; Index < (sizeof(Angles) / sizeof(Angles[0])); Index++)
    {
        WorldRenderLight Spot = MakeLight(WorldLightType_Spot, Apex, Axis, 1.0f, 8.0f, 1U);
        Spot.Cone.OuterAngle = DEGREES(Angles[Index]);
        Vector3 Center;
        float Radius;
        WorldLocalShadows_GetSpotBounds(&Spot, &Center, &Radius);
        float Slack = Radius * 1e-5f;
        Vector3 Tip = Vector3Add(Apex, Vector3Scale(Axis, 8.0f));
        float Angle = Spot.Cone.OuterAngle;
        Vector3 Rim = Vector3Add(Apex, Vector3Add(Vector3Scale(Axis, 8.0f * cosf(Angle)), Vector3Scale(Side,
            8.0f * sinf(Angle))));
        TEST_CHECK(run, (Vector3Distance(Center, Apex) <= (Radius + Slack))
            && (Vector3Distance(Center, Tip) <= (Radius + Slack)) && (Vector3Distance(Center, Rim) <= (Radius + Slack)));
        float Touching = (Angles[Index] < 45.0f) ? Vector3Distance(Center, Apex) : Vector3Distance(Center, Rim);
        TEST_CHECK(run, fabsf(Touching - Radius) <= Slack);
        // Every point of the reach in between too: the cone's sides and its cap, at every turn about the axis.
        Vector3 Other = Vector3CrossProduct(Axis, Side);
        bool IsHeld = true;
        for (int Turn = 0; Turn < 16; Turn++)
        {
            float Around = (float)Turn * DEGREES(22.5);
            Vector3 Out = Vector3Add(Vector3Scale(Side, cosf(Around)), Vector3Scale(Other, sinf(Around)));
            for (int Step = 0; Step <= 8; Step++)
            {
                float Along = (float)Step / 8.0f;
                Vector3 OnSide = Vector3Add(Apex, Vector3Scale(Vector3Add(Vector3Scale(Axis, cosf(Angle)),
                    Vector3Scale(Out, sinf(Angle))), 8.0f * Along));
                Vector3 OnCap = Vector3Add(Apex, Vector3Scale(Vector3Add(Vector3Scale(Axis, cosf(Angle * Along)),
                    Vector3Scale(Out, sinf(Angle * Along))), 8.0f));
                IsHeld = IsHeld && (Vector3Distance(Center, OnSide) <= (Radius + Slack))
                    && (Vector3Distance(Center, OnCap) <= (Radius + Slack));
            }
        }
        TEST_CHECK(run, IsHeld);
    }
}

/* A spot's map sees along its axis: a point on the axis lands at the map's centre at OpenGL's perspective depth, a point
 * on the outer cone inside the map by the margin, a point behind the light nowhere; straight down works (its map's up
 * switches); a wide cone's map stops at 80 degrees. */
static void SpotMapProjectsAlongTheAxis(TestRun* run)
{
    Vector3 Lamp = { 2.0f, 6.0f, -1.0f };
    Vector3 Axis = Vector3Normalize((Vector3){ 0.0f, -1.0f, 1.0f });
    WorldSpotShadowFrame Frame = WorldLocalShadows_GetSpotFrame(Lamp, Axis, DEGREES(30.0), 12.0f,
        WORLD_LOCAL_SHADOWS_SPOT_MAP_SIZE);
    TEST_CHECK(run, (Frame.Near == WORLD_LOCAL_SHADOWS_SPOT_NEAR) && (Frame.Far == 12.0f)
        && (Frame.HalfAngle > DEGREES(30.0)) && (Frame.HalfAngle < DEGREES(30.5)));
    Vector3 Map;
    float Depth = 5.0f;
    const float Depths[3] = { 0.5f, 2.0f, Depth };
    for (size_t Index = 0; Index < 3U; Index++)
    {
        TEST_CHECK(run, WorldLocalShadows_ProjectSpot(&Frame, Vector3Add(Lamp, Vector3Scale(Axis, Depths[Index])), &Map));
        float Expected = (Frame.Far * (1.0f - (Frame.Near / Depths[Index]))) / (Frame.Far - Frame.Near);
        TEST_CHECK(run, (fabsf(Map.x - 0.5f) < 1e-5f) && (fabsf(Map.y - 0.5f) < 1e-5f)
            && (fabsf(Map.z - Expected) < 1e-5f));
    }

    // On the outer cone, sideways and up the map: inside it, a few texels from its edge.
    Vector3 Side = Vector3Normalize(Vector3CrossProduct(Axis, (Vector3){ 0.0f, 1.0f, 0.0f }));
    Vector3 Up = Vector3CrossProduct(Side, Axis);
    const Vector3 Ways[2] = { Side, Up };
    float EdgeShare = tanf(DEGREES(30.0)) / tanf(Frame.HalfAngle);
    for (size_t Way = 0; Way < 2U; Way++)
    {
        Vector3 OnCone = Vector3Add(Lamp, Vector3Scale(Vector3Add(Vector3Scale(Axis, cosf(DEGREES(30.0))),
            Vector3Scale(Ways[Way], sinf(DEGREES(30.0)))), Depth));
        TEST_CHECK(run, WorldLocalShadows_ProjectSpot(&Frame, OnCone, &Map));
        float Off = fmaxf(fabsf(Map.x - 0.5f), fabsf(Map.y - 0.5f)) * 2.0f;
        TEST_CHECK(run, (fabsf(Off - EdgeShare) < 1e-4f) && (Off < 1.0f)
            && (((1.0f - Off) * 0.5f * (float)WORLD_LOCAL_SHADOWS_SPOT_MAP_SIZE) > 3.0f));
    }
    TEST_CHECK(run, !WorldLocalShadows_ProjectSpot(&Frame, Vector3Subtract(Lamp, Axis), &Map));

    // The bias: half a texel along the axis at a depth d, through the depth's slope there (Far Near / ((Far - Near) d^2),
    // the derivative of the depth checked above), is DepthBiasScale / d.
    float Slope = (Frame.Far * Frame.Near) / ((Frame.Far - Frame.Near) * Depth * Depth);
    float Bias = Frame.DepthBiasScale / Depth;
    float HalfTexel = WORLD_LOCAL_SHADOWS_DEPTH_BIAS_TEXELS * Frame.TexelScale * Depth;
    TEST_CHECK(run, (fabsf(Bias - (Slope * HalfTexel)) < (Bias * 1e-4f)) && (Bias > 0.0f));
    TEST_CHECK(run, (fabsf(Frame.TexelScale - ((2.0f * tanf(Frame.HalfAngle)) / 1024.0f)) < 1e-9f)
        && (fabsf(Frame.NormalOffsetScale - (WORLD_LOCAL_SHADOWS_NORMAL_OFFSET_TEXELS * Frame.TexelScale)) < 1e-9f));

    // Straight down: the map's up is +Z, the point under the lamp is its centre, and points beside it lie off the
    // centre along the map's two axes (world up as the map's up would leave the view without them).
    WorldSpotShadowFrame Down = WorldLocalShadows_GetSpotFrame(Lamp, (Vector3){ 0.0f, -1.0f, 0.0f }, DEGREES(40.0),
        8.0f, WORLD_LOCAL_SHADOWS_SPOT_MAP_SIZE);
    TEST_CHECK(run, WorldLocalShadows_ProjectSpot(&Down, (Vector3){ 2.0f, 1.0f, -1.0f }, &Map)
        && (fabsf(Map.x - 0.5f) < 1e-5f) && (fabsf(Map.y - 0.5f) < 1e-5f) && isfinite(Map.z));
    Vector3 BesideX;
    Vector3 BesideZ;
    TEST_CHECK(run, WorldLocalShadows_ProjectSpot(&Down, (Vector3){ 3.0f, 1.0f, -1.0f }, &BesideX)
        && WorldLocalShadows_ProjectSpot(&Down, (Vector3){ 2.0f, 1.0f, 0.0f }, &BesideZ));
    TEST_CHECK(run, (fabsf(BesideX.x - 0.5f) > 0.05f) && (fabsf(BesideX.y - 0.5f) < 1e-5f)
        && (fabsf(BesideZ.y - 0.5f) > 0.05f) && (fabsf(BesideZ.x - 0.5f) < 1e-5f));
    // A wide cone stops at 80 degrees; a short range keeps its far plane past the near one.
    WorldSpotShadowFrame Wide = WorldLocalShadows_GetSpotFrame(Lamp, Axis, DEGREES(89.0), 0.01f, 512);
    TEST_CHECK(run, (Wide.HalfAngle == WORLD_LOCAL_SHADOWS_MAX_SPOT_HALF_ANGLE)
        && (Wide.Far == (2.0f * WORLD_LOCAL_SHADOWS_SPOT_NEAR)) && isfinite(Wide.DepthBiasScale));
}


// Public functions.
void WorldLocalShadowsTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "cube_shadow_goes_to_the_nearest_casting_point", CubeShadowGoesToTheNearestCastingPoint },
        { "spot_shadows_go_to_the_strongest_spots_in_view", SpotShadowsGoToTheStrongestSpotsInView },
        { "spot_shadows_keep_their_lamps_across_frames", SpotShadowsKeepTheirLampsAcrossFrames },
        { "spot_bounds_hold_the_reach", SpotBoundsHoldTheReach },
        { "spot_map_projects_along_the_axis", SpotMapProjectsAlongTheAxis },
    };
    TestRun_RunSuite(run, "WorldLocalShadows", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
