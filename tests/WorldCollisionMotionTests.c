#include "TestSuites.h"
#include "TestWorld.h"
#include "World.h"
#include "WorldColliderComponent.h"
#include "WorldCollision.h"
#include "GameMath.h"
#include "raylib/raymath.h"
#include <math.h>
#include <string.h>


// Macros.
/** The game's tick (480 Hz). */
#define TEST_DELTA (1.0f / 480.0f)
/** Positions the solver gives are this close to the expected ones (its skin is a millimetre). */
#define TEST_EPSILON 3e-3f


// Types.
/* A world without assets and a collision world over it: every collider uses an own shape. */
typedef struct MotionSceneStruct
{
    World World;
    KeyRegistry Keys;
    WorldCollision* Collision;
    bool IsBuilt;
} MotionScene;

/* What a test object is. */
typedef enum TestBodyKindEnum
{
    TestBodyKind_Static,
    TestBodyKind_Kinematic,
    TestBodyKind_Pushable
} TestBodyKind;


// Static functions: helpers.
static bool IsNear(float a, float b)
{
    return fabsf(a - b) < TEST_EPSILON;
}

static bool MotionScene_Construct(TestRun* run, MotionScene* self)
{
    Memory_Zero(self, sizeof(*self));
    self->IsBuilt = TestWorld_Construct(run, &self->World, &self->Keys, NULL, NULL);
    return self->IsBuilt && TEST_CHECK_SUCCESS(run, WorldCollision_Create(&self->World, NULL, NULL, NULL,
        &self->Collision));
}

static void MotionScene_Deconstruct(TestRun* run, MotionScene* self)
{
    WorldCollision_Destroy(self->Collision);
    if (self->IsBuilt)
    {
        TestWorld_Deconstruct(run, &self->World, &self->Keys);
    }
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

static WorldColliderComponent* GetCollider(MotionScene* scene, uint64_t id)
{
    WorldComponent* Component = NULL;
    return World_TryGetComponent(&scene->World, id, WorldComponentType_Collider, &Component)
        ? WorldColliderComponent_FromComponent(Component) : NULL;
}

/* An object at @p position of the scene (TestWorld_At: shifted in the far run) with one own-shape collider: static,
 * kinematic (movable) or pushable (movable). */
static uint64_t AddShapeObject(TestRun* run, MotionScene* scene, Vector3 position, const CollisionShape* shape,
    TestBodyKind kind)
{
    WorldObject* Object = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(&scene->World, (const unsigned char*)u8"thing", &Object)))
    {
        return 0U;
    }
    uint64_t Id = WorldObject_GetId(Object);
    TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, TestWorld_At(position)));
    TEST_CHECK_SUCCESS(run, WorldObject_SetMobility(Object, (kind == TestBodyKind_Static) ? WorldMobility_Static
        : WorldMobility_Movable));
    WorldComponent* Component = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_AddComponent(&scene->World, Id, WorldComponentType_Collider, &Component)))
    {
        return Id;
    }
    WorldColliderComponent* Collider = WorldColliderComponent_FromComponent(Component);
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOwnShape(Collider, shape));
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetUsesOwnShape(Collider, true));
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetPushable(Collider, kind == TestBodyKind_Pushable));
    return Id;
}

static WorldObject* GetObject(MotionScene* scene, uint64_t id)
{
    WorldObject* Object = NULL;
    World_TryGetObject(&scene->World, id, &Object);
    return Object;
}

/* An object's world position in the scene. */
static Vector3 GetPosition(MotionScene* scene, uint64_t id)
{
    return TestWorld_ToScene(WorldObject_GetWorldPosition(GetObject(scene, id)));
}

/* The driver: moves an object (a root) to @p position of the scene. */
static void MoveTo(TestRun* run, MotionScene* scene, uint64_t id, Vector3 position)
{
    TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(GetObject(scene, id), TestWorld_At(position)));
}

/* The driver: moves an object (a root, or a child of an unturned, unscaled parent) by @p motion, in double. */
static void MoveBy(TestRun* run, MotionScene* scene, uint64_t id, Vector3 motion)
{
    WorldObject* Object = GetObject(scene, id);
    TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, Vector3Double_Add(WorldObject_GetPosition(Object),
        GameMath_FromVector3(motion))));
}

static void Step(MotionScene* scene)
{
    WorldCollision_Step(scene->Collision, TEST_DELTA);
}

static size_t CountMotionEvents(MotionScene* scene, WorldMotionEventKind kind, uint64_t objectId)
{
    size_t Count = 0U;
    const WorldMotionEvent* Events = WorldCollision_GetMotionEvents(scene->Collision, &Count);
    size_t Found = 0U;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        Found += ((Events[Index].Kind == kind) && (Events[Index].ObjectId == objectId)) ? 1U : 0U;
    }
    return Found;
}

static size_t CountContactEvents(MotionScene* scene, WorldContactEventKind kind, uint64_t objectA, uint64_t objectB)
{
    size_t Count = 0U;
    const WorldContactEvent* Events = WorldCollision_GetContactEvents(scene->Collision, &Count);
    size_t Found = 0U;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        Found += ((Events[Index].Kind == kind) && (Events[Index].ObjectIdA == objectA)
            && (Events[Index].ObjectIdB == objectB)) ? 1U : 0U;
    }
    return Found;
}

/* The deepest overlap between any two of the boxes of @p ids (each a box of @p half half extents, unturned): below 0
 * means apart. */
static float GetDeepestBoxOverlap(MotionScene* scene, const uint64_t* ids, size_t count, float half)
{
    float Deepest = -1000.0f;
    for (size_t A = 0U; A < count; A++)
    {
        Vector3 PositionA = GetPosition(scene, ids[A]);
        for (size_t B = A + 1U; B < count; B++)
        {
            Vector3 PositionB = GetPosition(scene, ids[B]);
            float OverlapX = (2.0f * half) - fabsf(PositionA.x - PositionB.x);
            float OverlapY = (2.0f * half) - fabsf(PositionA.y - PositionB.y);
            float OverlapZ = (2.0f * half) - fabsf(PositionA.z - PositionB.z);
            Deepest = fmaxf(Deepest, fminf(OverlapX, fminf(OverlapY, OverlapZ)));
        }
    }
    return Deepest;
}


// Static functions: tunnelling.
static void AThinWallStopsFastSpheresAndBoxes(TestRun* run)
{
    // A sphere at 1000 m/s and a box at 100 m/s against a 1 cm wall: each tick its driver moves it on from where the
    // last step left it.
    struct { CollisionShape Shape; float Speed; float Half; } Cases[] =
    {
        { MakeSphere(0.1f), 1000.0f, 0.1f },
        { MakeBox((Vector3){ 0.1f, 0.1f, 0.1f }), 100.0f, 0.1f },
    };
    for (size_t Case = 0U; Case < (sizeof(Cases) / sizeof(Cases[0])); Case++)
    {
        MotionScene Scene;
        if (!MotionScene_Construct(run, &Scene))
        {
            MotionScene_Deconstruct(run, &Scene);
            return;
        }
        CollisionShape Wall = MakeBox((Vector3){ 0.005f, 2.0f, 2.0f });
        AddShapeObject(run, &Scene, (Vector3){ 5.0f, 0.0f, 0.0f }, &Wall, TestBodyKind_Static);
        uint64_t Mover = AddShapeObject(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f }, &Cases[Case].Shape,
            TestBodyKind_Pushable);
        Step(&Scene);
        float Limit = 5.0f - 0.005f - Cases[Case].Half;
        bool IsAlwaysBefore = true;
        for (uint32_t Tick = 0U; Tick < 40U; Tick++)
        {
            MoveBy(run, &Scene, Mover, (Vector3){ Cases[Case].Speed * TEST_DELTA, 0.0f, 0.0f });
            Step(&Scene);
            IsAlwaysBefore = IsAlwaysBefore && (GetPosition(&Scene, Mover).x <= Limit);
        }
        TEST_CHECK(run, IsAlwaysBefore);
        TEST_CHECK(run, IsNear(GetPosition(&Scene, Mover).x, Limit - WORLD_COLLISION_SKIN));
        MotionScene_Deconstruct(run, &Scene);
    }
}

static void AFastFanBladePushesASphereInsteadOfPassingIt(TestRun* run)
{
    MotionScene Scene;
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    // A blade 1 m long from its pivot, turning 40 degrees a tick about Y; a sphere 0.6 m out, 30 degrees ahead.
    CollisionShape Blade = MakeBox((Vector3){ 0.5f, 0.05f, 0.05f });
    Blade.Center = (Vector3){ 0.5f, 0.0f, 0.0f };
    uint64_t Fan = AddShapeObject(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f }, &Blade, TestBodyKind_Kinematic);
    CollisionShape Ball = MakeSphere(0.1f);
    float Ahead = -30.0f * DEG2RAD; // turning by negative yaw carries +X toward +Z
    uint64_t Sphere = AddShapeObject(run, &Scene, (Vector3){ 0.6f * cosf(Ahead), 0.0f, -0.6f * sinf(Ahead) }, &Ball,
        TestBodyKind_Pushable);
    Step(&Scene);
    bool IsAlwaysAhead = true;
    bool IsNeverInside = true;
    for (uint32_t Tick = 1U; Tick <= 3U; Tick++)
    {
        float Yaw = -40.0f * DEG2RAD * (float)Tick;
        TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(GetObject(&Scene, Fan), (Vector3){ 0.0f, Yaw, 0.0f }));
        Step(&Scene);
        Vector3 Position = GetPosition(&Scene, Sphere);
        // The blade lies along (cos yaw, 0, -sin yaw): the sphere must be on its leading side, clear of it.
        Vector3 Along = { cosf(Yaw), 0.0f, -sinf(Yaw) };
        Vector3 Leading = { -sinf(-Yaw) * 0.0f + Along.z * -1.0f, 0.0f, Along.x };
        float Side = Vector3DotProduct(Position, Leading);
        IsAlwaysAhead = IsAlwaysAhead && (Side > 0.0f);
        float AlongDistance = Vector3DotProduct(Position, Along);
        bool IsOnBlade = (AlongDistance > -0.1f) && (AlongDistance < 1.1f);
        IsNeverInside = IsNeverInside && (!IsOnBlade || (Side >= (0.05f + 0.1f - TEST_EPSILON)));
    }
    TEST_CHECK(run, IsAlwaysAhead);
    TEST_CHECK(run, IsNeverInside);
    WorldCollisionStats Stats = WorldCollision_GetStats(Scene.Collision);
    TEST_CHECK(run, Stats.SubstepCount == 3U); // 40 degrees in three substeps of at most 15
    MotionScene_Deconstruct(run, &Scene);
}

