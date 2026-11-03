#include "TestSuites.h"
#include "WorldFarShadows.h"
#include "WorldRenderSpace.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"
#include "wr/WRVectorMath.h"
#include <math.h>


/**
 * The terrain's shadows at any distance (source/WorldFarShadows.h): the nested maps' sizes and their frame on the
 * coarsest texel grid, the maps drawn from above, the light's two disc edges, the sweep's passes and their schedule
 * across frames, which map a point reads with its height lowered by the curve, the disc's share, and the sweep's
 * doubling against a brute-force march (exact along an axis, close at a slant), ridges' shadows with the exclusion, and a
 * ray continued in the coarser map.
 */


// Macros.
/** The small maps the CPU sweeps run on (texels a side). */
#define SMALL_MAP_SIZE 64
/** How close the doubling's shadow heights must come to the brute-force march's along an axis (float rounding of the
 *  climbs' sums), and at a slant (the bilinear reads of maxima: metres on a hill of HILL_HEIGHT). */
#define AXIS_TOLERANCE 1.0e-2f
#define SLANT_TOLERANCE 0.03f
/** The smooth hill the slanted sweep runs over: its height and its spread, in texels. */
#define HILL_HEIGHT 400.0f
#define HILL_SPREAD 9.0f
/** One degree, in radians. */
#define DEGREE 0.017453292519943295


// Static functions.
/* Whether two floats agree within a tolerance. */
static bool AreNear(float a, float b, float tolerance)
{
    return fabsf(a - b) <= tolerance;
}

/* A unit direction toward a light at an elevation, along a horizontal direction (x, z). */
static Vector3 LightToward(double elevation, double x, double z)
{
    double Length = sqrt((x * x) + (z * z));
    return (Vector3){ (float)(cos(elevation) * x / Length), (float)sin(elevation), (float)(cos(elevation) * z / Length) };
}

/* A small map's heights from a function of the texel's column and row. */
static float* MakeHeights(int size, float (*height)(int column, int row))
{
    float* Heights = Memory_Allocate(sizeof(float) * (size_t)size * (size_t)size);
    for (int Row = 0; Row < size; Row++)
    {
        for (int Column = 0; Column < size; Column++)
        {
            Heights[(size_t)Row * (size_t)size + (size_t)Column] = height(Column, Row);
        }
    }
    return Heights;
}

/* Ground far below every ray in these tests: it casts nothing. */
static float SunkenGround(int column, int row)
{
    (void)column;
    (void)row;
    return -10000.0f;
}

/* A ridge across the map at column 40, 100 m high, flat ground elsewhere. */
static float RidgeAtColumn40(int column, int row)
{
    (void)row;
    return (column == 40) ? 100.0f : 0.0f;
}

/* Bumpy ground whose texels are all different (a hash): the axis sweep must still be exact. */
static float BumpyGround(int column, int row)
{
    uint32_t Hash = ((uint32_t)column * 374761393U) ^ ((uint32_t)row * 668265263U);
    Hash = (Hash ^ (Hash >> 13)) * 1274126177U;
    return (float)((Hash >> 8) & 1023U) * 0.25f;
}

/* A round, smooth hill in the middle of the map. */
static float SmoothHill(int column, int row)
{
    float X = ((float)column - 32.0f) / HILL_SPREAD;
    float Y = ((float)row - 32.0f) / HILL_SPREAD;
    return HILL_HEIGHT * expf(-((X * X) + (Y * Y)));
}

/* A small map's bilinear height at a uv, clamped at its edges (GL's reads). */
static float HeightAt(const float* heights, int size, float u, float v)
{
    float X = u * (float)size - 0.5f;
    float Y = v * (float)size - 0.5f;
    int Column = (int)floorf(X);
    int Row = (int)floorf(Y);
    float FractionX = X - (float)Column;
    float FractionY = Y - (float)Row;
    float Corners[4];
    for (int Corner = 0; Corner < 4; Corner++)
    {
        int C = Column + (Corner & 1);
        int R = Row + (Corner >> 1);
        C = (C < 0) ? 0 : ((C >= size) ? (size - 1) : C);
        R = (R < 0) ? 0 : ((R >= size) ? (size - 1) : R);
        Corners[Corner] = heights[(size_t)R * (size_t)size + (size_t)C];
    }
    float Low = Corners[0] + ((Corners[1] - Corners[0]) * FractionX);
    float High = Corners[2] + ((Corners[3] - Corners[2]) * FractionX);
    return Low + ((High - Low) * FractionY);
}

/* The brute-force march: for a texel, the highest of the ray's samples every texel from the exclusion on, on the map. */
static void MarchTexel(const float* heights, int size, uint32_t map, const WorldFarShadowLight* light, int column,
    int row, float* outTop, float* outBottom)
{
    float Width = (float)WorldFarShadows_GetMapWidth(map);
    float Texel = (float)WorldFarShadows_GetTexel(map, size);
    float Exclusion = (float)WORLD_FAR_SHADOW_EXCLUSION_TEXELS * Texel;
    float U = ((float)column + 0.5f) / (float)size;
    float V = ((float)row + 0.5f) / (float)size;
    *outTop = WORLD_FAR_SHADOW_NO_GROUND;
    *outBottom = WORLD_FAR_SHADOW_NO_GROUND;
    for (int Sample = 0; Sample < 4 * size; Sample++)
    {
        float Run = Exclusion + ((float)Sample * Texel);
        float SampleU = U + (light->Toward.x * Run / Width);
        float SampleV = V + (light->Toward.y * Run / Width);
        if ((SampleU < 0.0f) || (SampleU > 1.0f) || (SampleV < 0.0f) || (SampleV > 1.0f))
        {
            break;
        }
        float Height = HeightAt(heights, size, SampleU, SampleV);
        *outTop = fmaxf(*outTop, Height - (Run * light->TopSlope));
        *outBottom = fmaxf(*outBottom, Height - (Run * light->BottomSlope));
    }
}

