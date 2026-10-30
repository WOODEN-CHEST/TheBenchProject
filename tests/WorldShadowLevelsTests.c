#include "TestSuites.h"
#include "GameMath.h"
#include "WorldShadowLevels.h"
#include "raylib/raymath.h"
#include "wr/WRVectorMath.h"
#include <math.h>


/**
 * The sun's and the moon's shadow levels (source/WorldShadowLevels.h): their sizes and texels, the levels the reach
 * needs, which level a point reads with the bands between them and the reach's fade, the size cut, the LODs far levels
 * pick, the update schedule, and the light frames kept levels are read with, near the origin and 10 000 km out.
 */


// Macros.
/** How close two floats computed two ways must be, as a share of the larger. */
#define RELATIVE_TOLERANCE 1e-6f
/** How close a kept level must put a world point in its light clip coordinates, frame after frame (float rounding of
 *  coordinates of order 1). */
#define CLIP_TOLERANCE 2e-6
/** How many frames the schedule tests run. */
#define SCHEDULE_FRAME_COUNT 64U
/** Ten thousand kilometres, in metres. */
#define VERY_FAR_DISTANCE 1e7


// Static functions.
/* Whether two floats agree within RELATIVE_TOLERANCE of the larger. */
static bool AreClose(float a, float b)
{
    float Scale = fmaxf(fabsf(a), fabsf(b));
    return fabsf(a - b) <= (RELATIVE_TOLERANCE * fmaxf(Scale, 1.0f));
}

/* A unit direction toward a light at an elevation and an azimuth, in radians. */
static Vector3 LightAt(double elevation, double azimuth)
{
    return (Vector3){ (float)(cos(elevation) * sin(azimuth)), (float)sin(elevation),
        (float)(cos(elevation) * cos(azimuth)) };
}

/* A world point's light clip coordinates in a level's frame: the point relative to the frame's origin (in double, rounded
 * once, as the render cache does), through the level's view-projection. */
static Vector3 ToLevelClip(const WorldShadowLevelFrame* level, Vector3Double point, Vector3Double origin)
{
    Vector3 Relative = GameMath_ToVector3Relative(point, origin);
    return Vector3Transform(Relative, level->Cascade.ViewProjection);
}

/* The settings every test starts from: the defaults. */
static WorldShadowLevelsSettings DefaultSettings(void)
{
    return WorldShadowLevelsSettings_CreateDefault();
}

static void TheLevelsGrowByTheFactor(TestRun* run)
{
    WorldShadowLevelsSettings Settings = DefaultSettings();
    const float Sizes[] = { 32.0f, 128.0f, 512.0f, 2048.0f, 8192.0f };
    for (uint32_t Level = 0U; Level < 5U; Level++)
    {
        TEST_CHECK(run, WorldShadowLevels_GetSize(&Settings, Level) == Sizes[Level]);
        TEST_CHECK(run, WorldShadowLevels_GetTexel(&Settings, Level) == (Sizes[Level] / 2048.0f));
    }
    Settings.Factor = 2.0f;
    TEST_CHECK(run, WorldShadowLevels_GetSize(&Settings, 3U) == 256.0f);
    // Every level's lookups stop the same share of its half size short of its edge: four of its texels.
    TEST_CHECK(run, AreClose(WorldShadowLevels_GetEdge(&Settings), 1.0f - (8.0f / 2048.0f)));
}

