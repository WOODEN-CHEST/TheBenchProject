#include "TestSuites.h"
#include "TestWorld.h"
#include "WorldInterior.h"
#include "WorldAmbientZoneComponent.h"
#include "WorldObject.h"
#include "GameMath.h"
#include "raylib/raymath.h"
#include <math.h>
#include <stdio.h>


// Macros.
/** Closeness for values computed through a float world matrix and square roots. */
#define TEST_INTERIOR_EPSILON 1e-4f


// Types.
/* A stand-in for the sky's occupancy volume: closed boxes (each a footprint and a span of heights) over an optional
 * ground sheet (its height rising by GroundSlope a metre along +X), voxelized as the renderer draws the volume: each
 * surface crossing a texel's column toggles the slices whose centre lies below it and a box's surface sets the slice it
 * passes through (the ground's, as the terrain's, does not), each box's toggles its own (the renderer's layers keep
 * overlapping things apart), ORed together. */
typedef struct InteriorVolumeBoxStruct
{
    float MinX;
    float MaxX;
    float MinY;
    float MaxY;
    float MinZ;
    float MaxZ;
} InteriorVolumeBox;

typedef struct InteriorVolumeStruct
{
    const InteriorVolumeBox* Boxes;
    size_t BoxCount;
    /** Whether a ground sheet lies under them, its height at X = 0, and its rise a metre along +X. */
    bool HasGround;
    float Ground;
    float GroundSlope;
    /** The map and its window, the stand-in's own (around the camera at the origin unless set otherwise). */
    WorldInteriorSkyMap Map;
    float WindowBottom;
    /** The map's clip space -> render space, for level-0 texels' centres. */
    Matrix ClipToRender;
} InteriorVolume;


// Static functions.
static bool IsNear(float a, float b)
{
    return fabsf(a - b) < TEST_INTERIOR_EPSILON;
}

static bool IsNearVector(Vector3 a, Vector3 b)
{
    return IsNear(a.x, b.x) && IsNear(a.y, b.y) && IsNear(a.z, b.z);
}

/* A volume of these boxes, with a flat ground at 0 or none, around a camera at the origin. */
static InteriorVolume MakeVolume(const InteriorVolumeBox* boxes, size_t boxCount, bool hasGround)
{
    InteriorVolume Volume = { .Boxes = boxes, .BoxCount = boxCount, .HasGround = hasGround, .Ground = 0.0f,
        .GroundSlope = 0.0f };
    Vector3Double Origin = { 0.0, 0.0, 0.0 };
    Volume.Map = WorldInterior_GetSkyMap(Origin);
    Volume.WindowBottom = WorldInterior_GetSkyWindowBottom(Origin);
    Volume.ClipToRender = MatrixInvert(Volume.Map.ViewProjection);
    return Volume;
}

/* Toggles a surface's parity slices into a column and, given a presence column (a model's surface, not the terrain's),
 * sets its slice. */
static void AddSurface(float height, float windowBottom, uint32_t* parity, uint32_t* presence)
{
    uint32_t Toggled[WORLD_INTERIOR_SKY_SLICE_WORDS];
    uint32_t Passed[WORLD_INTERIOR_SKY_SLICE_WORDS];
    WorldInterior_GetSliceParityMask(height, windowBottom, Toggled);
    WorldInterior_GetSlicePresenceMask(height, windowBottom, Passed);
    for (int Word = 0; Word < WORLD_INTERIOR_SKY_SLICE_WORDS; Word++)
    {
        parity[Word] ^= Toggled[Word];
        if (presence != NULL)
        {
            presence[Word] |= Passed[Word];
        }
    }
}

/* The stand-in's level-0 column at the centre of a texel. */
static void GetBaseColumn(const InteriorVolume* volume, int x, int y, uint32_t* outColumn)
{
    float Resolution = (float)WORLD_INTERIOR_SKY_MAP_RESOLUTION;
    Vector3 Clip = { (((float)x + 0.5f) / Resolution) * 2.0f - 1.0f, (((float)y + 0.5f) / Resolution) * 2.0f - 1.0f,
        0.0f };
    Vector3 Center = Vector3Transform(Clip, volume->ClipToRender);
    uint32_t Union[WORLD_INTERIOR_SKY_SLICE_WORDS] = { 0U };
    uint32_t Presence[WORLD_INTERIOR_SKY_SLICE_WORDS] = { 0U };
    if (volume->HasGround)
    {
        AddSurface(volume->Ground + volume->GroundSlope * Center.x, volume->WindowBottom, Union, NULL);
    }
    for (size_t Index = 0U; Index < volume->BoxCount; Index++)
    {
        const InteriorVolumeBox* Box = &volume->Boxes[Index];
        if ((Center.x >= Box->MinX) && (Center.x <= Box->MaxX) && (Center.z >= Box->MinZ) && (Center.z <= Box->MaxZ))
        {
            uint32_t Parity[WORLD_INTERIOR_SKY_SLICE_WORDS] = { 0U };
            AddSurface(Box->MaxY, volume->WindowBottom, Parity, Presence);
            AddSurface(Box->MinY, volume->WindowBottom, Parity, Presence);
            for (int Word = 0; Word < WORLD_INTERIOR_SKY_SLICE_WORDS; Word++)
            {
                Union[Word] |= Parity[Word];
            }
        }
    }
    for (int Word = 0; Word < WORLD_INTERIOR_SKY_SLICE_WORDS; Word++)
    {
        outColumn[Word] = Union[Word] | Presence[Word];
    }
}

/* The stand-in's column at a texel of a level: the union of the level-0 columns under it (WorldInteriorColumnFunction). */
static void GetColumn(const void* context, int level, int x, int y, uint32_t* outColumn)
{
    const InteriorVolume* Volume = context;
    for (int Word = 0; Word < WORLD_INTERIOR_SKY_SLICE_WORDS; Word++)
    {
        outColumn[Word] = 0U;
    }
    int Side = 1 << level;
    for (int Row = y * Side; Row < (y + 1) * Side; Row++)
    {
        for (int Column = x * Side; Column < (x + 1) * Side; Column++)
        {
            uint32_t Base[WORLD_INTERIOR_SKY_SLICE_WORDS];
            GetBaseColumn(Volume, Column, Row, Base);
            for (int Word = 0; Word < WORLD_INTERIOR_SKY_SLICE_WORDS; Word++)
            {
                outColumn[Word] |= Base[Word];
            }
        }
    }
}

/* A surface's visibility in the stand-in, its march unturned in the most directions. */
static float GetVisibility(const InteriorVolume* volume, Vector3 position, Vector3 normal)
{
    return WorldInterior_GetSkyVisibility(GetColumn, volume, &volume->Map, volume->WindowBottom, position, normal, 0.0f,
        WORLD_INTERIOR_SKY_MAX_AZIMUTHS);
}

/* A surface's visibility in the stand-in as a lit shader reads it without the pre-pass: what lies straight above. */
static float GetOverhead(const InteriorVolume* volume, Vector3 position, Vector3 normal)
{
    return WorldInterior_GetSkyOverhead(GetColumn, volume, &volume->Map, volume->WindowBottom, position, normal);
}

/* A box zone in render space, axis-aligned, with its own light. */
static WorldInteriorZone MakeBoxZone(Vector3 center, Vector3 halfExtents, float blend, float ambient, int32_t priority)
{
    WorldInteriorZone Zone = { 0 };
    Zone.Shape = WorldAmbientZoneShape_Box;
    Zone.Center = center;
    Zone.Axes[0] = (Vector3){ 1.0f, 0.0f, 0.0f };
    Zone.Axes[1] = (Vector3){ 0.0f, 1.0f, 0.0f };
    Zone.Axes[2] = (Vector3){ 0.0f, 0.0f, 1.0f };
    Zone.HalfExtents = halfExtents;
    Zone.BoundsRadius = Vector3Length(halfExtents);
    Zone.BlendDistance = blend;
    Zone.Ambient[0] = ambient;
    Zone.Ambient[1] = ambient;
    Zone.Ambient[2] = ambient;
    Zone.SkyAmbientScale = 1.0f;
    Zone.Haze = 1.0f;
    Zone.Priority = priority;
    return Zone;
}

/* A world with one object carrying an ambient zone: its placement given, its component returned. */
static WorldAmbientZoneComponent* AddZoneObject(TestRun* run, World* world, Vector3 position, Vector3 rotation,
    Vector3 scale, WorldObject** outObject)
{
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(world, (const unsigned char*)u8"room", &Object))
        || !TEST_CHECK_SUCCESS(run, World_AddComponent(world, WorldObject_GetId(Object), WorldComponentType_AmbientZone,
            &Component)))
    {
        return NULL;
    }
    TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, GameMath_FromVector3(position)));
    TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(Object, rotation));
    TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Object, scale));
    *outObject = Object;
    return WorldAmbientZoneComponent_FromComponent(Component);
}


// Tests: the component.
/* A new zone has its documented defaults, its setters keep the table's rules (and advance the revision), and its table
 * passes the property tables' checks. */