/* Sweeps a small map on the CPU (WorldFarShadows_Sweep); the result's (top, bottom) pairs, to free. */
static float* SweepMap(const float* heights, const float* coarser, uint32_t map, const WorldFarShadowLight* light)
{
    size_t Count = (size_t)SMALL_MAP_SIZE * (size_t)SMALL_MAP_SIZE * 2U;
    float* Scratch = Memory_Allocate(sizeof(float) * Count);
    float* Result = Memory_Allocate(sizeof(float) * Count);
    WorldFarShadows_Sweep(heights, coarser, map, SMALL_MAP_SIZE, light, Scratch, Result);
    Memory_Free(Scratch);
    return Result;
}

/* A texel's (top, bottom) shadow heights in a swept map. */
static void ReadTexel(const float* swept, int column, int row, float* outTop, float* outBottom)
{
    size_t Index = ((size_t)row * (size_t)SMALL_MAP_SIZE + (size_t)column) * 2U;
    *outTop = swept[Index];
    *outBottom = swept[Index + 1U];
}

/* The input of a frame for the schedule tests. */
static WorldFarShadowsInput MakeInput(Vector3Double origin, Vector3 towardLight)
{
    WorldFarShadowsInput Input =
    {
        .Origin = origin,
        .TowardLight = towardLight,
        .DiscRadius = WORLD_FAR_SHADOW_SUN_RADIUS,
        .MapSize = WORLD_FAR_SHADOW_DEFAULT_MAP_SIZE,
        .CurvatureScale = WORLD_RENDER_SPACE_CURVATURE_SCALE(6371000.0),
        .TerrainListingCount = 1U,
        .DrawDistanceScale = 1.0f,
        .ShadowDistanceScale = 1.0f
    };
    return Input;
}

/* Runs the schedule's frames until the sweep that runs finishes (at most 100 frames). */
static void FinishSweep(WorldFarShadows* schedule, const WorldFarShadowsInput* input, WorldFarShadowsWork* ioWork)
{
    for (uint32_t Frame = 0U; (Frame < 100U) && !ioWork->FinishesSweep; Frame++)
    {
        WorldFarShadows_BeginFrame(schedule, input, ioWork);
    }
}


// Test cases.
static void TheMapsGrowByTheFactor(TestRun* run)
{
    TEST_CHECK(run, WorldFarShadows_GetMapWidth(0U) == 8192.0);
    TEST_CHECK(run, WorldFarShadows_GetMapWidth(1U) == 32768.0);
    TEST_CHECK(run, WorldFarShadows_GetMapWidth(2U) == 131072.0);
    TEST_CHECK(run, WorldFarShadows_GetTexel(0U, 1024) == 8.0);
    TEST_CHECK(run, WorldFarShadows_GetTexel(2U, 1024) == 128.0);
    TEST_CHECK(run, WorldFarShadows_GetTexel(1U, 512) == 64.0);
    // The map size comes into range as a power of two.
    TEST_CHECK(run, WorldFarShadows_SanitizeMapSize(1024) == 1024);
    TEST_CHECK(run, WorldFarShadows_SanitizeMapSize(1000) == 512);
    TEST_CHECK(run, WorldFarShadows_SanitizeMapSize(100) == WORLD_FAR_SHADOW_MIN_MAP_SIZE);
    TEST_CHECK(run, WorldFarShadows_SanitizeMapSize(-7) == WORLD_FAR_SHADOW_MIN_MAP_SIZE);
    TEST_CHECK(run, WorldFarShadows_SanitizeMapSize(1 << 20) == WORLD_FAR_SHADOW_MAX_MAP_SIZE);
    // The sweep's doublings cover each size's diagonal.
    for (int Size = WORLD_FAR_SHADOW_MIN_MAP_SIZE; Size <= WORLD_FAR_SHADOW_MAX_MAP_SIZE; Size *= 2)
    {
        uint32_t Passes = WorldFarShadows_GetPassesPerMap(Size);
        TEST_CHECK(run, ldexp(1.0, (int)Passes - 1) >= 1.4142135623730951 * (double)Size);
        TEST_CHECK(run, ldexp(1.0, (int)Passes - 2) < 1.4142135623730951 * (double)Size);
        TEST_CHECK(run, WorldFarShadows_GetSweepPassCount(Size) == (Passes + 1U) * WORLD_FAR_SHADOW_MAP_COUNT);
    }
    TEST_CHECK(run, WorldFarShadows_GetPassesPerMap(1024) == 12U);
}

