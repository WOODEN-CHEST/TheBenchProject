#include "TestSuites.h"
#include "TestAssets.h"
#include "TestTerrain.h"
#include "TestText.h"
#include "TestWorld.h"
#include "CollisionLayer.h"
#include "PathGround.h"
#include "World.h"
#include "WorldColliderComponent.h"
#include "WorldCollision.h"
#include "WorldFile.h"
#include "WorldObject.h"
#include "WorldPathComponent.h"
#include "WorldPathDebug.h"
#include "WorldQuery.h"
#include "WorldTerrainComponent.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"
#include <math.h>
#include <string.h>


// Types.
/* A world with the fixture's assets and its collision world, for what stands on the ground. */
typedef struct PathSceneStruct
{
    TestAssetFixture Fixture;
    World World;
    KeyRegistry Keys;
    WorldCollision* Collision;
    bool IsWorldBuilt;
} PathScene;


// Static functions: helpers.
static PathNode MakeNode(double x, double y, double z, PathNodeMode mode)
{
    PathNode Node = PathNode_CreateDefault();
    Node.Position = Vector3Double_Create(x, y, z);
    Node.Mode = mode;
    return Node;
}

static WorldPathComponent* AddPath(TestRun* run, World* world, WorldObject* object)
{
    WorldComponent* Component = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_AddComponent(world, WorldObject_GetId(object), WorldComponentType_Path, &Component)))
    {
        return NULL;
    }
    return WorldPathComponent_FromComponent(Component);
}

static WorldObject* MakeObject(TestRun* run, World* world, const char* name, Vector3 position)
{
    WorldObject* Object = NULL;
    if (TEST_CHECK_SUCCESS(run, World_CreateObject(world, (const unsigned char*)name, &Object)))
    {
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, TestWorld_At(position)));
    }
    return Object;
}

/* An object with a closed square region of the given half size around its own origin, 0 to 20 m. */
static WorldPathComponent* MakeSquareRegion(TestRun* run, World* world, const char* name, Vector3 position, double half)
{
    WorldObject* Object = MakeObject(run, world, name, position);
    WorldPathComponent* Path = (Object != NULL) ? AddPath(run, world, Object) : NULL;
    if (Path != NULL)
    {
        PathNode Nodes[4] = { MakeNode(-half, 0.0, -half, PathNodeMode_Corner), MakeNode(half, 0.0, -half, PathNodeMode_Corner),
            MakeNode(half, 0.0, half, PathNodeMode_Corner), MakeNode(-half, 0.0, half, PathNodeMode_Corner) };
        TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(Path, Nodes, 4U));
        TEST_CHECK_SUCCESS(run, WorldPathComponent_SetClosed(Path, true));
    }
    return Path;
}

static bool Scene_Construct(TestRun* run, PathScene* self, Logger* logger)
{
    Memory_Zero(self, sizeof(*self));
    if (!TestAssetFixture_Construct(run, &self->Fixture))
    {
        return false;
    }
    self->IsWorldBuilt = TestWorld_Construct(run, &self->World, &self->Keys, self->Fixture.Manager, logger);
    return self->IsWorldBuilt
        && TEST_CHECK_SUCCESS(run, WorldCollision_Create(&self->World, self->Fixture.Manager, logger, NULL,
            &self->Collision));
}

static void Scene_Deconstruct(TestRun* run, PathScene* self)
{
    WorldCollision_Destroy(self->Collision);
    if (self->IsWorldBuilt)
    {
        TestWorld_Deconstruct(run, &self->World, &self->Keys);
    }
    TestAssetFixture_Deconstruct(run, &self->Fixture);
}

static float Flat(float x, float z)
{
    (void)x;
    (void)z;
    return 0.0f;
}

static float Rising(float x, float z)
{
    (void)z;
    return 0.25f * x;
}

/* A static object at @p position of the scene placing a terrain from a height function, 40 m a side from its origin. */
static uint64_t AddTerrain(TestRun* run, PathScene* scene, const char* name, TestTerrainHeightFunction height, Vector3 position)
{
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 40U, 40U, 1.0f, 0.0f, 0.0f, height);
    TerrainMesh* Ground = TestTerrain_CreateMesh(run, &Grid, 1U, 0U, 8.0f);
    TestTerrain_FreeGrid(&Grid);
    if ((Ground == NULL) || !TestAssets_AddTerrain(run, &scene->Fixture, name, Ground, NULL, NULL))
    {
        TerrainMesh_Destroy(Ground);
        return 0U;
    }
    TerrainMesh_Destroy(Ground);
    WorldObject* Object = MakeObject(run, &scene->World, "ground", position);
    WorldComponent* Component = NULL;
    if ((Object == NULL)
        || !TEST_CHECK_SUCCESS(run, World_AddComponent(&scene->World, WorldObject_GetId(Object), WorldComponentType_Terrain,
            &Component)))
    {
        return 0U;
    }
    TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetTerrain(WorldTerrainComponent_FromComponent(Component),
        (const unsigned char*)name));
    return WorldObject_GetId(Object);
}

/* A static box collider of half extents 0.5 m scaled by @p scale at @p position, on @p layer. */
static uint64_t AddBox(TestRun* run, PathScene* scene, Vector3 position, Vector3 scale, CollisionLayer layer)
{
    WorldObject* Object = MakeObject(run, &scene->World, "box", position);
    WorldComponent* Component = NULL;
    if ((Object == NULL) || !TEST_CHECK_SUCCESS(run, World_AddComponent(&scene->World, WorldObject_GetId(Object),
        WorldComponentType_Collider, &Component)))
    {
        return 0U;
    }
    TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Object, scale));
    WorldColliderComponent* Collider = WorldColliderComponent_FromComponent(Component);
    CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Box);
    Shape.HalfExtents = (Vector3){ 0.5f, 0.5f, 0.5f };
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOwnShape(Collider, &Shape));
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetUsesOwnShape(Collider, true));
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOverridden(Collider, WorldColliderSetting_Layer, true));
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetLayer(Collider, layer));
    return WorldObject_GetId(Object);
}

static bool Project(PathScene* scene, Vector3 point, const PathGroundSettings* settings, PathGroundHit* outHit)
{
    return PathGround_Project(scene->Collision, TestWorld_At(point), settings, outHit);
}


