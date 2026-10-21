#include "TestSuites.h"
#include "TestAssets.h"
#include "TestWorld.h"
#include "CollisionGeometry.h"
#include "CollisionMath.h"
#include "Logger.h"
#include "World.h"
#include "WorldColliderComponent.h"
#include "WorldCollision.h"
#include "WorldModelComponent.h"
#include "raylib/raymath.h"
#include "wr/WRFileSystem.h"
#include "wr/WRPath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** Tolerance for distances the narrowphase computes (its own tolerances are about 1e-4 m). */
#define TEST_EPSILON 2e-3f
/** Room for paths and a log file. */
#define COLLISION_TEST_TEXT_CAPACITY 4096
/** The seeded random scene: objects, queries and its half size. */
#define RANDOM_OBJECT_COUNT 150U
#define RANDOM_QUERY_COUNT 300U
#define RANDOM_SCENE_HALF_SIZE 20.0f


// Types.
/* A world with assets and a collision world over it. */
typedef struct CollisionSceneStruct
{
    TestAssetFixture Fixture;
    World World;
    KeyRegistry Keys;
    WorldCollision* Collision;
    bool IsWorldBuilt;
} CollisionScene;

/* One object's own shape placed as the collision world places it (the brute force of the tree test). */
typedef struct PlacedOwnShapeStruct
{
    uint64_t Id;
    CollisionShape Shape;
    CollisionAffine ToWorld;
    CollisionAffine FromWorld;
} PlacedOwnShape;


// Fields.
/* A flat 10 x 10 m terrain patch at y = 0, two triangles facing up. */
static const float PatchVertices[] = { -5.0f, 0.0f, -5.0f, 5.0f, 0.0f, -5.0f, 5.0f, 0.0f, 5.0f, -5.0f, 0.0f, 5.0f };
static const unsigned short PatchIndices[] = { 0, 2, 1, 0, 3, 2 };


// Static functions: helpers.
static bool IsNear(float a, float b)
{
    return fabsf(a - b) < TEST_EPSILON;
}

static bool IsNearVector(Vector3 a, Vector3 b)
{
    return IsNear(a.x, b.x) && IsNear(a.y, b.y) && IsNear(a.z, b.z);
}

static uint32_t NextRandom(uint32_t* state)
{
    *state = (*state * 1664525U) + 1013904223U;
    return *state >> 8U;
}

static float RandomRange(uint32_t* state, float minimum, float maximum)
{
    return minimum + ((maximum - minimum) * ((float)NextRandom(state) / 16777216.0f));
}

static Vector3 RandomVector(uint32_t* state, float halfSize)
{
    return (Vector3){ RandomRange(state, -halfSize, halfSize), RandomRange(state, -halfSize, halfSize),
        RandomRange(state, -halfSize, halfSize) };
}

/* Counts the occurrences of @p needle in "<directory>/logs/latest.log". */
static size_t CountInLatestLog(TestRun* run, const unsigned char* directory, const char* needle)
{
    size_t Found = 0U;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, COLLISION_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, COLLISION_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, Path_Append(directory, (const unsigned char*)u8"logs/latest.log", &Path))
        && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, &Text))
        && TEST_CHECK(run, GenericBuffer_NullTerminate(&Text)))
    {
        for (const char* At = strstr((const char*)Text._data, needle); At != NULL; At = strstr(At + 1, needle))
        {
            Found++;
        }
    }
    Memory_Free(Text._data);
    Memory_Free(Path._data);
    return Found;
}


// Static functions: scenes.
/* The fixture, its world (with the fixture's assets and @p logger) and a collision world; @p options may be NULL. */
static bool CollisionScene_Construct(TestRun* run, CollisionScene* self, Logger* logger,
    const WorldCollisionOptions* options)
{
    Memory_Zero(self, sizeof(*self));
    if (!TestAssetFixture_Construct(run, &self->Fixture))
    {
        return false;
    }
    self->IsWorldBuilt = TestWorld_Construct(run, &self->World, &self->Keys, self->Fixture.Manager, logger);
    return self->IsWorldBuilt && TEST_CHECK_SUCCESS(run, WorldCollision_Create(&self->World, self->Fixture.Manager,
        logger, options, &self->Collision));
}

static void CollisionScene_Deconstruct(TestRun* run, CollisionScene* self)
{
    WorldCollision_Destroy(self->Collision);
    if (self->IsWorldBuilt)
    {
        TestWorld_Deconstruct(run, &self->World, &self->Keys);
    }
    TestAssetFixture_Deconstruct(run, &self->Fixture);
}

static WorldObject* AddObject(TestRun* run, World* world, Vector3 position, WorldMobility mobility)
{
    WorldObject* Object = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(world, (const unsigned char*)u8"thing", &Object)))
    {
        return NULL;
    }
    TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, position));
    TEST_CHECK_SUCCESS(run, WorldObject_SetMobility(Object, mobility));
    return Object;
}

static WorldColliderComponent* AddCollider(TestRun* run, World* world, const WorldObject* object)
{
    WorldComponent* Component = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_AddComponent(world, WorldObject_GetId(object), WorldComponentType_Collider,
        &Component)))
    {
        return NULL;
    }
    return WorldColliderComponent_FromComponent(Component);
}

/* A collider that uses @p shape as its own primitive. */
static WorldColliderComponent* AddOwnShape(TestRun* run, World* world, const WorldObject* object,
    const CollisionShape* shape)
{
    WorldColliderComponent* Collider = AddCollider(run, world, object);
    if ((Collider == NULL) || !TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOwnShape(Collider, shape))
        || !TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetUsesOwnShape(Collider, true)))
    {
        return NULL;
    }
    return Collider;
}

static bool AddWorldModel(TestRun* run, World* world, const WorldObject* object, const char* worldModel)
{
    WorldComponent* Component = NULL;
    return TEST_CHECK_SUCCESS(run, World_AddComponent(world, WorldObject_GetId(object), WorldComponentType_Model,
        &Component)) && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(
        WorldModelComponent_FromComponent(Component), (const unsigned char*)worldModel));
}

static CollisionShape MakeSphere(float radius)
{
    CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Sphere);
    Shape.Radius = radius;
    return Shape;
}

static CollisionShape MakeBox(Vector3 halfExtents)
{
    CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Box);
    Shape.HalfExtents = halfExtents;
    return Shape;
}

/* Overrides a collider's layer (and, when @p isTrigger, makes it a trigger). */
static void SetLayer(TestRun* run, WorldColliderComponent* collider, CollisionLayer layer, bool isTrigger)
{
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOverridden(collider, WorldColliderSetting_Layer, true));
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetLayer(collider, layer));
    if (isTrigger)
    {
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOverridden(collider, WorldColliderSetting_IsTrigger, true));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetIsTrigger(collider, true));
    }
}

/* The distance of a ray's nearest hit, or -1 when it hits nothing. */
static float CastRay(WorldCollision* collision, Vector3 origin, Vector3 direction, const WorldCollisionFilter* filter,
    WorldRayHit* outHit)
{
    WorldRayHit Hit;
    if (!WorldCollision_Raycast(collision, origin, direction, 100.0f, filter, &Hit))
    {
        return -1.0f;
    }
    if (outHit != NULL)
    {
        *outHit = Hit;
    }
    return Hit.Distance;
}

/* The distance down to the nearest hit below (x, 10, z), or -1. */
static float CastDown(WorldCollision* collision, float x, float z)
{
    return CastRay(collision, (Vector3){ x, 10.0f, z }, (Vector3){ 0.0f, -1.0f, 0.0f }, NULL, NULL);
}

static bool AreIdsEqual(const GenericBuffer* ids, const uint64_t* expected, size_t expectedCount)
{
    if (ids->_count != expectedCount)
    {
        return false;
    }
    const uint64_t* Found = (const uint64_t*)(const void*)ids->_data;
    for (size_t Index = 0U; Index < expectedCount; Index++)
    {
        if (Found[Index] != expected[Index])
        {
            return false;
        }
    }
    return true;
}

/* The world models of the sync tests: two sets of a crate, and another crate. */
static bool AddCrates(TestRun* run, TestAssetFixture* fixture)
{
    BoundingBox Box = { .min = { -0.5f, 0.0f, -0.5f }, .max = { 0.5f, 1.0f, 0.5f } };
    return TestAssets_AddModel(run, fixture, "crate_mesh", Box, NULL)
        && TestAssets_AddWorldModel(run, fixture, "{\"name\": \"crate\", \"lods\": [{\"model\": \"crate_mesh\", "
            "\"max_distance\": 50}], \"collision\": {\"sets\": ["
            "{\"name\": \"closed\", \"shapes\": [{\"type\": \"box\", \"center\": [0, 0.5, 0], "
                "\"half_extents\": [0.5, 0.5, 0.5]}]},"
            "{\"name\": \"open\", \"shapes\": [{\"type\": \"box\", \"center\": [1, 0.5, 0], "
                "\"half_extents\": [0.1, 0.5, 0.5]}]}]}}")
        && TestAssets_AddWorldModel(run, fixture, "{\"name\": \"tall_crate\", \"lods\": [{\"model\": \"crate_mesh\", "
            "\"max_distance\": 50}], \"collision\": {\"sets\": [{\"name\": \"closed\", \"shapes\": [{\"type\": \"box\", "
            "\"center\": [0, 1, 0], \"half_extents\": [0.5, 1, 0.5]}]}]}}");
}


