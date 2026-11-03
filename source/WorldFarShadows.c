// The terrain's shadows at any distance as pure maths: the nested height maps' layout and frame, the light's disc edges,
// the sweep's passes and their schedule across frames, the lookup's choice and the disc's share. See WorldFarShadows.h.

#include <math.h>
#include "WorldFarShadows.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"


// Macros.
/** A light this near straight up (the horizontal length of its unit direction) gets a fixed direction and huge slopes. */
#define STRAIGHT_UP_LENGTH 1.0e-6f
/** The slope of a light straight up: every ray clears every ridge. */
#define STRAIGHT_UP_SLOPE 1.0e6f
/** pi, for the disc's share. */
#define FAR_SHADOW_PI 3.14159265358979323846

_Static_assert(WORLD_FAR_SHADOW_MAP_COUNT <= WORLD_FAR_SHADOW_PASSES_PER_FRAME, "the copies run in one frame");


// Static functions.
/* @p value rounded to the nearest whole multiple of @p step. */
static double SnapTo(double value, double step)
{
    return floor((value / step) + 0.5) * step;
}

/* Whether the maps drawn for @p self no longer fit the frame's input. */
static bool AreMapsStale(const WorldFarShadows* self, const WorldFarShadowsInput* input)
{
    if (!self->_hasMaps)
    {
        return true;
    }
    const WorldFarShadowFrame* Frame = &self->_mapsFrame;
    double Limit = WORLD_FAR_SHADOW_RECENTRE_SHARE * WorldFarShadows_GetMapWidth(0U);
    bool HasMoved = (fabs(input->Origin.X - Frame->Center.X) > Limit) || (fabs(input->Origin.Z - Frame->Center.Z) > Limit)
        || !isfinite(input->Origin.X) || !isfinite(input->Origin.Z);
    return HasMoved || (Frame->MapSize != input->MapSize) || (Frame->CurvatureScale != input->CurvatureScale)
        || (self->_mapsListingCount != input->TerrainListingCount)
        || (self->_mapsDrawDistanceScale != input->DrawDistanceScale)
        || (self->_mapsShadowDistanceScale != input->ShadowDistanceScale);
}

/* Whether the light has turned past the limit from @p from (or switched across the sky). */
static bool HasLightTurned(Vector3 from, Vector3 to)
{
    Vector3 A = Vector3Normalize(from);
    Vector3 B = Vector3Normalize(to);
    float Cosine = fminf(fmaxf(Vector3DotProduct(A, B), -1.0f), 1.0f);
    return !(acosf(Cosine) <= (float)WORLD_FAR_SHADOW_TURN_LIMIT);
}

/* A map's exclusion's distance, metres. */
static double GetExclusion(uint32_t map, int mapSize)
{
    return WORLD_FAR_SHADOW_EXCLUSION_TEXELS * WorldFarShadows_GetTexel(map, mapSize);
}

/* A float texel of a two-channel map (top, bottom), clamped to its edge as GL's CLAMP_TO_EDGE reads it. */
static void ReadPair(const float* map, int mapSize, int column, int row, float* outTop, float* outBottom)
{
    int Column = (column < 0) ? 0 : ((column >= mapSize) ? (mapSize - 1) : column);
    int Row = (row < 0) ? 0 : ((row >= mapSize) ? (mapSize - 1) : row);
    size_t Index = ((size_t)Row * (size_t)mapSize + (size_t)Column) * 2U;
    *outTop = map[Index];
    *outBottom = map[Index + 1U];
}

/* Bilinear reads at a uv, as GL's linear filter with CLAMP_TO_EDGE: of a one-channel map, and of a two-channel one. */
static float SampleOne(const float* map, int mapSize, Vector2 uv)
{
    float X = uv.x * (float)mapSize - 0.5f;
    float Y = uv.y * (float)mapSize - 0.5f;
    int Column = (int)floorf(X);
    int Row = (int)floorf(Y);
    float FractionX = X - (float)Column;
    float FractionY = Y - (float)Row;
    float Values[4];
    for (int Corner = 0; Corner < 4; Corner++)
    {
        int C = Column + (Corner & 1);
        int R = Row + (Corner >> 1);
        C = (C < 0) ? 0 : ((C >= mapSize) ? (mapSize - 1) : C);
        R = (R < 0) ? 0 : ((R >= mapSize) ? (mapSize - 1) : R);
        Values[Corner] = map[(size_t)R * (size_t)mapSize + (size_t)C];
    }
    float Bottom = Values[0] + ((Values[1] - Values[0]) * FractionX);
    float Top = Values[2] + ((Values[3] - Values[2]) * FractionX);
    return Bottom + ((Top - Bottom) * FractionY);
}