static void TwoFastSpheresMeetInTheMiddle(TestRun* run)
{
    MotionScene Scene;
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    CollisionShape Ball = MakeSphere(0.1f);
    uint64_t Left = AddShapeObject(run, &Scene, (Vector3){ -5.0f, 0.0f, 0.0f }, &Ball, TestBodyKind_Pushable);
    uint64_t Right = AddShapeObject(run, &Scene, (Vector3){ 5.0f, 0.0f, 0.0f }, &Ball, TestBodyKind_Pushable);
    Step(&Scene);
    bool IsAlwaysApart = true;
    for (uint32_t Tick = 0U; Tick < 12U; Tick++)
    {
        MoveBy(run, &Scene, Left, (Vector3){ 1000.0f * TEST_DELTA, 0.0f, 0.0f });
        MoveBy(run, &Scene, Right, (Vector3){ -1000.0f * TEST_DELTA, 0.0f, 0.0f });
        Step(&Scene);
        IsAlwaysApart = IsAlwaysApart && ((GetPosition(&Scene, Right).x - GetPosition(&Scene, Left).x) >= 0.2f);
    }
    TEST_CHECK(run, IsAlwaysApart);
    TEST_CHECK(run, IsNear(GetPosition(&Scene, Left).x, -0.1f - (0.5f * WORLD_COLLISION_SKIN)));
    TEST_CHECK(run, IsNear(GetPosition(&Scene, Right).x, 0.1f + (0.5f * WORLD_COLLISION_SKIN)));
    MotionScene_Deconstruct(run, &Scene);
}


static void CollidersThatAreNotContinuousAreStillSweptAgainst(TestRun* run)
{
    MotionScene Scene;
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    // A sphere at 1000 m/s toward a kinematic wall that slides sideways and whose own motion is not swept: the sphere's
    // sweep still sees the wall (where it ends up).
    CollisionShape Slab = MakeBox((Vector3){ 0.05f, 2.0f, 2.0f });
    uint64_t Wall = AddShapeObject(run, &Scene, (Vector3){ 5.0f, 0.0f, 0.0f }, &Slab, TestBodyKind_Kinematic);
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetContinuous(GetCollider(&Scene, Wall), false));
    CollisionShape Ball = MakeSphere(0.1f);
    uint64_t Sphere = AddShapeObject(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f }, &Ball, TestBodyKind_Pushable);
    Step(&Scene);
    float Farthest = -1000.0f;
    for (uint32_t Tick = 0U; Tick < 8U; Tick++)
    {
        MoveBy(run, &Scene, Sphere, (Vector3){ 1000.0f * TEST_DELTA, 0.0f, 0.0f });
        MoveBy(run, &Scene, Wall, (Vector3){ 0.0f, 0.0f, 0.001f });
        Step(&Scene);
        Farthest = fmaxf(Farthest, GetPosition(&Scene, Sphere).x);
    }
    TEST_CHECK(run, Farthest <= (4.95f - 0.1f));
    // A door moving 0.5 m a tick into a resting crate whose own motion is not swept: the crate is pushed ahead of it.
    CollisionShape Panel = MakeBox((Vector3){ 0.05f, 1.0f, 1.0f });
    uint64_t Door = AddShapeObject(run, &Scene, (Vector3){ -1.0f, 0.0f, 10.0f }, &Panel, TestBodyKind_Kinematic);
    CollisionShape Crate = MakeBox((Vector3){ 0.2f, 0.2f, 0.2f });
    uint64_t Box = AddShapeObject(run, &Scene, (Vector3){ 0.0f, 0.0f, 10.0f }, &Crate, TestBodyKind_Pushable);
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetContinuous(GetCollider(&Scene, Box), false));
    Step(&Scene);
    bool IsAlwaysAhead = true;
    for (uint32_t Tick = 0U; Tick < 4U; Tick++)
    {
        MoveBy(run, &Scene, Door, (Vector3){ 0.5f, 0.0f, 0.0f });
        Step(&Scene);
        IsAlwaysAhead = IsAlwaysAhead && ((GetPosition(&Scene, Box).x - GetPosition(&Scene, Door).x)
            >= (0.25f - WORLD_COLLISION_SLOP));
    }
    TEST_CHECK(run, IsAlwaysAhead);
    MotionScene_Deconstruct(run, &Scene);
}

static void AFastPushableIsSweptAgainstASlowOneWhicheverCameFirst(TestRun* run)
{
    // A 1000 kg sphere at 100 m/s into a 1 kg crate pinned on a wall and nudged 1 mm a tick by its driver (so both are
    // pushed and both move): the sphere stops at the crate, whichever of the two was made first.
    for (uint32_t Order = 0U; Order < 2U; Order++)
    {
        MotionScene Scene;
        if (!MotionScene_Construct(run, &Scene))
        {
            MotionScene_Deconstruct(run, &Scene);
            return;
        }
        CollisionShape Slab = MakeBox((Vector3){ 0.1f, 2.0f, 2.0f });
        AddShapeObject(run, &Scene, (Vector3){ 5.3f, 0.0f, 0.0f }, &Slab, TestBodyKind_Static);
        CollisionShape Crate = MakeBox((Vector3){ 0.2f, 0.2f, 0.2f });
        CollisionShape Ball = MakeSphere(0.1f);
        Vector3 CratePosition = { 5.0f - WORLD_COLLISION_SKIN, 0.0f, 0.0f };
        uint64_t Box = (Order == 0U) ? AddShapeObject(run, &Scene, CratePosition, &Crate, TestBodyKind_Pushable) : 0U;
        uint64_t Sphere = AddShapeObject(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f }, &Ball, TestBodyKind_Pushable);
        Box = (Order == 1U) ? AddShapeObject(run, &Scene, CratePosition, &Crate, TestBodyKind_Pushable) : Box;
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOverridden(GetCollider(&Scene, Sphere),
            WorldColliderSetting_Mass, true));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetMass(GetCollider(&Scene, Sphere), 1000.0f));
        Step(&Scene);
        float Farthest = -1000.0f;
        for (uint32_t Tick = 0U; Tick < 40U; Tick++)
        {
            MoveBy(run, &Scene, Sphere, (Vector3){ 100.0f * TEST_DELTA, 0.0f, 0.0f });
            MoveBy(run, &Scene, Box, (Vector3){ 0.0f, 0.001f, 0.0f });
            Step(&Scene);
            Farthest = fmaxf(Farthest, GetPosition(&Scene, Sphere).x);
        }
        TEST_CHECK(run, Farthest <= ((CratePosition.x - 0.2f) - 0.1f + WORLD_COLLISION_SLOP));
        TEST_CHECK(run, IsNear(GetPosition(&Scene, Box).x, CratePosition.x));
        MotionScene_Deconstruct(run, &Scene);
    }
}


// Static functions: pushing.
/* A kinematic wall from x = -1 pushing toward a static wall at x = 3.2; returns the pusher's id. */
static uint64_t AddPushingWalls(TestRun* run, MotionScene* scene, WorldColliderBlockedMode whenBlocked)
{
    CollisionShape Slab = MakeBox((Vector3){ 0.1f, 1.0f, 2.0f });
    AddShapeObject(run, scene, (Vector3){ 3.3f, 0.0f, 0.0f }, &Slab, TestBodyKind_Static);
    uint64_t Pusher = AddShapeObject(run, scene, (Vector3){ -1.0f, 0.0f, 0.0f }, &Slab, TestBodyKind_Kinematic);
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetWhenBlocked(GetCollider(scene, Pusher), whenBlocked));
    return Pusher;
}

static void AWallPushesARowAtOnceAndStopsWhenItIsPinned(TestRun* run)
{
    MotionScene Scene;
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    uint64_t Pusher = AddPushingWalls(run, &Scene, WorldColliderBlockedMode_Stop);
    CollisionShape Crate = MakeBox((Vector3){ 0.2f, 0.2f, 0.2f });
    uint64_t Row[5];
    for (size_t Index = 0U; Index < 5U; Index++)
    {
        Row[Index] = AddShapeObject(run, &Scene, (Vector3){ (float)Index * 0.5f, 0.0f, 0.0f }, &Crate,
            TestBodyKind_Pushable);
    }
    Step(&Scene);
    WorldCollision_ClearMotionEvents(Scene.Collision);
    // The pusher drives at 2 m/s for 1.5 s: far past where the row packs against the far wall.
    float DeepestDuring = -1.0f;
    for (uint32_t Tick = 0U; Tick < 720U; Tick++)
    {
        MoveBy(run, &Scene, Pusher, (Vector3){ 2.0f * TEST_DELTA, 0.0f, 0.0f });
        Step(&Scene);
        DeepestDuring = fmaxf(DeepestDuring, GetDeepestBoxOverlap(&Scene, Row, 5U, 0.2f));
    }
    TEST_CHECK(run, DeepestDuring <= WORLD_COLLISION_SLOP);
    // Packed: the last crate against the far wall (its face at 3.2), each one a skin from the next, and the pusher
    // stopped against the first.
    TEST_CHECK(run, GetPosition(&Scene, Row[4]).x <= (3.2f - 0.2f));
    TEST_CHECK(run, IsNear(GetPosition(&Scene, Row[4]).x, 3.2f - 0.2f - WORLD_COLLISION_SKIN));
    TEST_CHECK(run, IsNear(GetPosition(&Scene, Row[0]).x, 3.2f - 2.0f - (5.0f * WORLD_COLLISION_SKIN) + 0.2f));
    TEST_CHECK(run, GetPosition(&Scene, Pusher).x <= (GetPosition(&Scene, Row[0]).x - 0.3f + WORLD_COLLISION_SLOP));
    WorldMotionResult Result;
    TEST_CHECK(run, WorldCollision_GetMotionResult(Scene.Collision, Pusher, &Result));
    TEST_CHECK(run, Result.IsBlocked && Result.IsStopped && (Result.BlockedById == Row[0]));
    TEST_CHECK(run, WorldCollision_GetStats(Scene.Collision).BlockedCount == 1U); // set back, it has no body left
    TEST_CHECK(run, CountMotionEvents(&Scene, WorldMotionEventKind_BlockedBegin, Pusher) == 1U);
    for (size_t Index = 0U; Index < 5U; Index++)
    {
        TEST_CHECK(run, WorldCollision_GetMotionResult(Scene.Collision, Row[Index], &Result) && !Result.IsCrushed);
    }
    // Backing off frees it: the block ends.
    WorldCollision_ClearMotionEvents(Scene.Collision);
    MoveBy(run, &Scene, Pusher, (Vector3){ -0.5f, 0.0f, 0.0f });
    Step(&Scene);
    TEST_CHECK(run, CountMotionEvents(&Scene, WorldMotionEventKind_BlockedEnd, Pusher) == 1U);
    MotionScene_Deconstruct(run, &Scene);
}