// Static functions: test cases.
static void TheCurveFollowsEveryChange(TestRun* run)
{
    World TestWorldValue;
    KeyRegistry Keys;
    if (!TestWorld_Construct(run, &TestWorldValue, &Keys, NULL, NULL))
    {
        return;
    }
    WorldObject* Object = MakeObject(run, &TestWorldValue, "path", (Vector3){ 0.0f, 0.0f, 0.0f });
    WorldPathComponent* Path = AddPath(run, &TestWorldValue, Object);
    if (Path == NULL)
    {
        TestWorld_Deconstruct(run, &TestWorldValue, &Keys);
        return;
    }
    // A node's table and its plain default agree on every row.
    PathNode FromTable;
    TEST_CHECK_SUCCESS(run, Properties_Construct(WorldPathComponent_GetNodePropertyTable(), &FromTable));
    PathNode Plain = PathNode_CreateDefault();
    TEST_CHECK(run, Properties_Equals(WorldPathComponent_GetNodePropertyTable(), &FromTable, &Plain));

    size_t Count = 7U;
    TEST_CHECK(run, (WorldPathComponent_GetNodes(Path, &Count) == NULL) && (Count == 0U) && !WorldPathComponent_IsClosed(Path)
        && (WorldPathComponent_GetUpMode(Path) == PathUpMode_Upright)
        && (WorldPathComponent_GetSampleSpacing(Path) == WORLD_PATH_DEFAULT_SAMPLE_SPACING)
        && (PathCurve_GetNodeCount(WorldPathComponent_GetCurve(Path)) == 0U));
    const PathCurve* Curve = WorldPathComponent_GetCurve(Path);

    // Nodes through the typed setters: copied, and the curve rebuilt each time.
    uint32_t Revision = WorldComponent_GetRevision(&Path->Base);
    PathNode Nodes[3] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner),
        MakeNode(10.0, 0.0, 10.0, PathNodeMode_Corner) };
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(Path, Nodes, 3U));
    TEST_CHECK(run, (WorldComponent_GetRevision(&Path->Base) != Revision) && (PathCurve_GetLength(Curve) == 20.0)
        && (WorldPathComponent_GetNodes(Path, &Count) != Nodes) && (Count == 3U));
    Nodes[2].Position.Z = 500.0; // a copy, so this changes nothing
    TEST_CHECK(run, PathCurve_GetLength(Curve) == 20.0);

    PathNode One = MakeNode(10.0, 0.0, 30.0, PathNodeMode_Corner);
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNode(Path, 2U, &One));
    TEST_CHECK(run, PathCurve_GetLength(Curve) == 40.0);
    PathNode Between = MakeNode(10.0, 3.0, 0.0, PathNodeMode_Corner);
    TEST_CHECK_SUCCESS(run, WorldPathComponent_InsertNode(Path, 2U, &Between));
    const PathNode* Read = WorldPathComponent_GetNodes(Path, &Count);
    TEST_CHECK(run, (Count == 4U) && (Read[2].Position.Y == 3.0) && (Read[3].Position.Z == 30.0)
        && (PathCurve_GetNodeCount(Curve) == 4U));
    PathNode Last = MakeNode(0.0, 0.0, 30.0, PathNodeMode_Corner);
    TEST_CHECK_SUCCESS(run, WorldPathComponent_InsertNode(Path, 4U, &Last));
    Read = WorldPathComponent_GetNodes(Path, &Count);
    TEST_CHECK(run, (Count == 5U) && (Read[4].Position.X == 0.0));
    TEST_CHECK_SUCCESS(run, WorldPathComponent_RemoveNode(Path, 2U));
    Read = WorldPathComponent_GetNodes(Path, &Count);
    TEST_CHECK(run, (Count == 4U) && (Read[2].Position.Z == 30.0) && (Read[3].Position.X == 0.0)
        && (PathCurve_GetLength(Curve) == 50.0));
    TEST_CHECK_SUCCESS(run, WorldPathComponent_RemoveNode(Path, 3U));
    TEST_CHECK(run, PathCurve_GetLength(Curve) == 40.0);

    // The other rows: closing and the up mode reshape it, the spacing does not touch it but still advances the revision.
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetClosed(Path, true));
    TEST_CHECK(run, PathCurve_IsClosed(Curve) && WorldPathComponent_IsClosed(Path)
        && (PathCurve_GetSegmentCount(Curve) == 3U));
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetUpMode(Path, PathUpMode_RotationMinimizing));
    TEST_CHECK(run, (Curve->_tableRights != NULL) && (WorldPathComponent_GetUpMode(Path) == PathUpMode_RotationMinimizing));
    Revision = WorldComponent_GetRevision(&Path->Base);
    const void* Block = Curve->_block;
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetSampleSpacing(Path, 2.5f));
    TEST_CHECK(run, (WorldComponent_GetRevision(&Path->Base) != Revision) && (Curve->_block == Block)
        && (WorldPathComponent_GetSampleSpacing(Path) == 2.5f));

    // Through the table alike, row by row: whichever way a row changes, the curve follows.
    const PropertyTable* Table = WorldPathComponent_GetPropertyTable();
    PropertyValue Value = PropertyValue_FromBool(false);
    TEST_CHECK_SUCCESS(run, Properties_Set(Table, Path, u8"is_closed", &Value));
    TEST_CHECK(run, !PathCurve_IsClosed(Curve) && (PathCurve_GetSegmentCount(Curve) == 2U));
    Value = PropertyValue_FromEnum((int32_t)PathUpMode_Upright);
    TEST_CHECK_SUCCESS(run, Properties_Set(Table, Path, u8"up_mode", &Value));
    TEST_CHECK(run, Curve->_tableRights == NULL);
    PropertyRecordList* List = NULL;
    PathNode Short[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(3.0, 4.0, 0.0, PathNodeMode_Corner) };
    if (TEST_CHECK_SUCCESS(run, PropertyRecordList_Create(WorldPathComponent_GetNodePropertyTable(), Short, 2U, &List)))
    {
        Value = PropertyValue_FromRecordList(List);
        TEST_CHECK_SUCCESS(run, Properties_Set(Table, Path, u8"nodes", &Value));
        TEST_CHECK(run, PathCurve_GetLength(Curve) == 5.0);
        PropertyRecordList_Destroy(List);
    }

    // Refused by the rows' rules, naming what broke them; nothing changes.
    Revision = WorldComponent_GetRevision(&Path->Base);
    for (int Breaker = 0; Breaker < 8; Breaker++)
    {
        PathNode Broken[2] = { Short[0], Short[1] };
        const char* Fragment = "record 1: path_node";
        switch (Breaker)
        {
            case 0: Broken[1].Position.X = (double)NAN; break;
            case 1: Broken[1].Position.Y = 2.0e7; break;
            case 2: Broken[1].InHandle.x = INFINITY; break;
            case 3: Broken[1].OutHandle.z = -2.0e7f; break;
            case 4: Broken[1].Roll = NAN; break;
            case 5: Broken[1].Width = -1.0f; break;
            case 6: Broken[1].Scale = 2.0e6f; break;
            default: Broken[1].Mode = (PathNodeMode)9; break;
        }
        const char* const Fragments[] = { "path_component.nodes", Fragment, NULL };
        TestText_CheckErrorMessage(run, WorldPathComponent_SetNodes(Path, Broken, 2U), ErrorCode_ArgumentOutOfRange, Fragments);
        TestText_CheckErrorMessage(run, WorldPathComponent_SetNode(Path, 1U, &Broken[1]), ErrorCode_ArgumentOutOfRange,
            Fragments);
    }
    TEST_CHECK(run, (PathCurve_GetLength(Curve) == 5.0) && (WorldComponent_GetRevision(&Path->Base) == Revision));
    PathNode Any = Short[0];
    TEST_CHECK_ERROR(run, WorldPathComponent_SetNode(Path, 2U, &Any), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, WorldPathComponent_InsertNode(Path, 3U, &Any), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, WorldPathComponent_RemoveNode(Path, 2U), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, WorldPathComponent_SetNodes(NULL, Short, 2U), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, WorldPathComponent_SetNodes(Path, NULL, 2U), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, WorldPathComponent_SetNode(NULL, 0U, &Any), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, WorldPathComponent_SetNode(Path, 0U, NULL), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, WorldPathComponent_InsertNode(NULL, 0U, &Any), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, WorldPathComponent_InsertNode(Path, 0U, NULL), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, WorldPathComponent_RemoveNode(NULL, 0U), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, WorldPathComponent_SetClosed(NULL, true), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, WorldPathComponent_SetUpMode(NULL, PathUpMode_Upright), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, WorldPathComponent_SetUpMode(Path, (PathUpMode)5), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, WorldPathComponent_SetSampleSpacing(NULL, 1.0f), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, WorldPathComponent_SetSampleSpacing(Path, 0.0f), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, WorldPathComponent_SetSampleSpacing(Path, NAN), ErrorCode_ArgumentOutOfRange);

    // Back to no nodes at all.
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(Path, NULL, 0U));
    TEST_CHECK(run, (WorldPathComponent_GetNodes(Path, &Count) == NULL) && (PathCurve_GetNodeCount(Curve) == 0U));
    PathNode First = Short[1];
    TEST_CHECK_SUCCESS(run, WorldPathComponent_InsertNode(Path, 0U, &First));
    TEST_CHECK(run, PathCurve_GetNodeCount(Curve) == 1U);
    TEST_CHECK_SUCCESS(run, WorldPathComponent_RemoveNode(Path, 0U));
    TEST_CHECK(run, (WorldPathComponent_GetNodes(Path, &Count) == NULL) && (Count == 0U));

    // The list's limit, with an insertion past it.
    PathNode* Many = Memory_Allocate((WORLD_PATH_MAX_NODES + 1U) * sizeof(PathNode));
    for (size_t Index = 0U; Index <= WORLD_PATH_MAX_NODES; Index++)
    {
        Many[Index] = MakeNode((double)Index, 0.0, 0.0, PathNodeMode_Corner);
    }
    const char* const LimitFragments[] = { "path_component.nodes", "at most 16384 records", NULL };
    TestText_CheckErrorMessage(run, WorldPathComponent_SetNodes(Path, Many, WORLD_PATH_MAX_NODES + 1U),
        ErrorCode_ArgumentOutOfRange, LimitFragments);
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(Path, Many, WORLD_PATH_MAX_NODES));
    TEST_CHECK(run, (PathCurve_GetNodeCount(Curve) == WORLD_PATH_MAX_NODES)
        && (PathCurve_GetLength(Curve) == (double)(WORLD_PATH_MAX_NODES - 1U)));
    TestText_CheckErrorMessage(run, WorldPathComponent_InsertNode(Path, 0U, &Many[0]), ErrorCode_ArgumentOutOfRange,
        LimitFragments);
    TEST_CHECK(run, PathCurve_GetNodeCount(Curve) == WORLD_PATH_MAX_NODES);
    Memory_Free(Many);
    TestWorld_Deconstruct(run, &TestWorldValue, &Keys);
}