static void TheZoneComponentHasItsDefaultsAndRules(TestRun* run)
{
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(WorldAmbientZoneComponent_GetPropertyTable()));
    World TestWorld;
    KeyRegistry Keys;
    if (!TestWorld_Construct(run, &TestWorld, &Keys, NULL, NULL))
    {
        return;
    }
    WorldObject* Object = NULL;
    WorldAmbientZoneComponent* Zone = AddZoneObject(run, &TestWorld, (Vector3){ 0.0f, 0.0f, 0.0f },
        (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 1.0f, 1.0f, 1.0f }, &Object);
    if (Zone != NULL)
    {
        Vector3 Halves = WorldAmbientZoneComponent_GetHalfExtents(Zone);
        Color Light = WorldAmbientZoneComponent_GetAmbientColor(Zone);
        TEST_CHECK(run, (WorldAmbientZoneComponent_GetShape(Zone) == WorldAmbientZoneShape_Box)
            && (Halves.x == 2.0f) && (Halves.y == 1.5f) && (Halves.z == 2.0f)
            && (WorldAmbientZoneComponent_GetRadius(Zone) == 2.0f)
            && (WorldAmbientZoneComponent_GetBlendDistance(Zone) == 0.1f)
            && (Light.r == 255) && (Light.g == 200) && (Light.b == 150)
            && (WorldAmbientZoneComponent_GetAmbientIntensity(Zone) == WORLD_AMBIENT_ZONE_DEFAULT_INTENSITY)
            && (WorldAmbientZoneComponent_GetSkyAmbientScale(Zone) == 1.0f)
            && (WorldAmbientZoneComponent_GetExposureBias(Zone) == 0.0f)
            && (WorldAmbientZoneComponent_GetPriority(Zone) == 0) && (WorldAmbientZoneComponent_GetHaze(Zone) == 1.0f));

        uint32_t Revision = WorldComponent_GetRevision(&Zone->Base);
        TEST_CHECK_SUCCESS(run, WorldAmbientZoneComponent_SetShape(Zone, WorldAmbientZoneShape_Sphere));
        TEST_CHECK_SUCCESS(run, WorldAmbientZoneComponent_SetPriority(Zone, -7));
        TEST_CHECK_SUCCESS(run, WorldAmbientZoneComponent_SetExposureBias(Zone, -WORLD_AMBIENT_ZONE_MAX_EXPOSURE_BIAS));
        TEST_CHECK(run, WorldComponent_GetRevision(&Zone->Base) == (Revision + 3U));

        // The rules: sizes, blend, intensity and sky scale at least 0, the bias and the haze within their ranges, a
        // shape of the enum's; a refused value changes nothing.
        TEST_CHECK_ERROR(run, WorldAmbientZoneComponent_SetHalfExtents(Zone, (Vector3){ 1.0f, -0.5f, 1.0f }),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldAmbientZoneComponent_SetRadius(Zone, NAN), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldAmbientZoneComponent_SetBlendDistance(Zone, -0.1f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldAmbientZoneComponent_SetAmbientIntensity(Zone, -1.0f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldAmbientZoneComponent_SetSkyAmbientScale(Zone, -1.0f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldAmbientZoneComponent_SetExposureBias(Zone, WORLD_AMBIENT_ZONE_MAX_EXPOSURE_BIAS + 1.0f),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldAmbientZoneComponent_SetHaze(Zone, WORLD_AMBIENT_ZONE_MAX_HAZE + 1.0f),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldAmbientZoneComponent_SetShape(Zone, (WorldAmbientZoneShape)5),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, (WorldAmbientZoneComponent_GetShape(Zone) == WorldAmbientZoneShape_Sphere)
            && (WorldAmbientZoneComponent_GetExposureBias(Zone) == -WORLD_AMBIENT_ZONE_MAX_EXPOSURE_BIAS));
        TEST_CHECK_ERROR(run, WorldAmbientZoneComponent_SetHaze(NULL, 1.0f), ErrorCode_IllegalArgument);
    }
    TestWorld_Deconstruct(run, &TestWorld, &Keys);
}


// Tests: zones.
/* A zone placed by its object's world matrix: a box turned a quarter about Y and scaled 2 along its X lies along the
 * world's Z, twice as long, around the object's position relative to the frame's origin (subtracted in double, 1e6 m
 * out too); a sphere takes its largest scale; a collapsed axis leaves a zone nothing is inside. */
static void ZonesArePlacedByTheirObjects(TestRun* run)
{
    World TestWorld;
    KeyRegistry Keys;
    if (!TestWorld_Construct(run, &TestWorld, &Keys, NULL, NULL))
    {
        return;
    }
    WorldObject* Object = NULL;
    WorldAmbientZoneComponent* Component = AddZoneObject(run, &TestWorld, (Vector3){ 3.0f, 1.0f, -2.0f },
        (Vector3){ 0.0f, 0.5f * PI, 0.0f }, (Vector3){ 2.0f, 1.0f, 1.0f }, &Object);
    if (Component != NULL)
    {
        TEST_CHECK_SUCCESS(run, WorldAmbientZoneComponent_SetHalfExtents(Component, (Vector3){ 1.5f, 1.0f, 0.5f }));
        TEST_CHECK_SUCCESS(run, WorldAmbientZoneComponent_SetRadius(Component, 1.25f));
        Matrix4Double WorldMatrix = WorldObject_GetWorldMatrix(Object);
        Vector3Double Origin = { 1.0, 0.0, 0.0 };
        WorldInteriorZone Zone = WorldInterior_MakeZone(Component, &WorldMatrix, Origin);
        // Its x axis (scaled 2) now along the world's -Z (a quarter turn about Y takes +X to -Z).
        TEST_CHECK(run, IsNearVector(Zone.Center, (Vector3){ 2.0f, 1.0f, -2.0f })
            && IsNearVector(Zone.Axes[0], (Vector3){ 0.0f, 0.0f, -1.0f })
            && IsNearVector(Zone.Axes[1], (Vector3){ 0.0f, 1.0f, 0.0f })
            && IsNearVector(Zone.HalfExtents, (Vector3){ 3.0f, 1.0f, 0.5f }));
        // Inside 2.9 m along -Z from the centre, outside 3.1 m along it and 0.6 m along X.
        TEST_CHECK(run, IsNear(WorldInterior_GetZoneDistance(&Zone, (Vector3){ 2.0f, 1.0f, -4.9f }), -0.1f)
            && IsNear(WorldInterior_GetZoneDistance(&Zone, (Vector3){ 2.0f, 1.0f, -5.1f }), 0.1f)
            && IsNear(WorldInterior_GetZoneDistance(&Zone, (Vector3){ 2.6f, 1.0f, -2.0f }), 0.1f));
        // Far out: the same zone relative to an origin a million metres away, subtracted in double.
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, (Vector3Double){ 1000003.0, 1.0, -2.0 }));
        Matrix4Double FarMatrix = WorldObject_GetWorldMatrix(Object);
        WorldInteriorZone Far = WorldInterior_MakeZone(Component, &FarMatrix, (Vector3Double){ 1000001.0, 0.0, 0.0 });
        TEST_CHECK(run, IsNearVector(Far.Center, (Vector3){ 2.0f, 1.0f, -2.0f }));

        // A sphere: its radius times the largest scale.
        TEST_CHECK_SUCCESS(run, WorldAmbientZoneComponent_SetShape(Component, WorldAmbientZoneShape_Sphere));
        WorldInteriorZone Sphere = WorldInterior_MakeZone(Component, &FarMatrix, (Vector3Double){ 1000001.0, 0.0, 0.0 });
        TEST_CHECK(run, IsNear(Sphere.Radius, 2.5f) && IsNear(Sphere.BoundsRadius, 2.5f)
            && IsNear(WorldInterior_GetZoneDistance(&Sphere, (Vector3){ 2.0f, 4.0f, -2.0f }), 0.5f));

        TEST_CHECK(run, WorldInterior_HasInside(&Sphere) && WorldInterior_HasInside(&Far));

        // A collapsed axis: nothing is inside, for either shape.
        TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Object, (Vector3){ 1.0f, 0.0f, 1.0f }));
        Matrix4Double Flat = WorldObject_GetWorldMatrix(Object);
        WorldInteriorZone Empty = WorldInterior_MakeZone(Component, &Flat, (Vector3Double){ 1000001.0, 0.0, 0.0 });
        TEST_CHECK(run, !WorldInterior_HasInside(&Empty) && !(WorldInterior_GetZoneDistance(&Empty, Empty.Center) < 0.0f));
        TEST_CHECK_SUCCESS(run, WorldAmbientZoneComponent_SetShape(Component, WorldAmbientZoneShape_Box));
        WorldInteriorZone EmptyBox = WorldInterior_MakeZone(Component, &Flat, (Vector3Double){ 1000001.0, 0.0, 0.0 });
        TEST_CHECK(run, !WorldInterior_HasInside(&EmptyBox));
        // A zero size: nothing is inside either.
        TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Object, (Vector3){ 1.0f, 1.0f, 1.0f }));
        TEST_CHECK_SUCCESS(run, WorldAmbientZoneComponent_SetHalfExtents(Component, (Vector3){ 1.5f, 0.0f, 0.5f }));
        Matrix4Double Unit = WorldObject_GetWorldMatrix(Object);
        WorldInteriorZone Thin = WorldInterior_MakeZone(Component, &Unit, (Vector3Double){ 1000001.0, 0.0, 0.0 });
        TEST_CHECK(run, !WorldInterior_HasInside(&Thin));
    }
    TestWorld_Deconstruct(run, &TestWorld, &Keys);
}

/* A zone's weight fades in from its surface over its blend distance; a surface on or inside it that faces into it takes
 * it whole (a room's wall, floor and ceiling on its zone's surface), one beyond it takes nothing (a wall's outside face),
 * and a point with no normal (the camera) takes the fade alone. */
