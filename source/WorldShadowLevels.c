// The sun's and the moon's shadow levels as pure maths: their sizes, which level a point reads, what each draws and when
// each renders. See WorldShadowLevels.h.

#include <math.h>
#include "WorldShadowLevels.h"
#include "wr/WRMemory.h"


// Macros.
/** The square root of 2: a box's corner lies this many half sizes from its centre. */
#define SQUARE_ROOT_OF_TWO 1.41421356237309504880
/** The smallest share of a level's half size its lookups may read, whatever the map's size. */
#define MIN_EDGE 0.5f


// Static functions.
/* The fade of the reach's last stretch at a distance: 0 before it, rising linearly to 1 at the reach. */
static float GetReachFadeOut(const WorldShadowLevelsSettings* settings, float distance)
{
    if (!(distance >= 0.0f))
    {
        return 1.0f;
    }
    float Start = WorldShadowLevels_GetReachFadeStart(settings);
    if (distance <= Start)
    {
        return 0.0f;
    }
    if ((distance >= settings->Reach) || !(settings->Reach > Start))
    {
        return 1.0f;
    }
    return (distance - Start) / (settings->Reach - Start);
}

/* Whether two settings give the same maps: the levels' sizes and their depth range (the reach). */
static bool AreMapsAlike(const WorldShadowLevelsSettings* a, const WorldShadowLevelsSettings* b)
{
    return (a->Factor == b->Factor) && (a->MapSize == b->MapSize) && (a->Reach == b->Reach);
}

/* The angle a level's light may turn by before it renders again: WORLD_SHADOW_TURN_TEXELS of its texels at the corner of
 * its box, the same for every level (the texel grows with the box). */
static double GetTurnLimit(const WorldShadowLevelsSettings* settings)
{
    return ((double)WORLD_SHADOW_TURN_TEXELS / (double)settings->MapSize) * (2.0 / SQUARE_ROOT_OF_TWO);
}

/* How fast a box's axes turn about its light, in radians per radian the light turns, at most: the light's own turn and the
 * turn of its up vector's projection, fastest right before the up vector switches (WorldRenderSpace.h). */
static double GetAxesTurnFactor(void)
{
    double StraightUp = (double)WORLD_RENDER_SPACE_CASCADE_STRAIGHT_UP_Y;
    return 1.0 + (1.0 / sqrt(1.0 - (StraightUp * StraightUp)));
}

/* The angle between two directions, in radians: from the cross product's length and the dot product, which stays exact
 * for the small angles the turn's limit compares (an arccosine of the dot product would read a float unit vector's
 * rounding against itself as a turn of about 3e-4 radians). */
static double GetAngle(Vector3 a, Vector3 b)
{
    Vector3Double A = Vector3Double_Create((double)a.x, (double)a.y, (double)a.z);
    Vector3Double B = Vector3Double_Create((double)b.x, (double)b.y, (double)b.z);
    return atan2(Vector3Double_Length(Vector3Double_Cross(A, B)), Vector3Double_Dot(A, B));
}


// Internal functions (declared in WorldShadowLevels.h).
WorldShadowLevelsSettings WorldShadowLevelsSettings_CreateDefault(void)
{
    WorldShadowLevelsSettings Settings =
    {
        .LevelCount = WORLD_SHADOW_DEFAULT_LEVEL_COUNT,
        .Factor = WORLD_SHADOW_DEFAULT_FACTOR,
        .MapSize = WORLD_SHADOW_DEFAULT_MAP_SIZE,
        .Reach = WORLD_SHADOW_DEFAULT_REACH,
        .UpdatePeriods = { 1U, 1U, 2U, 4U, 8U, 16U, 32U, 64U }
    };
    return Settings;
}