static void TheFrameSitsOnTheCoarsestTexelGrid(TestRun* run)
{
    WorldFarShadowFrame Frame = WorldFarShadows_GetFrame(Vector3Double_Create(1000.7, 333.3, -5555.5), 1024, 0.5f);
    TEST_CHECK(run, Frame.Center.X == 1024.0);
    TEST_CHECK(run, Frame.Center.Z == -5504.0);
    TEST_CHECK(run, Frame.Center.Y == 0.0);
    TEST_CHECK(run, (Frame.MapSize == 1024) && (Frame.CurvatureScale == 0.5f));
    // Every map's texels then lie on the world's grid of their size: a map drawn again holds the same texels.
    for (uint32_t Map = 0U; Map < WORLD_FAR_SHADOW_MAP_COUNT; Map++)
    {
        double Texel = WorldFarShadows_GetTexel(Map, 1024);
        double Edge = Frame.Center.X - (WorldFarShadows_GetMapWidth(Map) * 0.5);
        TEST_CHECK(run, fmod(Edge, Texel) == 0.0);
    }
    // Far out, in double.
    WorldFarShadowFrame Far = WorldFarShadows_GetFrame(Vector3Double_Create(1.0e7 + 63.9, 1500.0, -1.0e7 - 64.1), 1024,
        0.0f);
    TEST_CHECK(run, Far.Center.X == 1.0e7);
    TEST_CHECK(run, Far.Center.Z == -1.0e7 - 128.0);
    TEST_CHECK(run, Far.Center.Y == 1024.0);
}

static void AMapIsDrawnFromAboveHigherNearer(TestRun* run)
{
    Matrix View;
    Matrix Projection;
    WorldFarShadows_GetMapMatrices(1U, &View, &Projection);
    Matrix ViewProjection = MatrixMultiply(View, Projection);
    double Half = WorldFarShadows_GetMapWidth(1U) * 0.5;
    // x runs along the map's u, z along its v: the corner (+x, -z) at clip (1, -1).
    Vector3 Corner = Vector3Transform((Vector3){ (float)Half, 0.0f, (float)-Half }, ViewProjection);
    TEST_CHECK(run, AreNear(Corner.x, 1.0f, 1e-6f) && AreNear(Corner.y, -1.0f, 1e-6f));
    Vector3 Low = Vector3Transform((Vector3){ 0.0f, -100.0f, 0.0f }, ViewProjection);
    Vector3 High = Vector3Transform((Vector3){ 0.0f, 900.0f, 0.0f }, ViewProjection);
    TEST_CHECK(run, High.z < Low.z); // nearer: the depth test keeps the highest surface
    TEST_CHECK(run, (High.z > -1.0f) && (Low.z < 1.0f));
    // A drop moves only the depth: the clip-space up has no x or y.
    Vector4 Up = WorldRenderSpace_GetClipUp(ViewProjection);
    TEST_CHECK(run, (Up.x == 0.0f) && (Up.y == 0.0f) && (Up.z < 0.0f) && (Up.w == 0.0f));
    TEST_CHECK(run, AreNear(Up.z * WORLD_FAR_SHADOW_HEIGHT_RANGE, -1.0f, 1e-6f));
}

static void TheLightHasTwoEdges(TestRun* run)
{
    double Elevation = 5.0 * DEGREE;
    WorldFarShadowLight Light = WorldFarShadows_GetLight(LightToward(Elevation, 3.0, -4.0), 0.004f);
    TEST_CHECK(run, AreNear(Light.Toward.x, 0.6f, 1e-6f) && AreNear(Light.Toward.y, -0.8f, 1e-6f));
    TEST_CHECK(run, AreNear(Light.TopSlope, tanf((float)Elevation + 0.004f), 1e-5f));
    TEST_CHECK(run, AreNear(Light.BottomSlope, tanf((float)Elevation - 0.004f), 1e-5f));
    TEST_CHECK(run, Light.TopSlope > Light.BottomSlope);
    // A light below the horizon by its disc: the bottom edge's ray goes down.
    WorldFarShadowLight Setting = WorldFarShadows_GetLight(LightToward(0.002, 1.0, 0.0), 0.004f);
    TEST_CHECK(run, (Setting.BottomSlope < 0.0f) && (Setting.TopSlope > 0.0f));
    // Straight up: every ray clears every ridge.
    WorldFarShadowLight Up = WorldFarShadows_GetLight((Vector3){ 0.0f, 1.0f, 0.0f }, 0.004f);
    TEST_CHECK(run, (Up.TopSlope >= 1.0e6f) && (Up.BottomSlope >= 1.0e6f));
    TEST_CHECK(run, (Up.Toward.x == 1.0f) && (Up.Toward.y == 0.0f));
    WorldFarShadowLight None = WorldFarShadows_GetLight((Vector3){ 0.0f, 0.0f, 0.0f }, 0.004f);
    TEST_CHECK(run, None.TopSlope >= 1.0e6f);
}