static void SamplePair(const float* map, int mapSize, Vector2 uv, float* outTop, float* outBottom)
{
    float X = uv.x * (float)mapSize - 0.5f;
    float Y = uv.y * (float)mapSize - 0.5f;
    int Column = (int)floorf(X);
    int Row = (int)floorf(Y);
    float FractionX = X - (float)Column;
    float FractionY = Y - (float)Row;
    float Tops[4];
    float Bottoms[4];
    for (int Corner = 0; Corner < 4; Corner++)
    {
        ReadPair(map, mapSize, Column + (Corner & 1), Row + (Corner >> 1), &Tops[Corner], &Bottoms[Corner]);
    }
    float TopLow = Tops[0] + ((Tops[1] - Tops[0]) * FractionX);
    float TopHigh = Tops[2] + ((Tops[3] - Tops[2]) * FractionX);
    float BottomLow = Bottoms[0] + ((Bottoms[1] - Bottoms[0]) * FractionX);
    float BottomHigh = Bottoms[2] + ((Bottoms[3] - Bottoms[2]) * FractionX);
    *outTop = TopLow + ((TopHigh - TopLow) * FractionY);
    *outBottom = BottomLow + ((BottomHigh - BottomLow) * FractionY);
}

/* Whether a uv lies on a map (its sides included). */
static bool IsOnMap(Vector2 uv)
{
    return (uv.x >= 0.0f) && (uv.x <= 1.0f) && (uv.y >= 0.0f) && (uv.y <= 1.0f);
}

/* How far, in uv, a ray from @p uv along @p toward runs before it leaves the map (a large value along no axis). */
static float GetExitRun(Vector2 uv, Vector2 toward)
{
    float RunX = (toward.x > 0.0f) ? ((1.0f - uv.x) / toward.x) : ((toward.x < 0.0f) ? (uv.x / -toward.x) : 1.0e30f);
    float RunY = (toward.y > 0.0f) ? ((1.0f - uv.y) / toward.y) : ((toward.y < 0.0f) ? (uv.y / -toward.y) : 1.0e30f);
    return fmaxf(fminf(RunX, RunY), 0.0f);
}


// Internal functions (declared in WorldFarShadows.h).
double WorldFarShadows_GetMapWidth(uint32_t map)
{
    return WORLD_FAR_SHADOW_FINEST_WIDTH * pow(WORLD_FAR_SHADOW_MAP_FACTOR, (double)map);
}

double WorldFarShadows_GetTexel(uint32_t map, int mapSize)
{
    return WorldFarShadows_GetMapWidth(map) / (double)((mapSize > 0) ? mapSize : 1);
}

uint32_t WorldFarShadows_GetPassesPerMap(int mapSize)
{
    // The doublings reach 2^k texels from the exclusion on: enough when 2^k covers the diagonal (sqrt(2) x the side).
    double Diagonal = 1.4142135623730951 * (double)((mapSize > 0) ? mapSize : 1);
    uint32_t Doublings = 0U;
    while (ldexp(1.0, (int)Doublings) < Diagonal)
    {
        Doublings++;
    }
    return Doublings + 1U;
}

uint32_t WorldFarShadows_GetSweepPassCount(int mapSize)
{
    return (WorldFarShadows_GetPassesPerMap(mapSize) + 1U) * WORLD_FAR_SHADOW_MAP_COUNT;
}

int WorldFarShadows_SanitizeMapSize(int mapSize)
{
    int Size = WORLD_FAR_SHADOW_MIN_MAP_SIZE;
    while (((Size * 2) <= mapSize) && (Size < WORLD_FAR_SHADOW_MAX_MAP_SIZE))
    {
        Size *= 2;
    }
    return Size;
}

WorldFarShadowFrame WorldFarShadows_GetFrame(Vector3Double origin, int mapSize, float curvatureScale)
{
    double Grid = WorldFarShadows_GetTexel(WORLD_FAR_SHADOW_MAP_COUNT - 1U, mapSize);
    WorldFarShadowFrame Frame =
    {
        .Center = Vector3Double_Create(SnapTo(origin.X, Grid), SnapTo(origin.Y, WORLD_FAR_SHADOW_HEIGHT_STEP),
            SnapTo(origin.Z, Grid)),
        .MapSize = mapSize,
        .CurvatureScale = curvatureScale
    };
    return Frame;
}