static void TheReachDecidesTheLevelsUsed(TestRun* run)
{
    WorldShadowLevelsSettings Settings = DefaultSettings();
    TEST_CHECK(run, WorldShadowLevels_GetUsedCount(&Settings) == 5U); // 4 km: the 8 km level holds it
    Settings.Reach = 100.0f;
    TEST_CHECK(run, WorldShadowLevels_GetUsedCount(&Settings) == 3U); // the 512 m level holds 100 m
    Settings.Reach = 15.0f;
    TEST_CHECK(run, WorldShadowLevels_GetUsedCount(&Settings) == 1U);
    Settings.Reach = 0.0f;
    TEST_CHECK(run, WorldShadowLevels_GetUsedCount(&Settings) == 0U);
    Settings.Reach = 1e6f;
    TEST_CHECK(run, WorldShadowLevels_GetUsedCount(&Settings) == 5U); // the player's level count caps it
    Settings.LevelCount = 2U;
    TEST_CHECK(run, WorldShadowLevels_GetUsedCount(&Settings) == 2U);

    // The depth runs the reach both ways along the light (a little more), and never less than the minimum.
    Settings = DefaultSettings();
    TEST_CHECK(run, AreClose(WorldShadowLevels_GetDepthHalfRange(&Settings), 4000.0f * WORLD_SHADOW_DEPTH_REACH_SCALE));
    WorldRenderCascadeShape Shape = WorldShadowLevels_GetShape(&Settings, 2U);
    TEST_CHECK(run, (Shape.Size == 512.0f) && (Shape.MapSize == 2048));
    // The eye near the centre, the box the half range both ways from it.
    TEST_CHECK(run, Shape.LightDistance == WORLD_SHADOW_EYE_DISTANCE);
    TEST_CHECK(run, AreClose(Shape.LightDistance - Shape.NearPlane, WorldShadowLevels_GetDepthHalfRange(&Settings)));
    TEST_CHECK(run, AreClose(Shape.FarPlane - Shape.LightDistance, WorldShadowLevels_GetDepthHalfRange(&Settings)));
    Settings.Reach = 1.0f;
    TEST_CHECK(run, WorldShadowLevels_GetDepthHalfRange(&Settings) == WORLD_SHADOW_MIN_DEPTH_HALF_RANGE);
}

static void APointReadsTheSmallestLevelThatHoldsIt(TestRun* run)
{
    WorldShadowLevelsSettings Settings = DefaultSettings();
    uint32_t Used = WorldShadowLevels_GetUsedCount(&Settings);
    float Edge = WorldShadowLevels_GetEdge(&Settings);

    WorldShadowLookup Lookup = WorldShadowLevels_GetLookup(&Settings, Used, 0.0f, 0.0f);
    TEST_CHECK(run, Lookup.IsInside && (Lookup.Level == 0U) && !Lookup.HasNext && (Lookup.NextWeight == 0.0f));
    TEST_CHECK(run, Lookup.ReachFade == 1.0f);

    for (uint32_t Level = 0U; Level < Used; Level++)
    {
        float Usable = WorldShadowLevels_GetSize(&Settings, Level) * 0.5f * Edge;
        float BandStart = Usable * (1.0f - WORLD_SHADOW_BLEND_SHARE);
        // Before the band: the level alone.
        Lookup = WorldShadowLevels_GetLookup(&Settings, Used, BandStart * 0.99f, 1.0f);
        TEST_CHECK(run, Lookup.IsInside && (Lookup.Level == Level) && (Lookup.NextWeight == 0.0f));
        // Across the band the next level's share rises to 1 at the level's usable edge.
        Lookup = WorldShadowLevels_GetLookup(&Settings, Used, (BandStart + Usable) * 0.5f, 1.0f);
        TEST_CHECK(run, Lookup.IsInside && (Lookup.Level == Level) && AreClose(Lookup.NextWeight, 0.5f));
        TEST_CHECK(run, Lookup.HasNext == ((Level + 1U) < Used));
        Lookup = WorldShadowLevels_GetLookup(&Settings, Used, Usable, 1.0f);
        TEST_CHECK(run, Lookup.IsInside && (Lookup.Level == Level) && (Lookup.NextWeight == 1.0f));
        // Just past the edge: the next level alone (its share was 1 just before: no step), or nothing past the last.
        Lookup = WorldShadowLevels_GetLookup(&Settings, Used, Usable * 1.0001f, 1.0f);
        if ((Level + 1U) < Used)
        {
            TEST_CHECK(run, Lookup.IsInside && (Lookup.Level == (Level + 1U)) && (Lookup.NextWeight == 0.0f));
        }
        else
        {
            TEST_CHECK(run, !Lookup.IsInside);
        }
    }
    // Nothing without levels, and nothing for a NaN.
    TEST_CHECK(run, !WorldShadowLevels_GetLookup(&Settings, 0U, 1.0f, 1.0f).IsInside);
    TEST_CHECK(run, !WorldShadowLevels_GetLookup(&Settings, Used, NAN, 1.0f).IsInside);
}