WorldShadowLevelsSettings WorldShadowLevelsSettings_Sanitize(const WorldShadowLevelsSettings* settings)
{
    WorldShadowLevelsSettings Settings = *settings;
    if (Settings.LevelCount < 1U)
    {
        Settings.LevelCount = 1U;
    }
    if (Settings.LevelCount > WORLD_SHADOW_MAX_LEVELS)
    {
        Settings.LevelCount = WORLD_SHADOW_MAX_LEVELS;
    }
    if (!isfinite(Settings.Factor) || !(Settings.Factor > 1.0f))
    {
        Settings.Factor = WORLD_SHADOW_DEFAULT_FACTOR;
    }
    if (Settings.MapSize <= 0)
    {
        Settings.MapSize = WORLD_SHADOW_DEFAULT_MAP_SIZE;
    }
    if (!isfinite(Settings.Reach) || !(Settings.Reach >= 0.0f))
    {
        Settings.Reach = 0.0f;
    }
    for (uint32_t Level = 0U; Level < WORLD_SHADOW_MAX_LEVELS; Level++)
    {
        if (Settings.UpdatePeriods[Level] < 1U)
        {
            Settings.UpdatePeriods[Level] = 1U;
        }
    }
    return Settings;
}

float WorldShadowLevels_GetSize(const WorldShadowLevelsSettings* settings, uint32_t level)
{
    double Size = (double)WORLD_SHADOW_BASE_SIZE;
    for (uint32_t Index = 0U; Index < level; Index++)
    {
        Size *= (double)settings->Factor;
    }
    return (float)Size;
}

float WorldShadowLevels_GetTexel(const WorldShadowLevelsSettings* settings, uint32_t level)
{
    return WorldShadowLevels_GetSize(settings, level) / (float)settings->MapSize;
}

float WorldShadowLevels_GetEdge(const WorldShadowLevelsSettings* settings)
{
    float Edge = 1.0f - ((2.0f * WORLD_SHADOW_MARGIN_TEXELS) / (float)settings->MapSize);
    return fmaxf(Edge, MIN_EDGE);
}

uint32_t WorldShadowLevels_GetUsedCount(const WorldShadowLevelsSettings* settings)
{
    if (!(settings->Reach > 0.0f))
    {
        return 0U;
    }
    float Edge = WorldShadowLevels_GetEdge(settings);
    for (uint32_t Count = 1U; Count < settings->LevelCount; Count++)
    {
        float Usable = WorldShadowLevels_GetSize(settings, Count - 1U) * 0.5f * Edge;
        if (Usable >= settings->Reach)
        {
            return Count;
        }
    }
    return settings->LevelCount;
}

float WorldShadowLevels_GetReachFadeStart(const WorldShadowLevelsSettings* settings)
{
    float Band = fminf(fmaxf(settings->Reach * WORLD_SHADOW_REACH_FADE_SHARE, WORLD_SHADOW_MIN_REACH_FADE),
        settings->Reach);
    return settings->Reach - Band;
}

float WorldShadowLevels_GetDepthHalfRange(const WorldShadowLevelsSettings* settings)
{
    return fmaxf(settings->Reach * WORLD_SHADOW_DEPTH_REACH_SCALE, WORLD_SHADOW_MIN_DEPTH_HALF_RANGE);
}

WorldRenderCascadeShape WorldShadowLevels_GetShape(const WorldShadowLevelsSettings* settings, uint32_t level)
{
    // The light's eye stays close to the box's centre (a far eye's float view matrix would carry a large translation
    // and round each frame's rasterization differently, flickering thin casters' texels); the box reaches behind it.
    float HalfRange = WorldShadowLevels_GetDepthHalfRange(settings);
    WorldRenderCascadeShape Shape =
    {
        .Size = WorldShadowLevels_GetSize(settings, level),
        .LightDistance = WORLD_SHADOW_EYE_DISTANCE,
        .NearPlane = WORLD_SHADOW_EYE_DISTANCE - HalfRange,
        .FarPlane = WORLD_SHADOW_EYE_DISTANCE + HalfRange,
        .MapSize = settings->MapSize
    };
    return Shape;
}