static void AReaderFindsTheNearestPathAboveIt(TestRun* run)
{
    World TestWorldValue;
    KeyRegistry Keys;
    if (!TestWorld_Construct(run, &TestWorldValue, &Keys, NULL, NULL))
    {
        return;
    }
    Vector3 Zero = { 0.0f, 0.0f, 0.0f };
    WorldObject* Road = MakeObject(run, &TestWorldValue, "road", Zero);
    WorldObject* Kerbs = MakeObject(run, &TestWorldValue, "kerbs", Zero);
    WorldObject* Kerb = MakeObject(run, &TestWorldValue, "kerb", Zero);
    WorldObject* Fence = MakeObject(run, &TestWorldValue, "fence", Zero);
    WorldObject* Alone = MakeObject(run, &TestWorldValue, "alone", Zero);
    TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorldValue, WorldObject_GetId(Kerbs), WorldObject_GetId(Road), false));
    TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorldValue, WorldObject_GetId(Kerb), WorldObject_GetId(Kerbs), false));
    TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorldValue, WorldObject_GetId(Fence), WorldObject_GetId(Kerbs), false));
    WorldPathComponent* RoadPath = AddPath(run, &TestWorldValue, Road);
    WorldPathComponent* FencePath = AddPath(run, &TestWorldValue, Fence);
    TEST_CHECK(run, WorldPathComponent_FindForObject(Road) == RoadPath); // its own
    TEST_CHECK(run, WorldPathComponent_FindForObject(Kerbs) == RoadPath); // its parent's
    TEST_CHECK(run, WorldPathComponent_FindForObject(Kerb) == RoadPath); // two levels up
    TEST_CHECK(run, WorldPathComponent_FindForObject(Fence) == FencePath); // its own, nearer than the road's
    TEST_CHECK(run, WorldPathComponent_FindForObject(Alone) == NULL);
    TEST_CHECK(run, WorldPathComponent_FindForObject(NULL) == NULL);
    TestWorld_Deconstruct(run, &TestWorldValue, &Keys);
}

static void AWatchSeesWhatChanged(TestRun* run)
{
    World TestWorldValue;
    KeyRegistry Keys;
    if (!TestWorld_Construct(run, &TestWorldValue, &Keys, NULL, NULL))
    {
        return;
    }
    Vector3 Zero = { 0.0f, 0.0f, 0.0f };
    WorldObject* Parent = MakeObject(run, &TestWorldValue, "parent", Zero);
    WorldObject* Object = MakeObject(run, &TestWorldValue, "path", Zero);
    TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorldValue, WorldObject_GetId(Object), WorldObject_GetId(Parent), false));
    WorldPathWatch Watch = WorldPathWatch_Create();
    WorldPathChange Change = WorldPathWatch_Update(&Watch, NULL);
    TEST_CHECK(run, !Change.HasPathChanged && !Change.HasPlaceChanged); // nothing yet, nothing to tell
    WorldPathComponent* Path = AddPath(run, &TestWorldValue, Object);
    Change = WorldPathWatch_Update(&Watch, Path);
    TEST_CHECK(run, Change.HasPathChanged && Change.HasPlaceChanged); // found
    Change = WorldPathWatch_Update(&Watch, Path);
    TEST_CHECK(run, !Change.HasPathChanged && !Change.HasPlaceChanged); // at rest

    // Every change of a row counts, whichever way it came; each is seen once.
    PathNode Node = MakeNode(1.0, 2.0, 3.0, PathNodeMode_Smooth);
    TEST_CHECK_SUCCESS(run, WorldPathComponent_InsertNode(Path, 0U, &Node));
    Change = WorldPathWatch_Update(&Watch, Path);
    TEST_CHECK(run, Change.HasPathChanged && !Change.HasPlaceChanged);
    TEST_CHECK(run, !WorldPathWatch_Update(&Watch, Path).HasPathChanged);
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetSampleSpacing(Path, 3.0f));
    TEST_CHECK(run, WorldPathWatch_Update(&Watch, Path).HasPathChanged);
    PropertyValue Value = PropertyValue_FromBool(true);
    TEST_CHECK_SUCCESS(run, Properties_Set(WorldPathComponent_GetPropertyTable(), Path, u8"is_closed", &Value));
    TEST_CHECK(run, WorldPathWatch_Update(&Watch, Path).HasPathChanged);

    // Moving it, or a parent, is a change of place alone.
    TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, TestWorld_At((Vector3){ 1.0f, 0.0f, 0.0f })));
    Change = WorldPathWatch_Update(&Watch, Path);
    TEST_CHECK(run, !Change.HasPathChanged && Change.HasPlaceChanged);
    TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Parent, TestWorld_At((Vector3){ 2.0f, 0.0f, 0.0f })));
    Change = WorldPathWatch_Update(&Watch, Path);
    TEST_CHECK(run, !Change.HasPathChanged && Change.HasPlaceChanged);
    TEST_CHECK(run, !WorldPathWatch_Update(&Watch, Path).HasPlaceChanged);

    // A new component in its place is another path, even if its revision happens to match.
    TEST_CHECK_SUCCESS(run, World_RemoveComponent(&TestWorldValue, WorldObject_GetId(Object), WorldComponentType_Path));
    Change = WorldPathWatch_Update(&Watch, NULL);
    TEST_CHECK(run, Change.HasPathChanged && Change.HasPlaceChanged); // lost
    WorldPathWatch Other = WorldPathWatch_Create();
    WorldPathComponent* Second = AddPath(run, &TestWorldValue, Parent);
    WorldPathWatch_Update(&Other, Second);
    WorldPathComponent* Third = AddPath(run, &TestWorldValue, Object);
    Change = WorldPathWatch_Update(&Other, Third);
    TEST_CHECK(run, Change.HasPathChanged && Change.HasPlaceChanged); // replaced
    TestWorld_Deconstruct(run, &TestWorldValue, &Keys);
}