static void ZoneWeightsFadeInAndFaceIn(TestRun* run)
{
    WorldInteriorZone Room = MakeBoxZone((Vector3){ 0.0f, 1.5f, 0.0f }, (Vector3){ 2.0f, 1.5f, 2.0f }, 0.5f, 0.01f, 0);
    Vector3 None = { 0.0f, 0.0f, 0.0f };
    // The camera: deep inside, half the blend in, on the surface, beyond it.
    TEST_CHECK(run, WorldInterior_GetZoneWeight(&Room, (Vector3){ 0.0f, 1.5f, 0.0f }, None) == 1.0f);
    TEST_CHECK(run, IsNear(WorldInterior_GetZoneWeight(&Room, (Vector3){ 1.75f, 1.5f, 0.0f }, None), 0.5f));
    TEST_CHECK(run, WorldInterior_GetZoneWeight(&Room, (Vector3){ 2.0f, 1.5f, 0.0f }, None) == 0.0f);
    TEST_CHECK(run, WorldInterior_GetZoneWeight(&Room, (Vector3){ 2.5f, 1.5f, 0.0f }, None) == 0.0f);
    // The room's walls, floor and ceiling, on its surface and facing into it.
    TEST_CHECK(run, WorldInterior_GetZoneWeight(&Room, (Vector3){ 2.0f, 1.0f, 0.3f }, (Vector3){ -1.0f, 0.0f, 0.0f })
        == 1.0f);
    TEST_CHECK(run, WorldInterior_GetZoneWeight(&Room, (Vector3){ 0.5f, 0.0f, -1.0f }, (Vector3){ 0.0f, 1.0f, 0.0f })
        == 1.0f);
    TEST_CHECK(run, WorldInterior_GetZoneWeight(&Room, (Vector3){ 0.5f, 3.0f, 1.9f }, (Vector3){ 0.0f, -1.0f, 0.0f })
        == 1.0f);
    // A wall's outside face, 0.2 m beyond the zone and facing away: nothing; within the tolerance it still counts.
    TEST_CHECK(run, WorldInterior_GetZoneWeight(&Room, (Vector3){ 2.2f, 1.0f, 0.3f }, (Vector3){ 1.0f, 0.0f, 0.0f })
        == 0.0f);
    TEST_CHECK(run, WorldInterior_GetZoneWeight(&Room, (Vector3){ 2.005f, 1.0f, 0.3f },
        (Vector3){ -1.0f, 0.0f, 0.0f }) == 1.0f);
    // Inside the band facing out (toward its nearest face): the fade alone, 0.1 m in of 0.5; half facing in: half.
    TEST_CHECK(run, IsNear(WorldInterior_GetZoneWeight(&Room, (Vector3){ 1.9f, 1.0f, 0.3f },
        (Vector3){ 1.0f, 0.0f, 0.0f }), 0.2f));
    TEST_CHECK(run, IsNear(WorldInterior_GetZoneWeight(&Room, (Vector3){ 2.0f, 1.0f, 0.3f },
        (Vector3){ -0.5f, 0.0f, 0.8660254f }), 0.5f));
    // Inward from a box's nearest face; toward a sphere's centre (none at the centre).
    TEST_CHECK(run, IsNearVector(WorldInterior_GetZoneInward(&Room, (Vector3){ 0.0f, 0.2f, 0.0f }),
        (Vector3){ 0.0f, 1.0f, 0.0f }));
    TEST_CHECK(run, IsNearVector(WorldInterior_GetZoneInward(&Room, (Vector3){ -1.9f, 1.5f, 0.5f }),
        (Vector3){ 1.0f, 0.0f, 0.0f }));
    WorldInteriorZone Ball = Room;
    Ball.Shape = WorldAmbientZoneShape_Sphere;
    Ball.Radius = 2.0f;
    TEST_CHECK(run, IsNearVector(WorldInterior_GetZoneInward(&Ball, (Vector3){ 0.0f, 2.5f, 0.0f }),
        (Vector3){ 0.0f, -1.0f, 0.0f }) && IsNearVector(WorldInterior_GetZoneInward(&Ball, Ball.Center), None));
    TEST_CHECK(run, IsNear(WorldInterior_GetZoneWeight(&Ball, (Vector3){ 0.0f, 3.25f, 0.0f }, None), 0.5f));
    // A blend distance of 0: whole anywhere inside, nothing on the surface (but for a surface facing in).
    Room.BlendDistance = 0.0f;
    TEST_CHECK(run, (WorldInterior_GetZoneWeight(&Room, (Vector3){ 1.99f, 1.5f, 0.0f }, None) == 1.0f)
        && (WorldInterior_GetZoneWeight(&Room, (Vector3){ 2.0f, 1.5f, 0.0f }, None) == 0.0f));
    // A broken point weighs nothing.
    TEST_CHECK(run, WorldInterior_GetZoneWeight(&Room, (Vector3){ NAN, 1.5f, 0.0f }, None) == 0.0f);
}

/* Zones laid over each other: the higher priority wins where it is whole and lets the lower show through its fade;
 * equal ones blend by their weights; outside every zone nothing changes (no light, the sky's scale 1, no bias). */
static void OverlappingZonesFollowTheirPriority(TestRun* run)
{
    WorldInteriorZone Zones[3] =
    {
        MakeBoxZone((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 10.0f, 10.0f, 10.0f }, 1.0f, 1.0f, 0),
        MakeBoxZone((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 2.0f, 2.0f, 2.0f }, 1.0f, 4.0f, 3),
        MakeBoxZone((Vector3){ 5.0f, 0.0f, 0.0f }, (Vector3){ 2.0f, 2.0f, 2.0f }, 1.0f, 3.0f, 0)
    };
    Zones[0].SkyAmbientScale = 0.5f;
    Zones[1].SkyAmbientScale = 0.0f;
    Zones[1].ExposureBias = 2.0f;
    Zones[2].SkyAmbientScale = 1.0f;
    Vector3 None = { 0.0f, 0.0f, 0.0f };
    size_t Order[3] = { 0U, 2U, 1U }; // priority ascending, equal ones adjacent
    // Deep in the high-priority room: its values alone.
    WorldInteriorZoneLight Inner = WorldInterior_ComposeZones(Zones, Order, 3U, (Vector3){ 0.0f, 0.0f, 0.0f }, None);
    TEST_CHECK(run, IsNear(Inner.Ambient[0], 4.0f) && (Inner.SkyAmbientScale == 0.0f) && IsNear(Inner.ExposureBias, 2.0f)
        && (Inner.Weight == 1.0f));
    // Half way through its fade: half of it over the big zone's.
    WorldInteriorZoneLight Edge = WorldInterior_ComposeZones(Zones, Order, 3U, (Vector3){ 1.5f, 0.0f, 0.0f }, None);
    TEST_CHECK(run, IsNear(Edge.Ambient[0], 2.5f) && IsNear(Edge.SkyAmbientScale, 0.25f) && IsNear(Edge.ExposureBias, 1.0f));
    // Where the two equal ones overlap, both whole: their mean.
    WorldInteriorZoneLight Equal = WorldInterior_ComposeZones(Zones, Order, 3U, (Vector3){ 5.0f, 0.0f, 0.0f }, None);
    TEST_CHECK(run, IsNear(Equal.Ambient[0], 2.0f) && IsNear(Equal.SkyAmbientScale, 0.75f));
    // Two equal ones each half way through its fade: their mean, covering by the stronger's weight (not their sum).
    WorldInteriorZone Pair[2] =
    {
        MakeBoxZone((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 1.0f, 1.0f, 1.0f }, 1.0f, 1.0f, 0),
        MakeBoxZone((Vector3){ 1.0f, 0.0f, 0.0f }, (Vector3){ 1.0f, 1.0f, 1.0f }, 1.0f, 3.0f, 0)
    };
    size_t PairOrder[2] = { 0U, 1U };
    WorldInteriorZoneLight Both = WorldInterior_ComposeZones(Pair, PairOrder, 2U, (Vector3){ 0.5f, 0.0f, 0.0f }, None);
    TEST_CHECK(run, IsNear(Both.Ambient[0], 1.0f) && IsNear(Both.Weight, 0.5f) && IsNear(Both.SkyAmbientScale, 1.0f));
    // Outside every zone.
    WorldInteriorZoneLight Outside = WorldInterior_ComposeZones(Zones, Order, 3U, (Vector3){ 20.0f, 0.0f, 0.0f }, None);
    TEST_CHECK(run, (Outside.Ambient[0] == 0.0f) && (Outside.SkyAmbientScale == 1.0f) && (Outside.ExposureBias == 0.0f)
        && (Outside.Weight == 0.0f));
    TEST_CHECK(run, WorldInterior_ComposeZones(NULL, NULL, 0U, (Vector3){ 0.0f, 0.0f, 0.0f }, None).SkyAmbientScale
        == 1.0f);
}

/* A draw takes at most WORLD_INTERIOR_MAX_DRAW_ZONES zones whose inside reaches its sphere: the highest priorities, then
 * the nearest, then the first listed; written priority ascending, equal ones in their listed order. */
static void DrawsTakeAtMostFourZones(TestRun* run)
{
    WorldInteriorZone Zones[7] =
    {
        MakeBoxZone((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 1.0f, 1.0f, 1.0f }, 0.5f, 1.0f, 1),   // 0
        MakeBoxZone((Vector3){ 3.0f, 0.0f, 0.0f }, (Vector3){ 1.0f, 1.0f, 1.0f }, 0.5f, 1.0f, 1),   // 1: farther
        MakeBoxZone((Vector3){ 0.0f, 0.0f, 0.5f }, (Vector3){ 1.0f, 1.0f, 1.0f }, 0.5f, 1.0f, 9),   // 2: highest
        MakeBoxZone((Vector3){ 0.5f, 0.0f, 0.0f }, (Vector3){ 1.0f, 1.0f, 1.0f }, 0.5f, 1.0f, -2),  // 3: lowest
        MakeBoxZone((Vector3){ 30.0f, 0.0f, 0.0f }, (Vector3){ 1.0f, 1.0f, 1.0f }, 0.5f, 1.0f, 50), // 4: too far
        MakeBoxZone((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 1.0f, 1.0f, 1.0f }, 0.5f, 1.0f, 1),   // 5: like 0, later
        MakeBoxZone((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.5f, 0.5f, 0.5f }, 0.5f, 1.0f, 5)    // 6
    };
    size_t Selected[WORLD_INTERIOR_MAX_DRAW_ZONES];
    // A sphere of radius 2.5 around the origin: zone 4 is out of reach; of the rest the lowest priority (3) goes, and of
    // the three at priority 1 the farther (1) goes.
    size_t Count = WorldInterior_SelectZones(Zones, 7U, (Vector3){ 0.0f, 0.0f, 0.0f }, 2.5f, Selected);
    TEST_CHECK(run, (Count == WORLD_INTERIOR_MAX_DRAW_ZONES) && (Selected[0] == 0U) && (Selected[1] == 5U)
        && (Selected[2] == 6U) && (Selected[3] == 2U));
    // A zone whose bounds sphere touches the draw's sphere but whose box does not reach it is not taken: the corner of a
    // thin box.
    WorldInteriorZone Thin = MakeBoxZone((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 3.0f, 0.1f, 3.0f }, 0.5f, 1.0f, 0);
    TEST_CHECK(run, WorldInterior_SelectZones(&Thin, 1U, (Vector3){ 3.0f, 1.0f, 3.0f }, 0.8f, Selected) == 0U);
    // A point (the camera) takes the zones it is inside.
    TEST_CHECK(run, (WorldInterior_SelectZones(Zones, 7U, (Vector3){ 3.0f, 0.0f, 0.0f }, 0.0f, Selected) == 1U)
        && (Selected[0] == 1U));
    TEST_CHECK(run, WorldInterior_SelectZones(NULL, 0U, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, Selected) == 0U);
    // Five equal zones (the same priority and distance): the first four listed.
    WorldInteriorZone Same[5];
    for (size_t Index = 0U; Index < 5U; Index++)
    {
        Same[Index] = MakeBoxZone((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 1.0f, 1.0f, 1.0f }, 0.5f, 1.0f, 2);
    }
    TEST_CHECK(run, (WorldInterior_SelectZones(Same, 5U, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, Selected) == 4U)
        && (Selected[0] == 0U) && (Selected[1] == 1U) && (Selected[2] == 2U) && (Selected[3] == 3U));
}

