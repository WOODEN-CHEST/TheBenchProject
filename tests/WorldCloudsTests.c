#include "TestSuites.h"
#include "WorldClouds.h"
#include "WorldAtmosphere.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** The steps of the brute-force march along a ray that the shell's stretch is checked against. */
#define MARCH_STEP_COUNT 200000
/** How far the brute force looks along a ray, metres (farther than any stretch through the shells tried). */
#define MARCH_REACH 1.2e6


// Static functions.
static WorldEnvironment MakeEnvironment(void)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    return Environment;
}

/* A value wrapped into [0, period), as the anchors are (in double). */
static double Wrapped(double value, double period)
{
    double Result = fmod(value, period);
    return (Result < 0.0) ? (Result + period) : Result;
}

/* Whether an anchor's component is a value wrapped by a period: equal to it up to the float's rounding, or a period off
 * at the wrap's ends. */
static bool IsWrappedTo(float anchor, double value, double period)
{
    double Expected = Wrapped(value, period);
    double Difference = fabs((double)anchor - Expected);
    double Tolerance = 1e-6 * period;
    return (Difference < Tolerance) || (fabs(Difference - period) < Tolerance);
}

/* Whether a point `distance` along the ray lies inside the shell (between the heights) and above the ground. */
static bool IsInShell(WorldAtmosphereRay ray, double distance, double base, double top)
{
    double Height = WorldAtmosphere_GetHeightAt(ray, distance);
    return (Height >= base) && (Height <= top);
}

/* Where along the ray the shell is entered or left between a and b (one inside, the other not), by bisection. */
static double FindEdge(WorldAtmosphereRay ray, double a, double b, double base, double top)
{
    bool IsInsideAtA = IsInShell(ray, a, base, top);
    for (int Iteration = 0; Iteration < 80; Iteration++)
    {
        double Middle = 0.5 * (a + b);
        if (IsInShell(ray, Middle, base, top) == IsInsideAtA)
        {
            a = Middle;
        }
        else
        {
            b = Middle;
        }
    }
    return 0.5 * (a + b);
}

/* The first stretch of the ray inside the shell by marching it (stopped by the ground): false when there is none. */
static bool MarchShell(double viewHeight, double cosZenith, double base, double top, double* outStart, double* outEnd)
{
    WorldAtmosphereRay ViewRay = { .Height = viewHeight, .CosZenith = cosZenith };
    double Step = MARCH_REACH / MARCH_STEP_COUNT;
    bool WasInside = IsInShell(ViewRay, 0.0, base, top);
    double Start = WasInside ? 0.0 : -1.0;
    for (int Index = 1; Index <= MARCH_STEP_COUNT; Index++)
    {
        double Distance = Index * Step;
        if (WorldAtmosphere_GetHeightAt(ViewRay, Distance) < 0.0)
        {
            if (Start < 0.0)
            {
                return false;
            }
            *outStart = Start;
            *outEnd = WorldAtmosphere_GetDistanceToGround(ViewRay);
            return true;
        }
        bool IsInside = IsInShell(ViewRay, Distance, base, top);
        if (IsInside && !WasInside)
        {
            Start = FindEdge(ViewRay, Distance - Step, Distance, base, top);
        }
        if (!IsInside && WasInside)
        {
            *outStart = Start;
            *outEnd = FindEdge(ViewRay, Distance - Step, Distance, base, top);
            return true;
        }
        WasInside = IsInside;
    }
    return false;
}