/* In the world: the object's placement in double, its frames turned, regions at any distance from the origin. */
static void PointsReachTheWorldInDouble(TestRun* run)
{
    World TestWorldValue;
    KeyRegistry Keys;
    if (!TestWorld_Construct(run, &TestWorldValue, &Keys, NULL, NULL))
    {
        return;
    }
    Vector3 Place = { 10.0f, 2.0f, -4.0f };
    WorldObject* Object = MakeObject(run, &TestWorldValue, "path", Place);
    WorldPathComponent* Path = AddPath(run, &TestWorldValue, Object);
    if (Path == NULL)
    {
        TestWorld_Deconstruct(run, &TestWorldValue, &Keys);
        return;
    }
    PathNode Nodes[4] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(20.0, 0.0, 0.0, PathNodeMode_Corner),
        MakeNode(20.0, 0.0, 20.0, PathNodeMode_Corner), MakeNode(0.0, 0.0, 20.0, PathNodeMode_Corner) };
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(Path, Nodes, 4U));
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetClosed(Path, true));

    // A millimetre off the origin keeps every digit, however far out: nothing went through a float.
    Vector3Double Offset = TestWorld_GetOffset();
    Vector3Double Local = Vector3Double_Create(0.001, 0.002, 0.003);
    Vector3Double InTheWorld = WorldPathComponent_LocalToWorld(Path, Local);
    Vector3Double Expected = Vector3Double_Create(Offset.X + 10.001, Offset.Y + 2.002, Offset.Z - 3.997);
    TEST_CHECK(run, Vector3Double_EqualsClose(InTheWorld, Expected, 1e-9));
    Vector3Double Back;
    TEST_CHECK(run, WorldPathComponent_WorldToLocal(Path, InTheWorld, &Back) && Vector3Double_EqualsClose(Back, Local, 1e-9));

    // Turned a quarter about Y and doubled: +X runs to -Z, the point lies twice as far off, and the region turns with it.
    TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(Object, (Vector3){ 0.0f, PI / 2.0f, 0.0f }));
    TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Object, (Vector3){ 2.0f, 2.0f, 2.0f }));
    PathCurvePoint Point;
    PathCurvePoint InWorld;
    TEST_CHECK(run, PathCurve_GetPointAt(WorldPathComponent_GetCurve(Path), 5.0, &Point));
    WorldPathComponent_PointToWorld(Path, &Point, &InWorld);
    Vector3Double Scene = Vector3Double_Subtract(InWorld.Position, Offset);
    TEST_CHECK(run, Vector3Double_EqualsClose(Scene, Vector3Double_Create(10.0, 2.0, -14.0), 1e-5));
    TEST_CHECK(run, (fabsf(InWorld.Tangent.z + 1.0f) < 1e-5f) && (fabsf(InWorld.Up.y - 1.0f) < 1e-5f)
        && (fabsf(InWorld.Right.x - 1.0f) < 1e-5f) && (InWorld.Distance == Point.Distance));
    TEST_CHECK(run, WorldPathComponent_IsWorldPointInside(Path, TestWorld_At((Vector3){ 30.0f, 500.0f, -24.0f })));
    TEST_CHECK(run, !WorldPathComponent_IsWorldPointInside(Path, TestWorld_At((Vector3){ -5.0f, 2.0f, -24.0f })));
    TEST_CHECK(run, !WorldPathComponent_IsWorldPointInside(Path, TestWorld_At((Vector3){ 30.0f, 2.0f, 6.0f })));

    // A sliver inside the outline by a millimetre tells apart, however far out it lies: the sums run in double.
    TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(Object, (Vector3){ 0.0f, 0.0f, 0.0f }));
    TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Object, (Vector3){ 1.0f, 1.0f, 1.0f }));
    TEST_CHECK(run, WorldPathComponent_IsWorldPointInside(Path, Vector3Double_Create(Offset.X + 10.001, 0.0, Offset.Z + 5.0)));
    TEST_CHECK(run, !WorldPathComponent_IsWorldPointInside(Path, Vector3Double_Create(Offset.X + 9.999, 0.0, Offset.Z + 5.0)));

    // Under an uneven scale the frame stays unit and square.
    TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Object, (Vector3){ 3.0f, 0.5f, 1.0f }));
    PathNode Diagonal[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 7.0, 10.0, PathNodeMode_Corner) };
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(Path, Diagonal, 2U));
    TEST_CHECK(run, PathCurve_GetPointAt(WorldPathComponent_GetCurve(Path), 3.0, &Point));
    WorldPathComponent_PointToWorld(Path, &Point, &InWorld);
    Vector3Double T = Vector3Double_Create((double)InWorld.Tangent.x, (double)InWorld.Tangent.y, (double)InWorld.Tangent.z);
    Vector3Double U = Vector3Double_Create((double)InWorld.Up.x, (double)InWorld.Up.y, (double)InWorld.Up.z);
    Vector3Double R = Vector3Double_Create((double)InWorld.Right.x, (double)InWorld.Right.y, (double)InWorld.Right.z);
    TEST_CHECK(run, (fabs(Vector3Double_Length(T) - 1.0) < 1e-5) && (fabs(Vector3Double_Length(U) - 1.0) < 1e-5)
        && (fabs(Vector3Double_Dot(T, U)) < 1e-5) && Vector3Double_EqualsClose(Vector3Double_Cross(T, U), R, 1e-5));
    // Given itself as the output, the same.
    PathCurvePoint InPlace = Point;
    WorldPathComponent_PointToWorld(Path, &InPlace, &InPlace);
    TEST_CHECK(run, Vector3Double_EqualsClose(InPlace.Position, InWorld.Position, 0.0));

    // Scaled flat, nothing comes back, and nothing is inside the square that holds it when it is not flat.
    TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Object, (Vector3){ 1.0f, 1.0f, 1.0f }));
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(Path, Nodes, 4U));
    TEST_CHECK(run, WorldPathComponent_IsWorldPointInside(Path, TestWorld_At((Vector3){ 15.0f, 0.0f, 0.0f })));
    TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Object, (Vector3){ 1.0f, 1.0f, 0.0f }));
    TEST_CHECK(run, !WorldPathComponent_WorldToLocal(Path, InTheWorld, &Back));
    TEST_CHECK(run, !WorldPathComponent_IsWorldPointInside(Path, TestWorld_At((Vector3){ 15.0f, 0.0f, 0.0f })));

    // And a curve's frame stays unit and square under it, whichever axis is gone and however the path runs.
    Vector3 Flats[3] = { { 0.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 0.0f } };
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetClosed(Path, false));
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(Path, Diagonal, 2U));
    TEST_CHECK(run, PathCurve_GetPointAt(WorldPathComponent_GetCurve(Path), 3.0, &Point));
    for (size_t Flat = 0U; Flat < 3U; Flat++)
    {
        TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Object, Flats[Flat]));
        WorldPathComponent_PointToWorld(Path, &Point, &InWorld);
        T = Vector3Double_Create((double)InWorld.Tangent.x, (double)InWorld.Tangent.y, (double)InWorld.Tangent.z);
        U = Vector3Double_Create((double)InWorld.Up.x, (double)InWorld.Up.y, (double)InWorld.Up.z);
        R = Vector3Double_Create((double)InWorld.Right.x, (double)InWorld.Right.y, (double)InWorld.Right.z);
        TEST_CHECK(run, (fabs(Vector3Double_Length(T) - 1.0) < 1e-5) && (fabs(Vector3Double_Length(U) - 1.0) < 1e-5)
            && (fabs(Vector3Double_Length(R) - 1.0) < 1e-5) && (fabs(Vector3Double_Dot(T, U)) < 1e-5)
            && Vector3Double_EqualsClose(Vector3Double_Cross(T, U), R, 1e-5));
        // Flat in Y the up has nothing left in the world, and what it was before the flattening says which way is up.
        TEST_CHECK(run, (Flat != 1U) || (U.Y > 0.99));
    }
    TestWorld_Deconstruct(run, &TestWorldValue, &Keys);
}