static void TheSweepRunsCoarseMapsFirstAndCopiesLast(TestRun* run)
{
    int Size = 1024;
    uint32_t PerMap = WorldFarShadows_GetPassesPerMap(Size);
    uint32_t Count = WorldFarShadows_GetSweepPassCount(Size);
    double Texel[WORLD_FAR_SHADOW_MAP_COUNT];
    for (uint32_t Map = 0U; Map < WORLD_FAR_SHADOW_MAP_COUNT; Map++)
    {
        Texel[Map] = WorldFarShadows_GetTexel(Map, Size);
    }
    for (uint32_t Index = 0U; Index < Count; Index++)
    {
        WorldFarShadowPass Pass = WorldFarShadows_GetPass(Index, Size);
        if (Index >= (PerMap * WORLD_FAR_SHADOW_MAP_COUNT))
        {
            // The copies: coarsest first, finest last.
            uint32_t Copy = Index - (PerMap * WORLD_FAR_SHADOW_MAP_COUNT);
            TEST_CHECK(run, Pass.IsCopy && (Pass.Map == WORLD_FAR_SHADOW_MAP_COUNT - 1U - Copy));
            continue;
        }
        TEST_CHECK(run, !Pass.IsCopy);
        TEST_CHECK(run, Pass.Map == WORLD_FAR_SHADOW_MAP_COUNT - 1U - (Index / PerMap));
        TEST_CHECK(run, Pass.Step == Index % PerMap);
        // The passes take turns, and each map's last one writes its own target.
        TEST_CHECK(run, Pass.WritesMapTarget == (((PerMap - 1U - Pass.Step) % 2U) == 0U));
        if (Pass.Step == (PerMap - 1U))
        {
            TEST_CHECK(run, Pass.WritesMapTarget);
        }
        TEST_CHECK(run, Pass.Width == (float)WorldFarShadows_GetMapWidth(Pass.Map));
        double Expected = (Pass.Step == 0U) ? (WORLD_FAR_SHADOW_EXCLUSION_TEXELS * Texel[Pass.Map])
            : (Texel[Pass.Map] * ldexp(1.0, (int)Pass.Step - 1));
        TEST_CHECK(run, Pass.Run == (float)Expected);
        // Only a finer map's first pass continues into the coarser one.
        bool ExpectsCoarser = (Pass.Step == 0U) && (Pass.Map < (WORLD_FAR_SHADOW_MAP_COUNT - 1U));
        TEST_CHECK(run, Pass.HasCoarser == ExpectsCoarser);
        if (ExpectsCoarser)
        {
            TEST_CHECK(run, Pass.CoarserWidth == (float)WorldFarShadows_GetMapWidth(Pass.Map + 1U));
            TEST_CHECK(run, Pass.CoarserExclusion == (float)(WORLD_FAR_SHADOW_EXCLUSION_TEXELS * Texel[Pass.Map + 1U]));
        }
    }
}

static void TheScheduleDrawsSweepsAndFinishes(TestRun* run)
{
    WorldFarShadows Schedule;
    Memory_Zero(&Schedule, sizeof(Schedule));
    Vector3 Light = LightToward(10.0 * DEGREE, 1.0, 0.3);
    WorldFarShadowsInput Input = MakeInput(Vector3Double_Create(50.0, 2.0, -40.0), Light);
    uint32_t Count = WorldFarShadows_GetSweepPassCount(Input.MapSize);
    WorldFarShadowsWork Work;
    WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
    // The first frame draws the maps and starts sweeping them.
    TEST_CHECK(run, Work.DrawsMaps && (Work.FirstPass == 0U) && (Work.PassCount == WORLD_FAR_SHADOW_PASSES_PER_FRAME));
    TEST_CHECK(run, (Work.DrawFrame.Center.X == 0.0) && (Work.DrawFrame.Center.Z == 0.0));
    TEST_CHECK(run, !Work.FinishesSweep && !Schedule._hasResult);
    uint32_t Done = Work.PassCount;
    uint32_t Frames = 1U;
    while (!Work.FinishesSweep && (Frames < 100U))
    {
        WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
        TEST_CHECK(run, !Work.DrawsMaps && (Work.FirstPass == Done) && (Work.PassCount > 0U));
        Done += Work.PassCount;
        Frames++;
    }
    TEST_CHECK(run, Work.FinishesSweep && (Done == Count) && Schedule._hasResult);
    TEST_CHECK(run, Frames == (Count + WORLD_FAR_SHADOW_PASSES_PER_FRAME - 1U) / WORLD_FAR_SHADOW_PASSES_PER_FRAME);
    // Nothing to do while nothing changes.
    WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
    TEST_CHECK(run, !Work.DrawsMaps && (Work.PassCount == 0U));
    // A turn below the limit keeps the result; past it, a sweep without drawing the maps again.
    Input.TowardLight = LightToward(10.05 * DEGREE, 1.0, 0.3);
    WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
    TEST_CHECK(run, Work.PassCount == 0U);
    Input.TowardLight = LightToward(10.2 * DEGREE, 1.0, 0.3);
    WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
    TEST_CHECK(run, !Work.DrawsMaps && (Work.FirstPass == 0U) && (Work.PassCount > 0U));
    TEST_CHECK(run, AreNear(atanf(Work.SweepLight.TopSlope), (float)(10.2 * DEGREE) + WORLD_FAR_SHADOW_SUN_RADIUS, 1e-5f));
    // A camera that moves past an eighth of the finest map during a sweep waits for it to finish.
    Input.Origin = Vector3Double_Create(50.0 + 1100.0, 2.0, -40.0);
    WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
    TEST_CHECK(run, !Work.DrawsMaps && (Work.FirstPass == WORLD_FAR_SHADOW_PASSES_PER_FRAME));
    FinishSweep(&Schedule, &Input, &Work);
    TEST_CHECK(run, Work.FinishesSweep);
    // The result keeps the frame its maps were drawn in.
    TEST_CHECK(run, Schedule._resultFrame.Center.X == 0.0);
    WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
    TEST_CHECK(run, Work.DrawsMaps && (Work.DrawFrame.Center.X == 1152.0) && (Work.FirstPass == 0U));
    FinishSweep(&Schedule, &Input, &Work);
    TEST_CHECK(run, Work.FinishesSweep);
    TEST_CHECK(run, Schedule._resultFrame.Center.X == 1152.0);
    // A terrain that changes, the curve, the size and the draw and shadow distance scales draw the maps again.
    Input.TerrainListingCount = 2U;
    WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
    TEST_CHECK(run, Work.DrawsMaps);
    WorldFarShadows_Invalidate(&Schedule);
    TEST_CHECK(run, !Schedule._hasResult && !Schedule._hasMaps && !Schedule._isSweeping);
    WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
    FinishSweep(&Schedule, &Input, &Work);
    TEST_CHECK(run, Work.FinishesSweep);
    Input.CurvatureScale = 0.0f;
    WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
    TEST_CHECK(run, Work.DrawsMaps && (Work.DrawFrame.CurvatureScale == 0.0f));
    FinishSweep(&Schedule, &Input, &Work);
    TEST_CHECK(run, Work.FinishesSweep);
    Input.ShadowDistanceScale = 0.5f;
    WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
    TEST_CHECK(run, Work.DrawsMaps);
    FinishSweep(&Schedule, &Input, &Work);
    TEST_CHECK(run, Work.FinishesSweep);
    // The draw distance scale scales the casters' shadow distances too.
    Input.DrawDistanceScale = 1.5f;
    WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
    TEST_CHECK(run, Work.DrawsMaps);
    FinishSweep(&Schedule, &Input, &Work);
    TEST_CHECK(run, Work.FinishesSweep);
    WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
    TEST_CHECK(run, !Work.DrawsMaps && (Work.PassCount == 0U));
    Input.MapSize = 512;
    WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
    TEST_CHECK(run, Work.DrawsMaps && (Work.DrawFrame.MapSize == 512));
    uint32_t Small = Work.PassCount;
    for (uint32_t Frame = 0U; (Frame < 100U) && !Work.FinishesSweep; Frame++)
    {
        WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
        Small += Work.PassCount;
    }
    TEST_CHECK(run, Small == WorldFarShadows_GetSweepPassCount(512));
}