// Static functions: sync.
static void SyncFollowsTheWorld(TestRun* run)
{
    CollisionScene Scene;
    if (!CollisionScene_Construct(run, &Scene, NULL, NULL) || !AddCrates(run, &Scene.Fixture))
    {
        CollisionScene_Deconstruct(run, &Scene);
        return;
    }
    WorldCollision* Collision = Scene.Collision;
    WorldObject* Crate = AddObject(run, &Scene.World, (Vector3){ 0.0f, 0.0f, 0.0f }, WorldMobility_Movable);
    WorldColliderComponent* Collider = (Crate != NULL) ? AddCollider(run, &Scene.World, Crate) : NULL;
    if ((Collider == NULL) || !AddWorldModel(run, &Scene.World, Crate, "crate"))
    {
        CollisionScene_Deconstruct(run, &Scene);
        return;
    }
    TEST_CHECK(run, CastDown(Collision, 0.0f, 0.0f) < 0.0f); // nothing before the first sync
    WorldCollision_Sync(Collision);
    WorldCollisionStats Stats = WorldCollision_GetStats(Collision);
    TEST_CHECK(run, (Stats.InstanceCount == 1U) && (Stats.MovableInstanceCount == 1U) && (Stats.ShapeCount == 1U)
        && (Stats.RebuildCount == 1U) && (Stats.MoveCount == 0U) && (Stats.Tick == 1U));
    WorldRayHit Hit;
    TEST_CHECK(run, IsNear(CastRay(Collision, (Vector3){ 0.0f, 10.0f, 0.0f }, (Vector3){ 0.0f, -2.0f, 0.0f }, NULL,
        &Hit), 9.0f));
    TEST_CHECK(run, (Hit.Shape.ObjectId == WorldObject_GetId(Crate)) && (Hit.Shape.ShapeIndex == 0U)
        && (Hit.Shape.ShapeType == CollisionShapeType_Box) && (Hit.Shape.Layer == CollisionLayer_Prop)
        && !Hit.Shape.IsTrigger && !Hit.Shape.HasTriangle && IsNearVector(Hit.Normal, (Vector3){ 0.0f, 1.0f, 0.0f })
        && IsNearVector(Hit.Point, (Vector3){ 0.0f, 1.0f, 0.0f }) && IsNear(Hit.Fraction, 0.09f) && !Hit.IsStartInside);

    // A move moves the shapes without rebuilding them; a sync without changes does nothing.
    TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Crate, (Vector3){ 10.0f, 0.0f, 0.0f }));
    WorldCollision_Sync(Collision);
    WorldCollision_Sync(Collision);
    Stats = WorldCollision_GetStats(Collision);
    TEST_CHECK(run, (Stats.RebuildCount == 1U) && (Stats.MoveCount == 1U) && (Stats.Tick == 3U));
    TEST_CHECK(run, IsNear(CastDown(Collision, 10.0f, 0.0f), 9.0f) && (CastDown(Collision, 0.0f, 0.0f) < 0.0f));

    // Switching sets swaps the shapes (a door's closed and open hitboxes).
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOverridden(Collider, WorldColliderSetting_CollisionSet, true));
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetCollisionSet(Collider, (const unsigned char*)u8"open"));
    WorldCollision_Sync(Collision);
    TEST_CHECK(run, WorldCollision_GetStats(Collision).RebuildCount == 2U);
    TEST_CHECK(run, (CastDown(Collision, 10.0f, 0.0f) < 0.0f) && IsNear(CastDown(Collision, 11.0f, 0.0f), 9.0f));

    // Another world model: its shapes.
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOverridden(Collider, WorldColliderSetting_CollisionSet, false));
    WorldModelComponent* ModelComponent = WorldModelComponent_FromComponent(WorldObject_GetComponent(Crate,
        WorldComponentType_Model));
    TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(ModelComponent, (const unsigned char*)u8"tall_crate"));
    WorldCollision_Sync(Collision);
    TEST_CHECK(run, IsNear(CastDown(Collision, 10.0f, 0.0f), 8.0f));

    // Disabled (the collider, the object or an ancestor): out of the collision world until enabled again.
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetEnabled(Collider, false));
    WorldCollision_Sync(Collision);
    TEST_CHECK(run, (WorldCollision_GetStats(Collision).InstanceCount == 0U) && (CastDown(Collision, 10.0f, 0.0f) < 0.0f));
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetEnabled(Collider, true));
    WorldCollision_Sync(Collision);
    TEST_CHECK(run, IsNear(CastDown(Collision, 10.0f, 0.0f), 8.0f));
    WorldObject* Parent = AddObject(run, &Scene.World, (Vector3){ 0.0f, 0.0f, 0.0f }, WorldMobility_Movable);
    if ((Parent != NULL) && TEST_CHECK_SUCCESS(run, World_SetParent(&Scene.World, WorldObject_GetId(Crate),
        WorldObject_GetId(Parent), true)))
    {
        TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(Parent, false));
        WorldCollision_Sync(Collision);
        TEST_CHECK(run, CastDown(Collision, 10.0f, 0.0f) < 0.0f);
        TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(Parent, true));
        WorldCollision_Sync(Collision);
        TEST_CHECK(run, IsNear(CastDown(Collision, 10.0f, 0.0f), 8.0f));
        // A parent's move moves the child's collider.
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Parent, (Vector3){ 0.0f, 0.0f, 5.0f }));
        WorldCollision_Sync(Collision);
        TEST_CHECK(run, IsNear(CastDown(Collision, 10.0f, 5.0f), 8.0f) && (CastDown(Collision, 10.0f, 0.0f) < 0.0f));
    }

    // A replaced collider starts afresh, even at its predecessor's address with the revision it had.
    TEST_CHECK_SUCCESS(run, World_RemoveComponent(&Scene.World, WorldObject_GetId(Crate), WorldComponentType_Collider));
    WorldColliderComponent* Own = AddCollider(run, &Scene.World, Crate);
    if (Own != NULL)
    {
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetUsesOwnShape(Own, true)); // a box of 0.5 at the origin
        WorldCollision_Sync(Collision);
        TEST_CHECK(run, IsNear(CastDown(Collision, 10.0f, 5.0f), 9.5f));
        uint32_t Revision = WorldComponent_GetRevision(&Own->Base);
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&Scene.World, WorldObject_GetId(Crate),
            WorldComponentType_Collider));
        WorldColliderComponent* Replacement = AddCollider(run, &Scene.World, Crate);
        if (Replacement != NULL)
        {
            TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetRotationMode(Replacement, WorldColliderRotationMode_None));
            TEST_CHECK(run, WorldComponent_GetRevision(&Replacement->Base) == Revision);
            uint64_t RebuildsBefore = WorldCollision_GetStats(Collision).RebuildCount;
            WorldCollision_Sync(Collision);
            TEST_CHECK(run, IsNear(CastDown(Collision, 10.0f, 5.0f), 8.0f)
                && (WorldCollision_GetStats(Collision).RebuildCount == (RebuildsBefore + 1U)));
        }
    }

    // Without a world model and an own shape there is nothing to collide.
    TEST_CHECK_SUCCESS(run, World_RemoveComponent(&Scene.World, WorldObject_GetId(Crate), WorldComponentType_Model));
    WorldCollision_Sync(Collision);
    TEST_CHECK(run, (WorldCollision_GetStats(Collision).InstanceCount == 0U) && (CastDown(Collision, 10.0f, 5.0f) < 0.0f));
    // Removing the object removes its instance.
    TEST_CHECK_SUCCESS(run, World_RemoveObject(&Scene.World, WorldObject_GetId(Parent)));
    WorldCollision_Sync(Collision);
    TEST_CHECK(run, WorldCollision_GetStats(Collision).InstanceCount == 0U);
    CollisionScene_Deconstruct(run, &Scene);
}

static void CollidersFollowTheirRotationModeAndTransform(TestRun* run)
{
    CollisionScene Scene;
    if (!CollisionScene_Construct(run, &Scene, NULL, NULL))
    {
        CollisionScene_Deconstruct(run, &Scene);
        return;
    }
    WorldCollision* Collision = Scene.Collision;
    WorldCollision_SetDebugListEnabled(Collision, true);
    CollisionShape Board = MakeBox((Vector3){ 1.0f, 0.25f, 0.5f });
    WorldObject* Turned = AddObject(run, &Scene.World, (Vector3){ 0.0f, 0.0f, 0.0f }, WorldMobility_Movable);
    WorldColliderComponent* Collider = (Turned != NULL) ? AddOwnShape(run, &Scene.World, Turned, &Board) : NULL;
    if (Collider == NULL)
    {
        CollisionScene_Deconstruct(run, &Scene);
        return;
    }
    Vector3 FromFront = { 0.0f, 0.0f, 5.0f };
    Vector3 Back = { 0.0f, 0.0f, -1.0f };
    // A yaw of 90 degrees: the board's X (1 m) lies along the world's Z, unless the mode keeps it axis-aligned.
    TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(Turned, (Vector3){ 0.0f, PI * 0.5f, 0.0f }));
    WorldCollision_Sync(Collision);
    TEST_CHECK(run, IsNear(CastRay(Collision, FromFront, Back, NULL, NULL), 4.0f));
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetRotationMode(Collider, WorldColliderRotationMode_YawOnly));
    WorldCollision_Sync(Collision);
    TEST_CHECK(run, IsNear(CastRay(Collision, FromFront, Back, NULL, NULL), 4.0f));
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetRotationMode(Collider, WorldColliderRotationMode_None));
    WorldCollision_Sync(Collision);
    TEST_CHECK(run, IsNear(CastRay(Collision, FromFront, Back, NULL, NULL), 4.5f));

    // A pitch of 90 degrees: the board's Z (0.5 m) stands up in full mode; yaw_only and none keep it flat.
    TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(Turned, (Vector3){ PI * 0.5f, 0.0f, 0.0f }));
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetRotationMode(Collider, WorldColliderRotationMode_Full));
    WorldCollision_Sync(Collision);
    TEST_CHECK(run, IsNear(CastDown(Collision, 0.0f, 0.0f), 9.5f));
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetRotationMode(Collider, WorldColliderRotationMode_YawOnly));
    WorldCollision_Sync(Collision);
    TEST_CHECK(run, IsNear(CastDown(Collision, 0.0f, 0.0f), 9.75f));

    // yaw_only keeps exactly the yaw of the object's forward (its local +Z in the world), and stays upright.
    TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(Turned, (Vector3){ 0.3f, 0.7f, 0.0f }));
    WorldCollision_Sync(Collision);
    Matrix ObjectWorld = WorldObject_GetWorldMatrix(Turned);
    float Yaw = atan2f(ObjectWorld.m8, ObjectWorld.m10);
    size_t DebugCount = 0U;
    const WorldCollisionDebugShape* Debug = WorldCollision_GetDebugShapes(Collision, &DebugCount);
    if (TEST_CHECK(run, (Debug != NULL) && (DebugCount == 1U)))
    {
        Matrix Placed = Debug[0].Transform;
        TEST_CHECK(run, (fabsf(Yaw - 0.7f) > 0.01f) // the pitch turns the forward too: not the Euler angle
            && IsNearVector((Vector3){ Placed.m0, Placed.m1, Placed.m2 }, (Vector3){ cosf(Yaw), 0.0f, -sinf(Yaw) })
            && IsNearVector((Vector3){ Placed.m4, Placed.m5, Placed.m6 }, (Vector3){ 0.0f, 1.0f, 0.0f }));
        TEST_CHECK(run, (Debug[0].Shape->Type == CollisionShapeType_Box) && !Debug[0].IsStatic && !Debug[0].IsTrigger
            && (Debug[0].Layer == CollisionLayer_Prop));
    }

    // A mirrored object: yaw_only and none keep the mirror where full has it (the turn they keep is a proper rotation).
    TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(Turned, (Vector3){ 0.0f, 0.0f, 0.0f }));
    TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Turned, (Vector3){ 1.0f, 1.0f, -1.0f }));
    CollisionShape Offset = MakeBox((Vector3){ 0.25f, 0.25f, 0.25f });
    Offset.Center = (Vector3){ 1.0f, 0.0f, 2.0f };
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOwnShape(Collider, &Offset));
    static const WorldColliderRotationMode Modes[] =
    {
        WorldColliderRotationMode_Full, WorldColliderRotationMode_YawOnly, WorldColliderRotationMode_None
    };
    for (size_t Mode = 0U; Mode < (sizeof(Modes) / sizeof(Modes[0])); Mode++)
    {
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetRotationMode(Collider, Modes[Mode]));
        WorldCollision_Sync(Collision);
        Debug = WorldCollision_GetDebugShapes(Collision, &DebugCount);
        TEST_CHECK(run, (DebugCount == 1U) && IsNearVector((Vector3){ Debug[0].Transform.m12, Debug[0].Transform.m13,
            Debug[0].Transform.m14 }, (Vector3){ 1.0f, 0.0f, -2.0f }));
    }
    TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Turned, (Vector3){ 1.0f, 1.0f, 1.0f }));
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetRotationMode(Collider, WorldColliderRotationMode_YawOnly));

    // The collider's own transform: scale, then rotation, then offset, on the object's.
    TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(Turned, (Vector3){ 0.0f, 0.0f, 0.0f }));
    CollisionShape Cube = MakeBox((Vector3){ 0.5f, 0.5f, 0.5f });
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOwnShape(Collider, &Cube));
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetLocalTransform(Collider, (Vector3){ 0.0f, 1.0f, 0.0f },
        (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 2.0f, 1.0f, 1.0f }));
    WorldCollision_Sync(Collision);
    TEST_CHECK(run, IsNear(CastRay(Collision, (Vector3){ 5.0f, 1.0f, 0.0f }, (Vector3){ -1.0f, 0.0f, 0.0f }, NULL,
        NULL), 4.0f) && IsNear(CastDown(Collision, 0.0f, 0.0f), 8.5f));

    // An uneven scale makes a sphere an ellipsoid (semi-axes 2, 1, 1), exactly.
    CollisionShape Ball = MakeSphere(1.0f);
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOwnShape(Collider, &Ball));
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetLocalTransform(Collider, (Vector3){ 0.0f, 0.0f, 0.0f },
        (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 2.0f, 1.0f, 1.0f }));
    WorldCollision_Sync(Collision);
    Vector3 Left = { -1.0f, 0.0f, 0.0f };
    TEST_CHECK(run, IsNear(CastRay(Collision, (Vector3){ 5.0f, 0.0f, 0.0f }, Left, NULL, NULL), 3.0f)
        && IsNear(CastDown(Collision, 0.0f, 0.0f), 9.0f)
        && IsNear(CastRay(Collision, (Vector3){ 5.0f, 0.8f, 0.0f }, Left, NULL, NULL), 3.8f));
    GenericBuffer Ids;
    GenericBuffer_AllocateVariable(&Ids, 4U, sizeof(uint64_t));
    TEST_CHECK(run, WorldCollision_OverlapSphere(Collision, (Vector3){ 1.9f, 0.0f, 0.0f }, 0.05f, NULL, &Ids) == 1U);
    GenericBuffer_Clear(&Ids);
    TEST_CHECK(run, WorldCollision_OverlapSphere(Collision, (Vector3){ 0.0f, 1.1f, 0.0f }, 0.05f, NULL, &Ids) == 0U);
    Memory_Free(Ids._data);

    // A parent's uneven scale stretches its child's sphere the same way (semi-axes 0.5, 1.5, 0.5 around y = 3).
    WorldObject* Tall = AddObject(run, &Scene.World, (Vector3){ 20.0f, 0.0f, 0.0f }, WorldMobility_Movable);
    WorldObject* Child = AddObject(run, &Scene.World, (Vector3){ 0.0f, 0.0f, 0.0f }, WorldMobility_Movable);
    CollisionShape Small = MakeSphere(0.5f);
    if ((Tall != NULL) && (Child != NULL) && (AddOwnShape(run, &Scene.World, Child, &Small) != NULL)
        && TEST_CHECK_SUCCESS(run, World_SetParent(&Scene.World, WorldObject_GetId(Child), WorldObject_GetId(Tall),
            false)))
    {
        TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Tall, (Vector3){ 1.0f, 3.0f, 1.0f }));
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Child, (Vector3){ 0.0f, 1.0f, 0.0f }));
        WorldCollision_Sync(Collision);
        TEST_CHECK(run, IsNear(CastDown(Collision, 20.0f, 0.0f), 5.5f));
    }
    CollisionScene_Deconstruct(run, &Scene);
}