static void QueriesFindWhatIsInsideARegion(TestRun* run)
{
    World TestWorldValue;
    KeyRegistry Keys;
    if (!TestWorld_Construct(run, &TestWorldValue, &Keys, NULL, NULL))
    {
        return;
    }
    Vector3 ParkPlace = { 0.0f, 0.0f, 0.0f };
    Vector3 PlotPlace = { 100.0f, 0.0f, 0.0f };
    WorldPathComponent* Park = MakeSquareRegion(run, &TestWorldValue, "park", ParkPlace, 20.0);
    WorldPathComponent* Plot = MakeSquareRegion(run, &TestWorldValue, "plot", PlotPlace, 10.0);
    uint32_t ParkTag = 0U;
    uint32_t TownTag = 0U;
    TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(&Keys, (const unsigned char*)u8"town:park", &ParkTag));
    TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(&Keys, (const unsigned char*)u8"town:parts", &TownTag));
    uint64_t ParkId = WorldObject_GetId(Park->Base._owner);
    uint64_t PlotId = WorldObject_GetId(Plot->Base._owner);
    TEST_CHECK_SUCCESS(run, World_AddTag(&TestWorldValue, ParkId, ParkTag));
    TEST_CHECK_SUCCESS(run, World_AddTag(&TestWorldValue, ParkId, TownTag));
    TEST_CHECK_SUCCESS(run, World_AddTag(&TestWorldValue, PlotId, TownTag));

    WorldObject* InPark = MakeObject(run, &TestWorldValue, "bench", (Vector3){ 5.0f, 30.0f, 5.0f });
    WorldObject* InPlot = MakeObject(run, &TestWorldValue, "shed", (Vector3){ 105.0f, 0.0f, 0.0f });
    WorldObject* Outside = MakeObject(run, &TestWorldValue, "bus", (Vector3){ 50.0f, 0.0f, 0.0f });
    // A child's place is its world place: its parent stands outside, it stands in the park.
    WorldObject* Child = MakeObject(run, &TestWorldValue, "sign", (Vector3){ 0.0f, 0.0f, 0.0f });
    TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorldValue, WorldObject_GetId(Child), WorldObject_GetId(Outside), false));
    TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Child, Vector3Double_Create(-45.0, 0.0, 0.0)));
    uint64_t BenchId = WorldObject_GetId(InPark);
    uint64_t ShedId = WorldObject_GetId(InPlot);
    uint64_t BusId = WorldObject_GetId(Outside);
    uint64_t SignId = WorldObject_GetId(Child);

    GenericBuffer Ids;
    GenericBuffer_AllocateVariable(&Ids, 0U, sizeof(uint64_t));
    WorldQuery Query = WorldQuery_Create();
    Query.InsideRegionObject = ParkId;
    TEST_CHECK_SUCCESS(run, World_Query(&TestWorldValue, &Query, &Ids));
    // The region's own object stands at its middle, inside it.
    const uint64_t* Found = (const uint64_t*)Ids._data;
    TEST_CHECK(run, (Ids._count == 3U) && (Found[0] == ParkId) && (Found[1] == BenchId) && (Found[2] == SignId));

    // "Is the player in the park": by its tag, one object at a time.
    bool IsMatch = false;
    Query = WorldQuery_Create();
    Query.InsideRegionTag = ParkTag;
    TEST_CHECK(run, TEST_CHECK_SUCCESS(run, World_DoesObjectMatch(&TestWorldValue, &Query, BenchId, &IsMatch)) && IsMatch);
    TEST_CHECK(run, TEST_CHECK_SUCCESS(run, World_DoesObjectMatch(&TestWorldValue, &Query, ShedId, &IsMatch)) && !IsMatch);
    TEST_CHECK(run, TEST_CHECK_SUCCESS(run, World_DoesObjectMatch(&TestWorldValue, &Query, BusId, &IsMatch)) && !IsMatch);

    // A tag on several regions: inside any of them. Both filters at once: inside both.
    Query = WorldQuery_Create();
    Query.InsideRegionTag = TownTag;
    GenericBuffer_Clear(&Ids);
    TEST_CHECK_SUCCESS(run, World_Query(&TestWorldValue, &Query, &Ids));
    TEST_CHECK(run, Ids._count == 5U); // both regions' objects, the bench, the shed and the sign
    Query.InsideRegionObject = PlotId;
    GenericBuffer_Clear(&Ids);
    TEST_CHECK_SUCCESS(run, World_Query(&TestWorldValue, &Query, &Ids));
    Found = (const uint64_t*)Ids._data;
    TEST_CHECK(run, (Ids._count == 2U) && (Found[0] == PlotId) && (Found[1] == ShedId));

    // Combined with another filter, in its order and its limit.
    Query = WorldQuery_Create();
    Query.InsideRegionObject = ParkId;
    Query.IsSortedByDistance = true;
    Query.Origin = TestWorld_At((Vector3){ -45.0f + 50.0f, 0.0f, 0.0f });
    Query.Limit = 1U;
    GenericBuffer_Clear(&Ids);
    TEST_CHECK_SUCCESS(run, World_Query(&TestWorldValue, &Query, &Ids));
    TEST_CHECK(run, (Ids._count == 1U) && (((const uint64_t*)Ids._data)[0] == SignId));

    // No such region, no path, an open path, a tag nobody carries: nothing matches, and none of them is an error.
    Query = WorldQuery_Create();
    Query.InsideRegionObject = 987654U;
    GenericBuffer_Clear(&Ids);
    TEST_CHECK_SUCCESS(run, World_Query(&TestWorldValue, &Query, &Ids));
    TEST_CHECK(run, Ids._count == 0U);
    Query.InsideRegionObject = BusId;
    TEST_CHECK_SUCCESS(run, World_Query(&TestWorldValue, &Query, &Ids));
    TEST_CHECK(run, Ids._count == 0U);
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetClosed(Plot, false));
    Query.InsideRegionObject = PlotId;
    TEST_CHECK_SUCCESS(run, World_Query(&TestWorldValue, &Query, &Ids));
    TEST_CHECK(run, Ids._count == 0U);
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetClosed(Plot, true));
    uint32_t NobodyTag = 0U;
    TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(&Keys, (const unsigned char*)u8"town:nobody", &NobodyTag));
    Query = WorldQuery_Create();
    Query.InsideRegionTag = NobodyTag;
    TEST_CHECK_SUCCESS(run, World_Query(&TestWorldValue, &Query, &Ids));
    TEST_CHECK(run, Ids._count == 0U);

    // A disabled region counts for a search that finds disabled things too, and not for a script's.
    TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(Park->Base._owner, false));
    Query = WorldQuery_Create();
    Query.InsideRegionTag = ParkTag;
    TEST_CHECK(run, TEST_CHECK_SUCCESS(run, World_DoesObjectMatch(&TestWorldValue, &Query, BenchId, &IsMatch)) && IsMatch);
    Query.IsEnabledOnly = true;
    TEST_CHECK(run, TEST_CHECK_SUCCESS(run, World_DoesObjectMatch(&TestWorldValue, &Query, BenchId, &IsMatch)) && !IsMatch);
    Query = WorldQuery_Create();
    Query.InsideRegionObject = ParkId;
    Query.IsEnabledOnly = true;
    TEST_CHECK(run, TEST_CHECK_SUCCESS(run, World_DoesObjectMatch(&TestWorldValue, &Query, BenchId, &IsMatch)) && !IsMatch);
    TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(Park->Base._owner, true));

    // A moved path takes its region along, at once: the world placement is read when asked.
    TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Park->Base._owner, TestWorld_At((Vector3){ 500.0f, 0.0f, 0.0f })));
    Query = WorldQuery_Create();
    Query.InsideRegionObject = ParkId;
    TEST_CHECK(run, TEST_CHECK_SUCCESS(run, World_DoesObjectMatch(&TestWorldValue, &Query, BenchId, &IsMatch)) && !IsMatch);
    Memory_Free(Ids._data);
    TestWorld_Deconstruct(run, &TestWorldValue, &Keys);
}

