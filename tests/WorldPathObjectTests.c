#include "TestSuites.h"
#include "TestAssets.h"
#include "TestTerrain.h"
#include "TestText.h"
#include "TestWorld.h"
#include "AssetManager.h"
#include "CollisionLayer.h"
#include "PathObject.h"
#include "PathObjectPlacement.h"
#include "TerrainAsset.h"
#include "World.h"
#include "WorldColliderComponent.h"
#include "WorldCollision.h"
#include "WorldFile.h"
#include "WorldGeneratedModel.h"
#include "WorldModelComponent.h"
#include "WorldModelInstancesComponent.h"
#include "WorldObject.h"
#include "WorldPathComponent.h"
#include "WorldPathObjectComponent.h"
#include "WorldPathObjects.h"
#include "WorldRenderCache.h"
#include "WorldTerrainComponent.h"
#include "WorldTransform.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"
#include <math.h>
#include <string.h>


// Macros.
#define NEAR(a, b, tolerance) (fabs((double)(a) - (double)(b)) <= (double)(tolerance))
/** How close a placed thing must lie, in metres: a double's work, so far out as at the origin. */
#define PLACE_TOLERANCE 1.0e-3


// Types.
/* A world with the fixture's assets, its collision world and the system. */
typedef struct SceneStruct
{
    TestAssetFixture Fixture;
    World World;
    KeyRegistry Keys;
    WorldCollision* Collision;
    WorldPathObjects* System;
    bool IsWorldBuilt;
} Scene;


// Static functions: the scene.
/* A box along X from @p fromX of @p length, standing on Y = 0: two triangles a face, unindexed. */
static void WriteBoxVertices(float fromX, float length, float height, float halfWidth, float* outVertices)
{
    const float X0 = fromX;
    const float X1 = fromX + length;
    const float Corners[8][3] =
    {
        { X0, 0.0f, -halfWidth }, { X1, 0.0f, -halfWidth }, { X1, height, -halfWidth }, { X0, height, -halfWidth },
        { X0, 0.0f, halfWidth }, { X1, 0.0f, halfWidth }, { X1, height, halfWidth }, { X0, height, halfWidth }
    };
    const int Faces[6][4] = { { 0, 3, 2, 1 }, { 4, 5, 6, 7 }, { 0, 4, 7, 3 }, { 1, 2, 6, 5 }, { 3, 7, 6, 2 }, { 0, 1, 5, 4 } };
    const int Fan[6] = { 0, 1, 2, 0, 2, 3 };
    size_t Written = 0U;
    for (size_t Face = 0U; Face < 6U; Face++)
    {
        for (size_t Corner = 0U; Corner < 6U; Corner++)
        {
            const float* Point = Corners[Faces[Face][Fan[Corner]]];
            outVertices[Written++] = Point[0];
            outVertices[Written++] = Point[1];
            outVertices[Written++] = Point[2];
        }
    }
}

static bool AddBoxModel(TestRun* run, Scene* scene, const char* name, float fromX, float length)
{
    float Vertices[108];
    WriteBoxVertices(fromX, length, 1.0f, 0.1f, Vertices);
    TestModelPart Part = { .Vertices = Vertices, .Texcoords = NULL, .VertexCount = 36, .Indices = NULL, .TriangleCount = 0,
        .Material = 0 };
    return TestAssets_AddPartsModel(run, &scene->Fixture, name, &Part, 1, 1, MatrixIdentity(), NULL);
}

/* A wedge along X from 0: a box 1 high to X = 1 in material 0, and one 2 high to X = 2 in material 1, its tangents along +X.
 * Which way round it lies shows in its heights. */
static bool AddWedgeModel(TestRun* run, Scene* scene)
{
    float Low[108];
    float High[108];
    WriteBoxVertices(0.0f, 1.0f, 1.0f, 0.1f, Low);
    WriteBoxVertices(1.0f, 1.0f, 2.0f, 0.1f, High);
    float Tangents[36 * 4];
    for (size_t Vertex = 0U; Vertex < 36U; Vertex++)
    {
        Tangents[Vertex * 4U] = 1.0f;
        Tangents[(Vertex * 4U) + 1U] = 0.0f;
        Tangents[(Vertex * 4U) + 2U] = 0.0f;
        Tangents[(Vertex * 4U) + 3U] = 1.0f;
    }
    TestModelPart Parts[2] =
    {
        { .Vertices = Low, .VertexCount = 36, .Material = 0, .Tangents = Tangents },
        { .Vertices = High, .VertexCount = 36, .Material = 1, .Tangents = Tangents }
    };
    return TestAssets_AddPartsModel(run, &scene->Fixture, "wedge", Parts, 2, 2, MatrixIdentity(), NULL);
}

/* A box of 2 m in its file whose import transform doubles its X: a piece 4 m long. */
static bool AddLongPanelModel(TestRun* run, Scene* scene)
{
    float Vertices[108];
    WriteBoxVertices(0.0f, 2.0f, 1.0f, 0.1f, Vertices);
    TestModelPart Part = { .Vertices = Vertices, .VertexCount = 36 };
    return TestAssets_AddPartsModel(run, &scene->Fixture, "long_panel", &Part, 1, 1, MatrixScale(2.0f, 1.0f, 1.0f), NULL);
}

static bool AddPathObjectAsset(TestRun* run, Scene* scene, const char* json)
{
    GenericBuffer Raw;
    GenericBuffer_AllocateVariable(&Raw, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Raw, (const unsigned char*)json);
    AssetDefinition* Definition = NULL;
    bool IsBuilt = TEST_CHECK_SUCCESS(run, AssetManager_BuildDefinition(scene->Fixture.Manager, scene->Fixture.Types.PathObject,
        &Raw,
        (const unsigned char*)u8"test path object", &Definition));
    Memory_Free(Raw._data);
    return IsBuilt && TEST_CHECK_SUCCESS(run, AssetManager_SetDefinition(scene->Fixture.Manager, Definition));
}

/* The scene, with a panel of 2 m, a post of 0.2 m and a rail bent in two LODs, and the assets over them. */
static bool Scene_Construct(TestRun* run, Scene* self)
{
    Memory_Zero(self, sizeof(*self));
    if (!TestAssetFixture_Construct(run, &self->Fixture))
    {
        return false;
    }
    self->IsWorldBuilt = TestWorld_Construct(run, &self->World, &self->Keys, self->Fixture.Manager, NULL);
    if (!self->IsWorldBuilt || !TEST_CHECK_SUCCESS(run, WorldCollision_Create(&self->World, self->Fixture.Manager, NULL, NULL,
        &self->Collision)))
    {
        return false;
    }
    bool IsMade = AddBoxModel(run, self, "panel", 0.0f, 2.0f) && AddBoxModel(run, self, "post", -0.1f, 0.2f)
        && AddBoxModel(run, self, "rail", 0.0f, 1.0f) && AddBoxModel(run, self, "rail_far", 0.0f, 1.0f)
        && AddWedgeModel(run, self) && AddLongPanelModel(run, self);
    IsMade = IsMade && TestAssets_AddWorldModel(run, &self->Fixture, "{\"name\": \"wedge\", \"lods\": [{\"model\": \"wedge\", "
        "\"max_distance\": 500}]}");
    IsMade = IsMade && TestAssets_AddWorldModel(run, &self->Fixture,
        "{\"name\": \"long_panel\", \"lods\": [{\"model\": \"long_panel\", "
        "\"max_distance\": 500}]}");
    IsMade = IsMade && TestAssets_AddWorldModel(run, &self->Fixture, "{\"name\": \"gatepost\", \"lods\": [{\"model\": \"post\", "
        "\"max_distance\": 500}], \"collision\": { \"layer\": \"world\", \"default_set\": \"small\", \"sets\": ["
        "{ \"name\": \"small\", \"shapes\": [ { \"type\": \"box\", \"center\": [0, 1, 0], \"half_extents\": [0.5, 1, 0.5] } ] }, "
        "{ \"name\": \"tall\", \"shapes\": [ { \"type\": \"box\", \"center\": [0, 3, 0], \"half_extents\": [0.5, 3, 0.5] } ] } "
            "] } }");
    IsMade = IsMade && AddPathObjectAsset(run, self,
        "{ \"name\": \"wedges\", \"models\": [ { \"slot\": \"middle\", \"world_model\": "
        "\"wedge\" } ], \"slots\": [ { \"slot\": \"middle\", \"curve\": \"bent\", \"slice_spacing\": 0.25, \"collision\": "
            "\"none\" } ] }");
    IsMade = IsMade && AddPathObjectAsset(run, self,
        "{ \"name\": \"long_panels\", \"models\": [ { \"slot\": \"middle\", \"world_model\": "
        "\"long_panel\" } ] }");
    IsMade = IsMade && AddPathObjectAsset(run, self,
        "{ \"name\": \"gateposts\", \"models\": [ { \"slot\": \"middle\", \"world_model\": "
        "\"gatepost\" } ], \"fit\": \"fixed\", \"uses_gap\": true, \"gap\": 5.0, \"slots\": [ { \"slot\": \"middle\", "
            "\"length\": 0 } ] }");
    IsMade = IsMade && TestAssets_AddWorldModel(run, &self->Fixture, "{\"name\": \"panel\", \"lods\": [{\"model\": \"panel\", "
        "\"max_distance\": 500}]}");
    IsMade = IsMade && TestAssets_AddWorldModel(run, &self->Fixture, "{\"name\": \"post\", \"lods\": [{\"model\": \"post\", "
        "\"max_distance\": 500}], \"collision\": \"bounds\"}");
    IsMade = IsMade && TestAssets_AddWorldModel(run, &self->Fixture, "{\"name\": \"rail\", \"lods\": [{\"model\": \"rail\", "
        "\"max_distance\": 30}, {\"model\": \"rail_far\", \"max_distance\": 500}]}");
    IsMade = IsMade && AddPathObjectAsset(run, self, "{ \"name\": \"fence\", \"has_separator\": true, \"has_start_cap\": true, "
        "\"has_end_cap\": true, \"models\": [ { \"slot\": \"middle\", \"world_model\": \"panel\" }, { \"slot\": \"separator\", "
        "\"world_model\": \"post\" }, { \"slot\": \"start_cap\", \"world_model\": \"post\" }, { \"slot\": \"end_cap\", "
            "\"world_model\": "
        "\"post\" } ], \"slots\": [ { \"slot\": \"separator\", \"length\": 0 }, { \"slot\": \"start_cap\", \"length\": 0 }, { "
            "\"slot\": "
        "\"end_cap\", \"length\": 0 } ] }");
    IsMade = IsMade && AddPathObjectAsset(run, self,
        "{ \"name\": \"hedge\", \"models\": [ { \"slot\": \"middle\", \"world_model\": "
        "\"rail\" } ], \"slots\": [ { \"slot\": \"middle\", \"curve\": \"bent\", \"slice_spacing\": 0.25, \"collision\": "
            "\"mesh\" } ] }");
    return IsMade && TEST_CHECK_SUCCESS(run, WorldPathObjects_Create(&self->World, self->Collision, self->Fixture.Manager, NULL,
        &self->System));
}

static void Scene_Deconstruct(TestRun* run, Scene* self)
{
    WorldPathObjects_Destroy(self->System);
    WorldCollision_Destroy(self->Collision);
    if (self->IsWorldBuilt)
    {
        TestWorld_Deconstruct(run, &self->World, &self->Keys);
    }
    TestAssetFixture_Deconstruct(run, &self->Fixture);
}

static void Scene_Update(Scene* self)
{
    WorldCollision_Sync(self->Collision);
    WorldPathObjects_Update(self->System);
    WorldCollision_Sync(self->Collision);
}

static PathNode MakeNode(double x, double y, double z, PathNodeMode mode)
{
    PathNode Node = PathNode_CreateDefault();
    Node.Position = Vector3Double_Create(x, y, z);
    Node.Mode = mode;
    return Node;
}

/* An object at a place of the scene with a path through @p nodes and a path object of an asset; its id, 0 on a failure. */
static uint64_t AddFence(TestRun* run, Scene* scene, Vector3 position, const PathNode* nodes, size_t nodeCount, const char* asset,
    bool snaps)
{
    WorldObject* Object = NULL;
    WorldComponent* PathPart = NULL;
    WorldComponent* ObjectPart = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(&scene->World, (const unsigned char*)u8"fence", &Object)))
    {
        return 0U;
    }
    uint64_t Id = WorldObject_GetId(Object);
    TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, TestWorld_At(position)));
    if (!TEST_CHECK_SUCCESS(run, World_AddComponent(&scene->World, Id, WorldComponentType_Path, &PathPart))
        || !TEST_CHECK_SUCCESS(run, World_AddComponent(&scene->World, Id, WorldComponentType_PathObject, &ObjectPart)))
    {
        return 0U;
    }
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(WorldPathComponent_FromComponent(PathPart), nodes, nodeCount));
    WorldPathObjectComponent* Component = WorldPathObjectComponent_FromComponent(ObjectPart);
    TEST_CHECK_SUCCESS(run, WorldPathObjectComponent_SetPathObject(Component, (const unsigned char*)asset));
    PropertyValue Flag = PropertyValue_FromBool(!snaps);
    PropertyValue Value = PropertyValue_FromBool(snaps);
    const PropertyTable* Table = WorldPathObjectComponent_GetPropertyTable();
    TEST_CHECK_SUCCESS(run, Properties_Set(Table, Component, (const unsigned char*)u8"overrides_snaps_to_terrain", &Flag));
    TEST_CHECK_SUCCESS(run, Properties_Set(Table, Component, (const unsigned char*)u8"snaps_to_terrain", &Value));
    return Id;
}

static WorldPathObjectComponent* GetComponent(Scene* scene, uint64_t id)
{
    WorldComponent* Component = NULL;
    return World_TryGetComponent(&scene->World, id, WorldComponentType_PathObject, &Component)
        ? WorldPathObjectComponent_FromComponent(Component) : NULL;
}

static WorldPathComponent* GetPath(Scene* scene, uint64_t id)
{
    WorldComponent* Component = NULL;
    return World_TryGetComponent(&scene->World, id, WorldComponentType_Path, &Component)
        ? WorldPathComponent_FromComponent(Component) : NULL;
}

/* Sets a placement's row by its key. */
static void SetRow(TestRun* run, Scene* scene, uint64_t id, const char* key, PropertyValue value)
{
    TEST_CHECK_SUCCESS(run, Properties_Set(WorldPathObjectComponent_GetPropertyTable(), GetComponent(scene, id),
        (const unsigned char*)key, &value));
}

// Tests: collision.
/* What a ray straight down finds at a point of the scene, on every layer: true when it hits. */
static bool RayDownHits(Scene* scene, Vector3 point, WorldRayHit* outHit)
{
    WorldCollisionFilter Filter = WorldCollisionFilter_Create(COLLISION_LAYER_MASK_ALL, WorldCollisionTriggerMode_Ignore);
    return WorldCollision_Raycast(scene->Collision, TestWorld_At(point), (Vector3){ 0.0f, -1.0f, 0.0f }, 20.0f, &Filter, outHit);
}

// Tests: bent pieces.
/* The child holding part @p index of bent pieces: its model component; NULL for none. */
static WorldModelComponent* FindPart(Scene* scene, uint64_t id, size_t index)
{
    WorldObject* Parent = NULL;
    if (!World_TryGetObject(&scene->World, id, &Parent))
    {
        return NULL;
    }
    size_t Found = 0U;
    for (WorldObject* Child = WorldObject_GetFirstChild(Parent); Child != NULL; Child = WorldObject_GetNextSibling(Child))
    {
        WorldModelComponent* Part = WorldModelComponent_FromComponent(WorldObject_GetComponent(Child, WorldComponentType_Model));
        if ((Part != NULL) && (Found++ == index))
        {
            return Part;
        }
    }
    return NULL;
}

/* Sets a placement's list of slot records to these. */
static void SetSlots(TestRun* run, Scene* scene, uint64_t id, const PathObjectSlotRecord* records, size_t count)
{
    PropertyRecordList* List = NULL;
    TEST_CHECK_SUCCESS(run, PropertyRecordList_Create(PathObject_GetSlotPropertyTable(), records, count, &List));
    SetRow(run, scene, id, "slots", PropertyValue_FromRecordList(List));
    PropertyRecordList_Destroy(List);
}

/* What a part's meshes reach in its object's space, LOD 0, of the vertices at or above a height (in the object's space too). */
static BoundingBoxDouble GetPartBox(Scene* scene, uint64_t id, size_t part, double fromHeight)
{
    BoundingBoxDouble Box = { .Min = Vector3Double_Create(1.0e30, 1.0e30, 1.0e30), .Max = Vector3Double_Create(-1.0e30, -1.0e30,
        -1.0e30) };
    WorldModelComponent* Component = FindPart(scene, id, part);
    const WorldGeneratedModel* Generated = (Component != NULL) ? WorldModelComponent_GetGeneratedModel(Component) : NULL;
    if (Generated == NULL)
    {
        return Box;
    }
    Vector3Double Place = WorldObject_GetPosition(WorldComponent_GetOwner(&Component->Base));
    for (uint32_t MeshIndex = 0U; MeshIndex < Generated->Lods[0].MeshCount; MeshIndex++)
    {
        const WorldGeneratedMesh* Held = &Generated->Lods[0].Meshes[MeshIndex];
        for (uint32_t Vertex = 0U; Vertex < Held->VertexCount; Vertex++)
        {
            const float* P = &Held->Positions[(size_t)Vertex * 3U];
            Vector3Double Point = Vector3Double_Create((double)P[0] + Place.X, (double)P[1] + Place.Y, (double)P[2] + Place.Z);
            if (Point.Y >= fromHeight)
            {
                Box = BoundingBoxDouble_UnionPoint(Box, Point);
            }
        }
    }
    return Box;
}

/* The runtime child of an object holding a world model's copies; NULL for none. */
static WorldObject* FindCopiesChild(Scene* scene, uint64_t id, const char* worldModel)
{
    WorldObject* Parent = NULL;
    if (!World_TryGetObject(&scene->World, id, &Parent))
    {
        return NULL;
    }
    for (WorldObject* Child = WorldObject_GetFirstChild(Parent); Child != NULL; Child = WorldObject_GetNextSibling(Child))
    {
        WorldModelInstancesComponent* Copies = WorldModelInstancesComponent_FromComponent(WorldObject_GetComponent(Child,
            WorldComponentType_ModelInstances));
        if ((Copies != NULL) && (strcmp((const char*)WorldModelInstancesComponent_GetWorldModel(Copies), worldModel) == 0))
        {
            return Child;
        }
    }
    return NULL;
}

/* The first collider among a path object's children whose slot's world model is @p worldModel. */
static WorldColliderComponent* FindCollider(Scene* scene, uint64_t id, const char* worldModel)
{
    WorldObject* Child = FindCopiesChild(scene, id, worldModel);
    return (Child != NULL) ? WorldColliderComponent_FromComponent(WorldObject_GetComponent(Child,
        WorldComponentType_Collider)) : NULL;
}

/* A world model's copies under an object: their list; NULL with a count of 0 for none. */
static const PropertyInstanceEntry* GetCopies(Scene* scene, uint64_t id, const char* worldModel, size_t* outCount)
{
    *outCount = 0U;
    WorldObject* Child = FindCopiesChild(scene, id, worldModel);
    if (Child == NULL)
    {
        return NULL;
    }
    return WorldModelInstancesComponent_GetInstances(WorldModelInstancesComponent_FromComponent(WorldObject_GetComponent(Child,
        WorldComponentType_ModelInstances)), outCount);
}

/* A point of a copy's mesh in the object's space. */
static Vector3Double ThroughCopy(const PropertyInstanceEntry* copy, Vector3 point)
{
    Matrix4Double Placement = WorldTransform_ComposeSheared(copy->Position, copy->Rotation, copy->Scale, copy->Shear);
    return Matrix4Double_TransformPoint(&Placement, Vector3Double_Create((double)point.x, (double)point.y, (double)point.z));
}

static size_t CountChildren(Scene* scene, uint64_t id)
{
    WorldObject* Parent = NULL;
    if (!World_TryGetObject(&scene->World, id, &Parent))
    {
        return 0U;
    }
    return WorldObject_GetChildCount(Parent);
}

static float RisingHeight(float x, float z)
{
    (void)z;
    return 0.25f * x;
}

static float FlatHeight(float x, float z)
{
    (void)x;
    (void)z;
    return 0.0f;
}

static uint64_t AddTerrain(TestRun* run, Scene* scene, const char* name, TestTerrainHeightFunction height, Vector3 position)
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
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(&scene->World, (const unsigned char*)u8"ground", &Object)))
    {
        return 0U;
    }
    TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, TestWorld_At(position)));
    if (!TEST_CHECK_SUCCESS(run, World_AddComponent(&scene->World, WorldObject_GetId(Object), WorldComponentType_Terrain,
        &Component)))
    {
        return 0U;
    }
    TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetTerrain(WorldTerrainComponent_FromComponent(Component),
        (const unsigned char*)name));
    return WorldObject_GetId(Object);
}


// Tests: copies.
static void APathObjectMakesCopiesUnderItsObject(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 1.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 1.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 5.0f, 0.0f, 5.0f }, Nodes, 2U, "fence", false);
        WorldPathObjects_Update(S.System);
        size_t Panels = 0U;
        const PropertyInstanceEntry* PanelCopies = GetCopies(&S, Id, "tbp:panel", &Panels);
        // Five panels; a child per slot and model: the separators' and the two caps' each hold their own.
        TEST_CHECK(run, (Panels == 5U) && (PanelCopies != NULL));
        TEST_CHECK(run, CountChildren(&S, Id) == 4U);
        for (size_t Index = 0U; (Index < Panels) && (PanelCopies != NULL); Index++)
        {
            Vector3Double Foot = ThroughCopy(&PanelCopies[Index], (Vector3){ 0.0f, 0.0f, 0.0f });
            Vector3Double End = ThroughCopy(&PanelCopies[Index], (Vector3){ 2.0f, 0.0f, 0.0f });
            TEST_CHECK(run, NEAR(Foot.X, 2.0 * (double)Index, PLACE_TOLERANCE) && NEAR(Foot.Y, 1.0, PLACE_TOLERANCE));
            TEST_CHECK(run, NEAR(End.X, 2.0 * (double)(Index + 1U), PLACE_TOLERANCE));
        }
        // The children are runtime objects under the object, placed by it: in the world they stand where the path does.
        WorldObject* Child = FindCopiesChild(&S, Id, "tbp:panel");
        TEST_CHECK(run, (Child != NULL) && !WorldObject_IsPersistent(Child));
        World_UpdateTransforms(&S.World);
        Vector3 Place = TestWorld_ToScene(WorldObject_GetWorldPosition(Child));
        TEST_CHECK(run, NEAR(Place.x, 5.0, PLACE_TOLERANCE) && NEAR(Place.z, 5.0, PLACE_TOLERANCE));
        WorldPathObjectsStats Stats = WorldPathObjects_GetStats(S.System);
        TEST_CHECK(run,
            (Stats.ComponentCount == 1U) && (Stats.PieceCount == 11U) && (Stats.CopyCount == 11U) && (Stats.ChildCount == 4U)
            && (Stats.RebuildCount == 1U) && (Stats.GeneratedModelCount == 0U));
        // Nothing changed: no rebuild.
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).RebuildCount == 1U);
    }
    Scene_Deconstruct(run, &S);
}