void WorldFarShadows_GetMapMatrices(uint32_t map, Matrix* outView, Matrix* outProjection)
{
    // View x = world x, view y = world z, view z = world y: the map's u along x, its v along z, its depth the height.
    *outView = (Matrix){ 0 };
    outView->m0 = 1.0f;
    outView->m9 = 1.0f;
    outView->m6 = 1.0f;
    outView->m15 = 1.0f;
    double Half = WorldFarShadows_GetMapWidth(map) * 0.5;
    // OpenGL's orthographic depth: view z from -far to -near is visible; with near -H and far H every height in [-H, H]
    // is, the higher the nearer.
    *outProjection = MatrixOrtho(-Half, Half, -Half, Half, -(double)WORLD_FAR_SHADOW_HEIGHT_RANGE,
        (double)WORLD_FAR_SHADOW_HEIGHT_RANGE);
}

WorldFarShadowLight WorldFarShadows_GetLight(Vector3 towardLight, float discRadius)
{
    Vector3 Toward = Vector3Normalize(towardLight);
    if (Vector3LengthSqr(Toward) <= 0.0f)
    {
        Toward = (Vector3){ 0.0f, 1.0f, 0.0f };
    }
    WorldFarShadowLight Light;
    float Horizontal = sqrtf((Toward.x * Toward.x) + (Toward.z * Toward.z));
    if (!(Horizontal > STRAIGHT_UP_LENGTH))
    {
        Light.Toward = (Vector2){ 1.0f, 0.0f };
        Light.TopSlope = STRAIGHT_UP_SLOPE;
        Light.BottomSlope = STRAIGHT_UP_SLOPE;
        return Light;
    }
    Light.Toward = (Vector2){ Toward.x / Horizontal, Toward.z / Horizontal };
    float Elevation = atan2f(Toward.y, Horizontal);
    float Radius = fmaxf(discRadius, 0.0f);
    float HalfPi = (float)(FAR_SHADOW_PI * 0.5);
    float Top = fminf(Elevation + Radius, HalfPi - 1.0e-4f);
    float Bottom = fmaxf(Elevation - Radius, -HalfPi + 1.0e-4f);
    Light.TopSlope = tanf(Top);
    Light.BottomSlope = tanf(Bottom);
    return Light;
}

WorldFarShadowPass WorldFarShadows_GetPass(uint32_t pass, int mapSize)
{
    WorldFarShadowPass Pass = { 0 };
    uint32_t PassesPerMap = WorldFarShadows_GetPassesPerMap(mapSize);
    uint32_t SweepPasses = PassesPerMap * WORLD_FAR_SHADOW_MAP_COUNT;
    if (pass >= SweepPasses)
    {
        // The copies, finest last: the lit shaders read every map at once, so they all change in one frame (the frame
        // that finishes the sweep: WorldFarShadows_BeginFrame).
        uint32_t Copy = pass - SweepPasses;
        Pass.IsCopy = true;
        Pass.Map = (Copy < WORLD_FAR_SHADOW_MAP_COUNT) ? (WORLD_FAR_SHADOW_MAP_COUNT - 1U - Copy) : 0U;
        Pass.Width = (float)WorldFarShadows_GetMapWidth(Pass.Map);
        return Pass;
    }
    // The coarsest map first: each finer one continues its rays in the one swept before it.
    Pass.Map = WORLD_FAR_SHADOW_MAP_COUNT - 1U - (pass / PassesPerMap);
    Pass.Step = pass % PassesPerMap;
    Pass.WritesMapTarget = (((PassesPerMap - 1U - Pass.Step) % 2U) == 0U);
    Pass.Width = (float)WorldFarShadows_GetMapWidth(Pass.Map);
    double Texel = WorldFarShadows_GetTexel(Pass.Map, mapSize);
    Pass.Run = (Pass.Step == 0U) ? (float)GetExclusion(Pass.Map, mapSize)
        : (float)(Texel * ldexp(1.0, (int)Pass.Step - 1));
    Pass.HasCoarser = (Pass.Step == 0U) && ((Pass.Map + 1U) < WORLD_FAR_SHADOW_MAP_COUNT);
    if (Pass.HasCoarser)
    {
        Pass.CoarserWidth = (float)WorldFarShadows_GetMapWidth(Pass.Map + 1U);
        Pass.CoarserExclusion = (float)GetExclusion(Pass.Map + 1U, mapSize);
    }
    return Pass;
}