/* The haze skips the part of the line of sight inside a zone by its haze: a room's still air (haze 0) seen from inside
 * is clear; seen from outside, only the inside stretch is taken off; a zone the line misses, or with the outside air's
 * haze, changes nothing; a hazier zone adds; a sphere the same way; never below 0. */
static void TheHazeSkipsThePathInsideZones(TestRun* run)
{
    WorldInteriorZone Room = MakeBoxZone((Vector3){ 0.0f, 0.0f, 10.0f }, (Vector3){ 2.0f, 2.0f, 2.0f }, 0.5f, 0.0f, 0);
    Room.Haze = 0.0f;
    size_t Order[1] = { 0U };
    Vector3 Forward = { 0.0f, 0.0f, 1.0f };
    // From the camera at the origin to the room's far wall, 12 m: 4 m of it inside.
    TEST_CHECK(run, IsNear(WorldInterior_GetZonePathLength(&Room, Forward, 12.0f), 4.0f));
    TEST_CHECK(run, IsNear(WorldInterior_GetHazeDistance(&Room, Order, 1U, (Vector3){ 0.0f, 0.0f, 12.0f }), 8.0f));
    // Beyond the room: 4 m of 20 skipped. Short of it: nothing.
    TEST_CHECK(run, IsNear(WorldInterior_GetHazeDistance(&Room, Order, 1U, (Vector3){ 0.0f, 0.0f, 20.0f }), 16.0f));
    TEST_CHECK(run, WorldInterior_GetHazeDistance(&Room, Order, 1U, (Vector3){ 0.0f, 0.0f, 5.0f }) == 5.0f);
    // A line that misses it; the whole box when the point lies beyond it (both lines parallel to two of its axes, whose
    // slabs divide by nothing).
    TEST_CHECK(run, WorldInterior_GetZonePathLength(&Room, (Vector3){ 1.0f, 0.0f, 0.0f }, 30.0f) == 0.0f);
    TEST_CHECK(run, WorldInterior_GetZonePathLength(&Room, Forward, 30.0f) == 4.0f);
    // The camera inside the room: every point in it is clear.
    WorldInteriorZone Around = MakeBoxZone((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 3.0f, 3.0f, 3.0f }, 0.5f, 0.0f, 0);
    Around.Haze = 0.0f;
    TEST_CHECK(run, WorldInterior_GetHazeDistance(&Around, Order, 1U, (Vector3){ 1.0f, 0.5f, 2.0f }) == 0.0f);
    // Two still zones over each other take the stretch off twice: never below 0. Inside a zone three times as hazy as
    // the air: the stretch from the camera, three times over.
    WorldInteriorZone Overlaid[2] = { Around, Around };
    size_t Both[2] = { 0U, 1U };
    TEST_CHECK(run, WorldInterior_GetHazeDistance(Overlaid, Both, 2U, (Vector3){ 1.0f, 0.5f, 2.0f }) == 0.0f);
    Around.Haze = 3.0f;
    TEST_CHECK(run, IsNear(WorldInterior_GetHazeDistance(&Around, Order, 1U, (Vector3){ 0.0f, 0.0f, 2.0f }), 6.0f));
    // The outside air's haze: exactly the distance; a hazier zone: longer.
    Room.Haze = 1.0f;
    TEST_CHECK(run, WorldInterior_GetHazeDistance(&Room, Order, 1U, (Vector3){ 0.0f, 0.0f, 12.0f }) == 12.0f);
    Room.Haze = 3.0f;
    TEST_CHECK(run, IsNear(WorldInterior_GetHazeDistance(&Room, Order, 1U, (Vector3){ 0.0f, 0.0f, 12.0f }), 20.0f));
    // A sphere: its chord.
    WorldInteriorZone Ball = Room;
    Ball.Shape = WorldAmbientZoneShape_Sphere;
    Ball.Radius = 2.0f;
    Ball.Haze = 0.0f;
    TEST_CHECK(run, IsNear(WorldInterior_GetHazeDistance(&Ball, Order, 1U, (Vector3){ 0.0f, 0.0f, 20.0f }), 16.0f));
    TEST_CHECK(run, WorldInterior_GetHazeDistance(&Ball, Order, 0U, (Vector3){ 0.0f, 0.0f, 20.0f }) == 20.0f);
}


// Tests: the sky's visibility.
/* The map stands still on its texel grid while the camera moves (its centre the camera's X and Z rounded to whole
 * texels, in double, a million metres out too) and its box spans the extent around it and the depth from half above to
 * half below the camera. */
static void TheSkyMapStandsOnItsTexelGrid(TestRun* run)
{
    const double Texel = WORLD_INTERIOR_SKY_MAP_EXTENT / (double)WORLD_INTERIOR_SKY_MAP_RESOLUTION;
    // On the texel grid: at the origin, and 10 666 667 texels (a million metres) out.
    const double Bases[] = { 0.0, 10666667.0 * Texel };
    for (size_t Index = 0U; Index < (sizeof(Bases) / sizeof(Bases[0])); Index++)
    {
        Vector3Double Position = { Bases[Index] + 10.0 * Texel + 0.2 * Texel, 3.0, -Bases[Index] - 0.3 * Texel };
        WorldInteriorSkyMap Map = WorldInterior_GetSkyMap(Position);
        Vector3Double Moved = { Position.X + 0.25 * Texel, 3.5, Position.Z - 0.1 * Texel };
        WorldInteriorSkyMap Same = WorldInterior_GetSkyMap(Moved);
        TEST_CHECK(run, (Map.Center.X == Same.Center.X) && (Map.Center.Z == Same.Center.Z)
            && (Map.Center.X == Bases[Index] + 10.0 * Texel) && (Map.Center.Z == -Bases[Index])
            && (Map.Center.Y == Position.Y));
        // In render space: the map's centre a fraction of a texel from the camera maps to the middle of its clip space;
        // a point half the extent away along X to its edge; the top of the box to depth -1, its bottom to 1.
        Vector3 Center = GameMath_ToVector3Relative(Map.Center, Position);
        Vector3 Middle = Vector3Transform(Center, Map.ViewProjection);
        Vector3 Edge = Vector3Transform((Vector3){ Center.x + (float)(WORLD_INTERIOR_SKY_MAP_EXTENT * 0.5), 0.0f,
            Center.z }, Map.ViewProjection);
        Vector3 Top = Vector3Transform((Vector3){ Center.x, (float)(WORLD_INTERIOR_SKY_MAP_DEPTH * 0.5), Center.z },
            Map.ViewProjection);
        Vector3 Bottom = Vector3Transform((Vector3){ Center.x, -(float)(WORLD_INTERIOR_SKY_MAP_DEPTH * 0.5), Center.z },
            Map.ViewProjection);
        TEST_CHECK(run, IsNear(Middle.x, 0.0f) && IsNear(Middle.y, 0.0f) && IsNear(fabsf(Edge.x) + fabsf(Edge.y), 1.0f)
            && IsNear(Top.z, -1.0f) && IsNear(Bottom.z, 1.0f));
    }
}

/* The window starts on the slices' grid below the camera (whole slices from the world's height 0, far out too); a surface
 * toggles the slices whose centre lies below it and passes through the slice it lies in (above the window, its top
 * one); a range of heights takes the slices it passes through, the top one wherever it reaches above the window, none
 * below it. */
static void TheSlicesFollowTheWindow(TestRun* run)
{
    const double Slice = WORLD_INTERIOR_SKY_SLICE_HEIGHT;
    const double Heights[] = { 0.0, 1.7, -3.13, 4000000.0 + 0.3 };
    for (size_t Index = 0U; Index < (sizeof(Heights) / sizeof(Heights[0])); Index++)
    {
        Vector3Double Eye = { 5.0, Heights[Index], -2.0 };
        double Bottom = Eye.Y + (double)WorldInterior_GetSkyWindowBottom(Eye);
        double Slices = Bottom / Slice;
        TEST_CHECK(run, (fabs(Slices - round(Slices)) < 1e-3) && (Bottom <= Eye.Y - WORLD_INTERIOR_SKY_WINDOW_BELOW + 1e-3)
            && (Bottom > Eye.Y - WORLD_INTERIOR_SKY_WINDOW_BELOW - Slice - 1e-3));
    }
    // A window from -12: slice k spans [-12 + k / 4, -12 + (k + 1) / 4).
    float Bottom = -12.0f;
    uint32_t Mask[WORLD_INTERIOR_SKY_SLICE_WORDS];
    // At 0.2 the slices up to 48 have their centres below it (48's at 0.125): bits 0 to 48; at 0.05, below 48's centre,
    // bits 0 to 47.
    WorldInterior_GetSliceParityMask(0.2f, Bottom, Mask);
    TEST_CHECK(run, (Mask[0] == 0xFFFFFFFFU) && (Mask[1] == 0x0001FFFFU) && (Mask[2] == 0U) && (Mask[3] == 0U));
    WorldInterior_GetSliceParityMask(0.05f, Bottom, Mask);
    TEST_CHECK(run, (Mask[0] == 0xFFFFFFFFU) && (Mask[1] == 0x0000FFFFU) && (Mask[2] == 0U) && (Mask[3] == 0U));
    WorldInterior_GetSlicePresenceMask(0.2f, Bottom, Mask);
    TEST_CHECK(run, (Mask[0] == 0U) && (Mask[1] == 0x00010000U) && (Mask[2] == 0U) && (Mask[3] == 0U));
    // Below the window nothing; above it every slice toggled and the top one passed through (what lies above keeps the
    // sky out of the column too).
    WorldInterior_GetSliceParityMask(-20.0f, Bottom, Mask);
    TEST_CHECK(run, (Mask[0] | Mask[1] | Mask[2] | Mask[3]) == 0U);
    WorldInterior_GetSlicePresenceMask(-20.0f, Bottom, Mask);
    TEST_CHECK(run, (Mask[0] | Mask[1] | Mask[2] | Mask[3]) == 0U);
    WorldInterior_GetSliceParityMask(100.0f, Bottom, Mask);
    TEST_CHECK(run, (Mask[0] & Mask[1] & Mask[2] & Mask[3]) == 0xFFFFFFFFU);
    WorldInterior_GetSlicePresenceMask(100.0f, Bottom, Mask);
    TEST_CHECK(run, (Mask[0] == 0U) && (Mask[1] == 0U) && (Mask[2] == 0U) && (Mask[3] == 0x80000000U));
    WorldInterior_GetSlicePresenceMask(NAN, Bottom, Mask);
    TEST_CHECK(run, (Mask[0] | Mask[1] | Mask[2] | Mask[3]) == 0U);
    // A slab thinner than a slice: its two faces toggle the same slices, it passes through one.
    uint32_t Parity[WORLD_INTERIOR_SKY_SLICE_WORDS] = { 0U };
    uint32_t Presence[WORLD_INTERIOR_SKY_SLICE_WORDS] = { 0U };
    AddSurface(0.83f, Bottom, Parity, Presence);
    AddSurface(0.77f, Bottom, Parity, Presence);
    TEST_CHECK(run, ((Parity[0] | Parity[1] | Parity[2] | Parity[3]) == 0U) && (Presence[1] == (1U << 19U)));
    // Ranges: across two words; reaching above the window (its top slice); wholly above; below; broken.
    WorldInterior_GetSliceRangeMask(-4.2f, -3.9f, Bottom, Mask);
    TEST_CHECK(run, (Mask[0] == 0x80000000U) && (Mask[1] == 0x00000001U) && (Mask[2] == 0U) && (Mask[3] == 0U));
    WorldInterior_GetSliceRangeMask(19.7f, INFINITY, Bottom, Mask);
    TEST_CHECK(run, (Mask[0] == 0U) && (Mask[1] == 0U) && (Mask[2] == 0U) && (Mask[3] == 0xC0000000U));
    WorldInterior_GetSliceRangeMask(30.0f, 31.0f, Bottom, Mask);
    TEST_CHECK(run, (Mask[0] == 0U) && (Mask[1] == 0U) && (Mask[2] == 0U) && (Mask[3] == 0x80000000U));
    WorldInterior_GetSliceRangeMask(-30.0f, -12.5f, Bottom, Mask);
    TEST_CHECK(run, (Mask[0] | Mask[1] | Mask[2] | Mask[3]) == 0U);
    WorldInterior_GetSliceRangeMask(1.0f, 0.5f, Bottom, Mask);
    TEST_CHECK(run, (Mask[0] | Mask[1] | Mask[2] | Mask[3]) == 0U);
    WorldInterior_GetSliceRangeMask(NAN, 0.5f, Bottom, Mask);
    TEST_CHECK(run, (Mask[0] | Mask[1] | Mask[2] | Mask[3]) == 0U);
}