static void TheReachFadesTheShadowOut(TestRun* run)
{
    WorldShadowLevelsSettings Settings = DefaultSettings();
    Settings.Reach = 100.0f; // the band: its last 10 m
    uint32_t Used = WorldShadowLevels_GetUsedCount(&Settings);
    TEST_CHECK(run, WorldShadowLevels_GetLookup(&Settings, Used, 50.0f, 89.0f).ReachFade == 1.0f);
    TEST_CHECK(run, AreClose(WorldShadowLevels_GetLookup(&Settings, Used, 50.0f, 95.0f).ReachFade, 0.5f));
    TEST_CHECK(run, !WorldShadowLevels_GetLookup(&Settings, Used, 50.0f, 100.0f).IsInside);
    TEST_CHECK(run, !WorldShadowLevels_GetLookup(&Settings, Used, 50.0f, 1000.0f).IsInside);
    // A short reach still fades over at least the minimum band (at most all of it).
    Settings.Reach = 10.0f;
    TEST_CHECK(run, AreClose(WorldShadowLevels_GetLookup(&Settings, 1U, 1.0f, 8.5f).ReachFade, 0.5f));
    Settings.Reach = 2.0f;
    TEST_CHECK(run, AreClose(WorldShadowLevels_GetLookup(&Settings, 1U, 0.5f, 1.0f).ReachFade, 0.5f));
}

static void SmallCastersStayOutOfCoarseLevels(TestRun* run)
{
    WorldShadowLevelsSettings Settings = DefaultSettings();
    // A cup (5 cm) casts into the first level only; a small tree (3 m) up to the 1 m level; a big tree (5 m) and a house
    // into every one.
    TEST_CHECK(run, WorldShadowLevels_IsLargeEnough(&Settings, 0U, 0.05f));
    TEST_CHECK(run, !WorldShadowLevels_IsLargeEnough(&Settings, 1U, 0.05f));
    TEST_CHECK(run, WorldShadowLevels_IsLargeEnough(&Settings, 3U, 3.0f));
    TEST_CHECK(run, !WorldShadowLevels_IsLargeEnough(&Settings, 4U, 3.0f));
    TEST_CHECK(run, WorldShadowLevels_IsLargeEnough(&Settings, 4U, 5.0f));
    // Exactly the cut casts; a NaN never does.
    float Texel = WorldShadowLevels_GetTexel(&Settings, 2U);
    TEST_CHECK(run, WorldShadowLevels_IsLargeEnough(&Settings, 2U, Texel * WORLD_SHADOW_SIZE_CUT_TEXELS * 0.5f));
    TEST_CHECK(run, !WorldShadowLevels_IsLargeEnough(&Settings, 2U, Texel * WORLD_SHADOW_SIZE_CUT_TEXELS * 0.49f));
    TEST_CHECK(run, !WorldShadowLevels_IsLargeEnough(&Settings, 0U, NAN));
}

static void FarLevelsPickTheLodOfTheirInnerEdge(TestRun* run)
{
    WorldShadowLevelsSettings Settings = DefaultSettings();
    float Edge = WorldShadowLevels_GetEdge(&Settings);
    TEST_CHECK(run, WorldShadowLevels_GetLodDistance(&Settings, 0U) == 0.0f);
    for (uint32_t Level = 1U; Level < 5U; Level++)
    {
        // Where the level is first read: the band of the level before it starts.
        float Expected = WorldShadowLevels_GetSize(&Settings, Level - 1U) * 0.5f * Edge * (1.0f - WORLD_SHADOW_BLEND_SHARE);
        float Distance = WorldShadowLevels_GetLodDistance(&Settings, Level);
        TEST_CHECK(run, AreClose(Distance, Expected));
        WorldShadowLookup Before = WorldShadowLevels_GetLookup(&Settings, 5U, Distance * 0.999f, Distance);
        WorldShadowLookup After = WorldShadowLevels_GetLookup(&Settings, 5U, Distance * 1.001f, Distance);
        TEST_CHECK(run, (Before.Level == (Level - 1U)) && (Before.NextWeight == 0.0f));
        TEST_CHECK(run, (After.Level == (Level - 1U)) && After.HasNext && (After.NextWeight > 0.0f));
    }
}