static void GroundProjectionFindsTheGroundUnderAPoint(TestRun* run)
{
    PathScene Scene;
    if (!Scene_Construct(run, &Scene, NULL))
    {
        Scene_Deconstruct(run, &Scene);
        return;
    }
    // A slope rising 1 m in 4 along X from x = 0, placed at the scene's origin, and a floating deck over its middle.
    AddTerrain(run, &Scene, "slope", Rising, (Vector3){ 0.0f, 0.0f, 0.0f });
    uint64_t Deck = AddBox(run, &Scene, (Vector3){ 20.0f, 10.0f, 20.0f }, (Vector3){ 8.0f, 1.0f, 8.0f }, CollisionLayer_Prop);
    WorldCollision_Sync(Scene.Collision);
    PathGroundSettings Settings = PathGroundSettings_CreateDefault();
    PathGroundHit Hit;

    // The ground under a point, whatever the point's height within the search.
    Vector3 Above = { 12.0f, 7.0f, 4.0f };
    TEST_CHECK(run, Project(&Scene, Above, &Settings, &Hit));
    Vector3 Ground = TestWorld_ToScene(Hit.Ground);
    TEST_CHECK(run, (fabsf(Ground.x - 12.0f) < 1e-4f) && (fabsf(Ground.y - 3.0f) < 1e-4f) && (fabsf(Ground.z - 4.0f) < 1e-4f));
    TEST_CHECK(run, Vector3Double_EqualsClose(Hit.Position, Hit.Ground, 0.0));
    TEST_CHECK(run, (Hit.Normal.y > 0.9f) && (Hit.Normal.x < 0.0f) && Hit.Shape.HasTriangle);

    // The offset lifts the answer, in double: the ground itself stays where it is.
    Settings.Offset = 0.5f;
    TEST_CHECK(run, Project(&Scene, Above, &Settings, &Hit));
    TEST_CHECK(run, (Hit.Position.Y - Hit.Ground.Y) == 0.5);
    Settings.Offset = -0.25f;
    TEST_CHECK(run, Project(&Scene, Above, &Settings, &Hit) && ((Hit.Position.Y - Hit.Ground.Y) == -0.25));
    Settings.Offset = 0.0f;

    // Terrain only: straight through the deck above the slope. On its layers: the deck's top, what a railing stands on.
    Vector3 OverDeck = { 20.0f, 10.8f, 20.0f };
    TEST_CHECK(run, Project(&Scene, OverDeck, &Settings, &Hit));
    TEST_CHECK(run, fabsf(TestWorld_ToScene(Hit.Ground).y - 5.0f) < 1e-4f);
    Settings.Target = PathGroundTarget_Layers;
    Settings.LayerMask = COLLISION_LAYER_BIT(CollisionLayer_Prop);
    TEST_CHECK(run, Project(&Scene, OverDeck, &Settings, &Hit));
    TEST_CHECK(run, (fabsf(TestWorld_ToScene(Hit.Ground).y - 10.5f) < 1e-4f) && (Hit.Shape.ObjectId == Deck));
    // Asked for the world layer the deck is not among them, and the slope answers.
    Settings.LayerMask = COLLISION_LAYER_BIT(CollisionLayer_World);
    TEST_CHECK(run, Project(&Scene, OverDeck, &Settings, &Hit) && (fabsf(TestWorld_ToScene(Hit.Ground).y - 5.0f) < 1e-4f));
    // Both layers: the nearer, the deck.
    Settings.LayerMask = COLLISION_LAYER_BIT(CollisionLayer_World) | COLLISION_LAYER_BIT(CollisionLayer_Prop);
    TEST_CHECK(run, Project(&Scene, OverDeck, &Settings, &Hit) && (Hit.Shape.ObjectId == Deck));
    // The reader's own colliders do not lift it: ignored, the deck is passed, and what hangs under it too.
    Settings.IgnoredObjectId = Deck;
    TEST_CHECK(run, Project(&Scene, OverDeck, &Settings, &Hit) && (Hit.Shape.ObjectId != Deck));
    uint64_t Plank = AddBox(run, &Scene, (Vector3){ 12.0f, 7.5f, 12.0f }, (Vector3){ 2.0f, 1.0f, 2.0f }, CollisionLayer_Prop);
    WorldObject* PlankObject = NULL;
    TEST_CHECK(run, World_TryGetObject(&Scene.World, Plank, &PlankObject));
    TEST_CHECK_SUCCESS(run, World_SetParent(&Scene.World, Plank, Deck, true));
    WorldCollision_Sync(Scene.Collision);
    TEST_CHECK(run, Project(&Scene, (Vector3){ 12.0f, 8.0f, 12.0f }, &Settings, &Hit) && (Hit.Shape.ObjectId != Plank));
    Settings.IgnoredObjectId = 0U;
    TEST_CHECK(run, Project(&Scene, (Vector3){ 12.0f, 8.0f, 12.0f }, &Settings, &Hit) && (Hit.Shape.ObjectId == Plank));

    // A surface the ray starts inside does not count: from inside the deck it goes on down to the slope.
    Vector3 InDeck = { 20.0f, 7.0f, 20.0f };
    Settings.SearchHeight = 3.0f; // 7 + 3 = 10: inside the deck, which spans 9.5 to 10.5
    TEST_CHECK(run, Project(&Scene, InDeck, &Settings, &Hit));
    TEST_CHECK(run, Hit.Shape.ObjectId != Deck);
    TEST_CHECK(run, fabsf(TestWorld_ToScene(Hit.Ground).y - 5.0f) < 1e-4f);
    Settings.SearchHeight = PATH_GROUND_DEFAULT_SEARCH_HEIGHT;

    // A point in a tunnel under the hill: the search starts within the hill, whose surface above it faces away, so it
    // finds the floor below the point and not the hilltop. A search from higher up finds the hill instead.
    Settings = PathGroundSettings_CreateDefault();
    Vector3 Under = { 24.0f, 1.0f, 20.0f }; // the slope is at 6 m here, 5 m over the point
    Settings.Target = PathGroundTarget_Layers;
    Settings.LayerMask = COLLISION_LAYER_BIT(CollisionLayer_World);
    AddBox(run, &Scene, (Vector3){ 24.0f, 0.0f, 20.0f }, (Vector3){ 4.0f, 1.0f, 4.0f }, CollisionLayer_World);
    WorldCollision_Sync(Scene.Collision);
    Settings.SearchHeight = 2.0f;
    TEST_CHECK(run, Project(&Scene, Under, &Settings, &Hit) && (fabsf(TestWorld_ToScene(Hit.Ground).y - 0.5f) < 1e-4f));
    Settings.SearchHeight = 8.0f;
    TEST_CHECK(run, Project(&Scene, Under, &Settings, &Hit) && (fabsf(TestWorld_ToScene(Hit.Ground).y - 6.0f) < 1e-4f));

    // The search reaches from its height above the point to its depth below it, and no further.
    Settings = PathGroundSettings_CreateDefault();
    Vector3 Reaching = { 4.0f, 3.0f, 4.0f }; // the slope is at 1 m here
    Settings.SearchDepth = 2.5f;
    TEST_CHECK(run, Project(&Scene, Reaching, &Settings, &Hit));
    Settings.SearchDepth = 1.5f;
    TEST_CHECK(run, !Project(&Scene, Reaching, &Settings, &Hit));

    // Nothing there, too far below, or from below a one-sided terrain: not found, and the hit is left alone.
    Settings = PathGroundSettings_CreateDefault();
    PathGroundHit Untouched;
    Memory_Set(&Untouched, 0x5A, sizeof(Untouched));
    Hit = Untouched;
    TEST_CHECK(run, !Project(&Scene, (Vector3){ -50.0f, 5.0f, 5.0f }, &Settings, &Hit));
    TEST_CHECK(run, memcmp(&Hit, &Untouched, sizeof(Hit)) == 0);
    Settings.SearchDepth = 1.0f;
    TEST_CHECK(run, !Project(&Scene, (Vector3){ 8.0f, 12.0f, 4.0f }, &Settings, &Hit));
    Settings.SearchDepth = PATH_GROUND_DEFAULT_SEARCH_DEPTH;
    TEST_CHECK(run, !Project(&Scene, (Vector3){ 8.0f, -10.0f, 4.0f }, &Settings, &Hit)); // under it: nothing below
    Settings.SearchHeight = 0.0f;
    Settings.SearchDepth = 0.0f;
    TEST_CHECK(run, !Project(&Scene, Above, &Settings, &Hit));
    Scene_Deconstruct(run, &Scene);
}

/* A tunnel's roof is terrain whose fronts face down, as a turned piece's do here: a ray from above goes through its back to
 * the floor, and the search is what tells the hill above a tunnel from the floor in it. */
static void GroundProjectionPassesARoofsBack(TestRun* run)
{
    PathScene Scene;
    if (!Scene_Construct(run, &Scene, NULL))
    {
        Scene_Deconstruct(run, &Scene);
        return;
    }
    uint64_t FloorId = AddTerrain(run, &Scene, "floor", Flat, (Vector3){ 0.0f, 0.0f, 0.0f });
    uint64_t RoofId = AddTerrain(run, &Scene, "roof", Flat, (Vector3){ 0.0f, 8.0f, 40.0f });
    uint64_t HillId = AddTerrain(run, &Scene, "hill", Flat, (Vector3){ 0.0f, 12.0f, 0.0f });
    WorldObject* Roof = NULL;
    TEST_CHECK(run, World_TryGetObject(&Scene.World, RoofId, &Roof));
    // A half turn about X: its fronts face down, and it still spans z 0 to 40.
    TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(Roof, (Vector3){ PI, 0.0f, 0.0f }));
    WorldCollision_Sync(Scene.Collision);
    PathGroundSettings Settings = PathGroundSettings_CreateDefault();
    PathGroundHit Hit;
    // In the tunnel at 1 m, searching from 6 m: under the roof, it finds the floor.
    TEST_CHECK(run, Project(&Scene, (Vector3){ 10.0f, 1.0f, 10.0f }, &Settings, &Hit) && (Hit.Shape.ObjectId == FloorId));
    // Searching from above the roof, through its back, it still finds the floor.
    Settings.SearchHeight = 9.0f;
    TEST_CHECK(run, Project(&Scene, (Vector3){ 10.0f, 1.0f, 10.0f }, &Settings, &Hit) && (Hit.Shape.ObjectId == FloorId));
    // Searching from above the hill it finds the hill, which is why the search starts low by default.
    Settings.SearchHeight = 20.0f;
    TEST_CHECK(run, Project(&Scene, (Vector3){ 10.0f, 1.0f, 10.0f }, &Settings, &Hit) && (Hit.Shape.ObjectId == HillId));
    Scene_Deconstruct(run, &Scene);
}

/* What it sees is what the collision world saw at its last sync, and a terrain that does not collide is not found. */
static void GroundProjectionSeesWhatCollides(TestRun* run)
{
    PathScene Scene;
    if (!Scene_Construct(run, &Scene, NULL))
    {
        Scene_Deconstruct(run, &Scene);
        return;
    }
    uint64_t Id = AddTerrain(run, &Scene, "field", Flat, (Vector3){ 0.0f, 2.0f, 0.0f });
    PathGroundSettings Settings = PathGroundSettings_CreateDefault();
    PathGroundHit Hit;
    Vector3 Point = { 10.0f, 3.0f, 10.0f };
    TEST_CHECK(run, !Project(&Scene, Point, &Settings, &Hit)); // not synced yet
    WorldCollision_Sync(Scene.Collision);
    TEST_CHECK(run, Project(&Scene, Point, &Settings, &Hit) && (Hit.Shape.ObjectId == Id));
    WorldComponent* Component = NULL;
    TEST_CHECK(run, World_TryGetComponent(&Scene.World, Id, WorldComponentType_Terrain, &Component));
    WorldTerrainComponent* Terrain = WorldTerrainComponent_FromComponent(Component);
    PropertyValue Value = PropertyValue_FromBool(true);
    TEST_CHECK_SUCCESS(run, Properties_Set(WorldTerrainComponent_GetPropertyTable(), Terrain,
        u8"overrides_is_collision_enabled", &Value));
    Value = PropertyValue_FromBool(false);
    TEST_CHECK_SUCCESS(run, Properties_Set(WorldTerrainComponent_GetPropertyTable(), Terrain, u8"is_collision_enabled",
        &Value));
    WorldCollision_Sync(Scene.Collision);
    TEST_CHECK(run, !Project(&Scene, Point, &Settings, &Hit));
    Scene_Deconstruct(run, &Scene);
}