static void TheCopiesRunInTheFrameThatFinishesTheSweep(TestRun* run)
{
    // At every map size: the passes in order, no frame over its budget, and the three copies together in the frame that
    // finishes the sweep (the lit shaders read every map at once, with the finished sweep's frame from that frame on).
    for (int Size = WORLD_FAR_SHADOW_MIN_MAP_SIZE; Size <= WORLD_FAR_SHADOW_MAX_MAP_SIZE; Size *= 2)
    {
        WorldFarShadows Schedule;
        Memory_Zero(&Schedule, sizeof(Schedule));
        WorldFarShadowsInput Input = MakeInput(Vector3Double_Create(0.0, 0.0, 0.0), LightToward(10.0 * DEGREE, 1.0, 0.3));
        Input.MapSize = Size;
        uint32_t FirstCopy = WorldFarShadows_GetPassesPerMap(Size) * WORLD_FAR_SHADOW_MAP_COUNT;
        uint32_t Count = WorldFarShadows_GetSweepPassCount(Size);
        uint32_t Done = 0U;
        uint32_t Frames = 0U;
        WorldFarShadowsWork Work = { 0 };
        while (!Work.FinishesSweep && (Frames < 100U))
        {
            WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
            TEST_CHECK(run, (Work.FirstPass == Done) && (Work.PassCount > 0U)
                && (Work.PassCount <= WORLD_FAR_SHADOW_PASSES_PER_FRAME));
            bool HasCopies = (Work.FirstPass + Work.PassCount) > FirstCopy;
            TEST_CHECK(run, HasCopies == Work.FinishesSweep);
            TEST_CHECK(run, !HasCopies || (Work.FirstPass <= FirstCopy));
            Done += Work.PassCount;
            Frames++;
        }
        TEST_CHECK(run, Work.FinishesSweep && (Done == Count));
        // Holding the copies back costs no frame at these sizes.
        TEST_CHECK(run, Frames == (Count + WORLD_FAR_SHADOW_PASSES_PER_FRAME - 1U) / WORLD_FAR_SHADOW_PASSES_PER_FRAME);
    }
}

static void AFrameWithoutTheFarShadowsDropsTheSweep(TestRun* run)
{
    WorldFarShadows Schedule;
    Memory_Zero(&Schedule, sizeof(Schedule));
    WorldFarShadowsInput Input = MakeInput(Vector3Double_Create(0.0, 0.0, 0.0), LightToward(10.0 * DEGREE, 1.0, 0.3));
    WorldFarShadowsWork Work;
    // The first maps' sweep, dropped after a frame: the next frame with the far shadows sweeps the same maps afresh.
    WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
    TEST_CHECK(run, Work.DrawsMaps && !Work.FinishesSweep);
    WorldFarShadows_SkipFrame(&Schedule);
    TEST_CHECK(run, !Schedule._isSweeping && !Schedule._hasResult);
    WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
    TEST_CHECK(run, !Work.DrawsMaps && (Work.FirstPass == 0U) && (Work.PassCount > 0U));
    FinishSweep(&Schedule, &Input, &Work);
    TEST_CHECK(run, Work.FinishesSweep && Schedule._hasResult);
    // A sweep for a turned light, dropped: the frames with the far shadows again sweep for their own light, not the
    // one it started with.
    Input.TowardLight = LightToward(12.0 * DEGREE, 1.0, 0.3);
    WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
    TEST_CHECK(run, !Work.DrawsMaps && (Work.FirstPass == 0U) && (Work.PassCount > 0U));
    WorldFarShadows_SkipFrame(&Schedule);
    WorldFarShadows_SkipFrame(&Schedule);
    Input.TowardLight = LightToward(30.0 * DEGREE, 1.0, 0.3);
    WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
    TEST_CHECK(run, !Work.DrawsMaps && (Work.FirstPass == 0U) && (Work.PassCount > 0U));
    TEST_CHECK(run, AreNear(atanf(Work.SweepLight.TopSlope), (float)(30.0 * DEGREE) + WORLD_FAR_SHADOW_SUN_RADIUS,
        1e-5f));
    FinishSweep(&Schedule, &Input, &Work);
    TEST_CHECK(run, Work.FinishesSweep && AreNear(Schedule._resultTowardLight.y, Input.TowardLight.y, 1e-7f));
    // The maps drawn again for a changed terrain, their sweep dropped: they still get one, though the light held still.
    Input.TerrainListingCount = 7U;
    WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
    TEST_CHECK(run, Work.DrawsMaps && !Work.FinishesSweep);
    WorldFarShadows_SkipFrame(&Schedule);
    WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
    TEST_CHECK(run, !Work.DrawsMaps && (Work.FirstPass == 0U) && (Work.PassCount > 0U));
    FinishSweep(&Schedule, &Input, &Work);
    TEST_CHECK(run, Work.FinishesSweep);
    // With no sweep running a frame without them changes nothing: the finished sweep stands.
    WorldFarShadows_SkipFrame(&Schedule);
    WorldFarShadows_BeginFrame(&Schedule, &Input, &Work);
    TEST_CHECK(run, !Work.DrawsMaps && (Work.PassCount == 0U) && Schedule._hasResult);
}