static void AChildReadsItsParentsPath(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(4.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Road = AddFence(run, &S, (Vector3){ 1.0f, 0.0f, 2.0f }, Nodes, 2U, "fence", false);
        TEST_CHECK_SUCCESS(run, WorldPathObjectComponent_SetPathObject(GetComponent(&S, Road), NULL));
        WorldObject* Kerb = NULL;
        WorldComponent* Component = NULL;
        TEST_CHECK_SUCCESS(run, World_CreateObject(&S.World, (const unsigned char*)u8"kerb", &Kerb));
        uint64_t KerbId = WorldObject_GetId(Kerb);
        TEST_CHECK_SUCCESS(run, World_SetParent(&S.World, KerbId, Road, false));
        TEST_CHECK_SUCCESS(run, World_AddComponent(&S.World, KerbId, WorldComponentType_PathObject, &Component));
        WorldPathObjectComponent* Own = WorldPathObjectComponent_FromComponent(Component);
        TEST_CHECK_SUCCESS(run, WorldPathObjectComponent_SetPathObject(Own, (const unsigned char*)u8"fence"));
        SetRow(run, &S, KerbId, "overrides_snaps_to_terrain", PropertyValue_FromBool(true));
        SetRow(run, &S, KerbId, "snaps_to_terrain", PropertyValue_FromBool(false));
        WorldPathObjects_Update(S.System);
        size_t Panels = 0U;
        const PropertyInstanceEntry* Copies = GetCopies(&S, KerbId, "tbp:panel", &Panels);
        TEST_CHECK(run, (Panels == 2U) && (Copies != NULL));
        // Its children lie in its space: with no transform of its own, exactly on the parent's path.
        WorldObject* Child = FindCopiesChild(&S, KerbId, "tbp:panel");
        World_UpdateTransforms(&S.World);
        Vector3 Place = TestWorld_ToScene(WorldObject_GetWorldPosition(Child));
        TEST_CHECK(run, NEAR(Place.x, 1.0, PLACE_TOLERANCE) && NEAR(Place.z, 2.0, PLACE_TOLERANCE));
        // Moving it moves all it made, with no rebuild while nothing snaps.
        uint64_t Rebuilds = WorldPathObjects_GetStats(S.System).RebuildCount;
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Kerb, Vector3Double_Create(0.0, 3.0, 0.0)));
        WorldPathObjects_Update(S.System);
        World_UpdateTransforms(&S.World);
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).RebuildCount == Rebuilds);
        Place = TestWorld_ToScene(WorldObject_GetWorldPosition(Child));
        TEST_CHECK(run, NEAR(Place.y, 3.0, PLACE_TOLERANCE));
        // Without a path above it, it holds nothing; with one again, it comes back.
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&S.World, Road, WorldComponentType_Path));
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, CountChildren(&S, KerbId) == 0U);
        Component = NULL;
        TEST_CHECK_SUCCESS(run, World_AddComponent(&S.World, Road, WorldComponentType_Path, &Component));
        TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(WorldPathComponent_FromComponent(Component), Nodes, 2U));
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, CountChildren(&S, KerbId) > 0U);
    }
    Scene_Deconstruct(run, &S);
}


// Tests: live.
static void EveryChangeRebuildsLiveAndOnlyRealOnesAreWritten(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", false);
        WorldPathObjects_Update(S.System);
        WorldModelInstancesComponent* Panels = WorldModelInstancesComponent_FromComponent(WorldObject_GetComponent(
            FindCopiesChild(&S, Id, "tbp:panel"), WorldComponentType_ModelInstances));
        uint32_t Revision = WorldModelInstancesComponent_GetInstancesRevision(Panels);

        // A node set to what it is: a rebuild that writes nothing, so nothing downstream rebuilds.
        WorldPathComponent* Path = GetPath(&S, Id);
        WorldPathObjectsStats Before = WorldPathObjects_GetStats(S.System);
        TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNode(Path, 1U, &Nodes[1]));
        WorldPathObjects_Update(S.System);
        WorldPathObjectsStats After = WorldPathObjects_GetStats(S.System);
        TEST_CHECK(run,
            (After.RebuildCount == (Before.RebuildCount + 1U))
            && (After.UnchangedRebuildCount == (Before.UnchangedRebuildCount + 1U))
            && (After.WriteCount == Before.WriteCount));
        TEST_CHECK(run, WorldModelInstancesComponent_GetInstancesRevision(Panels) == Revision);

        // A node moved: the path follows at once.
        PathNode Longer = MakeNode(20.0, 0.0, 0.0, PathNodeMode_Corner);
        TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNode(Path, 1U, &Longer));
        WorldPathObjects_Update(S.System);
        size_t Count = 0U;
        GetCopies(&S, Id, "tbp:panel", &Count);
        TEST_CHECK(run, Count == 10U);
        TEST_CHECK(run, WorldModelInstancesComponent_GetInstancesRevision(Panels) != Revision);

        // A setting of the placement: fewer pieces, by a set count.
        SetRow(run, &S, Id, "overrides_uses_count", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "uses_count", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "overrides_count", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "count", PropertyValue_FromUInt32(3U));
        WorldPathObjects_Update(S.System);
        GetCopies(&S, Id, "tbp:panel", &Count);
        TEST_CHECK(run, Count == 3U);
    }
    Scene_Deconstruct(run, &S);
}

static void APlacementOverridesItsAssetsSettings(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(11.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", false);
        // Fixed, the panels lie at their lengths, and the separators go: the caps stay.
        SetRow(run, &S, Id, "overrides_fit", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "fit", PropertyValue_FromEnum(PathObjectFit_Fixed));
        SetRow(run, &S, Id, "overrides_has_separator", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "has_separator", PropertyValue_FromBool(false));
        WorldPathObjects_Update(S.System);
        size_t Count = 0U;
        const PropertyInstanceEntry* Copies = GetCopies(&S, Id, "tbp:panel", &Count);
        TEST_CHECK(run, (Count == 5U) && (Copies != NULL) && NEAR(ThroughCopy(&Copies[1], (Vector3){ 0.0f, 0.0f, 0.0f }).X, 2.0,
            PLACE_TOLERANCE));
        TEST_CHECK(run, CountChildren(&S, Id) == 3U); // the separators' child went with its slot
        // A slot's setting: a record of the placement's, the rest still the asset's.
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesSide = true;
        Record.Side = PathObjectSide_Both;
        Record.OverridesSideOffset = true;
        Record.SideOffset = 1.5f;
        Record.OverridesCollision = true;
        Record.Collision = PathObjectCollision_None;
        PropertyRecordList* List = NULL;
        TEST_CHECK_SUCCESS(run, PropertyRecordList_Create(PathObject_GetSlotPropertyTable(), &Record, 1U, &List));
        SetRow(run, &S, Id, "slots", PropertyValue_FromRecordList(List));
        PropertyRecordList_Destroy(List);
        WorldPathObjects_Update(S.System);
        Copies = GetCopies(&S, Id, "tbp:panel", &Count);
        TEST_CHECK(run, Count == 10U);
        // Right of the path along +X (seen from above, +Y up) is +Z... the copies stand on either side, a left one turned round.
        double RightZ = ThroughCopy(&Copies[0], (Vector3){ 0.0f, 0.0f, 0.0f }).Z;
        double LeftZ = ThroughCopy(&Copies[1], (Vector3){ 0.0f, 0.0f, 0.0f }).Z;
        TEST_CHECK(run, NEAR(fabs(RightZ), 1.5, PLACE_TOLERANCE) && NEAR(LeftZ, -RightZ, PLACE_TOLERANCE));
        TEST_CHECK(run, NEAR(ThroughCopy(&Copies[1], (Vector3){ 2.0f, 0.0f, 0.0f }).X, 0.0, PLACE_TOLERANCE));
        // Models of the placement replace only what they name.
        PathObjectModelRecord Own = { .Slot = PathObjectSlot_Middle, .WorldModel = (unsigned char*)u8"tbp:rail", .Weight = 1.0f,
            .Tint = { 10, 20, 30, 255 } };
        TEST_CHECK_SUCCESS(run, PropertyRecordList_Create(PathObject_GetModelPropertyTable(), &Own, 1U, &List));
        SetRow(run, &S, Id, "models", PropertyValue_FromRecordList(List));
        SetRow(run, &S, Id, "overrides_models", PropertyValue_FromBool(true));
        PropertyRecordList_Destroy(List);
        WorldPathObjects_Update(S.System);
        Copies = GetCopies(&S, Id, "tbp:rail", &Count);
        TEST_CHECK(run, (Count == 22U) && (Copies != NULL) && (Copies[0].Tint.g == 20U));
        GetCopies(&S, Id, "tbp:post", &Count);
        TEST_CHECK(run, Count > 0U); // the caps keep the asset's
        TEST_CHECK(run, FindCopiesChild(&S, Id, "tbp:panel") == NULL);
    }
    Scene_Deconstruct(run, &S);
}

static void TheChildrenTakeTheSlotsRenderSettingsAndTheObjectsTint(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", false);
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record._overridesCastsShadows = true;
        Record._castsShadows = false;
        Record._overridesDrawDistance = true;
        Record._drawDistance = 77.0f;
        Record._drawDistanceMultiplier = 2.0f;
        PropertyRecordList* List = NULL;
        TEST_CHECK_SUCCESS(run, PropertyRecordList_Create(PathObject_GetSlotPropertyTable(), &Record, 1U, &List));
        SetRow(run, &S, Id, "slots", PropertyValue_FromRecordList(List));
        PropertyRecordList_Destroy(List);
        WorldObject* Object = NULL;
        TEST_CHECK(run, World_TryGetObject(&S.World, Id, &Object));
        RenderColor Tint = { .Tint = { 200, 100, 50, 255 }, .Brightness = 0.5f, .Opacity = 1.0f };
        TEST_CHECK_SUCCESS(run, WorldObject_SetTint(Object, Tint));
        WorldPathObjects_Update(S.System);
        WorldObject* Child = FindCopiesChild(&S, Id, "tbp:panel");
        WorldModelInstancesComponent* Panels = WorldModelInstancesComponent_FromComponent(WorldObject_GetComponent(Child,
            WorldComponentType_ModelInstances));
        TEST_CHECK(run, WorldModelInstancesComponent_IsOverridden(Panels, WorldModelSetting_CastsShadows)
            && !WorldModelInstancesComponent_CastsShadows(Panels));
        TEST_CHECK(run, WorldModelInstancesComponent_IsOverridden(Panels, WorldModelSetting_DrawDistance)
            && (WorldModelInstancesComponent_GetDrawDistance(Panels) == 77.0f)
            && (WorldModelInstancesComponent_GetDrawDistanceMultiplier(Panels) == 2.0f));
        // What the slot leaves stays the world model's.
        TEST_CHECK(run, !WorldModelInstancesComponent_IsOverridden(Panels, WorldModelSetting_HasOutline));
        TEST_CHECK(run, WorldObject_GetTint(Child).Tint.r == 200U);
        // A model's variant reaches its copies' rows.
        PathObjectModelRecord Model0 = { .Slot = PathObjectSlot_Middle, .WorldModel = (unsigned char*)u8"panel", .Weight = 1.0f,
            .UsesVariant = true,
            .Variant = (unsigned char*)u8"green", .Tint = { 255, 255, 255, 255 } };
        PropertyRecordList* Models = NULL;
        TEST_CHECK_SUCCESS(run, PropertyRecordList_Create(PathObject_GetModelPropertyTable(), &Model0, 1U, &Models));
        SetRow(run, &S, Id, "overrides_models", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "models", PropertyValue_FromRecordList(Models));
        PropertyRecordList_Destroy(Models);
        WorldPathObjects_Update(S.System);
        PropertyValue Variant;
        TEST_CHECK_SUCCESS(run, Properties_Get(WorldModelInstancesComponent_GetPropertyTable(), Panels,
            (const unsigned char*)u8"variant", &Variant));
        TEST_CHECK(run, (Variant.Data.StringValue != NULL) && (strcmp((const char*)Variant.Data.StringValue, "green") == 0));
        SetRow(run, &S, Id, "overrides_models", PropertyValue_FromBool(false));
        WorldPathObjects_Update(S.System);
        Child = FindCopiesChild(&S, Id, "tbp:panel");
        // A change of the tint alone reaches them.
        Tint.Tint.r = 10U;
        TEST_CHECK_SUCCESS(run, WorldObject_SetTint(Object, Tint));
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, WorldObject_GetTint(Child).Tint.r == 10U);
    }
    Scene_Deconstruct(run, &S);
}

/* A loaded asset never changes by itself: a rebuild lets go of what it holds and takes the definitions as they are now. */
static void ARebuildTakesTheDefinitionsAsTheyAreNow(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(20.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "gateposts", false);
        WorldPathObjects_Update(S.System);
        size_t Pieces = WorldPathObjects_GetStats(S.System).PieceCount;
        TEST_CHECK(run, Pieces > 0U);
        uint64_t Rebuilds = WorldPathObjects_GetStats(S.System).RebuildCount;
        // A new gap in the file: nothing happens until asked.
        TEST_CHECK(run, AddPathObjectAsset(run, &S, "{ \"name\": \"gateposts\", \"models\": [ { \"slot\": \"middle\", \"world_model\": "
            "\"gatepost\" } ], \"fit\": \"fixed\", \"uses_gap\": true, \"gap\": 1.0, \"slots\": [ { \"slot\": \"middle\", \"length\": 0 } ] }"));
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).RebuildCount == Rebuilds);
        WorldPathObjects_Rebuild(S.System);
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).RebuildCount == (Rebuilds + 1U));
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).PieceCount > Pieces);
        // The models were let go of too, and their measures with them: a model that comes back measures again, and it all still stands.
        TEST_CHECK(run, CountChildren(&S, Id) > 0U);
        WorldPathObjects_Rebuild(S.System);
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).RebuildCount == (Rebuilds + 2U));
    }
    Scene_Deconstruct(run, &S);
}

static void WhatItMadeFollowsItsComponentAndItsObject(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", false);
        WorldObject* Object = NULL;
        TEST_CHECK(run, World_TryGetObject(&S.World, Id, &Object));
        // A disabled object waits: nothing is made until it is enabled.
        TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(Object, false));
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, (CountChildren(&S, Id) == 0U) && (WorldPathObjects_GetStats(S.System).RebuildCount == 0U));
        TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(Object, true));
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, CountChildren(&S, Id) == 4U);
        // No asset: nothing, and a missing one neither, without trying again every tick.
        TEST_CHECK_SUCCESS(run, WorldPathObjectComponent_SetPathObject(GetComponent(&S, Id), (const unsigned char*)u8"no_such"));
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, CountChildren(&S, Id) == 0U);
        uint64_t Rebuilds = WorldPathObjects_GetStats(S.System).RebuildCount;
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).RebuildCount == Rebuilds);
        TEST_CHECK_SUCCESS(run, WorldPathObjectComponent_SetPathObject(GetComponent(&S, Id), (const unsigned char*)u8"fence"));
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, CountChildren(&S, Id) == 4U);
        // A child removed from outside comes back at the next rebuild.
        uint64_t ChildId = WorldObject_GetId(FindCopiesChild(&S, Id, "tbp:panel"));
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&S.World, ChildId));
        const PathNode Moved = MakeNode(12.0, 0.0, 0.0, PathNodeMode_Corner);
        TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNode(GetPath(&S, Id), 1U, &Moved));
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, FindCopiesChild(&S, Id, "tbp:panel") != NULL);
        // Fences coming and going around this one: a walk along both lists finds each its own, with nothing built again.
        uint64_t Before = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 20.0f }, Nodes, 2U, "fence", false);
        uint64_t Between = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 40.0f }, Nodes, 2U, "fence", false);
        WorldPathObjects_Update(S.System);
        uint64_t RebuildsBefore = WorldPathObjects_GetStats(S.System).RebuildCount;
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&S.World, Before, WorldComponentType_PathObject));
        uint64_t After = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 60.0f }, Nodes, 2U, "fence", false);
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).RebuildCount == (RebuildsBefore + 1U));
        TEST_CHECK(run, (CountChildren(&S, Before) == 0U) && (CountChildren(&S, Between) == 4U) && (CountChildren(&S, After) == 4U) && (CountChildren(&S, Id) == 4U));
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&S.World, Between));
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&S.World, After));
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&S.World, Before));
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).RebuildCount == (RebuildsBefore + 1U));
        // The component removed: its children go, and what they held leaves the stats.
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&S.World, Id, WorldComponentType_PathObject));
        WorldPathObjects_Update(S.System);
        WorldPathObjectsStats Stats = WorldPathObjects_GetStats(S.System);
        TEST_CHECK(run, (CountChildren(&S,
            Id) == 0U) && (Stats.PieceCount == 0U) && (Stats.CopyCount == 0U) && (Stats.ComponentCount == 0U));
        // A replaced component starts afresh; the whole object removed takes its children, with nothing left to do.
        WorldComponent* Again = NULL;
        TEST_CHECK_SUCCESS(run, World_AddComponent(&S.World, Id, WorldComponentType_PathObject, &Again));
        TEST_CHECK_SUCCESS(run, WorldPathObjectComponent_SetPathObject(WorldPathObjectComponent_FromComponent(Again),
            (const unsigned char*)u8"fence"));
        SetRow(run, &S, Id, "overrides_snaps_to_terrain", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "snaps_to_terrain", PropertyValue_FromBool(false));
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, CountChildren(&S, Id) == 4U);
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&S.World, Id));
        WorldPathObjects_Update(S.System);
        Stats = WorldPathObjects_GetStats(S.System);
        TEST_CHECK(run, (Stats.ComponentCount == 0U) && (Stats.PieceCount == 0U));
    }
    Scene_Deconstruct(run, &S);
}

/* A climbing path's pieces stand by the object's +Y: the curve's own up is square to its tangent, and leans. Only a roll leans
 * them. */
static void PiecesStandVerticalOnAClimbingPath(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(20.0, 10.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", false);
        WorldPathObjects_Update(S.System);
        // Panels shear and posts step: a point a metre over a foot lies straight above it.
        const char* const Models[2] = { "tbp:panel", "tbp:post" };
        for (size_t ModelIndex = 0U; ModelIndex < 2U; ModelIndex++)
        {
            size_t Count = 0U;
            const PropertyInstanceEntry* Copies = GetCopies(&S, Id, Models[ModelIndex], &Count);
            TEST_CHECK(run, Count > 0U);
            for (size_t Index = 0U; (Index < Count) && (Copies != NULL); Index++)
            {
                Vector3Double Foot = ThroughCopy(&Copies[Index], (Vector3){ 0.0f, 0.0f, 0.0f });
                Vector3Double Top = ThroughCopy(&Copies[Index], (Vector3){ 0.0f, 1.0f, 0.0f });
                TEST_CHECK(run, NEAR(Top.X - Foot.X, 0.0, PLACE_TOLERANCE) && NEAR(Top.Y - Foot.Y, 1.0, PLACE_TOLERANCE)
                    && NEAR(Top.Z - Foot.Z, 0.0, PLACE_TOLERANCE));
            }
        }
        // A tilted panel leans square to its chord.
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesSlope = true;
        Record.Slope = PathObjectSlope_Tilt;
        PropertyRecordList* List = NULL;
        TEST_CHECK_SUCCESS(run, PropertyRecordList_Create(PathObject_GetSlotPropertyTable(), &Record, 1U, &List));
        SetRow(run, &S, Id, "slots", PropertyValue_FromRecordList(List));
        PropertyRecordList_Destroy(List);
        WorldPathObjects_Update(S.System);
        size_t Count = 0U;
        const PropertyInstanceEntry* Panels = GetCopies(&S, Id, "tbp:panel", &Count);
        TEST_CHECK(run, Count > 0U);
        for (size_t Index = 0U; (Index < Count) && (Panels != NULL); Index++)
        {
            Vector3Double Foot = ThroughCopy(&Panels[Index], (Vector3){ 0.0f, 0.0f, 0.0f });
            Vector3Double Top = ThroughCopy(&Panels[Index], (Vector3){ 0.0f, 1.0f, 0.0f });
            TEST_CHECK(run, NEAR(((Top.X - Foot.X) * 20.0) + ((Top.Y - Foot.Y) * 10.0), 0.0, PLACE_TOLERANCE));
        }
        // A roll leans them to the side and not along the path.
        Nodes[0].Roll = 0.5f;
        Nodes[1].Roll = 0.5f;
        TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(GetPath(&S, Id), Nodes, 2U));
        WorldPathObjects_Update(S.System);
        const PropertyInstanceEntry* Posts = GetCopies(&S, Id, "tbp:post", &Count);
        TEST_CHECK(run, Count > 0U);
        for (size_t Index = 0U; (Index < Count) && (Posts != NULL); Index++)
        {
            Vector3Double Foot = ThroughCopy(&Posts[Index], (Vector3){ 0.0f, 0.0f, 0.0f });
            Vector3Double Top = ThroughCopy(&Posts[Index], (Vector3){ 0.0f, 1.0f, 0.0f });
            TEST_CHECK(run, NEAR(Top.X - Foot.X, 0.0, 0.15) && (fabs(Top.Z - Foot.Z) > 0.3) && NEAR(Vector3Double_Distance(Top,
                Foot), 1.0, PLACE_TOLERANCE));
        }
    }
    Scene_Deconstruct(run, &S);
}

/* A bent piece on a climbing path stands vertical too: over the path's line at a vertex's own X, every vertex of its metre-high
 * box lies between 0 and 1 m, both reached. A leaning piece would pass a metre there. */