static void ALevelLeavesOutWhatTheLevelBeforeHolds(TestRun* run)
{
    WorldShadowLevelsSettings Settings = DefaultSettings();
    TEST_CHECK(run, WorldShadowLevels_GetInnerInset(&Settings, 0U) == 0.0f);
    // The second level by hand: to the band's start 16 x (1 - (1 - 8 / 2048) x 0.85) = 2.4531, the moves
    // 0.02 x (64 + 2 x 16) = 1.92, the snaps 2 x 1/64 = 0.03125, the reads 4 x 1/16 = 0.25, the turns: the limit
    // (0.5 / 2048) x sqrt 2 times 3 x (4200 + 16) + 3 sqrt 2 x (1 + 1 / sqrt(1 - 0.99^2)) x 16 + 2 x (2 x 4200 + 16 + 1.92)
    // = 10.3694.
    TEST_CHECK(run, fabsf(WorldShadowLevels_GetInnerInset(&Settings, 1U) - 15.023753f) < 1e-4f);
    float Edge = WorldShadowLevels_GetEdge(&Settings);
    for (uint32_t Level = 1U; Level < 5U; Level++)
    {
        // More than the band of the level before (both are read there), less than its half size (its ring holds
        // something), growing outward.
        float Below = WorldShadowLevels_GetSize(&Settings, Level - 1U) * 0.5f;
        float Inset = WorldShadowLevels_GetInnerInset(&Settings, Level);
        TEST_CHECK(run, Inset > (Below * (1.0f - (Edge * (1.0f - WORLD_SHADOW_BLEND_SHARE)))));
        TEST_CHECK(run, Inset < Below);
        TEST_CHECK(run, Inset > WorldShadowLevels_GetInnerInset(&Settings, Level - 1U));
    }
    // A longer reach turns the levels' light over a longer depth: 7 x (21 000 - 4200) x (0.5 / 2048) x sqrt 2 more.
    WorldShadowLevelsSettings Far = Settings;
    Far.Reach = 20000.0f;
    TEST_CHECK(run, fabsf(WorldShadowLevels_GetInnerInset(&Far, 1U) - WorldShadowLevels_GetInnerInset(&Settings, 1U)
        - 40.603397f) < 1e-3f);
}

static void PeriodsThatArePowersOfTwoNeverMeet(TestRun* run)
{
    TEST_CHECK(run, WorldShadowLevels_GetPhase(0U) == 0U);
    TEST_CHECK(run, WorldShadowLevels_GetPhase(1U) == 0U);
    TEST_CHECK(run, WorldShadowLevels_GetPhase(2U) == 0U);
    TEST_CHECK(run, WorldShadowLevels_GetPhase(4U) == 1U);
    TEST_CHECK(run, WorldShadowLevels_GetPhase(8U) == 3U);
    TEST_CHECK(run, WorldShadowLevels_GetPhase(16U) == 7U);
    TEST_CHECK(run, WorldShadowLevels_GetPhase(3U) < 3U);
    const uint32_t Periods[] = { 2U, 4U, 8U, 16U, 32U, 64U };
    for (uint32_t Frame = 0U; Frame < 256U; Frame++)
    {
        uint32_t Count = 0U;
        for (size_t Index = 0U; Index < (sizeof(Periods) / sizeof(Periods[0])); Index++)
        {
            Count += ((Frame % Periods[Index]) == WorldShadowLevels_GetPhase(Periods[Index])) ? 1U : 0U;
        }
        TEST_CHECK(run, Count <= 1U);
    }
}