void WorldFarShadows_BeginFrame(WorldFarShadows* self, const WorldFarShadowsInput* input, WorldFarShadowsWork* outWork)
{
    Memory_Zero(outWork, sizeof(*outWork));
    if (!self->_isSweeping)
    {
        if (AreMapsStale(self, input))
        {
            // Drawn again only between sweeps, so every sweep reads the maps it started with.
            outWork->DrawsMaps = true;
            outWork->DrawFrame = WorldFarShadows_GetFrame(input->Origin, input->MapSize, input->CurvatureScale);
            self->_hasMaps = true;
            self->_mapsFrame = outWork->DrawFrame;
            self->_mapsListingCount = input->TerrainListingCount;
            self->_mapsDrawDistanceScale = input->DrawDistanceScale;
            self->_mapsShadowDistanceScale = input->ShadowDistanceScale;
            self->_isSweepDue = true;
        }
        if (self->_isSweepDue || !self->_hasResult || HasLightTurned(self->_resultTowardLight, input->TowardLight))
        {
            self->_isSweeping = true;
            self->_sweepPassesDone = 0U;
            self->_sweepTowardLight = input->TowardLight;
            self->_sweepDiscRadius = input->DiscRadius;
        }
    }
    if (!self->_isSweeping)
    {
        return;
    }
    uint32_t PassCount = WorldFarShadows_GetSweepPassCount(self->_mapsFrame.MapSize);
    uint32_t FirstCopy = WorldFarShadows_GetPassesPerMap(self->_mapsFrame.MapSize) * WORLD_FAR_SHADOW_MAP_COUNT;
    uint32_t Remaining = PassCount - self->_sweepPassesDone;
    uint32_t Count = (Remaining < WORLD_FAR_SHADOW_PASSES_PER_FRAME) ? Remaining : WORLD_FAR_SHADOW_PASSES_PER_FRAME;
    // A frame that would start the copies without finishing them stops before them: the next one runs them all (the lit
    // shaders read the maps the copies write with the finished sweep's frame, which changes as the sweep finishes).
    uint32_t End = self->_sweepPassesDone + Count;
    if ((self->_sweepPassesDone < FirstCopy) && (End > FirstCopy) && (End < PassCount))
    {
        Count = FirstCopy - self->_sweepPassesDone;
    }
    outWork->FirstPass = self->_sweepPassesDone;
    outWork->PassCount = Count;
    outWork->SweepLight = WorldFarShadows_GetLight(self->_sweepTowardLight, self->_sweepDiscRadius);
    self->_sweepPassesDone += outWork->PassCount;
    if (self->_sweepPassesDone >= PassCount)
    {
        outWork->FinishesSweep = true;
        self->_isSweeping = false;
        self->_isSweepDue = false;
        self->_hasResult = true;
        self->_resultFrame = self->_mapsFrame;
        self->_resultTowardLight = self->_sweepTowardLight;
    }
}

void WorldFarShadows_SkipFrame(WorldFarShadows* self)
{
    // Gone on later, it would sweep for the light it started with, however far that has turned meanwhile.
    self->_isSweeping = false;
}

void WorldFarShadows_Invalidate(WorldFarShadows* self)
{
    Memory_Zero(self, sizeof(*self));
}

float WorldFarShadows_GetEdge(int mapSize)
{
    float Half = (float)((mapSize > 0) ? mapSize : 1) * 0.5f;
    return fmaxf(1.0f - ((float)WORLD_FAR_SHADOW_EDGE_MARGIN_TEXELS / Half), 0.0f);
}

WorldFarShadowLookup WorldFarShadows_GetLookup(const WorldFarShadowFrame* frame, Vector3Double origin,
    Vector3 renderPosition)
{
    // The camera's offset from the frame's centre, subtracted in double and rounded once, as the renderer uploads it.
    Vector3 Offset =
    {
        (float)(origin.X - frame->Center.X), (float)(origin.Y - frame->Center.Y), (float)(origin.Z - frame->Center.Z)
    };
    Vector3 Place = Vector3Add(renderPosition, Offset);
    WorldFarShadowLookup Lookup;
    Lookup.Place = (Vector2){ Place.x, Place.z };
    Lookup.Height = Place.y - (((Place.x * Place.x) + (Place.z * Place.z)) * frame->CurvatureScale);
    float Edge = WorldFarShadows_GetEdge(frame->MapSize);
    float BandStart = Edge * (1.0f - WORLD_FAR_SHADOW_BLEND_SHARE);
    float Reach = fmaxf(fabsf(Place.x), fabsf(Place.z));
    Lookup.Map = WORLD_FAR_SHADOW_MAP_COUNT;
    Lookup.NextWeight = 0.0f;
    for (uint32_t Map = 0U; Map < WORLD_FAR_SHADOW_MAP_COUNT; Map++)
    {
        float Share = Reach / (float)(WorldFarShadows_GetMapWidth(Map) * 0.5);
        if (Share <= Edge)
        {
            Lookup.Map = Map;
            Lookup.NextWeight = fminf(fmaxf((Share - BandStart) / fmaxf(Edge - BandStart, 1.0e-6f), 0.0f), 1.0f);
            break;
        }
    }
    return Lookup;
}