static void BentPiecesStandVerticalOnAClimbingPath(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(8.0, 4.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "hedge", false);
        WorldPathObjects_Update(S.System);
        WorldModelComponent* Component = FindPart(&S, Id, 0U);
        TEST_CHECK(run, Component != NULL);
        const WorldGeneratedModel* Generated = (Component != NULL) ? WorldModelComponent_GetGeneratedModel(Component) : NULL;
        TEST_CHECK(run, (Generated != NULL) && (Generated->Lods[0].MeshCount == 1U));
        if ((Generated != NULL) && (Generated->Lods[0].MeshCount == 1U))
        {
            Vector3Double Place = WorldObject_GetPosition(WorldComponent_GetOwner(&Component->Base));
            const WorldGeneratedMesh* Held = &Generated->Lods[0].Meshes[0];
            double Lowest = 1.0e9;
            double Highest = -1.0e9;
            for (uint32_t Vertex = 0U; Vertex < Held->VertexCount; Vertex++)
            {
                const float* P = &Held->Positions[(size_t)Vertex * 3U];
                double Height = ((double)P[1] + Place.Y) - (0.5 * ((double)P[0] + Place.X));
                Lowest = fmin(Lowest, Height);
                Highest = fmax(Highest, Height);
            }
            TEST_CHECK(run, (Held->VertexCount > 0U) && NEAR(Lowest, 0.0, 1.0e-3) && NEAR(Highest, 1.0, 1.0e-3));
        }
        // Tilted, it leans square to the path: a metre along its up is sqrt(1 + 1/4) over the line.
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesSlope = true;
        Record.Slope = PathObjectSlope_Tilt;
        SetSlots(run, &S, Id, &Record, 1U);
        WorldPathObjects_Update(S.System);
        Component = FindPart(&S, Id, 0U);
        Generated = (Component != NULL) ? WorldModelComponent_GetGeneratedModel(Component) : NULL;
        if (TEST_CHECK(run, Generated != NULL))
        {
            Vector3Double Place = WorldObject_GetPosition(WorldComponent_GetOwner(&Component->Base));
            const WorldGeneratedMesh* Held = &Generated->Lods[0].Meshes[0];
            double Highest = -1.0e9;
            for (uint32_t Vertex = 0U; Vertex < Held->VertexCount; Vertex++)
            {
                const float* P = &Held->Positions[(size_t)Vertex * 3U];
                Highest = fmax(Highest, ((double)P[1] + Place.Y) - (0.5 * ((double)P[0] + Place.X)));
            }
            TEST_CHECK(run, NEAR(Highest, sqrt(1.25), 1.0e-3));
        }
    }
    Scene_Deconstruct(run, &S);
}

/* A flip turns a piece end for end, about its up: always, every other one, or by the seed. */
static void AFlipTurnsAPieceEndForEnd(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(20.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", false);
        const PathObjectFlip Flips[4] = { PathObjectFlip_None, PathObjectFlip_Always, PathObjectFlip_Alternating,
            PathObjectFlip_Random };
        for (size_t Flip = 0U; Flip < 4U; Flip++)
        {
            PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
            Record.OverridesFlip = true;
            Record.Flip = Flips[Flip];
            SetSlots(run, &S, Id, &Record, 1U);
            WorldPathObjects_Update(S.System);
            size_t Count = 0U;
            const PropertyInstanceEntry* Copies = GetCopies(&S, Id, "tbp:panel", &Count);
            TEST_CHECK(run, Count == 10U);
            size_t Turned = 0U;
            for (size_t Index = 0U; (Index < Count) && (Copies != NULL); Index++)
            {
                double Along = ThroughCopy(&Copies[Index], (Vector3){ 1.0f, 0.0f, 0.0f }).X - ThroughCopy(&Copies[Index],
                    (Vector3){ 0.0f, 0.0f, 0.0f }).X;
                bool IsTurned = Along < 0.0;
                Turned += IsTurned ? 1U : 0U;
                if (Flips[Flip] == PathObjectFlip_Alternating)
                {
                    TEST_CHECK(run, IsTurned == ((Index % 2U) == 1U));
                }
            }
            switch (Flips[Flip])
            {
                case PathObjectFlip_None: TEST_CHECK(run, Turned == 0U); break;
                case PathObjectFlip_Always: TEST_CHECK(run, Turned == Count); break;
                case PathObjectFlip_Alternating: TEST_CHECK(run, Turned == 5U); break;
                case PathObjectFlip_Random: TEST_CHECK(run, (Turned > 0U) && (Turned < Count)); break;
            }
        }
    }
    Scene_Deconstruct(run, &S);
}

/* Sides: to the path's right, its left, both, every other one; off by the setting times the node's width. */
static void ASideStandsOffThePathByTheNodesWidth(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(20.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", false);
        const PathObjectSide Sides[4] = { PathObjectSide_Right, PathObjectSide_Left, PathObjectSide_Both,
            PathObjectSide_Alternating };
        for (size_t Side = 0U; Side < 4U; Side++)
        {
            PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
            Record.OverridesSide = true;
            Record.Side = Sides[Side];
            Record.OverridesSideOffset = true;
            Record.SideOffset = 1.5f;
            SetSlots(run, &S, Id, &Record, 1U);
            WorldPathObjects_Update(S.System);
            size_t Count = 0U;
            const PropertyInstanceEntry* Copies = GetCopies(&S, Id, "tbp:panel", &Count);
            TEST_CHECK(run, Count == ((Sides[Side] == PathObjectSide_Both) ? 20U : 10U));
            size_t Right = 0U;
            size_t Left = 0U;
            for (size_t Index = 0U; (Index < Count) && (Copies != NULL); Index++)
            {
                double Z = ThroughCopy(&Copies[Index], (Vector3){ 1.0f, 0.0f, 0.0f }).Z;
                TEST_CHECK(run, NEAR(fabs(Z), 1.5, PLACE_TOLERANCE));
                Right += (Z > 0.0) ? 1U : 0U;
                Left += (Z < 0.0) ? 1U : 0U;
                // A left piece is its right twin turned end for end, so the same face looks away from the path.
                double SideOfItsRight = ThroughCopy(&Copies[Index], (Vector3){ 1.0f, 0.0f, 1.0f }).Z - Z;
                TEST_CHECK(run, (Z > 0.0) == (SideOfItsRight > 0.0));
            }
            switch (Sides[Side])
            {
                case PathObjectSide_Right: TEST_CHECK(run, Left == 0U); break;
                case PathObjectSide_Left: TEST_CHECK(run, Right == 0U); break;
                case PathObjectSide_Both:
                case PathObjectSide_Alternating: TEST_CHECK(run, Right == Left); break;
                case PathObjectSide_OnPath: break;
            }
        }
        // A wider node: farther off, and a bent piece wider and a node's scale taller.
        Nodes[0].Width = 2.0f;
        Nodes[1].Width = 2.0f;
        TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(GetPath(&S, Id), Nodes, 2U));
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesSide = true;
        Record.Side = PathObjectSide_Right;
        Record.OverridesSideOffset = true;
        Record.SideOffset = 1.5f;
        SetSlots(run, &S, Id, &Record, 1U);
        WorldPathObjects_Update(S.System);
        size_t Count = 0U;
        const PropertyInstanceEntry* Copies = GetCopies(&S, Id, "tbp:panel", &Count);
        TEST_CHECK(run, (Count == 10U) && NEAR(ThroughCopy(&Copies[0], (Vector3){ 1.0f, 0.0f, 0.0f }).Z, 3.0, PLACE_TOLERANCE));
    }
    Scene_Deconstruct(run, &S);
}

/* A joint stands centred on its node, along the way it arrives. */
static void AJointStandsCentredOnItsNode(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[3] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner),
            MakeNode(10.0, 0.0, 10.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 3U, "fence", false);
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Separator);
        Record.OverridesLength = true;
        Record.Length = 1.0f;
        SetSlots(run, &S, Id, &Record, 1U);
        SetRow(run, &S, Id, "overrides_separator_every", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "separator_every", PropertyValue_FromUInt32(1000U));
        WorldPathObjects_Update(S.System);
        // The posts' mesh is centred on X = 0 and its body on its own middle: a post's origin is the piece's middle. Each slot's
        // posts lie on a child of their own.
        WorldObject* Parent = NULL;
        TEST_CHECK(run, World_TryGetObject(&S.World, Id, &Parent));
        bool IsOnNode = false;
        for (WorldObject* Child = WorldObject_GetFirstChild(Parent); Child != NULL; Child = WorldObject_GetNextSibling(Child))
        {
            WorldModelInstancesComponent* Copies = WorldModelInstancesComponent_FromComponent(WorldObject_GetComponent(Child,
                WorldComponentType_ModelInstances));
            size_t Count = 0U;
            const PropertyInstanceEntry* Posts = (Copies != NULL) ? WorldModelInstancesComponent_GetInstances(Copies,
                &Count) : NULL;
            for (size_t Index = 0U; Index < Count; Index++)
            {
                Vector3Double Middle = ThroughCopy(&Posts[Index], (Vector3){ 0.0f, 0.0f, 0.0f });
                IsOnNode = IsOnNode || (NEAR(Middle.X, 10.0, PLACE_TOLERANCE) && NEAR(Middle.Z, 0.0, PLACE_TOLERANCE));
            }
        }
        TEST_CHECK(run, IsOnNode);
    }
    Scene_Deconstruct(run, &S);
}

/* A model's length is its size under its import transform. */
static void APieceIsAsLongAsItsImportTransformMakesIt(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(12.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "long_panels", false);
        WorldPathObjects_Update(S.System);
        size_t Count = 0U;
        GetCopies(&S, Id, "tbp:long_panel", &Count);
        TEST_CHECK(run, Count == 3U);
    }
    Scene_Deconstruct(run, &S);
}

/* A part takes the object's tint, as copies do. */
static void APartTakesTheObjectsTint(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(8.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "hedge", false);
        WorldObject* Object = NULL;
        TEST_CHECK(run, World_TryGetObject(&S.World, Id, &Object));
        RenderColor Tint = { .Tint = { 200, 100, 50, 255 }, .Brightness = 0.5f, .Opacity = 1.0f };
        TEST_CHECK_SUCCESS(run, WorldObject_SetTint(Object, Tint));
        WorldPathObjects_Update(S.System);
        WorldModelComponent* Part = FindPart(&S, Id, 0U);
        TEST_CHECK(run, (Part != NULL) && (WorldObject_GetTint(WorldComponent_GetOwner(&Part->Base)).Tint.r == 200U));
    }
    Scene_Deconstruct(run, &S);
}

/* A generated model's comparison sees each of its arrays and its material slots. */
static WorldGeneratedMesh MakeGeneratedMesh(int32_t slot, float value, bool hasOptionals)
{
    const uint32_t VertexCount = 3U;
    WorldGeneratedMesh Made = { .MaterialSlot = slot, .VertexCount = VertexCount };
    Made.Positions = Memory_Allocate(VertexCount * 3U * sizeof(float));
    Made.Normals = Memory_Allocate(VertexCount * 3U * sizeof(float));
    for (uint32_t Index = 0U; Index < (VertexCount * 3U); Index++)
    {
        Made.Positions[Index] = value + (float)Index;
        Made.Normals[Index] = (float)Index;
    }
    if (hasOptionals)
    {
        Made.Tangents = Memory_Allocate(VertexCount * 4U * sizeof(float));
        Memory_Zero(Made.Tangents, VertexCount * 4U * sizeof(float));
        Made.Texcoords = Memory_Allocate(VertexCount * 2U * sizeof(float));
        Memory_Zero(Made.Texcoords, VertexCount * 2U * sizeof(float));
    }
    return Made;
}

static WorldGeneratedModel* MakeGeneratedModel(int32_t slot, float value, bool hasOptionals)
{
    WorldGeneratedModel* Made = WorldGeneratedModel_Create(1U);
    WorldGeneratedMesh Mesh0 = MakeGeneratedMesh(slot, value, hasOptionals);
    (void)WorldGeneratedModel_AddMesh(Made, 0U, &Mesh0);
    return Made;
}

static void AGeneratedModelComparesWhatItHolds(TestRun* run)
{
    WorldGeneratedModel* Base = MakeGeneratedModel(1, 0.0f, true);
    WorldGeneratedModel* Same = MakeGeneratedModel(1, 0.0f, true);
    TEST_CHECK(run, WorldGeneratedModel_Equals(Base, Same) && WorldGeneratedModel_Equals(NULL,
        NULL) && !WorldGeneratedModel_Equals(Base, NULL));
    WorldGeneratedModel* OtherSlot = MakeGeneratedModel(2, 0.0f, true);
    WorldGeneratedModel* OtherPlace = MakeGeneratedModel(1, 5.0f, true);
    WorldGeneratedModel* OtherArrays = MakeGeneratedModel(1, 0.0f, false);
    WorldGeneratedModel* OtherNormal = MakeGeneratedModel(1, 0.0f, true);
    OtherNormal->Lods[0].Meshes[0].Normals[4] = 99.0f;
    WorldGeneratedModel* OtherTexcoord = MakeGeneratedModel(1, 0.0f, true);
    OtherTexcoord->Lods[0].Meshes[0].Texcoords[1] = 99.0f;
    TEST_CHECK(run, !WorldGeneratedModel_Equals(Base, OtherSlot) && !WorldGeneratedModel_Equals(Base, OtherPlace)
        && !WorldGeneratedModel_Equals(Base, OtherArrays) && !WorldGeneratedModel_Equals(Base, OtherNormal)
        && !WorldGeneratedModel_Equals(Base, OtherTexcoord));
    WorldGeneratedModel_Destroy(Base);
    WorldGeneratedModel_Destroy(Same);
    WorldGeneratedModel_Destroy(OtherSlot);
    WorldGeneratedModel_Destroy(OtherPlace);
    WorldGeneratedModel_Destroy(OtherArrays);
    WorldGeneratedModel_Destroy(OtherNormal);
    WorldGeneratedModel_Destroy(OtherTexcoord);
}

/* Its bounds are its finest LOD's that holds anything. */
static void AGeneratedModelsBoundsAreItsFinestLods(TestRun* run)
{
    WorldGeneratedModel* Made = WorldGeneratedModel_Create(2U);
    WorldGeneratedMesh Coarse = MakeGeneratedMesh(0, 100.0f, false);
    (void)WorldGeneratedModel_AddMesh(Made, 1U, &Coarse);
    WorldGeneratedModel_UpdateBounds(Made);
    TEST_CHECK(run, NEAR(Made->Box.min.x, 100.0, 1.0e-3));
    WorldGeneratedMesh Fine = MakeGeneratedMesh(0, 0.0f, false);
    (void)WorldGeneratedModel_AddMesh(Made, 0U, &Fine);
    WorldGeneratedModel_UpdateBounds(Made);
    TEST_CHECK(run, NEAR(Made->Box.min.x, 0.0, 1.0e-3) && (Made->Box.max.x < 50.0f) && (Made->Radius > 0.0f));
    WorldGeneratedModel_Destroy(Made);
}

/* A bent piece: a node's width and scale reach its mesh; its own length is its slices' business. */
static void ABentPieceTakesANodesWidthAndScale(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(8.0, 0.0, 0.0, PathNodeMode_Corner) };
        Nodes[0].Width = 2.0f;
        Nodes[1].Width = 2.0f;
        Nodes[0].Scale = 2.0f;
        Nodes[1].Scale = 2.0f;
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "hedge", false);
        WorldPathObjects_Update(S.System);
        BoundingBoxDouble Box = GetPartBox(&S, Id, 0U, -1.0e30);
        // The rail is a metre high and 0.2 wide: scale 2 makes it 2 high and, with width 2, 0.8 wide.
        TEST_CHECK(run, NEAR(Box.Max.Y - Box.Min.Y, 2.0, PLACE_TOLERANCE) && NEAR(Box.Max.Z - Box.Min.Z, 0.8, PLACE_TOLERANCE));
    }
    Scene_Deconstruct(run, &S);
}

/* A turned bent piece shows its other end first, and every triangle still faces out. */
static void ATurnedBentPieceShowsItsOtherEndFirst(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(2.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "wedges", false);
        WorldPathObjects_Update(S.System);
        BoundingBoxDouble High = GetPartBox(&S, Id, 0U, 1.5);
        TEST_CHECK(run, NEAR(High.Min.X, 1.0, PLACE_TOLERANCE) && NEAR(High.Max.X, 2.0, PLACE_TOLERANCE));
        // The same piece and its material slots, its tangents along the path.
        WorldModelComponent* Part = FindPart(&S, Id, 0U);
        const WorldGeneratedModel* Generated = WorldModelComponent_GetGeneratedModel(Part);
        TEST_CHECK(run, (Generated->Lods[0].MeshCount == 2U) && (Generated->Lods[0].Meshes[0].MaterialSlot == 0)
            && (Generated->Lods[0].Meshes[1].MaterialSlot == 1) && (Generated->Lods[0].Meshes[0].Tangents != NULL)
            && NEAR(Generated->Lods[0].Meshes[0].Tangents[0], 1.0, 1.0e-4));
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesFlip = true;
        Record.Flip = PathObjectFlip_Always;
        SetSlots(run, &S, Id, &Record, 1U);
        WorldPathObjects_Update(S.System);
        High = GetPartBox(&S, Id, 0U, 1.5);
        TEST_CHECK(run, NEAR(High.Min.X, 0.0, PLACE_TOLERANCE) && NEAR(High.Max.X, 1.0, PLACE_TOLERANCE));
        // Turned, not mirrored: each closed box keeps its volume's sign, its triangles still facing out.
        Part = FindPart(&S, Id, 0U);
        Generated = WorldModelComponent_GetGeneratedModel(Part);
        for (uint32_t MeshIndex = 0U; MeshIndex < Generated->Lods[0].MeshCount; MeshIndex++)
        {
            const WorldGeneratedMesh* Held = &Generated->Lods[0].Meshes[MeshIndex];
            double Volume = 0.0;
            for (uint32_t Vertex = 0U; (Vertex + 2U) < Held->VertexCount; Vertex += 3U)
            {
                const float* A = &Held->Positions[(size_t)Vertex * 3U];
                Vector3 First = { A[0], A[1], A[2] };
                Vector3 Second = { A[3], A[4], A[5] };
                Vector3 Third = { A[6], A[7], A[8] };
                Volume += (double)Vector3DotProduct(First, Vector3CrossProduct(Second, Third)) / 6.0;
            }
            TEST_CHECK(run, NEAR(Volume, (MeshIndex == 0U) ? 0.2 : 0.4, 1.0e-3));
        }
        // On the left, a flipped piece is turned twice: as it was.
        Record.OverridesSide = true;
        Record.Side = PathObjectSide_Left;
        SetSlots(run, &S, Id, &Record, 1U);
        WorldPathObjects_Update(S.System);
        High = GetPartBox(&S, Id, 0U, 1.5);
        TEST_CHECK(run, NEAR(High.Min.X, 1.0, PLACE_TOLERANCE) && NEAR(High.Max.X, 2.0, PLACE_TOLERANCE));
    }
    Scene_Deconstruct(run, &S);
}

/* A turned bent piece follows a curve as any other: every vertex stays within its half width of the path, in plan. */
static void ATurnedBentPieceStillFollowsTheCurve(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        PathNode Nodes[9];
        for (size_t Index = 0U; Index < 9U; Index++)
        {
            double Angle = ((double)Index / 8.0) * 1.5707963267948966;
            Nodes[Index] = MakeNode(20.0 * cos(Angle), 0.0, 20.0 * sin(Angle), PathNodeMode_Smooth);
        }
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 9U, "hedge", false);
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesFlip = true;
        Record.Flip = PathObjectFlip_Always;
        SetSlots(run, &S, Id, &Record, 1U);
        WorldPathObjects_Update(S.System);
        size_t Checked = 0U;
        double Worst = 0.0;
        for (size_t Part = 0U; Part < 4U; Part++)
        {
            WorldModelComponent* Component = FindPart(&S, Id, Part);
            const WorldGeneratedModel* Generated = (Component != NULL) ? WorldModelComponent_GetGeneratedModel(Component) : NULL;
            if (Generated == NULL)
            {
                continue;
            }
            Vector3Double Place = WorldObject_GetPosition(WorldComponent_GetOwner(&Component->Base));
            const WorldGeneratedMesh* Held = &Generated->Lods[0].Meshes[0];
            for (uint32_t Vertex = 0U; Vertex < Held->VertexCount; Vertex++)
            {
                const float* P = &Held->Positions[(size_t)Vertex * 3U];
                PathCurvePoint Nearest;
                double Separation = 1.0e9;
                TEST_CHECK(run, PathCurve_FindNearest(WorldPathComponent_GetCurve(GetPath(&S, Id)),
                    Vector3Double_Create((double)P[0] + Place.X, 0.0,
                    (double)P[2] + Place.Z), true, &Nearest, &Separation));
                Worst = fmax(Worst, Separation);
                Checked++;
            }
        }
        TEST_CHECK(run, (Checked > 100U) && (Worst < 0.12));
    }
    Scene_Deconstruct(run, &S);
}

/* An entry depth reaches past a bent piece's end straight on, stretched with the piece. */
static void AnEntryDepthStretchesWithABentPiece(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(3.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "hedge", false);
        SetRow(run, &S, Id, "overrides_uses_count", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "uses_count", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "overrides_count", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "count", PropertyValue_FromUInt32(1U));
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesEntryDepthEnd = true;
        Record.EntryDepthEnd = 0.25f;
        SetSlots(run, &S, Id, &Record, 1U);
        WorldPathObjects_Update(S.System);
        // The rail's body is 0.75 m, stretched to 3 m: four times, and what lies past its end too.
        BoundingBoxDouble Box = GetPartBox(&S, Id, 0U, -1.0e30);
        TEST_CHECK(run, NEAR(Box.Min.X, 0.0, PLACE_TOLERANCE) && NEAR(Box.Max.X, 4.0, PLACE_TOLERANCE));
    }
    Scene_Deconstruct(run, &S);
}

/* A bent run over a corner mitres: its two sides meet on the corner's bisector. */
static void ABentPieceMitresOverACorner(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[3] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner),
            MakeNode(10.0, 0.0, 10.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 3U, "hedge", false);
        SetRow(run, &S, Id, "overrides_fits_each_section", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "fits_each_section", PropertyValue_FromBool(false));
        WorldPathObjects_Update(S.System);
        // The outer corner lies at (10.1, -0.1), the inner at (9.9, 0.1): at the top and at the foot.
        const double Wanted[2][2] = { { 10.1, -0.1 }, { 9.9, 0.1 } };
        bool IsFound[2] = { false, false };
        WorldObject* Parent = NULL;
        TEST_CHECK(run, World_TryGetObject(&S.World, Id, &Parent));
        for (size_t Part = 0U; Part < 4U; Part++)
        {
            WorldModelComponent* Component = FindPart(&S, Id, Part);
            const WorldGeneratedModel* Generated = (Component != NULL) ? WorldModelComponent_GetGeneratedModel(Component) : NULL;
            if (Generated == NULL)
            {
                continue;
            }
            Vector3Double Place = WorldObject_GetPosition(WorldComponent_GetOwner(&Component->Base));
            const WorldGeneratedMesh* Held = &Generated->Lods[0].Meshes[0];
            for (uint32_t Vertex = 0U; Vertex < Held->VertexCount; Vertex++)
            {
                const float* P = &Held->Positions[(size_t)Vertex * 3U];
                for (size_t Corner = 0U; Corner < 2U; Corner++)
                {
                    IsFound[Corner] = IsFound[Corner] || (NEAR((double)P[0] + Place.X, Wanted[Corner][0], 1.0e-3)
                        && NEAR((double)P[2] + Place.Z, Wanted[Corner][1], 1.0e-3));
                }
            }
        }
        TEST_CHECK(run, IsFound[0] && IsFound[1]);
    }
    Scene_Deconstruct(run, &S);
}