static void AWallPushesAGridAtOnceAndPushingThroughCrushesThePinned(TestRun* run)
{
    MotionScene Scene;
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    uint64_t Pusher = AddPushingWalls(run, &Scene, WorldColliderBlockedMode_PushThrough);
    CollisionShape Crate = MakeBox((Vector3){ 0.1f, 0.1f, 0.1f });
    uint64_t Grid[49];
    for (size_t Index = 0U; Index < 49U; Index++)
    {
        float X = 0.3f * (float)(Index % 7U);
        float Z = -0.9f + (0.3f * (float)(Index / 7U));
        Grid[Index] = AddShapeObject(run, &Scene, (Vector3){ X, 0.0f, Z }, &Crate, TestBodyKind_Pushable);
    }
    Step(&Scene);
    // Until the grid is packed (7 crates of 0.2 m in front of the far wall's face at 3.2) nothing overlaps.
    float DeepestWhilePushing = -1.0f;
    for (uint32_t Tick = 0U; (Tick < 1000U) && ((GetPosition(&Scene, Pusher).x + 0.1f) < (3.2f - 1.42f)); Tick++)
    {
        MoveBy(run, &Scene, Pusher, (Vector3){ 2.0f * TEST_DELTA, 0.0f, 0.0f });
        Step(&Scene);
        DeepestWhilePushing = fmaxf(DeepestWhilePushing, GetDeepestBoxOverlap(&Scene, Grid, 49U, 0.1f));
    }
    TEST_CHECK(run, DeepestWhilePushing <= WORLD_COLLISION_SLOP);
    TEST_CHECK(run, (GetPosition(&Scene, Pusher).x + 0.1f) >= (3.2f - 1.42f));
    // Pushing on through the packed grid: the pinned crates are crushed, and the pusher is blocked but moves on.
    WorldCollision_ClearMotionEvents(Scene.Collision);
    float PusherBefore = GetPosition(&Scene, Pusher).x;
    for (uint32_t Tick = 0U; Tick < 60U; Tick++)
    {
        MoveBy(run, &Scene, Pusher, (Vector3){ 2.0f * TEST_DELTA, 0.0f, 0.0f });
        Step(&Scene);
    }
    TEST_CHECK(run, GetPosition(&Scene, Pusher).x > (PusherBefore + 0.1f));
    WorldMotionResult Result;
    TEST_CHECK(run, WorldCollision_GetMotionResult(Scene.Collision, Pusher, &Result) && Result.IsBlocked
        && !Result.IsStopped);
    TEST_CHECK(run, WorldCollision_GetStats(Scene.Collision).BlockedCount == 1U);
    size_t Crushed = 0U;
    for (size_t Index = 0U; Index < 49U; Index++)
    {
        Crushed += (WorldCollision_GetMotionResult(Scene.Collision, Grid[Index], &Result) && Result.IsCrushed) ? 1U : 0U;
    }
    TEST_CHECK(run, Crushed > 0U);
    size_t EventCount = 0U;
    const WorldMotionEvent* Events = WorldCollision_GetMotionEvents(Scene.Collision, &EventCount);
    size_t CrushedBegins = 0U;
    for (size_t Index = 0U; Index < EventCount; Index++)
    {
        CrushedBegins += (Events[Index].Kind == WorldMotionEventKind_CrushedBegin) ? 1U : 0U;
    }
    TEST_CHECK(run, CrushedBegins > 0U);
    MotionScene_Deconstruct(run, &Scene);
}

static void MassesShareAPush(TestRun* run)
{
    // Two spheres placed overlapping by 0.2 m are pushed apart in their first step: equal masses by half each, a
    // ten-times heavier one by a tenth of the lighter one's push.
    float Masses[2][2] = { { 1.0f, 1.0f }, { 1.0f, 10.0f } };
    for (size_t Case = 0U; Case < 2U; Case++)
    {
        MotionScene Scene;
        if (!MotionScene_Construct(run, &Scene))
        {
            MotionScene_Deconstruct(run, &Scene);
            return;
        }
        CollisionShape Ball = MakeSphere(0.5f);
        uint64_t Left = AddShapeObject(run, &Scene, (Vector3){ -0.4f, 0.0f, 0.0f }, &Ball, TestBodyKind_Pushable);
        uint64_t Right = AddShapeObject(run, &Scene, (Vector3){ 0.4f, 0.0f, 0.0f }, &Ball, TestBodyKind_Pushable);
        uint64_t Ids[2] = { Left, Right };
        for (size_t Side = 0U; Side < 2U; Side++)
        {
            WorldColliderComponent* Collider = GetCollider(&Scene, Ids[Side]);
            TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOverridden(Collider, WorldColliderSetting_Mass, true));
            TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetMass(Collider, Masses[Case][Side]));
        }
        Step(&Scene);
        float Push = 0.2f + WORLD_COLLISION_SKIN;
        float ShareLeft = Masses[Case][1] / (Masses[Case][0] + Masses[Case][1]);
        TEST_CHECK(run, IsNear(GetPosition(&Scene, Left).x, -0.4f - (Push * ShareLeft)));
        TEST_CHECK(run, IsNear(GetPosition(&Scene, Right).x, 0.4f + (Push * (1.0f - ShareLeft))));
        TEST_CHECK(run, IsNear(GetPosition(&Scene, Right).x - GetPosition(&Scene, Left).x, 1.0f + WORLD_COLLISION_SKIN));
        MotionScene_Deconstruct(run, &Scene);
    }
}

static void PushablesSlideAlongWalls(TestRun* run)
{
    MotionScene Scene;
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    CollisionShape Wall = MakeBox((Vector3){ 0.05f, 2.0f, 4.0f });
    AddShapeObject(run, &Scene, (Vector3){ 1.0f, 0.0f, 0.0f }, &Wall, TestBodyKind_Static);
    CollisionShape Ball = MakeSphere(0.1f);
    uint64_t Sphere = AddShapeObject(run, &Scene, (Vector3){ 0.5f, 0.0f, 0.0f }, &Ball, TestBodyKind_Pushable);
    Step(&Scene);
    MoveBy(run, &Scene, Sphere, (Vector3){ 1.0f, 0.0f, 1.0f });
    Step(&Scene);
    Vector3 Position = GetPosition(&Scene, Sphere);
    TEST_CHECK(run, IsNear(Position.x, 0.95f - 0.1f - WORLD_COLLISION_SKIN));
    TEST_CHECK(run, IsNear(Position.z, 1.0f)); // the whole motion along the wall
    WorldMotionResult Result;
    TEST_CHECK(run, WorldCollision_GetMotionResult(Scene.Collision, Sphere, &Result));
    TEST_CHECK(run, IsNear(Result.Correction.x, (0.95f - 0.1f - WORLD_COLLISION_SKIN) - 1.5f));
    TEST_CHECK(run, IsNear(Result.Correction.z, 0.0f));
    MotionScene_Deconstruct(run, &Scene);
}


/* A train of kinematic doors that stop when blocked, each a skin behind a crate, the last crate a skin before a wall:
 * @p count doors and crates (at most 12), from the farthest from the wall. */
static void AddDoorTrain(TestRun* run, MotionScene* scene, uint32_t count, uint64_t* outDoors, uint64_t* outCrates)
{
    CollisionShape Slab = MakeBox((Vector3){ 0.05f, 1.0f, 1.0f });
    AddShapeObject(run, scene, (Vector3){ 10.0f, 0.0f, 0.0f }, &Slab, TestBodyKind_Static);
    CollisionShape Panel = MakeBox((Vector3){ 0.05f, 0.5f, 0.5f });
    CollisionShape Crate = MakeBox((Vector3){ 0.2f, 0.2f, 0.2f });
    float Right = 9.95f - WORLD_COLLISION_SKIN;
    for (uint32_t Index = count; Index > 0U; Index--)
    {
        float CrateX = Right - 0.2f;
        outCrates[Index - 1U] = AddShapeObject(run, scene, (Vector3){ CrateX, 0.0f, 0.0f }, &Crate,
            TestBodyKind_Pushable);
        float DoorX = CrateX - 0.2f - WORLD_COLLISION_SKIN - 0.05f;
        outDoors[Index - 1U] = AddShapeObject(run, scene, (Vector3){ DoorX, 0.0f, 0.0f }, &Panel,
            TestBodyKind_Kinematic);
        Right = DoorX - 0.05f - WORLD_COLLISION_SKIN;
    }
}