static void TheFarLevelsRenderOnTheirSchedule(TestRun* run)
{
    WorldShadowLevelsSettings Settings = DefaultSettings();
    WorldShadowLevels Levels;
    WorldShadowLevels_Construct(&Levels);
    WorldShadowLevelFrame Frames[WORLD_SHADOW_MAX_LEVELS];
    Vector3 Light = LightAt(0.8, 0.3);
    Vector3Double Origin = Vector3Double_Create(12.0, 3.0, -40.0);

    // The first frame renders every level used, and none past them.
    TEST_CHECK(run, WorldShadowLevels_BeginFrame(&Levels, &Settings, Origin, Light, Frames) == 5U);
    for (uint32_t Level = 0U; Level < WORLD_SHADOW_MAX_LEVELS; Level++)
    {
        TEST_CHECK(run, Frames[Level].IsUsed == (Level < 5U));
        TEST_CHECK(run, Frames[Level].IsRendered == (Level < 5U));
    }
    // Then, with the camera and the light holding still, each renders on its own frames.
    uint32_t RenderCounts[WORLD_SHADOW_MAX_LEVELS] = { 0U };
    for (uint32_t Frame = 1U; Frame <= SCHEDULE_FRAME_COUNT; Frame++)
    {
        WorldShadowLevels_BeginFrame(&Levels, &Settings, Origin, Light, Frames);
        for (uint32_t Level = 0U; Level < 5U; Level++)
        {
            uint32_t Period = Settings.UpdatePeriods[Level];
            TEST_CHECK(run, Frames[Level].IsRendered == ((Frame % Period) == WorldShadowLevels_GetPhase(Period)));
            RenderCounts[Level] += Frames[Level].IsRendered ? 1U : 0U;
        }
    }
    TEST_CHECK(run, (RenderCounts[0] == 64U) && (RenderCounts[1] == 64U) && (RenderCounts[2] == 32U));
    TEST_CHECK(run, (RenderCounts[3] == 16U) && (RenderCounts[4] == 8U));
    WorldShadowLevels_Deconstruct(&Levels);
}

static void ATurnOrAMoveRendersAKeptLevelAtOnce(TestRun* run)
{
    WorldShadowLevelsSettings Settings = DefaultSettings();
    WorldShadowLevels Levels;
    WorldShadowLevels_Construct(&Levels);
    WorldShadowLevelFrame Frames[WORLD_SHADOW_MAX_LEVELS];
    Vector3Double Origin = Vector3Double_Create(0.0, 2.0, 0.0);
    double Elevation = 0.7;
    WorldShadowLevels_BeginFrame(&Levels, &Settings, Origin, LightAt(Elevation, 0.0), Frames);

    // The turn that may wait: a quarter of the limit (half a texel at a box's corner) on a frame where only levels 0, 1
    // and 3 render: level 2 and level 4 keep their maps.
    double Limit = (double)WORLD_SHADOW_TURN_TEXELS * 1.41421356237309504880 / 2048.0;
    WorldShadowLevels_BeginFrame(&Levels, &Settings, Origin, LightAt(Elevation + (Limit * 0.25), 0.0), Frames);
    TEST_CHECK(run, !Frames[2].IsRendered && Frames[3].IsRendered && !Frames[4].IsRendered);
    // Kept, they hold the light they were rendered from.
    TEST_CHECK(run, Vector3Equals(Frames[4].TowardLight, LightAt(Elevation, 0.0)));
    // Past the limit since their render, every level renders.
    WorldShadowLevels_BeginFrame(&Levels, &Settings, Origin, LightAt(Elevation + (Limit * 1.5), 0.0), Frames);
    for (uint32_t Level = 0U; Level < 5U; Level++)
    {
        TEST_CHECK(run, Frames[Level].IsRendered);
    }

    // A move of more than its share of a level's half size renders it; a smaller one keeps it.
    Vector3 Light = LightAt(Elevation + (Limit * 1.5), 0.0);
    WorldShadowLevels_BeginFrame(&Levels, &Settings, Origin, Light, Frames); // frame 3: levels 0, 1 and 4
    double Move = (double)WORLD_SHADOW_MOVE_SHARE * 0.5 * 2048.0 * 1.5; // past level 3's share, short of level 4's
    Vector3Double Moved = Vector3Double_Add(Origin, Vector3Double_Create(0.0, 0.0, Move));
    WorldShadowLevels_BeginFrame(&Levels, &Settings, Moved, Light, Frames); // frame 4: by their periods levels 0 to 2
    TEST_CHECK(run, Frames[3].IsRendered && !Frames[4].IsRendered);

    // A turn far below the limit that takes the light across the up vector's switch (near straight up) renders every
    // level: every box turns about the light at once there. The next small turn keeps them again.
    double Switch = asin((double)WORLD_RENDER_SPACE_CASCADE_STRAIGHT_UP_Y);
    WorldShadowLevels_BeginFrame(&Levels, &Settings, Moved, LightAt(Switch - (Limit * 0.1), 0.7), Frames); // frame 5
    WorldShadowLevels_BeginFrame(&Levels, &Settings, Moved, LightAt(Switch + (Limit * 0.1), 0.7), Frames); // frame 6
    TEST_CHECK(run, Frames[3].IsRendered && Frames[4].IsRendered);
    WorldShadowLevels_BeginFrame(&Levels, &Settings, Moved, LightAt(Switch + (Limit * 0.15), 0.7), Frames); // frame 7
    TEST_CHECK(run, !Frames[2].IsRendered && !Frames[4].IsRendered);
    WorldShadowLevels_Deconstruct(&Levels);
}