/* Own shapes stay right when the instances' buffer grows (a later spawn), and a query with an ignored subtree between a
 * removal and the next sync reads nothing freed; queries see the colliders as of the last sync. */
static void OwnShapesSurviveGrowthAndQueriesSeeTheLastSync(TestRun* run)
{
    CollisionScene Scene;
    if (!CollisionScene_Construct(run, &Scene, NULL, NULL))
    {
        CollisionScene_Deconstruct(run, &Scene);
        return;
    }
    WorldCollision* Collision = Scene.Collision;
    CollisionShape Ball = MakeSphere(0.5f);
    WorldObject* First[20];
    for (int Index = 0; Index < 20; Index++)
    {
        First[Index] = AddObject(run, &Scene.World, (Vector3){ 2.0f * (float)Index, 0.0f, 0.0f }, WorldMobility_Movable);
        if ((First[Index] == NULL) || (AddOwnShape(run, &Scene.World, First[Index], &Ball) == NULL))
        {
            CollisionScene_Deconstruct(run, &Scene);
            return;
        }
        if (Index == 15)
        {
            WorldCollision_Sync(Collision); // 16 instances, then 4 more: the instances' buffer grows
        }
    }
    WorldCollision_Sync(Collision);
    TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(First[3], (Vector3){ 6.0f, 0.0f, 5.0f })); // moved after the growth
    WorldCollision_Sync(Collision);
    bool AreAllRight = IsNear(CastDown(Collision, 6.0f, 5.0f), 9.5f);
    for (int Index = 0; Index < 20; Index++)
    {
        float Expected = (Index == 3) ? -1.0f : 9.5f;
        float Distance = CastDown(Collision, 2.0f * (float)Index, 0.0f);
        AreAllRight = AreAllRight && ((Expected < 0.0f) ? (Distance < 0.0f) : IsNear(Distance, Expected));
    }
    TEST_CHECK(run, AreAllRight);

    // A child of the first ball, removed before the next sync: still found as synced, also through a subtree filter.
    WorldObject* Child = AddObject(run, &Scene.World, (Vector3){ 0.0f, 0.0f, 3.0f }, WorldMobility_Movable);
    if ((Child != NULL) && (AddOwnShape(run, &Scene.World, Child, &Ball) != NULL)
        && TEST_CHECK_SUCCESS(run, World_SetParent(&Scene.World, WorldObject_GetId(Child), WorldObject_GetId(First[0]),
            true)))
    {
        WorldCollision_Sync(Collision);
        uint64_t ChildId = WorldObject_GetId(Child);
        WorldCollisionFilter Filter = WorldCollisionFilter_Create(COLLISION_LAYER_MASK_ALL,
            WorldCollisionTriggerMode_Ignore);
        Filter.IgnoredObjectId = WorldObject_GetId(First[0]);
        Filter.IsIgnoredSubtree = true;
        Vector3 Above = { 0.0f, 10.0f, 3.0f };
        Vector3 Down = { 0.0f, -1.0f, 0.0f };
        TEST_CHECK(run, CastRay(Collision, Above, Down, &Filter, NULL) < 0.0f); // the child is in the ignored subtree
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&Scene.World, ChildId));
        WorldRayHit Hit;
        TEST_CHECK(run, (CastRay(Collision, Above, Down, &Filter, &Hit) > 0.0f) && (Hit.Shape.ObjectId == ChildId));
        WorldCollision_Sync(Collision);
        TEST_CHECK(run, CastRay(Collision, Above, Down, NULL, NULL) < 0.0f);
    }
    CollisionScene_Deconstruct(run, &Scene);
}


// Static functions: the trees against brute force.
static void PlaceOwnShape(const WorldObject* object, const WorldColliderComponent* collider, PlacedOwnShape* outPlaced)
{
    outPlaced->Id = WorldObject_GetId(object);
    outPlaced->Shape = WorldColliderComponent_GetOwnShape(collider);
    Matrix Frame = MatrixMultiply(WorldColliderComponent_GetLocalTransform(collider), WorldObject_GetWorldMatrix(object));
    outPlaced->ToWorld = CollisionAffine_FromMatrix(MatrixMultiply(CollisionShape_GetTransform(&outPlaced->Shape),
        Frame));
    CollisionAffine_TryInvert(&outPlaced->ToWorld, &outPlaced->FromWorld);
}

/* Compares every query of the collision world with a scan of every placed shape: the nearest ray hits and the objects
 * a sphere overlaps must be the same. Returns how many queries disagreed. */
static size_t CompareWithBruteForce(TestRun* run, WorldCollision* collision, const PlacedOwnShape* placed,
    size_t placedCount, uint32_t* state)
{
    size_t Disagreements = 0U;
    GenericBuffer Ids;
    GenericBuffer_AllocateVariable(&Ids, 16U, sizeof(uint64_t));
    GenericBuffer Expected;
    GenericBuffer_AllocateVariable(&Expected, 16U, sizeof(uint64_t));
    for (uint32_t Query = 0U; Query < RANDOM_QUERY_COUNT; Query++)
    {
        // The nearest hit of a ray (ties at a start inside two shapes compare by distance only).
        Vector3 Origin = RandomVector(state, RANDOM_SCENE_HALF_SIZE + 5.0f);
        Vector3 Direction = RandomVector(state, 1.0f);
        float Length = CollisionMath_Length(Direction);
        if (Length < 0.1f)
        {
            continue;
        }
        Vector3 Unit = CollisionMath_Scale(Direction, 1.0f / Length);
        float Best = 40.0f;
        uint64_t BestId = 0U;
        bool IsTied = false;
        for (size_t Index = 0U; Index < placedCount; Index++)
        {
            CollisionRayHit Hit;
            if (CollisionGeometry_RaycastShape(&placed[Index].Shape, &placed[Index].ToWorld, &placed[Index].FromWorld,
                Origin, Unit, 40.0f, &Hit) && (Hit.T <= Best))
            {
                IsTied = (BestId != 0U) && (Hit.T == Best);
                Best = Hit.T;
                BestId = placed[Index].Id;
            }
        }
        WorldRayHit Found;
        bool IsHit = WorldCollision_Raycast(collision, Origin, Direction, 40.0f, NULL, &Found);
        bool IsSame = (IsHit == (BestId != 0U)) && (!IsHit || ((Found.Distance == Best)
            && (IsTied || (Found.Shape.ObjectId == BestId))));
        Disagreements += IsSame ? 0U : 1U;

        // The objects a sphere touches.
        Vector3 Center = RandomVector(state, RANDOM_SCENE_HALF_SIZE + 2.0f);
        float Radius = RandomRange(state, 0.2f, 3.0f);
        CollisionShape Probe = MakeSphere(Radius);
        CollisionPose Pose = { .Position = Center, .Rotation = QuaternionIdentity() };
        CollisionAffine PoseToWorld = CollisionPose_ToAffine(&Pose);
        CollisionAffine Local = CollisionAffine_FromCenterRotation(Probe.Center, Probe.Rotation);
        CollisionAffine ProbeToWorld = CollisionAffine_Multiply(&PoseToWorld, &Local);
        CollisionConvex ProbeView;
        CollisionConvex_FromShape(&Probe, &ProbeToWorld, &ProbeView);
        GenericBuffer_Clear(&Expected);
        for (size_t Index = 0U; Index < placedCount; Index++)
        {
            CollisionConvex Target;
            CollisionContact Contact;
            if (CollisionConvex_FromShape(&placed[Index].Shape, &placed[Index].ToWorld, &Target)
                && CollisionGeometry_Collide(&ProbeView, &Target, 0.0f, &Contact))
            {
                uint64_t Id = placed[Index].Id; // ids ascend: the objects were made in order
                GenericBuffer_AddLast(&Expected, &Id);
            }
        }
        GenericBuffer_Clear(&Ids);
        WorldCollision_OverlapSphere(collision, Center, Radius, NULL, &Ids);
        Disagreements += AreIdsEqual(&Ids, (const uint64_t*)(const void*)Expected._data, Expected._count) ? 0U : 1U;
    }
    Memory_Free(Ids._data);
    Memory_Free(Expected._data);
    TEST_CHECK(run, Disagreements == 0U);
    return Disagreements;
}