// Static functions: tests.
static void NoCoverageDrawsNothing(TestRun* run)
{
    WorldEnvironment Environment = MakeEnvironment();
    WorldCloudsDrift Drift;
    WorldCloudsDrift_Reset(&Drift);
    WorldCloudsFrame Frame;
    WorldClouds_GetFrame(&Environment, &Drift, Vector3Double_Zero(), &Frame);
    TEST_CHECK(run, !WorldClouds_HasAny(&Frame));
    TEST_CHECK(run, (Frame.Low.Extinction == 0.0f) && (Frame.Mid.Extinction == 0.0f) && (Frame.HighDepth == 0.0f));
    // Coverage without density is no cloud either; density with coverage is.
    Environment.CloudCoverage = 0.5f;
    WorldClouds_GetFrame(&Environment, &Drift, Vector3Double_Zero(), &Frame);
    TEST_CHECK(run, !WorldClouds_HasAny(&Frame));
    Environment.CloudDensity = 0.5f;
    WorldClouds_GetFrame(&Environment, &Drift, Vector3Double_Zero(), &Frame);
    TEST_CHECK(run, WorldClouds_HasAny(&Frame) && (Frame.Low.Extinction > 0.0f));
    // Each upper layer on its own.
    Environment.CloudCoverage = 0.0f;
    Environment.CloudMidCoverage = 0.25f;
    WorldClouds_GetFrame(&Environment, &Drift, Vector3Double_Zero(), &Frame);
    TEST_CHECK(run, WorldClouds_HasAny(&Frame) && (Frame.Low.Extinction == 0.0f) && (Frame.Mid.Extinction > 0.0f));
    Environment.CloudMidCoverage = 0.0f;
    Environment.CloudHighCoverage = 0.25f;
    WorldClouds_GetFrame(&Environment, &Drift, Vector3Double_Zero(), &Frame);
    TEST_CHECK(run, WorldClouds_HasAny(&Frame) && (Frame.HighDepth > 0.0f));
    Environment.CloudHighDensity = 0.0f;
    WorldClouds_GetFrame(&Environment, &Drift, Vector3Double_Zero(), &Frame);
    TEST_CHECK(run, !WorldClouds_HasAny(&Frame));
    WorldEnvironment_Deconstruct(&Environment);
}