static void NewSettingsRenderEveryLevelAgain(TestRun* run)
{
    WorldShadowLevelsSettings Settings = DefaultSettings();
    WorldShadowLevels Levels;
    WorldShadowLevels_Construct(&Levels);
    WorldShadowLevelFrame Frames[WORLD_SHADOW_MAX_LEVELS];
    Vector3Double Origin = Vector3Double_Create(0.0, 2.0, 0.0);
    Vector3 Light = LightAt(0.9, 1.0);
    WorldShadowLevels_BeginFrame(&Levels, &Settings, Origin, Light, Frames);
    WorldShadowLevels_BeginFrame(&Levels, &Settings, Origin, Light, Frames);
    TEST_CHECK(run, !Frames[4].IsRendered);

    // Another map size, another factor, another reach: every map is rendered again.
    Settings.MapSize = 1024;
    WorldShadowLevels_BeginFrame(&Levels, &Settings, Origin, Light, Frames);
    TEST_CHECK(run, Frames[2].IsRendered && Frames[3].IsRendered && Frames[4].IsRendered);
    Settings.Reach = 3000.0f;
    WorldShadowLevels_BeginFrame(&Levels, &Settings, Origin, Light, Frames);
    TEST_CHECK(run, Frames[2].IsRendered && Frames[3].IsRendered && Frames[4].IsRendered);
    // Another period keeps the maps.
    Settings.UpdatePeriods[4] = 16U;
    WorldShadowLevels_BeginFrame(&Levels, &Settings, Origin, Light, Frames);
    TEST_CHECK(run, !Frames[4].IsRendered);

    // A level left unused (fewer levels, the maps' sizes and the reach kept) forgets its map: it renders as soon as it is
    // used again, though neither its period nor a turn or a move asks for it (frame 6).
    Settings.LevelCount = 3U;
    TEST_CHECK(run, WorldShadowLevels_BeginFrame(&Levels, &Settings, Origin, Light, Frames) == 3U);
    TEST_CHECK(run, !Frames[3].IsUsed && !Frames[4].IsUsed);
    Settings.LevelCount = 5U;
    WorldShadowLevels_BeginFrame(&Levels, &Settings, Origin, Light, Frames);
    TEST_CHECK(run, Frames[3].IsRendered && Frames[4].IsRendered);
    // Invalidated (its map texture made again), every level renders.
    WorldShadowLevels_BeginFrame(&Levels, &Settings, Origin, Light, Frames);
    WorldShadowLevels_Invalidate(&Levels);
    WorldShadowLevels_BeginFrame(&Levels, &Settings, Origin, Light, Frames);
    for (uint32_t Level = 0U; Level < 5U; Level++)
    {
        TEST_CHECK(run, Frames[Level].IsRendered);
    }
    WorldShadowLevels_Deconstruct(&Levels);
}