static void TheTreesAgreeWithBruteForce(TestRun* run)
{
    CollisionScene Scene;
    if (!CollisionScene_Construct(run, &Scene, NULL, NULL))
    {
        CollisionScene_Deconstruct(run, &Scene);
        return;
    }
    uint32_t State = 20261021U;
    WorldObject* Objects[RANDOM_OBJECT_COUNT];
    WorldColliderComponent* Colliders[RANDOM_OBJECT_COUNT];
    for (uint32_t Index = 0U; Index < RANDOM_OBJECT_COUNT; Index++)
    {
        WorldMobility Mobility = ((NextRandom(&State) & 1U) != 0U) ? WorldMobility_Movable : WorldMobility_Static;
        Objects[Index] = AddObject(run, &Scene.World, RandomVector(&State, RANDOM_SCENE_HALF_SIZE), Mobility);
        if (Objects[Index] == NULL)
        {
            CollisionScene_Deconstruct(run, &Scene);
            return;
        }
        TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(Objects[Index], RandomVector(&State, PI)));
        static const CollisionShapeType Types[] =
        {
            CollisionShapeType_Sphere, CollisionShapeType_Capsule, CollisionShapeType_Box, CollisionShapeType_Cylinder
        };
        CollisionShape Shape = CollisionShape_Create(Types[NextRandom(&State) % 4U]);
        Shape.Radius = RandomRange(&State, 0.2f, 1.5f);
        Shape.HalfHeight = RandomRange(&State, 0.1f, 1.5f);
        Shape.HalfExtents = (Vector3){ RandomRange(&State, 0.1f, 1.5f), RandomRange(&State, 0.1f, 1.5f),
            RandomRange(&State, 0.1f, 1.5f) };
        Shape.Center = RandomVector(&State, 0.5f);
        Shape.Rotation = RandomVector(&State, PI);
        Colliders[Index] = AddOwnShape(run, &Scene.World, Objects[Index], &Shape);
        if (Colliders[Index] == NULL)
        {
            CollisionScene_Deconstruct(run, &Scene);
            return;
        }
        // Uneven scales on a third of them.
        if ((NextRandom(&State) % 3U) == 0U)
        {
            TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetLocalTransform(Colliders[Index], RandomVector(&State, 0.3f),
                RandomVector(&State, PI), (Vector3){ RandomRange(&State, 0.5f, 2.0f), RandomRange(&State, 0.5f, 2.0f),
                RandomRange(&State, 0.5f, 2.0f) }));
        }
    }
    WorldCollision_Sync(Scene.Collision);
    PlacedOwnShape Placed[RANDOM_OBJECT_COUNT];
    for (uint32_t Index = 0U; Index < RANDOM_OBJECT_COUNT; Index++)
    {
        PlaceOwnShape(Objects[Index], Colliders[Index], &Placed[Index]);
    }
    WorldCollisionStats Stats = WorldCollision_GetStats(Scene.Collision);
    TEST_CHECK(run, (Stats.InstanceCount == RANDOM_OBJECT_COUNT) && (Stats.StaticInstanceCount > 0U)
        && (Stats.MovableInstanceCount > 0U));
    CompareWithBruteForce(run, Scene.Collision, Placed, RANDOM_OBJECT_COUNT, &State);

    // Moves: small ones (within the movable tree's margin), large ones, and objects that switch trees.
    for (uint32_t Round = 0U; Round < 3U; Round++)
    {
        for (uint32_t Index = 0U; Index < RANDOM_OBJECT_COUNT; Index++)
        {
            uint32_t Choice = NextRandom(&State) % 8U;
            Vector3 Position = WorldObject_GetPosition(Objects[Index]);
            if (Choice < 3U)
            {
                Position = CollisionMath_Add(Position, RandomVector(&State, 0.05f));
            }
            else if (Choice < 5U)
            {
                Position = RandomVector(&State, RANDOM_SCENE_HALF_SIZE);
            }
            else if (Choice == 5U)
            {
                bool IsStatic = WorldObject_GetMobility(Objects[Index]) == WorldMobility_Static;
                TEST_CHECK_SUCCESS(run, WorldObject_SetMobility(Objects[Index], IsStatic ? WorldMobility_Movable
                    : WorldMobility_Static));
            }
            TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Objects[Index], Position));
        }
        WorldCollision_Sync(Scene.Collision);
        for (uint32_t Index = 0U; Index < RANDOM_OBJECT_COUNT; Index++)
        {
            PlaceOwnShape(Objects[Index], Colliders[Index], &Placed[Index]);
        }
        CompareWithBruteForce(run, Scene.Collision, Placed, RANDOM_OBJECT_COUNT, &State);
    }
    TEST_CHECK(run, WorldCollision_GetStats(Scene.Collision).MoveCount > 0U);
    CollisionScene_Deconstruct(run, &Scene);
}


// Static functions: filters and mobility.
static void FiltersChooseWhatAQuerySees(TestRun* run)
{
    CollisionScene Scene;
    if (!CollisionScene_Construct(run, &Scene, NULL, NULL))
    {
        CollisionScene_Deconstruct(run, &Scene);
        return;
    }
    WorldCollision* Collision = Scene.Collision;
    CollisionShape Ball = MakeSphere(0.5f);
    WorldObject* Objects[4];
    WorldColliderComponent* Colliders[4];
    static const CollisionLayer Layers[] =
    {
        CollisionLayer_Prop, CollisionLayer_World, CollisionLayer_Character, CollisionLayer_Trigger
    };
    for (int Index = 0; Index < 4; Index++)
    {
        Objects[Index] = AddObject(run, &Scene.World, (Vector3){ 2.0f * (float)Index, 0.0f, 0.0f },
            WorldMobility_Movable);
        Colliders[Index] = (Objects[Index] != NULL) ? AddOwnShape(run, &Scene.World, Objects[Index], &Ball) : NULL;
        if (Colliders[Index] == NULL)
        {
            CollisionScene_Deconstruct(run, &Scene);
            return;
        }
        SetLayer(run, Colliders[Index], Layers[Index], Layers[Index] == CollisionLayer_Trigger);
    }
    uint64_t Prop = WorldObject_GetId(Objects[0]);
    uint64_t Wall = WorldObject_GetId(Objects[1]);
    uint64_t Person = WorldObject_GetId(Objects[2]);
    uint64_t Zone = WorldObject_GetId(Objects[3]);
    WorldCollision_Sync(Collision);
    Vector3 Start = { -5.0f, 0.0f, 0.0f };
    Vector3 Right = { 1.0f, 0.0f, 0.0f };
    WorldRayHit Hit;
    TEST_CHECK(run, (CastRay(Collision, Start, Right, NULL, &Hit) > 0.0f) && (Hit.Shape.ObjectId == Prop));
    WorldCollisionFilter Filter = WorldCollisionFilter_Create(COLLISION_LAYER_BIT(CollisionLayer_World),
        WorldCollisionTriggerMode_Ignore);
    TEST_CHECK(run, (CastRay(Collision, Start, Right, &Filter, &Hit) > 0.0f) && (Hit.Shape.ObjectId == Wall)
        && (Hit.Shape.Layer == CollisionLayer_World));
    Filter.LayerMask = COLLISION_LAYER_BIT(CollisionLayer_Trigger);
    TEST_CHECK(run, CastRay(Collision, Start, Right, &Filter, NULL) < 0.0f); // triggers ignored
    Filter.TriggerMode = WorldCollisionTriggerMode_Only;
    Filter.LayerMask = COLLISION_LAYER_MASK_ALL;
    TEST_CHECK(run, (CastRay(Collision, Start, Right, &Filter, &Hit) > 0.0f) && (Hit.Shape.ObjectId == Zone)
        && Hit.Shape.IsTrigger);

    // Every hit along the ray, nearest first; the trigger only when asked for.
    GenericBuffer Hits;
    GenericBuffer_AllocateVariable(&Hits, 4U, sizeof(WorldRayHit));
    TEST_CHECK(run, WorldCollision_RaycastAll(Collision, Start, Right, 100.0f, NULL, &Hits) == 3U);
    const WorldRayHit* All = (const WorldRayHit*)(const void*)Hits._data;
    TEST_CHECK(run, (Hits._count == 3U) && (All[0].Shape.ObjectId == Prop) && (All[1].Shape.ObjectId == Wall)
        && (All[2].Shape.ObjectId == Person) && IsNear(All[0].Distance, 4.5f) && IsNear(All[2].Distance, 8.5f));
    Memory_Free(Hits._data);

    // Overlaps see triggers by default.
    GenericBuffer Ids;
    GenericBuffer_AllocateVariable(&Ids, 4U, sizeof(uint64_t));
    Vector3 Middle = { 3.0f, 0.0f, 0.0f };
    Vector3 Wide = { 4.0f, 1.0f, 1.0f };
    uint64_t Everything[] = { Prop, Wall, Person, Zone };
    WorldCollision_OverlapBox(Collision, Middle, Wide, QuaternionIdentity(), NULL, &Ids);
    TEST_CHECK(run, AreIdsEqual(&Ids, Everything, 4U));
    GenericBuffer_Clear(&Ids);
    Filter = WorldCollisionFilter_Create(COLLISION_LAYER_MASK_ALL, WorldCollisionTriggerMode_Ignore);
    WorldCollision_OverlapBox(Collision, Middle, Wide, QuaternionIdentity(), &Filter, &Ids);
    TEST_CHECK(run, AreIdsEqual(&Ids, Everything, 3U));
    GenericBuffer_Clear(&Ids);
    Filter.TriggerMode = WorldCollisionTriggerMode_Only;
    WorldCollision_OverlapBox(Collision, Middle, Wide, QuaternionIdentity(), &Filter, &Ids);
    TEST_CHECK(run, AreIdsEqual(&Ids, &Zone, 1U));

    // An ignored object, and with its subtree what it carries.
    Filter = WorldCollisionFilter_Create(COLLISION_LAYER_MASK_ALL, WorldCollisionTriggerMode_Ignore);
    Filter.IgnoredObjectId = Prop;
    TEST_CHECK(run, (CastRay(Collision, Start, Right, &Filter, &Hit) > 0.0f) && (Hit.Shape.ObjectId == Wall));
    if (TEST_CHECK_SUCCESS(run, World_SetParent(&Scene.World, Wall, Prop, true)))
    {
        WorldCollision_Sync(Collision);
        TEST_CHECK(run, (CastRay(Collision, Start, Right, &Filter, &Hit) > 0.0f) && (Hit.Shape.ObjectId == Wall));
        Filter.IsIgnoredSubtree = true;
        TEST_CHECK(run, (CastRay(Collision, Start, Right, &Filter, &Hit) > 0.0f) && (Hit.Shape.ObjectId == Person));
    }

    // An object's own contacts follow the layer rule both ways: its mask and the others'.
    WorldObject* Mover = AddObject(run, &Scene.World, (Vector3){ 3.0f, 0.0f, 0.0f }, WorldMobility_Movable);
    // The balls' centres lie inside the plank, nearest its end faces: the push-outs are along X.
    CollisionShape Plank = MakeBox((Vector3){ 1.2f, 0.6f, 0.6f });
    WorldColliderComponent* MoverCollider = (Mover != NULL) ? AddOwnShape(run, &Scene.World, Mover, &Plank) : NULL;
    if (MoverCollider != NULL)
    {
        GenericBuffer Manifolds;
        GenericBuffer_AllocateVariable(&Manifolds, 4U, sizeof(WorldContactManifold));
        WorldCollision_Sync(Collision);
        TEST_CHECK(run, WorldCollision_CollideObject(Collision, WorldObject_GetId(Mover), 0.0f, NULL, &Manifolds) == 2U);
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOverridden(MoverCollider, WorldColliderSetting_CollidesWith,
            true));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetCollidesWith(MoverCollider,
            COLLISION_LAYER_BIT(CollisionLayer_Character)));
        WorldCollision_Sync(Collision);
        GenericBuffer_Clear(&Manifolds);
        TEST_CHECK(run, WorldCollision_CollideObject(Collision, WorldObject_GetId(Mover), 0.0f, NULL, &Manifolds) == 1U);
        const WorldContactManifold* Contact = (const WorldContactManifold*)(const void*)Manifolds._data;
        TEST_CHECK(run, (Manifolds._count == 1U) && (Contact->ShapeA.ObjectId == WorldObject_GetId(Mover))
            && (Contact->ShapeB.ObjectId == Person) && (Contact->Separation < 0.0f)
            && IsNearVector(Contact->Normal, (Vector3){ 1.0f, 0.0f, 0.0f }));
        // The other side's mask counts too: a character that does not collide with props.
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOverridden(Colliders[2], WorldColliderSetting_CollidesWith,
            true));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetCollidesWith(Colliders[2],
            COLLISION_LAYER_BIT(CollisionLayer_World)));
        WorldCollision_Sync(Collision);
        GenericBuffer_Clear(&Manifolds);
        TEST_CHECK(run, WorldCollision_CollideObject(Collision, WorldObject_GetId(Mover), 0.0f, NULL, &Manifolds) == 0U);
        Memory_Free(Manifolds._data);
    }
    Memory_Free(Ids._data);
    CollisionScene_Deconstruct(run, &Scene);
}