/* On a slope, a tilted bent piece leans square to the ground it stands on, not to the flat curve it snapped from. */
static void ATiltedBentPieceLeansWithTheGround(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        AddTerrain(run, &S, "slope", RisingHeight, (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldCollision_Sync(S.Collision);
        const PathNode Nodes[2] = { MakeNode(4.0, 0.0, 10.0, PathNodeMode_Corner), MakeNode(16.0, 0.0, 10.0,
            PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "hedge", true);
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesSlope = true;
        Record.Slope = PathObjectSlope_Tilt;
        SetSlots(run, &S, Id, &Record, 1U);
        WorldPathObjects_Update(S.System);
        // Over the slope's line (a quarter per metre), a metre square to it is sqrt(1 + 1/16) straight up.
        double Highest = -1.0e30;
        for (size_t Part = 0U; Part < 2U; Part++)
        {
            WorldModelComponent* Component = FindPart(&S, Id, Part);
            const WorldGeneratedModel* Generated = (Component != NULL) ? WorldModelComponent_GetGeneratedModel(Component) : NULL;
            if (Generated == NULL)
            {
                continue;
            }
            Vector3Double Place = WorldObject_GetPosition(WorldComponent_GetOwner(&Component->Base));
            const WorldGeneratedMesh* Held = &Generated->Lods[0].Meshes[0];
            for (uint32_t Vertex = 0U; Vertex < Held->VertexCount; Vertex++)
            {
                const float* P = &Held->Positions[(size_t)Vertex * 3U];
                Highest = fmax(Highest, ((double)P[1] + Place.Y) - (0.25 * ((double)P[0] + Place.X)));
            }
        }
        TEST_CHECK(run, NEAR(Highest, sqrt(1.0 + (1.0 / 16.0)), 1.0e-2));
    }
    Scene_Deconstruct(run, &S);
}

/* A bent piece longer than a part lies in several, each holding only its own stretch. */
static void ALongBentPieceLiesInSeveralParts(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(100.0, 0.0, 0.0,
            PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "hedge", false);
        SetRow(run, &S, Id, "overrides_uses_count", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "uses_count", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "overrides_count", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "count", PropertyValue_FromUInt32(1U));
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        SetSlots(run, &S, Id, &Record, 1U);
        WorldPathObjects_Update(S.System);
        WorldPathObjectsStats Stats = WorldPathObjects_GetStats(S.System);
        TEST_CHECK(run, (Stats.PieceCount == 1U) && (Stats.GeneratedModelCount == 4U));
        double Reach = 0.0;
        for (size_t Part = 0U; Part < Stats.GeneratedModelCount; Part++)
        {
            BoundingBoxDouble Box = GetPartBox(&S, Id, Part, -1.0e30);
            TEST_CHECK(run, ((Box.Max.X - Box.Min.X) > 20.0) && ((Box.Max.X - Box.Min.X) < 33.0));
            TEST_CHECK(run, NEAR(Box.Min.X, Reach, 1.0e-2));
            Reach = Box.Max.X;
        }
        TEST_CHECK(run, NEAR(Reach, 100.0, 1.0e-2));
    }
    Scene_Deconstruct(run, &S);
}

/* A cut piece keeps the part of its mesh on the side away from the cut, at its own size. */
static void ACutPieceKeepsItsOwnPartOfTheMesh(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(3.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "wedges", false);
        SetRow(run, &S, Id, "overrides_fit", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "fit", PropertyValue_FromEnum(PathObjectFit_ResizeEnds));
        SetRow(run, &S, Id, "overrides_resize_method", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "resize_method", PropertyValue_FromEnum(PathObjectResizeMethod_Cut));
        WorldPathObjects_Update(S.System);
        // Two wedges of 2 m cut to 1.5 m: the first keeps mesh X 0.5 to 2 on the path's 0 to 1.5 (its high half from 0.5), the
        // second X 0 to 1.5 on 1.5 to 3 (its high half from 2.5).
        BoundingBoxDouble High = GetPartBox(&S, Id, 0U, 1.5);
        TEST_CHECK(run, NEAR(High.Min.X, 0.5, PLACE_TOLERANCE) && NEAR(High.Max.X, 3.0, PLACE_TOLERANCE));
        // Nothing of the high half lies between its two stretches.
        WorldModelComponent* Part = FindPart(&S, Id, 0U);
        const WorldGeneratedModel* Generated = WorldModelComponent_GetGeneratedModel(Part);
        Vector3Double Place = WorldObject_GetPosition(WorldComponent_GetOwner(&Part->Base));
        size_t Between = 0U;
        for (uint32_t MeshIndex = 0U; MeshIndex < Generated->Lods[0].MeshCount; MeshIndex++)
        {
            const WorldGeneratedMesh* Held = &Generated->Lods[0].Meshes[MeshIndex];
            for (uint32_t Vertex = 0U; Vertex < Held->VertexCount; Vertex++)
            {
                const float* P = &Held->Positions[(size_t)Vertex * 3U];
                double X = (double)P[0] + Place.X;
                Between += (((double)P[1] + Place.Y) > 1.5) && (X > 1.51) && (X < 2.49) ? 1U : 0U;
            }
        }
        TEST_CHECK(run, Between == 0U);
    }
    Scene_Deconstruct(run, &S);
}

/* A missing asset is not tried again until its name changes, whatever else changes. */
static void AMissingAssetWaitsForANewName(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "late", false);
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, CountChildren(&S, Id) == 0U);
        TEST_CHECK(run, AddPathObjectAsset(run, &S,
            "{ \"name\": \"late\", \"models\": [ { \"slot\": \"middle\", \"world_model\": \"panel\" } ] }"));
        SetRow(run, &S, Id, "overrides_seed", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "seed", PropertyValue_FromUInt32(5U));
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, CountChildren(&S, Id) == 0U);
        TEST_CHECK_SUCCESS(run, WorldPathObjectComponent_SetPathObject(GetComponent(&S, Id), (const unsigned char*)u8"fence"));
        WorldPathObjects_Update(S.System);
        TEST_CHECK_SUCCESS(run, WorldPathObjectComponent_SetPathObject(GetComponent(&S, Id), (const unsigned char*)u8"late"));
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, CountChildren(&S, Id) == 1U);
    }
    Scene_Deconstruct(run, &S);
}

/* The world's budget: what comes later gets what is left, a rebuild gets its own pieces back, and past it nothing is made. */
static void TheWorldsPiecesHaveABudget(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        WorldPathObjectsLimits Limits = WorldPathObjectsLimits_CreateDefault();
        Limits.MaxPieces = 12U;
        WorldPathObjects_SetLimits(S.System, &Limits);
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t First = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", false);
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).PieceCount == 11U);
        // Rebuilt, it holds as much as before: its own pieces do not count against it.
        Limits.MaxPieces = 11U;
        WorldPathObjects_SetLimits(S.System, &Limits);
        const PathNode Moved = MakeNode(10.5, 0.0, 0.0, PathNodeMode_Corner);
        TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNode(GetPath(&S, First), 1U, &Moved));
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).PieceCount == 11U);
        // The next one gets what is left.
        Limits.MaxPieces = 12U;
        WorldPathObjects_SetLimits(S.System, &Limits);
        uint64_t Second = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 4.0f }, Nodes, 2U, "fence", false);
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, (WorldPathObjects_GetStats(S.System).PieceCount == 12U) && (CountChildren(&S, Second) == 1U));
        // And one past it nothing.
        uint64_t Third = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 8.0f }, Nodes, 2U, "fence", false);
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, (WorldPathObjects_GetStats(S.System).PieceCount == 12U) && (CountChildren(&S, Third) == 0U));
    }
    Scene_Deconstruct(run, &S);
}

/* A part of a path object by its world model's name; NULL for none. */
static WorldModelComponent* FindPartOf(Scene* scene, uint64_t id, const char* worldModel)
{
    for (size_t Index = 0U; ; Index++)
    {
        WorldModelComponent* Part = FindPart(scene, id, Index);
        if ((Part == NULL) || (strcmp((const char*)WorldModelComponent_GetWorldModel(Part), worldModel) == 0))
        {
            return Part;
        }
    }
}

/* Two slots of one rebuild, each by its own spacing; and a path object laid after another samples its own path. */
static void SamplesAreNotSharedBeyondTheirSectionAndSpacing(TestRun* run)
{
    PathNode Curved[9];
    for (size_t Index = 0U; Index < 9U; Index++)
    {
        double Angle = ((double)Index / 8.0) * 1.5707963267948966;
        Curved[Index] = MakeNode(6.0 * cos(Angle), 0.0, 6.0 * sin(Angle), PathNodeMode_Smooth);
    }
    const PathNode Straight[2] = { MakeNode(0.0, 0.0, 20.0, PathNodeMode_Corner), MakeNode(9.42, 0.0, 20.0,
        PathNodeMode_Corner) };
    uint32_t Alone = 0U;
    for (int Round = 0; Round < 2; Round++)
    {
        Scene S;
        if (Scene_Construct(run, &S))
        {
            if (Round == 1)
            {
                AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Straight, 2U, "hedge", false);
            }
            uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Curved, 9U, "hedge", false);
            WorldPathObjects_Update(S.System);
            WorldModelComponent* Part = FindPart(&S, Id, 0U);
            const WorldGeneratedModel* Generated = (Part != NULL) ? WorldModelComponent_GetGeneratedModel(Part) : NULL;
            uint32_t Vertices = ((Generated != NULL)
                && (Generated->Lods[0].MeshCount > 0U)) ? Generated->Lods[0].Meshes[0].VertexCount : 0U;
            TEST_CHECK(run, Vertices > 0U);
            if (Round == 0)
            {
                Alone = Vertices;
            }
            else
            {
                TEST_CHECK(run, Vertices == Alone);
            }
        }
        Scene_Deconstruct(run, &S);
    }
    // A cap and a middle on one straight section: each piece has the slabs its own spacing gives.
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Line[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Line, 2U, "hedge", false);
        WorldPathObjects_Update(S.System);
        uint32_t PerRail = WorldModelComponent_GetGeneratedModel(FindPart(&S, Id, 0U))->Lods[0].Meshes[0].VertexCount / 10U;
        SetRow(run, &S, Id, "overrides_has_start_cap", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "has_start_cap", PropertyValue_FromBool(true));
        PathObjectModelRecord Cap = { .Slot = PathObjectSlot_StartCap, .WorldModel = (unsigned char*)u8"panel", .Weight = 1.0f,
            .Tint = { 255, 255, 255, 255 } };
        PropertyRecordList* Models = NULL;
        TEST_CHECK_SUCCESS(run, PropertyRecordList_Create(PathObject_GetModelPropertyTable(), &Cap, 1U, &Models));
        SetRow(run, &S, Id, "overrides_models", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "models", PropertyValue_FromRecordList(Models));
        PropertyRecordList_Destroy(Models);
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_StartCap);
        Record.OverridesCurve = true;
        Record.Curve = PathObjectCurve_Bent;
        Record.OverridesSliceSpacing = true;
        Record.SliceSpacing = 0.05f;
        SetSlots(run, &S, Id, &Record, 1U);
        WorldPathObjects_Update(S.System);
        WorldModelComponent* Middle = FindPartOf(&S, Id, "tbp:rail");
        TEST_CHECK(run, (Middle != NULL) && (FindPartOf(&S, Id, "tbp:panel") != NULL));
        if (Middle != NULL)
        {
            uint32_t Vertices = WorldModelComponent_GetGeneratedModel(Middle)->Lods[0].Meshes[0].VertexCount;
            TEST_CHECK(run, Vertices == (8U * PerRail));
        }
    }
    Scene_Deconstruct(run, &S);
}

/* The world's vertices and placed shapes have budgets too, shared the same way. */
static void TheWorldsVerticesAndShapesHaveBudgets(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner) };
        // Unlimited, for what one of each holds.
        uint64_t Hedge = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "hedge", false);
        uint64_t Fence = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 4.0f }, Nodes, 2U, "fence", false);
        WorldPathObjects_Update(S.System);
        WorldPathObjectsStats Whole = WorldPathObjects_GetStats(S.System);
        TEST_CHECK(run, (Whole.GeneratedTriangleCount > 0U) && (Whole.PlacedShapeCount == 6U));
        // Rebuilt under limits smaller than that: less comes out, and not past them.
        WorldPathObjectsLimits Limits = WorldPathObjectsLimits_CreateDefault();
        Limits.MaxVertices = 300U;
        Limits.MaxPlacedShapes = 4U;
        WorldPathObjects_SetLimits(S.System, &Limits);
        SetRow(run, &S, Hedge, "overrides_seed", PropertyValue_FromBool(true));
        SetRow(run, &S, Hedge, "seed", PropertyValue_FromUInt32(5U));
        SetRow(run, &S, Fence, "overrides_seed", PropertyValue_FromBool(true));
        SetRow(run, &S, Fence, "seed", PropertyValue_FromUInt32(5U));
        WorldPathObjects_Update(S.System);
        WorldPathObjectsStats Less = WorldPathObjects_GetStats(S.System);
        TEST_CHECK(run,
            ((Less.GeneratedTriangleCount * 3U) <= 300U) && (Less.GeneratedTriangleCount > 0U) && (Less.PlacedShapeCount == 4U));
        // Rebuilt again it gets its own back, each time.
        for (uint32_t Seed = 10U; Seed < 13U; Seed++)
        {
            SetRow(run, &S, Hedge, "seed", PropertyValue_FromUInt32(Seed));
            WorldPathObjects_Update(S.System);
            TEST_CHECK(run, WorldPathObjects_GetStats(S.System).GeneratedTriangleCount == Less.GeneratedTriangleCount);
        }
        // Another hedge gets what is left of the world's, so together they keep to it.
        uint64_t Second = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 8.0f }, Nodes, 2U, "hedge", false);
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, (WorldPathObjects_GetStats(S.System).GeneratedTriangleCount * 3U) <= 300U);
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&S.World, Second));
        WorldPathObjects_Update(S.System);
        // Nothing left: a second hedge makes none, however long, and no piece of it is bent for nothing.
        Limits.MaxVertices = 0U;
        WorldPathObjects_SetLimits(S.System, &Limits);
        SetRow(run, &S, Hedge, "seed", PropertyValue_FromUInt32(6U));
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).GeneratedTriangleCount == 0U);
        // What collides as boxes still does, with nothing bent.
        Limits.MaxPlacedShapes = WORLD_PATH_OBJECTS_MAX_TOTAL_SHAPES;
        WorldPathObjects_SetLimits(S.System, &Limits);
        PathObjectSlotRecord Boxed = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Boxed.OverridesCollision = true;
        Boxed.Collision = PathObjectCollision_Boxes;
        size_t ShapesBefore = WorldPathObjects_GetStats(S.System).PlacedShapeCount;
        SetSlots(run, &S, Hedge, &Boxed, 1U);
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).PlacedShapeCount > ShapesBefore);
        Boxed.Collision = PathObjectCollision_Mesh;
        SetSlots(run, &S, Hedge, &Boxed, 1U);
        // Back under the default limits, a change brings all of it back.
        Limits = WorldPathObjectsLimits_CreateDefault();
        WorldPathObjects_SetLimits(S.System, &Limits);
        SetRow(run, &S, Hedge, "seed", PropertyValue_FromUInt32(7U));
        SetRow(run, &S, Fence, "seed", PropertyValue_FromUInt32(7U));
        WorldPathObjects_Update(S.System);
        WorldPathObjectsStats Again = WorldPathObjects_GetStats(S.System);
        TEST_CHECK(run,
            (Again.GeneratedTriangleCount == Whole.GeneratedTriangleCount) && (Again.PlacedShapeCount == Whole.PlacedShapeCount));
        // Boxes spend the world's shapes as a set's do.
        Limits.MaxPlacedShapes = 2U;
        WorldPathObjects_SetLimits(S.System, &Limits);
        PathObjectSlotRecord Boxes = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Boxes.OverridesCollision = true;
        Boxes.Collision = PathObjectCollision_Boxes;
        SetSlots(run, &S, Fence, &Boxes, 1U);
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).PlacedShapeCount == 2U);
    }
    Scene_Deconstruct(run, &S);
}

/* Slices lie where a walk of its own along the curve says, wherever a piece falls: many pieces share a section's samples. */
static void PiecesOfOneSectionShareItsSamples(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        // A straight section at a spacing of 0.25: a metre's rail has its four slabs, each piece alike, last as first.
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(20.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "hedge", false);
        WorldPathObjects_Update(S.System);
        WorldModelComponent* Part = FindPart(&S, Id, 0U);
        const WorldGeneratedModel* Generated = (Part != NULL) ? WorldModelComponent_GetGeneratedModel(Part) : NULL;
        TEST_CHECK(run, Generated != NULL);
        size_t WithFourSlabs = Generated->Lods[0].Meshes[0].VertexCount;
        // One rail has 12 triangles over its ends, plus 8 triangles a side a slab; over 20 rails of 4 slabs each.
        TEST_CHECK(run, WithFourSlabs > (20U * 4U * 8U * 3U));
        // Another spacing in another rebuild samples anew.
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesSliceSpacing = true;
        Record.SliceSpacing = 0.5f;
        SetSlots(run, &S, Id, &Record, 1U);
        WorldPathObjects_Update(S.System);
        Part = FindPart(&S, Id, 0U);
        Generated = (Part != NULL) ? WorldModelComponent_GetGeneratedModel(Part) : NULL;
        TEST_CHECK(run, (Generated != NULL) && (Generated->Lods[0].Meshes[0].VertexCount < WithFourSlabs));
    }
    Scene_Deconstruct(run, &S);
}

/* Whether any vertex of a part's finest LOD lies at a place in plan, in the object's space. */
static bool HasVertexAt(Scene* scene, uint64_t id, double x, double z, double tolerance)
{
    for (size_t Part = 0U; ; Part++)
    {
        WorldModelComponent* Component = FindPart(scene, id, Part);
        if (Component == NULL)
        {
            return false;
        }
        const WorldGeneratedModel* Generated = WorldModelComponent_GetGeneratedModel(Component);
        Vector3Double Place = WorldObject_GetPosition(WorldComponent_GetOwner(&Component->Base));
        for (uint32_t MeshIndex = 0U; (Generated != NULL) && (MeshIndex < Generated->Lods[0].MeshCount); MeshIndex++)
        {
            const WorldGeneratedMesh* Held = &Generated->Lods[0].Meshes[MeshIndex];
            for (uint32_t Vertex = 0U; Vertex < Held->VertexCount; Vertex++)
            {
                const float* P = &Held->Positions[(size_t)Vertex * 3U];
                if (NEAR((double)P[0] + Place.X, x, tolerance) && NEAR((double)P[2] + Place.Z, z, tolerance))
                {
                    return true;
                }
            }
        }
    }
}

/* Two neighbouring bent pieces on the ground share their end's slice, exactly as they do off it. */
static void SnappedBentPiecesMeet(TestRun* run)
{
    for (int Snaps = 0; Snaps < 2; Snaps++)
    {
        Scene S;
        if (Scene_Construct(run, &S))
        {
            AddTerrain(run, &S, "flat", FlatHeight, (Vector3){ 0.0f, 0.0f, 0.0f });
            WorldCollision_Sync(S.Collision);
            PathNode Nodes[5];
            for (size_t Index = 0U; Index < 5U; Index++)
            {
                double Angle = (2.0 / 14.0) * ((double)Index / 4.0);
                Nodes[Index] = MakeNode(5.0 + (14.0 * sin(Angle)), (Snaps == 1) ? 2.0 : 0.0, 5.0 + (14.0 * (1.0 - cos(Angle))),
                    PathNodeMode_Smooth);
                Nodes[Index].Width = 5.0f;
            }
            uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 5U, "hedge", Snaps == 1);
            WorldPathObjects_Update(S.System);
            WorldModelComponent* Part = FindPart(&S, Id, 0U);
            const WorldGeneratedModel* Generated = (Part != NULL) ? WorldModelComponent_GetGeneratedModel(Part) : NULL;
            if (TEST_CHECK(run, (Generated != NULL) && (WorldPathObjects_GetStats(S.System).PieceCount == 2U)))
            {
                // The vertices of the first piece's end have twins in the second: the mesh holds the pieces in turn.
                const WorldGeneratedMesh* Held = &Generated->Lods[0].Meshes[0];
                uint32_t Half = Held->VertexCount / 2U;
                size_t Twins = 0U;
                for (uint32_t First = 0U; First < Half; First++)
                {
                    const float* A = &Held->Positions[(size_t)First * 3U];
                    bool HasTwin = false;
                    for (uint32_t Second = Half; (Second < Held->VertexCount) && !HasTwin; Second++)
                    {
                        const float* B = &Held->Positions[(size_t)Second * 3U];
                        HasTwin = NEAR(A[0], B[0], 1.0e-4) && NEAR(A[1], B[1], 1.0e-4) && NEAR(A[2], B[2], 1.0e-4);
                    }
                    Twins += HasTwin ? 1U : 0U;
                }
                TEST_CHECK(run, Twins >= 4U);
            }
        }
        Scene_Deconstruct(run, &S);
    }
}

/* What stands off to a side turns a corner by its mitre: rigid posts at the two sides' meeting, and a bent run with no leg
 * passing it. */