/* Runs a kept level's frames around an origin: a point of the world keeps the light clip coordinates the level was
 * rendered with while the camera moves (less than the move trigger) and the level keeps its map. */
static void CheckKeptLevelsHoldTheirPoints(TestRun* run, Vector3Double start)
{
    WorldShadowLevelsSettings Settings = DefaultSettings();
    WorldShadowLevels Levels;
    WorldShadowLevels_Construct(&Levels);
    WorldShadowLevelFrame Rendered[WORLD_SHADOW_MAX_LEVELS];
    WorldShadowLevelFrame Kept[WORLD_SHADOW_MAX_LEVELS];
    Vector3 Light = LightAt(0.35, 2.1);
    WorldShadowLevels_BeginFrame(&Levels, &Settings, start, Light, Rendered);

    const Vector3Double Offsets[] =
    {
        { 3.0, -1.5, 7.0 }, { -900.0, 40.0, 1300.0 }, { 2500.0, -12.0, -2700.0 }, { 0.25, 0.0, -0.5 }
    };
    // The camera walks a little each frame (frames 1 to 3: level 4 renders at frame 3, so check 1 and 2), and the light
    // turns a little (a third of the limit: the kept level reads its map with the light it was rendered from).
    double Limit = (double)WORLD_SHADOW_TURN_TEXELS * 1.41421356237309504880 / 2048.0;
    for (uint32_t Frame = 1U; Frame <= 2U; Frame++)
    {
        Vector3Double Origin = Vector3Double_Add(start, Vector3Double_Create(0.37 * Frame, 0.01 * Frame, -0.23 * Frame));
        Vector3 Turned = LightAt(0.35 + ((Limit / 6.0) * Frame), 2.1);
        WorldShadowLevels_BeginFrame(&Levels, &Settings, Origin, Turned, Kept);
        TEST_CHECK(run, !Kept[4].IsRendered);
        for (size_t Index = 0U; Index < (sizeof(Offsets) / sizeof(Offsets[0])); Index++)
        {
            Vector3Double Point = Vector3Double_Add(start, Offsets[Index]);
            Vector3 Before = ToLevelClip(&Rendered[4], Point, start);
            Vector3 After = ToLevelClip(&Kept[4], Point, Origin);
            TEST_CHECK(run, fabs((double)Before.x - (double)After.x) <= CLIP_TOLERANCE);
            TEST_CHECK(run, fabs((double)Before.y - (double)After.y) <= CLIP_TOLERANCE);
            TEST_CHECK(run, fabs((double)Before.z - (double)After.z) <= CLIP_TOLERANCE);
        }
    }
    WorldShadowLevels_Deconstruct(&Levels);
}

static void AKeptLevelIsReadAsItWasRendered(TestRun* run)
{
    CheckKeptLevelsHoldTheirPoints(run, Vector3Double_Create(1.0, 2.0, 3.0));
    CheckKeptLevelsHoldTheirPoints(run, Vector3Double_Create(VERY_FAR_DISTANCE, 150.0, -VERY_FAR_DISTANCE * 0.5));
}

static void EverythingWithinTheReachLiesInEveryLevelsDepth(TestRun* run)
{
    WorldShadowLevelsSettings Settings = DefaultSettings();
    WorldShadowLevels Levels;
    WorldShadowLevels_Construct(&Levels);
    WorldShadowLevelFrame Frames[WORLD_SHADOW_MAX_LEVELS];
    Vector3Double Origin = Vector3Double_Create(-50.0, 10.0, 75.0);
    // A low sun, so the light's depth axis lies almost along the ground.
    Vector3 Light = LightAt(0.05, 0.6);
    WorldShadowLevels_BeginFrame(&Levels, &Settings, Origin, Light, Frames);
    double Reach = (double)Settings.Reach;
    for (int Step = 0; Step < 64; Step++)
    {
        double Azimuth = (double)Step * 0.4;
        double Elevation = ((double)(Step % 9) - 4.0) * 0.35;
        Vector3Double Point = Vector3Double_Add(Origin, Vector3Double_Create(Reach * cos(Elevation) * sin(Azimuth),
            Reach * sin(Elevation), Reach * cos(Elevation) * cos(Azimuth)));
        for (uint32_t Level = 0U; Level < 5U; Level++)
        {
            float Depth = ToLevelClip(&Frames[Level], Point, Origin).z;
            TEST_CHECK(run, (Depth > -1.0f) && (Depth < 1.0f)); // OpenGL clip depth
        }
    }
    WorldShadowLevels_Deconstruct(&Levels);
}