static void ATrainOfStoppingDoorsStopsDoorByDoor(TestRun* run)
{
    // Every door moves 5 mm toward the wall. Each door set back pins the crate behind it, which blocks the door behind
    // that in the next round: five doors stop in five rounds.
    uint64_t Doors[12];
    uint64_t Crates[12];
    MotionScene Scene;
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    AddDoorTrain(run, &Scene, 5U, Doors, Crates);
    Step(&Scene);
    WorldCollision_ClearMotionEvents(Scene.Collision);
    float Before[5];
    for (size_t Index = 0U; Index < 5U; Index++)
    {
        Before[Index] = GetPosition(&Scene, Doors[Index]).x;
        MoveBy(run, &Scene, Doors[Index], (Vector3){ 0.005f, 0.0f, 0.0f });
    }
    Step(&Scene);
    for (size_t Index = 0U; Index < 5U; Index++)
    {
        WorldMotionResult Result;
        TEST_CHECK(run, fabsf(GetPosition(&Scene, Doors[Index]).x - Before[Index]) < 1e-6f);
        TEST_CHECK(run, WorldCollision_GetMotionResult(Scene.Collision, Doors[Index], &Result) && Result.IsBlocked
            && Result.IsStopped);
        TEST_CHECK(run, CountMotionEvents(&Scene, WorldMotionEventKind_BlockedBegin, Doors[Index]) == 1U);
    }
    WorldCollisionStats Stats = WorldCollision_GetStats(Scene.Collision);
    TEST_CHECK(run, (Stats.BlockedCount == 5U) && (Stats.ResolveCount == 5U) && !(Stats.DeepestOverlap > 0.0f));
    MotionScene_Deconstruct(run, &Scene);
    // Ten doors: past WORLD_COLLISION_MAX_BLOCK_ROUNDS the doors still blocked push through, and the overlap they leave
    // is reported (the last round's solve runs through: its contacts, its crushed crates).
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    AddDoorTrain(run, &Scene, 10U, Doors, Crates);
    Step(&Scene);
    for (size_t Index = 0U; Index < 10U; Index++)
    {
        MoveBy(run, &Scene, Doors[Index], (Vector3){ 0.005f, 0.0f, 0.0f });
    }
    Step(&Scene);
    uint32_t Stopped = 0U;
    float Deepest = 0.0f;
    bool IsEveryOverlapReported = true;
    for (size_t Index = 0U; Index < 10U; Index++)
    {
        WorldMotionResult Result;
        Stopped += (WorldCollision_GetMotionResult(Scene.Collision, Doors[Index], &Result) && Result.IsStopped) ? 1U
            : 0U;
        float CrateX = GetPosition(&Scene, Crates[Index]).x;
        float Behind = 0.25f - (CrateX - GetPosition(&Scene, Doors[Index]).x);
        float Ahead = 0.25f - (((Index < 9U) ? GetPosition(&Scene, Doors[Index + 1U]).x : 10.0f) - CrateX);
        float Overlap = fmaxf(Behind, Ahead);
        Deepest = fmaxf(Deepest, Overlap);
        bool IsCrushed = WorldCollision_GetMotionResult(Scene.Collision, Crates[Index], &Result) && Result.IsCrushed
            && (Result.CrushDepth >= (Overlap - 1e-4f));
        IsEveryOverlapReported = IsEveryOverlapReported && (!(Overlap > WORLD_COLLISION_SLOP) || IsCrushed);
    }
    TEST_CHECK(run, Stopped == WORLD_COLLISION_MAX_BLOCK_ROUNDS);
    TEST_CHECK(run, Deepest > WORLD_COLLISION_SLOP); // a door pushed through
    TEST_CHECK(run, IsEveryOverlapReported);
    TEST_CHECK(run, WorldCollision_GetStats(Scene.Collision).DeepestOverlap >= (Deepest - 1e-4f));
    MotionScene_Deconstruct(run, &Scene);
}

static void AMoverMovingAwayFromASqueezedPushableIsNotBlocked(TestRun* run)
{
    MotionScene Scene;
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    // A crate on an elevator, a skin from a wall on its right; a door on its left squeezes it into the wall (pushing
    // through) while the elevator (stop when blocked) moves down, away from it.
    CollisionShape Deck = MakeBox((Vector3){ 1.0f, 0.1f, 1.0f });
    uint64_t Elevator = AddShapeObject(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f }, &Deck, TestBodyKind_Kinematic);
    CollisionShape Crate = MakeBox((Vector3){ 0.2f, 0.2f, 0.2f });
    AddShapeObject(run, &Scene, (Vector3){ 0.0f, 0.3f + WORLD_COLLISION_SKIN, 0.0f }, &Crate, TestBodyKind_Pushable);
    CollisionShape Slab = MakeBox((Vector3){ 0.05f, 1.0f, 1.0f });
    AddShapeObject(run, &Scene, (Vector3){ 0.25f + WORLD_COLLISION_SKIN, 1.0f, 0.0f }, &Slab, TestBodyKind_Static);
    CollisionShape Panel = MakeBox((Vector3){ 0.05f, 0.5f, 1.0f });
    uint64_t Door = AddShapeObject(run, &Scene, (Vector3){ -0.25f - WORLD_COLLISION_SKIN, 0.5f, 0.0f }, &Panel,
        TestBodyKind_Kinematic);
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetWhenBlocked(GetCollider(&Scene, Door),
        WorldColliderBlockedMode_PushThrough));
    Step(&Scene);
    Step(&Scene);
    WorldCollision_ClearMotionEvents(Scene.Collision);
    float Start = GetPosition(&Scene, Elevator).y;
    for (uint32_t Tick = 0U; Tick < 10U; Tick++)
    {
        MoveBy(run, &Scene, Door, (Vector3){ 0.002f, 0.0f, 0.0f });
        MoveBy(run, &Scene, Elevator, (Vector3){ 0.0f, -0.002f, 0.0f });
        Step(&Scene);
    }
    // The elevator went all the way; the door, pressing into the pinned crate, is the one blocked.
    TEST_CHECK(run, fabsf(GetPosition(&Scene, Elevator).y - (Start - 0.02f)) < 1e-5f);
    WorldMotionResult Result;
    TEST_CHECK(run, WorldCollision_GetMotionResult(Scene.Collision, Elevator, &Result) && !Result.IsBlocked);
    TEST_CHECK(run, CountMotionEvents(&Scene, WorldMotionEventKind_BlockedBegin, Elevator) == 0U);
    TEST_CHECK(run, CountMotionEvents(&Scene, WorldMotionEventKind_BlockedBegin, Door) == 1U);
    MotionScene_Deconstruct(run, &Scene);
}

static void AHeavyPushableRestsSteadilyAgainstALightPinnedOne(TestRun* run)
{
    MotionScene Scene;
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    // An 80 kg sphere walking at 5 m/s into a 0.5 kg box against a wall: it comes to rest at the box and stays there.
    CollisionShape Slab = MakeBox((Vector3){ 0.1f, 2.0f, 2.0f });
    AddShapeObject(run, &Scene, (Vector3){ 1.3f, 0.0f, 0.0f }, &Slab, TestBodyKind_Static);
    CollisionShape Crate = MakeBox((Vector3){ 0.2f, 0.2f, 0.2f });
    uint64_t Box = AddShapeObject(run, &Scene, (Vector3){ 1.0f - WORLD_COLLISION_SKIN, 0.0f, 0.0f }, &Crate,
        TestBodyKind_Pushable);
    CollisionShape Ball = MakeSphere(0.3f);
    uint64_t Walker = AddShapeObject(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f }, &Ball, TestBodyKind_Pushable);
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOverridden(GetCollider(&Scene, Walker), WorldColliderSetting_Mass,
        true));
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetMass(GetCollider(&Scene, Walker), 80.0f));
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOverridden(GetCollider(&Scene, Box), WorldColliderSetting_Mass,
        true));
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetMass(GetCollider(&Scene, Box), 0.5f));
    Step(&Scene);
    float Touching = (1.0f - WORLD_COLLISION_SKIN - 0.2f) - 0.3f;
    float Lowest = 1000.0f;
    float Highest = -1000.0f;
    for (uint32_t Tick = 0U; Tick < 240U; Tick++)
    {
        MoveBy(run, &Scene, Walker, (Vector3){ 5.0f * TEST_DELTA, 0.0f, 0.0f });
        Step(&Scene);
        if (Tick >= 120U) // it arrives after about 100 ticks
        {
            Lowest = fminf(Lowest, GetPosition(&Scene, Walker).x);
            Highest = fmaxf(Highest, GetPosition(&Scene, Walker).x);
        }
    }
    TEST_CHECK(run, (Highest - Lowest) < 5e-4f);
    TEST_CHECK(run, (Highest <= (Touching + WORLD_COLLISION_SLOP)) && (Lowest >= (Touching - TEST_EPSILON)));
    TEST_CHECK(run, IsNear(GetPosition(&Scene, Box).x, 1.0f - WORLD_COLLISION_SKIN));
    MotionScene_Deconstruct(run, &Scene);
}

static void AScaledColliderPushesOutAndAShrunkOneLetsGo(TestRun* run)
{
    MotionScene Scene;
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    // A kinematic box scaled twice as large reaches 0.2 m into a resting crate: the crate is pushed out.
    CollisionShape Block = MakeBox((Vector3){ 0.5f, 0.5f, 0.5f });
    uint64_t Grower = AddShapeObject(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f }, &Block, TestBodyKind_Kinematic);
    CollisionShape Crate = MakeBox((Vector3){ 0.2f, 0.2f, 0.2f });
    uint64_t Pushed = AddShapeObject(run, &Scene, (Vector3){ 1.0f, 0.0f, 0.0f }, &Crate, TestBodyKind_Pushable);
    // A crate scaled three times as large reaches 0.3 m into a wall: it is pushed out.
    CollisionShape Slab = MakeBox((Vector3){ 0.1f, 1.0f, 1.0f });
    AddShapeObject(run, &Scene, (Vector3){ 5.0f, 0.0f, 0.0f }, &Slab, TestBodyKind_Static);
    uint64_t Swollen = AddShapeObject(run, &Scene, (Vector3){ 4.6f, 0.0f, 0.0f }, &Crate, TestBodyKind_Pushable);
    // A kinematic box shrunk to half its size lets go of the crate resting against it: the contact ends.
    uint64_t Shrinker = AddShapeObject(run, &Scene, (Vector3){ 10.0f, 0.0f, 0.0f }, &Block, TestBodyKind_Kinematic);
    uint64_t Leaning = AddShapeObject(run, &Scene, (Vector3){ 10.7f + WORLD_COLLISION_SKIN, 0.0f, 0.0f }, &Crate,
        TestBodyKind_Pushable);
    Step(&Scene);
    Step(&Scene);
    WorldCollision_ClearContactEvents(Scene.Collision);
    TEST_CHECK_SUCCESS(run, WorldObject_SetScale(GetObject(&Scene, Grower), (Vector3){ 2.0f, 2.0f, 2.0f }));
    TEST_CHECK_SUCCESS(run, WorldObject_SetScale(GetObject(&Scene, Swollen), (Vector3){ 3.0f, 3.0f, 3.0f }));
    TEST_CHECK_SUCCESS(run, WorldObject_SetScale(GetObject(&Scene, Shrinker), (Vector3){ 0.5f, 0.5f, 0.5f }));
    Step(&Scene);
    Step(&Scene);
    TEST_CHECK(run, IsNear(GetPosition(&Scene, Pushed).x, 1.0f + 0.2f + WORLD_COLLISION_SKIN));
    TEST_CHECK(run, (GetPosition(&Scene, Swollen).x + 0.6f) <= (4.9f + WORLD_COLLISION_SLOP));
    TEST_CHECK(run, CountContactEvents(&Scene, WorldContactEventKind_End, Shrinker, Leaning) == 1U);
    WorldMotionResult Result;
    TEST_CHECK(run, WorldCollision_GetMotionResult(Scene.Collision, Leaning, &Result) && (Result.ContactCount == 0U));
    MotionScene_Deconstruct(run, &Scene);
}