/* How many of a list's lines have a colour. */
static size_t CountLines(const GenericBuffer* lines, Color color)
{
    size_t Count = 0U;
    const WorldDebugLine* Lines = (const WorldDebugLine*)lines->_data;
    for (size_t Index = 0U; Index < lines->_count; Index++)
    {
        Color Other = Lines[Index].LineColor;
        if ((Other.r == color.r) && (Other.g == color.g) && (Other.b == color.b) && (Other.a == color.a))
        {
            Count++;
        }
    }
    return Count;
}

static void TheDebugViewDrawsWhatThePathsHold(TestRun* run)
{
    // The view's colours, as WorldPathDebug.c holds them.
    static const Color Curve0 = { 255, 150, 40, 255 };
    static const Color Curve1 = { 60, 220, 180, 255 };
    static const Color Skipped = { 90, 90, 90, 255 };
    static const Color Smooth = { 255, 255, 255, 255 };
    static const Color Corner = { 255, 230, 40, 255 };
    static const Color Bezier = { 40, 220, 255, 255 };
    static const Color Tangent = { 70, 110, 255, 255 };
    static const Color Up = { 60, 230, 80, 255 };
    static const Color Right = { 240, 60, 50, 255 };
    static const Color Sample = { 220, 220, 220, 255 };
    static const Color Ground = { 150, 150, 150, 255 };
    static const Color NoSnap = { 170, 90, 255, 255 };
    static const Color Hatch = { 90, 230, 130, 130 };

    PathScene Scene;
    if (!Scene_Construct(run, &Scene, NULL))
    {
        Scene_Deconstruct(run, &Scene);
        return;
    }
    GenericBuffer Lines;
    GenericBuffer_AllocateVariable(&Lines, 0U, sizeof(WorldDebugLine));
    WorldPathDebugOptions Options = { .ShowsDetail = false, .Collision = NULL };
    WorldPathDebug_Build(&Scene.World, &Options, &Lines);
    TEST_CHECK(run, Lines._count == 0U);

    // Three sections, the middle one skipped: 10 m at a spacing of 2, corners and a bezier node.
    AddTerrain(run, &Scene, "field", Flat, (Vector3){ 0.0f, 0.0f, 0.0f });
    WorldCollision_Sync(Scene.Collision);
    WorldObject* Object = MakeObject(run, &Scene.World, "walk", (Vector3){ 2.0f, 1.0f, 2.0f });
    WorldPathComponent* Path = AddPath(run, &Scene.World, Object);
    if (Path == NULL)
    {
        Memory_Free(Lines._data);
        Scene_Deconstruct(run, &Scene);
        return;
    }
    PathNode Nodes[4] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner),
        MakeNode(14.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(24.0, 0.0, 0.0, PathNodeMode_Smooth) };
    Nodes[1].SkipsSection = true;
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(Path, Nodes, 4U));
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetSampleSpacing(Path, 2.0f));
    GenericBuffer_Clear(&Lines);
    WorldPathDebug_Build(&Scene.World, &Options, &Lines);
    // The first section's and the last's curves (their section ticks in the same colours), the skipped one's dim.
    TEST_CHECK(run, CountLines(&Lines, Curve0) == (5U + 1U));
    TEST_CHECK(run, CountLines(&Lines, Skipped) == (2U + 1U));
    TEST_CHECK(run, CountLines(&Lines, Corner) == 9U); // three axes each
    TEST_CHECK(run, CountLines(&Lines, Smooth) == 3U);
    TEST_CHECK(run, CountLines(&Lines, Bezier) == 0U);
    TEST_CHECK(run, (CountLines(&Lines, Tangent) == 0U) && (CountLines(&Lines, Sample) == 0U));
    TEST_CHECK(run, CountLines(&Lines, Hatch) == 0U); // an open path has no area
    // The lines lie in the world where the object puts them, in double.
    const WorldDebugLine* First = (const WorldDebugLine*)Lines._data;
    Vector3 FirstEnd = TestWorld_ToScene(First[0].From);
    TEST_CHECK(run, (fabsf(FirstEnd.x - 2.0f) < 1e-5f) && (fabsf(FirstEnd.y - 1.0f) < 1e-5f) && (fabsf(FirstEnd.z - 2.0f) < 1e-5f));

    // A node's cross stands where the object puts it: the second corner's centre, 10 m along.
    bool HasNodeCross = false;
    for (size_t Index = 0U; Index < Lines._count; Index++)
    {
        Color Other = First[Index].LineColor;
        Vector3 Middle = TestWorld_ToScene(Vector3Double_Lerp(First[Index].From, First[Index].To, 0.5));
        if ((Other.r == Corner.r) && (Other.g == Corner.g) && (Other.b == Corner.b) && (fabsf(Middle.x - 12.0f) < 1e-4f)
            && (fabsf(Middle.y - 1.0f) < 1e-4f) && (fabsf(Middle.z - 2.0f) < 1e-4f))
        {
            HasNodeCross = true;
        }
    }
    TEST_CHECK(run, HasNodeCross);

    // A bezier node shows its handles: two lines, and a cross at each end.
    Nodes[1] = MakeNode(10.0, 0.0, 0.0, PathNodeMode_Bezier);
    Nodes[1].InHandle = (Vector3){ -1.0f, 0.0f, 0.0f };
    Nodes[1].OutHandle = (Vector3){ 1.0f, 0.0f, 0.0f };
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(Path, Nodes, 4U));
    GenericBuffer_Clear(&Lines);
    WorldPathDebug_Build(&Scene.World, &Options, &Lines);
    TEST_CHECK(run, CountLines(&Lines, Bezier) == (3U + 2U + 6U));

    // Detail: a frame every 2 m, a tick per sample and the ground under each, except where a section turns snapping off.
    Nodes[1] = MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner);
    Nodes[2].OverridesSnapsToTerrain = true;
    Nodes[2].SnapsToTerrain = false;
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(Path, Nodes, 4U));
    Options.ShowsDetail = true;
    Options.Collision = Scene.Collision;
    GenericBuffer_Clear(&Lines);
    WorldPathDebug_Build(&Scene.World, &Options, &Lines);
    size_t Samples = 6U + 3U + 6U; // 10, 4 and 10 m at a spacing of 2
    TEST_CHECK(run, CountLines(&Lines, Sample) == Samples);
    TEST_CHECK(run, (CountLines(&Lines, Tangent) == Samples) && (CountLines(&Lines, Up) == Samples)
        && (CountLines(&Lines, Right) == Samples));
    TEST_CHECK(run, CountLines(&Lines, NoSnap) == 6U);
    TEST_CHECK(run, CountLines(&Lines, Ground) == ((6U + 3U) * 4U)); // a line and a cross of three
    // A ground line ends on the ground, 1 m under the path.
    const WorldDebugLine* Every = (const WorldDebugLine*)Lines._data;
    bool HasGroundLine = false;
    for (size_t Index = 0U; Index < Lines._count; Index++)
    {
        Color Other = Every[Index].LineColor;
        if ((Other.r == Ground.r) && (Other.g == Ground.g) && (Every[Index].From.X == Every[Index].To.X)
            && (Every[Index].From.Z == Every[Index].To.Z) && (Every[Index].From.Y > Every[Index].To.Y + 0.5))
        {
            HasGroundLine = true;
            TEST_CHECK(run, fabsf(TestWorld_ToScene(Every[Index].To).y) < 1e-4f);
            TEST_CHECK(run, fabsf(TestWorld_ToScene(Every[Index].From).y - 1.0f) < 1e-4f);
        }
    }
    TEST_CHECK(run, HasGroundLine);
    // More samples than frames at a shorter spacing: a frame every 2 m whatever the samples do.
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetSampleSpacing(Path, 0.5f));
    GenericBuffer_Clear(&Lines);
    WorldPathDebug_Build(&Scene.World, &Options, &Lines);
    TEST_CHECK(run, (CountLines(&Lines, Sample) == (21U + 9U + 21U)) && (CountLines(&Lines, Tangent) == Samples)
        && (CountLines(&Lines, NoSnap) == 6U));
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetSampleSpacing(Path, 2.0f));

    // No collision world: the rest of the detail, no ground and no asking.
    Options.Collision = NULL;
    GenericBuffer_Clear(&Lines);
    WorldPathDebug_Build(&Scene.World, &Options, &Lines);
    TEST_CHECK(run, (CountLines(&Lines, Ground) == 0U) && (CountLines(&Lines, NoSnap) == 0U)
        && (CountLines(&Lines, Sample) == Samples));

    // A disabled path shows nothing; a writer appends.
    TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(Object, false));
    GenericBuffer_Clear(&Lines);
    WorldPathDebug_Build(&Scene.World, &Options, &Lines);
    TEST_CHECK(run, Lines._count == 0U);
    TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(Object, true));

    // A closed area: hatching only inside it, the region tests' inside, by a concave shape's two spans.
    // It starts in the notch, so a line's crossings do not come in order along the outline.
    PathNode Shape[8] = { MakeNode(20.0, 0.0, 30.0, PathNodeMode_Corner), MakeNode(20.0, 0.0, 10.0, PathNodeMode_Corner),
        MakeNode(10.0, 0.0, 10.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 30.0, PathNodeMode_Corner),
        MakeNode(0.0, 0.0, 30.0, PathNodeMode_Corner), MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner),
        MakeNode(30.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(30.0, 0.0, 30.0, PathNodeMode_Corner) };
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(Path, Shape, 8U));
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetClosed(Path, true));
    Options.ShowsDetail = false;
    GenericBuffer_Clear(&Lines);
    WorldPathDebug_Build(&Scene.World, &Options, &Lines);
    TEST_CHECK(run, CountLines(&Lines, Curve1) > 0U);
    TEST_CHECK(run, CountLines(&Lines, Hatch) == (5U + (10U * 2U)));
    const WorldDebugLine* Hatched = (const WorldDebugLine*)Lines._data;
    for (size_t Index = 0U; Index < Lines._count; Index++)
    {
        if (Hatched[Index].LineColor.a == Hatch.a)
        {
            Vector3Double Middle = Vector3Double_Lerp(Hatched[Index].From, Hatched[Index].To, 0.5);
            TEST_CHECK(run, WorldPathComponent_IsWorldPointInside(Path, Middle));
            TEST_CHECK(run, fabsf(TestWorld_ToScene(Hatched[Index].From).y - 1.0f) < 1e-4f);
        }
    }

    // A list stops at its limit, whatever the paths hold: a very long path, however short its spacing, and three of them.
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetClosed(Path, false));
    PathNode Long[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(0.0, 0.0, 5000000.0, PathNodeMode_Corner) };
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(Path, Long, 2U));
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetSampleSpacing(Path, 0.01f));
    Options.ShowsDetail = true;
    GenericBuffer_Clear(&Lines);
    WorldPathDebug_Build(&Scene.World, &Options, &Lines);
    TEST_CHECK(run, (Lines._count > 0U) && (Lines._count < WORLD_PATH_DEBUG_MAX_LINES));
    for (int Copy = 0; Copy < 2; Copy++)
    {
        WorldObject* Another = MakeObject(run, &Scene.World, "another", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPathComponent* AnotherPath = AddPath(run, &Scene.World, Another);
        if (AnotherPath != NULL)
        {
            TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(AnotherPath, Long, 2U));
        }
    }
    GenericBuffer_Clear(&Lines);
    WorldPathDebug_Build(&Scene.World, &Options, &Lines);
    TEST_CHECK(run, Lines._count == WORLD_PATH_DEBUG_MAX_LINES);
    Memory_Free(Lines._data);
    Scene_Deconstruct(run, &Scene);
}