static void TheLayersFollowTheEnvironment(TestRun* run)
{
    WorldEnvironment Environment = MakeEnvironment();
    Environment.CloudCoverage = 0.4f;
    Environment.CloudType = 0.7f;
    Environment.CloudDensity = 0.5f;
    Environment.CloudBase = 1200.0f;
    Environment.CloudThickness = 3000.0f;
    Environment.CloudMidCoverage = 0.3f;
    Environment.CloudMidType = 0.2f;
    Environment.CloudMidDensity = 0.8f;
    Environment.CloudMidBase = 6000.0f;
    Environment.CloudMidThickness = 400.0f;
    Environment.CloudHighCoverage = 0.6f;
    Environment.CloudHighType = 0.9f;
    Environment.CloudHighDensity = 0.25f;
    Environment.CloudHighHeight = 10000.0f;
    Environment.CloudDetail = 1.5f;
    Environment.CloudForwardScattering = 0.7f;
    Environment.CloudTint = (Color){ 255, 255, 255, 255 };
    Environment.CloudLightPollution = 2.0f;
    WorldCloudsDrift Drift;
    WorldCloudsDrift_Reset(&Drift);
    WorldCloudsFrame Frame;
    WorldClouds_GetFrame(&Environment, &Drift, Vector3Double_Zero(), &Frame);
    TEST_CHECK(run, (Frame.Low.Base == 1200.0f) && (Frame.Low.Top == 4200.0f) && (Frame.Low.Coverage == 0.4f)
        && (Frame.Low.Type == 0.7f) && (Frame.Low.Extinction == WORLD_CLOUDS_LOW_EXTINCTION * 0.5f));
    TEST_CHECK(run, (Frame.Mid.Base == 6000.0f) && (Frame.Mid.Top == 6400.0f) && (Frame.Mid.Coverage == 0.3f)
        && (Frame.Mid.Type == 0.2f) && (Frame.Mid.Extinction == WORLD_CLOUDS_MID_EXTINCTION * 0.8f));
    TEST_CHECK(run, (Frame.HighHeight == 10000.0f) && (Frame.HighCoverage == 0.6f) && (Frame.HighType == 0.9f)
        && (Frame.HighDepth == WORLD_CLOUDS_HIGH_DEPTH * 0.25f));
    TEST_CHECK(run, (Frame.DetailStrength == 1.5f) && (Frame.Forward == 0.7f) && (Frame.LightPollution == 2.0f));
    TEST_CHECK(run, (Frame.Tint[0] == 1.0f) && (Frame.Tint[1] == 1.0f) && (Frame.Tint[2] == 1.0f));
    // A tint is the colour's linear value, channel by channel.
    Environment.CloudTint = (Color){ 128, 64, 255, 255 };
    WorldClouds_GetFrame(&Environment, &Drift, Vector3Double_Zero(), &Frame);
    TEST_CHECK(run, (fabsf(Frame.Tint[0] - powf(128.0f / 255.0f, 2.2f)) < 1e-6f)
        && (fabsf(Frame.Tint[1] - powf(64.0f / 255.0f, 2.2f)) < 1e-6f) && (Frame.Tint[2] == 1.0f));
    // Every tile scales with the clouds' size.
    float SmallShape = Frame.Tiles[0];
    Environment.CloudScale = 2.0f;
    WorldClouds_GetFrame(&Environment, &Drift, Vector3Double_Zero(), &Frame);
    TEST_CHECK(run, (Frame.Tiles[0] == 2.0f * SmallShape) && (Frame.Tiles[0] == (float)(2.0 * WORLD_CLOUDS_SHAPE_TILE))
        && (Frame.Tiles[1] == (float)(2.0 * WORLD_CLOUDS_DETAIL_TILE))
        && (Frame.Tiles[2] == (float)(2.0 * WORLD_CLOUDS_COVERAGE_TILE))
        && (Frame.Tiles[3] == (float)(2.0 * WORLD_CLOUDS_MID_SHAPE_TILE))
        && (Frame.MapTiles[0] == (float)(2.0 * WORLD_CLOUDS_MID_MAP_TILE))
        && (Frame.MapTiles[1] == (float)(2.0 * WORLD_CLOUDS_HIGH_MAP_TILE)));
    // Values past their rules take their nearest bound: a density of 5 is 1, a thickness below the least is the least,
    // the town's light at most 10.
    Environment.CloudMidDensity = 5.0f;
    Environment.CloudThickness = -40.0f;
    Environment.CloudLightPollution = 50.0f;
    WorldClouds_GetFrame(&Environment, &Drift, Vector3Double_Zero(), &Frame);
    TEST_CHECK(run, (Frame.Mid.Extinction == WORLD_CLOUDS_MID_EXTINCTION)
        && (Frame.Low.Top - Frame.Low.Base == (float)WORLD_ENVIRONMENT_MIN_CLOUD_THICKNESS)
        && (Frame.LightPollution == 10.0f));
    // Broken values count as none, or as their least: an infinite base is at the ground, a broken thickness the least.
    Environment.CloudCoverage = NAN;
    Environment.CloudBase = INFINITY;
    Environment.CloudThickness = NAN;
    Environment.CloudHighCoverage = -1.0f;
    WorldClouds_GetFrame(&Environment, &Drift, Vector3Double_Zero(), &Frame);
    TEST_CHECK(run, (Frame.Low.Extinction == 0.0f) && (Frame.Low.Base == 0.0f)
        && (Frame.Low.Top == (float)WORLD_ENVIRONMENT_MIN_CLOUD_THICKNESS) && (Frame.HighDepth == 0.0f));
    WorldEnvironment_Deconstruct(&Environment);
}