static void MobilityChoosesTheTreeAndStaticMovesAreWarnedOnce(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, COLLISION_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    Logger Log;
    if (!TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "collision_static_moves", &Directory))
        || !TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Directory._data)))
    {
        Memory_Free(Directory._data);
        return;
    }
    CollisionScene Scene;
    WorldCollisionOptions Quiet = WorldCollisionOptions_CreateDefault();
    Quiet.WarnsWhenStaticObjectsMove = false;
    WorldCollision* QuietCollision = NULL;
    CollisionShape Ball = MakeSphere(0.5f);
    if (CollisionScene_Construct(run, &Scene, &Log, NULL)
        && TEST_CHECK_SUCCESS(run, WorldCollision_Create(&Scene.World, Scene.Fixture.Manager, &Log, &Quiet,
            &QuietCollision)))
    {
        WorldCollision* Collision = Scene.Collision;
        WorldObject* Statue = AddObject(run, &Scene.World, (Vector3){ 0.0f, 0.0f, 0.0f }, WorldMobility_Static);
        WorldObject* Base = AddObject(run, &Scene.World, (Vector3){ 5.0f, 0.0f, 0.0f }, WorldMobility_Movable);
        WorldObject* Rider = AddObject(run, &Scene.World, (Vector3){ 0.0f, 0.0f, 0.0f }, WorldMobility_Static);
        if ((Statue != NULL) && (Base != NULL) && (Rider != NULL)
            && (AddOwnShape(run, &Scene.World, Statue, &Ball) != NULL)
            && (AddOwnShape(run, &Scene.World, Rider, &Ball) != NULL)
            && TEST_CHECK_SUCCESS(run, World_SetParent(&Scene.World, WorldObject_GetId(Rider), WorldObject_GetId(Base),
                false)))
        {
            WorldCollision_Sync(Collision);
            WorldCollision_Sync(QuietCollision);
            // A static object under a movable parent moves with it: it is movable in effect.
            WorldCollisionStats Stats = WorldCollision_GetStats(Collision);
            TEST_CHECK(run, (Stats.StaticInstanceCount == 1U) && (Stats.MovableInstanceCount == 1U));

            // A static object that moves is found where it went, and warned about once.
            for (int Step = 1; Step <= 3; Step++)
            {
                TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Statue, (Vector3){ 0.0f, 0.0f, 3.0f * (float)Step }));
                WorldCollision_Sync(Collision);
                WorldCollision_Sync(QuietCollision);
            }
            TEST_CHECK(run, IsNear(CastDown(Collision, 0.0f, 9.0f), 9.5f) && (CastDown(Collision, 0.0f, 0.0f) < 0.0f)
                && IsNear(CastDown(QuietCollision, 0.0f, 9.0f), 9.5f));
            TEST_CHECK(run, WorldCollision_GetStats(Collision).MoveCount == 3U);

            // Becoming movable moves it to the other tree (a rebuild).
            uint64_t Rebuilds = WorldCollision_GetStats(Collision).RebuildCount;
            TEST_CHECK_SUCCESS(run, WorldObject_SetMobility(Statue, WorldMobility_Movable));
            WorldCollision_Sync(Collision);
            Stats = WorldCollision_GetStats(Collision);
            TEST_CHECK(run, (Stats.StaticInstanceCount == 0U) && (Stats.MovableInstanceCount == 2U)
                && (Stats.RebuildCount == (Rebuilds + 1U)) && IsNear(CastDown(Collision, 0.0f, 9.0f), 9.5f));
        }
    }
    WorldCollision_Destroy(QuietCollision);
    CollisionScene_Deconstruct(run, &Scene);
    TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "is static but moved") == 1U);
    Memory_Free(Directory._data);
}


// Static functions: triggers.
static bool HasEvent(const WorldTriggerEvent* event, WorldTriggerEventKind kind, uint64_t triggerId, uint64_t otherId)
{
    return (event->Kind == kind) && (event->TriggerId == triggerId) && (event->OtherId == otherId);
}