// Static functions: blocked movers, teleports, the hierarchy.
static void ADoorStopsOnAPinnedBoxOrPushesThrough(TestRun* run)
{
    WorldColliderBlockedMode Modes[2] = { WorldColliderBlockedMode_Stop, WorldColliderBlockedMode_PushThrough };
    for (size_t Case = 0U; Case < 2U; Case++)
    {
        MotionScene Scene;
        if (!MotionScene_Construct(run, &Scene))
        {
            MotionScene_Deconstruct(run, &Scene);
            return;
        }
        // A door sliding along X into a crate pinned against a post.
        CollisionShape Panel = MakeBox((Vector3){ 0.05f, 1.0f, 0.5f });
        uint64_t Door = AddShapeObject(run, &Scene, (Vector3){ -1.0f, 0.0f, 0.0f }, &Panel, TestBodyKind_Kinematic);
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetWhenBlocked(GetCollider(&Scene, Door), Modes[Case]));
        CollisionShape Crate = MakeBox((Vector3){ 0.2f, 0.2f, 0.2f });
        uint64_t Box = AddShapeObject(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f }, &Crate, TestBodyKind_Pushable);
        CollisionShape Pillar = MakeBox((Vector3){ 0.1f, 1.0f, 1.0f });
        uint64_t Post = AddShapeObject(run, &Scene, (Vector3){ 0.3f + WORLD_COLLISION_SKIN, 0.0f, 0.0f }, &Pillar,
            TestBodyKind_Static);
        Step(&Scene);
        WorldCollision_ClearMotionEvents(Scene.Collision);
        // At 1 m/s it reaches the crate (its face at -0.2) after 0.75 m, 360 ticks; 400 go 8 cm farther.
        for (uint32_t Tick = 0U; Tick < 400U; Tick++)
        {
            MoveBy(run, &Scene, Door, (Vector3){ 1.0f * TEST_DELTA, 0.0f, 0.0f });
            Step(&Scene);
        }
        WorldMotionResult Result;
        TEST_CHECK(run, WorldCollision_GetMotionResult(Scene.Collision, Door, &Result) && Result.IsBlocked);
        TEST_CHECK(run, Result.BlockedById == Box);
        TEST_CHECK(run, CountMotionEvents(&Scene, WorldMotionEventKind_BlockedBegin, Door) == 1U);
        if (Modes[Case] == WorldColliderBlockedMode_Stop)
        {
            // Stopped against the crate: its face a skin from the crate's.
            TEST_CHECK(run, Result.IsStopped);
            TEST_CHECK(run, IsNear(GetPosition(&Scene, Door).x, -0.2f - 0.05f - WORLD_COLLISION_SKIN));
            TEST_CHECK(run, IsNear(GetPosition(&Scene, Box).x, 0.0f));
            // Freed (the post goes), it goes on, pushing the crate.
            TEST_CHECK_SUCCESS(run, World_RemoveObject(&Scene.World, Post));
            WorldCollision_ClearMotionEvents(Scene.Collision);
            for (uint32_t Tick = 0U; Tick < 48U; Tick++)
            {
                MoveBy(run, &Scene, Door, (Vector3){ 1.0f * TEST_DELTA, 0.0f, 0.0f });
                Step(&Scene);
            }
            TEST_CHECK(run, CountMotionEvents(&Scene, WorldMotionEventKind_BlockedEnd, Door) == 1U);
            TEST_CHECK(run, GetPosition(&Scene, Door).x > -0.2f);
            TEST_CHECK(run, GetPosition(&Scene, Box).x > 0.05f);
        }
        else
        {
            TEST_CHECK(run, !Result.IsStopped);
            TEST_CHECK(run, GetPosition(&Scene, Door).x > -0.2f); // it moved on into the crate
            TEST_CHECK(run, WorldCollision_GetMotionResult(Scene.Collision, Box, &Result) && Result.IsCrushed);
            TEST_CHECK(run, CountMotionEvents(&Scene, WorldMotionEventKind_CrushedBegin, Box) == 1U);
            // The crate never passes the post.
            TEST_CHECK(run, GetPosition(&Scene, Box).x <= (0.3f + WORLD_COLLISION_SKIN - 0.1f - 0.2f + TEST_EPSILON));
        }
        MotionScene_Deconstruct(run, &Scene);
    }
}

static void TeleportsSkipTheSweepAndArePushedOut(TestRun* run)
{
    MotionScene Scene;
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    CollisionShape Wall = MakeBox((Vector3){ 0.005f, 2.0f, 2.0f });
    AddShapeObject(run, &Scene, (Vector3){ 1.0f, 0.0f, 0.0f }, &Wall, TestBodyKind_Static);
    CollisionShape Block = MakeBox((Vector3){ 0.5f, 0.5f, 0.5f });
    AddShapeObject(run, &Scene, (Vector3){ 5.0f, 0.0f, 0.0f }, &Block, TestBodyKind_Static);
    CollisionShape Ball = MakeSphere(0.1f);
    uint64_t Sphere = AddShapeObject(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f }, &Ball, TestBodyKind_Pushable);
    Step(&Scene);
    // A move through the wall is stopped at it...
    MoveTo(run, &Scene, Sphere, (Vector3){ 2.0f, 0.0f, 0.0f });
    Step(&Scene);
    TEST_CHECK(run, IsNear(GetPosition(&Scene, Sphere).x, 1.0f - 0.005f - 0.1f - WORLD_COLLISION_SKIN));
    // ...a teleport through it is not: it starts where it lands.
    MoveTo(run, &Scene, Sphere, (Vector3){ 2.0f, 0.0f, 0.0f });
    WorldCollision_Teleport(Scene.Collision, Sphere);
    Step(&Scene);
    TEST_CHECK(run, IsNear(GetPosition(&Scene, Sphere).x, 2.0f));
    // A teleport into the block is pushed out of it, the shortest way (its face at 4.5).
    MoveTo(run, &Scene, Sphere, (Vector3){ 4.6f, 0.0f, 0.0f });
    WorldCollision_Teleport(Scene.Collision, Sphere);
    Step(&Scene);
    TEST_CHECK(run, IsNear(GetPosition(&Scene, Sphere).x, 4.5f - 0.1f - WORLD_COLLISION_SKIN));
    MotionScene_Deconstruct(run, &Scene);
}

static void APushedChildIsWrittenBackThroughItsParent(TestRun* run)
{
    MotionScene Scene;
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    // A parent turned a quarter about Y and scaled 2: a child's local X is the world's -Z, two metres per unit.
    WorldObject* Parent = NULL;
    TEST_CHECK_SUCCESS(run, World_CreateObject(&Scene.World, (const unsigned char*)u8"parent", &Parent));
    uint64_t ParentId = WorldObject_GetId(Parent);
    TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Parent, TestWorld_At((Vector3){ 1.0f, 0.0f, 0.0f })));
    TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(Parent, (Vector3){ 0.0f, 90.0f * DEG2RAD, 0.0f }));
    TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Parent, (Vector3){ 2.0f, 2.0f, 2.0f }));
    TEST_CHECK_SUCCESS(run, WorldObject_SetMobility(Parent, WorldMobility_Movable));
    CollisionShape Ball = MakeSphere(0.05f); // 0.1 m in the world
    uint64_t Child = AddShapeObject(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f }, &Ball, TestBodyKind_Pushable);
    TEST_CHECK_SUCCESS(run, World_SetParent(&Scene.World, Child, ParentId, false));
    TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(GetObject(&Scene, Child), Vector3Double_Zero())); // at its parent
    // A wall whose face at x = 1.05 overlaps the child (world x 0.9 to 1.1) by 5 cm.
    CollisionShape Wall = MakeBox((Vector3){ 0.5f, 1.0f, 1.0f });
    AddShapeObject(run, &Scene, (Vector3){ 1.55f, 0.0f, 0.0f }, &Wall, TestBodyKind_Static);
    Step(&Scene);
    Vector3 Placed = GetPosition(&Scene, Child);
    TEST_CHECK(run, IsNear(Placed.x, 1.05f - 0.1f - WORLD_COLLISION_SKIN));
    TEST_CHECK(run, IsNear(Placed.z, 0.0f));
    // In the parent's space (a quarter turn about Y: its local +Z is the world's +X) at half the length.
    Vector3 Local = GameMath_ToVector3(WorldObject_GetPosition(GetObject(&Scene, Child)));
    TEST_CHECK(run, IsNear(Local.x, 0.0f) && IsNear(Local.y, 0.0f));
    TEST_CHECK(run, IsNear(Local.z, -0.5f * (0.05f + WORLD_COLLISION_SKIN)));
    TEST_CHECK(run, IsNear(GetPosition(&Scene, ParentId).x, 1.0f)); // the parent stays
    MotionScene_Deconstruct(run, &Scene);
}

static void APushableCarriesItsColliders(TestRun* run)
{
    MotionScene Scene;
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    // A pushable crate with a kinematic handle sticking out of it in +X; the handle hits a wall first.
    CollisionShape Crate = MakeBox((Vector3){ 0.2f, 0.2f, 0.2f });
    uint64_t Carrier = AddShapeObject(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f }, &Crate, TestBodyKind_Pushable);
    CollisionShape Handle = MakeBox((Vector3){ 0.1f, 0.05f, 0.05f });
    uint64_t Carried = AddShapeObject(run, &Scene, (Vector3){ 0.3f, 0.0f, 0.0f }, &Handle, TestBodyKind_Kinematic);
    TEST_CHECK_SUCCESS(run, World_SetParent(&Scene.World, Carried, Carrier, true));
    CollisionShape Wall = MakeBox((Vector3){ 0.05f, 1.0f, 1.0f });
    AddShapeObject(run, &Scene, (Vector3){ 1.0f, 0.0f, 0.0f }, &Wall, TestBodyKind_Static);
    Step(&Scene);
    MoveBy(run, &Scene, Carrier, (Vector3){ 1.0f, 0.0f, 0.0f });
    Step(&Scene);
    // The handle (its tip 0.4 ahead of the crate) stops a skin before the wall's face at 0.95; the crate with it.
    TEST_CHECK(run, IsNear(GetPosition(&Scene, Carried).x + 0.1f, 0.95f - WORLD_COLLISION_SKIN));
    TEST_CHECK(run, IsNear(GetPosition(&Scene, Carrier).x, 0.95f - WORLD_COLLISION_SKIN - 0.4f));
    // The crate and its own handle never block each other.
    size_t ContactCount = 0U;
    const WorldContact* Contacts = WorldCollision_GetContacts(Scene.Collision, &ContactCount);
    bool IsSelfContact = false;
    for (size_t Index = 0U; Index < ContactCount; Index++)
    {
        IsSelfContact = IsSelfContact || ((Contacts[Index].Manifold.ShapeA.ObjectId == Carrier)
            && (Contacts[Index].Manifold.ShapeB.ObjectId == Carried));
    }
    TEST_CHECK(run, !IsSelfContact);
    MotionScene_Deconstruct(run, &Scene);
}