static void TheSettingsComeIntoRange(TestRun* run)
{
    WorldShadowLevelsSettings Bad = { .LevelCount = 0U, .Factor = NAN, .MapSize = -3, .Reach = -1.0f };
    WorldShadowLevelsSettings Fixed = WorldShadowLevelsSettings_Sanitize(&Bad);
    TEST_CHECK(run, (Fixed.LevelCount == 1U) && (Fixed.Factor == WORLD_SHADOW_DEFAULT_FACTOR));
    TEST_CHECK(run, (Fixed.MapSize == WORLD_SHADOW_DEFAULT_MAP_SIZE) && (Fixed.Reach == 0.0f));
    for (uint32_t Level = 0U; Level < WORLD_SHADOW_MAX_LEVELS; Level++)
    {
        TEST_CHECK(run, Fixed.UpdatePeriods[Level] == 1U);
    }
    Bad = DefaultSettings();
    Bad.LevelCount = 40U;
    Bad.Factor = 1.0f;
    Bad.Reach = INFINITY;
    Fixed = WorldShadowLevelsSettings_Sanitize(&Bad);
    TEST_CHECK(run, (Fixed.LevelCount == WORLD_SHADOW_MAX_LEVELS) && (Fixed.Factor == WORLD_SHADOW_DEFAULT_FACTOR));
    TEST_CHECK(run, Fixed.Reach == 0.0f);
    WorldShadowLevelsSettings Good = DefaultSettings();
    Fixed = WorldShadowLevelsSettings_Sanitize(&Good);
    TEST_CHECK(run, (Fixed.LevelCount == Good.LevelCount) && (Fixed.Factor == Good.Factor) && (Fixed.Reach == Good.Reach));
}


// Public functions.
void WorldShadowLevelsTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_levels_grow_by_the_factor", TheLevelsGrowByTheFactor },
        { "the_reach_decides_the_levels_used", TheReachDecidesTheLevelsUsed },
        { "a_point_reads_the_smallest_level_that_holds_it", APointReadsTheSmallestLevelThatHoldsIt },
        { "the_reach_fades_the_shadow_out", TheReachFadesTheShadowOut },
        { "small_casters_stay_out_of_coarse_levels", SmallCastersStayOutOfCoarseLevels },
        { "far_levels_pick_the_lod_of_their_inner_edge", FarLevelsPickTheLodOfTheirInnerEdge },
        { "a_level_leaves_out_what_the_level_before_holds", ALevelLeavesOutWhatTheLevelBeforeHolds },
        { "periods_that_are_powers_of_two_never_meet", PeriodsThatArePowersOfTwoNeverMeet },
        { "the_far_levels_render_on_their_schedule", TheFarLevelsRenderOnTheirSchedule },
        { "a_turn_or_a_move_renders_a_kept_level_at_once", ATurnOrAMoveRendersAKeptLevelAtOnce },
        { "new_settings_render_every_level_again", NewSettingsRenderEveryLevelAgain },
        { "a_kept_level_is_read_as_it_was_rendered", AKeptLevelIsReadAsItWasRendered },
        { "everything_within_the_reach_lies_in_every_levels_depth", EverythingWithinTheReachLiesInEveryLevelsDepth },
        { "the_settings_come_into_range", TheSettingsComeIntoRange },
    };
    TestRun_RunSuite(run, "WorldShadowLevels", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