/* A file holds a path as authored, in both formats, and comes back with its curve built. */
static void PathsGoThroughWorldFiles(TestRun* run)
{
    World Source;
    KeyRegistry Keys;
    if (!TestWorld_Construct(run, &Source, &Keys, NULL, NULL))
    {
        return;
    }
    WorldObject* Object = MakeObject(run, &Source, "path", (Vector3){ 1.0f, 2.0f, 3.0f });
    WorldPathComponent* Path = AddPath(run, &Source, Object);
    if (Path == NULL)
    {
        TestWorld_Deconstruct(run, &Source, &Keys);
        return;
    }
    PathNode Nodes[3] = { MakeNode(0.125, 0.0, 987.6543210987, PathNodeMode_Smooth), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner),
        MakeNode(20.0, 1.0, 5.0, PathNodeMode_Bezier) };
    Nodes[0].Roll = 0.5f;
    Nodes[0].Width = 2.0f;
    Nodes[1].Scale = 0.5f;
    Nodes[1].SkipsSection = true;
    Nodes[2].InHandle = (Vector3){ -1.0f, 0.5f, 0.25f };
    Nodes[2].OutHandle = (Vector3){ 2.0f, -0.5f, 1.25f };
    Nodes[2].StartsSection = true;
    Nodes[2].OverridesSnapsToTerrain = true;
    Nodes[2].SnapsToTerrain = false;
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(Path, Nodes, 3U));
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetClosed(Path, true));
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetUpMode(Path, PathUpMode_RotationMinimizing));
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetSampleSpacing(Path, 0.25f));
    uint64_t Id = WorldObject_GetId(Object);

    for (int IsBinary = 0; IsBinary < 2; IsBinary++)
    {
        GenericBuffer Bytes;
        GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
        TEST_CHECK_SUCCESS(run, (IsBinary != 0) ? WorldFile_WriteGHDF(&Source, &Bytes) : WorldFile_WriteJSON(&Source, &Bytes));
        KeyRegistry LoadedKeys;
        World Loaded;
        WorldFileReadOptions Options = WorldFileReadOptions_Create(&LoadedKeys, NULL, NULL);
        if (TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&LoadedKeys)))
        {
            Error Result = (IsBinary != 0) ? WorldFile_ReadGHDF(Bytes._data, Bytes._count, &Options, &Loaded)
                : WorldFile_ReadJSON(Bytes._data, Bytes._count, &Options, &Loaded);
            if (TEST_CHECK_SUCCESS(run, Result))
            {
                WorldComponent* Component = NULL;
                TEST_CHECK(run, World_TryGetComponent(&Loaded, Id, WorldComponentType_Path, &Component));
                WorldPathComponent* Read = WorldPathComponent_FromComponent(Component);
                if (TEST_CHECK(run, Read != NULL))
                {
                    TEST_CHECK(run, Properties_Equals(WorldPathComponent_GetPropertyTable(), Path, Read));
                    const PathCurve* Curve = WorldPathComponent_GetCurve(Read);
                    TEST_CHECK(run, PathCurve_IsClosed(Curve) && (PathCurve_GetNodeCount(Curve) == 3U)
                        && (PathCurve_GetLength(Curve) == PathCurve_GetLength(WorldPathComponent_GetCurve(Path)))
                        && (Curve->_tableRights != NULL) && (PathCurve_GetSectionCount(Curve) == 2U));
                }
                TEST_CHECK_SUCCESS(run, World_Deconstruct(&Loaded));
            }
            KeyRegistry_Deconstruct(&LoadedKeys);
        }
        Memory_Free(Bytes._data);
    }

    // The JSON leaves each node's defaults out, and the writer's text reads as the reference says.
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, 0U, sizeof(unsigned char));
    TEST_CHECK_SUCCESS(run, WorldFile_WriteJSON(&Source, &Text));
    TEST_CHECK(run, GenericBuffer_NullTerminate(&Text));
    const char* Written = (const char*)Text._data;
    TEST_CHECK(run, (strstr(Written, "\"path\"") != NULL) && (strstr(Written, "\"is_closed\": true") != NULL)
        && (strstr(Written, "\"up_mode\": \"rotation_minimizing\"") != NULL) && (strstr(Written, "\"mode\": \"bezier\"") != NULL)
        && (strstr(Written, "\"mode\": \"corner\"") != NULL) && (strstr(Written, "\"mode\": \"smooth\"") == NULL)
        && (strstr(Written, "987.6543210987") != NULL) && (strstr(Written, "\"snaps_to_terrain\": false") != NULL)
        && (strstr(Written, "\"starts_section\": false") == NULL));
    Memory_Free(Text._data);
    TestWorld_Deconstruct(run, &Source, &Keys);
}


// Public functions.
void WorldPathTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_curve_follows_every_change", TheCurveFollowsEveryChange },
        { "a_reader_finds_the_nearest_path_above_it", AReaderFindsTheNearestPathAboveIt },
        { "a_watch_sees_what_changed", AWatchSeesWhatChanged },
        { "points_reach_the_world_in_double", PointsReachTheWorldInDouble },
        { "queries_find_what_is_inside_a_region", QueriesFindWhatIsInsideARegion },
        { "ground_projection_finds_the_ground_under_a_point", GroundProjectionFindsTheGroundUnderAPoint },
        { "ground_projection_passes_a_roofs_back", GroundProjectionPassesARoofsBack },
        { "ground_projection_sees_what_collides", GroundProjectionSeesWhatCollides },
        { "the_debug_view_draws_what_the_paths_hold", TheDebugViewDrawsWhatThePathsHold },
        { "paths_go_through_world_files", PathsGoThroughWorldFiles },
    };
    TestWorld_RunSuite(run, "WorldPath", "WorldPathFar", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