float WorldFarShadows_GetDiscShare(float fraction)
{
    double Clamped = fmin(fmax((double)fraction, 0.0), 1.0);
    double C = 1.0 - (2.0 * Clamped);
    return (float)((acos(C) - (C * sqrt(fmax(1.0 - (C * C), 0.0)))) / FAR_SHADOW_PI);
}

float WorldFarShadows_GetShadow(float height, float topHeight, float bottomHeight)
{
    float Fraction = (height - topHeight) / fmaxf(bottomHeight - topHeight, 1.0e-3f);
    return 1.0f - WorldFarShadows_GetDiscShare(Fraction);
}

void WorldFarShadows_Sweep(const float* heights, const float* coarser, uint32_t map, int mapSize,
    const WorldFarShadowLight* light, float* scratch, float* outShadowHeights)
{
    float Size = (float)mapSize;
    Vector2 Slopes = { light->TopSlope, light->BottomSlope };
    uint32_t PassesPerMap = WorldFarShadows_GetPassesPerMap(mapSize);
    for (uint32_t Step = 0U; Step < PassesPerMap; Step++)
    {
        WorldFarShadowPass Pass = WorldFarShadows_GetPass(((WORLD_FAR_SHADOW_MAP_COUNT - 1U - map) * PassesPerMap) + Step,
            mapSize);
        // The passes take turns between the scratch and the result, as the GPU's between the shared target and the map's.
        float* Target = Pass.WritesMapTarget ? outShadowHeights : scratch;
        const float* Source = Pass.WritesMapTarget ? scratch : outShadowHeights;
        Vector2 Offset = Vector2Scale(light->Toward, Pass.Run / Pass.Width);
        for (int Row = 0; Row < mapSize; Row++)
        {
            for (int Column = 0; Column < mapSize; Column++)
            {
                Vector2 Uv = { ((float)Column + 0.5f) / Size, ((float)Row + 0.5f) / Size };
                Vector2 SampleUv = Vector2Add(Uv, Offset);
                float Top = WORLD_FAR_SHADOW_NO_GROUND;
                float Bottom = WORLD_FAR_SHADOW_NO_GROUND;
                if (IsOnMap(SampleUv))
                {
                    if (Step == 0U)
                    {
                        float Height = SampleOne(heights, mapSize, SampleUv);
                        Top = Height;
                        Bottom = Height;
                    }
                    else
                    {
                        SamplePair(Source, mapSize, SampleUv, &Top, &Bottom);
                    }
                    Top -= Pass.Run * Slopes.x;
                    Bottom -= Pass.Run * Slopes.y;
                }
                if ((Step == 0U) && Pass.HasCoarser && (coarser != NULL))
                {
                    // The ray continues in the coarser map from where it leaves this one (from the exclusion if sooner).
                    float Leave = fmaxf(GetExitRun(Uv, light->Toward) * Pass.Width, Pass.Run);
                    Vector2 Exit = Vector2Add(Uv, Vector2Scale(light->Toward, Leave / Pass.Width));
                    Vector2 CoarserUv = Vector2Add(Vector2Scale(Vector2SubtractValue(Exit, 0.5f),
                        Pass.Width / Pass.CoarserWidth), (Vector2){ 0.5f, 0.5f });
                    CoarserUv = Vector2Subtract(CoarserUv, Vector2Scale(light->Toward,
                        Pass.CoarserExclusion / Pass.CoarserWidth));
                    float CoarserTop = 0.0f;
                    float CoarserBottom = 0.0f;
                    SamplePair(coarser, mapSize, CoarserUv, &CoarserTop, &CoarserBottom);
                    Top = fmaxf(Top, CoarserTop + ((Pass.CoarserExclusion - Leave) * Slopes.x));
                    Bottom = fmaxf(Bottom, CoarserBottom + ((Pass.CoarserExclusion - Leave) * Slopes.y));
                }
                size_t Index = ((size_t)Row * (size_t)mapSize + (size_t)Column) * 2U;
                if (Step > 0U)
                {
                    Top = fmaxf(Top, Source[Index]);
                    Bottom = fmaxf(Bottom, Source[Index + 1U]);
                }
                Target[Index] = Top;
                Target[Index + 1U] = Bottom;
            }
        }
    }
}