static void ASideTurnsACornerByItsMitre(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[3] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner),
            MakeNode(10.0, 0.0, 10.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 3U, "fence", false);
        // A fence on the right of a right turn: its side's corner lies at (9, 1), and on the left at (11, -1).
        for (int Side = 0; Side < 2; Side++)
        {
            PathObjectSlotRecord Records[2] = { PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle),
                PathObjectSlotRecord_CreateDefault(PathObjectSlot_Separator) };
            for (size_t Index = 0U; Index < 2U; Index++)
            {
                Records[Index].OverridesSide = true;
                Records[Index].Side = (Side == 0) ? PathObjectSide_Right : PathObjectSide_Left;
                Records[Index].OverridesSideOffset = true;
                Records[Index].SideOffset = 1.0f;
            }
            SetSlots(run, &S, Id, Records, 2U);
            SetRow(run, &S, Id, "overrides_separator_every", PropertyValue_FromBool(true));
            SetRow(run, &S, Id, "separator_every", PropertyValue_FromUInt32(1000U));
            WorldPathObjects_Update(S.System);
            size_t Count = 0U;
            const PropertyInstanceEntry* Posts = NULL;
            WorldObject* Parent = NULL;
            TEST_CHECK(run, World_TryGetObject(&S.World, Id, &Parent));
            bool IsThere = false;
            for (WorldObject* Child = WorldObject_GetFirstChild(Parent); Child != NULL; Child = WorldObject_GetNextSibling(Child))
            {
                WorldModelInstancesComponent* Copies = WorldModelInstancesComponent_FromComponent(WorldObject_GetComponent(Child,
                    WorldComponentType_ModelInstances));
                Posts = (Copies != NULL) ? WorldModelInstancesComponent_GetInstances(Copies, &Count) : NULL;
                for (size_t Index = 0U; (Posts != NULL) && (Index < Count); Index++)
                {
                    Vector3Double Foot = ThroughCopy(&Posts[Index], (Vector3){ 0.0f, 0.0f, 0.0f });
                    IsThere = IsThere || (NEAR(Foot.X, (Side == 0) ? 9.0 : 11.0, PLACE_TOLERANCE) && NEAR(Foot.Z,
                        (Side == 0) ? 1.0 : -1.0, PLACE_TOLERANCE));
                }
            }
            TEST_CHECK(run, IsThere);
            // And both legs' panels end there, the one before the corner and the one after it.
            size_t PanelCount = 0U;
            const PropertyInstanceEntry* Panels = GetCopies(&S, Id, "tbp:panel", &PanelCount);
            size_t EndsThere = 0U;
            for (size_t Index = 0U; (Panels != NULL) && (Index < PanelCount); Index++)
            {
                const Vector3 Ends[2] = { { 0.0f, 0.0f, 0.0f }, { 2.0f, 0.0f, 0.0f } };
                for (size_t End = 0U; End < 2U; End++)
                {
                    Vector3Double At = ThroughCopy(&Panels[Index], Ends[End]);
                    EndsThere += (NEAR(At.X, (Side == 0) ? 9.0 : 11.0, PLACE_TOLERANCE)
                        && NEAR(At.Z, (Side == 0) ? 1.0 : -1.0, PLACE_TOLERANCE)) ? 1U : 0U;
                }
            }
            TEST_CHECK(run, EndsThere == 2U);
        }
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&S.World, Id));
        WorldPathObjects_Update(S.System);
        // A bent run over the corner: on the inside no leg passes the corner of its side, and each meets the other there.
        uint64_t Bent = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 3U, "hedge", false);
        SetRow(run, &S, Bent, "overrides_fits_each_section", PropertyValue_FromBool(true));
        SetRow(run, &S, Bent, "fits_each_section", PropertyValue_FromBool(false));
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesSide = true;
        Record.Side = PathObjectSide_Right;
        Record.OverridesSideOffset = true;
        Record.SideOffset = 1.0f;
        SetSlots(run, &S, Bent, &Record, 1U);
        WorldPathObjects_Update(S.System);
        double Reach = -1.0e30;
        double Least = 1.0e30;
        for (size_t Part = 0U; Part < 4U; Part++)
        {
            BoundingBoxDouble Box = GetPartBox(&S, Bent, Part, -1.0e30);
            Reach = fmax(Reach, Box.Max.X);
            Least = fmin(Least, Box.Min.Z);
        }
        TEST_CHECK(run, NEAR(Reach, 9.1, 1.0e-2) && NEAR(Least, 0.9, 1.0e-2));
        // Its slice at the corner lies across the bisector, a metre in from the path's corner both ways.
        TEST_CHECK(run, HasVertexAt(&S, Bent, 8.9, 1.1, 1.0e-3) && HasVertexAt(&S, Bent, 9.1, 0.9, 1.0e-3));
    }
    Scene_Deconstruct(run, &S);
}

/* A corner in the middle of a bent piece keeps its slice on the inside too: the two legs meet there, not across a chord. */
static void ACornerInsideABentPieceKeepsItsSlice(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[3] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.5, 0.0, 0.0, PathNodeMode_Corner),
            MakeNode(10.5, 0.0, 10.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 3U, "hedge", false);
        SetRow(run, &S, Id, "overrides_fits_each_section", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "fits_each_section", PropertyValue_FromBool(false));
        SetRow(run, &S, Id, "overrides_uses_count", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "uses_count", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "overrides_count", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "count", PropertyValue_FromUInt32(4U));
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesSide = true;
        Record.Side = PathObjectSide_Right;
        Record.OverridesSideOffset = true;
        Record.SideOffset = 1.0f;
        SetSlots(run, &S, Id, &Record, 1U);
        WorldPathObjects_Update(S.System);
        // Four pieces over 20.5 m: the corner at 10.5 lies inside the third. Its side's corner is (9.5, 1).
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).PieceCount == 4U);
        TEST_CHECK(run, HasVertexAt(&S, Id, 9.4, 1.1, 1.0e-3) && HasVertexAt(&S, Id, 9.6, 0.9, 1.0e-3));
    }
    Scene_Deconstruct(run, &S);
}

/* A closed bent run mitres where it closes, as anywhere else. */
static void AClosedBentRunMitresWhereItCloses(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[4] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner),
            MakeNode(10.0, 0.0, 10.0, PathNodeMode_Corner), MakeNode(0.0, 0.0, 10.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 4U, "hedge", false);
        TEST_CHECK_SUCCESS(run, WorldPathComponent_SetClosed(GetPath(&S, Id), true));
        for (int FitsEach = 0; FitsEach < 2; FitsEach++)
        {
            SetRow(run, &S, Id, "overrides_fits_each_section", PropertyValue_FromBool(true));
            SetRow(run, &S, Id, "fits_each_section", PropertyValue_FromBool(FitsEach == 1));
            WorldPathObjects_Update(S.System);
            // The corner it closes on, node 0: inner (0.1, 0.1), outer (-0.1, -0.1).
            TEST_CHECK(run, HasVertexAt(&S, Id, 0.1, 0.1, 1.0e-3) && HasVertexAt(&S, Id, -0.1, -0.1, 1.0e-3));
            // And the one inside the run, node 2.
            TEST_CHECK(run, HasVertexAt(&S, Id, 9.9, 9.9, 1.0e-3) && HasVertexAt(&S, Id, 10.1, 10.1, 1.0e-3));
        }
    }
    Scene_Deconstruct(run, &S);
}

/* A bent piece's collision lies as its mesh does: the piece's own jitter, none of a rigid piece's turns. */
static void ABentPiecesCollisionLiesAsItsMeshDoes(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(1.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "hedge", false);
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesCollision = true;
        Record.Collision = PathObjectCollision_Boxes;
        Record.OverridesBoxSpacing = true;
        Record.BoxSpacing = 1.0f;
        Record.OverridesPositionJitter = true;
        Record.PositionJitter = (Vector3){ 0.0f, 0.0f, 2.0f };
        Record.OverridesScaleJitter = true;
        Record.ScaleJitter = 0.9f;
        Record.OverridesYawJitter = true;
        Record.YawJitter = 0.5f;
        Record.OverridesRotationOffset = true;
        Record.RotationOffset = (Vector3){ 0.0f, 0.5f, 0.0f };
        SetSlots(run, &S, Id, &Record, 1U);
        SetRow(run, &S, Id, "overrides_seed", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "seed", PropertyValue_FromUInt32(11U));
        WorldPathObjects_Update(S.System);
        BoundingBoxDouble Mesh0 = GetPartBox(&S, Id, 0U, -1.0e30);
        const WorldColliderPlacedShapes* Shapes = NULL;
        WorldObject* Parent = NULL;
        TEST_CHECK(run, World_TryGetObject(&S.World, Id, &Parent));
        for (WorldObject* Child = WorldObject_GetFirstChild(Parent); (Child != NULL)
            && (Shapes == NULL); Child = WorldObject_GetNextSibling(Child))
        {
            WorldColliderComponent* Collider = WorldColliderComponent_FromComponent(WorldObject_GetComponent(Child,
                WorldComponentType_Collider));
            Shapes = (Collider != NULL) ? WorldColliderComponent_GetPlacedShapes(Collider) : NULL;
        }
        if (TEST_CHECK(run, (Shapes != NULL) && (Shapes->Count == 1U)))
        {
            const Matrix4Double* Placement = &Shapes->Placements[0];
            Vector3 Center = Shapes->Shapes[0].Center;
            Vector3Double Middle = Matrix4Double_TransformPoint(Placement, Vector3Double_Create((double)Center.x,
                (double)Center.y, (double)Center.z));
            // It stands where the mesh stands, off the path by the same jitter, and straight along it; its height follows the
            // mesh's.
            TEST_CHECK(run, NEAR(Middle.Z, 0.5 * (Mesh0.Min.Z + Mesh0.Max.Z), 1.0e-2) && (fabs(Middle.Z) > 0.01));
            TEST_CHECK(run, NEAR(Placement->M[2], 0.0, 1.0e-6));
            double Height = sqrt((Placement->M[4] * Placement->M[4]) + (Placement->M[5] * Placement->M[5])
                + (Placement->M[6] * Placement->M[6]));
            TEST_CHECK(run, NEAR(Height, Mesh0.Max.Y - Mesh0.Min.Y, 1.0e-2));
        }
    }
    Scene_Deconstruct(run, &S);
}

/* A model's tint reaches a bent piece's part, its alpha as an opacity. */
static void AModelsTintReachesAPart(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(8.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "hedge", false);
        PathObjectModelRecord Model0 = { .Slot = PathObjectSlot_Middle, .WorldModel = (unsigned char*)u8"rail", .Weight = 1.0f,
            .Tint = { 100, 50, 25, 51 } };
        PropertyRecordList* Models = NULL;
        TEST_CHECK_SUCCESS(run, PropertyRecordList_Create(PathObject_GetModelPropertyTable(), &Model0, 1U, &Models));
        SetRow(run, &S, Id, "overrides_models", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "models", PropertyValue_FromRecordList(Models));
        PropertyRecordList_Destroy(Models);
        WorldObject* Object = NULL;
        TEST_CHECK(run, World_TryGetObject(&S.World, Id, &Object));
        RenderColor Tint = { .Tint = { 255, 128, 255, 255 }, .Brightness = 0.75f, .Opacity = 0.5f };
        TEST_CHECK_SUCCESS(run, WorldObject_SetTint(Object, Tint));
        WorldPathObjects_Update(S.System);
        WorldModelComponent* Part = FindPart(&S, Id, 0U);
        if (TEST_CHECK(run, Part != NULL))
        {
            RenderColor Taken = WorldObject_GetTint(WorldComponent_GetOwner(&Part->Base));
            TEST_CHECK(run,
                (Taken.Tint.r == 100U) && (Taken.Tint.g == 25U) && (Taken.Tint.b == 25U) && (Taken.Brightness == 0.75f)
                && NEAR(Taken.Opacity, 0.1, 1.0e-5));
        }
    }
    Scene_Deconstruct(run, &S);
}

/* A cut on a turned piece lies at the same end of the path: the other end of its mesh. */
static void ACutOnATurnedPieceStaysAtItsEndOfThePath(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(3.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "wedges", false);
        SetRow(run, &S, Id, "overrides_fit", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "fit", PropertyValue_FromEnum(PathObjectFit_ResizeEnds));
        SetRow(run, &S, Id, "overrides_resize_method", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "resize_method", PropertyValue_FromEnum(PathObjectResizeMethod_Cut));
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesFlip = true;
        Record.Flip = PathObjectFlip_Always;
        SetSlots(run, &S, Id, &Record, 1U);
        WorldPathObjects_Update(S.System);
        // Two wedges of 2 m turned round and cut to 1.5 m at the path's two ends. The first shows mesh X 1.5 to 0 on the path's 0
        // to 1.5: its high half on 0 to 0.5. The second shows mesh X 2 to 0.5 on 1.5 to 3: its high half on 1.5 to 2.5.
        BoundingBoxDouble High = GetPartBox(&S, Id, 0U, 1.5);
        TEST_CHECK(run, NEAR(High.Min.X, 0.0, PLACE_TOLERANCE) && NEAR(High.Max.X, 2.5, PLACE_TOLERANCE));
        // Nothing of a cut mesh reaches past the path's ends.
        BoundingBoxDouble Whole = GetPartBox(&S, Id, 0U, -1.0e30);
        TEST_CHECK(run, NEAR(Whole.Min.X, 0.0, PLACE_TOLERANCE) && NEAR(Whole.Max.X, 3.0, PLACE_TOLERANCE));
        WorldModelComponent* Part = FindPart(&S, Id, 0U);
        const WorldGeneratedModel* Generated = WorldModelComponent_GetGeneratedModel(Part);
        Vector3Double Place = WorldObject_GetPosition(WorldComponent_GetOwner(&Part->Base));
        size_t Between = 0U;
        for (uint32_t MeshIndex = 0U; MeshIndex < Generated->Lods[0].MeshCount; MeshIndex++)
        {
            const WorldGeneratedMesh* Held = &Generated->Lods[0].Meshes[MeshIndex];
            for (uint32_t Vertex = 0U; Vertex < Held->VertexCount; Vertex++)
            {
                const float* P = &Held->Positions[(size_t)Vertex * 3U];
                double X = (double)P[0] + Place.X;
                Between += (((double)P[1] + Place.Y) > 1.5) && (X > 0.51) && (X < 1.49) ? 1U : 0U;
            }
        }
        TEST_CHECK(run, Between == 0U);
    }
    Scene_Deconstruct(run, &S);
}

/* However long a bent piece and however coarse its slices, each part holds something, and its two ends keep their faces. */
static void EveryPartOfALongPieceHoldsSomething(TestRun* run)
{
    const float Spacings[3] = { 1.0f, 40.0f, 100.0f };
    for (size_t Index = 0U; Index < 3U; Index++)
    {
        Scene S;
        if (Scene_Construct(run, &S))
        {
            const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(300.0, 0.0, 0.0,
                PathNodeMode_Corner) };
            uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "hedge", false);
            SetRow(run, &S, Id, "overrides_uses_count", PropertyValue_FromBool(true));
            SetRow(run, &S, Id, "uses_count", PropertyValue_FromBool(true));
            SetRow(run, &S, Id, "overrides_count", PropertyValue_FromBool(true));
            SetRow(run, &S, Id, "count", PropertyValue_FromUInt32(1U));
            PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
            Record.OverridesSliceSpacing = true;
            Record.SliceSpacing = Spacings[Index];
            SetSlots(run, &S, Id, &Record, 1U);
            WorldPathObjects_Update(S.System);
            WorldPathObjectsStats Stats = WorldPathObjects_GetStats(S.System);
            TEST_CHECK(run, (Stats.PieceCount == 1U) && (Stats.GeneratedModelCount >= 3U) && (Stats.GeneratedModelCount <= 10U));
            double Least = 1.0e30;
            double Most = -1.0e30;
            for (size_t Part = 0U; Part < Stats.GeneratedModelCount; Part++)
            {
                BoundingBoxDouble Box = GetPartBox(&S, Id, Part, -1.0e30);
                TEST_CHECK(run, Box.Max.X > Box.Min.X);
                Least = fmin(Least, Box.Min.X);
                Most = fmax(Most, Box.Max.X);
            }
            TEST_CHECK(run, NEAR(Least, 0.0, 1.0e-2) && NEAR(Most, 300.0, 1.0e-2));
            // The faces that close its two ends are there: a triangle across each end's full height.
            size_t FacesAtStart = 0U;
            size_t FacesAtEnd = 0U;
            for (size_t Part = 0U; Part < Stats.GeneratedModelCount; Part++)
            {
                WorldModelComponent* Component = FindPart(&S, Id, Part);
                const WorldGeneratedMesh* Held = &WorldModelComponent_GetGeneratedModel(Component)->Lods[0].Meshes[0];
                Vector3Double Place = WorldObject_GetPosition(WorldComponent_GetOwner(&Component->Base));
                for (uint32_t Vertex = 0U; (Vertex + 2U) < Held->VertexCount; Vertex += 3U)
                {
                    const float* P = &Held->Positions[(size_t)Vertex * 3U];
                    bool IsStart = NEAR((double)P[0] + Place.X, 0.0, 1.0e-3) && NEAR((double)P[3] + Place.X, 0.0,
                        1.0e-3) && NEAR((double)P[6] + Place.X, 0.0, 1.0e-3);
                    bool IsEnd = NEAR((double)P[0] + Place.X, 300.0, 1.0e-2) && NEAR((double)P[3] + Place.X, 300.0,
                        1.0e-2) && NEAR((double)P[6] + Place.X, 300.0, 1.0e-2);
                    FacesAtStart += IsStart ? 1U : 0U;
                    FacesAtEnd += IsEnd ? 1U : 0U;
                }
            }
            TEST_CHECK(run, (FacesAtStart == 2U) && (FacesAtEnd == 2U));
        }
        Scene_Deconstruct(run, &S);
    }
}

/* A part its model has no triangle in is no part: a piece far longer than its mesh's body leaves most of its stretches empty. */
static void AStretchWithNothingInItMakesNoChild(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        TEST_CHECK(run, AddPathObjectAsset(run, &S,
            "{ \"name\": \"long_posts\", \"models\": [ { \"slot\": \"middle\", \"world_model\": \"post\" } ], "
            "\"uses_count\": true, \"count\": 1, \"slots\": [ { \"slot\": \"middle\", \"curve\": \"bent\", \"length\": 100, "
                "\"slice_spacing\": 1, "
            "\"collision\": \"none\" } ] }"));
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(90.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "long_posts", false);
        WorldPathObjects_Update(S.System);
        // The post's 0.2 m lie around its body's middle: only the middle one of the three stretches holds them.
        WorldPathObjectsStats Stats = WorldPathObjects_GetStats(S.System);
        TEST_CHECK(run, (Stats.PieceCount == 1U) && (Stats.GeneratedModelCount == 1U) && (CountChildren(&S, Id) == 1U));
    }
    Scene_Deconstruct(run, &S);
}

/* A hostile piece, very long or over many sections at a tiny spacing, stays within what a rebuild may do. */
static void AHostilePieceStaysWithinItsLimits(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(1.0e6, 0.0, 0.0,
            PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "hedge", false);
        SetRow(run, &S, Id, "overrides_uses_count", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "uses_count", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "overrides_count", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "count", PropertyValue_FromUInt32(1U));
        WorldPathObjects_Update(S.System);
        WorldPathObjectsStats Stats = WorldPathObjects_GetStats(S.System);
        TEST_CHECK(run, (Stats.GeneratedModelCount > 0U) && (Stats.GeneratedModelCount < PATH_OBJECT_MAX_SLICES));
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&S.World, Id));
        WorldPathObjects_Update(S.System);
        // One piece over 256 corners at a spacing of 22 micrometres.
        PathNode Zigzag[256];
        for (size_t Index = 0U; Index < 256U; Index++)
        {
            Zigzag[Index] = MakeNode(10.0 * (double)Index, 0.0, ((Index % 2U) == 0U) ? 5.0 : -5.0, PathNodeMode_Corner);
        }
        Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Zigzag, 256U, "hedge", false);
        SetRow(run, &S, Id, "overrides_uses_count", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "uses_count", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "overrides_count", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "count", PropertyValue_FromUInt32(1U));
        SetRow(run, &S, Id, "overrides_fits_each_section", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "fits_each_section", PropertyValue_FromBool(false));
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesSliceSpacing = true;
        Record.SliceSpacing = 2.2e-5f;
        SetSlots(run, &S, Id, &Record, 1U);
        WorldPathObjects_Update(S.System);
        Stats = WorldPathObjects_GetStats(S.System);
        TEST_CHECK(run, (Stats.PieceCount == 1U) && (Stats.GeneratedTriangleCount > 0U));
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&S.World, Id));
        WorldPathObjects_Update(S.System);
        // The children have a limit of their own.
        WorldPathObjectsLimits Limits = WorldPathObjectsLimits_CreateDefault();
        Limits.MaxChildren = 2U;
        WorldPathObjects_SetLimits(S.System, &Limits);
        AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", false);
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).ChildCount == 2U);
    }
    Scene_Deconstruct(run, &S);
}

/* Looks for the ground that may each pass over the object's own colliders have a limit: past it, pieces stay where the path is.
 */
static void LooksForTheGroundHaveALimit(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        AddTerrain(run, &S, "flat", FlatHeight, (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldCollision_Sync(S.Collision);
        const PathNode Nodes[2] = { MakeNode(1.0, 5.0, 5.0, PathNodeMode_Corner), MakeNode(31.0, 5.0, 5.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "gateposts", true);
        SetRow(run, &S, Id, "overrides_gap", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "gap", PropertyValue_FromFloat(0.001f));
        WorldPathObjects_Update(S.System);
        size_t Count = 0U;
        const PropertyInstanceEntry* Copies = GetCopies(&S, Id, "tbp:gatepost", &Count);
        TEST_CHECK(run, (Count > 20000U) && (Copies != NULL));
        TEST_CHECK(run, NEAR(ThroughCopy(&Copies[Count - 1U], (Vector3){ 0.0f, 0.0f, 0.0f }).Y, 0.0, PLACE_TOLERANCE));
        // Rebuilt, every ray may pass over as many shapes as it now holds: the budget runs out on the way.
        SetRow(run, &S, Id, "overrides_seed", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "seed", PropertyValue_FromUInt32(9U));
        WorldPathObjects_Update(S.System);
        Copies = GetCopies(&S, Id, "tbp:gatepost", &Count);
        TEST_CHECK(run, NEAR(ThroughCopy(&Copies[0], (Vector3){ 0.0f, 0.0f, 0.0f }).Y, 0.0, PLACE_TOLERANCE));
        TEST_CHECK(run, NEAR(ThroughCopy(&Copies[Count - 1U], (Vector3){ 0.0f, 0.0f, 0.0f }).Y, 5.0, PLACE_TOLERANCE));
    }
    Scene_Deconstruct(run, &S);
}

/* A path object under another's runtime child goes with it, whichever comes first in the world's list. */
static void APathObjectUnderAnothersChildGoesWithIt(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Outer = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", false);
        WorldPathObjects_Update(S.System);
        WorldObject* Child = FindCopiesChild(&S, Outer, "tbp:panel");
        uint64_t Inner = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", false);
        TEST_CHECK_SUCCESS(run, World_SetParent(&S.World, Inner, WorldObject_GetId(Child), false));
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).ComponentCount == 2U);
        // The outer one changes asset: the child goes, and the inner path object with it, in the same tick's list.
        TEST_CHECK_SUCCESS(run, WorldPathObjectComponent_SetPathObject(GetComponent(&S, Outer), (const unsigned char*)u8"hedge"));
        WorldPathObjects_Update(S.System);
        WorldObject* Gone = NULL;
        TEST_CHECK(run, !World_TryGetObject(&S.World, Inner, &Gone));
        WorldPathObjects_Update(S.System);
        WorldPathObjects_Update(S.System);
        WorldPathObjectsStats Stats = WorldPathObjects_GetStats(S.System);
        TEST_CHECK(run, (Stats.ComponentCount == 1U) && (Stats.CopyCount == 0U));
    }
    Scene_Deconstruct(run, &S);
}