WorldShadowLookup WorldShadowLevels_GetLookup(const WorldShadowLevelsSettings* settings, uint32_t usedCount,
    float boxDistance, float distance)
{
    WorldShadowLookup Lookup = { .IsInside = false, .Level = 0U, .HasNext = false, .NextWeight = 0.0f,
        .ReachFade = 0.0f };
    if ((usedCount == 0U) || !(boxDistance >= 0.0f))
    {
        return Lookup;
    }
    Lookup.ReachFade = 1.0f - GetReachFadeOut(settings, distance);
    if (!(Lookup.ReachFade > 0.0f))
    {
        return Lookup;
    }
    float Edge = WorldShadowLevels_GetEdge(settings);
    for (uint32_t Level = 0U; Level < usedCount; Level++)
    {
        float Usable = WorldShadowLevels_GetSize(settings, Level) * 0.5f * Edge;
        if (!(boxDistance <= Usable))
        {
            continue;
        }
        float BandStart = Usable * (1.0f - WORLD_SHADOW_BLEND_SHARE);
        float Weight = (boxDistance - BandStart) / (Usable * WORLD_SHADOW_BLEND_SHARE);
        Lookup.IsInside = true;
        Lookup.Level = Level;
        Lookup.NextWeight = fminf(fmaxf(Weight, 0.0f), 1.0f);
        Lookup.HasNext = (Lookup.NextWeight > 0.0f) && ((Level + 1U) < usedCount);
        return Lookup;
    }
    return Lookup;
}

float WorldShadowLevels_GetMinRadius(const WorldShadowLevelsSettings* settings, uint32_t level)
{
    return 0.5f * WORLD_SHADOW_SIZE_CUT_TEXELS * WorldShadowLevels_GetTexel(settings, level);
}

bool WorldShadowLevels_IsLargeEnough(const WorldShadowLevelsSettings* settings, uint32_t level, float radius)
{
    return radius >= WorldShadowLevels_GetMinRadius(settings, level);
}

float WorldShadowLevels_GetLodDistance(const WorldShadowLevelsSettings* settings, uint32_t level)
{
    if (level == 0U)
    {
        return 0.0f;
    }
    float Usable = WorldShadowLevels_GetSize(settings, level - 1U) * 0.5f * WorldShadowLevels_GetEdge(settings);
    return Usable * (1.0f - WORLD_SHADOW_BLEND_SHARE);
}

float WorldShadowLevels_GetInnerInset(const WorldShadowLevelsSettings* settings, uint32_t level)
{
    if (level == 0U)
    {
        return 0.0f;
    }
    float Below = WorldShadowLevels_GetSize(settings, level - 1U) * 0.5f;
    float Half = WorldShadowLevels_GetSize(settings, level) * 0.5f;
    float Band = Below * (1.0f - (WorldShadowLevels_GetEdge(settings) * (1.0f - WORLD_SHADOW_BLEND_SHARE)));
    float Moves = WORLD_SHADOW_MOVE_SHARE * (Half + (2.0f * Below));
    float Snaps = 2.0f * WorldShadowLevels_GetTexel(settings, level - 1U);
    float Reads = WORLD_SHADOW_INNER_READ_TEXELS * WorldShadowLevels_GetTexel(settings, level);
    // While the level keeps its map the level before's light may move three turn limits from where it was when the
    // level was drawn (each within a limit of the light of its frame), and the level's own two from that one's. The first
    // carries a left-out caster's place in the level before's box: over its depth (within the depth half range) at the
    // turn, over its place in the box (half a diagonal, its radius within the box's half size) at the box's axes' turn;
    // the second carries the line through the caster that the level's map holds: over the depth from the caster to what
    // it shades (both within the reach, the camera's moves between).
    double Depth = (double)WorldShadowLevels_GetDepthHalfRange(settings);
    double Inside = (double)Below;
    double Turns = GetTurnLimit(settings) * ((3.0 * (Depth + Inside))
        + (3.0 * SQUARE_ROOT_OF_TWO * GetAxesTurnFactor() * Inside) + (2.0 * ((2.0 * Depth) + Inside + (double)Moves)));
    return Band + Moves + Snaps + Reads + (float)Turns;
}