static void TheAnchorsWrapByTheirTiles(TestRun* run)
{
    WorldEnvironment Environment = MakeEnvironment();
    Environment.CloudCoverage = 0.5f;
    Environment.CloudDensity = 0.5f;
    WorldCloudsDrift Drift;
    WorldCloudsDrift_Reset(&Drift);
    Drift.LowShape = Vector3Double_Create(123.25, -45.5, 6.75);
    Drift.LowDetail = Vector3Double_Create(-310.5, 12.25, 77.0);
    Drift.LowCoverage = Vector3Double_Create(5000.5, 0.0, -2500.25);
    Drift.Mid = Vector3Double_Create(-13000.0, 3.5, 41000.75);
    Drift.High = Vector3Double_Create(250000.5, 0.0, -99000.25);
    WorldCloudsFrame Near;
    WorldCloudsFrame Far;
    WorldClouds_GetFrame(&Environment, &Drift, Vector3Double_Create(1000.0, 30.0, -2000.0), &Near);
    // Whole tiles farther out (a camera 9 000 km away): the same anchors, and every anchor inside its tile.
    double Shift = WORLD_CLOUDS_SHAPE_TILE * 1000.0;
    WorldClouds_GetFrame(&Environment, &Drift, Vector3Double_Create(1000.0 + Shift, 30.0 + Shift, -2000.0 - Shift), &Far);
    TEST_CHECK(run, (Near.ShapeAnchor[0] == Far.ShapeAnchor[0]) && (Near.ShapeAnchor[1] == Far.ShapeAnchor[1])
        && (Near.ShapeAnchor[2] == Far.ShapeAnchor[2]));
    const float* Anchors[] = { Far.ShapeAnchor, Far.DetailAnchor, Far.MidAnchor };
    const float Tiles[] = { Far.Tiles[0], Far.Tiles[1], Far.MapTiles[0] * (float)WORLD_CLOUDS_MID_ANCHOR_TILES };
    bool AreInside = true;
    for (int Index = 0; Index < 3; Index++)
    {
        for (int Axis = 0; Axis < 3; Axis++)
        {
            AreInside = AreInside && (Anchors[Index][Axis] >= 0.0f) && (Anchors[Index][Axis] < Tiles[Index]);
        }
    }
    AreInside = AreInside && (Far.CoverageAnchor[0] >= 0.0f) && (Far.CoverageAnchor[0] < Far.Tiles[2])
        && (Far.HighAnchor[1] >= 0.0f)
        && (Far.HighAnchor[1] < Far.MapTiles[1] * (float)WORLD_CLOUDS_HIGH_ANCHOR_TILES);
    TEST_CHECK(run, AreInside);
    // The origin less the drift, wrapped: 1000 - 123.25 of the shape's tile.
    TEST_CHECK(run, Near.ShapeAnchor[0] == 876.75f);
    TEST_CHECK(run, Near.ShapeAnchor[2] == (float)(WORLD_CLOUDS_SHAPE_TILE - 2000.0 - 6.75));
    // Every pattern by its own drift and its own wrap: the erosion's and the coverage's by their tiles, the mid layer's
    // across by WORLD_CLOUDS_MID_ANCHOR_TILES of its map and up by its shape's tile, the high layer's by
    // WORLD_CLOUDS_HIGH_ANCHOR_TILES of its map.
    double MidAcross = WORLD_CLOUDS_MID_MAP_TILE * WORLD_CLOUDS_MID_ANCHOR_TILES;
    double HighAcross = WORLD_CLOUDS_HIGH_MAP_TILE * WORLD_CLOUDS_HIGH_ANCHOR_TILES;
    TEST_CHECK(run, IsWrappedTo(Near.ShapeAnchor[1], 30.0 + 45.5, WORLD_CLOUDS_SHAPE_TILE)
        && IsWrappedTo(Near.DetailAnchor[0], 1000.0 + 310.5, WORLD_CLOUDS_DETAIL_TILE)
        && IsWrappedTo(Near.DetailAnchor[1], 30.0 - 12.25, WORLD_CLOUDS_DETAIL_TILE)
        && IsWrappedTo(Near.DetailAnchor[2], -2000.0 - 77.0, WORLD_CLOUDS_DETAIL_TILE));
    TEST_CHECK(run, IsWrappedTo(Near.CoverageAnchor[0], 1000.0 - 5000.5, WORLD_CLOUDS_COVERAGE_TILE)
        && IsWrappedTo(Near.CoverageAnchor[1], -2000.0 + 2500.25, WORLD_CLOUDS_COVERAGE_TILE));
    TEST_CHECK(run, IsWrappedTo(Near.MidAnchor[0], 1000.0 + 13000.0, MidAcross)
        && IsWrappedTo(Near.MidAnchor[1], 30.0 - 3.5, WORLD_CLOUDS_MID_SHAPE_TILE)
        && IsWrappedTo(Near.MidAnchor[2], -2000.0 - 41000.75, MidAcross));
    TEST_CHECK(run, IsWrappedTo(Near.HighAnchor[0], 1000.0 - 250000.5, HighAcross)
        && IsWrappedTo(Near.HighAnchor[1], -2000.0 + 99000.25, HighAcross));
    // The mid layer's wrap across is a whole number of its shape's tiles too (its shape and map share the anchor).
    double ShapesAcross = MidAcross / WORLD_CLOUDS_MID_SHAPE_TILE;
    TEST_CHECK(run, fabs(ShapesAcross - round(ShapesAcross)) < 1e-9);
    // Up, the mid layer's anchor wraps by its shape's tile (far up too).
    TEST_CHECK(run, IsWrappedTo(Far.MidAnchor[1], 30.0 + Shift - 3.5, WORLD_CLOUDS_MID_SHAPE_TILE)
        && (Far.MidAnchor[1] < (float)WORLD_CLOUDS_MID_SHAPE_TILE));
    // A metre of camera motion moves every anchor by a metre (inside its tile), far out too.
    WorldCloudsFrame Moved;
    WorldClouds_GetFrame(&Environment, &Drift, Vector3Double_Create(1001.0 + Shift, 30.0 + Shift, -2000.0 - Shift),
        &Moved);
    TEST_CHECK(run, fabsf((Moved.ShapeAnchor[0] - Far.ShapeAnchor[0]) - 1.0f) < 1.0e-3f);
    TEST_CHECK(run, fabsf((Moved.CoverageAnchor[0] - Far.CoverageAnchor[0]) - 1.0f) < 1.0e-2f);
    WorldEnvironment_Deconstruct(&Environment);
}