/* The march coarsens with the distance: at each distance the finest level whose texels are the ratio of it wide (level 0
 * near, the coarsest far), never finer as it goes out; it starts past the surface's own level-0 texel; texels land where
 * the map puts the point, coarser levels by whole halvings, off the map nothing. */
static void TheMarchCoarsensWithTheDistance(TestRun* run)
{
    const float Texel = (float)(WORLD_INTERIOR_SKY_MAP_EXTENT / (double)WORLD_INTERIOR_SKY_MAP_RESOLUTION);
    TEST_CHECK(run, (WORLD_INTERIOR_SKY_MARCH_START > Texel * 1.4143f) && (WORLD_INTERIOR_SKY_MARCH_END
        < (float)(WORLD_INTERIOR_SKY_MAP_EXTENT * 0.5)));
    int Previous = 0;
    for (float Distance = WORLD_INTERIOR_SKY_MARCH_START; Distance < WORLD_INTERIOR_SKY_MARCH_END; Distance *= 1.1f)
    {
        int Level = WorldInterior_GetSkyLevelAt(Distance);
        float LevelTexel = Texel * (float)(1 << Level);
        bool IsTheFinest = (Level == 0) || ((LevelTexel * 0.5f) < WORLD_INTERIOR_SKY_CELL_RATIO * Distance);
        bool IsWideEnough = (Level == WORLD_INTERIOR_SKY_LEVEL_COUNT - 1)
            || (LevelTexel >= WORLD_INTERIOR_SKY_CELL_RATIO * Distance * 0.9999f);
        TEST_CHECK(run, (Level >= Previous) && (Level < WORLD_INTERIOR_SKY_LEVEL_COUNT) && IsTheFinest && IsWideEnough);
        Previous = Level;
    }
    TEST_CHECK(run, (WorldInterior_GetSkyLevelAt(0.0f) == 0) && (WorldInterior_GetSkyLevelAt(-1.0f) == 0)
        && (WorldInterior_GetSkyLevelAt(NAN) == 0)
        && (WorldInterior_GetSkyLevelAt(1.0e6f) == WORLD_INTERIOR_SKY_LEVEL_COUNT - 1));

    InteriorVolume Volume = MakeVolume(NULL, 0U, true);
    int X = 0;
    int Y = 0;
    int CoarseX = 0;
    int CoarseY = 0;
    TEST_CHECK(run, WorldInterior_GetSkyTexel(&Volume.Map, 3.1f, -7.4f, 0, &X, &Y)
        && WorldInterior_GetSkyTexel(&Volume.Map, 3.1f, -7.4f, 3, &CoarseX, &CoarseY)
        && (CoarseX == (X >> 3)) && (CoarseY == (Y >> 3)));
    // A texel's centre maps back to that texel.
    float Resolution = (float)WORLD_INTERIOR_SKY_MAP_RESOLUTION;
    Vector3 Center = Vector3Transform((Vector3){ (((float)X + 0.5f) / Resolution) * 2.0f - 1.0f,
        (((float)Y + 0.5f) / Resolution) * 2.0f - 1.0f, 0.0f }, Volume.ClipToRender);
    int Again = 0;
    int AgainY = 0;
    TEST_CHECK(run, WorldInterior_GetSkyTexel(&Volume.Map, Center.x, Center.z, 0, &Again, &AgainY) && (Again == X)
        && (AgainY == Y) && (fabsf(Center.x - 3.1f) < Texel) && (fabsf(Center.z + 7.4f) < Texel));
    TEST_CHECK(run, !WorldInterior_GetSkyTexel(&Volume.Map, 60.0f, 0.0f, 0, &X, &Y)
        && !WorldInterior_GetSkyTexel(&Volume.Map, 0.0f, -60.0f, 2, &X, &Y));
}

/* The visibility through stand-ins for the volume: the open field (and no volume at all) sees all of the sky (exactly 1);
 * a closed room's floor, its corner and its ceiling none of it; its floor by an open doorway the sky below the lintel;
 * the ground under a carport the sky below its roof's edge; the foot of a wall about half; a crate a metre away a sliver;
 * a 30-degree slope and a curb no roof; a thin table top is a roof; an upright face's foot, in the ground's slice, sees
 * all of it in the open, and a face just under a thin roof little; a roof above the window still covers. */