uint32_t WorldShadowLevels_GetPhase(uint32_t period)
{
    if (period < 2U)
    {
        return 0U;
    }
    return ((period / 2U) - 1U) % period;
}

void WorldShadowLevels_Construct(WorldShadowLevels* self)
{
    Memory_Zero(self, sizeof(*self));
}

void WorldShadowLevels_Deconstruct(WorldShadowLevels* self)
{
    if (self == NULL)
    {
        return;
    }
    Memory_Zero(self, sizeof(*self));
}

void WorldShadowLevels_Invalidate(WorldShadowLevels* self)
{
    for (uint32_t Level = 0U; Level < WORLD_SHADOW_MAX_LEVELS; Level++)
    {
        self->_levels[Level].HasMap = false;
    }
}

uint32_t WorldShadowLevels_BeginFrame(WorldShadowLevels* self, const WorldShadowLevelsSettings* settings,
    Vector3Double origin, Vector3 towardLight, WorldShadowLevelFrame outLevels[WORLD_SHADOW_MAX_LEVELS])
{
    if (!self->_hasSettings || !AreMapsAlike(&self->_settings, settings))
    {
        WorldShadowLevels_Invalidate(self);
    }
    self->_settings = *settings;
    self->_hasSettings = true;

    uint32_t UsedCount = WorldShadowLevels_GetUsedCount(settings);
    double TurnLimit = GetTurnLimit(settings);
    for (uint32_t Level = 0U; Level < WORLD_SHADOW_MAX_LEVELS; Level++)
    {
        WorldShadowLevelFrame* Frame = &outLevels[Level];
        Memory_Zero(Frame, sizeof(*Frame));
        WorldShadowLevelState* State = &self->_levels[Level];
        if (Level >= UsedCount)
        {
            State->HasMap = false;
            continue;
        }

        Frame->IsUsed = true;
        Frame->Shape = WorldShadowLevels_GetShape(settings, Level);
        Frame->Size = Frame->Shape.Size;
        Frame->Texel = WorldShadowLevels_GetTexel(settings, Level);

        uint32_t Period = (settings->UpdatePeriods[Level] < 1U) ? 1U : settings->UpdatePeriods[Level];
        bool IsRendered = !State->HasMap || ((self->_frame % Period) == WorldShadowLevels_GetPhase(Period));
        // The light turned past the limit, or across the up vector's switch (every box turns about the light at once).
        if (!IsRendered && ((GetAngle(State->TowardLight, towardLight) > TurnLimit)
            || !WorldRenderSpace_HasSameCascadeUp(State->TowardLight, towardLight)))
        {
            IsRendered = true;
        }
        if (!IsRendered && (Vector3Double_Distance(origin, State->RenderOrigin)
            > ((double)WORLD_SHADOW_MOVE_SHARE * 0.5 * (double)Frame->Size)))
        {
            IsRendered = true;
        }

        if (IsRendered)
        {
            Frame->Cascade = WorldRenderSpace_StepCascade(&State->Grid, origin, towardLight, &Frame->Shape);
            State->TowardLight = towardLight;
            State->RenderOrigin = origin;
            State->HasMap = true;
        }
        else
        {
            Frame->Cascade = WorldRenderSpace_GetCascadeAt(origin, State->Grid.Anchor, State->TowardLight,
                &Frame->Shape);
        }
        Frame->IsRendered = IsRendered;
        Frame->TowardLight = State->TowardLight;
    }
    self->_frame++;
    return UsedCount;
}