static void APointReadsTheFinestMapThatHoldsIt(TestRun* run)
{
    float Scale = WORLD_RENDER_SPACE_CURVATURE_SCALE(6371000.0);
    WorldFarShadowFrame Frame = WorldFarShadows_GetFrame(Vector3Double_Create(1000.0, 0.0, 0.0), 1024, Scale);
    Vector3Double Origin = Vector3Double_Create(1000.0, 50.0, 0.0);
    float Edge = WorldFarShadows_GetEdge(1024);
    TEST_CHECK(run, AreNear(Edge, 1.0f - (5.0f / 512.0f), 1e-7f));
    WorldFarShadowLookup Near = WorldFarShadows_GetLookup(&Frame, Origin, (Vector3){ 100.0f, -50.0f, 0.0f });
    TEST_CHECK(run, (Near.Map == 0U) && (Near.NextWeight == 0.0f));
    // The place is relative to the frame's centre; the height lowered by its drop from there.
    TEST_CHECK(run, AreNear(Near.Place.x, 76.0f, 1e-4f) && AreNear(Near.Place.y, 0.0f, 1e-4f));
    TEST_CHECK(run, AreNear(Near.Height, -(76.0f * 76.0f * Scale), 1e-6f));
    // Out across the bands into the coarser maps, then none.
    float Half0 = 4096.0f * Edge;
    WorldFarShadowLookup Band = WorldFarShadows_GetLookup(&Frame, Origin,
        (Vector3){ Half0 * (1.0f - (WORLD_FAR_SHADOW_BLEND_SHARE * 0.5f)) - 24.0f, 0.0f, 0.0f });
    TEST_CHECK(run, (Band.Map == 0U) && (Band.NextWeight > 0.4f) && (Band.NextWeight < 0.6f));
    WorldFarShadowLookup Middle = WorldFarShadows_GetLookup(&Frame, Origin, (Vector3){ 0.0f, 0.0f, 9000.0f });
    TEST_CHECK(run, (Middle.Map == 1U) && (Middle.NextWeight == 0.0f));
    TEST_CHECK(run, AreNear(Middle.Height, 50.0f - (((24.0f * 24.0f) + (9000.0f * 9000.0f)) * Scale), 1e-3f));
    WorldFarShadowLookup Coarse = WorldFarShadows_GetLookup(&Frame, Origin, (Vector3){ -30000.0f, 0.0f, 20000.0f });
    TEST_CHECK(run, Coarse.Map == 2U);
    WorldFarShadowLookup Beyond = WorldFarShadows_GetLookup(&Frame, Origin, (Vector3){ 70000.0f, 0.0f, 0.0f });
    TEST_CHECK(run, Beyond.Map == WORLD_FAR_SHADOW_MAP_COUNT);
    // Far from the world's origin the frame's offset keeps the place exact.
    Vector3Double FarOrigin = Vector3Double_Create(1.0e7 + 5.25, 0.0, -1.0e7 + 2.5);
    WorldFarShadowFrame FarFrame = WorldFarShadows_GetFrame(FarOrigin, 1024, Scale);
    WorldFarShadowLookup FarPoint = WorldFarShadows_GetLookup(&FarFrame, FarOrigin, (Vector3){ 10.0f, 0.0f, 0.0f });
    TEST_CHECK(run, AreNear(FarPoint.Place.x, (float)((1.0e7 + 15.25) - FarFrame.Center.X), 1e-4f));
}