static void TriggersReportEnterAndExitOnce(TestRun* run)
{
    CollisionScene Scene;
    if (!CollisionScene_Construct(run, &Scene, NULL, NULL))
    {
        CollisionScene_Deconstruct(run, &Scene);
        return;
    }
    WorldCollision* Collision = Scene.Collision;
    CollisionShape Room = MakeBox((Vector3){ 1.0f, 1.0f, 1.0f });
    CollisionShape Ball = MakeSphere(0.5f);
    WorldObject* Zone = AddObject(run, &Scene.World, (Vector3){ 0.0f, 0.0f, 0.0f }, WorldMobility_Static);
    WorldObject* Visitor = AddObject(run, &Scene.World, (Vector3){ 5.0f, 0.0f, 0.0f }, WorldMobility_Movable);
    WorldColliderComponent* ZoneCollider = (Zone != NULL) ? AddOwnShape(run, &Scene.World, Zone, &Room) : NULL;
    if ((ZoneCollider == NULL) || (Visitor == NULL) || (AddOwnShape(run, &Scene.World, Visitor, &Ball) == NULL))
    {
        CollisionScene_Deconstruct(run, &Scene);
        return;
    }
    SetLayer(run, ZoneCollider, CollisionLayer_Trigger, true);
    uint64_t ZoneId = WorldObject_GetId(Zone);
    uint64_t VisitorId = WorldObject_GetId(Visitor);
    // Outside, inside (enter), still inside, at 5 mm (inside the exit margin: no exit), out at 2 cm (exit), back at
    // 5 mm (a new pair needs a touch: nothing).
    static const float Path[] = { 5.0f, 1.4f, 1.2f, 1.505f, 1.52f, 1.505f };
    size_t EventCounts[sizeof(Path) / sizeof(Path[0])];
    for (size_t Step = 0U; Step < (sizeof(Path) / sizeof(Path[0])); Step++)
    {
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Visitor, (Vector3){ Path[Step], 0.0f, 0.0f }));
        WorldCollision_Sync(Collision);
        WorldCollision_GetTriggerEvents(Collision, &EventCounts[Step]);
    }
    size_t Count = 0U;
    const WorldTriggerEvent* Events = WorldCollision_GetTriggerEvents(Collision, &Count);
    TEST_CHECK(run, (EventCounts[0] == 0U) && (EventCounts[1] == 1U) && (EventCounts[2] == 1U) && (EventCounts[3] == 1U)
        && (EventCounts[4] == 2U) && (EventCounts[5] == 2U));
    if (TEST_CHECK(run, (Events != NULL) && (Count == 2U)))
    {
        TEST_CHECK(run, HasEvent(&Events[0], WorldTriggerEventKind_Enter, ZoneId, VisitorId) && (Events[0].Tick == 2U));
        TEST_CHECK(run, HasEvent(&Events[1], WorldTriggerEventKind_Exit, ZoneId, VisitorId) && (Events[1].Tick == 5U));
    }
    WorldCollision_ClearTriggerEvents(Collision);
    TEST_CHECK(run, (WorldCollision_GetTriggerEvents(Collision, &Count) == NULL) && (Count == 0U));

    // Removing the visitor ends its pair; so does disabling the trigger.
    TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Visitor, (Vector3){ 0.0f, 0.0f, 0.0f }));
    WorldCollision_Sync(Collision);
    TEST_CHECK(run, WorldCollision_GetStats(Collision).TriggerPairCount == 1U);
    TEST_CHECK_SUCCESS(run, World_RemoveObject(&Scene.World, VisitorId));
    WorldCollision_Sync(Collision);
    Events = WorldCollision_GetTriggerEvents(Collision, &Count);
    TEST_CHECK(run, (Count == 2U) && HasEvent(&Events[1], WorldTriggerEventKind_Exit, ZoneId, VisitorId)
        && (WorldCollision_GetStats(Collision).TriggerPairCount == 0U));
    WorldCollision_ClearTriggerEvents(Collision);
    WorldObject* Second = AddObject(run, &Scene.World, (Vector3){ 0.5f, 0.0f, 0.0f }, WorldMobility_Movable);
    if ((Second != NULL) && (AddOwnShape(run, &Scene.World, Second, &Ball) != NULL))
    {
        WorldCollision_Sync(Collision);
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetEnabled(ZoneCollider, false));
        WorldCollision_Sync(Collision);
        Events = WorldCollision_GetTriggerEvents(Collision, &Count);
        TEST_CHECK(run, (Count == 2U) && HasEvent(&Events[0], WorldTriggerEventKind_Enter, ZoneId, WorldObject_GetId(Second))
            && HasEvent(&Events[1], WorldTriggerEventKind_Exit, ZoneId, WorldObject_GetId(Second)));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetEnabled(ZoneCollider, true));
        WorldCollision_ClearTriggerEvents(Collision);
    }

    // No pairs: two triggers, a static trigger and a static collider, colliders whose layers do not collide. Inside
    // the room: the second visitor (0.5 m ball at x 0.5), another trigger, a static pillar and a ghost (0.3 m balls),
    // none touching another but the room.
    CollisionShape Small = MakeSphere(0.3f);
    WorldObject* OtherZone = AddObject(run, &Scene.World, (Vector3){ -0.6f, 0.0f, -0.6f }, WorldMobility_Movable);
    WorldObject* Pillar = AddObject(run, &Scene.World, (Vector3){ -0.6f, 0.0f, 0.6f }, WorldMobility_Static);
    WorldObject* Ghost = AddObject(run, &Scene.World, (Vector3){ 0.5f, 0.6f, -0.6f }, WorldMobility_Movable);
    WorldColliderComponent* OtherCollider = (OtherZone != NULL) ? AddOwnShape(run, &Scene.World, OtherZone, &Small)
        : NULL;
    WorldColliderComponent* GhostCollider = (Ghost != NULL) ? AddOwnShape(run, &Scene.World, Ghost, &Small) : NULL;
    if ((Second != NULL) && (OtherCollider != NULL) && (GhostCollider != NULL) && (Pillar != NULL)
        && (AddOwnShape(run, &Scene.World, Pillar, &Small) != NULL))
    {
        SetLayer(run, OtherCollider, CollisionLayer_Trigger, true);
        SetLayer(run, GhostCollider, CollisionLayer_Interaction, false); // collides with nothing
        WorldCollision_Sync(Collision);
        WorldCollision_Sync(Collision);
        Events = WorldCollision_GetTriggerEvents(Collision, &Count);
        // Only the second visitor's enter again (the room came back), none for the others.
        TEST_CHECK(run, (Count == 1U) && HasEvent(&Events[0], WorldTriggerEventKind_Enter, ZoneId,
            WorldObject_GetId(Second)));
        WorldCollision_ClearTriggerEvents(Collision);

        // Events of one sync come by trigger id, then other id, enters before exits: the ghost, a prop again, moves
        // into the other trigger as the second visitor leaves.
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOverridden(GhostCollider, WorldColliderSetting_Layer, false));
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Ghost, (Vector3){ -0.6f, 0.5f, -0.6f }));
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Second, (Vector3){ 5.0f, 0.0f, 0.0f }));
        WorldCollision_Sync(Collision);
        Events = WorldCollision_GetTriggerEvents(Collision, &Count);
        if (TEST_CHECK(run, Count == 3U))
        {
            TEST_CHECK(run, HasEvent(&Events[0], WorldTriggerEventKind_Enter, ZoneId, WorldObject_GetId(Ghost))
                && HasEvent(&Events[1], WorldTriggerEventKind_Enter, WorldObject_GetId(OtherZone),
                    WorldObject_GetId(Ghost))
                && HasEvent(&Events[2], WorldTriggerEventKind_Exit, ZoneId, WorldObject_GetId(Second)));
        }
    }
    CollisionScene_Deconstruct(run, &Scene);
}


// Static functions: queries.
/* A door (a panel and a named handle) and a terrain patch of two triangles. */
static bool AddDoorAndTerrain(TestRun* run, TestAssetFixture* fixture)
{
    BoundingBox Box = { .min = { -0.5f, 0.0f, -0.05f }, .max = { 0.5f, 2.0f, 0.05f } };
    return TestAssets_AddModel(run, fixture, "door_mesh", Box, NULL)
        && TestAssets_AddMeshModel(run, fixture, "patch_mesh", PatchVertices, 4, PatchIndices, 2, MatrixIdentity(),
            NULL)
        && TestAssets_AddWorldModel(run, fixture, "{\"name\": \"door\", \"lods\": [{\"model\": \"door_mesh\", "
            "\"max_distance\": 50}], \"collision\": {\"layer\": \"world\", \"sets\": [{\"name\": \"closed\", \"shapes\": ["
            "{\"type\": \"box\", \"name\": \"panel\", \"center\": [0, 1, 0], \"half_extents\": [0.5, 1, 0.05]},"
            "{\"type\": \"sphere\", \"name\": \"handle\", \"center\": [0.4, 1, 0.1], \"radius\": 0.04}]}]}}")
        && TestAssets_AddWorldModel(run, fixture, "{\"name\": \"terrain\", \"lods\": [{\"model\": \"patch_mesh\", "
            "\"max_distance\": 50}], \"collision\": {\"layer\": \"world\", \"sets\": [{\"name\": \"ground\", "
            "\"shapes\": [{\"type\": \"mesh\", \"model\": \"patch_mesh\", \"is_two_sided\": false}]}]}}");
}

static void QueriesReportShapesPartsAndPushOuts(TestRun* run)
{
    CollisionScene Scene;
    if (!CollisionScene_Construct(run, &Scene, NULL, NULL) || !AddDoorAndTerrain(run, &Scene.Fixture))
    {
        CollisionScene_Deconstruct(run, &Scene);
        return;
    }
    WorldCollision* Collision = Scene.Collision;
    WorldObject* Door = AddObject(run, &Scene.World, (Vector3){ 0.0f, 0.0f, 0.0f }, WorldMobility_Static);
    WorldObject* Post = AddObject(run, &Scene.World, (Vector3){ 1.2f, 1.5f, 1.8f }, WorldMobility_Static);
    CollisionShape Knob = MakeSphere(0.2f);
    if ((Door == NULL) || (AddCollider(run, &Scene.World, Door) == NULL) || !AddWorldModel(run, &Scene.World, Door,
        "door") || (Post == NULL) || (AddOwnShape(run, &Scene.World, Post, &Knob) == NULL))
    {
        CollisionScene_Deconstruct(run, &Scene);
        return;
    }
    uint64_t DoorId = WorldObject_GetId(Door);
    WorldCollision_Sync(Collision);

    // The nearest shape names its part; every hit comes nearest first.
    WorldRayHit Hit;
    Vector3 Origin = { 0.4f, 1.0f, 5.0f };
    Vector3 Back = { 0.0f, 0.0f, -1.0f };
    TEST_CHECK(run, IsNear(CastRay(Collision, Origin, Back, NULL, &Hit), 4.86f) && (Hit.Shape.ObjectId == DoorId)
        && (Hit.Shape.ShapeIndex == 1U) && (Hit.Shape.ShapeName != NULL)
        && (strcmp((const char*)Hit.Shape.ShapeName, "handle") == 0) && (Hit.Shape.ShapeType == CollisionShapeType_Sphere)
        && (Hit.Shape.Layer == CollisionLayer_World) && IsNearVector(Hit.Normal, (Vector3){ 0.0f, 0.0f, 1.0f }));
    GenericBuffer Hits;
    GenericBuffer_AllocateVariable(&Hits, 4U, sizeof(WorldRayHit));
    TEST_CHECK(run, WorldCollision_RaycastAll(Collision, Origin, Back, 100.0f, NULL, &Hits) == 2U);
    const WorldRayHit* All = (const WorldRayHit*)(const void*)Hits._data;
    TEST_CHECK(run, (Hits._count == 2U) && (All[0].Shape.ShapeIndex == 1U) && (All[1].Shape.ShapeIndex == 0U)
        && IsNear(All[1].Distance, 4.95f) && (strcmp((const char*)All[1].Shape.ShapeName, "panel") == 0));
    Memory_Free(Hits._data);

    // A fast sphere does not pass through the thin panel: one cast over 200 m stops where it touches.
    WorldCastHit Cast;
    if (TEST_CHECK(run, WorldCollision_SweepSphere(Collision, (Vector3){ 0.0f, 1.5f, 100.0f },
        (Vector3){ 0.0f, 1.5f, -100.0f }, 0.1f, NULL, &Cast)))
    {
        TEST_CHECK(run, (Cast.Shape.ObjectId == DoorId) && (Cast.Shape.ShapeIndex == 0U) && (Cast.CastShapeIndex == 0U)
            && IsNear(Cast.Distance, 99.85f) && IsNear(Cast.Fraction * 200.0f, 99.85f) && !Cast.IsStartPenetrating
            && IsNearVector(Cast.Normal, (Vector3){ 0.0f, 0.0f, 1.0f })
            && IsNear(Cast.Point.z, 0.05f) && IsNear(Cast.PointOnCast.z, 0.05f));
    }
    // A cast that starts inside reports the push-out.
    if (TEST_CHECK(run, WorldCollision_SweepSphere(Collision, (Vector3){ -0.2f, 1.5f, 0.02f },
        (Vector3){ -0.2f, 1.5f, 1.0f }, 0.1f, NULL, &Cast)))
    {
        TEST_CHECK(run, Cast.IsStartPenetrating && (Cast.Fraction == 0.0f) && IsNear(Cast.PenetrationDepth, 0.13f)
            && IsNearVector(Cast.Normal, (Vector3){ 0.0f, 0.0f, 1.0f }));
    }
    // A turning stick hits the post on its way round; without the turn it misses.
    CollisionShape Stick = MakeBox((Vector3){ 2.0f, 0.1f, 0.1f });
    Vector3 Pivot = { 0.0f, 1.5f, 3.0f };
    Quaternion Turned = QuaternionFromAxisAngle((Vector3){ 0.0f, 1.0f, 0.0f }, PI * 0.5f);
    TEST_CHECK(run, !WorldCollision_CastShape(Collision, &Stick, 1U, Pivot, QuaternionIdentity(), Pivot,
        QuaternionIdentity(), NULL, &Cast));
    TEST_CHECK(run, !WorldCollision_CastShape(Collision, &Stick, 1U, Pivot, Turned, Pivot, Turned, NULL, &Cast));
    if (TEST_CHECK(run, WorldCollision_CastShape(Collision, &Stick, 1U, Pivot, QuaternionIdentity(), Pivot, Turned, NULL,
        &Cast)))
    {
        TEST_CHECK(run, (Cast.Shape.ObjectId == WorldObject_GetId(Post)) && (Cast.Fraction > 0.2f)
            && (Cast.Fraction < 0.8f) && !Cast.IsStartPenetrating);
    }

    // Overlaps give each object once, ascending.
    GenericBuffer Ids;
    GenericBuffer_AllocateVariable(&Ids, 4U, sizeof(uint64_t));
    uint64_t Both[] = { DoorId, WorldObject_GetId(Post) };
    WorldCollision_OverlapBox(Collision, (Vector3){ 0.5f, 1.0f, 0.5f }, (Vector3){ 1.0f, 1.0f, 1.5f },
        QuaternionIdentity(), NULL, &Ids);
    TEST_CHECK(run, AreIdsEqual(&Ids, Both, 2U));
    GenericBuffer_Clear(&Ids);
    WorldCollision_OverlapCapsule(Collision, (Vector3){ 0.4f, 1.0f, 0.2f }, 0.07f, 0.5f, NULL, &Ids);
    TEST_CHECK(run, AreIdsEqual(&Ids, Both, 1U));
    Memory_Free(Ids._data);

    // A contact manifold: A the query's box sunk 1 cm into the panel's front, its face on the panel's.
    GenericBuffer Manifolds;
    GenericBuffer_AllocateVariable(&Manifolds, 4U, sizeof(WorldContactManifold));
    CollisionShape Block = MakeBox((Vector3){ 0.1f, 0.1f, 0.1f });
    Vector3 Resting = { -0.2f, 1.5f, 0.14f };
    TEST_CHECK(run, WorldCollision_CollideShape(Collision, &Block, 1U, Resting, QuaternionIdentity(), 0.0f, NULL,
        &Manifolds) == 1U);
    const WorldContactManifold* Contact = (const WorldContactManifold*)(const void*)Manifolds._data;
    if (TEST_CHECK(run, Manifolds._count == 1U))
    {
        TEST_CHECK(run, (Contact->ShapeA.ObjectId == 0U) && (Contact->ShapeB.ObjectId == DoorId)
            && (Contact->ShapeB.ShapeIndex == 0U) && (Contact->PointCount == 4U) && IsNear(Contact->Separation, -0.01f)
            && IsNearVector(Contact->Normal, (Vector3){ 0.0f, 0.0f, -1.0f }));
        bool ArePointsRight = true;
        for (uint32_t Point = 0U; Point < Contact->PointCount; Point++)
        {
            const WorldContactPoint* At = &Contact->Points[Point];
            ArePointsRight = ArePointsRight && IsNear(At->Separation, -0.01f) && IsNear(At->LocalPositionA.z, -0.1f)
                && IsNear(At->LocalPositionB.z, 0.05f) && IsNear(fabsf(At->LocalPositionA.x), 0.1f);
        }
        TEST_CHECK(run, ArePointsRight);
    }
    // Out to a separation: speculative contacts 5 cm away.
    GenericBuffer_Clear(&Manifolds);
    Vector3 Near = { -0.2f, 1.5f, 0.2f };
    TEST_CHECK(run, WorldCollision_CollideShape(Collision, &Block, 1U, Near, QuaternionIdentity(), 0.0f, NULL,
        &Manifolds) == 0U);
    TEST_CHECK(run, (WorldCollision_CollideShape(Collision, &Block, 1U, Near, QuaternionIdentity(), 0.1f, NULL,
        &Manifolds) == 1U) && IsNear(((const WorldContactManifold*)(const void*)Manifolds._data)->Separation, 0.05f));
    Memory_Free(Manifolds._data);
    CollisionScene_Deconstruct(run, &Scene);
}