static void TheDriftFollowsTheWind(TestRun* run)
{
    WorldEnvironment Environment = MakeEnvironment();
    Environment.WindDirection = 1.5707963f; // toward +X
    Environment.WindSpeed = 8.0f;
    Environment.WindGust = 2.0f;
    Environment.CloudWindScale = 2.0f;
    Environment.CloudEvolution = 0.0f;
    WorldCloudsDrift Drift;
    WorldCloudsDrift_Reset(&Drift);
    WorldCloudsDrift_Advance(&Drift, &Environment, 3.0);
    // (8 + 2) x 2 x 3 = 60 m toward +X; the mid layer 1.5 times, the high 2.5 times; without evolution the shape and the
    // erosion drift with the coverage and do not rise.
    TEST_CHECK(run, (fabs(Drift.LowCoverage.X - 60.0) < 1e-6) && (fabs(Drift.LowCoverage.Z) < 1e-4));
    TEST_CHECK(run, (fabs(Drift.Mid.X - 90.0) < 1e-6) && (fabs(Drift.High.X - 150.0) < 1e-6));
    TEST_CHECK(run, (Drift.LowShape.X == Drift.LowCoverage.X) && (Drift.LowShape.Y == 0.0)
        && (Drift.LowDetail.X == Drift.LowCoverage.X) && (Drift.LowDetail.Y == 0.0));
    // Evolution: the shape and the erosion drift faster and rise.
    Environment.CloudEvolution = 1.0f;
    WorldCloudsDrift_Reset(&Drift);
    WorldCloudsDrift_Advance(&Drift, &Environment, 3.0);
    TEST_CHECK(run, fabs(Drift.LowShape.X - 60.0 * (1.0 + WORLD_CLOUDS_SHAPE_DRIFT_SHARE)) < 1e-6);
    TEST_CHECK(run, fabs(Drift.LowDetail.X - 60.0 * (1.0 + WORLD_CLOUDS_DETAIL_DRIFT_SHARE)) < 1e-6);
    TEST_CHECK(run, (fabs(Drift.LowShape.Y - 3.0 * WORLD_CLOUDS_SHAPE_RISE) < 1e-9)
        && (fabs(Drift.LowDetail.Y - 3.0 * WORLD_CLOUDS_DETAIL_RISE) < 1e-9));
    // The patterns move with the wind: what a point's place in the pattern was is found 60 m downwind (toward +X) once
    // the drift has moved on, and the billows rise.
    {
        WorldEnvironment Covered = Environment;
        Covered.CloudCoverage = 0.5f;
        Covered.CloudDensity = 0.5f;
        WorldCloudsDrift Before;
        WorldCloudsDrift_Reset(&Before);
        WorldCloudsDrift After = Before;
        Covered.CloudEvolution = 0.0f;
        WorldCloudsDrift_Advance(&After, &Covered, 3.0);
        WorldCloudsFrame Earlier;
        WorldCloudsFrame Later;
        Vector3Double Origin = Vector3Double_Create(100.0, 2.0, 50.0);
        WorldClouds_GetFrame(&Covered, &Before, Origin, &Earlier);
        WorldClouds_GetFrame(&Covered, &After, Origin, &Later);
        // A render-space point x reads the pattern at x + anchor: the later frame's x + 60 reads the earlier frame's x.
        TEST_CHECK(run, IsWrappedTo(Later.CoverageAnchor[0], (double)Earlier.CoverageAnchor[0] - 60.0,
            WORLD_CLOUDS_COVERAGE_TILE) && IsWrappedTo(Later.ShapeAnchor[0], (double)Earlier.ShapeAnchor[0] - 60.0,
            WORLD_CLOUDS_SHAPE_TILE));
        Covered.CloudEvolution = 1.0f;
        After = Before;
        WorldCloudsDrift_Advance(&After, &Covered, 2.0);
        WorldClouds_GetFrame(&Covered, &After, Origin, &Later);
        TEST_CHECK(run, IsWrappedTo(Later.ShapeAnchor[1], (double)Earlier.ShapeAnchor[1]
            - 2.0 * WORLD_CLOUDS_SHAPE_RISE, WORLD_CLOUDS_SHAPE_TILE));
    }
    // No time, a negative or a broken one, or a broken wind, moves nothing.
    WorldCloudsDrift Before = Drift;
    WorldCloudsDrift_Advance(&Drift, &Environment, 0.0);
    WorldCloudsDrift_Advance(&Drift, &Environment, -1.0);
    WorldCloudsDrift_Advance(&Drift, &Environment, NAN);
    Environment.WindSpeed = INFINITY;
    WorldCloudsDrift_Advance(&Drift, &Environment, 1.0);
    TEST_CHECK(run, memcmp(&Before, &Drift, sizeof(Drift)) == 0);
    // Still air: only the evolution's rise.
    Environment.WindSpeed = 0.0f;
    Environment.WindGust = 0.0f;
    WorldCloudsDrift_Advance(&Drift, &Environment, 1.0);
    TEST_CHECK(run, (Drift.LowCoverage.X == Before.LowCoverage.X) && (Drift.LowShape.Y > Before.LowShape.Y));
    WorldEnvironment_Deconstruct(&Environment);
}