static void APushableChildStaysWhenItsParentIsPushed(TestRun* run)
{
    MotionScene Scene;
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    // A pushable trailer under a pushable cart, 2 m from it: a door pushes the cart; the trailer is pushed on its own
    // and stays where it is, then nothing it did not do is reported.
    CollisionShape Crate = MakeBox((Vector3){ 0.2f, 0.2f, 0.2f });
    uint64_t Cart = AddShapeObject(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f }, &Crate, TestBodyKind_Pushable);
    uint64_t Trailer = AddShapeObject(run, &Scene, (Vector3){ 0.0f, 0.0f, 2.0f }, &Crate, TestBodyKind_Pushable);
    TEST_CHECK_SUCCESS(run, World_SetParent(&Scene.World, Trailer, Cart, true));
    CollisionShape Panel = MakeBox((Vector3){ 0.05f, 0.5f, 0.15f });
    uint64_t Door = AddShapeObject(run, &Scene, (Vector3){ -0.3f, 0.0f, 0.0f }, &Panel, TestBodyKind_Kinematic);
    Step(&Scene);
    Step(&Scene);
    Vector3 TrailerBefore = GetPosition(&Scene, Trailer);
    MoveBy(run, &Scene, Door, (Vector3){ 0.1f, 0.0f, 0.0f });
    Step(&Scene);
    TEST_CHECK(run, IsNear(GetPosition(&Scene, Cart).x, 0.05f + WORLD_COLLISION_SKIN));
    TEST_CHECK(run, Vector3Length(Vector3Subtract(GetPosition(&Scene, Trailer), TrailerBefore)) < 1e-5f);
    Step(&Scene);
    WorldMotionResult Result;
    TEST_CHECK(run, WorldCollision_GetMotionResult(Scene.Collision, Trailer, &Result)
        && (Vector3Length(Result.Velocity) == 0.0f));
    TEST_CHECK(run, WorldCollision_GetStats(Scene.Collision).MoverCount == 0U);
    MotionScene_Deconstruct(run, &Scene);
}

static void AReparentedMoverPushesThroughInsteadOfJumping(TestRun* run)
{
    MotionScene Scene;
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    // A door (stop when blocked) a skin from a crate pinned on a wall is put under a new parent (keeping its place) and
    // moved into the crate in the same tick: its last local transform means nothing under the new parent, so it is not
    // set back but pushes through, where its driver put it.
    CollisionShape Slab = MakeBox((Vector3){ 0.05f, 1.0f, 1.0f });
    AddShapeObject(run, &Scene, (Vector3){ 1.0f, 0.0f, 0.0f }, &Slab, TestBodyKind_Static);
    CollisionShape Crate = MakeBox((Vector3){ 0.2f, 0.2f, 0.2f });
    uint64_t Box = AddShapeObject(run, &Scene, (Vector3){ 0.75f - WORLD_COLLISION_SKIN, 0.0f, 0.0f }, &Crate,
        TestBodyKind_Pushable);
    CollisionShape Panel = MakeBox((Vector3){ 0.05f, 0.5f, 0.5f });
    uint64_t Door = AddShapeObject(run, &Scene, (Vector3){ 0.5f - (2.0f * WORLD_COLLISION_SKIN), 0.0f, 0.0f },
        &Panel, TestBodyKind_Kinematic);
    WorldObject* Holder = NULL;
    TEST_CHECK_SUCCESS(run, World_CreateObject(&Scene.World, (const unsigned char*)u8"holder", &Holder));
    TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Holder, TestWorld_At((Vector3){ -5.0f, 0.0f, 0.0f })));
    TEST_CHECK_SUCCESS(run, WorldObject_SetMobility(Holder, WorldMobility_Movable));
    Step(&Scene);
    Step(&Scene);
    TEST_CHECK_SUCCESS(run, World_SetParent(&Scene.World, Door, WorldObject_GetId(Holder), true));
    MoveBy(run, &Scene, Door, (Vector3){ 0.005f, 0.0f, 0.0f });
    Vector3 Target = GetPosition(&Scene, Door);
    Step(&Scene);
    WorldMotionResult Result;
    TEST_CHECK(run, Vector3Length(Vector3Subtract(GetPosition(&Scene, Door), Target)) < 1e-5f);
    TEST_CHECK(run, WorldCollision_GetMotionResult(Scene.Collision, Door, &Result) && Result.IsBlocked
        && !Result.IsStopped && (Result.BlockedById == Box));
    MotionScene_Deconstruct(run, &Scene);
}


// Static functions: events, results, sleeping, determinism.
/* T33: spheres their drivers push onto a box every tick, one down onto its top and one sideways against its side, rest
 * the skin from it, to a micrometre, and slide nowhere along it; their contacts and ground points lie where they
 * touch, to a micrometre; one driven along the top reports the velocity it slides at. 173 km from the world's origin in
 * the far run, where a float's step is 8 mm. */
static void SpheresRestingOnABoxStayASkinFromIt(TestRun* run)
{
    MotionScene Scene;
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    // The box's top at y = 0, its +X side at x = 1.345.
    CollisionShape Block = MakeBox((Vector3){ 1.0f, 0.5f, 1.0f });
    Vector3 BoxCenter = { 0.345f, -0.5f, 0.123f };
    AddShapeObject(run, &Scene, BoxCenter, &Block, TestBodyKind_Static);
    CollisionShape Ball = MakeSphere(0.25f);
    const Vector3 Starts[] = { { 0.3f, 0.3f, -0.2f }, { 1.645f, -0.5f, 0.3f } };
    const Vector3 Pushes[] = { { 0.0f, -0.01f, 0.0f }, { -0.01f, 0.0f, 0.0f } };
    const Vector3 Faces[] = { { 0.3f, 0.0f, -0.2f }, { 1.345f, -0.5f, 0.3f } }; // the face's point under each centre
    for (size_t Index = 0U; Index < 2U; Index++)
    {
        uint64_t Sphere = AddShapeObject(run, &Scene, Starts[Index], &Ball, TestBodyKind_Pushable);
        Vector3Double Face = TestWorld_At(Faces[Index]);
        Vector3Double Normal = Vector3Double_Negate(GameMath_FromVector3(Vector3Normalize(Pushes[Index])));
        double WorstGap = 0.0;
        double WorstSlide = 0.0;
        for (uint32_t Tick = 0U; Tick < 1000U; Tick++)
        {
            MoveBy(run, &Scene, Sphere, Pushes[Index]);
            Step(&Scene);
            if (Tick >= 10U) // it reaches the box within six ticks
            {
                Vector3Double Offset = Vector3Double_Subtract(WorldObject_GetWorldPosition(GetObject(&Scene, Sphere)),
                    Face);
                double Gap = Vector3Double_Dot(Offset, Normal) - 0.25;
                WorstGap = fmax(WorstGap, fabs(Gap - (double)WORLD_COLLISION_SKIN));
                WorstSlide = fmax(WorstSlide, Vector3Double_Length(Vector3Double_Reject(Offset, Normal)));
            }
        }
        TEST_CHECK(run, (WorstGap < 1e-6) && (WorstSlide < 1e-6));

        // Its contact (A the box, made first): on the face, and on the sphere a skin off it, in the world and in each
        // object's own space; the top's sphere stands on the box, its ground point on the face.
        size_t ContactCount = 0U;
        const WorldContact* Contacts = WorldCollision_GetContacts(Scene.Collision, &ContactCount);
        Vector3Double OnSphere = Vector3Double_AddScaled(Face, Normal, (double)WORLD_COLLISION_SKIN);
        Vector3 LocalOnBox = GameMath_ToVector3Relative(Face, TestWorld_At(BoxCenter));
        Vector3 LocalOnSphere = Vector3Scale(GameMath_ToVector3(Normal), -0.25f);
        size_t PointCount = 0U;
        double WorstPoint = 0.0;
        float WorstLocal = 0.0f;
        for (size_t Contact = 0U; Contact < ContactCount; Contact++)
        {
            for (uint32_t Point = 0U; (Contacts[Contact].Manifold.ShapeB.ObjectId == Sphere)
                && (Point < Contacts[Contact].Manifold.PointCount); Point++)
            {
                const WorldContactPoint* At = &Contacts[Contact].Manifold.Points[Point];
                WorstPoint = fmax(WorstPoint, fmax(Vector3Double_Distance(At->PositionA, Face),
                    Vector3Double_Distance(At->PositionB, OnSphere)));
                WorstLocal = fmaxf(WorstLocal, fmaxf(Vector3Distance(At->LocalPositionA, LocalOnBox),
                    Vector3Distance(At->LocalPositionB, LocalOnSphere)));
                PointCount++;
            }
        }
        TEST_CHECK(run, (PointCount == 1U) && (WorstPoint < 1e-6) && (WorstLocal < 1e-6f));
        WorldMotionResult Result;
        TEST_CHECK(run, WorldCollision_GetMotionResult(Scene.Collision, Sphere, &Result)
            && (Result.HasGround == (Index == 0U))
            && (!Result.HasGround || (Vector3Double_Distance(Result.GroundPoint, Face) < 1e-6)));
    }

    // A third, driven along the top at 0.5 m/s across while pushed down: its result's velocity is the slide's, to a
    // millimetre a second (along the shift in the far run, where a float's step a tick would be 3.75 m/s).
    uint64_t Slider = AddShapeObject(run, &Scene, (Vector3){ -0.5f, 0.3f, 0.4f }, &Ball, TestBodyKind_Pushable);
    float WorstVelocity = 0.0f;
    for (uint32_t Tick = 0U; Tick < 600U; Tick++)
    {
        MoveBy(run, &Scene, Slider, (Vector3){ 0.5f * TEST_DELTA, -0.01f, 0.0f });
        Step(&Scene);
        WorldMotionResult Result;
        if ((Tick >= 10U) && TEST_CHECK(run, WorldCollision_GetMotionResult(Scene.Collision, Slider, &Result)))
        {
            WorstVelocity = fmaxf(WorstVelocity, Vector3Distance(Result.Velocity, (Vector3){ 0.5f, 0.0f, 0.0f }));
        }
    }
    TEST_CHECK(run, WorstVelocity < 1e-3f);
    MotionScene_Deconstruct(run, &Scene);
}