static void SkyVisibilityLooksAroundThroughTheVolume(TestRun* run)
{
    Vector3 Up = { 0.0f, 1.0f, 0.0f };
    Vector3 Down = { 0.0f, -1.0f, 0.0f };
    InteriorVolume Open = MakeVolume(NULL, 0U, true);
    TEST_CHECK(run, GetVisibility(&Open, (Vector3){ 3.0f, 0.0f, 4.0f }, Up) == 1.0f);
    TEST_CHECK(run, GetVisibility(&Open, (Vector3){ 3.0f, 1.5f, 4.0f }, (Vector3){ 1.0f, 0.0f, 0.0f }) == 1.0f);
    InteriorVolume Nothing = MakeVolume(NULL, 0U, false);
    TEST_CHECK(run, GetVisibility(&Nothing, (Vector3){ 3.0f, 0.0f, 4.0f }, Up) == 1.0f);

    // A room inside x and z in [-2, 2]: walls 0.2 m thick and 3 m high, a roof from 3 to 3.2 m over them.
    InteriorVolumeBox Room[5] =
    {
        { .MinX = -2.2f, .MaxX = -2.0f, .MinY = 0.0f, .MaxY = 3.0f, .MinZ = -2.2f, .MaxZ = 2.2f },
        { .MinX = 2.0f, .MaxX = 2.2f, .MinY = 0.0f, .MaxY = 3.0f, .MinZ = -2.2f, .MaxZ = 2.2f },
        { .MinX = -2.2f, .MaxX = 2.2f, .MinY = 0.0f, .MaxY = 3.0f, .MinZ = -2.2f, .MaxZ = -2.0f },
        { .MinX = -2.2f, .MaxX = 2.2f, .MinY = 0.0f, .MaxY = 3.0f, .MinZ = 2.0f, .MaxZ = 2.2f },
        { .MinX = -2.2f, .MaxX = 2.2f, .MinY = 3.0f, .MaxY = 3.2f, .MinZ = -2.2f, .MaxZ = 2.2f }
    };
    InteriorVolume Closed = MakeVolume(Room, 5U, true);
    TEST_CHECK(run, GetVisibility(&Closed, (Vector3){ 0.0f, 0.0f, 0.0f }, Up) == 0.0f);
    TEST_CHECK(run, GetVisibility(&Closed, (Vector3){ 1.85f, 0.0f, 1.85f }, Up) == 0.0f);
    TEST_CHECK(run, GetVisibility(&Closed, (Vector3){ 1.0f, 3.0f, 0.5f }, Down) == 0.0f);
    TEST_CHECK(run, GetVisibility(&Closed, (Vector3){ -2.0f, 1.5f, 0.0f }, (Vector3){ 1.0f, 0.0f, 0.0f }) == 0.0f);

    // The same room with a doorway 1 m wide and 2.1 m high in its +Z wall: the floor inside by it sees the street's sky
    // below the lintel, deep inside it does not.
    InteriorVolumeBox Doorway[7] =
    {
        Room[0], Room[1], Room[2], Room[4],
        { .MinX = -2.2f, .MaxX = -0.5f, .MinY = 0.0f, .MaxY = 3.0f, .MinZ = 2.0f, .MaxZ = 2.2f },
        { .MinX = 0.5f, .MaxX = 2.2f, .MinY = 0.0f, .MaxY = 3.0f, .MinZ = 2.0f, .MaxZ = 2.2f },
        { .MinX = -0.5f, .MaxX = 0.5f, .MinY = 2.1f, .MaxY = 3.0f, .MinZ = 2.0f, .MaxZ = 2.2f }
    };
    InteriorVolume Door = MakeVolume(Doorway, 7U, true);
    float ByTheDoor = GetVisibility(&Door, (Vector3){ 0.0f, 0.0f, 1.6f }, Up);
    TEST_CHECK(run, (ByTheDoor > 0.04f) && (ByTheDoor < 0.4f));
    TEST_CHECK(run, GetVisibility(&Door, (Vector3){ 0.0f, 0.0f, -1.5f }, Up) < 0.5f * ByTheDoor);
    // The door's own threshold, half in the doorway, sees more still.
    TEST_CHECK(run, GetVisibility(&Door, (Vector3){ 0.0f, 0.0f, 2.1f }, Up) > ByTheDoor);

    // A carport: a roof 2.5 to 2.7 m up, 4 m across, on four thin posts. Under it the sky below its edge shows.
    InteriorVolumeBox Carport[5] =
    {
        { .MinX = -2.0f, .MaxX = 2.0f, .MinY = 2.5f, .MaxY = 2.7f, .MinZ = -2.0f, .MaxZ = 2.0f },
        { .MinX = -2.0f, .MaxX = -1.9f, .MinY = 0.0f, .MaxY = 2.5f, .MinZ = -2.0f, .MaxZ = -1.9f },
        { .MinX = 1.9f, .MaxX = 2.0f, .MinY = 0.0f, .MaxY = 2.5f, .MinZ = -2.0f, .MaxZ = -1.9f },
        { .MinX = -2.0f, .MaxX = -1.9f, .MinY = 0.0f, .MaxY = 2.5f, .MinZ = 1.9f, .MaxZ = 2.0f },
        { .MinX = 1.9f, .MaxX = 2.0f, .MinY = 0.0f, .MaxY = 2.5f, .MinZ = 1.9f, .MaxZ = 2.0f }
    };
    InteriorVolume Shelter = MakeVolume(Carport, 5U, true);
    float UnderCarport = GetVisibility(&Shelter, (Vector3){ 0.0f, 0.0f, 0.0f }, Up);
    TEST_CHECK(run, (UnderCarport > 0.08f) && (UnderCarport < 0.75f)
        && (UnderCarport > GetVisibility(&Closed, (Vector3){ 0.0f, 0.0f, 0.0f }, Up) + 0.08f));

    // A long wall 3 m high: its foot, 0.5 m away, sees about half of the sky.
    InteriorVolumeBox Wall[1] = { { .MinX = -20.0f, .MaxX = 20.0f, .MinY = 0.0f, .MaxY = 3.0f, .MinZ = 1.0f,
        .MaxZ = 1.2f } };
    InteriorVolume Street = MakeVolume(Wall, 1U, true);
    float Foot = GetVisibility(&Street, (Vector3){ 0.0f, 0.0f, 0.5f }, Up);
    TEST_CHECK(run, (Foot > 0.35f) && (Foot < 0.7f));

    // A crate 0.4 m high a metre away hides a sliver; a curb 0.1 m high under the bands' lift hides nothing.
    InteriorVolumeBox Crate[1] = { { .MinX = 0.8f, .MaxX = 1.2f, .MinY = 0.0f, .MaxY = 0.4f, .MinZ = -0.2f,
        .MaxZ = 0.2f } };
    InteriorVolume Yard = MakeVolume(Crate, 1U, true);
    float BesideCrate = GetVisibility(&Yard, (Vector3){ 0.0f, 0.0f, 0.0f }, Up);
    TEST_CHECK(run, (BesideCrate < 1.0f) && (BesideCrate > 0.95f));
    InteriorVolumeBox Curb[1] = { { .MinX = 1.0f, .MaxX = 1.6f, .MinY = 0.0f, .MaxY = 0.1f, .MinZ = -10.0f,
        .MaxZ = 10.0f } };
    InteriorVolume Pavement = MakeVolume(Curb, 1U, true);
    TEST_CHECK(run, GetVisibility(&Pavement, (Vector3){ 0.0f, 0.0f, 0.0f }, Up) == 1.0f);

    // A 30-degree slope rising toward +X: its own ground is no roof, uphill or down (a coarse texel holds it at its
    // highest corner: the bands are tested above the slope's plane across it).
    InteriorVolume Hill = MakeVolume(NULL, 0U, true);
    Hill.GroundSlope = 0.5773503f;
    TEST_CHECK(run, GetVisibility(&Hill, (Vector3){ 4.0f, 4.0f * 0.5773503f, 1.0f },
        (Vector3){ -0.5f, 0.8660254f, 0.0f }) == 1.0f);

    // A table top 6 cm thick, inside one slice: the floor under it is roofed.
    InteriorVolumeBox Table[1] = { { .MinX = -0.8f, .MaxX = 0.8f, .MinY = 0.77f, .MaxY = 0.83f, .MinZ = -0.4f,
        .MaxZ = 0.4f } };
    InteriorVolume Desk = MakeVolume(Table, 1U, true);
    float UnderTable = GetVisibility(&Desk, (Vector3){ 0.0f, 0.0f, 0.0f }, Up);
    TEST_CHECK(run, (UnderTable < 0.9f) && (UnderTable > 0.2f));

    // A crate's side and an upright face in the open, from their foot (in the ground's slice: the ground is what they
    // stand over, no wall) to their top, see all of the sky.
    InteriorVolumeBox Cube[1] = { { .MinX = 0.0f, .MaxX = 0.5f, .MinY = 0.0f, .MaxY = 0.5f, .MinZ = 0.0f, .MaxZ = 0.5f } };
    InteriorVolume Lone = MakeVolume(Cube, 1U, true);
    for (float Height = 0.01f; Height < 0.5f; Height += 0.07f)
    {
        TEST_CHECK(run, GetVisibility(&Lone, (Vector3){ 0.5f, Height, 0.25f }, (Vector3){ 1.0f, 0.0f, 0.0f }) == 1.0f);
        TEST_CHECK(run, GetVisibility(&Open, (Vector3){ 3.0f, Height, 4.0f }, (Vector3){ 0.6f, 0.0f, 0.8f }) == 1.0f);
    }
    // Ground 0.15 m up fills its slice (above the slice's centre): an upright face's foot just above it, its bands
    // starting in that slice, still sees all of the sky, and so does the read without the pre-pass.
    InteriorVolume Raised = MakeVolume(NULL, 0U, true);
    Raised.Ground = 0.15f;
    TEST_CHECK(run, (GetVisibility(&Raised, (Vector3){ 3.0f, 0.16f, 4.0f }, (Vector3){ 1.0f, 0.0f, 0.0f }) == 1.0f)
        && (GetOverhead(&Raised, (Vector3){ 3.0f, 0.16f, 4.0f }, (Vector3){ 1.0f, 0.0f, 0.0f }) == 1.0f));
    // A shed's thin roof (inside one slice) on one wall: the wall's face just under it, in the roof's slice, is covered
    // (the slab has air below it: no ground).
    InteriorVolumeBox Shed[2] =
    {
        { .MinX = -3.0f, .MaxX = 3.0f, .MinY = 2.55f, .MaxY = 2.6f, .MinZ = -3.0f, .MaxZ = 3.0f },
        { .MinX = -3.2f, .MaxX = -3.0f, .MinY = 0.0f, .MaxY = 2.55f, .MinZ = -3.0f, .MaxZ = 3.0f }
    };
    InteriorVolume UnderShed = MakeVolume(Shed, 2U, true);
    TEST_CHECK(run, GetVisibility(&UnderShed, (Vector3){ -3.0f, 2.52f, 0.0f }, (Vector3){ 1.0f, 0.0f, 0.0f }) < 0.15f);
    // A closed roof wholly above the window (25 m up; the window's top is at 20 m) still covers the ground under it.
    InteriorVolumeBox HighRoof[1] = { { .MinX = -40.0f, .MaxX = 40.0f, .MinY = 25.0f, .MaxY = 26.0f, .MinZ = -40.0f,
        .MaxZ = 40.0f } };
    InteriorVolume UnderHighRoof = MakeVolume(HighRoof, 1U, true);
    TEST_CHECK(run, GetVisibility(&UnderHighRoof, (Vector3){ 0.0f, 0.0f, 0.0f }, Up) < 0.5f);

    // The pre-pass's turned reads differ, but every turn of the open field sees all of the sky, and no turn finds a way
    // out of the closed room anywhere on its floor (its march crosses every texel: no wall falls between two reads).
    for (float Turn = 0.03125f; Turn < 1.0f; Turn += 0.0625f)
    {
        TEST_CHECK(run, WorldInterior_GetSkyVisibility(GetColumn, &Open, &Open.Map, Open.WindowBottom,
            (Vector3){ 1.0f, 0.0f, 1.0f }, Up, Turn, WORLD_INTERIOR_SKY_PATTERN_AZIMUTHS) == 1.0f);
        float Worst = 0.0f;
        for (float X = -1.8f; X <= 1.8f; X += 0.15f)
        {
            for (float Z = -1.8f; Z <= 1.8f; Z += 0.15f)
            {
                Worst = fmaxf(Worst, WorldInterior_GetSkyVisibility(GetColumn, &Closed, &Closed.Map, Closed.WindowBottom,
                    (Vector3){ X, 0.0f, Z }, Up, Turn, WORLD_INTERIOR_SKY_PATTERN_AZIMUTHS));
            }
        }
        TEST_CHECK(run, Worst == 0.0f);
    }
}

/* The bands' numbers, in one direction (along +X, unturned) from a point facing up on open ground: a long wall ahead,
 * its near face on the coarse texels' edge 1.5 m out and its top slice ending 1.5 m up, hides the bands wholly below its
 * top seen from that face (from the point's lift, 0.3 m up) and the band holding it in part, by solid angle, the rest of
 * the sky open; a wall 8 m out rising past the window's top hides every band in its direction (it goes on up: the
 * window's top alone, seen from there, lies below the highest band's). The expected values are the band model's sums
 * by hand: each band's solid angle times its middle's sine, and the zenith's cap. */