static void TheShellStretchMatchesAMarch(TestRun* run)
{
    const double Heights[] = { 2.5, 900.0, 2000.0, 4500.0 };
    const double Cosines[] = { 1.0, 0.7, 0.2, 0.05, 0.01, 0.002, 0.0, -0.002, -0.02, -0.2, -1.0 };
    const double Base = 1500.0;
    const double Top = 4000.0;
    bool AreAgreeing = true;
    for (size_t HeightIndex = 0U; HeightIndex < (sizeof(Heights) / sizeof(Heights[0])); HeightIndex++)
    {
        for (size_t CosineIndex = 0U; CosineIndex < (sizeof(Cosines) / sizeof(Cosines[0])); CosineIndex++)
        {
            double Height = Heights[HeightIndex];
            double Cosine = Cosines[CosineIndex];
            double Start = 0.0;
            double End = 0.0;
            double MarchStart = 0.0;
            double MarchEnd = 0.0;
            bool IsMet = WorldClouds_GetShellSegment(Height, Cosine, Base, Top, &Start, &End);
            bool IsMarched = MarchShell(Height, Cosine, Base, Top, &MarchStart, &MarchEnd);
            // The march's step: a stretch shorter than it may be missed by the march alone.
            bool Agrees = (IsMet == IsMarched) && (!IsMet || ((fabs(Start - MarchStart) < 1e-3 * fmax(1.0, Start))
                && (fabs(End - MarchEnd) < 1e-3 * fmax(1.0, End))));
            if (!Agrees)
            {
                printf("    from %.1f m at cos %.4f: %d %.3f .. %.3f against the march's %d %.3f .. %.3f\n", Height,
                    Cosine, IsMet ? 1 : 0, Start, End, IsMarched ? 1 : 0, MarchStart, MarchEnd);
            }
            AreAgreeing = AreAgreeing && Agrees;
        }
    }
    TEST_CHECK(run, AreAgreeing);
}