static void MeshesReportTheirTriangles(TestRun* run)
{
    CollisionScene Scene;
    if (!CollisionScene_Construct(run, &Scene, NULL, NULL) || !AddDoorAndTerrain(run, &Scene.Fixture))
    {
        CollisionScene_Deconstruct(run, &Scene);
        return;
    }
    WorldCollision* Collision = Scene.Collision;
    WorldObject* Ground = AddObject(run, &Scene.World, (Vector3){ 20.0f, 0.0f, 0.0f }, WorldMobility_Static);
    if ((Ground == NULL) || (AddCollider(run, &Scene.World, Ground) == NULL)
        || !AddWorldModel(run, &Scene.World, Ground, "terrain"))
    {
        CollisionScene_Deconstruct(run, &Scene);
        return;
    }
    uint64_t GroundId = WorldObject_GetId(Ground);
    WorldCollision_Sync(Collision);
    WorldCollisionStats Stats = WorldCollision_GetStats(Collision);
    TEST_CHECK(run, (Stats.ShapeCount == 1U) && (Stats.TriangleCount == 2U));

    // A ray down: the triangle, its model part and source triangle, the corners' weights at the hit.
    WorldRayHit Hit;
    TEST_CHECK(run, IsNear(CastRay(Collision, (Vector3){ 21.0f, 3.0f, 2.0f }, (Vector3){ 0.0f, -1.0f, 0.0f }, NULL,
        &Hit), 3.0f));
    if (TEST_CHECK(run, Hit.Shape.HasTriangle && (Hit.Shape.ShapeType == CollisionShapeType_Mesh)))
    {
        TEST_CHECK(run, (Hit.Shape.ObjectId == GroundId) && (Hit.Shape.MeshPart == 0U) && (Hit.Shape.MaterialIndex == 0)
            && (Hit.Shape.SourceTriangle < 2U) && IsNearVector(Hit.Normal, (Vector3){ 0.0f, 1.0f, 0.0f }));
        Vector3 Weights = Hit.Shape.Barycentric;
        TEST_CHECK(run, IsNear(Weights.x + Weights.y + Weights.z, 1.0f) && (Weights.x >= 0.0f) && (Weights.y >= 0.0f)
            && (Weights.z >= 0.0f));
        // The hit at (1, 2) of the patch lies in its second triangle (corners 0, 3, 2: x < z in patch space).
        const unsigned short* Corners = &PatchIndices[Hit.Shape.SourceTriangle * 3U];
        Vector3 Rebuilt = { 20.0f, 0.0f, 0.0f };
        for (int Corner = 0; Corner < 3; Corner++)
        {
            float Weight = (Corner == 0) ? Weights.x : ((Corner == 1) ? Weights.y : Weights.z);
            const float* Vertex = &PatchVertices[Corners[Corner] * 3U];
            Rebuilt = CollisionMath_AddScaled(Rebuilt, (Vector3){ Vertex[0], Vertex[1], Vertex[2] }, Weight);
        }
        TEST_CHECK(run, (Hit.Shape.SourceTriangle == 1U) && IsNearVector(Rebuilt, Hit.Point));
    }
    // One-sided: a ray from below passes through.
    TEST_CHECK(run, CastRay(Collision, (Vector3){ 21.0f, -3.0f, 1.0f }, (Vector3){ 0.0f, 1.0f, 0.0f }, NULL, NULL)
        < 0.0f);

    // A character's capsule lands on it.
    WorldCastHit Cast;
    if (TEST_CHECK(run, WorldCollision_SweepCapsule(Collision, (Vector3){ 20.0f, 5.0f, 0.5f },
        (Vector3){ 20.0f, -5.0f, 0.5f }, 0.3f, 1.8f, NULL, &Cast)))
    {
        TEST_CHECK(run, Cast.Shape.HasTriangle && IsNear(Cast.Distance, 4.1f) && IsNear(Cast.Fraction, 0.41f)
            && IsNearVector(Cast.Normal, (Vector3){ 0.0f, 1.0f, 0.0f }) && IsNear(Cast.Point.y, 0.0f)
            && (Cast.Shape.MaterialIndex == 0));
    }
    // A box sunk 1 cm across the seam touches both triangles, with no ghost contact along the flat seam.
    GenericBuffer Manifolds;
    GenericBuffer_AllocateVariable(&Manifolds, 4U, sizeof(WorldContactManifold));
    CollisionShape Crate = MakeBox((Vector3){ 0.5f, 0.5f, 0.5f });
    size_t Found = WorldCollision_CollideShape(Collision, &Crate, 1U, (Vector3){ 20.0f, 0.49f, 0.0f },
        QuaternionIdentity(), 0.0f, NULL, &Manifolds);
    TEST_CHECK(run, Found == 2U);
    const WorldContactManifold* Contacts = (const WorldContactManifold*)(const void*)Manifolds._data;
    for (size_t Index = 0U; Index < Manifolds._count; Index++)
    {
        TEST_CHECK(run, Contacts[Index].ShapeB.HasTriangle && (Contacts[Index].ShapeB.ObjectId == GroundId)
            && IsNearVector(Contacts[Index].Normal, (Vector3){ 0.0f, -1.0f, 0.0f })
            && IsNear(Contacts[Index].Separation, -0.01f) && (Contacts[Index].PointCount >= 3U));
        for (uint32_t Point = 0U; Point < Contacts[Index].PointCount; Point++)
        {
            TEST_CHECK(run, IsNear(Contacts[Index].Points[Point].LocalPositionB.y, 0.0f)
                && IsNear(Contacts[Index].Points[Point].LocalPositionA.y, -0.5f));
        }
    }
    Memory_Free(Manifolds._data);
    CollisionScene_Deconstruct(run, &Scene);
}