/* A part with no mesh at its finest LOD collides as nothing, not as its world model. */
static void APartOfNothingHasNoCollider(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        TEST_CHECK(run, AddPathObjectAsset(run, &S,
            "{ \"name\": \"bent_posts\", \"models\": [ { \"slot\": \"middle\", \"world_model\": \"post\" } ], "
            "\"slots\": [ { \"slot\": \"middle\", \"curve\": \"bent\", \"slice_spacing\": 0.05, \"collision\": \"mesh\" } ] }"));
        PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(4.0, 0.0, 0.0, PathNodeMode_Corner) };
        Nodes[0].Scale = 0.0f;
        Nodes[1].Scale = 0.0f;
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "bent_posts", false);
        Scene_Update(&S);
        WorldRayHit Hit;
        TEST_CHECK(run, !RayDownHits(&S, (Vector3){ 0.5f, 5.0f, 0.0f }, &Hit));
        WorldObject* Parent = NULL;
        TEST_CHECK(run, World_TryGetObject(&S.World, Id, &Parent));
        for (WorldObject* Child = WorldObject_GetFirstChild(Parent); Child != NULL; Child = WorldObject_GetNextSibling(Child))
        {
            WorldColliderComponent* Collider = WorldColliderComponent_FromComponent(WorldObject_GetComponent(Child,
                WorldComponentType_Collider));
            TEST_CHECK(run, (Collider == NULL) || (WorldColliderComponent_GetPlacedShapes(Collider) != NULL));
        }
    }
    Scene_Deconstruct(run, &S);
}

/* What lies past where an object may lie is left out, once, and writes nothing tick after tick. */
static void WhatLiesTooFarOutIsLeftOut(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const double Far = WORLD_POSITION_LIMIT - 0.5;
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, Far, PathNodeMode_Corner), MakeNode(10.0, 0.0, Far, PathNodeMode_Corner) };
        // A hedge off to its right: its parts' places pass the limit. No write, no rebuild after the first.
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "hedge", false);
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesSide = true;
        Record.Side = PathObjectSide_Right;
        Record.OverridesSideOffset = true;
        Record.SideOffset = 2.0f;
        SetSlots(run, &S, Id, &Record, 1U);
        Scene_Update(&S);
        Scene_Update(&S);
        WorldPathObjectsStats Before = WorldPathObjects_GetStats(S.System);
        for (int Tick = 0; Tick < 5; Tick++)
        {
            Scene_Update(&S);
        }
        WorldPathObjectsStats After = WorldPathObjects_GetStats(S.System);
        TEST_CHECK(run,
            (After.WriteCount == Before.WriteCount) && (After.RebuildCount == Before.RebuildCount) && (After.WriteCount == 0U));
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&S.World, Id));
        WorldPathObjects_Update(S.System);
        // Copies on both sides: those past the limit are left out alone, and the list still holds the others.
        Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", false);
        Record.Side = PathObjectSide_Both;
        SetSlots(run, &S, Id, &Record, 1U);
        WorldPathObjects_Update(S.System);
        size_t Count = 0U;
        const PropertyInstanceEntry* Copies = GetCopies(&S, Id, "tbp:panel", &Count);
        TEST_CHECK(run, (Count == 5U) && (Copies != NULL));
        for (size_t Index = 0U; (Index < Count) && (Copies != NULL); Index++)
        {
            TEST_CHECK(run, Copies[Index].Position.Z < Far);
        }
    }
    Scene_Deconstruct(run, &S);
}

/* A list of copies holds so many: both sides of more pieces than that fill it, and it is not refused whole. */
static void AListOfCopiesFillsUpAndNoMore(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(20000.0, 0.0, 0.0,
            PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "gateposts", false);
        SetRow(run, &S, Id, "overrides_gap", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "gap", PropertyValue_FromFloat(0.125f));
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesSide = true;
        Record.Side = PathObjectSide_Both;
        Record.OverridesCollision = true;
        Record.Collision = PathObjectCollision_None;
        SetSlots(run, &S, Id, &Record, 1U);
        WorldPathObjects_Update(S.System);
        size_t Count = 0U;
        GetCopies(&S, Id, "tbp:gatepost", &Count);
        TEST_CHECK(run, Count == WORLD_MODEL_INSTANCES_MAX_COUNT);
    }
    Scene_Deconstruct(run, &S);
}

/* An object of no size when it was built looks for the ground when it has one again. */
static void AFlatObjectFindsTheGroundWhenItGrowsBack(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        AddTerrain(run, &S, "flat", FlatHeight, (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldCollision_Sync(S.Collision);
        const PathNode Nodes[2] = { MakeNode(2.0, 5.0, 5.0, PathNodeMode_Corner), MakeNode(12.0, 5.0, 5.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", true);
        WorldObject* Object = NULL;
        TEST_CHECK(run, World_TryGetObject(&S.World, Id, &Object));
        TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Object, (Vector3){ 0.0f, 0.0f, 0.0f }));
        WorldPathObjects_Update(S.System);
        TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Object, (Vector3){ 1.0f, 1.0f, 1.0f }));
        WorldPathObjects_Update(S.System);
        size_t Count = 0U;
        const PropertyInstanceEntry* Copies = GetCopies(&S, Id, "tbp:panel", &Count);
        TEST_CHECK(run, (Count > 0U) && NEAR(ThroughCopy(&Copies[0], (Vector3){ 0.0f, 0.0f, 0.0f }).Y, 0.0, PLACE_TOLERANCE));
    }
    Scene_Deconstruct(run, &S);
}

/* A cap with a length of its own stays as thick on a slope, a level piece not stretching; a panel spans its chord's run. */
static void ACapStaysAsThickOnASlope(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 10.0, 0.0,
            PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", false);
        PathObjectSlotRecord Records[2] = { PathObjectSlotRecord_CreateDefault(PathObjectSlot_StartCap),
            PathObjectSlotRecord_CreateDefault(PathObjectSlot_Separator) };
        Records[0].OverridesLength = true;
        Records[0].Length = 0.2f;
        Records[1].OverridesLength = true;
        Records[1].Length = 0.2f;
        SetSlots(run, &S, Id, Records, 2U);
        WorldPathObjects_Update(S.System);
        size_t Count = 0U;
        const PropertyInstanceEntry* Posts = GetCopies(&S, Id, "tbp:post", &Count);
        TEST_CHECK(run, Count > 0U);
        for (size_t Index = 0U; (Index < Count) && (Posts != NULL); Index++)
        {
            TEST_CHECK(run, NEAR(Posts[Index].Scale.x, 1.0, 1.0e-4));
        }
        // A level panel that stretches is the fit's: it spans its run.
        PathObjectSlotRecord Level = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Level.OverridesSlope = true;
        Level.Slope = PathObjectSlope_Step;
        SetSlots(run, &S, Id, &Level, 1U);
        WorldPathObjects_Update(S.System);
        const PropertyInstanceEntry* Panels = GetCopies(&S, Id, "tbp:panel", &Count);
        TEST_CHECK(run, (Count > 0U) && (Panels[0].Scale.x < 0.9f));
        // One that does not stretch keeps its own length, but for the end pieces a fit resizes.
        Level.OverridesStretches = true;
        Level.Stretches = false;
        SetSlots(run, &S, Id, &Level, 1U);
        SetRow(run, &S, Id, "overrides_fit", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "fit", PropertyValue_FromEnum(PathObjectFit_ResizeEnds));
        WorldPathObjects_Update(S.System);
        Panels = GetCopies(&S, Id, "tbp:panel", &Count);
        TEST_CHECK(run, Count >= 3U);
        if (Count >= 3U)
        {
            TEST_CHECK(run, NEAR(Panels[1].Scale.x, 1.0, 1.0e-4) && !NEAR(Panels[0].Scale.x, 1.0, 1.0e-2));
        }
    }
    Scene_Deconstruct(run, &S);
}

/* A world model's trigger flag reaches what its copies collide as: no model component on the child can say so. */
static void ACopysColliderTakesItsModelsTriggerFlag(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        TEST_CHECK(run, TestAssets_AddWorldModel(run, &S.Fixture,
            "{\"name\": \"zone\", \"lods\": [{\"model\": \"post\", \"max_distance\": 500}], "
            "\"collision\": { \"is_trigger\": true, \"sets\": [ { \"name\": \"all\", \"shapes\": [ { \"type\": \"box\", "
                "\"half_extents\": [1, 1, 1] } ] } ] } }"));
        TEST_CHECK(run, AddPathObjectAsset(run, &S,
            "{ \"name\": \"zones\", \"models\": [ { \"slot\": \"middle\", \"world_model\": \"zone\" } ] }"));
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "zones", false);
        WorldPathObjects_Update(S.System);
        WorldColliderComponent* Collider = FindCollider(&S, Id, "tbp:zone");
        TEST_CHECK(run, (Collider != NULL) && WorldColliderComponent_IsOverridden(Collider, WorldColliderSetting_IsTrigger)
            && WorldColliderComponent_IsTrigger(Collider));
    }
    Scene_Deconstruct(run, &S);
}

/* A snapped path object's own colliders change the ground under it: a rebuild follows, finds nothing different, and it comes to
 * rest, in every kind of collision. */
static void ItsOwnCollidersDoNotKeepItRebuilding(TestRun* run)
{
    static const PathObjectCollision Kinds[3] = { PathObjectCollision_Model, PathObjectCollision_Boxes,
        PathObjectCollision_Mesh };
    for (size_t Kind = 0U; Kind < 3U; Kind++)
    {
        Scene S;
        if (Scene_Construct(run, &S))
        {
            AddTerrain(run, &S, "slope", RisingHeight, (Vector3){ 0.0f, 0.0f, 0.0f });
            WorldCollision_Sync(S.Collision);
            const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 10.0, PathNodeMode_Corner), MakeNode(20.0, 0.0, 10.0,
                PathNodeMode_Corner) };
            uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, (Kind == 2U) ? "hedge" : "fence", true);
            PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
            Record.OverridesCollision = true;
            Record.Collision = Kinds[Kind];
            PropertyRecordList* List = NULL;
            TEST_CHECK_SUCCESS(run, PropertyRecordList_Create(PathObject_GetSlotPropertyTable(), &Record, 1U, &List));
            SetRow(run, &S, Id, "slots", PropertyValue_FromRecordList(List));
            PropertyRecordList_Destroy(List);
            for (int Tick = 0; Tick < 3; Tick++)
            {
                Scene_Update(&S);
            }
            WorldPathObjectsStats Settled = WorldPathObjects_GetStats(S.System);
            TEST_CHECK(run, (Settled.RebuildCount >= 1U) && (Settled.RebuildCount <= 2U));
            uint64_t Writes = Settled.WriteCount;
            for (int Tick = 0; Tick < 10; Tick++)
            {
                Scene_Update(&S);
            }
            WorldPathObjectsStats Later = WorldPathObjects_GetStats(S.System);
            TEST_CHECK(run, (Later.RebuildCount == Settled.RebuildCount) && (Later.WriteCount == Writes));
        }
        Scene_Deconstruct(run, &S);
    }
}

/* Several path objects: each state follows its own component, whichever goes. */
static void EachComponentKeepsItsOwnState(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Ids[4];
        for (size_t Index = 0U; Index < 4U; Index++)
        {
            Ids[Index] = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 3.0f * (float)Index }, Nodes, 2U, "fence", false);
        }
        WorldPathObjects_Update(S.System);
        WorldPathObjectsStats Stats = WorldPathObjects_GetStats(S.System);
        TEST_CHECK(run, (Stats.ComponentCount == 4U) && (Stats.RebuildCount == 4U) && (Stats.ChildCount == 16U));
        // One in the middle goes: the others rebuild nothing, and their counts stay.
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&S.World, Ids[1]));
        WorldPathObjects_Update(S.System);
        Stats = WorldPathObjects_GetStats(S.System);
        TEST_CHECK(run, (Stats.ComponentCount == 3U) && (Stats.RebuildCount == 4U) && (Stats.ChildCount == 12U));
        // The last one changes: it alone rebuilds, as a new one builds for the first time.
        const PathNode Moved = MakeNode(12.0, 0.0, 0.0, PathNodeMode_Corner);
        TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNode(GetPath(&S, Ids[3]), 1U, &Moved));
        uint64_t Added = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 20.0f }, Nodes, 2U, "fence", false);
        WorldPathObjects_Update(S.System);
        Stats = WorldPathObjects_GetStats(S.System);
        TEST_CHECK(run, (Stats.ComponentCount == 4U) && (Stats.RebuildCount == 6U) && (Stats.ChildCount == 16U));
        TEST_CHECK(run, (CountChildren(&S, Ids[3]) == 4U) && (CountChildren(&S, Added) == 4U) && (CountChildren(&S,
            Ids[0]) == 4U));
        size_t Count = 0U;
        GetCopies(&S, Ids[3], "tbp:panel", &Count);
        TEST_CHECK(run, Count == 6U);
        GetCopies(&S, Ids[2], "tbp:panel", &Count);
        TEST_CHECK(run, Count == 5U);
    }
    Scene_Deconstruct(run, &S);
}

/* A child gone from outside leaves the count when a rebuild finds it gone, used or not, and the count stays the world's. */
static void TheChildCountFollowsChildrenRemovedFromOutside(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", false);
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).ChildCount == CountChildren(&S, Id));
        const PathNode Moved = MakeNode(12.0, 0.0, 0.0, PathNodeMode_Corner);
        for (int Round = 0; Round < 3; Round++)
        {
            TEST_CHECK_SUCCESS(run, World_RemoveObject(&S.World, WorldObject_GetId(FindCopiesChild(&S, Id, "tbp:panel"))));
            TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNode(GetPath(&S, Id), 1U, &Moved));
            WorldPathObjects_Update(S.System);
            TEST_CHECK(run, (FindCopiesChild(&S, Id, "tbp:panel") != NULL)
                && (WorldPathObjects_GetStats(S.System).ChildCount == CountChildren(&S, Id)));
        }
    }
    Scene_Deconstruct(run, &S);
}

/* The system going takes what it made along: the collision it held borrows from world models that go with it. */
static void TheSystemGoingTakesWhatItMade(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", false);
        Scene_Update(&S);
        WorldRayHit Hit;
        TEST_CHECK(run, (CountChildren(&S, Id) == 4U) && RayDownHits(&S, (Vector3){ 2.0f, 5.0f, 0.0f }, &Hit));
        WorldPathObjects_Destroy(S.System);
        S.System = NULL;
        TEST_CHECK(run, CountChildren(&S, Id) == 0U);
        // With no sync of its own: the world models its collision borrowed from may be gone by now.
        TEST_CHECK(run, !RayDownHits(&S, (Vector3){ 2.0f, 5.0f, 0.0f }, &Hit));
        // Its component and its path stay: another system takes over.
        TEST_CHECK(run, (GetComponent(&S, Id) != NULL) && TEST_CHECK_SUCCESS(run, WorldPathObjects_Create(&S.World, S.Collision,
            S.Fixture.Manager, NULL, &S.System)));
        if (S.System != NULL)
        {
            Scene_Update(&S);
            TEST_CHECK(run, (CountChildren(&S, Id) == 4U) && RayDownHits(&S, (Vector3){ 2.0f, 5.0f, 0.0f }, &Hit));
        }
    }
    Scene_Deconstruct(run, &S);
}


// Tests: files.
static void OnlyTheComponentGoesIntoWorldFiles(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 1.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 1.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 2.0f, 0.0f, 3.0f }, Nodes, 2U, "fence", false);
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Separator);
        Record.OverridesSlope = true;
        Record.Slope = PathObjectSlope_Tilt;
        PropertyRecordList* List = NULL;
        TEST_CHECK_SUCCESS(run, PropertyRecordList_Create(PathObject_GetSlotPropertyTable(), &Record, 1U, &List));
        SetRow(run, &S, Id, "slots", PropertyValue_FromRecordList(List));
        PropertyRecordList_Destroy(List);
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, CountChildren(&S, Id) == 4U);
        for (int IsBinary = 0; IsBinary < 2; IsBinary++)
        {
            GenericBuffer Bytes;
            GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
            TEST_CHECK_SUCCESS(run, (IsBinary != 0) ? WorldFile_WriteGHDF(&S.World, &Bytes) : WorldFile_WriteJSON(&S.World,
                &Bytes));
            if (IsBinary == 0)
            {
                TEST_CHECK(run, GenericBuffer_NullTerminate(&Bytes));
                const char* Text = (const char*)Bytes._data;
                TEST_CHECK(run, (strstr(Text, "\"path_object\": \"tbp:fence\"") != NULL) && (strstr(Text,
                    "\"slope\": \"tilt\"") != NULL));
                // No child, no copy of it.
                TEST_CHECK(run, (strstr(Text, "path_object_copies") == NULL) && (strstr(Text, "model_instances") == NULL));
            }
            KeyRegistry LoadedKeys;
            TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&LoadedKeys));
            World Loaded;
            WorldFileReadOptions Options = WorldFileReadOptions_Create(&LoadedKeys, S.Fixture.Manager, NULL);
            size_t Length = (IsBinary == 0) ? strlen((const char*)Bytes._data) : Bytes._count;
            Error Read = (IsBinary != 0) ? WorldFile_ReadGHDF(Bytes._data, Length, &Options, &Loaded)
                : WorldFile_ReadJSON(Bytes._data, Length, &Options, &Loaded);
            if (TEST_CHECK_SUCCESS(run, Read))
            {
                WorldObject* Object = NULL;
                TEST_CHECK(run, World_TryGetObject(&Loaded, Id, &Object) && (WorldObject_GetChildCount(Object) == 0U));
                WorldPathObjectComponent* Component = WorldPathObjectComponent_FromComponent(WorldObject_GetComponent(Object,
                    WorldComponentType_PathObject));
                TEST_CHECK(run, (Component != NULL) && (strcmp((const char*)WorldPathObjectComponent_GetPathObject(Component),
                    "tbp:fence") == 0));
                TEST_CHECK(run, !PathObject_ResolveSettings(NULL,
                    WorldPathObjectComponent_GetSettings(Component)).SnapsToTerrain);
                PathObjectSlotRecord Back = PathObject_ResolveSlot(PathObjectSlot_Separator, NULL,
                    WorldPathObjectComponent_GetSettings(Component));
                TEST_CHECK(run, Back.Slope == PathObjectSlope_Tilt);
                // What it makes comes back from it, the same.
                WorldPathObjects* System = NULL;
                TEST_CHECK_SUCCESS(run, WorldPathObjects_Create(&Loaded, NULL, S.Fixture.Manager, NULL, &System));
                WorldPathObjects_Update(System);
                TEST_CHECK(run, WorldObject_GetChildCount(Object) == 4U);
                TEST_CHECK(run, WorldPathObjects_GetStats(System).PieceCount == WorldPathObjects_GetStats(S.System).PieceCount);
                WorldPathObjects_Destroy(System);
                TEST_CHECK_SUCCESS(run, World_Deconstruct(&Loaded));
            }
            KeyRegistry_Deconstruct(&LoadedKeys);
            Memory_Free(Bytes._data);
        }
    }
    Scene_Deconstruct(run, &S);
}


// Tests: the ground.
static void PiecesStandOnTheGround(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        AddTerrain(run, &S, "slope", RisingHeight, (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldCollision_Sync(S.Collision);
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 10.0, PathNodeMode_Corner), MakeNode(20.0, 0.0, 10.0,
            PathNodeMode_Corner) };
        // The path stands flat; the search reaches 5 m above a point: the ground lies up to 5 m over it.
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", true);
        WorldPathObjects_Update(S.System);
        size_t Count = 0U;
        const PropertyInstanceEntry* Copies = GetCopies(&S, Id, "tbp:panel", &Count);
        TEST_CHECK(run, (Count == 10U) && (Copies != NULL));
        for (size_t Index = 0U; (Index < Count) && (Copies != NULL); Index++)
        {
            Vector3Double Foot = ThroughCopy(&Copies[Index], (Vector3){ 0.0f, 0.0f, 0.0f });
            Vector3Double End = ThroughCopy(&Copies[Index], (Vector3){ 2.0f, 0.0f, 0.0f });
            TEST_CHECK(run, NEAR(Foot.Y, 0.25 * Foot.X, PLACE_TOLERANCE) && NEAR(End.Y, 0.25 * End.X, PLACE_TOLERANCE));
            // A panel shears by default: straight up over its foot.
            Vector3Double Top = ThroughCopy(&Copies[Index], (Vector3){ 0.0f, 1.0f, 0.0f });
            TEST_CHECK(run, NEAR(Top.X, Foot.X, PLACE_TOLERANCE) && NEAR(Top.Y, Foot.Y + 1.0, PLACE_TOLERANCE));
            TEST_CHECK(run, NEAR(Copies[Index].Shear, 0.25, 1.0e-3));
        }
        // An offset lifts every piece.
        SetRow(run, &S, Id, "overrides_ground_offset", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "ground_offset", PropertyValue_FromFloat(0.5f));
        WorldPathObjects_Update(S.System);
        Copies = GetCopies(&S, Id, "tbp:panel", &Count);
        TEST_CHECK(run, NEAR(ThroughCopy(&Copies[0], (Vector3){ 0.0f, 0.0f, 0.0f }).Y, 0.5, PLACE_TOLERANCE));
        // A placement turns it off: the same asset floats, at the path's own height.
        SetRow(run, &S, Id, "snaps_to_terrain", PropertyValue_FromBool(false));
        SetRow(run, &S, Id, "overrides_snaps_to_terrain", PropertyValue_FromBool(true));
        WorldPathObjects_Update(S.System);
        Copies = GetCopies(&S, Id, "tbp:panel", &Count);
        TEST_CHECK(run, NEAR(ThroughCopy(&Copies[4], (Vector3){ 2.0f, 0.0f, 0.0f }).Y, 0.0, PLACE_TOLERANCE));
        // A section says otherwise for its stretch: a bridge over the rest.
        SetRow(run, &S, Id, "snaps_to_terrain", PropertyValue_FromBool(true));
        PathNode Bridge[3] = { MakeNode(0.0, 0.0, 10.0, PathNodeMode_Corner), MakeNode(10.0, 3.0, 10.0, PathNodeMode_Corner),
            MakeNode(20.0, 3.0, 10.0, PathNodeMode_Corner) };
        Bridge[1].OverridesSnapsToTerrain = true;
        Bridge[1].SnapsToTerrain = false;
        TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(GetPath(&S, Id), Bridge, 3U));
        SetRow(run, &S, Id, "ground_offset", PropertyValue_FromFloat(0.0f));
        WorldPathObjects_Update(S.System);
        Copies = GetCopies(&S, Id, "tbp:panel", &Count);
        TEST_CHECK(run, (Count == 10U) && NEAR(ThroughCopy(&Copies[9], (Vector3){ 0.0f, 0.0f, 0.0f }).Y, 3.0, PLACE_TOLERANCE)
            && NEAR(ThroughCopy(&Copies[0], (Vector3){ 2.0f, 0.0f, 0.0f }).Y, 0.5, PLACE_TOLERANCE));
    }
    Scene_Deconstruct(run, &S);
}