static void TheDiscShareRunsFromNoneToAll(TestRun* run)
{
    TEST_CHECK(run, WorldFarShadows_GetDiscShare(0.0f) == 0.0f);
    TEST_CHECK(run, AreNear(WorldFarShadows_GetDiscShare(1.0f), 1.0f, 1e-6f));
    TEST_CHECK(run, AreNear(WorldFarShadows_GetDiscShare(0.5f), 0.5f, 1e-6f));
    TEST_CHECK(run, WorldFarShadows_GetDiscShare(-3.0f) == 0.0f);
    TEST_CHECK(run, AreNear(WorldFarShadows_GetDiscShare(7.0f), 1.0f, 1e-6f));
    float Last = 0.0f;
    for (int Step = 1; Step <= 100; Step++)
    {
        float Fraction = (float)Step / 100.0f;
        float Share = WorldFarShadows_GetDiscShare(Fraction);
        TEST_CHECK(run, Share > Last);
        TEST_CHECK(run, AreNear(Share + WorldFarShadows_GetDiscShare(1.0f - Fraction), 1.0f, 1e-5f));
        Last = Share;
    }
    // The shadow: none above the bottom edge's height, all of it below the top edge's, between in between.
    TEST_CHECK(run, WorldFarShadows_GetShadow(10.0f, 0.0f, 5.0f) == 0.0f);
    TEST_CHECK(run, AreNear(WorldFarShadows_GetShadow(-1.0f, 0.0f, 5.0f), 1.0f, 1e-6f));
    float Between = WorldFarShadows_GetShadow(2.5f, 0.0f, 5.0f);
    TEST_CHECK(run, AreNear(Between, 0.5f, 1e-6f));
    // Edges that meet make a hard step.
    TEST_CHECK(run, WorldFarShadows_GetShadow(1.0f, 0.5f, 0.5f) == 0.0f);
    TEST_CHECK(run, AreNear(WorldFarShadows_GetShadow(0.0f, 0.5f, 0.5f), 1.0f, 1e-6f));
}

static void TheDoublingMatchesTheMarchAlongAnAxis(TestRun* run)
{
    float* Heights = MakeHeights(SMALL_MAP_SIZE, BumpyGround);
    // Toward +x and toward -z: every sample lands on a texel's centre, so the doubling is the march.
    static const double Directions[][2] = { { 1.0, 0.0 }, { 0.0, -1.0 } };
    for (size_t Direction = 0U; Direction < 2U; Direction++)
    {
        WorldFarShadowLight Light = WorldFarShadows_GetLight(LightToward(0.5 * DEGREE, Directions[Direction][0],
            Directions[Direction][1]), WORLD_FAR_SHADOW_SUN_RADIUS);
        Light.Toward = (Vector2){ (float)Directions[Direction][0], (float)Directions[Direction][1] };
        float* Swept = SweepMap(Heights, NULL, 2U, &Light);
        bool AreAllNear = true;
        for (int Row = 0; Row < SMALL_MAP_SIZE; Row++)
        {
            for (int Column = 0; Column < SMALL_MAP_SIZE; Column++)
            {
                float Top = 0.0f;
                float Bottom = 0.0f;
                float MarchedTop = 0.0f;
                float MarchedBottom = 0.0f;
                ReadTexel(Swept, Column, Row, &Top, &Bottom);
                MarchTexel(Heights, SMALL_MAP_SIZE, 2U, &Light, Column, Row, &MarchedTop, &MarchedBottom);
                AreAllNear = AreAllNear && AreNear(Top, MarchedTop, AXIS_TOLERANCE)
                    && AreNear(Bottom, MarchedBottom, AXIS_TOLERANCE);
            }
        }
        TEST_CHECK(run, AreAllNear);
        Memory_Free(Swept);
    }
    Memory_Free(Heights);
}

static void TheDoublingFollowsTheMarchAtASlant(TestRun* run)
{
    float* Heights = MakeHeights(SMALL_MAP_SIZE, SmoothHill);
    WorldFarShadowLight Light = WorldFarShadows_GetLight(LightToward(1.0 * DEGREE, 0.6, 0.8),
        WORLD_FAR_SHADOW_SUN_RADIUS);
    float* Swept = SweepMap(Heights, NULL, 2U, &Light);
    float Worst = 0.0f;
    for (int Row = 0; Row < SMALL_MAP_SIZE; Row++)
    {
        for (int Column = 0; Column < SMALL_MAP_SIZE; Column++)
        {
            float Top = 0.0f;
            float Bottom = 0.0f;
            float MarchedTop = 0.0f;
            float MarchedBottom = 0.0f;
            ReadTexel(Swept, Column, Row, &Top, &Bottom);
            MarchTexel(Heights, SMALL_MAP_SIZE, 2U, &Light, Column, Row, &MarchedTop, &MarchedBottom);
            if ((MarchedTop > -1.0e8f) && (Top > -1.0e8f))
            {
                Worst = fmaxf(Worst, fabsf(Top - MarchedTop));
            }
            // Never below the march by more than the filter's blur: the doubling's samples are a superset's blur.
            TEST_CHECK(run, (Top > -1.0e8f) == (MarchedTop > -1.0e8f));
        }
    }
    TEST_CHECK(run, Worst <= SLANT_TOLERANCE * HILL_HEIGHT);
    Memory_Free(Swept);
    Memory_Free(Heights);
}