static void TheBandsHideWhatAWallSpans(TestRun* run)
{
    Vector3 Up = { 0.0f, 1.0f, 0.0f };
    float BandStep = WORLD_INTERIOR_SKY_BAND_DEGREES * DEG2RAD;
    float Weights[WORLD_INTERIOR_SKY_BANDS];
    float Sum = 0.0f;
    for (int Band = 0; Band < WORLD_INTERIOR_SKY_BANDS; Band++)
    {
        Weights[Band] = sinf(((float)Band + 0.5f) * BandStep)
            * (sinf((float)(Band + 1) * BandStep) - sinf((float)Band * BandStep));
        Sum += Weights[Band];
    }
    float Cap = (1.0f - sinf((float)WORLD_INTERIOR_SKY_BANDS * BandStep)) + WORLD_INTERIOR_SKY_ZENITH_FLOOR;
    float WallTop = atanf((1.5f - WORLD_INTERIOR_SKY_LIFT) / 1.5f);
    float Open = Cap;
    for (int Band = 0; Band < WORLD_INTERIOR_SKY_BANDS; Band++)
    {
        float Low = (float)Band * BandStep;
        float High = (float)(Band + 1) * BandStep;
        float Share = (High <= WallTop) ? 0.0f : ((Low >= WallTop) ? 1.0f
            : ((sinf(High) - sinf(WallTop)) / (sinf(High) - sinf(Low))));
        Open += Weights[Band] * Share;
    }
    InteriorVolumeBox Long[1] = { { .MinX = 1.5f, .MaxX = 3.5f, .MinY = 0.0f, .MaxY = 1.49f, .MinZ = -20.0f,
        .MaxZ = 20.0f } };
    InteriorVolume Street = MakeVolume(Long, 1U, true);
    float Visibility = WorldInterior_GetSkyVisibility(GetColumn, &Street, &Street.Map, Street.WindowBottom,
        (Vector3){ 0.0f, 0.0f, 0.0f }, Up, 0.0f, 1);
    // (The march steps a hair past each texel's side: the wall's face reads 0.75 mm further than 1.5 m.)
    TEST_CHECK(run, (WallTop > 2.0f * BandStep) && (WallTop < 3.0f * BandStep)
        && (fabsf(Visibility - (Open / (Sum + Cap))) < 2e-3f));
    InteriorVolumeBox Tall[1] = { { .MinX = 8.0f, .MaxX = 10.0f, .MinY = 0.0f, .MaxY = 60.0f, .MinZ = -20.0f,
        .MaxZ = 20.0f } };
    InteriorVolume Cliff = MakeVolume(Tall, 1U, true);
    TEST_CHECK(run, fabsf(WorldInterior_GetSkyVisibility(GetColumn, &Cliff, &Cliff.Map, Cliff.WindowBottom,
        (Vector3){ 0.0f, 0.0f, 0.0f }, Up, 0.0f, 1) - (Cap / (Sum + Cap))) < 1e-4f);
}

/* The read without the pre-pass sees what lies straight above: the open field and the ground beside a crate are open
 * (exactly 1), and so is an upright face's foot in the ground's slice; a room's floor, its wall's face (read just in
 * front of it), its ceiling facing down and the ground under a carport are covered (0); under one big roof the cover
 * fades out toward the volume's edge as the march's does. */
static void TheOverheadReadSeesWhatLiesAbove(TestRun* run)
{
    Vector3 Up = { 0.0f, 1.0f, 0.0f };
    InteriorVolume Open = MakeVolume(NULL, 0U, true);
    TEST_CHECK(run, (GetOverhead(&Open, (Vector3){ 3.0f, 0.0f, 4.0f }, Up) == 1.0f)
        && (GetOverhead(&Open, (Vector3){ 3.0f, 0.02f, 4.0f }, (Vector3){ 1.0f, 0.0f, 0.0f }) == 1.0f));
    InteriorVolumeBox Crate[1] = { { .MinX = 0.8f, .MaxX = 1.2f, .MinY = 0.0f, .MaxY = 0.4f, .MinZ = -0.2f,
        .MaxZ = 0.2f } };
    InteriorVolume Yard = MakeVolume(Crate, 1U, true);
    TEST_CHECK(run, (GetOverhead(&Yard, (Vector3){ 0.6f, 0.0f, 0.0f }, Up) == 1.0f)
        && (GetOverhead(&Yard, (Vector3){ 0.8f, 0.1f, 0.0f }, (Vector3){ -1.0f, 0.0f, 0.0f }) == 1.0f));
    InteriorVolumeBox Room[5] =
    {
        { .MinX = -2.2f, .MaxX = -2.0f, .MinY = 0.0f, .MaxY = 3.0f, .MinZ = -2.2f, .MaxZ = 2.2f },
        { .MinX = 2.0f, .MaxX = 2.2f, .MinY = 0.0f, .MaxY = 3.0f, .MinZ = -2.2f, .MaxZ = 2.2f },
        { .MinX = -2.2f, .MaxX = 2.2f, .MinY = 0.0f, .MaxY = 3.0f, .MinZ = -2.2f, .MaxZ = -2.0f },
        { .MinX = -2.2f, .MaxX = 2.2f, .MinY = 0.0f, .MaxY = 3.0f, .MinZ = 2.0f, .MaxZ = 2.2f },
        { .MinX = -2.2f, .MaxX = 2.2f, .MinY = 3.0f, .MaxY = 3.2f, .MinZ = -2.2f, .MaxZ = 2.2f }
    };
    InteriorVolume Closed = MakeVolume(Room, 5U, true);
    TEST_CHECK(run, (GetOverhead(&Closed, (Vector3){ 0.5f, 0.0f, -0.5f }, Up) == 0.0f)
        && (GetOverhead(&Closed, (Vector3){ -2.0f, 1.5f, 0.0f }, (Vector3){ 1.0f, 0.0f, 0.0f }) == 0.0f)
        && (GetOverhead(&Closed, (Vector3){ 1.0f, 3.0f, 0.5f }, (Vector3){ 0.0f, -1.0f, 0.0f }) == 0.0f));
    InteriorVolumeBox Carport[1] = { { .MinX = -2.0f, .MaxX = 2.0f, .MinY = 2.5f, .MaxY = 2.7f, .MinZ = -2.0f,
        .MaxZ = 2.0f } };
    InteriorVolume Shelter = MakeVolume(Carport, 1U, true);
    TEST_CHECK(run, (GetOverhead(&Shelter, (Vector3){ 1.5f, 0.0f, 0.0f }, Up) == 0.0f)
        && (GetOverhead(&Shelter, (Vector3){ 2.3f, 0.0f, 0.0f }, Up) == 1.0f));
    InteriorVolumeBox Roof[1] = { { .MinX = -60.0f, .MaxX = 60.0f, .MinY = 4.0f, .MaxY = 4.3f, .MinZ = -60.0f,
        .MaxZ = 60.0f } };
    InteriorVolume Hall = MakeVolume(Roof, 1U, true);
    float Start = WORLD_INTERIOR_SKY_FADE_END - WORLD_INTERIOR_SKY_FADE_WIDTH;
    TEST_CHECK(run, (GetOverhead(&Hall, (Vector3){ 0.0f, 0.0f, Start - 1.0f }, Up) == 0.0f)
        && IsNear(GetOverhead(&Hall, (Vector3){ Start + 0.5f * WORLD_INTERIOR_SKY_FADE_WIDTH, 0.0f, 0.0f }, Up), 0.5f)
        && (GetOverhead(&Hall, (Vector3){ 0.0f, 0.0f, -WORLD_INTERIOR_SKY_FADE_END }, Up) == 1.0f));
}

/* Toward the map's edge, and the window's top and bottom, the visibility fades to open: none within the fade's start
 * (exactly), half way through it half the way to open, open from its end and for a broken position; the open stays
 * exactly open throughout. */
static void SkyVisibilityFadesOutTowardTheVolumesEdges(TestRun* run)
{
    float Bottom = -12.0f;
    float Top = Bottom + (float)(WORLD_INTERIOR_SKY_SLICE_COUNT * WORLD_INTERIOR_SKY_SLICE_HEIGHT);
    float Start = WORLD_INTERIOR_SKY_FADE_END - WORLD_INTERIOR_SKY_FADE_WIDTH;
    TEST_CHECK(run, (WORLD_INTERIOR_SKY_FADE_END < (float)(WORLD_INTERIOR_SKY_MAP_EXTENT * 0.5)) && (Start > 0.0f));
    TEST_CHECK(run, (WorldInterior_GetSkyEdgeFade((Vector3){ 3.0f, 5.0f, 4.0f }, Bottom) == 0.0f)
        && (WorldInterior_GetSkyEdgeFade((Vector3){ 0.0f, 0.0f, -Start }, Bottom) == 0.0f)
        && IsNear(WorldInterior_GetSkyEdgeFade((Vector3){ 0.6f * (Start + 0.5f * WORLD_INTERIOR_SKY_FADE_WIDTH), 0.0f,
            0.8f * (Start + 0.5f * WORLD_INTERIOR_SKY_FADE_WIDTH) }, Bottom), 0.5f)
        && (WorldInterior_GetSkyEdgeFade((Vector3){ WORLD_INTERIOR_SKY_FADE_END, -3.0f, 0.0f }, Bottom) == 1.0f)
        && (WorldInterior_GetSkyEdgeFade((Vector3){ 0.0f, 0.0f, 1000.0f }, Bottom) == 1.0f)
        && (WorldInterior_GetSkyEdgeFade((Vector3){ NAN, 0.0f, 0.0f }, Bottom) == 1.0f)
        && (WorldInterior_GetSkyEdgeFade((Vector3){ 0.0f, NAN, 0.0f }, Bottom) == 1.0f));
    TEST_CHECK(run, IsNear(WorldInterior_GetSkyEdgeFade((Vector3){ 0.0f, Bottom + 0.5f * WORLD_INTERIOR_SKY_WINDOW_FADE,
            0.0f }, Bottom), 0.5f)
        && (WorldInterior_GetSkyEdgeFade((Vector3){ 0.0f, Top, 0.0f }, Bottom) == 1.0f)
        && (WorldInterior_GetSkyEdgeFade((Vector3){ 0.0f, Bottom - 5.0f, 0.0f }, Bottom) == 1.0f)
        && (WorldInterior_GetSkyEdgeFade((Vector3){ 0.0f, Top - WORLD_INTERIOR_SKY_WINDOW_FADE, 0.0f }, Bottom) == 0.0f));
    // Under one big roof: covered near the camera, half covered half way through the fade, open beyond it.
    InteriorVolumeBox Roof[1] = { { .MinX = -60.0f, .MaxX = 60.0f, .MinY = 4.0f, .MaxY = 4.3f, .MinZ = -60.0f,
        .MaxZ = 60.0f } };
    InteriorVolume Hall = MakeVolume(Roof, 1U, true);
    Vector3 Up = { 0.0f, 1.0f, 0.0f };
    float Middle = Start + 0.5f * WORLD_INTERIOR_SKY_FADE_WIDTH;
    // (Near the map's edge the march leaves the map before the lowest band meets the roof: that band stays open there.)
    float Inside = GetVisibility(&Hall, (Vector3){ 0.0f, 0.0f, Start - 1.0f }, Up);
    float Half = GetVisibility(&Hall, (Vector3){ Middle, 0.0f, 0.0f }, Up);
    TEST_CHECK(run, (Inside < 0.1f) && (Half > 0.45f) && (Half < 0.6f)
        && (GetVisibility(&Hall, (Vector3){ 0.0f, 0.0f, -WORLD_INTERIOR_SKY_FADE_END }, Up) == 1.0f));
    InteriorVolume Field = MakeVolume(NULL, 0U, true);
    for (float Distance = 0.0f; Distance < 50.0f; Distance += 3.7f)
    {
        TEST_CHECK(run, GetVisibility(&Field, (Vector3){ Distance, 0.0f, 0.3f }, Up) == 1.0f);
    }
}

