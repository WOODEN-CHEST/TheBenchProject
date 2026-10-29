#include "WorldInterior.h"
#include <math.h>
#include "GameMath.h"
#include "raylib/raymath.h"


// Macros.
/** sRGB gamma of a zone's stored colour (as the renderer converts a light's). */
#define INTERIOR_SRGB_GAMMA 2.2f
/** A line of sight's direction component below this counts as this, so no slab divides by zero (the shaders do the
 *  same). */
#define INTERIOR_MIN_DIRECTION 1e-8f
/** Below this a world matrix's axis counts as collapsed. */
#define INTERIOR_MIN_AXIS_LENGTH 1e-12
/** The tangent of an elevation beyond every band's: a band's blockers before any is met (the shader's
 *  INTERIOR_SKY_NO_BLOCKER). */
#define INTERIOR_NO_BLOCKER 1e30f


// Fields.
/* The pyramid's level each step of a surface's march reads (shaders/common/interior.glsl's INTERIOR_SKY_STEP_LEVELS,
 * pinned by tests/ShaderSourceTests.c): the finest level whose texels span the step from the last (no wall between two
 * steps is skipped), never so coarse that a texel at the step can reach back to the surface's own column. */


// Static functions.
/* A slice mask with no slice set. */
static void ClearSliceMask(uint32_t* mask)
{
    for (int Word = 0; Word < WORLD_INTERIOR_SKY_SLICE_WORDS; Word++)
    {
        mask[Word] = 0U;
    }
}

/* Sets the slices from first to last (inclusive, each within the column) in a mask. */
static void SetSliceRun(int first, int last, uint32_t* mask)
{
    for (int Word = 0; Word < WORLD_INTERIOR_SKY_SLICE_WORDS; Word++)
    {
        int Low = (first > (32 * Word)) ? (first - (32 * Word)) : 0;
        int High = (last < ((32 * Word) + 31)) ? (last - (32 * Word)) : 31;
        if (Low <= High)
        {
            uint32_t Count = (uint32_t)(High - Low + 1);
            uint32_t Bits = (Count == 32U) ? 0xFFFFFFFFU : ((1U << Count) - 1U);
            mask[Word] |= Bits << (uint32_t)Low;
        }
    }
}

/* The lowest and the highest slice two masks share, as a column's solid slices within a band's; false when they share
 * none. */
static bool FindSolidSlices(const uint32_t* column, const uint32_t* mask, int* outFirst, int* outLast)
{
    int First = -1;
    int Last = -1;
    for (int Word = 0; Word < WORLD_INTERIOR_SKY_SLICE_WORDS; Word++)
    {
        uint32_t Shared = column[Word] & mask[Word];
        for (int Bit = 0; (Shared != 0U) && (Bit < 32); Bit++)
        {
            if ((Shared & (1U << (uint32_t)Bit)) != 0U)
            {
                First = (First < 0) ? ((32 * Word) + Bit) : First;
                Last = (32 * Word) + Bit;
            }
        }
    }
    *outFirst = First;
    *outLast = Last;
    return First >= 0;
}

/* Where a render-space point (x, z) lands on the map, in [0, 1) across it (linear in the point), as the shaders find
 * it. */
static Vector2 GetMapPlace(const WorldInteriorSkyMap* map, float x, float z)
{
    Vector3 Clip = Vector3Transform((Vector3){ x, 0.0f, z }, map->ViewProjection);
    return (Vector2){ (Clip.x * 0.5f) + 0.5f, (Clip.y * 0.5f) + 0.5f };
}

/* Whether a slice mask has no slice set. */
static bool IsMaskEmpty(const uint32_t* bits)
{
    return (bits[0] | bits[1] | bits[2] | bits[3]) == 0U;
}

/* The lowest slice set in a mask that has one; and the mask without it. */
static int TakeLowestSlice(uint32_t* bits)
{
    int Word = (bits[0] != 0U) ? 0 : ((bits[1] != 0U) ? 1 : ((bits[2] != 0U) ? 2 : 3));
    int Bit = 0;
    while ((bits[Word] & (1U << (uint32_t)Bit)) == 0U)
    {
        Bit++;
    }
    bits[Word] &= bits[Word] - 1U;
    return (32 * Word) + Bit;
}

/* The sine of the elevation whose tangent is @p tangent. */
static float GetSineOfTangent(float tangent)
{
    return tangent / sqrtf(1.0f + (tangent * tangent));
}

/* The share of a band of elevations (by their tangents, from low to high) left open around the tangents its blockers
 * span (lowest to highest; none while lowest is above highest), by solid angle: what lies below them and above them. */
static float GetBandOpenShare(float low, float high, float lowest, float highest)
{
    if (!(lowest <= highest))
    {
        return 1.0f;
    }
    float SineLow = GetSineOfTangent(low);
    float SineHigh = GetSineOfTangent(high);
    float BlockedLow = GetSineOfTangent(fminf(fmaxf(lowest, low), high));
    float BlockedHigh = GetSineOfTangent(fminf(fmaxf(highest, low), high));
    return ((BlockedLow - SineLow) + (SineHigh - BlockedHigh)) / (SineHigh - SineLow);
}

/* The lowest and the highest band in a set of bands (a non-empty one). */
static void GetBandRange(unsigned int bands, int* outLowest, int* outHighest)
{
    int Lowest = WORLD_INTERIOR_SKY_BANDS;
    int Highest = -1;
    for (int Band = 0; Band < WORLD_INTERIOR_SKY_BANDS; Band++)
    {
        if ((bands & (1U << Band)) != 0U)
        {
            Lowest = (Band < Lowest) ? Band : Lowest;
            Highest = Band;
        }
    }
    *outLowest = Lowest;
    *outHighest = Highest;
}

/* Whether a column's slice is solid: none below the window, every one above it (a column solid at the window's top goes on
 * up, so this is asked only of the slice above a solid one). */