static void ARidgeShadesTheValleyBesideIt(TestRun* run)
{
    float* Heights = MakeHeights(SMALL_MAP_SIZE, RidgeAtColumn40);
    // The light toward +x, its edges' slopes 0.1 and 0.12: the ridge (100 m) shades the ground west of it while 100 m
    // less the climb stays above it.
    WorldFarShadowLight Light = { .Toward = { 1.0f, 0.0f }, .TopSlope = 0.12f, .BottomSlope = 0.1f };
    uint32_t Map = 0U;
    float* Swept = SweepMap(Heights, NULL, Map, &Light);
    float Texel = (float)WorldFarShadows_GetTexel(Map, SMALL_MAP_SIZE);
    for (int Column = 0; Column < SMALL_MAP_SIZE; Column++)
    {
        float Top = 0.0f;
        float Bottom = 0.0f;
        ReadTexel(Swept, Column, 20, &Top, &Bottom);
        float Run = (float)(40 - Column) * Texel;
        float Shadow = WorldFarShadows_GetShadow(0.0f, Top, Bottom);
        if ((Column > 40) || (Run < (float)WORLD_FAR_SHADOW_EXCLUSION_TEXELS * Texel))
        {
            // Beyond the ridge, and within the exclusion of it (the shadow levels' casters): nothing.
            TEST_CHECK(run, Shadow == 0.0f);
        }
        else if ((100.0f - (Run * 0.1f)) < 0.0f)
        {
            TEST_CHECK(run, Shadow == 0.0f); // so far out that every ray clears it
        }
        else if ((100.0f - (Run * 0.12f)) > 0.0f)
        {
            TEST_CHECK(run, AreNear(Shadow, 1.0f, 1e-6f)); // even the disc's top is hidden
            TEST_CHECK(run, AreNear(Top, 100.0f - (Run * 0.12f), AXIS_TOLERANCE));
        }
        else
        {
            TEST_CHECK(run, (Shadow > 0.0f) && (Shadow < 1.0f)); // the penumbra
        }
    }
    // A point high enough is lit even right behind the ridge.
    float Top = 0.0f;
    float Bottom = 0.0f;
    ReadTexel(Swept, 35, 20, &Top, &Bottom);
    TEST_CHECK(run, WorldFarShadows_GetShadow(200.0f, Top, Bottom) == 0.0f);
    Memory_Free(Swept);
    Memory_Free(Heights);
}

static void ARayContinuesInTheCoarserMap(TestRun* run)
{
    // The coarser map (map 2, four times as wide) holds a ridge east of the finer map; the finer map's ground lies far
    // below every ray.
    // The ridge's column in the coarser map: 3/4 of the way across, where the finer map (its middle quarter) ends at 5/8.
    float* Coarse = MakeHeights(SMALL_MAP_SIZE, RidgeAtColumn40);
    WorldFarShadowLight Light = { .Toward = { 1.0f, 0.0f }, .TopSlope = 0.01f, .BottomSlope = 0.009f };
    float* CoarseSwept = SweepMap(Coarse, NULL, 2U, &Light);
    float* Flat = MakeHeights(SMALL_MAP_SIZE, SunkenGround);
    float* FineSwept = SweepMap(Flat, CoarseSwept, 1U, &Light);
    float* Alone = SweepMap(Flat, NULL, 1U, &Light);
    // The ridge's distance from a finer texel: from the centres, in metres.
    float RidgeX = ((40.5f / (float)SMALL_MAP_SIZE) - 0.5f) * (float)WorldFarShadows_GetMapWidth(2U);
    // Every texel whose ray leaves the finer map farther than its exclusion (the last few take the ridge from the edge of
    // the coarser map's own exclusion, blended).
    for (int Column = 0; Column < (SMALL_MAP_SIZE - 4); Column += 7)
    {
        float Top = 0.0f;
        float Bottom = 0.0f;
        ReadTexel(FineSwept, Column, 32, &Top, &Bottom);
        float X = (((float)Column + 0.5f) / (float)SMALL_MAP_SIZE - 0.5f) * (float)WorldFarShadows_GetMapWidth(1U);
        float Expected = 100.0f - ((RidgeX - X) * 0.01f);
        TEST_CHECK(run, AreNear(Top, Expected, 1e-2f));
        float AloneTop = 0.0f;
        float AloneBottom = 0.0f;
        ReadTexel(Alone, Column, 32, &AloneTop, &AloneBottom);
        TEST_CHECK(run, AloneTop < -9000.0f); // without the coarser map nothing casts
    }
    Memory_Free(Alone);
    Memory_Free(FineSwept);
    Memory_Free(Flat);
    Memory_Free(CoarseSwept);
    Memory_Free(Coarse);
}


// Public functions.
void WorldFarShadowsTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_maps_grow_by_the_factor", TheMapsGrowByTheFactor },
        { "the_frame_sits_on_the_coarsest_texel_grid", TheFrameSitsOnTheCoarsestTexelGrid },
        { "a_map_is_drawn_from_above_higher_nearer", AMapIsDrawnFromAboveHigherNearer },
        { "the_light_has_two_edges", TheLightHasTwoEdges },
        { "the_sweep_runs_coarse_maps_first_and_copies_last", TheSweepRunsCoarseMapsFirstAndCopiesLast },
        { "the_schedule_draws_sweeps_and_finishes", TheScheduleDrawsSweepsAndFinishes },
        { "the_copies_run_in_the_frame_that_finishes_the_sweep", TheCopiesRunInTheFrameThatFinishesTheSweep },
        { "a_frame_without_the_far_shadows_drops_the_sweep", AFrameWithoutTheFarShadowsDropsTheSweep },
        { "a_point_reads_the_finest_map_that_holds_it", APointReadsTheFinestMapThatHoldsIt },
        { "the_disc_share_runs_from_none_to_all", TheDiscShareRunsFromNoneToAll },
        { "the_doubling_matches_the_march_along_an_axis", TheDoublingMatchesTheMarchAlongAnAxis },
        { "the_doubling_follows_the_march_at_a_slant", TheDoublingFollowsTheMarchAtASlant },
        { "a_ridge_shades_the_valley_beside_it", ARidgeShadesTheValleyBesideIt },
        { "a_ray_continues_in_the_coarser_map", ARayContinuesInTheCoarserMap },
    };
    TestRun_RunSuite(run, "WorldFarShadows", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