static void ContactEventsBeginPersistAndEndOnce(TestRun* run)
{
    MotionScene Scene;
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    CollisionShape Block = MakeBox((Vector3){ 0.5f, 0.5f, 0.5f });
    uint64_t Floor = AddShapeObject(run, &Scene, (Vector3){ 0.0f, -0.5f, 0.0f }, &Block, TestBodyKind_Static);
    CollisionShape Crate = MakeBox((Vector3){ 0.1f, 0.1f, 0.1f });
    uint64_t Box = AddShapeObject(run, &Scene, (Vector3){ 0.0f, 0.5f, 0.0f }, &Crate, TestBodyKind_Pushable);
    Step(&Scene);
    TEST_CHECK(run, CountContactEvents(&Scene, WorldContactEventKind_Begin, Floor, Box) == 0U);
    // Dropped onto the floor at 2.4 m/s: one begin, with the impact.
    for (uint32_t Tick = 0U; Tick < 120U; Tick++)
    {
        MoveBy(run, &Scene, Box, (Vector3){ 0.0f, -0.005f, 0.0f });
        Step(&Scene);
    }
    TEST_CHECK(run, CountContactEvents(&Scene, WorldContactEventKind_Begin, Floor, Box) == 1U);
    TEST_CHECK(run, CountContactEvents(&Scene, WorldContactEventKind_Persist, Floor, Box) == 0U);
    size_t EventCount = 0U;
    const WorldContactEvent* Events = WorldCollision_GetContactEvents(Scene.Collision, &EventCount);
    size_t EventContactCount = 0U;
    const WorldContact* EventContacts = WorldCollision_GetContactEventContacts(Scene.Collision, &EventContactCount);
    TEST_CHECK(run, (EventCount == 1U) && (Events[0].ContactCount == 1U) && (EventContactCount == 1U));
    if ((EventCount == 1U) && (EventContactCount == 1U))
    {
        TEST_CHECK(run, IsNear(Events[0].ApproachSpeed, 0.005f / TEST_DELTA));
        TEST_CHECK(run, (Events[0].TimeOfImpact >= 0.0f) && (Events[0].TimeOfImpact <= 1.0f));
        const WorldContactManifold* Manifold = &EventContacts[0].Manifold;
        TEST_CHECK(run, (Manifold->ShapeA.ObjectId == Floor) && (Manifold->ShapeB.ObjectId == Box));
        TEST_CHECK(run, IsNear(Manifold->Normal.y, 1.0f) && (Manifold->PointCount == 4U)); // a box on a box: corners
        TEST_CHECK(run, EventContacts[0].IsTouching);
    }
    // It rests: the contact and its points age, step by step.
    size_t ContactCount = 0U;
    const WorldContact* Contacts = WorldCollision_GetContacts(Scene.Collision, &ContactCount);
    uint32_t AgeBefore = (ContactCount == 1U) ? Contacts[0].Points[0].AgeTicks : 0U;
    Step(&Scene);
    Step(&Scene);
    Contacts = WorldCollision_GetContacts(Scene.Collision, &ContactCount);
    TEST_CHECK(run, (ContactCount == 1U) && (Contacts[0].Points[0].AgeTicks == (AgeBefore + 2U)));
    // Persisting contacts reported while asked for.
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetReportsPersistingContacts(GetCollider(&Scene, Box), true));
    WorldCollision_ClearContactEvents(Scene.Collision);
    Step(&Scene);
    Step(&Scene);
    Step(&Scene);
    TEST_CHECK(run, CountContactEvents(&Scene, WorldContactEventKind_Persist, Floor, Box) == 3U);
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetReportsPersistingContacts(GetCollider(&Scene, Box), false));
    // Lifted away: one end.
    WorldCollision_ClearContactEvents(Scene.Collision);
    MoveBy(run, &Scene, Box, (Vector3){ 0.0f, 0.5f, 0.0f });
    Step(&Scene);
    Step(&Scene);
    TEST_CHECK(run, CountContactEvents(&Scene, WorldContactEventKind_End, Floor, Box) == 1U);
    TEST_CHECK(run, CountContactEvents(&Scene, WorldContactEventKind_Begin, Floor, Box) == 0U);
    // Back down, then the floor removed while touching: the contact ends.
    for (uint32_t Tick = 0U; Tick < 120U; Tick++)
    {
        MoveBy(run, &Scene, Box, (Vector3){ 0.0f, -0.01f, 0.0f });
        Step(&Scene);
    }
    WorldCollision_ClearContactEvents(Scene.Collision);
    TEST_CHECK_SUCCESS(run, World_RemoveObject(&Scene.World, Floor));
    Step(&Scene);
    TEST_CHECK(run, CountContactEvents(&Scene, WorldContactEventKind_End, Floor, Box) == 1U);
    MotionScene_Deconstruct(run, &Scene);
}

static void ResultsReportTheGroundAndAMovingPlatform(TestRun* run)
{
    MotionScene Scene;
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    CollisionShape Deck = MakeBox((Vector3){ 1.0f, 0.1f, 1.0f });
    uint64_t Platform = AddShapeObject(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f }, &Deck, TestBodyKind_Kinematic);
    CollisionShape Crate = MakeBox((Vector3){ 0.2f, 0.2f, 0.2f });
    uint64_t Box = AddShapeObject(run, &Scene, (Vector3){ 0.0f, 0.3f + WORLD_COLLISION_SKIN, 0.0f }, &Crate,
        TestBodyKind_Pushable);
    Step(&Scene);
    WorldMotionResult Result;
    TEST_CHECK(run, WorldCollision_GetMotionResult(Scene.Collision, Box, &Result));
    TEST_CHECK(run, Result.HasGround && (Result.GroundObjectId == Platform) && IsNear(Result.GroundNormal.y, 1.0f));
    TEST_CHECK(run, Result.ContactCount == 1U);
    // The platform rises at 1 m/s and lifts the crate with it.
    for (uint32_t Tick = 0U; Tick < 10U; Tick++)
    {
        MoveBy(run, &Scene, Platform, (Vector3){ 0.0f, 1.0f * TEST_DELTA, 0.0f });
        Step(&Scene);
    }
    TEST_CHECK(run, IsNear(GetPosition(&Scene, Box).y - GetPosition(&Scene, Platform).y, 0.3f + WORLD_COLLISION_SKIN));
    TEST_CHECK(run, WorldCollision_GetMotionResult(Scene.Collision, Box, &Result));
    TEST_CHECK(run, Result.IsAwake && IsNear(Result.Correction.y, TEST_DELTA));
    TEST_CHECK(run, IsNear(Result.Velocity.y, 1.0f) && IsNear(Result.GroundVelocity.y, 1.0f));
    MotionScene_Deconstruct(run, &Scene);
}

static void ContactPushesAreInMetres(TestRun* run)
{
    // Two 1 kg spheres placed 0.2 m into each other, and a 10 kg sphere placed 0.1 m into a wall: a contact's push is
    // how far it moved the two apart.
    struct { float Mass; bool HasWall; float Expected; } Cases[] =
    {
        { 1.0f, false, 0.2f + WORLD_COLLISION_SKIN },
        { 10.0f, true, 0.1f + WORLD_COLLISION_SKIN },
    };
    for (size_t Case = 0U; Case < (sizeof(Cases) / sizeof(Cases[0])); Case++)
    {
        MotionScene Scene;
        if (!MotionScene_Construct(run, &Scene))
        {
            MotionScene_Deconstruct(run, &Scene);
            return;
        }
        CollisionShape Ball = MakeSphere(0.5f);
        CollisionShape Slab = MakeBox((Vector3){ 0.1f, 2.0f, 2.0f });
        if (Cases[Case].HasWall)
        {
            AddShapeObject(run, &Scene, (Vector3){ 1.0f, 0.0f, 0.0f }, &Slab, TestBodyKind_Static);
            uint64_t Sphere = AddShapeObject(run, &Scene, (Vector3){ 0.5f, 0.0f, 0.0f }, &Ball, TestBodyKind_Pushable);
            TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOverridden(GetCollider(&Scene, Sphere),
                WorldColliderSetting_Mass, true));
            TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetMass(GetCollider(&Scene, Sphere), Cases[Case].Mass));
        }
        else
        {
            AddShapeObject(run, &Scene, (Vector3){ -0.4f, 0.0f, 0.0f }, &Ball, TestBodyKind_Pushable);
            AddShapeObject(run, &Scene, (Vector3){ 0.4f, 0.0f, 0.0f }, &Ball, TestBodyKind_Pushable);
        }
        Step(&Scene);
        size_t Count = 0U;
        const WorldContact* Contacts = WorldCollision_GetContacts(Scene.Collision, &Count);
        TEST_CHECK(run, (Count == 1U) && (Contacts[0].Manifold.PointCount == 1U));
        if ((Count == 1U) && (Contacts[0].Manifold.PointCount == 1U))
        {
            TEST_CHECK(run, fabsf(Contacts[0].TotalPush - Cases[Case].Expected) < 1e-4f);
            TEST_CHECK(run, fabsf(Contacts[0].Points[0].Push - Cases[Case].Expected) < 1e-4f);
            TEST_CHECK(run, fabsf(Contacts[0].Points[0].PushSpeed - (Cases[Case].Expected / TEST_DELTA)) < 0.05f);
        }
        MotionScene_Deconstruct(run, &Scene);
    }
}