static bool IsSliceSolid(const uint32_t* column, int slice)
{
    if (slice >= WORLD_INTERIOR_SKY_SLICE_COUNT)
    {
        return true;
    }
    return (slice >= 0) && ((column[slice / 32] & (1U << (uint32_t)(slice % 32))) != 0U);
}

/* Whether the solid slices found in a column up to the last are only the top of what the surface stands over: solid in
 * its own slice alone, below it too, not above it (the ground at a wall's foot shares the foot's slice; a slab within
 * the slice, air below it, is no such top: a ceiling just above). */
static bool IsUnderfoot(const uint32_t* column, int last, int ownSlice)
{
    return (last == ownSlice) && !IsSliceSolid(column, last + 1) && IsSliceSolid(column, last - 1);
}

/* The lift the bands start at above a surface: between the upright and the facing-up one by its normal (mix as GLSL
 * defines it: a(1 - t) + bt). */
static float GetBandBase(Vector3 position, Vector3 normal)
{
    float Up = fminf(fmaxf(normal.y, 0.0f), 1.0f);
    return position.y + (WORLD_INTERIOR_SKY_UPRIGHT_LIFT * (1.0f - Up)) + (WORLD_INTERIOR_SKY_LIFT * Up);
}

/* The slice a height lies in. */
static int GetOwnSlice(float height, float windowBottom)
{
    return (int)floorf((height - windowBottom) / (float)WORLD_INTERIOR_SKY_SLICE_HEIGHT);
}

/* Whether nothing the volume holds lies straight above a surface, its bands' base and its own slice given: read in its
 * own column (an upright surface's a texel and a half in front of it, not in its own wall's; less so as it turns down,
 * so what lies above an underside covers it), the top of what it stands over aside. */
static bool IsOpenOverhead(WorldInteriorColumnFunction columns, const void* context, const WorldInteriorSkyMap* map,
    float windowBottom, Vector3 position, Vector3 normal, float base, int ownSlice)
{
    float Texel = (float)(WORLD_INTERIOR_SKY_MAP_EXTENT / (double)WORLD_INTERIOR_SKY_MAP_RESOLUTION);
    float Ahead = 1.5f * Texel * fminf(fmaxf(1.0f + normal.y, 0.0f), 1.0f);
    int X = 0;
    int Y = 0;
    if (!WorldInterior_GetSkyTexel(map, position.x + (normal.x * Ahead), position.z + (normal.z * Ahead), 0, &X, &Y))
    {
        return true; // off the map: nothing known, open
    }
    uint32_t Column[WORLD_INTERIOR_SKY_SLICE_WORDS];
    uint32_t Mask[WORLD_INTERIOR_SKY_SLICE_WORDS];
    columns(context, 0, X, Y, Column);
    WorldInterior_GetSliceRangeMask(base, INFINITY, windowBottom, Mask);
    int First = 0;
    int Last = 0;
    return !FindSolidSlices(Column, Mask, &First, &Last) || IsUnderfoot(Column, Last, ownSlice);
}

/* Rec. 709 luminance of linear RGB. */
static float Luminance(const float rgb[3])
{
    return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
}

