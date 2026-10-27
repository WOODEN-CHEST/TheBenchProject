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


// Fields.
/* The taps' offsets from the place a surface faces, per unit of their ring's radius: the centre, the inner ring (the
 * diagonals), then the outer ring from +X toward +Z (shaders/common/interior.glsl's INTERIOR_SKY_TAPS, pinned by
 * tests/ShaderSourceTests.c). */
static const float SkyTapOffsets[WORLD_INTERIOR_SKY_TAP_COUNT][2] =
{
    { 0.0f, 0.0f },
    { 0.70710678f, 0.70710678f }, { -0.70710678f, 0.70710678f }, { -0.70710678f, -0.70710678f },
    { 0.70710678f, -0.70710678f },
    { 1.0f, 0.0f }, { 0.70710678f, 0.70710678f }, { 0.0f, 1.0f }, { -0.70710678f, 0.70710678f },
    { -1.0f, 0.0f }, { -0.70710678f, -0.70710678f }, { 0.0f, -1.0f }, { 0.70710678f, -0.70710678f }
};


// Static functions.
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

Vector3 WorldInterior_GetSkyRingCenter(Vector3 position, Vector3 normal)
{
    return (Vector3){ position.x + normal.x * WORLD_INTERIOR_SKY_RING_RADIUS, position.y,
        position.z + normal.z * WORLD_INTERIOR_SKY_RING_RADIUS };
}

Vector2 WorldInterior_GetSkyTap(Vector3 position, Vector3 normal, int tap)
{
    int Index = ((tap >= 0) && (tap < WORLD_INTERIOR_SKY_TAP_COUNT)) ? tap : 0;
    float Radius = (Index <= WORLD_INTERIOR_SKY_INNER_TAPS) ? WORLD_INTERIOR_SKY_INNER_RADIUS
        : WORLD_INTERIOR_SKY_RING_RADIUS;
    Vector3 Center = WorldInterior_GetSkyRingCenter(position, normal);
    return (Vector2){ Center.x + SkyTapOffsets[Index][0] * Radius, Center.z + SkyTapOffsets[Index][1] * Radius };
}

float WorldInterior_GetSkyTapReference(Vector3 position, Vector3 normal, Vector2 tap)
{
    float Rise = -(normal.x * (tap.x - position.x) + normal.z * (tap.y - position.z))
        / fmaxf(fabsf(normal.y), WORLD_INTERIOR_SKY_MIN_UP);
    float Up = fminf(fmaxf(normal.y, 0.0f), 1.0f);
    float Bias = WORLD_INTERIOR_SKY_UPRIGHT_BIAS * (1.0f - Up) + WORLD_INTERIOR_SKY_BIAS * Up;
    float Upright = position.y + fmaxf(Rise, 0.0f) + Bias;
    float Downward = position.y - WORLD_INTERIOR_SKY_SELF;
    float Down = fminf(fmaxf(-WORLD_INTERIOR_SKY_DOWN_SCALE * normal.y, 0.0f), 1.0f);
    // mix as GLSL defines it: a(1 - t) + bt.
    return Upright * (1.0f - Down) + Downward * Down;
}

float WorldInterior_GetSkyEdgeFade(Vector3 position)
{
    float Distance = sqrtf(position.x * position.x + position.z * position.z);
    float Fade = (Distance - (WORLD_INTERIOR_SKY_FADE_END - WORLD_INTERIOR_SKY_FADE_WIDTH)) / WORLD_INTERIOR_SKY_FADE_WIDTH;
    return (Fade < 1.0f) ? fmaxf(Fade, 0.0f) : 1.0f; // a broken position counts as beyond the edge: open
}

float WorldInterior_GetSkyVisibility(WorldInteriorHeightFunction heights, const void* context, Vector3 position,
    Vector3 normal)
{
    float Edge = WorldInterior_GetSkyEdgeFade(position);
    if (!(Edge < 1.0f))
    {
        return 1.0f;
    }
    int NearOpen = 0;
    int RingOpen = 0;
    for (int Tap = 0; Tap < WORLD_INTERIOR_SKY_TAP_COUNT; Tap++)
    {
        Vector2 Point = WorldInterior_GetSkyTap(position, normal, Tap);
        float Height = heights(context, Point.x, Point.y);
        int Open = (Height > WorldInterior_GetSkyTapReference(position, normal, Point)) ? 0 : 1;
        if (Tap <= WORLD_INTERIOR_SKY_INNER_TAPS)
        {
            NearOpen += Open;
        }
        else
        {
            RingOpen += Open;
        }
    }
    float Visibility = ((float)NearOpen / (float)(1 + WORLD_INTERIOR_SKY_INNER_TAPS))
        * ((float)RingOpen / (float)WORLD_INTERIOR_SKY_RING_TAPS);
    // Toward open by the edge's fade, written so the open (1) and the unfaded stay exact (the shader writes it alike).
    return Visibility + (1.0f - Visibility) * Edge;
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