static void APushableResultHoldsWhatItCarries(TestRun* run)
{
    MotionScene Scene;
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    // A pushable body standing on its carried feet on a floor: its result has the feet's contact and their ground.
    CollisionShape Block = MakeBox((Vector3){ 2.0f, 0.5f, 2.0f });
    uint64_t Floor = AddShapeObject(run, &Scene, (Vector3){ 0.0f, -0.5f, 0.0f }, &Block, TestBodyKind_Static);
    CollisionShape Torso = MakeBox((Vector3){ 0.2f, 0.2f, 0.2f });
    uint64_t Body = AddShapeObject(run, &Scene, (Vector3){ 0.0f, 0.6f, 0.0f }, &Torso, TestBodyKind_Pushable);
    CollisionShape Legs = MakeBox((Vector3){ 0.1f, 0.2f, 0.1f });
    uint64_t Feet = AddShapeObject(run, &Scene, (Vector3){ 0.0f, 0.2f, 0.0f }, &Legs, TestBodyKind_Kinematic);
    TEST_CHECK_SUCCESS(run, World_SetParent(&Scene.World, Feet, Body, true));
    Step(&Scene);
    MoveBy(run, &Scene, Body, (Vector3){ 0.001f, 0.0f, 0.0f });
    Step(&Scene);
    WorldMotionResult Result;
    TEST_CHECK(run, WorldCollision_GetMotionResult(Scene.Collision, Body, &Result) && (Result.ContactCount == 1U)
        && Result.HasGround && (Result.GroundObjectId == Floor) && IsNear(Result.GroundNormal.y, 1.0f));
    // Pinned between two walls that reach 1 cm into it: the body is crushed, once; the feet it carries are not.
    WorldCollision_ClearMotionEvents(Scene.Collision);
    CollisionShape Slab = MakeBox((Vector3){ 0.05f, 0.1f, 1.0f });
    AddShapeObject(run, &Scene, (Vector3){ 0.24f, 0.6f, 0.0f }, &Slab, TestBodyKind_Static);
    AddShapeObject(run, &Scene, (Vector3){ -0.24f, 0.6f, 0.0f }, &Slab, TestBodyKind_Static);
    Step(&Scene);
    TEST_CHECK(run, WorldCollision_GetMotionResult(Scene.Collision, Body, &Result) && Result.IsCrushed);
    TEST_CHECK(run, WorldCollision_GetMotionResult(Scene.Collision, Feet, &Result) && !Result.IsCrushed);
    TEST_CHECK(run, CountMotionEvents(&Scene, WorldMotionEventKind_CrushedBegin, Body) == 1U);
    TEST_CHECK(run, CountMotionEvents(&Scene, WorldMotionEventKind_CrushedBegin, Feet) == 0U);
    MotionScene_Deconstruct(run, &Scene);
}

static void APileAtRestSleeps(TestRun* run)
{
    MotionScene Scene;
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    CollisionShape Block = MakeBox((Vector3){ 2.0f, 0.5f, 2.0f });
    AddShapeObject(run, &Scene, (Vector3){ 0.0f, -0.5f, 0.0f }, &Block, TestBodyKind_Static);
    CollisionShape Crate = MakeBox((Vector3){ 0.1f, 0.1f, 0.1f });
    uint64_t Crates[9];
    for (uint32_t Index = 0U; Index < 9U; Index++)
    {
        Crates[Index] = AddShapeObject(run, &Scene, (Vector3){ -0.3f + (0.3f * (float)(Index % 3U)), 0.1f,
            -0.3f + (0.3f * (float)(Index / 3U)) }, &Crate, TestBodyKind_Pushable);
    }
    Step(&Scene);
    WorldCollisionStats First = WorldCollision_GetStats(Scene.Collision);
    TEST_CHECK(run, (First.AwakeCount == 9U) && (First.ContactCount == 9U));
    Step(&Scene);
    WorldCollisionStats Resting = WorldCollision_GetStats(Scene.Collision);
    TEST_CHECK(run, (Resting.AwakeCount == 0U) && (Resting.PairCount == 0U) && (Resting.MoverCount == 0U));
    TEST_CHECK(run, Resting.ContactCount == 9U); // their contacts are kept as they were
    // One crate moving wakes nothing beyond its reach, and the others' contacts with the floor go on untouched.
    WorldCollision_ClearContactEvents(Scene.Collision);
    MoveBy(run, &Scene, Crates[0], (Vector3){ 0.001f, 0.0f, 0.0f });
    Step(&Scene);
    WorldCollisionStats OneMoving = WorldCollision_GetStats(Scene.Collision);
    TEST_CHECK(run, (OneMoving.AwakeCount == 1U) && (OneMoving.ContactCount == 9U));
    size_t EventCount = 0U;
    WorldCollision_GetContactEvents(Scene.Collision, &EventCount);
    TEST_CHECK(run, EventCount == 0U);
    MotionScene_Deconstruct(run, &Scene);
}

/* Runs a scene of a wall pushing a grid and a fast sphere against a wall; writes every crate's and the sphere's final
 * position into @p outPositions (10 of them). */
static void RunDeterminismScene(TestRun* run, Vector3* outPositions)
{
    MotionScene Scene;
    if (!MotionScene_Construct(run, &Scene))
    {
        MotionScene_Deconstruct(run, &Scene);
        return;
    }
    uint64_t Pusher = AddPushingWalls(run, &Scene, WorldColliderBlockedMode_PushThrough);
    CollisionShape Crate = MakeBox((Vector3){ 0.15f, 0.15f, 0.15f });
    uint64_t Ids[10];
    for (uint32_t Index = 0U; Index < 9U; Index++)
    {
        Ids[Index] = AddShapeObject(run, &Scene, (Vector3){ 0.4f * (float)(Index % 3U), 0.0f,
            -0.37f + (0.37f * (float)(Index / 3U)) }, &Crate, TestBodyKind_Pushable);
    }
    CollisionShape Ball = MakeSphere(0.1f);
    Ids[9] = AddShapeObject(run, &Scene, (Vector3){ 0.0f, 3.0f, 0.0f }, &Ball, TestBodyKind_Pushable);
    CollisionShape Wall = MakeBox((Vector3){ 0.005f, 2.0f, 2.0f });
    AddShapeObject(run, &Scene, (Vector3){ 5.0f, 3.0f, 0.0f }, &Wall, TestBodyKind_Static);
    for (uint32_t Tick = 0U; Tick < 600U; Tick++)
    {
        MoveBy(run, &Scene, Pusher, (Vector3){ 3.0f * TEST_DELTA, 0.0f, 0.0f });
        MoveBy(run, &Scene, Ids[9], (Vector3){ 300.0f * TEST_DELTA, 0.0f, 0.2f * TEST_DELTA });
        Step(&Scene);
    }
    for (size_t Index = 0U; Index < 10U; Index++)
    {
        outPositions[Index] = GetPosition(&Scene, Ids[Index]);
    }
    MotionScene_Deconstruct(run, &Scene);
}

static void TheSameSceneTwiceGivesTheSamePoses(TestRun* run)
{
    Vector3 First[10];
    Vector3 Second[10];
    Memory_Zero(First, sizeof(First));
    Memory_Zero(Second, sizeof(Second));
    RunDeterminismScene(run, First);
    RunDeterminismScene(run, Second);
    TEST_CHECK(run, memcmp(First, Second, sizeof(First)) == 0);
}


// Public functions.
void WorldCollisionMotionTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "a_thin_wall_stops_fast_spheres_and_boxes", AThinWallStopsFastSpheresAndBoxes },
        { "a_fast_fan_blade_pushes_a_sphere_instead_of_passing_it", AFastFanBladePushesASphereInsteadOfPassingIt },
        { "two_fast_spheres_meet_in_the_middle", TwoFastSpheresMeetInTheMiddle },
        { "colliders_that_are_not_continuous_are_still_swept_against",
            CollidersThatAreNotContinuousAreStillSweptAgainst },
        { "a_fast_pushable_is_swept_against_a_slow_one_whichever_came_first",
            AFastPushableIsSweptAgainstASlowOneWhicheverCameFirst },
        { "a_wall_pushes_a_row_at_once_and_stops_when_it_is_pinned", AWallPushesARowAtOnceAndStopsWhenItIsPinned },
        { "a_wall_pushes_a_grid_at_once_and_pushing_through_crushes_the_pinned",
            AWallPushesAGridAtOnceAndPushingThroughCrushesThePinned },
        { "masses_share_a_push", MassesShareAPush },
        { "pushables_slide_along_walls", PushablesSlideAlongWalls },
        { "a_train_of_stopping_doors_stops_door_by_door", ATrainOfStoppingDoorsStopsDoorByDoor },
        { "a_mover_moving_away_from_a_squeezed_pushable_is_not_blocked",
            AMoverMovingAwayFromASqueezedPushableIsNotBlocked },
        { "a_heavy_pushable_rests_steadily_against_a_light_pinned_one",
            AHeavyPushableRestsSteadilyAgainstALightPinnedOne },
        { "a_scaled_collider_pushes_out_and_a_shrunk_one_lets_go", AScaledColliderPushesOutAndAShrunkOneLetsGo },
        { "a_door_stops_on_a_pinned_box_or_pushes_through", ADoorStopsOnAPinnedBoxOrPushesThrough },
        { "teleports_skip_the_sweep_and_are_pushed_out", TeleportsSkipTheSweepAndArePushedOut },
        { "spheres_resting_on_a_box_stay_a_skin_from_it", SpheresRestingOnABoxStayASkinFromIt },
        { "a_pushed_child_is_written_back_through_its_parent", APushedChildIsWrittenBackThroughItsParent },
        { "a_pushable_carries_its_colliders", APushableCarriesItsColliders },
        { "a_pushable_child_stays_when_its_parent_is_pushed", APushableChildStaysWhenItsParentIsPushed },
        { "a_reparented_mover_pushes_through_instead_of_jumping", AReparentedMoverPushesThroughInsteadOfJumping },
        { "contact_events_begin_persist_and_end_once", ContactEventsBeginPersistAndEndOnce },
        { "results_report_the_ground_and_a_moving_platform", ResultsReportTheGroundAndAMovingPlatform },
        { "contact_pushes_are_in_metres", ContactPushesAreInMetres },
        { "a_pushable_result_holds_what_it_carries", APushableResultHoldsWhatItCarries },
        { "a_pile_at_rest_sleeps", APileAtRestSleeps },
        { "the_same_scene_twice_gives_the_same_poses", TheSameSceneTwiceGivesTheSamePoses },
    };
    TestWorld_RunSuite(run, "WorldCollisionMotion", "WorldCollisionMotionFar", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