static float Dot(Vector3 a, Vector3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/* A point in a zone's own frame: its offset from the centre along each of the zone's axes. */
static Vector3 ToZoneFrame(const WorldInteriorZone* zone, Vector3 offset)
{
    return (Vector3){ Dot(offset, zone->Axes[0]), Dot(offset, zone->Axes[1]), Dot(offset, zone->Axes[2]) };
}

/* x unless its size is below INTERIOR_MIN_DIRECTION, which keeps its sign (0 counts as positive). */
static float AwayFromZero(float x)
{
    if (fabsf(x) >= INTERIOR_MIN_DIRECTION)
    {
        return x;
    }
    return (x < 0.0f) ? -INTERIOR_MIN_DIRECTION : INTERIOR_MIN_DIRECTION;
}

/* The stretch of t from the line's start to its end ([tNear, tFar] clipped to [0, distance]). */
static float GetInsideLength(float tNear, float tFar, float distance)
{
    float Inside = fminf(tFar, distance) - fmaxf(tNear, 0.0f);
    return (Inside > 0.0f) ? Inside : 0.0f;
}

/* Whether candidate a beats b for a draw: the higher priority, then the nearer, then the first listed. */
static bool IsBetterZone(int32_t priorityA, float distanceA, size_t indexA, int32_t priorityB, float distanceB,
    size_t indexB)
{
    if (priorityA != priorityB)
    {
        return priorityA > priorityB;
    }
    if (distanceA != distanceB)
    {
        return distanceA < distanceB;
    }
    return indexA < indexB;
}

/* The share of the step a frame of @p deltaSeconds takes (0 for no time). */
static float GetEaseShare(float deltaSeconds)
{
    if (!(deltaSeconds > 0.0f))
    {
        return 0.0f;
    }
    float Seconds = fminf(deltaSeconds, WORLD_INTERIOR_CAMERA_MAX_DELTA_SECONDS);
    return 1.0f - expf(-WORLD_INTERIOR_CAMERA_EASE_RATE * Seconds);
}

static float Ease(float from, float to, float share)
{
    return from + (to - from) * share;
}


// Internal functions (declared in WorldInterior.h): zones.
WorldInteriorZone WorldInterior_MakeZone(const WorldAmbientZoneComponent* component, const Matrix4Double* worldMatrix,
    Vector3Double origin)
{
    WorldInteriorZone Zone = { 0 };
    Zone.Shape = component->_shape;
    Zone.Center = GameMath_ToVector3Relative(Matrix4Double_GetTranslation(worldMatrix), origin);
    Zone.Axes[0] = (Vector3){ 1.0f, 0.0f, 0.0f };
    Zone.Axes[1] = (Vector3){ 0.0f, 1.0f, 0.0f };
    Zone.Axes[2] = (Vector3){ 0.0f, 0.0f, 1.0f };
    Zone.BlendDistance = component->_blendDistance;
    float Linear[3] =
    {
        powf((float)component->_ambientColor.r / 255.0f, INTERIOR_SRGB_GAMMA),
        powf((float)component->_ambientColor.g / 255.0f, INTERIOR_SRGB_GAMMA),
        powf((float)component->_ambientColor.b / 255.0f, INTERIOR_SRGB_GAMMA)
    };
    for (int Channel = 0; Channel < 3; Channel++)
    {
        Zone.Ambient[Channel] = Linear[Channel] * component->_ambientIntensity;
    }
    Zone.SkyAmbientScale = component->_skyAmbientScale;
    Zone.ExposureBias = component->_exposureBias;
    Zone.Haze = component->_haze;
    Zone.Priority = component->_priority;

    // The matrix's columns are the object's axes times its scale: their lengths scale the zone, their directions turn
    // it (made square in order, so a parent's shear leaves the nearest box).
    double Columns[3][3];
    double Lengths[3];
    for (int Column = 0; Column < 3; Column++)
    {
        for (int Row = 0; Row < 3; Row++)
        {
            Columns[Column][Row] = worldMatrix->M[Column * 4 + Row];
        }
        Lengths[Column] = sqrt(Columns[Column][0] * Columns[Column][0] + Columns[Column][1] * Columns[Column][1]
            + Columns[Column][2] * Columns[Column][2]);
    }
    if (!(Lengths[0] > INTERIOR_MIN_AXIS_LENGTH) || !(Lengths[1] > INTERIOR_MIN_AXIS_LENGTH)
        || !(Lengths[2] > INTERIOR_MIN_AXIS_LENGTH) || !isfinite(Lengths[0] + Lengths[1] + Lengths[2]))
    {
        return Zone; // a collapsed axis: no size, so no point is ever inside
    }
    double Axes[3][3];
    for (int Axis = 0; Axis < 3; Axis++)
    {
        double Vector[3] = { Columns[Axis][0], Columns[Axis][1], Columns[Axis][2] };
        for (int Earlier = 0; Earlier < Axis; Earlier++)
        {
            double Along = Vector[0] * Axes[Earlier][0] + Vector[1] * Axes[Earlier][1] + Vector[2] * Axes[Earlier][2];
            for (int Row = 0; Row < 3; Row++)
            {
                Vector[Row] -= Along * Axes[Earlier][Row];
            }
        }
        double Length = sqrt(Vector[0] * Vector[0] + Vector[1] * Vector[1] + Vector[2] * Vector[2]);
        if (!(Length > INTERIOR_MIN_AXIS_LENGTH * Lengths[Axis]))
        {
            return Zone; // two axes lie on one line: as collapsed
        }
        for (int Row = 0; Row < 3; Row++)
        {
            Axes[Axis][Row] = Vector[Row] / Length;
        }
        Zone.Axes[Axis] = (Vector3){ (float)Axes[Axis][0], (float)Axes[Axis][1], (float)Axes[Axis][2] };
    }
    Zone.HalfExtents = (Vector3)
    {
        (float)((double)component->_halfExtents.x * Lengths[0]),
        (float)((double)component->_halfExtents.y * Lengths[1]),
        (float)((double)component->_halfExtents.z * Lengths[2])
    };
    double LongestScale = fmax(Lengths[0], fmax(Lengths[1], Lengths[2]));
    Zone.Radius = (float)((double)component->_radius * LongestScale);
    Zone.BoundsRadius = (Zone.Shape == WorldAmbientZoneShape_Sphere) ? Zone.Radius
        : sqrtf(Dot(Zone.HalfExtents, Zone.HalfExtents));
    return Zone;
}

bool WorldInterior_HasInside(const WorldInteriorZone* zone)
{
    if (zone->Shape == WorldAmbientZoneShape_Sphere)
    {
        return zone->Radius > 0.0f;
    }
    return (zone->HalfExtents.x > 0.0f) && (zone->HalfExtents.y > 0.0f) && (zone->HalfExtents.z > 0.0f);
}

float WorldInterior_GetZoneDistance(const WorldInteriorZone* zone, Vector3 point)
{
    Vector3 Offset = Vector3Subtract(point, zone->Center);
    if (zone->Shape == WorldAmbientZoneShape_Sphere)
    {
        return sqrtf(Dot(Offset, Offset)) - zone->Radius;
    }
    Vector3 Local = ToZoneFrame(zone, Offset);
    Vector3 Beyond =
    {
        fabsf(Local.x) - zone->HalfExtents.x, fabsf(Local.y) - zone->HalfExtents.y, fabsf(Local.z) - zone->HalfExtents.z
    };
    Vector3 Outside = { fmaxf(Beyond.x, 0.0f), fmaxf(Beyond.y, 0.0f), fmaxf(Beyond.z, 0.0f) };
    float Inside = fminf(fmaxf(Beyond.x, fmaxf(Beyond.y, Beyond.z)), 0.0f);
    return sqrtf(Dot(Outside, Outside)) + Inside;
}

Vector3 WorldInterior_GetZoneInward(const WorldInteriorZone* zone, Vector3 point)
{
    Vector3 Offset = Vector3Subtract(point, zone->Center);
    if (zone->Shape == WorldAmbientZoneShape_Sphere)
    {
        float Length = sqrtf(Dot(Offset, Offset));
        return (Length > 0.0f) ? Vector3Scale(Offset, -1.0f / Length) : (Vector3){ 0.0f, 0.0f, 0.0f };
    }
    Vector3 Local = ToZoneFrame(zone, Offset);
    float Beyond[3] =
    {
        fabsf(Local.x) - zone->HalfExtents.x, fabsf(Local.y) - zone->HalfExtents.y, fabsf(Local.z) - zone->HalfExtents.z
    };
    float Sides[3] = { Local.x, Local.y, Local.z };
    // The nearest face: the axis the point lies furthest out along (the first on a tie).
    int Axis = (Beyond[1] > Beyond[0]) ? 1 : 0;
    Axis = (Beyond[2] > Beyond[Axis]) ? 2 : Axis;
    float Away = (Sides[Axis] > 0.0f) ? -1.0f : ((Sides[Axis] < 0.0f) ? 1.0f : 0.0f);
    return Vector3Scale(zone->Axes[Axis], Away);
}

float WorldInterior_GetZoneWeight(const WorldInteriorZone* zone, Vector3 point, Vector3 normal)
{
    float Distance = WorldInterior_GetZoneDistance(zone, point);
    if (!(Distance < WORLD_INTERIOR_ZONE_SURFACE_TOLERANCE))
    {
        return 0.0f; // beyond the surface (or a broken number)
    }
    float Fade = (zone->BlendDistance > 0.0f) ? fminf(fmaxf(-Distance / zone->BlendDistance, 0.0f), 1.0f)
        : ((Distance < 0.0f) ? 1.0f : 0.0f);
    float Facing = fminf(fmaxf(Dot(normal, WorldInterior_GetZoneInward(zone, point)), 0.0f), 1.0f);
    return fmaxf(Fade, Facing);
}

size_t WorldInterior_SelectZones(const WorldInteriorZone* zones, size_t zoneCount, Vector3 center, float radius,
    size_t* outSelected)
{
    size_t Kept[WORLD_INTERIOR_MAX_DRAW_ZONES];
    float KeptDistance[WORLD_INTERIOR_MAX_DRAW_ZONES];
    size_t KeptCount = 0U;
    for (size_t Index = 0U; Index < zoneCount; Index++)
    {
        const WorldInteriorZone* Zone = &zones[Index];
        Vector3 Offset = Vector3Subtract(center, Zone->Center);
        float Reach = radius + Zone->BoundsRadius;
        if (!(Dot(Offset, Offset) <= Reach * Reach))
        {
            continue; // the spheres do not touch
        }
        float Distance = WorldInterior_GetZoneDistance(Zone, center);
        if (!(Distance < radius))
        {
            continue; // the zone's inside does not reach the draw's sphere
        }
        // Into the kept set when it is not full, else in place of the worst kept when it beats it.
        size_t Slot = KeptCount;
        if (KeptCount == WORLD_INTERIOR_MAX_DRAW_ZONES)
        {
            size_t Worst = 0U;
            for (size_t Position = 1U; Position < KeptCount; Position++)
            {
                if (IsBetterZone(zones[Kept[Worst]].Priority, KeptDistance[Worst], Kept[Worst],
                    zones[Kept[Position]].Priority, KeptDistance[Position], Kept[Position]))
                {
                    Worst = Position;
                }
            }
            if (!IsBetterZone(Zone->Priority, Distance, Index, zones[Kept[Worst]].Priority, KeptDistance[Worst],
                Kept[Worst]))
            {
                continue;
            }
            Slot = Worst;
        }
        else
        {
            KeptCount++;
        }
        Kept[Slot] = Index;
        KeptDistance[Slot] = Distance;
    }

    // The order the shaders lay them in: priority ascending, equal ones by their index (insertion sort of at most 4).
    for (size_t Position = 0U; Position < KeptCount; Position++)
    {
        size_t Current = Kept[Position];
        size_t Insert = Position;
        while ((Insert > 0U) && ((zones[outSelected[Insert - 1U]].Priority > zones[Current].Priority)
            || ((zones[outSelected[Insert - 1U]].Priority == zones[Current].Priority)
                && (outSelected[Insert - 1U] > Current))))
        {
            outSelected[Insert] = outSelected[Insert - 1U];
            Insert--;
        }
        outSelected[Insert] = Current;
    }
    return KeptCount;
}

WorldInteriorZoneLight WorldInterior_ComposeZones(const WorldInteriorZone* zones, const size_t* order, size_t count,
    Vector3 point, Vector3 normal)
{
    WorldInteriorZoneLight Light = { .Ambient = { 0.0f, 0.0f, 0.0f }, .SkyAmbientScale = 1.0f, .ExposureBias = 0.0f,
        .Weight = 0.0f };
    float Uncovered = 1.0f;
    size_t First = 0U;
    while (First < count)
    {
        int32_t Priority = zones[order[First]].Priority;
        float GroupWeight = 0.0f;
        float WeightSum = 0.0f;
        float Ambient[3] = { 0.0f, 0.0f, 0.0f };
        float SkyScale = 0.0f;
        float Bias = 0.0f;
        size_t Next = First;
        for (; (Next < count) && (zones[order[Next]].Priority == Priority); Next++)
        {
            const WorldInteriorZone* Zone = &zones[order[Next]];
            float Weight = WorldInterior_GetZoneWeight(Zone, point, normal);
            GroupWeight = fmaxf(GroupWeight, Weight);
            WeightSum += Weight;
            for (int Channel = 0; Channel < 3; Channel++)
            {
                Ambient[Channel] += Weight * Zone->Ambient[Channel];
            }
            SkyScale += Weight * Zone->SkyAmbientScale;
            Bias += Weight * Zone->ExposureBias;
        }
        if (WeightSum > 0.0f)
        {
            // Over what lies under it by its strongest member's weight (mix as GLSL defines it: a(1 - t) + bt).
            float Keep = 1.0f - GroupWeight;
            for (int Channel = 0; Channel < 3; Channel++)
            {
                Light.Ambient[Channel] = Light.Ambient[Channel] * Keep + (Ambient[Channel] / WeightSum) * GroupWeight;
            }
            Light.SkyAmbientScale = Light.SkyAmbientScale * Keep + (SkyScale / WeightSum) * GroupWeight;
            Light.ExposureBias = Light.ExposureBias * Keep + (Bias / WeightSum) * GroupWeight;
            Uncovered *= Keep;
        }
        First = Next;
    }
    Light.Weight = 1.0f - Uncovered;
    return Light;
}

float WorldInterior_GetZonePathLength(const WorldInteriorZone* zone, Vector3 direction, float distance)
{
    if (!(distance > 0.0f))
    {
        return 0.0f;
    }
    // The camera sits at render space's origin: the line starts at -centre in the zone's frame.
    Vector3 Start = Vector3Negate(zone->Center);
    if (zone->Shape == WorldAmbientZoneShape_Sphere)
    {
        float Along = Dot(Start, direction);
        float Discriminant = Along * Along - (Dot(Start, Start) - zone->Radius * zone->Radius);
        if (!(Discriminant > 0.0f))
        {
            return 0.0f;
        }
        float Root = sqrtf(Discriminant);
        return GetInsideLength(-Along - Root, -Along + Root, distance);
    }
    Vector3 LocalStart = ToZoneFrame(zone, Start);
    Vector3 LocalDirection = ToZoneFrame(zone, direction);
    float Starts[3] = { LocalStart.x, LocalStart.y, LocalStart.z };
    float Directions[3] = { LocalDirection.x, LocalDirection.y, LocalDirection.z };
    float Halves[3] = { zone->HalfExtents.x, zone->HalfExtents.y, zone->HalfExtents.z };
    float Near = -INFINITY;
    float Far = INFINITY;
    for (int Axis = 0; Axis < 3; Axis++)
    {
        float Inverse = 1.0f / AwayFromZero(Directions[Axis]);
        float A = (-Halves[Axis] - Starts[Axis]) * Inverse;
        float B = (Halves[Axis] - Starts[Axis]) * Inverse;
        Near = fmaxf(Near, fminf(A, B));
        Far = fminf(Far, fmaxf(A, B));
    }
    return GetInsideLength(Near, Far, distance);
}

float WorldInterior_GetHazeDistance(const WorldInteriorZone* zones, const size_t* order, size_t count, Vector3 point)
{
    float Distance = sqrtf(Dot(point, point));
    if (!(Distance > 0.0f))
    {
        return 0.0f;
    }
    Vector3 Direction = Vector3Scale(point, 1.0f / Distance);
    float Hazed = Distance;
    for (size_t Position = 0U; Position < count; Position++)
    {
        const WorldInteriorZone* Zone = &zones[order[Position]];
        Hazed -= WorldInterior_GetZonePathLength(Zone, Direction, Distance) * (1.0f - Zone->Haze);
    }
    return fmaxf(Hazed, 0.0f);
}


// Internal functions (declared in WorldInterior.h): the sky's visibility.
WorldInteriorSkyMap WorldInterior_GetSkyMap(Vector3Double origin)
{
    const double Texel = WORLD_INTERIOR_SKY_MAP_EXTENT / (double)WORLD_INTERIOR_SKY_MAP_RESOLUTION;
    WorldInteriorSkyMap Map;
    Map.Center = (Vector3Double){ round(origin.X / Texel) * Texel, origin.Y, round(origin.Z / Texel) * Texel };
    // In render space: the centre a fraction of a texel from the camera, the eye half the depth above it.
    Vector3 Center = GameMath_ToVector3Relative(Map.Center, origin);
    float HalfDepth = (float)(WORLD_INTERIOR_SKY_MAP_DEPTH * 0.5);
    Vector3 Eye = { Center.x, HalfDepth, Center.z };
    Vector3 Below = { Center.x, HalfDepth - 1.0f, Center.z };
    Map.View = MatrixLookAt(Eye, Below, (Vector3){ 0.0f, 0.0f, 1.0f });
    double Half = WORLD_INTERIOR_SKY_MAP_EXTENT * 0.5;
    Map.Projection = MatrixOrtho(-Half, Half, -Half, Half, 0.0, WORLD_INTERIOR_SKY_MAP_DEPTH);
    Map.ViewProjection = MatrixMultiply(Map.View, Map.Projection);
    return Map;
}

float WorldInterior_GetSkyWindowBottom(Vector3Double origin)
{
    double Bottom = floor((origin.Y - WORLD_INTERIOR_SKY_WINDOW_BELOW) / WORLD_INTERIOR_SKY_SLICE_HEIGHT)
        * WORLD_INTERIOR_SKY_SLICE_HEIGHT;
    return (float)(Bottom - origin.Y);
}

void WorldInterior_GetSliceParityMask(float height, float windowBottom, uint32_t* outMask)
{
    ClearSliceMask(outMask);
    // The slices whose centre lies below the height: k with bottom + (k + 0.5) x slice height below it.
    float Below = ((height - windowBottom) / (float)WORLD_INTERIOR_SKY_SLICE_HEIGHT) - 0.5f;
    if (Below > 0.0f)
    {
        int Count = (Below < (float)WORLD_INTERIOR_SKY_SLICE_COUNT) ? (int)ceilf(Below) : WORLD_INTERIOR_SKY_SLICE_COUNT;
        SetSliceRun(0, Count - 1, outMask);
    }
}

void WorldInterior_GetSlicePresenceMask(float height, float windowBottom, uint32_t* outMask)
{
    ClearSliceMask(outMask);
    float Slice = (height - windowBottom) / (float)WORLD_INTERIOR_SKY_SLICE_HEIGHT;
    if (Slice >= 0.0f)
    {
        // Above the window, its top slice: what lies above keeps the sky out of the column too.
        int Index = (Slice < (float)WORLD_INTERIOR_SKY_SLICE_COUNT) ? (int)floorf(Slice) : WORLD_INTERIOR_SKY_SLICE_COUNT - 1;
        SetSliceRun(Index, Index, outMask);
    }
}

void WorldInterior_GetSliceRangeMask(float low, float high, float windowBottom, uint32_t* outMask)
{
    ClearSliceMask(outMask);
    float Top = windowBottom + (float)(WORLD_INTERIOR_SKY_SLICE_COUNT * WORLD_INTERIOR_SKY_SLICE_HEIGHT);
    if (!(high >= low) || !(high >= windowBottom))
    {
        return; // broken, or wholly below the window
    }
    int Last = WORLD_INTERIOR_SKY_SLICE_COUNT - 1;
    if (low >= Top)
    {
        SetSliceRun(Last, Last, outMask); // wholly above: a column solid at the window's top goes on up
        return;
    }
    int First = (int)floorf((fmaxf(low, windowBottom) - windowBottom) / (float)WORLD_INTERIOR_SKY_SLICE_HEIGHT);
    int End = (high < Top) ? (int)floorf((high - windowBottom) / (float)WORLD_INTERIOR_SKY_SLICE_HEIGHT) : Last;
    SetSliceRun(First, (End < Last) ? End : Last, outMask);
}

int WorldInterior_GetSkyLevelAt(float distance)
{
    float Texel = (float)(WORLD_INTERIOR_SKY_MAP_EXTENT / (double)WORLD_INTERIOR_SKY_MAP_RESOLUTION);
    float Level = ceilf(log2f(WORLD_INTERIOR_SKY_CELL_RATIO * distance / Texel));
    // NaN and below 0 count as 0 (fmaxf takes the number).
    Level = fminf(fmaxf(Level, 0.0f), (float)(WORLD_INTERIOR_SKY_LEVEL_COUNT - 1));
    return (int)Level;
}

bool WorldInterior_GetSkyTexel(const WorldInteriorSkyMap* map, float x, float z, int level, int* outX, int* outY)
{
    Vector3 Clip = Vector3Transform((Vector3){ x, 0.0f, z }, map->ViewProjection);
    float U = (Clip.x * 0.5f) + 0.5f;
    float V = (Clip.y * 0.5f) + 0.5f;
    if (!(U >= 0.0f) || !(U < 1.0f) || !(V >= 0.0f) || !(V < 1.0f))
    {
        return false;
    }
    *outX = (int)floorf(U * (float)WORLD_INTERIOR_SKY_MAP_RESOLUTION) >> level;
    *outY = (int)floorf(V * (float)WORLD_INTERIOR_SKY_MAP_RESOLUTION) >> level;
    return true;
}

float WorldInterior_GetSkyEdgeFade(Vector3 position, float windowBottom)
{
    float Distance = sqrtf(position.x * position.x + position.z * position.z);
    if (!(Distance >= 0.0f) || isnan(position.y))
    {
        return 1.0f; // a broken position counts as beyond the edge: open
    }
    float Top = windowBottom + (float)(WORLD_INTERIOR_SKY_SLICE_COUNT * WORLD_INTERIOR_SKY_SLICE_HEIGHT);
    float Edge = (Distance - (WORLD_INTERIOR_SKY_FADE_END - WORLD_INTERIOR_SKY_FADE_WIDTH)) / WORLD_INTERIOR_SKY_FADE_WIDTH;
    float Below = (windowBottom + WORLD_INTERIOR_SKY_WINDOW_FADE - position.y) / WORLD_INTERIOR_SKY_WINDOW_FADE;
    float Above = (position.y - (Top - WORLD_INTERIOR_SKY_WINDOW_FADE)) / WORLD_INTERIOR_SKY_WINDOW_FADE;
    float Fade = fmaxf(Edge, fmaxf(Below, Above));
    return (Fade < 1.0f) ? fmaxf(Fade, 0.0f) : 1.0f;
}

float WorldInterior_GetSkyOverhead(WorldInteriorColumnFunction columns, const void* context,
    const WorldInteriorSkyMap* map, float windowBottom, Vector3 position, Vector3 normal)
{
    float Fade = WorldInterior_GetSkyEdgeFade(position, windowBottom);
    if (!(Fade < 1.0f))
    {
        return 1.0f;
    }
    bool IsOpen = IsOpenOverhead(columns, context, map, windowBottom, position, normal, GetBandBase(position, normal),
        GetOwnSlice(position.y, windowBottom));
    return IsOpen ? 1.0f : Fade;
}

float WorldInterior_GetSkyVisibility(WorldInteriorColumnFunction columns, const void* context,
    const WorldInteriorSkyMap* map, float windowBottom, Vector3 position, Vector3 normal, float turn, int azimuthCount)
{
    float Fade = WorldInterior_GetSkyEdgeFade(position, windowBottom);
    if (!(Fade < 1.0f))
    {
        return 1.0f;
    }
    int Azimuths = ((azimuthCount >= 1) && (azimuthCount <= WORLD_INTERIOR_SKY_MAX_AZIMUTHS)) ? azimuthCount
        : WORLD_INTERIOR_SKY_MAX_AZIMUTHS;
    float Base = GetBandBase(position, normal);
    // A surface facing up goes on around itself as its plane: how fast that plane rises, at its steepest.
    bool IsSloped = normal.y > WORLD_INTERIOR_SKY_SLOPE_UP;
    float MaxRise = IsSloped ? fminf(sqrtf(normal.x * normal.x + normal.z * normal.z) / normal.y,
        WORLD_INTERIOR_SKY_MAX_RISE) : 0.0f;
    float Texel = (float)(WORLD_INTERIOR_SKY_MAP_EXTENT / (double)WORLD_INTERIOR_SKY_MAP_RESOLUTION);
    float BandStep = WORLD_INTERIOR_SKY_BAND_DEGREES * DEG2RAD;
    // The bands' edges, as the tangents of their elevations.
    float Tangents[WORLD_INTERIOR_SKY_BANDS + 1];
    for (int Edge = 0; Edge <= WORLD_INTERIOR_SKY_BANDS; Edge++)
    {
        Tangents[Edge] = tanf((float)Edge * BandStep);
    }
    // The slice the surface's own height lies in, and where it lies on the map (the map's place is linear in the point:
    // a direction's places are its own plus the distance times their change a metre).
    int OwnSlice = GetOwnSlice(position.y, windowBottom);
    Vector2 PlaceAt = GetMapPlace(map, position.x, position.z);
    float WindowTop = windowBottom + (float)(WORLD_INTERIOR_SKY_SLICE_COUNT * WORLD_INTERIOR_SKY_SLICE_HEIGHT);
    float Open = 0.0f;
    float Total = 0.0f;
    for (int Azimuth = 0; Azimuth < Azimuths; Azimuth++)
    {
        float Angle = (((float)Azimuth + turn) / (float)Azimuths) * 2.0f * PI;
        float DirectionX = cosf(Angle);
        float DirectionZ = sinf(Angle);
        Vector2 PlaceAlong = Vector2Subtract(GetMapPlace(map, position.x + DirectionX, position.z + DirectionZ), PlaceAt);
        // How fast the surface's plane rises along this direction (uphill only).
        float Uphill = IsSloped ? fmaxf(-(normal.x * DirectionX + normal.z * DirectionZ), 0.0f) / normal.y : 0.0f;
        // Each band's weight: its solid angle per unit of azimuth times the surface's cosine to its middle; and the
        // tangents of the elevations its blockers span, lowest and highest (none yet).
        float Weights[WORLD_INTERIOR_SKY_BANDS];
        float Lowest[WORLD_INTERIOR_SKY_BANDS];
        float Highest[WORLD_INTERIOR_SKY_BANDS];
        unsigned int Weighted = 0U;
        for (int Band = 0; Band < WORLD_INTERIOR_SKY_BANDS; Band++)
        {
            float Middle = ((float)Band + 0.5f) * BandStep;
            Vector3 Direction = { cosf(Middle) * DirectionX, sinf(Middle), cosf(Middle) * DirectionZ };
            Weights[Band] = fmaxf(Vector3DotProduct(normal, Direction), 0.0f)
                * (sinf((float)(Band + 1) * BandStep) - sinf((float)Band * BandStep));
            Weighted |= (Weights[Band] > 0.0f) ? (1U << Band) : 0U;
            Lowest[Band] = INTERIOR_NO_BLOCKER;
            Highest[Band] = -INTERIOR_NO_BLOCKER;
        }
        unsigned int Closed = 0U;
        // Through the texels the direction crosses, one after another (no wall between two reads), at the level whose
        // texels are WORLD_INTERIOR_SKY_CELL_RATIO of the distance wide: each read over the distances it spans, nearest
        // to farthest.
        float Nearest = WORLD_INTERIOR_SKY_MARCH_START;
        for (int Cell = 0; (Cell < WORLD_INTERIOR_SKY_MAX_CELLS) && (Nearest < WORLD_INTERIOR_SKY_MARCH_END)
            && ((Closed & Weighted) != Weighted); Cell++)
        {
            int Level = WorldInterior_GetSkyLevelAt(Nearest);
            float CellsAcross = (float)WORLD_INTERIOR_SKY_MAP_RESOLUTION / (float)(1 << Level);
            Vector2 At = Vector2Scale(Vector2Add(PlaceAt, Vector2Scale(PlaceAlong, Nearest)), CellsAcross);
            Vector2 Corner = { floorf(At.x), floorf(At.y) };
            if (!(Corner.x >= 0.0f) || !(Corner.y >= 0.0f) || !(Corner.x < CellsAcross) || !(Corner.y < CellsAcross))
            {
                break; // off the map: nothing known beyond, open
            }
            // Where the direction leaves the texel: the nearer of its two far sides.
            Vector2 Rate = Vector2Scale(PlaceAlong, CellsAcross);
            float ExitX = ((Rate.x >= 0.0f) ? ((Corner.x + 1.0f) - At.x) : (At.x - Corner.x)) / fmaxf(fabsf(Rate.x), 1e-9f);
            float ExitY = ((Rate.y >= 0.0f) ? ((Corner.y + 1.0f) - At.y) : (At.y - Corner.y)) / fmaxf(fabsf(Rate.y), 1e-9f);
            float Farthest = fminf(Nearest + fminf(ExitX, ExitY), WORLD_INTERIOR_SKY_MARCH_END);
            uint32_t Column[WORLD_INTERIOR_SKY_SLICE_WORDS];
            columns(context, Level, (int)Corner.x, (int)Corner.y, Column);
            float LevelTexel = Texel * (float)(1 << Level);
            // The surface's own plane there, as high as it reaches across the texel.
            float Plane = (Uphill * Farthest) + (MaxRise * LevelTexel * 1.41421356f);
            // What is solid over the heights the bands still open pass there, above that plane: mostly nothing. Where
            // the plane reaches the window's top slice, what is solid there may be the surface's own ground going on up:
            // nothing is known.
            int LowestBand = 0;
            int HighestBand = 0;
            GetBandRange(Weighted & ~Closed, &LowestBand, &HighestBand);
            uint32_t Union[WORLD_INTERIOR_SKY_SLICE_WORDS] = { 0U, 0U, 0U, 0U };
            if ((Base + Plane) < (WindowTop - (float)WORLD_INTERIOR_SKY_SLICE_HEIGHT))
            {
                WorldInterior_GetSliceRangeMask(Base + fmaxf(Nearest * Tangents[LowestBand], Plane),
                    Base + (Farthest * Tangents[HighestBand + 1]), windowBottom, Union);
            }
            uint32_t Solid[WORLD_INTERIOR_SKY_SLICE_WORDS];
            for (int Word = 0; Word < WORLD_INTERIOR_SKY_SLICE_WORDS; Word++)
            {
                Solid[Word] = Column[Word] & Union[Word];
            }
            // Its runs of solid slices, each hiding the elevations from its bottom seen from the farthest of those
            // distances to its top seen from the nearest (a column solid at the window's top goes on up) in the bands
            // they reach into; past the first few, the rest count as one.
            uint32_t Starts[WORLD_INTERIOR_SKY_SLICE_WORDS];
            uint32_t Ends[WORLD_INTERIOR_SKY_SLICE_WORDS];
            for (int Word = 0; Word < WORLD_INTERIOR_SKY_SLICE_WORDS; Word++)
            {
                uint32_t Below = (Solid[Word] << 1U) | ((Word > 0) ? (Solid[Word - 1] >> 31U) : 0U);
                uint32_t Above = (Solid[Word] >> 1U)
                    | ((Word < WORLD_INTERIOR_SKY_SLICE_WORDS - 1) ? (Solid[Word + 1] << 31U) : 0U);
                Starts[Word] = Solid[Word] & ~Below;
                Ends[Word] = Solid[Word] & ~Above;
            }
            for (int Run = 0; (Run < WORLD_INTERIOR_SKY_MAX_RUNS) && !IsMaskEmpty(Starts); Run++)
            {
                int First = TakeLowestSlice(Starts);
                int Last = TakeLowestSlice(Ends);
                if (Run == WORLD_INTERIOR_SKY_MAX_RUNS - 1)
                {
                    int Ignored = 0;
                    uint32_t All[WORLD_INTERIOR_SKY_SLICE_WORDS] = { ~0U, ~0U, ~0U, ~0U };
                    if (!IsMaskEmpty(Ends))
                    {
                        FindSolidSlices(Ends, All, &Ignored, &Last);
                    }
                }
                if (IsUnderfoot(Column, Last, OwnSlice))
                {
                    continue; // the top of what the surface stands over: no wall
                }
                float Bottom = ((windowBottom + ((float)First * (float)WORLD_INTERIOR_SKY_SLICE_HEIGHT)) - Base) / Farthest;
                float Top = (Last == WORLD_INTERIOR_SKY_SLICE_COUNT - 1) ? INTERIOR_NO_BLOCKER
                    : ((windowBottom + ((float)(Last + 1) * (float)WORLD_INTERIOR_SKY_SLICE_HEIGHT)) - Base) / Nearest;
                for (int Band = LowestBand; Band <= HighestBand; Band++)
                {
                    unsigned int Bit = 1U << Band;
                    if (((Weighted & ~Closed & Bit) == 0U) || !(Top > Tangents[Band]) || !(Bottom < Tangents[Band + 1]))
                    {
                        continue;
                    }
                    Lowest[Band] = fminf(Lowest[Band], Bottom);
                    Highest[Band] = fmaxf(Highest[Band], Top);
                    bool IsWhole = (Lowest[Band] <= Tangents[Band]) && (Highest[Band] >= Tangents[Band + 1]);
                    Closed |= IsWhole ? Bit : 0U;
                }
            }
            // On into the next texel, just past the side (a hair of the texel: never stuck on it).
            Nearest = Farthest + (1e-3f * LevelTexel);
        }
        for (int Band = 0; Band < WORLD_INTERIOR_SKY_BANDS; Band++)
        {
            Total += Weights[Band];
            Open += Weights[Band] * GetBandOpenShare(Tangents[Band], Tangents[Band + 1], Lowest[Band], Highest[Band]);
        }
    }
    // The zenith's cap above the bands, open when nothing lies straight above: weighed as the bands are (its solid angle
    // per unit of azimuth, every direction's), plus a floor so a surface facing down takes its openness.
    float Cap = (float)Azimuths * (((1.0f - sinf((float)WORLD_INTERIOR_SKY_BANDS * BandStep)) * fmaxf(normal.y, 0.0f))
        + WORLD_INTERIOR_SKY_ZENITH_FLOOR);
    Total += Cap;
    Open += IsOpenOverhead(columns, context, map, windowBottom, position, normal, Base, OwnSlice) ? Cap : 0.0f;
    float Visibility = Open / Total;
    // Toward open by the edges' fade, written so the open (1) and the unfaded stay exact (the shader writes it alike).
    return Visibility + (1.0f - Visibility) * Fade;
}

float WorldInterior_GetSkyShare(float visibility)
{
    float Visibility = (visibility > 0.0f) ? fminf(visibility, 1.0f) : 0.0f;
    return 1.0f - (1.0f - WORLD_INTERIOR_SKY_FLOOR) * (1.0f - Visibility);
}


// Internal functions (declared in WorldInterior.h): the camera.
void WorldInteriorCamera_Construct(WorldInteriorCamera* self)
{
    *self = (WorldInteriorCamera){ .Openness = 1.0f, .Zones = { .SkyAmbientScale = 1.0f }, .IsStarted = false };
}

void WorldInteriorCamera_Update(WorldInteriorCamera* self, float openness, const WorldInteriorZoneLight* zones,
    float deltaSeconds)
{
    float Openness = isnan(openness) ? 1.0f : fminf(fmaxf(openness, 0.0f), 1.0f);
    if (!self->IsStarted)
    {
        self->Openness = Openness;
        self->Zones = *zones;
        self->IsStarted = true;
        return;
    }
    float Share = GetEaseShare(deltaSeconds);
    self->Openness = Ease(self->Openness, Openness, Share);
    for (int Channel = 0; Channel < 3; Channel++)
    {
        self->Zones.Ambient[Channel] = Ease(self->Zones.Ambient[Channel], zones->Ambient[Channel], Share);
    }
    self->Zones.SkyAmbientScale = Ease(self->Zones.SkyAmbientScale, zones->SkyAmbientScale, Share);
    self->Zones.ExposureBias = Ease(self->Zones.ExposureBias, zones->ExposureBias, Share);
    self->Zones.Weight = Ease(self->Zones.Weight, zones->Weight, Share);
}

void WorldInterior_GetCameraLuminance(const WorldSkyLightFrame* light, const WorldInteriorCamera* camera,
    float* outScene, float* outDay)
{
    if (!camera->IsStarted)
    {
        *outScene = WorldSkyLight_GetSceneLuminance(light);
        *outDay = WorldSkyLight_GetDayLuminance(light);
        return;
    }
    // The sky's ambient as a surface takes it at the openness's visibility, and the zones' sky scale; the sun and the
    // moon through the openings.
    float SkyShare = WorldInterior_GetSkyShare(camera->Openness) * camera->Zones.SkyAmbientScale;
    WorldSkyLightFrame Indoors = *light;
    for (int Channel = 0; Channel < 3; Channel++)
    {
        Indoors.Sky[Channel] *= SkyShare;
        Indoors.SunSky[Channel] *= SkyShare;
        Indoors.Ground[Channel] *= SkyShare;
        Indoors.Night[Channel] *= SkyShare;
        Indoors.Sun[Channel] *= camera->Openness;
        Indoors.Moon[Channel] *= camera->Openness;
    }
    float ZoneLuminance = Luminance(camera->Zones.Ambient);
    *outScene = WorldSkyLight_GetSceneLuminance(&Indoors) + ZoneLuminance;
    *outDay = WorldSkyLight_GetDayLuminance(&Indoors) + ZoneLuminance;
}