static void TheShadowMapStandsOnItsTexels(TestRun* run)
{
    WorldEnvironment Environment = MakeEnvironment();
    Environment.CloudCoverage = 0.5f;
    Environment.CloudDensity = 0.5f;
    Environment.CloudBase = 1500.0f;
    WorldCloudsDrift Drift;
    WorldCloudsDrift_Reset(&Drift);
    WorldCloudsFrame Clouds;
    Vector3Double Origin = Vector3Double_Create(123456.7, 2.5, -98765.4);
    WorldClouds_GetFrame(&Environment, &Drift, Origin, &Clouds);
    Vector3 Light = { 0.3f, 0.8f, 0.52f };
    float Length = sqrtf(Light.x * Light.x + Light.y * Light.y + Light.z * Light.z);
    Light = (Vector3){ Light.x / Length, Light.y / Length, Light.z / Length };
    WorldCloudShadowFrame Shadow;
    WorldClouds_GetShadowFrame(&Clouds, Origin, 2.5f, Light, 512, &Shadow);
    double Texel = WORLD_CLOUDS_SHADOW_EXTENT / 512.0;
    // The plane at the base, in render space; the centre where the camera's ray toward the light crosses it, within half
    // a texel, on the texel grid in the world.
    TEST_CHECK(run, (Shadow.Plane == 1497.5f) && (Shadow.Extent == (float)WORLD_CLOUDS_SHADOW_EXTENT));
    double CrossX = (double)Light.x * 1497.5 / (double)Light.y;
    TEST_CHECK(run, fabs((double)Shadow.Center[0] - CrossX) <= 0.5 * Texel + 1e-3);
    double WorldX = Origin.X + (double)Shadow.Center[0];
    double WorldZ = Origin.Z + (double)Shadow.Center[1];
    TEST_CHECK(run, (fabs(WorldX / Texel - round(WorldX / Texel)) < 1e-4)
        && (fabs(WorldZ / Texel - round(WorldZ / Texel)) < 1e-4));
    // The low layer's top, in render space too.
    TEST_CHECK(run, Shadow.Top == 1497.5f + Environment.CloudThickness);
    // A camera that moves keeps the map where it stands in the world while the crossing stays in its texel: moved by
    // half the way from the crossing to its texel's far edge, on both axes.
    double CrossZ = (double)Light.z * 1497.5 / (double)Light.y;
    double OffsetX = (Origin.X + CrossX) - WorldX;
    double OffsetZ = (Origin.Z + CrossZ) - WorldZ;
    double StepX = 0.5 * (0.5 * Texel - OffsetX);
    double StepZ = 0.5 * (0.5 * Texel - OffsetZ);
    TEST_CHECK(run, (StepX > 0.25 * 0.5 * Texel) || (StepZ > 0.25 * 0.5 * Texel)); // a real move, metres
    WorldCloudShadowFrame Moved;
    Vector3Double Nudged = Vector3Double_Create(Origin.X + StepX, Origin.Y, Origin.Z + StepZ);
    WorldClouds_GetShadowFrame(&Clouds, Nudged, 2.5f, Light, 512, &Moved);
    TEST_CHECK(run, (fabs((Nudged.X + (double)Moved.Center[0]) - WorldX) < 1e-2)
        && (fabs((Nudged.Z + (double)Moved.Center[1]) - WorldZ) < 1e-2));
    // A light near the horizon climbs at least the least the map takes.
    Vector3 Low = { 0.999f, 0.001f, 0.0f };
    WorldClouds_GetShadowFrame(&Clouds, Origin, 2.5f, Low, 512, &Shadow);
    TEST_CHECK(run, fabs((double)Shadow.Center[0] - 0.999 * 1497.5 / (double)WORLD_CLOUDS_SHADOW_MIN_UP)
        <= 0.5 * Texel + 1e-2);
    WorldEnvironment_Deconstruct(&Environment);
}


// Public functions.
void WorldCloudsTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "no_coverage_draws_nothing", NoCoverageDrawsNothing },
        { "the_layers_follow_the_environment", TheLayersFollowTheEnvironment },
        { "the_anchors_wrap_by_their_tiles", TheAnchorsWrapByTheirTiles },
        { "the_drift_follows_the_wind", TheDriftFollowsTheWind },
        { "the_shell_stretch_matches_a_march", TheShellStretchMatchesAMarch },
        { "the_shadow_map_stands_on_its_texels", TheShadowMapStandsOnItsTexels },
    };
    TestRun_RunSuite(run, "WorldClouds", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