static void PlanesAreBoundedOrInfinite(TestRun* run)
{
    CollisionScene Scene;
    if (!CollisionScene_Construct(run, &Scene, NULL, NULL))
    {
        CollisionScene_Deconstruct(run, &Scene);
        return;
    }
    WorldCollision* Collision = Scene.Collision;
    CollisionShape Floor = CollisionShape_Create(CollisionShapeType_Plane);
    Floor.IsBounded = false;
    CollisionShape Shelf = CollisionShape_Create(CollisionShapeType_Plane);
    Shelf.PlaneHalfExtents = (Vector2){ 1.0f, 1.0f };
    WorldObject* Ground = AddObject(run, &Scene.World, (Vector3){ 0.0f, -10.0f, 0.0f }, WorldMobility_Static);
    WorldObject* Board = AddObject(run, &Scene.World, (Vector3){ 0.0f, 5.0f, 10.0f }, WorldMobility_Static);
    if ((Ground == NULL) || (Board == NULL) || (AddOwnShape(run, &Scene.World, Ground, &Floor) == NULL)
        || (AddOwnShape(run, &Scene.World, Board, &Shelf) == NULL))
    {
        CollisionScene_Deconstruct(run, &Scene);
        return;
    }
    WorldCollision_Sync(Collision);
    WorldCollisionStats Stats = WorldCollision_GetStats(Collision);
    TEST_CHECK(run, (Stats.PlaneCount == 1U) && (Stats.ShapeCount == 2U));

    // The infinite floor is everywhere below y = -10, a kilometre away too.
    TEST_CHECK(run, IsNear(CastDown(Collision, 1000.0f, 1000.0f), 20.0f));
    GenericBuffer Ids;
    GenericBuffer_AllocateVariable(&Ids, 4U, sizeof(uint64_t));
    uint64_t GroundId = WorldObject_GetId(Ground);
    uint64_t BoardId = WorldObject_GetId(Board);
    WorldCollision_OverlapSphere(Collision, (Vector3){ 500.0f, -10.2f, 500.0f }, 0.1f, NULL, &Ids);
    TEST_CHECK(run, AreIdsEqual(&Ids, &GroundId, 1U));
    WorldCastHit Cast;
    TEST_CHECK(run, WorldCollision_SweepSphere(Collision, (Vector3){ 300.0f, 0.0f, 0.0f },
        (Vector3){ 300.0f, -20.0f, 0.0f }, 0.5f, NULL, &Cast) && (Cast.Shape.ObjectId == GroundId)
        && IsNear(Cast.Distance, 9.5f) && IsNearVector(Cast.Normal, (Vector3){ 0.0f, 1.0f, 0.0f }));

    // The rectangle: hit from the front inside its extents only, never from behind.
    TEST_CHECK(run, IsNear(CastRay(Collision, (Vector3){ 0.5f, 6.0f, 10.5f }, (Vector3){ 0.0f, -1.0f, 0.0f }, NULL,
        NULL), 1.0f));
    TEST_CHECK(run, CastRay(Collision, (Vector3){ 1.5f, 6.0f, 10.0f }, (Vector3){ 0.0f, -1.0f, 0.0f }, NULL, NULL)
        > 1.5f); // past the shelf, onto the floor
    WorldRayHit Hit;
    TEST_CHECK(run, (CastRay(Collision, (Vector3){ 0.5f, 4.0f, 10.0f }, (Vector3){ 0.0f, 1.0f, 0.0f }, NULL, &Hit)
        < 0.0f));
    // A ball resting on it touches it; one coming up from behind does not (one-way at depth 0).
    GenericBuffer_Clear(&Ids);
    WorldCollision_OverlapSphere(Collision, (Vector3){ 0.0f, 5.09f, 10.0f }, 0.1f, NULL, &Ids);
    TEST_CHECK(run, AreIdsEqual(&Ids, &BoardId, 1U));
    GenericBuffer_Clear(&Ids);
    WorldCollision_OverlapSphere(Collision, (Vector3){ 0.0f, 4.95f, 10.0f }, 0.1f, NULL, &Ids);
    TEST_CHECK(run, Ids._count == 0U);
    // A query's own plane finds what rests on it (a plane never pairs with a plane).
    WorldObject* Marble = AddObject(run, &Scene.World, (Vector3){ 0.5f, 5.05f, 10.5f }, WorldMobility_Movable);
    CollisionShape Ball = MakeSphere(0.1f);
    if ((Marble != NULL) && (AddOwnShape(run, &Scene.World, Marble, &Ball) != NULL))
    {
        WorldCollision_Sync(Collision);
        uint64_t MarbleId = WorldObject_GetId(Marble);
        GenericBuffer_Clear(&Ids);
        WorldCollision_OverlapShape(Collision, &Shelf, 1U, (Vector3){ 0.0f, 5.0f, 10.0f }, QuaternionIdentity(), NULL,
            &Ids);
        TEST_CHECK(run, AreIdsEqual(&Ids, &MarbleId, 1U));
    }
    Memory_Free(Ids._data);
    CollisionScene_Deconstruct(run, &Scene);
}

static void TheDebugListShowsEveryActiveShape(TestRun* run)
{
    CollisionScene Scene;
    if (!CollisionScene_Construct(run, &Scene, NULL, NULL) || !AddDoorAndTerrain(run, &Scene.Fixture))
    {
        CollisionScene_Deconstruct(run, &Scene);
        return;
    }
    WorldCollision* Collision = Scene.Collision;
    WorldObject* Door = AddObject(run, &Scene.World, (Vector3){ 3.0f, 0.0f, 0.0f }, WorldMobility_Static);
    WorldObject* Zone = AddObject(run, &Scene.World, (Vector3){ 0.0f, 0.0f, 0.0f }, WorldMobility_Movable);
    CollisionShape Room = MakeBox((Vector3){ 1.0f, 1.0f, 1.0f });
    WorldColliderComponent* ZoneCollider = (Zone != NULL) ? AddOwnShape(run, &Scene.World, Zone, &Room) : NULL;
    if ((Door == NULL) || (ZoneCollider == NULL) || (AddCollider(run, &Scene.World, Door) == NULL)
        || !AddWorldModel(run, &Scene.World, Door, "door"))
    {
        CollisionScene_Deconstruct(run, &Scene);
        return;
    }
    SetLayer(run, ZoneCollider, CollisionLayer_Trigger, true);
    size_t Count = 0U;
    WorldCollision_Sync(Collision);
    TEST_CHECK(run, (WorldCollision_GetDebugShapes(Collision, &Count) == NULL) && (Count == 0U)); // off by default
    WorldCollision_SetDebugListEnabled(Collision, true);
    WorldCollision_Sync(Collision);
    const WorldCollisionDebugShape* Shapes = WorldCollision_GetDebugShapes(Collision, &Count);
    TEST_CHECK(run, (Count == 3U) && (Count == WorldCollision_GetStats(Collision).ShapeCount));
    bool HasHandle = false;
    bool HasZone = false;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        Vector3 Center = Vector3Transform((Vector3){ 0.0f, 0.0f, 0.0f }, Shapes[Index].Transform);
        if (Shapes[Index].Shape->Type == CollisionShapeType_Sphere)
        {
            HasHandle = IsNearVector(Center, (Vector3){ 3.4f, 1.0f, 0.1f }) && Shapes[Index].IsStatic
                && (Shapes[Index].Layer == CollisionLayer_World) && !Shapes[Index].IsTrigger;
        }
        if (Shapes[Index].IsTrigger)
        {
            HasZone = !Shapes[Index].IsStatic && (Shapes[Index].Layer == CollisionLayer_Trigger)
                && IsNearVector(Center, (Vector3){ 0.0f, 0.0f, 0.0f });
        }
    }
    TEST_CHECK(run, HasHandle && HasZone);
    WorldCollision_SetDebugListEnabled(Collision, false);
    TEST_CHECK(run, (WorldCollision_GetDebugShapes(Collision, &Count) == NULL) && (Count == 0U));
    CollisionScene_Deconstruct(run, &Scene);
}

static void ProblemsAreWarnedOnce(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, COLLISION_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    Logger Log;
    if (!TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "collision_warnings", &Directory))
        || !TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Directory._data)))
    {
        Memory_Free(Directory._data);
        return;
    }
    CollisionScene Scene;
    if (CollisionScene_Construct(run, &Scene, &Log, NULL) && AddDoorAndTerrain(run, &Scene.Fixture)
        && TestAssets_AddWorldModel(run, &Scene.Fixture, "{\"name\": \"plain\", \"lods\": [{\"model\": \"door_mesh\", "
            "\"max_distance\": 50}]}"))
    {
        WorldCollision* Collision = Scene.Collision;
        WorldObject* Lost = AddObject(run, &Scene.World, (Vector3){ 0.0f, 0.0f, 0.0f }, WorldMobility_Movable);
        WorldObject* Ajar = AddObject(run, &Scene.World, (Vector3){ 5.0f, 0.0f, 0.0f }, WorldMobility_Movable);
        WorldObject* Flat = AddObject(run, &Scene.World, (Vector3){ 10.0f, 0.0f, 0.0f }, WorldMobility_Movable);
        WorldObject* Plain = AddObject(run, &Scene.World, (Vector3){ 15.0f, 0.0f, 0.0f }, WorldMobility_Movable);
        WorldColliderComponent* AjarCollider = (Ajar != NULL) ? AddCollider(run, &Scene.World, Ajar) : NULL;
        CollisionShape Ball = MakeSphere(0.5f);
        if ((Lost != NULL) && (AddCollider(run, &Scene.World, Lost) != NULL)
            && AddWorldModel(run, &Scene.World, Lost, "no_such_world_model")
            && (AjarCollider != NULL) && AddWorldModel(run, &Scene.World, Ajar, "door")
            && (Flat != NULL) && (AddOwnShape(run, &Scene.World, Flat, &Ball) != NULL)
            && (Plain != NULL) && (AddCollider(run, &Scene.World, Plain) != NULL)
            && AddWorldModel(run, &Scene.World, Plain, "plain"))
        {
            TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOverridden(AjarCollider,
                WorldColliderSetting_CollisionSet, true));
            TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetCollisionSet(AjarCollider, (const unsigned char*)u8"ajar"));
            TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Flat, (Vector3){ 1.0f, 0.0f, 1.0f }));
            for (int Step = 0; Step < 3; Step++)
            {
                WorldCollision_Sync(Collision);
                // Changes that rebuild (the same problems stay unwarned).
                TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetRotationMode(AjarCollider,
                    (Step % 2 == 0) ? WorldColliderRotationMode_None : WorldColliderRotationMode_Full));
            }
            TEST_CHECK(run, WorldCollision_GetStats(Collision).InstanceCount == 1U); // the flat ball: in, colliding with nothing
            TEST_CHECK(run, CastDown(Collision, 10.0f, 0.0f) < 0.0f);
            // Unflattened, it collides again.
            TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Flat, (Vector3){ 1.0f, 1.0f, 1.0f }));
            WorldCollision_Sync(Collision);
            TEST_CHECK(run, IsNear(CastDown(Collision, 10.0f, 0.0f), 9.5f));
        }
    }
    CollisionScene_Deconstruct(run, &Scene);
    TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "has no collision from its world model "
        "\"no_such_world_model\": No definition named") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "its world model has no collision set \"ajar\"") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "that its transform flattens") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "has a collider but no shapes: it uses no own shape and its "
        "world model \"plain\" has no collision section.") == 1U);
    Memory_Free(Directory._data);
}


// Public functions.
void WorldCollisionTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "sync_follows_the_world", SyncFollowsTheWorld },
        { "own_shapes_survive_growth_and_queries_see_the_last_sync", OwnShapesSurviveGrowthAndQueriesSeeTheLastSync },
        { "colliders_follow_their_rotation_mode_and_transform", CollidersFollowTheirRotationModeAndTransform },
        { "the_trees_agree_with_brute_force", TheTreesAgreeWithBruteForce },
        { "filters_choose_what_a_query_sees", FiltersChooseWhatAQuerySees },
        { "mobility_chooses_the_tree_and_static_moves_are_warned_once",
            MobilityChoosesTheTreeAndStaticMovesAreWarnedOnce },
        { "triggers_report_enter_and_exit_once", TriggersReportEnterAndExitOnce },
        { "queries_report_shapes_parts_and_push_outs", QueriesReportShapesPartsAndPushOuts },
        { "meshes_report_their_triangles", MeshesReportTheirTriangles },
        { "planes_are_bounded_or_infinite", PlanesAreBoundedOrInfinite },
        { "the_debug_list_shows_every_active_shape", TheDebugListShowsEveryActiveShape },
        { "problems_are_warned_once", ProblemsAreWarnedOnce },
    };
    TestRun_RunSuite(run, "WorldCollision", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