static void TheGroundChangingUnderItMovesItAndNothingElseDoes(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t GroundId = AddTerrain(run, &S, "floor", FlatHeight, (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldCollision_Sync(S.Collision);
        const PathNode Nodes[2] = { MakeNode(4.0, 1.0, 10.0, PathNodeMode_Corner), MakeNode(14.0, 1.0, 10.0,
            PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", true);
        Scene_Update(&S);
        Scene_Update(&S); // its own posts' colliders changed the world: a look at the ground that changes nothing
        size_t Count = 0U;
        const PropertyInstanceEntry* Copies = GetCopies(&S, Id, "tbp:panel", &Count);
        TEST_CHECK(run, (Count == 5U) && NEAR(ThroughCopy(&Copies[0], (Vector3){ 0.0f, 0.0f, 0.0f }).Y, 0.0, PLACE_TOLERANCE));
        // Nothing happening is nothing done, tick after tick: it does not loop on its own colliders.
        uint64_t Rebuilds = WorldPathObjects_GetStats(S.System).RebuildCount;
        for (int Tick = 0; Tick < 5; Tick++)
        {
            Scene_Update(&S);
        }
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).RebuildCount == Rebuilds);

        // The terrain raised: it follows at the next look.
        WorldObject* Ground = NULL;
        TEST_CHECK(run, World_TryGetObject(&S.World, GroundId, &Ground));
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Ground, TestWorld_At((Vector3){ 0.0f, 2.0f, 0.0f })));
        Scene_Update(&S);
        Copies = GetCopies(&S, Id, "tbp:panel", &Count);
        TEST_CHECK(run, NEAR(ThroughCopy(&Copies[0], (Vector3){ 0.0f, 0.0f, 0.0f }).Y, 2.0, PLACE_TOLERANCE));

        // The object moved: what snaps looks again, and stays on the ground: its children lie in its space.
        WorldObject* Object = NULL;
        TEST_CHECK(run, World_TryGetObject(&S.World, Id, &Object));
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, TestWorld_At((Vector3){ 2.0f, 1.5f, 0.0f })));
        Scene_Update(&S);
        Copies = GetCopies(&S, Id, "tbp:panel", &Count);
        TEST_CHECK(run, NEAR(ThroughCopy(&Copies[0], (Vector3){ 0.0f, 0.0f, 0.0f }).Y, 0.5, PLACE_TOLERANCE));

        // Something static elsewhere changes nothing for what snaps to terrain alone.
        Rebuilds = WorldPathObjects_GetStats(S.System).RebuildCount;
        WorldObject* Box = NULL;
        WorldComponent* Collider = NULL;
        TEST_CHECK_SUCCESS(run, World_CreateObject(&S.World, (const unsigned char*)u8"box", &Box));
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Box, TestWorld_At((Vector3){ 8.0f, 2.5f, 10.0f })));
        TEST_CHECK_SUCCESS(run, World_AddComponent(&S.World, WorldObject_GetId(Box), WorldComponentType_Collider, &Collider));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetUsesOwnShape(WorldColliderComponent_FromComponent(Collider), true));
        Scene_Update(&S);
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).RebuildCount == Rebuilds);

        // On chosen layers, it stands on what is there, and looks again when that goes.
        SetRow(run, &S, Id, "overrides_ground_target", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "ground_target", PropertyValue_FromEnum(PathGroundTarget_Layers));
        Scene_Update(&S);
        Copies = GetCopies(&S, Id, "tbp:panel", &Count);
        double Highest = -1.0e9;
        for (size_t Index = 0U; Index < Count; Index++)
        {
            Highest = fmax(Highest, ThroughCopy(&Copies[Index], (Vector3){ 0.0f, 0.0f, 0.0f }).Y);
        }
        TEST_CHECK(run, Highest > 1.0);
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&S.World, WorldObject_GetId(Box)));
        Scene_Update(&S);
        Copies = GetCopies(&S, Id, "tbp:panel", &Count);
        Highest = -1.0e9;
        for (size_t Index = 0U; Index < Count; Index++)
        {
            Highest = fmax(Highest, ThroughCopy(&Copies[Index], (Vector3){ 0.0f, 0.0f, 0.0f }).Y);
        }
        TEST_CHECK(run, NEAR(Highest, 0.5, PLACE_TOLERANCE));
    }
    Scene_Deconstruct(run, &S);
}

static void TheCollisionWorldReportsGroundChanges(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Start = WorldCollision_GetGroundRevision(S.Collision);
        BoundingBoxDouble Here = { .Min = TestWorld_At((Vector3){ -1.0f, -1.0f, -1.0f }), .Max = TestWorld_At((Vector3){ 1.0f,
            1.0f, 1.0f }) };
        BoundingBoxDouble Away = { .Min = TestWorld_At((Vector3){ 50.0f, -1.0f, 50.0f }), .Max = TestWorld_At((Vector3){ 51.0f,
            1.0f, 51.0f }) };
        TEST_CHECK(run, !WorldCollision_HasGroundChanged(S.Collision, Start, &Here, false));
        WorldObject* Box = NULL;
        WorldComponent* Collider = NULL;
        TEST_CHECK_SUCCESS(run, World_CreateObject(&S.World, (const unsigned char*)u8"box", &Box));
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Box, TestWorld_At((Vector3){ 0.0f, 50.0f, 0.0f })));
        TEST_CHECK_SUCCESS(run, World_AddComponent(&S.World, WorldObject_GetId(Box), WorldComponentType_Collider, &Collider));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetUsesOwnShape(WorldColliderComponent_FromComponent(Collider), true));
        WorldCollision_Sync(S.Collision);
        // From above: the box is 50 m up, in the area all the same; another place did not change; a terrain did not either.
        uint64_t Added = WorldCollision_GetGroundRevision(S.Collision);
        TEST_CHECK(run, (Added > Start) && WorldCollision_HasGroundChanged(S.Collision, Start, &Here, false));
        TEST_CHECK(run, !WorldCollision_HasGroundChanged(S.Collision, Start, &Away, false));
        TEST_CHECK(run, !WorldCollision_HasGroundChanged(S.Collision, Start, &Here, true));
        TEST_CHECK(run, !WorldCollision_HasGroundChanged(S.Collision, Added, &Here, false));
        // Moved away: both places changed. Movable things are no ground.
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Box, TestWorld_At((Vector3){ 50.5f, 0.0f, 50.5f })));
        WorldCollision_Sync(S.Collision);
        TEST_CHECK(run, WorldCollision_HasGroundChanged(S.Collision, Added, &Here, false)
            && WorldCollision_HasGroundChanged(S.Collision, Added, &Away, false));
        uint64_t Moved = WorldCollision_GetGroundRevision(S.Collision);
        TEST_CHECK_SUCCESS(run, WorldObject_SetMobility(Box, WorldMobility_Movable));
        WorldCollision_Sync(S.Collision);
        uint64_t AfterMobile = WorldCollision_GetGroundRevision(S.Collision);
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Box, TestWorld_At((Vector3){ 40.0f, 0.0f, 40.0f })));
        WorldCollision_Sync(S.Collision);
        TEST_CHECK(run, (AfterMobile > Moved) && (WorldCollision_GetGroundRevision(S.Collision) == AfterMobile));
        // Further back than it remembers, it says yes.
        TEST_CHECK_SUCCESS(run, WorldObject_SetMobility(Box, WorldMobility_Static));
        for (int Step = 0; Step < 40; Step++)
        {
            TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Box, TestWorld_At((Vector3){ 40.0f + (float)Step, 0.0f, 40.0f })));
            WorldCollision_Sync(S.Collision);
        }
        BoundingBoxDouble Nowhere = { .Min = TestWorld_At((Vector3){ -900.0f, 0.0f, -900.0f }),
            .Max = TestWorld_At((Vector3){ -899.0f, 0.0f, -899.0f }) };
        TEST_CHECK(run, WorldCollision_HasGroundChanged(S.Collision, Start, &Nowhere, false));
        // But a reader of terrains alone knows no terrain changed in all that; one that does, it sees.
        TEST_CHECK(run, !WorldCollision_HasGroundChanged(S.Collision, Start, &Nowhere, true));
        AddTerrain(run, &S, "slope", RisingHeight, (Vector3){ 900.0f, 0.0f, 900.0f });
        WorldCollision_Sync(S.Collision);
        for (int Step = 0; Step < 40; Step++)
        {
            TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Box, TestWorld_At((Vector3){ 40.0f, 0.0f, 40.0f + (float)Step })));
            WorldCollision_Sync(S.Collision);
        }
        TEST_CHECK(run, WorldCollision_HasGroundChanged(S.Collision, Start, &Nowhere, true));
        TEST_CHECK(run, !WorldCollision_HasGroundChanged(S.Collision, WorldCollision_GetGroundRevision(S.Collision) - 1U,
            &Nowhere, false));
    }
    Scene_Deconstruct(run, &S);
}


static void BentPiecesBecomeAGeneratedModelPerStretch(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        // A quarter of a circle of 60 m radius: about 94 m, three stretches, four with rounding.
        PathNode Nodes[9];
        for (size_t Index = 0U; Index < 9U; Index++)
        {
            double Angle = ((double)Index / 8.0) * 1.5707963267948966;
            Nodes[Index] = MakeNode(60.0 * cos(Angle), 0.0, 60.0 * sin(Angle), PathNodeMode_Smooth);
        }
        uint64_t Id = AddFence(run, &S, (Vector3){ 3.0f, 1.0f, -2.0f }, Nodes, 9U, "hedge", false);
        WorldPathObjects_Update(S.System);
        WorldPathObjectsStats Stats = WorldPathObjects_GetStats(S.System);
        TEST_CHECK(run,
            (Stats.GeneratedModelCount >= 3U) && (Stats.GeneratedModelCount <= 4U) && (Stats.GeneratedTriangleCount > 1000U)
            && (Stats.CopyCount == 0U));
        World_UpdateTransforms(&S.World);
        for (size_t Part = 0U; Part < Stats.GeneratedModelCount; Part++)
        {
            WorldModelComponent* Component = FindPart(&S, Id, Part);
            TEST_CHECK(run, Component != NULL);
            if (Component == NULL)
            {
                continue;
            }
            const WorldGeneratedModel* Generated = WorldModelComponent_GetGeneratedModel(Component);
            TEST_CHECK(run, (Generated != NULL) && (Generated->LodCount == 2U));
            TEST_CHECK(run, strcmp((const char*)WorldModelComponent_GetWorldModel(Component), "tbp:rail") == 0);
            // A LOD slices coarser than the one before.
            const WorldGeneratedLod* Fine = &Generated->Lods[0];
            const WorldGeneratedLod* Coarse = &Generated->Lods[1];
            TEST_CHECK(run,
                (Fine->MeshCount == 1U) && (Coarse->MeshCount == 1U)
                && (Coarse->Meshes[0].VertexCount < Fine->Meshes[0].VertexCount));
            // Every vertex lies on the curve, in the world, within the piece's width: a nearest point in plan.
            const WorldObject* Child = WorldComponent_GetOwner(&Component->Base);
            Matrix4Double ToWorld = WorldObject_GetWorldMatrix(Child);
            const PathCurve* Curve = WorldPathComponent_GetCurve(GetPath(&S, Id));
            for (uint32_t Vertex = 0U; Vertex < Fine->Meshes[0].VertexCount; Vertex += 7U)
            {
                const float* P = &Fine->Meshes[0].Positions[(size_t)Vertex * 3U];
                Vector3Double InWorld = Matrix4Double_TransformPoint(&ToWorld, Vector3Double_Create((double)P[0], (double)P[1],
                    (double)P[2]));
                Vector3 Scene3 = TestWorld_ToScene(InWorld);
                PathCurvePoint Nearest;
                double Separation = 1.0e9;
                TEST_CHECK(run, PathCurve_FindNearest(Curve, Vector3Double_Create((double)Scene3.x - 3.0, 0.0,
                    (double)Scene3.z + 2.0), true, &Nearest,
                    &Separation));
                TEST_CHECK(run, (Separation <= 0.1005) && (Scene3.y >= 0.99f) && (Scene3.y <= 2.01f));
            }
            // Its bounds hold what it holds; a stretch is about 32 m long at most.
            TEST_CHECK(run, Generated->Radius < 30.0f);
            for (uint32_t Vertex = 0U; Vertex < Fine->Meshes[0].VertexCount; Vertex += 7U)
            {
                const float* P = &Fine->Meshes[0].Positions[(size_t)Vertex * 3U];
                TEST_CHECK(run, Vector3Distance((Vector3){ P[0], P[1], P[2] },
                    Generated->Center) <= (Generated->Radius + 1.0e-3f));
            }
        }
        // A rebuild to the same place writes nothing: the meshes stay, byte for byte.
        WorldModelComponent* First = FindPart(&S, Id, 0U);
        uint32_t Revision = WorldModelComponent_GetGeneratedRevision(First);
        TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNode(GetPath(&S, Id), 3U, &Nodes[3]));
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, WorldModelComponent_GetGeneratedRevision(First) == Revision);
        // A node moved far along makes a new mesh.
        Nodes[1].Position.Y += 1.0;
        TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNode(GetPath(&S, Id), 1U, &Nodes[1]));
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, WorldModelComponent_GetGeneratedRevision(First) != Revision);
    }
    Scene_Deconstruct(run, &S);
}

static void ACutEndIsAGeneratedMeshEvenForARigidSlot(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(11.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", false);
        SetRow(run, &S, Id, "overrides_fit", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "fit", PropertyValue_FromEnum(PathObjectFit_ResizeEnds));
        SetRow(run, &S, Id, "overrides_resize_method", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "resize_method", PropertyValue_FromEnum(PathObjectResizeMethod_Cut));
        WorldPathObjects_Update(S.System);
        // Four whole panels as copies; the two cut ones are meshes, 1.5 m long each.
        size_t Count = 0U;
        GetCopies(&S, Id, "tbp:panel", &Count);
        TEST_CHECK(run, Count == 4U);
        WorldModelComponent* Part = FindPart(&S, Id, 0U);
        TEST_CHECK(run, Part != NULL);
        if (Part != NULL)
        {
            const WorldGeneratedModel* Generated = WorldModelComponent_GetGeneratedModel(Part);
            TEST_CHECK(run, (Generated != NULL) && NEAR(Generated->Box.max.y - Generated->Box.min.y, 1.0, 1.0e-3));
            // The first stands from 0 to 1.5; the part holds it relative to its own origin.
            const WorldObject* Child = WorldComponent_GetOwner(&Part->Base);
            double Near = (double)Generated->Box.min.x + WorldObject_GetPosition(Child).X;
            double Far = (double)Generated->Box.max.x + WorldObject_GetPosition(Child).X;
            TEST_CHECK(run, NEAR(Near, 0.0, PLACE_TOLERANCE) || NEAR(Near, 9.5, PLACE_TOLERANCE));
            TEST_CHECK(run, NEAR(Far - Near, 1.5, PLACE_TOLERANCE) || NEAR(Far - Near, 11.0, PLACE_TOLERANCE));
        }
    }
    Scene_Deconstruct(run, &S);
}


static void TheSlotsSayWhatCollides(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", false);
        Scene_Update(&S);
        // The posts' world model has "bounds"; the panel's has no collision section: they stand free.
        WorldRayHit Hit;
        TEST_CHECK(run, RayDownHits(&S, (Vector3){ 2.0f, 5.0f, 0.0f }, &Hit) && NEAR(Hit.Distance, 4.0, 1.0e-3));
        TEST_CHECK(run, !RayDownHits(&S, (Vector3){ 1.0f, 5.0f, 0.0f }, &Hit));
        // The middle in boxes: one around each panel.
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesCollision = true;
        Record.Collision = PathObjectCollision_Boxes;
        PropertyRecordList* List = NULL;
        TEST_CHECK_SUCCESS(run, PropertyRecordList_Create(PathObject_GetSlotPropertyTable(), &Record, 1U, &List));
        SetRow(run, &S, Id, "slots", PropertyValue_FromRecordList(List));
        PropertyRecordList_Destroy(List);
        Scene_Update(&S);
        TEST_CHECK(run, RayDownHits(&S, (Vector3){ 1.0f, 5.0f, 0.0f }, &Hit) && NEAR(Hit.Distance, 4.0, 1.0e-3));
        // Its layer and mask are the slot's: rays on other layers go through it.
        Record.OverridesCollisionLayerValue = true;
        Record.CollisionLayerValue = CollisionLayer_Vehicle;
        TEST_CHECK_SUCCESS(run, PropertyRecordList_Create(PathObject_GetSlotPropertyTable(), &Record, 1U, &List));
        SetRow(run, &S, Id, "slots", PropertyValue_FromRecordList(List));
        PropertyRecordList_Destroy(List);
        Scene_Update(&S);
        WorldCollisionFilter Filter = WorldCollisionFilter_Create(COLLISION_LAYER_BIT(CollisionLayer_Prop),
            WorldCollisionTriggerMode_Ignore);
        TEST_CHECK(run, !WorldCollision_Raycast(S.Collision, TestWorld_At((Vector3){ 1.0f, 5.0f, 0.0f }), (Vector3){ 0.0f, -1.0f,
            0.0f }, 20.0f,
            &Filter, &Hit));
        Filter = WorldCollisionFilter_Create(COLLISION_LAYER_BIT(CollisionLayer_Vehicle), WorldCollisionTriggerMode_Ignore);
        TEST_CHECK(run, WorldCollision_Raycast(S.Collision, TestWorld_At((Vector3){ 1.0f, 5.0f, 0.0f }), (Vector3){ 0.0f, -1.0f,
            0.0f }, 20.0f,
            &Filter, &Hit));
        // None: gone from the world at the next sync.
        Record.Collision = PathObjectCollision_None;
        TEST_CHECK_SUCCESS(run, PropertyRecordList_Create(PathObject_GetSlotPropertyTable(), &Record, 1U, &List));
        SetRow(run, &S, Id, "slots", PropertyValue_FromRecordList(List));
        PropertyRecordList_Destroy(List);
        Scene_Update(&S);
        TEST_CHECK(run, !RayDownHits(&S, (Vector3){ 1.0f, 5.0f, 0.0f }, &Hit));
        TEST_CHECK(run, RayDownHits(&S, (Vector3){ 2.0f, 5.0f, 0.0f }, &Hit));
    }
    Scene_Deconstruct(run, &S);
}

/* What a slot says of its collision reaches its collider: which pieces, a box's size and place, the mask. */
static void TheSlotsCollisionSettingsReachTheCollider(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", false);
        // The separators' posts on none: only the caps' collide.
        PathObjectSlotRecord Records[2] = { PathObjectSlotRecord_CreateDefault(PathObjectSlot_Separator),
            PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle) };
        Records[0].OverridesCollision = true;
        Records[0].Collision = PathObjectCollision_None;
        Records[1].OverridesCollision = true;
        Records[1].Collision = PathObjectCollision_Boxes;
        Records[1].OverridesBoxSize = true;
        Records[1].BoxSize = (Vector3){ 0.0f, 3.0f, 0.5f };
        Records[1].OverridesCollidesWith = true;
        Records[1].CollidesWith = COLLISION_LAYER_BIT(CollisionLayer_Character);
        SetSlots(run, &S, Id, Records, 2U);
        Scene_Update(&S);
        WorldRayHit Hit;
        TEST_CHECK(run, RayDownHits(&S, (Vector3){ 0.0f, 10.0f, 0.0f }, &Hit));
        WorldColliderComponent* Panels = FindCollider(&S, Id, "tbp:panel");
        TEST_CHECK(run,
            (Panels != NULL)
            && (WorldColliderComponent_GetCollidesWith(Panels) == COLLISION_LAYER_BIT(CollisionLayer_Character)));
        // The box: 3 high around the mesh's middle, half a metre up: its top at 2 m.
        TEST_CHECK(run, RayDownHits(&S, (Vector3){ 1.0f, 10.0f, 0.0f }, &Hit) && NEAR(Hit.Distance, 8.0, 1.0e-3));
        const WorldColliderPlacedShapes* Shapes = (Panels != NULL) ? WorldColliderComponent_GetPlacedShapes(Panels) : NULL;
        TEST_CHECK(run, (Shapes != NULL) && (Shapes->Count == 5U) && NEAR(Shapes->Shapes[0].HalfExtents.y, 1.5, 1.0e-6)
            && NEAR(Shapes->Shapes[0].HalfExtents.z, 0.25, 1.0e-6));
        // Only a box's size changes: same placements, new shapes.
        Records[1].BoxSize = (Vector3){ 0.0f, 5.0f, 0.5f };
        SetSlots(run, &S, Id, Records, 2U);
        Scene_Update(&S);
        Shapes = (Panels != NULL) ? WorldColliderComponent_GetPlacedShapes(Panels) : NULL;
        TEST_CHECK(run, (Shapes != NULL) && NEAR(Shapes->Shapes[0].HalfExtents.y, 2.5, 1.0e-6));
        TEST_CHECK(run, RayDownHits(&S, (Vector3){ 1.0f, 10.0f, 0.0f }, &Hit) && NEAR(Hit.Distance, 7.0, 1.0e-3));
        // A box's offset lifts it.
        Records[1].OverridesBoxOffset = true;
        Records[1].BoxOffset = (Vector3){ 0.0f, 2.0f, 0.0f };
        SetSlots(run, &S, Id, Records, 2U);
        Scene_Update(&S);
        TEST_CHECK(run, RayDownHits(&S, (Vector3){ 1.0f, 10.0f, 0.0f }, &Hit) && NEAR(Hit.Distance, 5.0, 1.0e-3));
    }
    Scene_Deconstruct(run, &S);
}

/* A separator on none does not collide, while the caps' posts do. */
static void ASlotOnNoneDoesNotCollide(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", false);
        Scene_Update(&S);
        WorldRayHit Hit;
        TEST_CHECK(run, RayDownHits(&S, (Vector3){ 2.0f, 10.0f, 0.0f }, &Hit));
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Separator);
        Record.OverridesCollision = true;
        Record.Collision = PathObjectCollision_None;
        SetSlots(run, &S, Id, &Record, 1U);
        Scene_Update(&S);
        TEST_CHECK(run, !RayDownHits(&S, (Vector3){ 2.0f, 10.0f, 0.0f }, &Hit));
        TEST_CHECK(run, RayDownHits(&S, (Vector3){ 0.0f, 10.0f, 0.0f }, &Hit));
    }
    Scene_Deconstruct(run, &S);
}

