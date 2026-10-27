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
/* A stand-in for the sky's visibility map: the topmost surface's height at a point of the ground plane, from boxes
 * (each a footprint and a top) over a ground (NaN: nothing below them). */
typedef struct InteriorHeightBoxStruct
{
    float MinX;
    float MaxX;
    float MinZ;
    float MaxZ;
    float Top;
} InteriorHeightBox;

typedef struct InteriorHeightsStruct
{
    const InteriorHeightBox* Boxes;
    size_t BoxCount;
    /** The ground's height, or NaN for none. */
    float Ground;
    /** A slope of the ground: its height rises by this per metre along +X. */
    float GroundSlope;
} InteriorHeights;


// Static functions.
static bool IsNear(float a, float b)
{
    return fabsf(a - b) < TEST_INTERIOR_EPSILON;
}

static bool IsNearVector(Vector3 a, Vector3 b)
{
    return IsNear(a.x, b.x) && IsNear(a.y, b.y) && IsNear(a.z, b.z);
}

/* The topmost height of the stand-in at (x, z). */
static float GetHeight(const void* context, float x, float z)
{
    const InteriorHeights* Heights = context;
    float Top = isnan(Heights->Ground) ? NAN : Heights->Ground + Heights->GroundSlope * x;
    for (size_t Index = 0U; Index < Heights->BoxCount; Index++)
    {
        const InteriorHeightBox* Box = &Heights->Boxes[Index];
        if ((x >= Box->MinX) && (x <= Box->MaxX) && (z >= Box->MinZ) && (z <= Box->MaxZ)
            && (isnan(Top) || (Box->Top > Top)))
        {
            Top = Box->Top;
        }
    }
    return Top;
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

/* A surface's taps lie around the place it faces (the point on flat ground, the ring's radius in front of a wall), the
 * inner ring close, the outer one further; the height a tap must be topped by is the surface's own or its tangent
 * plane's, plus a bias by its facing (the full one facing up, a small one upright), and just below its own facing down. */
static void SkyTapsLieAroundTheFacedPlace(TestRun* run)
{
    Vector3 Floor = { 1.0f, 0.5f, -2.0f };
    Vector3 Up = { 0.0f, 1.0f, 0.0f };
    Vector2 Center = WorldInterior_GetSkyTap(Floor, Up, 0);
    Vector2 Inner = WorldInterior_GetSkyTap(Floor, Up, 1);
    Vector2 Outer = WorldInterior_GetSkyTap(Floor, Up, 1 + WORLD_INTERIOR_SKY_INNER_TAPS);
    TEST_CHECK(run, (Center.x == 1.0f) && (Center.y == -2.0f)
        && IsNear(Vector2Distance(Inner, Center), WORLD_INTERIOR_SKY_INNER_RADIUS)
        && IsNear(Outer.x, 1.0f + WORLD_INTERIOR_SKY_RING_RADIUS) && IsNear(Outer.y, -2.0f));
    // A tap out of range counts as the centre.
    Vector2 Wrong = WorldInterior_GetSkyTap(Floor, Up, WORLD_INTERIOR_SKY_TAP_COUNT);
    TEST_CHECK(run, (Wrong.x == Center.x) && (Wrong.y == Center.y));
    // A wall facing -Z: its place lies the ring's radius in front of it.
    Vector3 Facade = { 0.0f, 1.0f, 0.0f };
    Vector3 South = { 0.0f, 0.0f, -1.0f };
    Vector2 FacadeCenter = WorldInterior_GetSkyTap(Facade, South, 0);
    TEST_CHECK(run, IsNear(FacadeCenter.x, 0.0f) && IsNear(FacadeCenter.y, -WORLD_INTERIOR_SKY_RING_RADIUS));
    // The references: flat ground, an upright wall, a ceiling.
    TEST_CHECK(run, IsNear(WorldInterior_GetSkyTapReference(Floor, Up, Outer), 0.5f + WORLD_INTERIOR_SKY_BIAS));
    TEST_CHECK(run, IsNear(WorldInterior_GetSkyTapReference(Facade, South, FacadeCenter),
        1.0f + WORLD_INTERIOR_SKY_UPRIGHT_BIAS));
    TEST_CHECK(run, IsNear(WorldInterior_GetSkyTapReference(Floor, (Vector3){ 0.0f, -1.0f, 0.0f }, Center),
        0.5f - WORLD_INTERIOR_SKY_SELF));
    // A 30-degree slope rising toward -X: 1 m uphill its tangent plane lies tan(30) higher, plus the bias between the
    // upright one and the full one by the normal's upward part; downhill the surface's own height.
    Vector3 Slope = { 0.5f, 0.8660254f, 0.0f };
    float Bias = WORLD_INTERIOR_SKY_UPRIGHT_BIAS * (1.0f - 0.8660254f) + WORLD_INTERIOR_SKY_BIAS * 0.8660254f;
    TEST_CHECK(run, IsNear(WorldInterior_GetSkyTapReference(Floor, Slope, (Vector2){ 0.0f, -2.0f }),
        0.5f + 0.5773503f + Bias));
    TEST_CHECK(run, IsNear(WorldInterior_GetSkyTapReference(Floor, Slope, (Vector2){ 2.0f, -2.0f }), 0.5f + Bias));
}

/* The visibility on stand-ins for the map: the open field sees all of the sky (exactly 1); a room's floor, its corner and
 * its ceiling see none of it (the sky beyond its walls does not leak in); a facade under eaves keeps most of it; the
 * ground beside a building keeps part; a slope's uphill side and a curb are no roof. */
static void SkyVisibilityReadsTheMapAroundTheFacedPlace(TestRun* run)
{
    Vector3 Up = { 0.0f, 1.0f, 0.0f };
    InteriorHeights Open = { .Boxes = NULL, .BoxCount = 0U, .Ground = 0.0f, .GroundSlope = 0.0f };
    TEST_CHECK(run, WorldInterior_GetSkyVisibility(GetHeight, &Open, (Vector3){ 3.0f, 0.0f, 4.0f }, Up) == 1.0f);
    InteriorHeights Nothing = { .Boxes = NULL, .BoxCount = 0U, .Ground = NAN, .GroundSlope = 0.0f };
    TEST_CHECK(run, WorldInterior_GetSkyVisibility(GetHeight, &Nothing, (Vector3){ 3.0f, 0.0f, 4.0f }, Up) == 1.0f);

    // A building: a 4 x 4 m room (inside x, z in [-2, 2]) under a roof 3 m up whose eaves reach 0.5 m past its 0.2 m
    // walls; the walls and the roof are one footprint in the map.
    InteriorHeightBox Building[1] = { { .MinX = -2.7f, .MaxX = 2.7f, .MinZ = -2.7f, .MaxZ = 2.7f, .Top = 3.2f } };
    InteriorHeights Town = { .Boxes = Building, .BoxCount = 1U, .Ground = 0.0f, .GroundSlope = 0.0f };
    TEST_CHECK(run, WorldInterior_GetSkyVisibility(GetHeight, &Town, (Vector3){ 0.0f, 0.0f, 0.0f }, Up) == 0.0f);
    TEST_CHECK(run, WorldInterior_GetSkyVisibility(GetHeight, &Town, (Vector3){ 1.9f, 0.0f, 1.9f }, Up) == 0.0f);
    TEST_CHECK(run, WorldInterior_GetSkyVisibility(GetHeight, &Town, (Vector3){ 1.0f, 3.0f, 1.9f },
        (Vector3){ 0.0f, -1.0f, 0.0f }) == 0.0f);
    // The inside of a wall, facing in, near the ceiling: its place lies inside.
    TEST_CHECK(run, WorldInterior_GetSkyVisibility(GetHeight, &Town, (Vector3){ 2.0f, 2.95f, 0.0f },
        (Vector3){ -1.0f, 0.0f, 0.0f }) == 0.0f);
    // The facade under the eaves, facing +X: its place lies in front of them, open; only the ring's taps that reach back
    // under the roof are covered (three in the eight: at the wall and beside it).
    float Facade = WorldInterior_GetSkyVisibility(GetHeight, &Town, (Vector3){ 2.2f, 1.5f, 0.0f },
        (Vector3){ 1.0f, 0.0f, 0.0f });
    TEST_CHECK(run, Facade == 5.0f / 8.0f);
    // The ground 1 m beside the building: open itself, part of its ring under the roof.
    float Beside = WorldInterior_GetSkyVisibility(GetHeight, &Town, (Vector3){ 3.7f, 0.0f, 0.0f }, Up);
    TEST_CHECK(run, (Beside > 0.5f) && (Beside < 1.0f));
    // Under the eaves, 0.3 m in from their edge: the place itself is under the roof.
    TEST_CHECK(run, WorldInterior_GetSkyVisibility(GetHeight, &Town, (Vector3){ 2.4f, 0.0f, 0.0f }, Up) < 0.5f);

    // A 30-degree slope, rising toward +X: its own ground is no roof, uphill or down.
    InteriorHeights Hill = { .Boxes = NULL, .BoxCount = 0U, .Ground = 0.0f, .GroundSlope = 0.5773503f };
    Vector3 HillNormal = { -0.5f, 0.8660254f, 0.0f };
    TEST_CHECK(run, WorldInterior_GetSkyVisibility(GetHeight, &Hill, (Vector3){ 4.0f, 4.0f * 0.5773503f, 1.0f },
        HillNormal) == 1.0f);
    // A curb 0.1 m high under three of a pavement point's ring taps: no roof; a wall there 0.3 m high is one.
    InteriorHeightBox Curb[1] = { { .MinX = 1.0f, .MaxX = 1.6f, .MinZ = -10.0f, .MaxZ = 10.0f, .Top = 0.1f } };
    InteriorHeights Street = { .Boxes = Curb, .BoxCount = 1U, .Ground = 0.0f, .GroundSlope = 0.0f };
    TEST_CHECK(run, WorldInterior_GetSkyVisibility(GetHeight, &Street, (Vector3){ 0.0f, 0.0f, 0.0f }, Up) == 1.0f);
    Curb[0].Top = 0.3f;
    TEST_CHECK(run, WorldInterior_GetSkyVisibility(GetHeight, &Street, (Vector3){ 0.0f, 0.0f, 0.0f }, Up) == 5.0f / 8.0f);
}

/* Toward the map's edge the visibility fades to open: none within the fade's start (exactly), half way through it half
 * the way to open, open from its end (inside the map's box with every tap) and for a broken position; the open stays
 * exactly open throughout. */
static void SkyVisibilityFadesOutTowardTheMapsEdge(TestRun* run)
{
    float Start = WORLD_INTERIOR_SKY_FADE_END - WORLD_INTERIOR_SKY_FADE_WIDTH;
    TEST_CHECK(run, (WORLD_INTERIOR_SKY_FADE_END + 2.0f * WORLD_INTERIOR_SKY_RING_RADIUS
        <= (float)(WORLD_INTERIOR_SKY_MAP_EXTENT * 0.5)) && (Start > 0.0f));
    TEST_CHECK(run, (WorldInterior_GetSkyEdgeFade((Vector3){ 3.0f, 50.0f, 4.0f }) == 0.0f)
        && (WorldInterior_GetSkyEdgeFade((Vector3){ 0.0f, 0.0f, -Start }) == 0.0f)
        && IsNear(WorldInterior_GetSkyEdgeFade((Vector3){ 0.6f * (Start + 0.5f * WORLD_INTERIOR_SKY_FADE_WIDTH), 0.0f,
            0.8f * (Start + 0.5f * WORLD_INTERIOR_SKY_FADE_WIDTH) }), 0.5f)
        && (WorldInterior_GetSkyEdgeFade((Vector3){ WORLD_INTERIOR_SKY_FADE_END, -3.0f, 0.0f }) == 1.0f)
        && (WorldInterior_GetSkyEdgeFade((Vector3){ 0.0f, 0.0f, 1000.0f }) == 1.0f)
        && (WorldInterior_GetSkyEdgeFade((Vector3){ NAN, 0.0f, 0.0f }) == 1.0f));
    // Under one big roof: covered near the camera, half covered half way through the fade, open beyond it.
    InteriorHeightBox Roof[1] = { { .MinX = -60.0f, .MaxX = 60.0f, .MinZ = -60.0f, .MaxZ = 60.0f, .Top = 4.0f } };
    InteriorHeights Hall = { .Boxes = Roof, .BoxCount = 1U, .Ground = 0.0f, .GroundSlope = 0.0f };
    Vector3 Up = { 0.0f, 1.0f, 0.0f };
    float Middle = Start + 0.5f * WORLD_INTERIOR_SKY_FADE_WIDTH;
    TEST_CHECK(run, (WorldInterior_GetSkyVisibility(GetHeight, &Hall, (Vector3){ 0.0f, 0.0f, Start - 1.0f }, Up) == 0.0f)
        && IsNear(WorldInterior_GetSkyVisibility(GetHeight, &Hall, (Vector3){ Middle, 0.0f, 0.0f }, Up), 0.5f)
        && (WorldInterior_GetSkyVisibility(GetHeight, &Hall, (Vector3){ 0.0f, 0.0f, -WORLD_INTERIOR_SKY_FADE_END }, Up)
            == 1.0f));
    InteriorHeights Field = { .Boxes = NULL, .BoxCount = 0U, .Ground = 0.0f, .GroundSlope = 0.0f };
    for (float Distance = 0.0f; Distance < 50.0f; Distance += 0.7f)
    {
        TEST_CHECK(run, WorldInterior_GetSkyVisibility(GetHeight, &Field, (Vector3){ Distance, 0.0f, 0.3f }, Up) == 1.0f);
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
        { "sky_taps_lie_around_the_faced_place", SkyTapsLieAroundTheFacedPlace },
        { "sky_visibility_reads_the_map_around_the_faced_place", SkyVisibilityReadsTheMapAroundTheFacedPlace },
        { "sky_visibility_fades_out_toward_the_maps_edge", SkyVisibilityFadesOutTowardTheMapsEdge },
        { "the_sky_share_keeps_a_floor", TheSkyShareKeepsAFloor },
        { "the_camera_eases_indoors", TheCameraEasesIndoors },
        { "the_camera_light_follows_the_room", TheCameraLightFollowsTheRoom },
    };
    TestRun_RunSuite(run, "WorldInterior", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