/* The share of the sky's light at a visibility: all of it in the open (exactly), the floor fully covered, linear between,
 * and clamped. */
static void TheSkyShareKeepsAFloor(TestRun* run)
{
    TEST_CHECK(run, WorldInterior_GetSkyShare(1.0f) == 1.0f);
    TEST_CHECK(run, IsNear(WorldInterior_GetSkyShare(0.0f), WORLD_INTERIOR_SKY_FLOOR));
    TEST_CHECK(run, IsNear(WorldInterior_GetSkyShare(0.5f), 0.5f * (1.0f + WORLD_INTERIOR_SKY_FLOOR)));
    TEST_CHECK(run, (WorldInterior_GetSkyShare(2.0f) == 1.0f) && IsNear(WorldInterior_GetSkyShare(-1.0f),
        WORLD_INTERIOR_SKY_FLOOR) && IsNear(WorldInterior_GetSkyShare(NAN), WORLD_INTERIOR_SKY_FLOOR));
}


// Tests: the camera.
/* The camera's interior: the first update sets it at once; later ones ease by the frame's time (the share
 * 1 - e^(-rate x seconds), a long frame counted short); no time holds it; a broken openness counts as the open. */
static void TheCameraEasesIndoors(TestRun* run)
{
    WorldInteriorCamera Eye;
    WorldInteriorCamera_Construct(&Eye);
    TEST_CHECK(run, !Eye.IsStarted && (Eye.Openness == 1.0f) && (Eye.Zones.SkyAmbientScale == 1.0f));
    WorldInteriorZoneLight Room = { .Ambient = { 0.01f, 0.01f, 0.01f }, .SkyAmbientScale = 0.5f, .ExposureBias = 1.0f,
        .Weight = 1.0f };
    WorldInteriorZoneLight Street = { .Ambient = { 0.0f, 0.0f, 0.0f }, .SkyAmbientScale = 1.0f, .ExposureBias = 0.0f,
        .Weight = 0.0f };
    WorldInteriorCamera_Update(&Eye, 0.0f, &Room, 0.5f);
    TEST_CHECK(run, Eye.IsStarted && (Eye.Openness == 0.0f) && (Eye.Zones.ExposureBias == 1.0f));
    WorldInteriorCamera_Update(&Eye, 1.0f, &Street, 0.05f);
    float Share = 1.0f - expf(-WORLD_INTERIOR_CAMERA_EASE_RATE * 0.05f);
    TEST_CHECK(run, IsNear(Eye.Openness, Share) && IsNear(Eye.Zones.ExposureBias, 1.0f - Share)
        && IsNear(Eye.Zones.SkyAmbientScale, 0.5f + 0.5f * Share));
    WorldInteriorCamera Held = Eye;
    WorldInteriorCamera_Update(&Eye, 1.0f, &Street, 0.0f);
    WorldInteriorCamera_Update(&Eye, 1.0f, &Street, NAN);
    TEST_CHECK(run, Eye.Openness == Held.Openness);
    // A stall counts as WORLD_INTERIOR_CAMERA_MAX_DELTA_SECONDS.
    WorldInteriorCamera Stalled = Held;
    WorldInteriorCamera Capped = Held;
    WorldInteriorCamera_Update(&Stalled, 1.0f, &Street, 10.0f);
    WorldInteriorCamera_Update(&Capped, 1.0f, &Street, WORLD_INTERIOR_CAMERA_MAX_DELTA_SECONDS);
    TEST_CHECK(run, Stalled.Openness == Capped.Openness);
    WorldInteriorCamera Fresh;
    WorldInteriorCamera_Construct(&Fresh);
    WorldInteriorCamera_Update(&Fresh, NAN, &Street, 0.1f);
    TEST_CHECK(run, Fresh.Openness == 1.0f);
    WorldInteriorCamera_Construct(&Fresh);
    WorldInteriorCamera_Update(&Fresh, 7.0f, &Street, 0.1f);
    TEST_CHECK(run, Fresh.Openness == 1.0f);
}

/* The light the eye is in: exactly the sky's before the camera started and in the open outside every zone; closed in,
 * the ambient's part at the floor's share and none of the sun's; a zone's own light added to the scene and to its
 * daylight part (lamp light is real light), and its sky scale taken off the ambient. */
static void TheCameraLightFollowsTheRoom(TestRun* run)
{
    WorldSkyLightFrame Light = { 0 };
    for (int Channel = 0; Channel < 3; Channel++)
    {
        Light.Sun[Channel] = 0.8f;
        Light.Sky[Channel] = 0.2f;
        Light.SunSky[Channel] = 0.15f;
        Light.Ground[Channel] = 0.1f;
        Light.AmbientTint[Channel] = 1.0f;
        Light.Night[Channel] = 0.004f;
    }
    float SkyScene = WorldSkyLight_GetSceneLuminance(&Light);
    float SkyDay = WorldSkyLight_GetDayLuminance(&Light);
    WorldInteriorCamera Eye;
    WorldInteriorCamera_Construct(&Eye);
    float Scene = 0.0f;
    float Day = 0.0f;
    WorldInterior_GetCameraLuminance(&Light, &Eye, &Scene, &Day);
    TEST_CHECK(run, (Scene == SkyScene) && (Day == SkyDay));
    WorldInteriorZoneLight Street = { .Ambient = { 0.0f, 0.0f, 0.0f }, .SkyAmbientScale = 1.0f, .ExposureBias = 0.0f,
        .Weight = 0.0f };
    WorldInteriorCamera_Update(&Eye, 1.0f, &Street, 0.1f);
    WorldInterior_GetCameraLuminance(&Light, &Eye, &Scene, &Day);
    TEST_CHECK(run, (Scene == SkyScene) && (Day == SkyDay));

    // Closed in: the ambient at the floor's share, no sun.
    WorldInteriorCamera Closed;
    WorldInteriorCamera_Construct(&Closed);
    WorldInteriorCamera_Update(&Closed, 0.0f, &Street, 0.1f);
    WorldInterior_GetCameraLuminance(&Light, &Closed, &Scene, &Day);
    WorldSkyLightFrame Shaded = Light;
    for (int Channel = 0; Channel < 3; Channel++)
    {
        Shaded.Sun[Channel] = 0.0f;
        Shaded.Sky[Channel] *= WORLD_INTERIOR_SKY_FLOOR;
        Shaded.SunSky[Channel] *= WORLD_INTERIOR_SKY_FLOOR;
        Shaded.Ground[Channel] *= WORLD_INTERIOR_SKY_FLOOR;
        Shaded.Night[Channel] *= WORLD_INTERIOR_SKY_FLOOR;
    }
    TEST_CHECK(run, IsNear(Scene, WorldSkyLight_GetSceneLuminance(&Shaded))
        && IsNear(Day, WorldSkyLight_GetDayLuminance(&Shaded)) && (Scene < 0.2f * SkyScene));

    // In a room with its own light and no sky: only the room's light, counted as daylight too.
    WorldInteriorZoneLight Cellar = { .Ambient = { 0.01f, 0.01f, 0.01f }, .SkyAmbientScale = 0.0f, .ExposureBias = 0.0f,
        .Weight = 1.0f };
    WorldInteriorCamera_Construct(&Closed);
    WorldInteriorCamera_Update(&Closed, 0.0f, &Cellar, 0.1f);
    WorldInterior_GetCameraLuminance(&Light, &Closed, &Scene, &Day);
    TEST_CHECK(run, IsNear(Scene, 0.01f) && IsNear(Day, 0.01f));
}


// Public functions.
void WorldInteriorTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_zone_component_has_its_defaults_and_rules", TheZoneComponentHasItsDefaultsAndRules },
        { "zones_are_placed_by_their_objects", ZonesArePlacedByTheirObjects },
        { "zone_weights_fade_in_and_face_in", ZoneWeightsFadeInAndFaceIn },
        { "overlapping_zones_follow_their_priority", OverlappingZonesFollowTheirPriority },
        { "draws_take_at_most_four_zones", DrawsTakeAtMostFourZones },
        { "the_haze_skips_the_path_inside_zones", TheHazeSkipsThePathInsideZones },
        { "the_sky_map_stands_on_its_texel_grid", TheSkyMapStandsOnItsTexelGrid },
        { "the_slices_follow_the_window", TheSlicesFollowTheWindow },
        { "the_march_coarsens_with_the_distance", TheMarchCoarsensWithTheDistance },
        { "sky_visibility_looks_around_through_the_volume", SkyVisibilityLooksAroundThroughTheVolume },
        { "sky_visibility_fades_out_toward_the_volumes_edges", SkyVisibilityFadesOutTowardTheVolumesEdges },
        { "the_overhead_read_sees_what_lies_above", TheOverheadReadSeesWhatLiesAbove },
        { "the_bands_hide_what_a_wall_spans", TheBandsHideWhatAWallSpans },
        { "the_sky_share_keeps_a_floor", TheSkyShareKeepsAFloor },
        { "the_camera_eases_indoors", TheCameraEasesIndoors },
        { "the_camera_light_follows_the_room", TheCameraLightFollowsTheRoom },
    };
    TestRun_RunSuite(run, "WorldInterior", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