/* A slot picks a collision set by name; one its world model lacks does not collide. */
static void ASlotPicksACollisionSet(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "gateposts", false);
        Scene_Update(&S);
        WorldRayHit Hit;
        TEST_CHECK(run, RayDownHits(&S, (Vector3){ 0.0f, 10.0f, 0.0f }, &Hit) && NEAR(Hit.Distance, 8.0, 1.0e-3));
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesLength = true;
        Record.Length = 0.0f;
        Record.OverridesCollisionSet = true;
        Record.CollisionSet = (unsigned char*)u8"tall";
        SetSlots(run, &S, Id, &Record, 1U);
        Scene_Update(&S);
        TEST_CHECK(run, RayDownHits(&S, (Vector3){ 0.0f, 10.0f, 0.0f }, &Hit) && NEAR(Hit.Distance, 4.0, 1.0e-3));
        Record.CollisionSet = (unsigned char*)u8"missing";
        SetSlots(run, &S, Id, &Record, 1U);
        Scene_Update(&S);
        TEST_CHECK(run, !RayDownHits(&S, (Vector3){ 0.0f, 10.0f, 0.0f }, &Hit));
    }
    Scene_Deconstruct(run, &S);
}

/* A child holds at most so many placed shapes: past it, the copies are still drawn. */
static void AChildHoldsSoManyPlacedShapes(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(7000.0, 0.0, 0.0,
            PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "gateposts", false);
        SetRow(run, &S, Id, "overrides_gap", PropertyValue_FromBool(true));
        SetRow(run, &S, Id, "gap", PropertyValue_FromFloat(0.1f));
        WorldPathObjects_Update(S.System);
        WorldPathObjectsStats Stats = WorldPathObjects_GetStats(S.System);
        TEST_CHECK(run,
            (Stats.CopyCount > WORLD_PATH_OBJECTS_MAX_PLACED_SHAPES)
            && (Stats.PlacedShapeCount == WORLD_PATH_OBJECTS_MAX_PLACED_SHAPES));
    }
    Scene_Deconstruct(run, &S);
}

/* Boxes along a bent piece, a box's spacing apart. */
static void ABentPieceHasBoxesAlongIt(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(8.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "hedge", false);
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesCollision = true;
        Record.Collision = PathObjectCollision_Boxes;
        Record.OverridesBoxSpacing = true;
        Record.BoxSpacing = 0.25f;
        SetSlots(run, &S, Id, &Record, 1U);
        WorldPathObjects_Update(S.System);
        // Eight pieces of a metre, four boxes each; no model instances beside them, for the pieces are bent.
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).PlacedShapeCount == 32U);
        TEST_CHECK(run, FindCopiesChild(&S, Id, "tbp:rail") == NULL);
        // A tiny spacing asks for far more boxes than a child holds: it stops there.
        Record.BoxSpacing = 1.0e-6f;
        SetSlots(run, &S, Id, &Record, 1U);
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).PlacedShapeCount == WORLD_PATH_OBJECTS_MAX_PLACED_SHAPES);
    }
    Scene_Deconstruct(run, &S);
}

/* A slot that stops drawing rigid copies but still collides keeps its collider and loses its copies. */
static void WhatNoLongerHasCopiesLosesItsCopiesComponent(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "long_panels", false);
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesCollision = true;
        Record.Collision = PathObjectCollision_Boxes;
        SetSlots(run, &S, Id, &Record, 1U);
        WorldPathObjects_Update(S.System);
        TEST_CHECK(run, FindCopiesChild(&S, Id, "tbp:long_panel") != NULL);
        Record.OverridesCurve = true;
        Record.Curve = PathObjectCurve_Bent;
        SetSlots(run, &S, Id, &Record, 1U);
        WorldPathObjects_Update(S.System);
        // Its copies have gone to a part; what collides stays on the slot's child, which holds no copies any more.
        TEST_CHECK(run, FindCopiesChild(&S, Id, "tbp:long_panel") == NULL);
        TEST_CHECK(run, FindPart(&S, Id, 0U) != NULL);
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).PlacedShapeCount > 0U);
    }
    Scene_Deconstruct(run, &S);
}

/* A part follows its path: its place when only that differs, its collision with its mesh, and goes when its slot stops colliding
 * as a mesh. The mesh collides from inside too. */
static void APartsPlaceAndCollisionFollowThePath(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(8.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "hedge", false);
        Scene_Update(&S);
        WorldRayHit Hit;
        TEST_CHECK(run, RayDownHits(&S, (Vector3){ 4.0f, 10.0f, 0.0f }, &Hit) && NEAR(Hit.Distance, 9.0, 1.0e-3));
        // From inside, a ray still finds its roof.
        WorldCollisionFilter Filter = WorldCollisionFilter_Create(COLLISION_LAYER_MASK_ALL, WorldCollisionTriggerMode_Ignore);
        TEST_CHECK(run, WorldCollision_Raycast(S.Collision, TestWorld_At((Vector3){ 4.0f, 0.5f, 0.0f }), (Vector3){ 0.0f, 1.0f,
            0.0f }, 5.0f, &Filter, &Hit)
            && NEAR(Hit.Distance, 0.5, 1.0e-3));
        // The whole path 20 m along: the same meshes, relative to their part: its place moves, and its collision with it.
        Vector3Double Before = WorldObject_GetPosition(WorldComponent_GetOwner(&FindPart(&S, Id, 0U)->Base));
        const PathNode Shifted[2] = { MakeNode(20.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(28.0, 0.0, 0.0,
            PathNodeMode_Corner) };
        TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(GetPath(&S, Id), Shifted, 2U));
        Scene_Update(&S);
        Vector3Double After = WorldObject_GetPosition(WorldComponent_GetOwner(&FindPart(&S, Id, 0U)->Base));
        TEST_CHECK(run, NEAR(After.X - Before.X, 20.0, PLACE_TOLERANCE));
        TEST_CHECK(run, !RayDownHits(&S, (Vector3){ 4.0f, 10.0f, 0.0f }, &Hit));
        TEST_CHECK(run, RayDownHits(&S, (Vector3){ 24.0f, 10.0f, 0.0f }, &Hit) && NEAR(Hit.Distance, 9.0, 1.0e-3));
        // A longer path: other meshes, and the collision is theirs.
        const PathNode Longer[2] = { MakeNode(20.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(31.0, 0.0, 0.0,
            PathNodeMode_Corner) };
        TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(GetPath(&S, Id), Longer, 2U));
        Scene_Update(&S);
        TEST_CHECK(run, RayDownHits(&S, (Vector3){ 30.5f, 10.0f, 0.0f }, &Hit));
        // On none: the part stays and its collider goes.
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesCollision = true;
        Record.Collision = PathObjectCollision_None;
        SetSlots(run, &S, Id, &Record, 1U);
        Scene_Update(&S);
        TEST_CHECK(run, !RayDownHits(&S, (Vector3){ 24.0f, 10.0f, 0.0f }, &Hit));
        WorldModelComponent* Part = FindPart(&S, Id, 0U);
        TEST_CHECK(run, (Part != NULL) && (WorldObject_GetComponent(WorldComponent_GetOwner(&Part->Base),
            WorldComponentType_Collider) == NULL));
    }
    Scene_Deconstruct(run, &S);
}

static void ABentMeshCollidesAsItLies(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        PathNode Nodes[5];
        for (size_t Index = 0U; Index < 5U; Index++)
        {
            double Angle = ((double)Index / 4.0) * 1.5707963267948966;
            Nodes[Index] = MakeNode(20.0 * cos(Angle), 0.0, 20.0 * sin(Angle), PathNodeMode_Smooth);
        }
        AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 5U, "hedge", false);
        Scene_Update(&S);
        // Down onto the bent top at the arc's middle, where no straight copy would lie; nothing beside it.
        double Middle = 0.7853981633974483;
        WorldRayHit Hit;
        Vector3 On = { (float)(20.0 * cos(Middle)), 5.0f, (float)(20.0 * sin(Middle)) };
        TEST_CHECK(run, RayDownHits(&S, On, &Hit) && NEAR(Hit.Distance, 4.0, 1.0e-2));
        Vector3 Off = { (float)(20.5 * cos(Middle)), 5.0f, (float)(20.5 * sin(Middle)) };
        TEST_CHECK(run, !RayDownHits(&S, Off, &Hit));
        // Boxes along it instead: hit too, by a chord's stand-in.
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesCollision = true;
        Record.Collision = PathObjectCollision_Boxes;
        Record.OverridesBoxSpacing = true;
        Record.BoxSpacing = 1.0f;
        PropertyRecordList* List = NULL;
        TEST_CHECK_SUCCESS(run, PropertyRecordList_Create(PathObject_GetSlotPropertyTable(), &Record, 1U, &List));
        WorldComponent* Component = NULL;
        size_t Count = 0U;
        WorldComponent* const* All = World_GetComponents(&S.World, WorldComponentType_PathObject, &Count);
        Component = All[0];
        PropertyValue Value = PropertyValue_FromRecordList(List);
        TEST_CHECK_SUCCESS(run, Properties_Set(WorldPathObjectComponent_GetPropertyTable(), Component,
            (const unsigned char*)u8"slots", &Value));
        PropertyRecordList_Destroy(List);
        Scene_Update(&S);
        TEST_CHECK(run, RayDownHits(&S, On, &Hit) && NEAR(Hit.Distance, 4.0, 1.0e-2));
        TEST_CHECK(run, !RayDownHits(&S, Off, &Hit));
        TEST_CHECK(run, WorldPathObjects_GetStats(S.System).PlacedShapeCount > 20U);
    }
    Scene_Deconstruct(run, &S);
}

static void PlacedShapesStayValidUntilTheNextSync(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Id = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", false);
        Scene_Update(&S);
        // The fence is replaced and the old block given back by the collider: a query before the next sync still reads what it
        // was synced with, whole.
        const PathNode Moved = MakeNode(20.0, 0.0, 0.0, PathNodeMode_Corner);
        TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNode(GetPath(&S, Id), 1U, &Moved));
        WorldPathObjects_Update(S.System);
        WorldRayHit Hit;
        TEST_CHECK(run, RayDownHits(&S, (Vector3){ 2.0f, 5.0f, 0.0f }, &Hit));
        WorldCollision_Sync(S.Collision);
        TEST_CHECK(run, RayDownHits(&S, (Vector3){ 20.0f, 5.0f, 0.0f }, &Hit));
        // The placed block's references: the caller's and a holder's; the last frees it and what it owns.
        WorldColliderPlacedShapes* Block = WorldColliderPlacedShapes_Create(2U, 0U);
        TEST_CHECK(run, (Block->Count == 2U) && (Block->Shapes[1].Type == CollisionShapeType_Box));
        WorldColliderPlacedShapes_Retain(Block);
        WorldColliderPlacedShapes_Release(Block);
        WorldColliderPlacedShapes_Release(Block);
        WorldColliderPlacedShapes_Retain(NULL);
        WorldColliderPlacedShapes_Release(NULL);
    }
    Scene_Deconstruct(run, &S);
}


// Tests: the render cache.
static void TheRenderCacheDrawsWhatItMade(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        const PathNode Nodes[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner) };
        uint64_t Fence = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "fence", false);
        uint64_t Hedge = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 20.0f }, Nodes, 2U, "hedge", false);
        WorldPathObjects_Update(S.System);
        WorldRenderCache Cache;
        WorldRenderCache_Construct(&Cache);
        AssetUserID User;
        TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(S.Fixture.Manager, &User));
        TEST_CHECK_SUCCESS(run, WorldRenderCache_Sync(&Cache, &S.World, TestWorld_At((Vector3){ 0.0f, 0.0f, 0.0f }),
            S.Fixture.Manager, User, NULL));
        size_t InstanceCount = 0U;
        WorldRenderCache_GetInstances(&Cache, &InstanceCount);
        TEST_CHECK(run, InstanceCount == 4U);
        size_t ModelCount = 0U;
        const WorldRenderModel* Models = WorldRenderCache_GetModels(&Cache, &ModelCount);
        TEST_CHECK(run, ModelCount == 1U);
        if (ModelCount == 1U)
        {
            const WorldRenderModel* Entry = &Models[0];
            // Its sphere is the generated mesh's, in the world, not its world model's; until the renderer's copy no LOD draws.
            TEST_CHECK(run, (Entry->Generated != NULL) && Entry->IsDrawn);
            Vector3 Center = TestWorld_ToScene(Entry->Center);
            TEST_CHECK(run, NEAR(Center.x, 5.0, 0.1) && NEAR(Center.z, 20.0, 0.1) && NEAR(Entry->Radius, 5.1, 0.2));
            TEST_CHECK(run, WorldRenderModel_GetLodModel(Entry, 0U) == NULL);
            GameModel Held[2];
            Memory_Zero(Held, sizeof(Held));
            GameModel* const Lods[2] = { &Held[0], &Held[1] };
            WorldRenderCache_SetGeneratedLods(&Cache, 0U, Lods);
            TEST_CHECK(run, (WorldRenderModel_GetLodModel(Entry, 1U) == &Held[1]) && (WorldRenderModel_GetLodModel(Entry,
                2U) == NULL));
            Held[0]._rayModel.transform = MatrixIdentity();
            // A generated mesh lies in the object's space: its LOD takes no import transform of the world model's.
            Matrix Drawn = WorldRenderModel_GetLodTransform(Entry, 0U);
            TEST_CHECK(run, NEAR(Drawn.m13, Entry->RenderMatrix.m13, 1.0e-6));
            // It tells the renderer when the mesh was replaced, by the component's own count, and holds no LODs past the frame.
            WorldModelComponent* Part = FindPart(&S, Hedge, 0U);
            TEST_CHECK(run, (Entry->GeneratedRevision == WorldModelComponent_GetGeneratedRevision(Part))
                && (Entry->GeneratedRevision > 0U));
            TEST_CHECK_SUCCESS(run, WorldRenderCache_Sync(&Cache, &S.World, TestWorld_At((Vector3){ 0.0f, 0.0f, 0.0f }),
                S.Fixture.Manager, User, NULL));
            Models = WorldRenderCache_GetModels(&Cache, &ModelCount);
            TEST_CHECK(run, (ModelCount == 1U) && (Models[0].GeneratedLods == NULL));
        }
        (void)Fence;
        (void)Hedge;
        // A bent piece of a model with an import transform: the mesh has it already, and its sphere takes none on top.
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&S.World, Hedge));
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&S.World, Fence));
        WorldPathObjects_Update(S.System);
        uint64_t Long = AddFence(run, &S, (Vector3){ 0.0f, 0.0f, 0.0f }, Nodes, 2U, "long_panels", false);
        PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
        Record.OverridesCurve = true;
        Record.Curve = PathObjectCurve_Bent;
        SetSlots(run, &S, Long, &Record, 1U);
        WorldPathObjects_Update(S.System);
        TEST_CHECK_SUCCESS(run, WorldRenderCache_Sync(&Cache, &S.World, TestWorld_At((Vector3){ 0.0f, 0.0f, 0.0f }),
            S.Fixture.Manager, User, NULL));
        Models = WorldRenderCache_GetModels(&Cache, &ModelCount);
        TEST_CHECK(run, ModelCount == 1U);
        if (ModelCount == 1U)
        {
            Vector3 Center = TestWorld_ToScene(Models[0].Center);
            TEST_CHECK(run, NEAR(Center.x, 5.0, 0.1) && NEAR(Models[0].Radius, 5.0, 0.2));
        }
        WorldRenderCache_Deconstruct(&Cache);
    }
    Scene_Deconstruct(run, &S);
}


// Public functions.
void WorldPathObjectTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "a_path_object_makes_copies_under_its_object", APathObjectMakesCopiesUnderItsObject },
        { "a_child_reads_its_parents_path", AChildReadsItsParentsPath },
        { "every_change_rebuilds_live_and_only_real_ones_are_written", EveryChangeRebuildsLiveAndOnlyRealOnesAreWritten },
        { "a_placement_overrides_its_assets_settings", APlacementOverridesItsAssetsSettings },
        { "the_children_take_the_slots_render_settings_and_the_objects_tint",
            TheChildrenTakeTheSlotsRenderSettingsAndTheObjectsTint },
        { "what_it_made_follows_its_component_and_its_object", WhatItMadeFollowsItsComponentAndItsObject },
        { "pieces_stand_vertical_on_a_climbing_path", PiecesStandVerticalOnAClimbingPath },
        { "bent_pieces_stand_vertical_on_a_climbing_path", BentPiecesStandVerticalOnAClimbingPath },
        { "a_flip_turns_a_piece_end_for_end", AFlipTurnsAPieceEndForEnd },
        { "a_side_stands_off_the_path_by_the_nodes_width", ASideStandsOffThePathByTheNodesWidth },
        { "a_joint_stands_centred_on_its_node", AJointStandsCentredOnItsNode },
        { "a_piece_is_as_long_as_its_import_transform_makes_it", APieceIsAsLongAsItsImportTransformMakesIt },
        { "a_part_takes_the_objects_tint", APartTakesTheObjectsTint },
        { "a_generated_model_compares_what_it_holds", AGeneratedModelComparesWhatItHolds },
        { "a_generated_models_bounds_are_its_finest_lods", AGeneratedModelsBoundsAreItsFinestLods },
        { "a_bent_piece_takes_a_nodes_width_and_scale", ABentPieceTakesANodesWidthAndScale },
        { "a_turned_bent_piece_shows_its_other_end_first", ATurnedBentPieceShowsItsOtherEndFirst },
        { "a_turned_bent_piece_still_follows_the_curve", ATurnedBentPieceStillFollowsTheCurve },
        { "an_entry_depth_stretches_with_a_bent_piece", AnEntryDepthStretchesWithABentPiece },
        { "a_bent_piece_mitres_over_a_corner", ABentPieceMitresOverACorner },
        { "a_tilted_bent_piece_leans_with_the_ground", ATiltedBentPieceLeansWithTheGround },
        { "a_long_bent_piece_lies_in_several_parts", ALongBentPieceLiesInSeveralParts },
        { "a_cut_piece_keeps_its_own_part_of_the_mesh", ACutPieceKeepsItsOwnPartOfTheMesh },
        { "a_missing_asset_waits_for_a_new_name", AMissingAssetWaitsForANewName },
        { "the_worlds_pieces_have_a_budget", TheWorldsPiecesHaveABudget },
        { "samples_are_not_shared_beyond_their_section_and_spacing", SamplesAreNotSharedBeyondTheirSectionAndSpacing },
        { "the_worlds_vertices_and_shapes_have_budgets", TheWorldsVerticesAndShapesHaveBudgets },
        { "pieces_of_one_section_share_its_samples", PiecesOfOneSectionShareItsSamples },
        { "snapped_bent_pieces_meet", SnappedBentPiecesMeet },
        { "a_side_turns_a_corner_by_its_mitre", ASideTurnsACornerByItsMitre },
        { "a_corner_inside_a_bent_piece_keeps_its_slice", ACornerInsideABentPieceKeepsItsSlice },
        { "a_closed_bent_run_mitres_where_it_closes", AClosedBentRunMitresWhereItCloses },
        { "a_bent_pieces_collision_lies_as_its_mesh_does", ABentPiecesCollisionLiesAsItsMeshDoes },
        { "a_models_tint_reaches_a_part", AModelsTintReachesAPart },
        { "a_cut_on_a_turned_piece_stays_at_its_end_of_the_path", ACutOnATurnedPieceStaysAtItsEndOfThePath },
        { "every_part_of_a_long_piece_holds_something", EveryPartOfALongPieceHoldsSomething },
        { "a_stretch_with_nothing_in_it_makes_no_child", AStretchWithNothingInItMakesNoChild },
        { "a_hostile_piece_stays_within_its_limits", AHostilePieceStaysWithinItsLimits },
        { "looks_for_the_ground_have_a_limit", LooksForTheGroundHaveALimit },
        { "a_path_object_under_anothers_child_goes_with_it", APathObjectUnderAnothersChildGoesWithIt },
        { "a_part_of_nothing_has_no_collider", APartOfNothingHasNoCollider },
        { "what_lies_too_far_out_is_left_out", WhatLiesTooFarOutIsLeftOut },
        { "a_list_of_copies_fills_up_and_no_more", AListOfCopiesFillsUpAndNoMore },
        { "a_flat_object_finds_the_ground_when_it_grows_back", AFlatObjectFindsTheGroundWhenItGrowsBack },
        { "a_cap_stays_as_thick_on_a_slope", ACapStaysAsThickOnASlope },
        { "a_copys_collider_takes_its_models_trigger_flag", ACopysColliderTakesItsModelsTriggerFlag },
        { "its_own_colliders_do_not_keep_it_rebuilding", ItsOwnCollidersDoNotKeepItRebuilding },
        { "each_component_keeps_its_own_state", EachComponentKeepsItsOwnState },
        { "the_child_count_follows_children_removed_from_outside", TheChildCountFollowsChildrenRemovedFromOutside },
        { "the_system_going_takes_what_it_made", TheSystemGoingTakesWhatItMade },
        { "only_the_component_goes_into_world_files", OnlyTheComponentGoesIntoWorldFiles },
        { "a_rebuild_takes_the_definitions_as_they_are_now", ARebuildTakesTheDefinitionsAsTheyAreNow },
        { "pieces_stand_on_the_ground", PiecesStandOnTheGround },
        { "the_ground_changing_under_it_moves_it_and_nothing_else_does", TheGroundChangingUnderItMovesItAndNothingElseDoes },
        { "the_collision_world_reports_ground_changes", TheCollisionWorldReportsGroundChanges },
        { "bent_pieces_become_a_generated_model_per_stretch", BentPiecesBecomeAGeneratedModelPerStretch },
        { "a_cut_end_is_a_generated_mesh_even_for_a_rigid_slot", ACutEndIsAGeneratedMeshEvenForARigidSlot },
        { "the_slots_say_what_collides", TheSlotsSayWhatCollides },
        { "the_slots_collision_settings_reach_the_collider", TheSlotsCollisionSettingsReachTheCollider },
        { "a_slot_on_none_does_not_collide", ASlotOnNoneDoesNotCollide },
        { "a_slot_picks_a_collision_set", ASlotPicksACollisionSet },
        { "a_child_holds_so_many_placed_shapes", AChildHoldsSoManyPlacedShapes },
        { "a_bent_piece_has_boxes_along_it", ABentPieceHasBoxesAlongIt },
        { "what_no_longer_has_copies_loses_its_copies_component", WhatNoLongerHasCopiesLosesItsCopiesComponent },
        { "a_parts_place_and_collision_follow_the_path", APartsPlaceAndCollisionFollowThePath },
        { "a_bent_mesh_collides_as_it_lies", ABentMeshCollidesAsItLies },
        { "placed_shapes_stay_valid_until_the_next_sync", PlacedShapesStayValidUntilTheNextSync },
        { "the_render_cache_draws_what_it_made", TheRenderCacheDrawsWhatItMade },
    };
    TestWorld_RunSuite(run, "WorldPathObject", "WorldPathObjectFar", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
