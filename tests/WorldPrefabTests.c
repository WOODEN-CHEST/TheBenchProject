#include "TestSuites.h"
#include "TestAssets.h"
#include "TestWorld.h"
#include "AssetManager.h"
#include "DataValue.h"
#include "Logger.h"
#include "Prefab.h"
#include "World.h"
#include "WorldColliderComponent.h"
#include "WorldFile.h"
#include "WorldLightComponent.h"
#include "WorldModelComponent.h"
#include "WorldObject.h"
#include "WorldPathComponent.h"
#include "WorldPathObjectComponent.h"
#include "WorldPathObjects.h"
#include "WorldPrefabComponent.h"
#include "WorldPrefabs.h"
#include "WorldSpriteComponent.h"
#include "wr/WRFileSystem.h"
#include "wr/WRMemory.h"
#include "wr/WRPath.h"
#include "wr/WRString.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
#define NEAR(a, b, tolerance) (fabs((double)(a) - (double)(b)) <= (double)(tolerance))
/** Where a scene's log goes, under the run's work directory. */
#define LOG_DIRECTORY "world_prefab_tests"
#define DEGREES_TO_RADIANS (3.14159265358979323846 / 180.0)


// Fields.
/* A prefab with a bit of everything: tags, a nested hierarchy, a light, and a property of each way to reach a row. */
static const char* const StopPrefab =
    "{ \"name\": \"stop\", \"format_version\": 3,"
    "  \"properties\": {"
    "    \"roof_color\": { \"type\": \"color\", \"default\": \"#7a3b2e\", \"display_name\": \"Roof colour\", \"category\": \"Looks\","
    "        \"targets\": [ { \"tag\": \"tbp:roof\", \"row\": \"tint\" } ] },"
    "    \"layout\": { \"type\": \"enum\", \"default\": \"plain\", \"options\": {"
    "        \"plain\":  [ { \"object\": \"garage\", \"row\": \"is_enabled\", \"value\": false } ],"
    "        \"garage\": [ { \"object\": \"door\", \"row\": \"is_enabled\", \"value\": false } ] } },"
    "    \"door_angle\": { \"type\": \"angle\", \"default\": 0, \"minimum\": 0, \"maximum\": 110,"
    "        \"targets\": [ { \"object\": \"door\", \"row\": \"rotation\", \"axis\": \"y\" } ] },"
    "    \"lamp_range\": { \"type\": \"float\", \"default\": 5,"
    "        \"targets\": [ { \"object\": \"lamp\", \"component\": \"light\", \"row\": \"range\" } ] }"
    "  },"
    "  \"objects\": ["
    "    { \"id\": 1, \"name\": \"root\" },"
    "    { \"id\": 2, \"parent\": 1, \"name\": \"roof\", \"tags\": [\"tbp:roof\"], \"position\": [0, 3, 0] },"
    "    { \"id\": 3, \"parent\": 2, \"name\": \"roof_cap\", \"tags\": [\"tbp:roof\"], \"position\": [0, 0.5, 0] },"
    "    { \"id\": 4, \"parent\": 1, \"name\": \"garage\" },"
    "    { \"id\": 5, \"parent\": 1, \"name\": \"door\", \"position\": [1, 0, 0], \"rotation\": [10, 0, 20] },"
    "    { \"id\": 6, \"parent\": 1, \"name\": \"lamp\", \"components\": { \"light\": { \"range\": 2 } } },"
    "    { \"id\": 7, \"parent\": 6, \"name\": \"bulb\", \"position\": [0, 2, 0] }"
    "  ] }";

/* A prefab that holds a copy of the one above, and passes it a property. */
static const char* const ShelterPrefab =
    "{ \"name\": \"shelter\", \"format_version\": 3,"
    "  \"properties\": {"
    "    \"stop_roof\": { \"type\": \"color\", \"default\": \"#112233\","
    "        \"targets\": [ { \"object\": \"stop_a\", \"property\": \"roof_color\" } ] }"
    "  },"
    "  \"objects\": ["
    "    { \"id\": 1, \"name\": \"root\" },"
    "    { \"id\": 2, \"parent\": 1, \"name\": \"stop_a\", \"position\": [5, 0, 0],"
    "      \"components\": { \"prefab\": { \"prefab\": \"stop\", \"values\": { \"lamp_range\": 7 } } } },"
    "    { \"id\": 3, \"parent\": 1, \"name\": \"stop_b\", \"position\": [-5, 0, 0],"
    "      \"components\": { \"prefab\": { \"prefab\": \"stop\","
    "        \"object_overrides\": { \"bulb\": { \"position\": [0, 9, 0] }, \"lamp\": { \"components\": { \"light\": { \"intensity\": 2 } } } } } } }"
    "  ] }";


// Types.
typedef struct SceneStruct
{
    TestAssetFixture Fixture;
    World World;
    KeyRegistry Keys;
    WorldPrefabs* System;
    Logger Log;
    GenericBuffer LogDirectory;
    bool HasLog;
    bool IsWorldBuilt;
} Scene;


// Static functions: the scene.
static bool AddPrefab(TestRun* run, Scene* scene, const char* json)
{
    GenericBuffer Raw;
    GenericBuffer_AllocateVariable(&Raw, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Raw, (const unsigned char*)json);
    AssetDefinition* Definition = NULL;
    bool IsBuilt = TEST_CHECK_SUCCESS(run, AssetManager_BuildDefinition(scene->Fixture.Manager, scene->Fixture.Types.Prefab, &Raw,
        (const unsigned char*)u8"test prefab", &Definition));
    Memory_Free(Raw._data);
    return IsBuilt && TEST_CHECK_SUCCESS(run, AssetManager_SetDefinition(scene->Fixture.Manager, Definition));
}

static bool Scene_ConstructBare(TestRun* run, Scene* self)
{
    Memory_Zero(self, sizeof(*self));
    if (!TestAssetFixture_Construct(run, &self->Fixture))
    {
        return false;
    }
    GenericBuffer_AllocateVariable(&self->LogDirectory, 0U, sizeof(unsigned char));
    self->HasLog = TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, LOG_DIRECTORY, &self->LogDirectory))
        && TEST_CHECK_SUCCESS(run, Logger_Construct(&self->Log, self->LogDirectory._data));
    if (!self->HasLog)
    {
        return false;
    }
    self->IsWorldBuilt = TestWorld_Construct(run, &self->World, &self->Keys, self->Fixture.Manager, NULL);
    return self->IsWorldBuilt && TEST_CHECK_SUCCESS(run, WorldPrefabs_Create(&self->World, self->Fixture.Manager, &self->Log, &self->System));
}

static bool Scene_Construct(TestRun* run, Scene* self)
{
    return Scene_ConstructBare(run, self) && AddPrefab(run, self, StopPrefab) && AddPrefab(run, self, ShelterPrefab);
}

static void Scene_Deconstruct(TestRun* run, Scene* self)
{
    WorldPrefabs_Destroy(self->System);
    if (self->IsWorldBuilt)
    {
        TestWorld_Deconstruct(run, &self->World, &self->Keys);
    }
    if (self->HasLog)
    {
        TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&self->Log));
    }
    Memory_Free(self->LogDirectory._data);
    TestAssetFixture_Deconstruct(run, &self->Fixture);
}

/* How often @p needle stands in the scene's log so far. */
static size_t CountInLog(TestRun* run, const Scene* scene, const char* needle)
{
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, 0U, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, 0U, sizeof(unsigned char));
    size_t Count = 0U;
    if (TEST_CHECK_SUCCESS(run, Path_Append(scene->LogDirectory._data, (const unsigned char*)u8"logs/latest.log", &Path))
        && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, &Text)))
    {
        GenericBuffer_AppendByte(&Text, 0U);
        for (const char* At = strstr((const char*)Text._data, needle); At != NULL; At = strstr(At + 1, needle))
        {
            Count++;
        }
    }
    Memory_Free(Path._data);
    Memory_Free(Text._data);
    return Count;
}

/* A copy: a root object placed by the scene, with the component. */
static uint64_t AddCopy(TestRun* run, Scene* scene, const char* name, const char* prefab, Vector3 at)
{
    WorldObject* Root = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(&scene->World, (const unsigned char*)name, &Root))
        || !TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Root, TestWorld_At(at))))
    {
        return 0U;
    }
    uint64_t Id = WorldObject_GetId(Root);
    WorldComponent* Component = NULL;
    if (TEST_CHECK_SUCCESS(run, World_AddComponent(&scene->World, Id, WorldComponentType_Prefab, &Component)))
    {
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetPrefab(WorldPrefabComponent_FromComponent(Component), (const unsigned char*)prefab));
    }
    return Id;
}

static WorldPrefabComponent* GetCopy(Scene* scene, uint64_t rootId)
{
    WorldComponent* Component = NULL;
    return World_TryGetComponent(&scene->World, rootId, WorldComponentType_Prefab, &Component)
        ? WorldPrefabComponent_FromComponent(Component) : NULL;
}

/* An object of a copy by its path; NULL when the copy does not hold it. */
static WorldObject* FindPart(Scene* scene, uint64_t rootId, const char* path)
{
    uint64_t Id = 0U;
    WorldObject* Object = NULL;
    return (WorldPrefabs_TryFindObject(scene->System, rootId, (const unsigned char*)path, &Id)
        && World_TryGetObject(&scene->World, Id, &Object)) ? Object : NULL;
}

static bool SetStringValue(TestRun* run, Scene* scene, uint64_t rootId, const char* name, const char* text)
{
    DataValue Value = DataValue_CreateTag();
    bool IsMade = TEST_CHECK_SUCCESS(run, DataValue_CreateString((const unsigned char*)text, &Value))
        && TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetValue(GetCopy(scene, rootId), (const unsigned char*)name, &Value));
    DataValue_Deconstruct(&Value);
    return IsMade;
}

static bool SetNumberValue(TestRun* run, Scene* scene, uint64_t rootId, const char* name, double number)
{
    DataValue Value = DataValue_FromFloat(number);
    return TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetValue(GetCopy(scene, rootId), (const unsigned char*)name, &Value));
}

/* The list [x, y, z] of floats. */
static DataValue MakeVector(TestRun* run, double x, double y, double z)
{
    DataValue List = DataValue_CreateTag();
    TEST_CHECK_SUCCESS(run, DataValue_CreateList(DataType_Float, &List));
    const double Parts[3] = { x, y, z };
    for (size_t Index = 0U; Index < 3U; Index++)
    {
        DataValue Part = DataValue_FromFloat(Parts[Index]);
        TEST_CHECK_SUCCESS(run, DataValue_ListAdd(&List, &Part));
    }
    return List;
}

static bool SetVectorRow(TestRun* run, Scene* scene, uint64_t rootId, const char* path, const char* row, double x, double y, double z)
{
    DataValue Vector = MakeVector(run, x, y, z);
    bool IsSet = TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetObjectRow(GetCopy(scene, rootId), (const unsigned char*)path, NULL,
        (const unsigned char*)row, &Vector));
    DataValue_Deconstruct(&Vector);
    return IsSet;
}

static RenderColor GetTint(const WorldObject* object)
{
    return (object != NULL) ? WorldObject_GetTint(object) : (RenderColor){ { 0, 0, 0, 0 }, 0.0f, 0.0f };
}

static float GetLampRange(Scene* scene, uint64_t rootId, const char* path)
{
    WorldObject* Lamp = FindPart(scene, rootId, path);
    return (Lamp != NULL) ? WorldLightComponent_FromComponent(WorldObject_GetComponent(Lamp, WorldComponentType_Light))->_range : -1.0f;
}


// Tests: copies.
static void ACopyMakesItsObjectsUnderItsRoot(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "stop_1", "stop", (Vector3){ 10.0f, 1.0f, -3.0f });
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_IsSettled(S.System));
        WorldPrefabsStats Stats = WorldPrefabs_GetStats(S.System);
        TEST_CHECK(run, (Stats.CopyCount == 1U) && (Stats.ObjectCount == 7U));
        WorldObject* PrefabRoot = FindPart(&S, Root, "root");
        WorldObject* Cap = FindPart(&S, Root, "roof_cap");
        WorldObject* Bulb = FindPart(&S, Root, "bulb");
        TEST_CHECK(run, (PrefabRoot != NULL) && (Cap != NULL) && (Bulb != NULL));
        if ((PrefabRoot != NULL) && (Cap != NULL) && (Bulb != NULL))
        {
            WorldObject* RootObject = NULL;
            World_TryGetObject(&S.World, Root, &RootObject);
            TEST_CHECK(run, WorldObject_GetParent(PrefabRoot) == RootObject);
            TEST_CHECK(run, WorldObject_GetParent(Cap) == FindPart(&S, Root, "roof"));
            TEST_CHECK(run, WorldObject_GetParent(Bulb) == FindPart(&S, Root, "lamp"));
            TEST_CHECK(run, !WorldObject_IsPersistent(PrefabRoot) && !WorldObject_IsPersistent(Bulb));
            Vector3Double CapPlace = WorldObject_GetWorldPosition(Cap);
            Vector3 InScene = TestWorld_ToScene(CapPlace);
            TEST_CHECK(run, NEAR(InScene.x, 10.0, 1e-3) && NEAR(InScene.y, 4.5, 1e-3) && NEAR(InScene.z, -3.0, 1e-3));
        }
        TEST_CHECK(run, NEAR(GetLampRange(&S, Root, "lamp"), 5.0, 1e-6)); // the property's default over the template's 2
    }
    Scene_Deconstruct(run, &S);
}

static void ItsIdsComeFromTheRootAndTheName(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Before = World_GetNextObjectId(&S.World);
        uint64_t First = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        uint64_t Second = AddCopy(run, &S, "b", "stop", (Vector3){ 9.0f, 0.0f, 0.0f });
        uint64_t AfterRoots = World_GetNextObjectId(&S.World);
        WorldPrefabs_Update(S.System);
        // the counter stays: a saved world's next id does not grow with what copies make
        TEST_CHECK(run, AfterRoots == (Before + 2U));
        TEST_CHECK(run, World_GetNextObjectId(&S.World) == AfterRoots);
        WorldObject* Bench = FindPart(&S, First, "lamp");
        TEST_CHECK(run, Bench != NULL);
        if (Bench != NULL)
        {
            TEST_CHECK(run, WorldObject_GetId(Bench) == WorldPrefabs_DeriveObjectId(First, (const unsigned char*)u8"lamp", 0U));
            TEST_CHECK(run, WorldObject_GetId(Bench) >= WORLD_OBJECT_DERIVED_ID_FIRST);
        }
        TEST_CHECK(run, WorldObject_GetId(FindPart(&S, First, "lamp")) != WorldObject_GetId(FindPart(&S, Second, "lamp")));
        // fixed text: an id must never change with the machine or a new build
        TEST_CHECK(run, WorldPrefabs_DeriveObjectId(12U, (const unsigned char*)u8"bench", 0U) == UINT64_C(0xEDA1A427A137EE28));
        TEST_CHECK(run, WorldPrefabs_DeriveObjectId(12U, (const unsigned char*)u8"bench", 1U)
            != WorldPrefabs_DeriveObjectId(12U, (const unsigned char*)u8"bench", 0U));
    }
    Scene_Deconstruct(run, &S);
}

static void ATakenIdTakesTheNextSalt(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        uint64_t Wanted = WorldPrefabs_DeriveObjectId(Root, (const unsigned char*)u8"lamp", 0U);
        WorldObject* Squatter = NULL;
        TEST_CHECK_SUCCESS(run, World_CreateObjectWithId(&S.World, (const unsigned char*)u8"squatter", Wanted, &Squatter));
        WorldPrefabs_Update(S.System);
        WorldObject* Lamp = FindPart(&S, Root, "lamp");
        TEST_CHECK(run, (Lamp != NULL) && (Lamp != Squatter));
        if (Lamp != NULL)
        {
            TEST_CHECK(run, WorldObject_GetId(Lamp) == WorldPrefabs_DeriveObjectId(Root, (const unsigned char*)u8"lamp", 1U));
        }
        TEST_CHECK(run, CountInLog(run, &S, "takes another") == 1U);
    }
    Scene_Deconstruct(run, &S);
}


// Tests: values.
static void ValuesReachEveryTargetAndDefaultsApply(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        RenderColor Roof = GetTint(FindPart(&S, Root, "roof"));
        RenderColor Cap = GetTint(FindPart(&S, Root, "roof_cap"));
        TEST_CHECK(run, (Roof.Tint.r == 0x7A) && (Roof.Tint.g == 0x3B) && (Roof.Tint.b == 0x2E));
        TEST_CHECK(run, (Cap.Tint.r == 0x7A) && (Cap.Tint.g == 0x3B) && (Cap.Tint.b == 0x2E));
        TEST_CHECK(run, GetTint(FindPart(&S, Root, "lamp")).Tint.r == 255); // the tag reaches only what carries it
        SetStringValue(run, &S, Root, "roof_color", "#2e4a7a");
        SetNumberValue(run, &S, Root, "lamp_range", 11.5);
        WorldPrefabs_Update(S.System);
        Roof = GetTint(FindPart(&S, Root, "roof"));
        Cap = GetTint(FindPart(&S, Root, "roof_cap"));
        TEST_CHECK(run, (Roof.Tint.r == 0x2E) && (Roof.Tint.b == 0x7A) && (Cap.Tint.r == 0x2E));
        TEST_CHECK(run, NEAR(GetLampRange(&S, Root, "lamp"), 11.5, 1e-6));
        // back to the default when the copy lets go of it
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetValue(GetCopy(&S, Root), (const unsigned char*)u8"roof_color", NULL));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, GetTint(FindPart(&S, Root, "roof")).Tint.r == 0x7A);
    }
    Scene_Deconstruct(run, &S);
}

static void AnAxisWritesOneAxisInDegrees(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        SetNumberValue(run, &S, Root, "door_angle", 90.0);
        WorldPrefabs_Update(S.System);
        Vector3 Rotation = WorldObject_GetRotation(FindPart(&S, Root, "door"));
        TEST_CHECK(run, NEAR(Rotation.y, 90.0 * DEGREES_TO_RADIANS, 1e-6));
        // the others keep the template's
        TEST_CHECK(run, NEAR(Rotation.x, 10.0 * DEGREES_TO_RADIANS, 1e-6) && NEAR(Rotation.z, 20.0 * DEGREES_TO_RADIANS, 1e-6));
    }
    Scene_Deconstruct(run, &S);
}

static void AnEnumsNextOptionUndoesTheLast(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, !WorldObject_IsEnabled(FindPart(&S, Root, "garage")));
        TEST_CHECK(run, WorldObject_IsEnabled(FindPart(&S, Root, "door")));
        SetStringValue(run, &S, Root, "layout", "garage");
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldObject_IsEnabled(FindPart(&S, Root, "garage")));
        TEST_CHECK(run, !WorldObject_IsEnabled(FindPart(&S, Root, "door")));
        SetStringValue(run, &S, Root, "layout", "plain");
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, !WorldObject_IsEnabled(FindPart(&S, Root, "garage")));
        TEST_CHECK(run, WorldObject_IsEnabled(FindPart(&S, Root, "door")));
    }
    Scene_Deconstruct(run, &S);
}

static void ABadValueTakesTheDefaultAndWarnsOnce(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        SetStringValue(run, &S, Root, "layout", "no_such_option");
        SetNumberValue(run, &S, Root, "door_angle", 500.0); // past its range
        SetStringValue(run, &S, Root, "no_such_property", "x");
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, !WorldObject_IsEnabled(FindPart(&S, Root, "garage")));
        TEST_CHECK(run, NEAR(WorldObject_GetRotation(FindPart(&S, Root, "door")).y, 0.0, 1e-6));
        SetNumberValue(run, &S, Root, "lamp_range", 3.0); // applies again: the warnings do not repeat
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, CountInLog(run, &S, "\"layout\" a bad value") == 1U);
        TEST_CHECK(run, CountInLog(run, &S, "\"door_angle\" a bad value") == 1U);
        TEST_CHECK(run, CountInLog(run, &S, "no longer has: \"no_such_property\"") == 1U);
    }
    Scene_Deconstruct(run, &S);
}

static void AQuietUpdateWritesNothing(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        WorldPrefabsStats Before = WorldPrefabs_GetStats(S.System);
        WorldObject* Roof = FindPart(&S, Root, "roof");
        uint32_t RoofRevision = WorldObject_GetRevision(Roof);
        uint64_t WorldRevision = World_GetComponentTypeRevision(&S.World, WorldComponentType_Light);
        WorldPrefabs_Update(S.System);
        WorldPrefabsStats Quiet = WorldPrefabs_GetStats(S.System);
        TEST_CHECK(run, (Quiet.ApplyCount == Before.ApplyCount) && (Quiet.WriteCount == Before.WriteCount));
        TEST_CHECK(run, Quiet.PassCount == Before.PassCount); // it did not even look over the copies
        // the same value again applies, and writes nothing
        SetStringValue(run, &S, Root, "layout", "plain");
        WorldPrefabs_Update(S.System);
        WorldPrefabsStats Again = WorldPrefabs_GetStats(S.System);
        TEST_CHECK(run, Again.ApplyCount == (Before.ApplyCount + 1U));
        TEST_CHECK(run, Again.WriteCount == Before.WriteCount);
        TEST_CHECK(run, WorldObject_GetRevision(Roof) == RoofRevision);
        TEST_CHECK(run, World_GetComponentTypeRevision(&S.World, WorldComponentType_Light) == WorldRevision);
        // and one that changes one row writes that row alone
        SetStringValue(run, &S, Root, "roof_color", "#010203");
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).WriteCount == (Before.WriteCount + 2U)); // the roof and its cap
    }
    Scene_Deconstruct(run, &S);
}

static void WhatItDoesNotWriteStaysAsChanged(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        WorldObject* Bulb = FindPart(&S, Root, "bulb");
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Bulb, Vector3Double_Create(0.0, 2.5, 0.0))); // a script moved it
        SetNumberValue(run, &S, Root, "lamp_range", 3.0);
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, NEAR(WorldObject_GetPosition(Bulb).Y, 2.5, 1e-9));
    }
    Scene_Deconstruct(run, &S);
}


// Tests: overrides.
static void AnOverrideChangesARowOfAnObject(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        uint64_t Other = AddCopy(run, &S, "b", "stop", (Vector3){ 0.0f, 0.0f, 9.0f });
        SetVectorRow(run, &S, Root, "bulb", "position", 1.0, 2.0, 3.0);
        SetVectorRow(run, &S, Root, "bulb", "rotation", 0.0, 35.0, 0.0);
        WorldPrefabs_Update(S.System);
        Vector3Double Place = WorldObject_GetPosition(FindPart(&S, Root, "bulb"));
        TEST_CHECK(run, NEAR(Place.X, 1.0, 1e-9) && NEAR(Place.Y, 2.0, 1e-9) && NEAR(Place.Z, 3.0, 1e-9));
        TEST_CHECK(run, NEAR(WorldObject_GetRotation(FindPart(&S, Root, "bulb")).y, 35.0 * DEGREES_TO_RADIANS, 1e-6));
        // a copy changes nothing of another's
        TEST_CHECK(run, NEAR(WorldObject_GetPosition(FindPart(&S, Other, "bulb")).Y, 2.0, 1e-9));
        TEST_CHECK(run, NEAR(WorldObject_GetPosition(FindPart(&S, Other, "bulb")).X, 0.0, 1e-9));
        // taking it off brings the prefab's back
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_ClearObjectOverride(GetCopy(&S, Root), (const unsigned char*)u8"bulb"));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, NEAR(WorldObject_GetPosition(FindPart(&S, Root, "bulb")).X, 0.0, 1e-9));
        TEST_CHECK(run, NEAR(WorldObject_GetRotation(FindPart(&S, Root, "bulb")).y, 0.0, 1e-9));
    }
    Scene_Deconstruct(run, &S);
}

static void AnOverrideBeatsAPropertyAndAComponentRowWorks(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        DataValue Range = DataValue_FromFloat(42.0);
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetObjectRow(GetCopy(&S, Root), (const unsigned char*)u8"lamp", (const unsigned char*)u8"light",
            (const unsigned char*)u8"range", &Range));
        SetNumberValue(run, &S, Root, "lamp_range", 3.0);
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, NEAR(GetLampRange(&S, Root, "lamp"), 42.0, 1e-6));
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetObjectRow(GetCopy(&S, Root), (const unsigned char*)u8"lamp", (const unsigned char*)u8"light",
            (const unsigned char*)u8"range", NULL));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, NEAR(GetLampRange(&S, Root, "lamp"), 3.0, 1e-6));
        TEST_CHECK(run, WorldPrefabComponent_GetObjectOverride(GetCopy(&S, Root), (const unsigned char*)u8"lamp") == NULL);
    }
    Scene_Deconstruct(run, &S);
}

static void AnOverrideRemovesAnObjectAndBringsItBack(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        uint64_t BulbId = WorldObject_GetId(FindPart(&S, Root, "bulb"));
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetObjectRemoved(GetCopy(&S, Root), (const unsigned char*)u8"lamp", true));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, FindPart(&S, Root, "lamp") == NULL);
        TEST_CHECK(run, FindPart(&S, Root, "bulb") == NULL); // what lies under it goes with it
        WorldObject* Gone = NULL;
        TEST_CHECK(run, !World_TryGetObject(&S.World, BulbId, &Gone));
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 5U);
        GenericBuffer Held;
        GenericBuffer_AllocateVariable(&Held, 0U, sizeof(uint64_t));
        TEST_CHECK(run, WorldPrefabs_GetCopyObjects(S.System, Root, &Held) && (Held._count == 5U)); // what it holds, not what it would
        Memory_Free(Held._data);
        // false is the same as saying nothing
        DataValue NotRemoved = DataValue_FromBool(false);
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetObjectRow(GetCopy(&S, Root), (const unsigned char*)u8"roof_cap", NULL,
            WORLD_PREFAB_OVERRIDE_IS_REMOVED_KEY, &NotRemoved));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, FindPart(&S, Root, "roof_cap") != NULL);
        // a property that reaches it finds nothing and does no harm
        SetNumberValue(run, &S, Root, "lamp_range", 3.0);
        WorldPrefabs_Update(S.System);
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetObjectRemoved(GetCopy(&S, Root), (const unsigned char*)u8"lamp", false));
        WorldPrefabs_Update(S.System);
        WorldObject* Back = FindPart(&S, Root, "bulb");
        TEST_CHECK(run, (Back != NULL) && (WorldObject_GetId(Back) == BulbId)); // the same id: a reference holds
        TEST_CHECK(run, NEAR(GetLampRange(&S, Root, "lamp"), 3.0, 1e-6));
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 7U);
    }
    Scene_Deconstruct(run, &S);
}

static void AStaleNameWarnsOnceAndIsIgnored(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        SetVectorRow(run, &S, Root, "no_such_object", "position", 1.0, 2.0, 3.0);
        SetVectorRow(run, &S, Root, "bulb", "no_such_row", 1.0, 2.0, 3.0);
        SetVectorRow(run, &S, Root, "bulb", "name", 1.0, 2.0, 3.0);
        SetVectorRow(run, &S, Root, "bulb", "parent", 1.0, 2.0, 3.0);
        DataValue Flag = DataValue_FromBool(true);
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetObjectRow(GetCopy(&S, Root), (const unsigned char*)u8"bulb",
            (const unsigned char*)u8"terrain", (const unsigned char*)u8"has_outline", &Flag));
        WorldPrefabs_Update(S.System);
        SetNumberValue(run, &S, Root, "lamp_range", 3.0);
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, FindPart(&S, Root, "bulb") != NULL);
        TEST_CHECK(run, CountInLog(run, &S, "no longer has: \"no_such_object\"") == 1U);
        TEST_CHECK(run, CountInLog(run, &S, "world_object.no_such_row") == 1U);
        TEST_CHECK(run, CountInLog(run, &S, "world_object.name") == 1U);
        TEST_CHECK(run, CountInLog(run, &S, "world_object.parent") == 1U);
        TEST_CHECK(run, CountInLog(run, &S, "does not have: \"terrain\"") == 1U);
    }
    Scene_Deconstruct(run, &S);
}

static void AValueARowRefusesWarnsAndLeavesItsRow(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        SetVectorRow(run, &S, Root, "bulb", "is_enabled", 1.0, 2.0, 3.0); // no vector is a bool
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldObject_IsEnabled(FindPart(&S, Root, "bulb")));
        TEST_CHECK(run, CountInLog(run, &S, "what a row refuses, in the object \"bulb\"") == 1U);
    }
    Scene_Deconstruct(run, &S);
}

static void WhatACopyAddsIsAnOrdinarySavedChild(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldObject* Added = NULL;
        TEST_CHECK_SUCCESS(run, World_CreateObject(&S.World, (const unsigned char*)u8"added", &Added));
        uint64_t AddedId = WorldObject_GetId(Added);
        TEST_CHECK_SUCCESS(run, World_SetParent(&S.World, AddedId, Root, false));
        WorldPrefabs_Update(S.System);
        SetStringValue(run, &S, Root, "layout", "garage");
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, World_TryGetObject(&S.World, AddedId, &Added) && WorldObject_IsPersistent(Added));
        uint64_t Found = 0U;
        TEST_CHECK(run, !WorldPrefabs_TryGetCopyRoot(&S.World, AddedId, &Found));
        // a whole new prefab leaves it too
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetPrefab(GetCopy(&S, Root), (const unsigned char*)u8"shelter"));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, World_TryGetObject(&S.World, AddedId, &Added));
        TEST_CHECK(run, FindPart(&S, Root, "stop_a") != NULL);
        TEST_CHECK(run, FindPart(&S, Root, "lamp") == NULL);
    }
    Scene_Deconstruct(run, &S);
}

static void AnObjectRemovedFromOutsideComesBackAtTheNextChange(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&S.World, WorldObject_GetId(FindPart(&S, Root, "lamp"))));
        WorldPrefabs_Update(S.System);
        WorldObject* Found = NULL;
        TEST_CHECK(run, !World_TryGetObject(&S.World, WorldPrefabs_DeriveObjectId(Root, (const unsigned char*)u8"lamp", 0U), &Found));
        SetNumberValue(run, &S, Root, "lamp_range", 3.0);
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, World_TryGetObject(&S.World, WorldPrefabs_DeriveObjectId(Root, (const unsigned char*)u8"lamp", 0U), &Found));
        TEST_CHECK(run, FindPart(&S, Root, "bulb") != NULL);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 7U);
    }
    Scene_Deconstruct(run, &S);
}


// Tests: nesting.
static void ANestedCopyMakesItsObjectsWithinOneUpdate(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "shelter", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        WorldObject* Bulb = FindPart(&S, Root, "stop_a/bulb");
        TEST_CHECK(run, Bulb != NULL);
        WorldPrefabsStats Stats = WorldPrefabs_GetStats(S.System);
        TEST_CHECK(run, (Stats.CopyCount == 3U) && (Stats.ObjectCount == 17U));
        if (Bulb != NULL)
        {
            WorldObject* NestedRoot = FindPart(&S, Root, "stop_a");
            TEST_CHECK(run, WorldObject_GetId(Bulb) == WorldPrefabs_DeriveObjectId(WorldObject_GetId(NestedRoot), (const unsigned char*)u8"bulb", 0U));
            Vector3 InScene = TestWorld_ToScene(WorldObject_GetWorldPosition(Bulb));
            TEST_CHECK(run, NEAR(InScene.x, 5.0, 1e-3) && NEAR(InScene.y, 2.0, 1e-3));
        }
        TEST_CHECK(run, NEAR(GetLampRange(&S, Root, "stop_a/lamp"), 7.0, 1e-6)); // the outer prefab's own value for it
        TEST_CHECK(run, NEAR(WorldObject_GetPosition(FindPart(&S, Root, "stop_b/bulb")).Y, 9.0, 1e-9)); // its own override
    }
    Scene_Deconstruct(run, &S);
}

static void APropertyPassesThroughToANestedCopy(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "shelter", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, GetTint(FindPart(&S, Root, "stop_a/roof")).Tint.r == 0x11); // the outer default reaches it
        TEST_CHECK(run, GetTint(FindPart(&S, Root, "stop_b/roof")).Tint.r == 0x7A); // not reached: its own default
        SetStringValue(run, &S, Root, "stop_roof", "#445566");
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, GetTint(FindPart(&S, Root, "stop_a/roof")).Tint.r == 0x44);
        TEST_CHECK(run, NEAR(GetLampRange(&S, Root, "stop_a/lamp"), 7.0, 1e-6)); // what the prefab set keeps
    }
    Scene_Deconstruct(run, &S);
}

static void AnOverridesPathReachesThroughANestedCopy(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "shelter", (Vector3){ 0.0f, 0.0f, 0.0f });
        SetVectorRow(run, &S, Root, "stop_a/bulb", "position", 1.0, 2.0, 3.0);
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetObjectRemoved(GetCopy(&S, Root), (const unsigned char*)u8"stop_b/roof_cap", true));
        // over what the prefab's own nested override says: a row by row merge
        SetVectorRow(run, &S, Root, "stop_b/bulb", "scale", 2.0, 2.0, 2.0);
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, NEAR(WorldObject_GetPosition(FindPart(&S, Root, "stop_a/bulb")).Z, 3.0, 1e-9));
        TEST_CHECK(run, FindPart(&S, Root, "stop_b/roof_cap") == NULL);
        TEST_CHECK(run, FindPart(&S, Root, "stop_a/roof_cap") != NULL);
        TEST_CHECK(run, NEAR(WorldObject_GetPosition(FindPart(&S, Root, "stop_b/bulb")).Y, 9.0, 1e-9));
        TEST_CHECK(run, NEAR(WorldObject_GetScale(FindPart(&S, Root, "stop_b/bulb")).x, 2.0, 1e-6));
        // an override's components merge with the prefab's own for that path, row by row
        DataValue Range = DataValue_FromFloat(21.0);
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetObjectRow(GetCopy(&S, Root), (const unsigned char*)u8"stop_b/lamp", (const unsigned char*)u8"light",
            (const unsigned char*)u8"range", &Range));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, NEAR(GetLampRange(&S, Root, "stop_b/lamp"), 21.0, 1e-6));
        WorldObject* Lamp = FindPart(&S, Root, "stop_b/lamp");
        TEST_CHECK(run, (Lamp != NULL) && NEAR(WorldLightComponent_FromComponent(WorldObject_GetComponent(Lamp, WorldComponentType_Light))->_intensity, 2.0, 1e-6));
        // taken off, the nested copy is as the prefab says again
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_ClearObjectOverride(GetCopy(&S, Root), (const unsigned char*)u8"stop_b/roof_cap"));
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_ClearObjectOverride(GetCopy(&S, Root), (const unsigned char*)u8"stop_b/bulb"));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, FindPart(&S, Root, "stop_b/roof_cap") != NULL);
        TEST_CHECK(run, NEAR(WorldObject_GetScale(FindPart(&S, Root, "stop_b/bulb")).x, 1.0, 1e-6));
        TEST_CHECK(run, NEAR(WorldObject_GetPosition(FindPart(&S, Root, "stop_b/bulb")).Y, 9.0, 1e-9));
        // a path through something that holds no copy
        SetVectorRow(run, &S, Root, "root/bulb", "position", 1.0, 2.0, 3.0);
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, CountInLog(run, &S, "holds no copy: \"root/bulb\"") == 1U);
    }
    Scene_Deconstruct(run, &S);
}

static void APrefabThatWouldHoldItselfMakesNothing(TestRun* run)
{
    Scene S;
    if (Scene_ConstructBare(run, &S))
    {
        AddPrefab(run, &S, "{ \"name\": \"left\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"root\", "
            "\"components\": { \"prefab\": { \"prefab\": \"right\" } } } ] }");
        AddPrefab(run, &S, "{ \"name\": \"right\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"root\" }, "
            "{ \"id\": 2, \"parent\": 1, \"name\": \"inner\", \"components\": { \"prefab\": { \"prefab\": \"left\" } } } ] }");
        AddPrefab(run, &S, "{ \"name\": \"self\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"root\", "
            "\"components\": { \"prefab\": { \"prefab\": \"self\" } } } ] }");
        uint64_t Root = AddCopy(run, &S, "a", "left", (Vector3){ 0.0f, 0.0f, 0.0f });
        uint64_t Self = AddCopy(run, &S, "b", "self", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, FindPart(&S, Root, "root/inner") != NULL); // left -> right
        TEST_CHECK(run, FindPart(&S, Root, "root/inner/root") == NULL); // right -> left again: refused
        TEST_CHECK(run, FindPart(&S, Self, "root") != NULL);
        TEST_CHECK(run, FindPart(&S, Self, "root/root") == NULL);
        TEST_CHECK(run, CountInLog(run, &S, "would hold itself") == 2U);
        TEST_CHECK(run, WorldPrefabs_IsSettled(S.System));
        // two copies of one prefab side by side, or one hung on another's root, are no cycle
        uint64_t Second = AddCopy(run, &S, "c", "right", (Vector3){ 0.0f, 0.0f, 0.0f });
        uint64_t Third = AddCopy(run, &S, "d", "right", (Vector3){ 0.0f, 0.0f, 0.0f });
        TEST_CHECK_SUCCESS(run, World_SetParent(&S.World, Third, Second, false));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, FindPart(&S, Third, "inner") != NULL);
    }
    Scene_Deconstruct(run, &S);
}

static void CopiesNestSoDeep(TestRun* run)
{
    Scene S;
    if (Scene_ConstructBare(run, &S))
    {
        char Json[256];
        const size_t Levels = WORLD_PREFABS_MAX_NESTING + 3U;
        for (size_t Level = 0U; Level < Levels; Level++)
        {
            if ((Level + 1U) == Levels)
            {
                snprintf(Json, sizeof(Json), "{ \"name\": \"level_%zu\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"root\" } ] }", Level);
            }
            else
            {
                snprintf(Json, sizeof(Json), "{ \"name\": \"level_%zu\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"root\", "
                    "\"components\": { \"prefab\": { \"prefab\": \"level_%zu\" } } } ] }", Level, Level + 1U);
            }
            AddPrefab(run, &S, Json);
        }
        uint64_t Root = AddCopy(run, &S, "a", "level_0", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).CopyCount == (WORLD_PREFABS_MAX_NESTING + 1U));
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == WORLD_PREFABS_MAX_NESTING);
        TEST_CHECK(run, CountInLog(run, &S, "copies nest at most") == 1U);
        TEST_CHECK(run, FindPart(&S, Root, "root") != NULL);
    }
    Scene_Deconstruct(run, &S);
}

static void AMissingPrefabMakesNothingAndWaitsForANewName(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "no_such_prefab", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 0U);
        TEST_CHECK(run, WorldPrefabs_GetPrefab(S.System, Root) == NULL);
        TEST_CHECK(run, CountInLog(run, &S, "its prefab does not load") == 1U);
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetPrefab(GetCopy(&S, Root), (const unsigned char*)u8"stop"));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 7U);
        TEST_CHECK(run, WorldPrefabs_GetPrefab(S.System, Root) != NULL);
        // and none takes everything away
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetPrefab(GetCopy(&S, Root), NULL));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 0U);
    }
    Scene_Deconstruct(run, &S);
}


// Tests: limits.
static void TheWorldsObjectsHaveABudget(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        WorldPrefabsLimits Limits = WorldPrefabsLimits_CreateDefault();
        Limits.MaxObjects = 10U;
        WorldPrefabs_SetLimits(S.System, &Limits);
        uint64_t First = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        uint64_t Second = AddCopy(run, &S, "b", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 10U);
        TEST_CHECK(run, FindPart(&S, First, "bulb") != NULL);
        TEST_CHECK(run, FindPart(&S, Second, "bulb") == NULL); // past it, and its parent's parent is not made
        TEST_CHECK(run, CountInLog(run, &S, "more objects than the world's copies may hold") == 1U);
        // what was left out is made when there is room and an update looks
        Limits.MaxObjects = 100U;
        WorldPrefabs_SetLimits(S.System, &Limits);
        SetNumberValue(run, &S, Second, "lamp_range", 3.0);
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, FindPart(&S, Second, "bulb") != NULL);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 14U);
    }
    Scene_Deconstruct(run, &S);
}

/* Room made by a copy going reaches what was left out, in the same update and with nothing else changing. */
static void WhatIsLeftOutIsMadeWhenAnUpdateFindsRoom(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        WorldPrefabsLimits Limits = WorldPrefabsLimits_CreateDefault();
        Limits.MaxObjects = 20U;
        WorldPrefabs_SetLimits(S.System, &Limits);
        uint64_t First = AddCopy(run, &S, "a", "shelter", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 17U);
        uint64_t Second = AddCopy(run, &S, "b", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 20U);
        TEST_CHECK(run, FindPart(&S, Second, "bulb") == NULL);
        // The shelter's name changes: its nested copies' objects go with it, which is known only a pass later, and even its own new objects
        // first find no room.
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetPrefab(GetCopy(&S, First), (const unsigned char*)u8"stop"));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, FindPart(&S, First, "bulb") != NULL);
        TEST_CHECK(run, FindPart(&S, Second, "bulb") != NULL);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 14U);
        TEST_CHECK(run, WorldPrefabs_IsSettled(S.System));
    }
    Scene_Deconstruct(run, &S);
}

/* A small file can ask for any number of copies: past the budgets each costs nothing, and takes up no ids, objects or memory for them. */
static void ACopyPastTheBudgetsCostsNothing(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        WorldPrefabsLimits Limits = WorldPrefabsLimits_CreateDefault();
        Limits.MaxObjects = 0U;
        WorldPrefabs_SetLimits(S.System, &Limits);
        uint64_t Root = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_IsSettled(S.System));
        WorldPrefabsStats Stats = WorldPrefabs_GetStats(S.System);
        TEST_CHECK(run, (Stats.ObjectCount == 0U) && (Stats.ByteCount == 0U) && (Stats.ApplyCount == 0U) && (Stats.CopyCount == 1U));
        TEST_CHECK(run, CountInLog(run, &S, "more objects than the world's copies may hold") == 1U);
        // It knows what it shows; it holds nothing, and the lookups say so.
        TEST_CHECK(run, WorldPrefabs_GetPrefab(S.System, Root) != NULL);
        TEST_CHECK(run, FindPart(&S, Root, "lamp") == NULL);
        GenericBuffer Held;
        GenericBuffer_AllocateVariable(&Held, 0U, sizeof(uint64_t));
        TEST_CHECK(run, WorldPrefabs_GetCopyObjects(S.System, Root, &Held) && (Held._count == 0U));
        Memory_Free(Held._data);
        // A change finds no room either, and does no harm.
        SetNumberValue(run, &S, Root, "lamp_range", 3.0);
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ApplyCount == 0U);
        // With room, it comes with what it was told.
        Limits.MaxObjects = 100U;
        WorldPrefabs_SetLimits(S.System, &Limits);
        AddCopy(run, &S, "b", "stop", (Vector3){ 1.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, NEAR(GetLampRange(&S, Root, "lamp"), 3.0, 1e-6));
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 14U);
    }
    Scene_Deconstruct(run, &S);
}

/* Objects alone do not bound what a file can ask for: a copy counts its prefab's file and what it writes, and both are given back. */
static void WhatCopiesHoldHasABudgetInBytes(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        AddPrefab(run, &S, "{ \"name\": \"inner\", \"format_version\": 3, \"properties\": { \"note\": { \"type\": \"string\", \"default\": \"\" } }, "
            "\"objects\": [ { \"id\": 1, \"name\": \"root\" } ] }");
        AddPrefab(run, &S, "{ \"name\": \"outer\", \"format_version\": 3, \"properties\": { \"note\": { \"type\": \"string\", \"default\": \"a\", "
            "\"targets\": [ { \"object\": \"inner\", \"property\": \"note\" } ] } }, "
            "\"objects\": [ { \"id\": 1, \"name\": \"root\" }, { \"id\": 2, \"parent\": 1, \"name\": \"inner\", \"components\": { \"prefab\": { \"prefab\": \"inner\" } } } ] }");
        uint64_t First = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        size_t File = Prefab_GetFileByteCount(WorldPrefabs_GetPrefab(S.System, First));
        TEST_CHECK(run, File == strlen(StopPrefab));
        uint64_t Bytes = WorldPrefabs_GetStats(S.System).ByteCount;
        TEST_CHECK(run, Bytes > File); // and what its defaults wrote

        // No room for a second file: it makes nothing, until the first goes.
        WorldPrefabsLimits Limits = WorldPrefabsLimits_CreateDefault();
        Limits.MaxBytes = (size_t)Bytes + File - 1U;
        WorldPrefabs_SetLimits(S.System, &Limits);
        uint64_t Second = AddCopy(run, &S, "b", "stop", (Vector3){ 1.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, FindPart(&S, Second, "lamp") == NULL);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ByteCount == Bytes);
        TEST_CHECK(run, CountInLog(run, &S, "its prefab's file is more than") == 1U);
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&S.World, First));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, FindPart(&S, Second, "lamp") != NULL);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ByteCount == Bytes);
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&S.World, Second));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ByteCount == 0U);

        // A value past what is left: its values go unwritten, and those it had come back to the template's.
        WorldPrefabs_SetLimits(S.System, &(WorldPrefabsLimits){ .MaxObjects = 100U, .MaxBytes = 4096U, .MaxUpdateWork = WORLD_PREFABS_MAX_UPDATE_WORK, .MaxWarnings = 64U });
        uint64_t Root = AddCopy(run, &S, "c", "outer", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        WorldObject* Inner = FindPart(&S, Root, "inner");
        TEST_CHECK(run, Inner != NULL);
        const WorldPrefabComponent* Nested = WorldPrefabComponent_FromComponent(WorldObject_GetComponent(Inner, WorldComponentType_Prefab));
        const DataValue* Note = WorldPrefabComponent_GetValue(Nested, (const unsigned char*)u8"note");
        TEST_CHECK(run, (Note != NULL) && (strcmp((const char*)Note->StringValue, "a") == 0));
        Bytes = WorldPrefabs_GetStats(S.System).ByteCount;
        char Long[4001];
        memset(Long, 'x', 4000U);
        Long[4000] = 0;
        SetStringValue(run, &S, Root, "note", Long);
        WorldPrefabs_Update(S.System);
        Note = WorldPrefabComponent_GetValue(Nested, (const unsigned char*)u8"note");
        TEST_CHECK(run, Note == NULL);
        TEST_CHECK(run, CountInLog(run, &S, "writes more than the world's copies may hold") == 1U);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ByteCount < Bytes);
        // With room it writes, and the count follows; a shorter one takes the difference back.
        WorldPrefabs_SetLimits(S.System, &(WorldPrefabsLimits){ .MaxObjects = 100U, .MaxBytes = 1048576U, .MaxUpdateWork = WORLD_PREFABS_MAX_UPDATE_WORK, .MaxWarnings = 64U });
        SetStringValue(run, &S, Root, "note", Long);
        WorldPrefabs_Update(S.System);
        Note = WorldPrefabComponent_GetValue(Nested, (const unsigned char*)u8"note");
        TEST_CHECK(run, (Note != NULL) && (strlen((const char*)Note->StringValue) == 4000U));
        uint64_t Written = WorldPrefabs_GetStats(S.System).ByteCount;
        TEST_CHECK(run, Written > (Bytes + 3900U));
        SetStringValue(run, &S, Root, "note", "b");
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ByteCount < (Written - 3900U));
        WorldPrefabs_Rebuild(S.System);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 3U);
    }
    Scene_Deconstruct(run, &S);
}

/* A nested copy's row takes thousands of names at once, and stops at what it holds with the rest still in. */
static void ANestedRowTakesManyWrites(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        AddPrefab(run, &S, "{ \"name\": \"holder\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"root\" }, "
            "{ \"id\": 2, \"parent\": 1, \"name\": \"held\", \"components\": { \"prefab\": { \"prefab\": \"stop\", \"object_overrides\": { \"x0\": { \"position\": [1, 1, 1] } } } } } ] }");
        uint64_t Root = AddCopy(run, &S, "a", "holder", (Vector3){ 0.0f, 0.0f, 0.0f });
        char Path[32];
        for (size_t Index = 0U; Index < 2000U; Index++)
        {
            snprintf(Path, sizeof(Path), "held/x%zu", Index);
            DataValue One = DataValue_FromBool(false);
            TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetObjectRow(GetCopy(&S, Root), (const unsigned char*)Path, NULL, WORLD_PREFAB_OVERRIDE_IS_REMOVED_KEY, &One));
        }
        WorldPrefabs_Update(S.System);
        WorldObject* Held = FindPart(&S, Root, "held");
        TEST_CHECK(run, Held != NULL);
        const WorldPrefabComponent* Nested = WorldPrefabComponent_FromComponent(WorldObject_GetComponent(Held, WorldComponentType_Prefab));
        TEST_CHECK(run, DataValue_GetCompoundCount(Nested->_objectOverrides) == 2000U);
        // The template's block for x0 merged with the copy's, whose value it is.
        const DataValue* Merged = WorldPrefabComponent_GetObjectOverride(Nested, (const unsigned char*)u8"x0");
        TEST_CHECK(run, (Merged != NULL) && (DataValue_FindCompoundEntry(Merged, (const unsigned char*)u8"position") != NULL)
            && (DataValue_FindCompoundEntry(Merged, WORLD_PREFAB_OVERRIDE_IS_REMOVED_KEY) != NULL));
    }
    Scene_Deconstruct(run, &S);
    // Each property passes 4000 bytes through: a row of 64 KiB takes what fits, in order, and the rest are warned about.
    if (Scene_Construct(run, &S))
    {
        GenericBuffer Text;
        GenericBuffer_AllocateVariable(&Text, 0U, sizeof(unsigned char));
        GenericBuffer_AppendString(&Text, (const unsigned char*)u8"{ \"name\": \"long\", \"format_version\": 3, \"properties\": {");
        char Long[4001];
        memset(Long, 'x', 4000U);
        Long[4000] = 0;
        for (size_t Index = 0U; Index < 20U; Index++)
        {
            char Part[128];
            snprintf(Part, sizeof(Part), "%s \"p%02zu\": { \"type\": \"string\", \"targets\": [ { \"object\": \"held\", \"property\": \"p%02zu\" } ], \"default\": \"", (Index == 0U) ? "" : ",", Index, Index);
            GenericBuffer_AppendString(&Text, (const unsigned char*)Part);
            GenericBuffer_AppendString(&Text, (const unsigned char*)Long);
            GenericBuffer_AppendString(&Text, (const unsigned char*)u8"\" }");
        }
        GenericBuffer_AppendString(&Text, (const unsigned char*)u8" }, \"objects\": [ { \"id\": 1, \"name\": \"root\" }, "
            "{ \"id\": 2, \"parent\": 1, \"name\": \"held\", \"components\": { \"prefab\": { \"prefab\": \"stop\" } } } ] }");
        GenericBuffer_NullTerminate(&Text);
        AddPrefab(run, &S, (const char*)Text._data);
        Memory_Free(Text._data);
        uint64_t Root = AddCopy(run, &S, "a", "long", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        WorldObject* Held = FindPart(&S, Root, "held");
        TEST_CHECK(run, Held != NULL);
        const WorldPrefabComponent* Nested = WorldPrefabComponent_FromComponent(WorldObject_GetComponent(Held, WorldComponentType_Prefab));
        size_t Fits = DataValue_GetCompoundCount(Nested->_values);
        TEST_CHECK(run, (Fits >= 15U) && (Fits <= 16U));
        TEST_CHECK(run, WorldPrefabComponent_GetValue(Nested, (const unsigned char*)u8"p00") != NULL);
        TEST_CHECK(run, CountInLog(run, &S, "a nested copy's row holds at most") == 1U);
    }
    Scene_Deconstruct(run, &S);
}

/* A copy changing every tick takes no other's turn. */
static void NoCopyStarves(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t First = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        uint64_t Second = AddCopy(run, &S, "b", "stop", (Vector3){ 1.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        WorldPrefabsLimits Limits = WorldPrefabsLimits_CreateDefault();
        Limits.MaxUpdateWork = 5U;
        WorldPrefabs_SetLimits(S.System, &Limits);
        SetNumberValue(run, &S, Second, "lamp_range", 9.0);
        for (int Tick = 0; Tick < 4; Tick++)
        {
            SetNumberValue(run, &S, First, "lamp_range", (double)Tick + 1.0);
            WorldPrefabs_Update(S.System);
        }
        TEST_CHECK(run, NEAR(GetLampRange(&S, Second, "lamp"), 9.0, 1e-6));
    }
    Scene_Deconstruct(run, &S);
}

/* An id an object left is nothing of the copy's: what took it since keeps its own, and the lookups name nothing. */
static void AnIdItsObjectLeftIsNotTheCopys(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        WorldObject* Bulb = FindPart(&S, Root, "bulb");
        TEST_CHECK(run, Bulb != NULL);
        uint64_t OldId = WorldObject_GetId(Bulb);
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&S.World, OldId));
        uint64_t Found = 0U;
        TEST_CHECK(run, !WorldPrefabs_TryFindObject(S.System, Root, (const unsigned char*)u8"bulb", &Found));
        GenericBuffer Held;
        GenericBuffer_AllocateVariable(&Held, 0U, sizeof(uint64_t));
        TEST_CHECK(run, WorldPrefabs_GetCopyObjects(S.System, Root, &Held));
        const uint64_t* Ids = (const uint64_t*)Held._data;
        for (size_t Index = 0U; Index < Held._count; Index++)
        {
            TEST_CHECK(run, Ids[Index] != OldId);
        }
        Memory_Free(Held._data);
        // Something saved takes the id.
        WorldObject* Stranger = NULL;
        TEST_CHECK_SUCCESS(run, World_CreateDerivedObject(&S.World, (const unsigned char*)u8"stranger", OldId, &Stranger));
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Stranger, TestWorld_At((Vector3){ 7.0f, 0.0f, 0.0f })));
        WorldObject_SetPersistent(Stranger, true);
        TEST_CHECK(run, !WorldPrefabs_TryFindObject(S.System, Root, (const unsigned char*)u8"bulb", &Found));
        // The copy's next change makes its own under another salt, and takes nothing of the stranger's.
        SetNumberValue(run, &S, Root, "lamp_range", 3.0);
        WorldPrefabs_Update(S.System);
        WorldObject* NewBulb = FindPart(&S, Root, "bulb");
        TEST_CHECK(run, (NewBulb != NULL) && (WorldObject_GetId(NewBulb) != OldId));
        TEST_CHECK(run, CountInLog(run, &S, "whose id another object holds") == 1U);
        // Going, the copy leaves it.
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&S.World, Root, WorldComponentType_Prefab));
        WorldPrefabs_Update(S.System);
        WorldObject* Still = NULL;
        TEST_CHECK(run, World_TryGetObject(&S.World, OldId, &Still));
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 0U);
    }
    Scene_Deconstruct(run, &S);
}

/* An object id in a template's data names one of the prefab's objects: in a copy, that object of the copy, however deep it stands. */
static void ObjectIdsInDataNameTheCopysObjects(TestRun* run)
{
    Scene S;
    if (Scene_ConstructBare(run, &S))
    {
        AddPrefab(run, &S, "{ \"name\": \"door\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"frame\", \"data\": {"
            " \"town:handle\": { \"$object\": 5 },"
            " \"town:all\": [ { \"$object\": 1 }, { \"$object\": 5 } ],"
            " \"town:deep\": { \"a\": { \"b\": { \"$object\": 5 } }, \"kind\": { \"$key\": \"town:wood\" }, \"n\": 3 },"
            " \"town:lost\": { \"$object\": 99 } } },"
            " { \"id\": 5, \"parent\": 1, \"name\": \"handle\" } ] }");
        WorldObject* Unrelated = NULL;
        TEST_CHECK_SUCCESS(run, World_CreateObject(&S.World, (const unsigned char*)u8"unrelated", &Unrelated));
        uint64_t Root = AddCopy(run, &S, "a", "door", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        uint64_t Handle = WorldObject_GetId(FindPart(&S, Root, "handle"));
        uint64_t Frame = WorldObject_GetId(FindPart(&S, Root, "frame"));
        uint32_t Key = 0U;
        const DataValue* Value = NULL;
        TEST_CHECK(run, KeyRegistry_TryFind(&S.Keys, (const unsigned char*)u8"town:handle", &Key) && World_TryGetData(&S.World, Frame, Key, &Value)
            && (Value->Type == DataType_Object) && (Value->ObjectValue == Handle));
        TEST_CHECK(run, KeyRegistry_TryFind(&S.Keys, (const unsigned char*)u8"town:all", &Key) && World_TryGetData(&S.World, Frame, Key, &Value)
            && (DataValue_GetListCount(Value) == 2U) && (DataValue_GetListElement(Value, 0U)->ObjectValue == Frame)
            && (DataValue_GetListElement(Value, 1U)->ObjectValue == Handle));
        TEST_CHECK(run, KeyRegistry_TryFind(&S.Keys, (const unsigned char*)u8"town:deep", &Key) && World_TryGetData(&S.World, Frame, Key, &Value));
        const DataValue* A = DataValue_FindCompoundEntry(Value, (const unsigned char*)u8"a");
        const DataValue* B = DataValue_FindCompoundEntry(A, (const unsigned char*)u8"b");
        TEST_CHECK(run, (B != NULL) && (B->ObjectValue == Handle));
        // Keys beside it come through their text; the rest as it is.
        uint32_t Wood = 0U;
        const DataValue* Kind = DataValue_FindCompoundEntry(Value, (const unsigned char*)u8"kind");
        TEST_CHECK(run, KeyRegistry_TryFind(&S.Keys, (const unsigned char*)u8"town:wood", &Wood) && (Kind != NULL) && (Kind->KeyValue == Wood));
        const DataValue* N = DataValue_FindCompoundEntry(Value, (const unsigned char*)u8"n");
        TEST_CHECK(run, (N != NULL) && (N->IntValue == 3));
        // An id no object of the prefab holds reads as none, and never as another object of the world.
        TEST_CHECK(run, KeyRegistry_TryFind(&S.Keys, (const unsigned char*)u8"town:lost", &Key) && World_TryGetData(&S.World, Frame, Key, &Value)
            && (Value->ObjectValue == 0U));
    }
    Scene_Deconstruct(run, &S);
}

/* What removes an object through a nested copy's own overrides can be taken back from outside, and that takes a false that stays written. */
static void AFalseThroughANestedCopyBringsBackWhatItRemoves(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        AddPrefab(run, &S, "{ \"name\": \"thin\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"root\" }, "
            "{ \"id\": 2, \"parent\": 1, \"name\": \"held\", \"components\": { \"prefab\": { \"prefab\": \"stop\", \"object_overrides\": { \"bulb\": { \"is_removed\": true } } } } } ] }");
        uint64_t Root = AddCopy(run, &S, "a", "thin", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, FindPart(&S, Root, "held/bulb") == NULL);
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetObjectRemoved(GetCopy(&S, Root), (const unsigned char*)u8"held/bulb", false));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, FindPart(&S, Root, "held/bulb") != NULL);
        const DataValue* Block = WorldPrefabComponent_GetObjectOverride(GetCopy(&S, Root), (const unsigned char*)u8"held/bulb");
        TEST_CHECK(run, (Block != NULL) && !DataValue_FindCompoundEntry(Block, WORLD_PREFAB_OVERRIDE_IS_REMOVED_KEY)->BoolValue);
        // Taken off, the nested copy's own say holds again.
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_ClearObjectOverride(GetCopy(&S, Root), (const unsigned char*)u8"held/bulb"));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, FindPart(&S, Root, "held/bulb") == NULL);
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetObjectRemoved(GetCopy(&S, Root), (const unsigned char*)u8"held/lamp", true));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, FindPart(&S, Root, "held/lamp") == NULL);
    }
    Scene_Deconstruct(run, &S);
}

/* A section of an override that is no object says so, as every other bad shape does. */
static void ABadComponentsSectionIsWarnedAbout(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        DataValue Three = DataValue_FromInt(3);
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetObjectRow(GetCopy(&S, Root), (const unsigned char*)u8"bulb", NULL, WORLD_PREFAB_OVERRIDE_COMPONENTS_KEY, &Three));
        DataValue Inner = DataValue_FromInt(3);
        DataValue Section = DataValue_CreateTag();
        TEST_CHECK_SUCCESS(run, DataValue_CreateCompound(&Section));
        TEST_CHECK_SUCCESS(run, DataValue_CompoundSet(&Section, (const unsigned char*)u8"light", &Inner));
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetObjectRow(GetCopy(&S, Root), (const unsigned char*)u8"lamp", NULL, WORLD_PREFAB_OVERRIDE_COMPONENTS_KEY, &Section));
        DataValue_Deconstruct(&Section);
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, CountInLog(run, &S, "a components section that is no object, on the object \"bulb\"") == 1U);
        TEST_CHECK(run, CountInLog(run, &S, "overrides the component \"light\" of the object \"lamp\" with what is no object") == 1U);
    }
    Scene_Deconstruct(run, &S);
}

/* A small file asks for a million objects: it gets what the world allows, the update ends and the log stays short. */
static void ANestingAskingForTooMuchGetsTheBudget(TestRun* run)
{
    Scene S;
    if (Scene_ConstructBare(run, &S))
    {
        char Json[4096];
        for (int Level = 0; Level < 6; Level++)
        {
            int Used = snprintf(Json, sizeof(Json), "{ \"name\": \"hostile_%d\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"root\" }", Level);
            for (int Child = 0; (Child < 10) && (Level < 5); Child++)
            {
                Used += snprintf(Json + Used, sizeof(Json) - (size_t)Used, ", { \"id\": %d, \"parent\": 1, \"name\": \"c%d\", \"components\": { \"prefab\": { \"prefab\": \"hostile_%d\" } } }",
                    Child + 2, Child, Level + 1);
            }
            snprintf(Json + Used, sizeof(Json) - (size_t)Used, " ] }");
            AddPrefab(run, &S, Json);
        }
        WorldPrefabsLimits Limits = WorldPrefabsLimits_CreateDefault();
        Limits.MaxObjects = 2000U;
        Limits.MaxWarnings = 8U;
        WorldPrefabs_SetLimits(S.System, &Limits);
        AddCopy(run, &S, "a", "hostile_0", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_IsSettled(S.System));
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 2000U);
        TEST_CHECK(run, World_GetObjectCount(&S.World) == 2001U);
        TEST_CHECK(run, CountInLog(run, &S, "more objects than the world's copies may hold") == 8U);
        TEST_CHECK(run, CountInLog(run, &S, "that is 8 warnings") == 1U);
        WorldPrefabsStats Before = WorldPrefabs_GetStats(S.System);
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).PassCount == Before.PassCount);
        // A rebuild may say it all again, once more.
        WorldPrefabs_Rebuild(S.System);
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 2000U);
        TEST_CHECK(run, CountInLog(run, &S, "more objects than the world's copies may hold") == 16U);
        TEST_CHECK(run, CountInLog(run, &S, "that is 8 warnings") == 2U);
    }
    Scene_Deconstruct(run, &S);
}

/* One update takes copies going from the middle of the list and new ones: what stands is not touched, and each finds its own again. */
static void CopiesComeAndGoInOneUpdate(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Roots[6];
        for (size_t Index = 0U; Index < 4U; Index++)
        {
            Roots[Index] = AddCopy(run, &S, "a", "stop", (Vector3){ (float)Index, 0.0f, 0.0f });
        }
        WorldPrefabs_Update(S.System);
        WorldObject* Standing[2] = { FindPart(&S, Roots[0], "lamp"), FindPart(&S, Roots[3], "lamp") };
        TEST_CHECK(run, (Standing[0] != NULL) && (Standing[1] != NULL));
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&S.World, Roots[1], WorldComponentType_Prefab));
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&S.World, Roots[2], WorldComponentType_Prefab));
        Roots[4] = AddCopy(run, &S, "a", "stop", (Vector3){ 4.0f, 0.0f, 0.0f });
        Roots[5] = AddCopy(run, &S, "a", "stop", (Vector3){ 5.0f, 0.0f, 0.0f });
        WorldPrefabsStats Before = WorldPrefabs_GetStats(S.System);
        WorldPrefabs_Update(S.System);
        WorldPrefabsStats After = WorldPrefabs_GetStats(S.System);
        TEST_CHECK(run, After.CopyCount == 4U);
        TEST_CHECK(run, After.ObjectCount == 28U);
        TEST_CHECK(run, After.ApplyCount == (Before.ApplyCount + 2U)); // the new ones alone
        TEST_CHECK(run, (FindPart(&S, Roots[0], "lamp") == Standing[0]) && (FindPart(&S, Roots[3], "lamp") == Standing[1]));
        const size_t Listed[4] = { 0U, 3U, 4U, 5U };
        for (size_t Index = 0U; Index < 4U; Index++)
        {
            TEST_CHECK(run, WorldPrefabs_GetPrefab(S.System, Roots[Listed[Index]]) != NULL);
            TEST_CHECK(run, FindPart(&S, Roots[Listed[Index]], "lamp") != NULL);
        }
        for (size_t Index = 1U; Index < 3U; Index++)
        {
            TEST_CHECK(run, WorldPrefabs_GetPrefab(S.System, Roots[Index]) == NULL);
            TEST_CHECK(run, FindPart(&S, Roots[Index], "lamp") == NULL);
        }
        // Put back, a component is a new one at the end of the list, and its objects have the ids they had.
        WorldComponent* Again = NULL;
        TEST_CHECK_SUCCESS(run, World_AddComponent(&S.World, Roots[1], WorldComponentType_Prefab, &Again));
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetPrefab(WorldPrefabComponent_FromComponent(Again), (const unsigned char*)u8"stop"));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_GetPrefab(S.System, Roots[1]) != NULL);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 35U);
        // The first of the list going leaves the rest as they were.
        After = WorldPrefabs_GetStats(S.System);
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&S.World, Roots[0], WorldComponentType_Prefab));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ApplyCount == After.ApplyCount);
        TEST_CHECK(run, FindPart(&S, Roots[3], "lamp") == Standing[1]);
        TEST_CHECK(run, FindPart(&S, Roots[0], "lamp") == NULL);
    }
    Scene_Deconstruct(run, &S);
}

/* A row an override no longer writes comes back once; from then on nothing writes it, and it is left alone like any other. */
static void ARowOnceRestoredIsLeftAlone(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        const unsigned char* const Intensity = (const unsigned char*)u8"intensity";
        WorldObject* Lamp = FindPart(&S, Root, "lamp");
        WorldLightComponent* Light = (Lamp != NULL) ? WorldLightComponent_FromComponent(WorldObject_GetComponent(Lamp, WorldComponentType_Light)) : NULL;
        if (TEST_CHECK(run, Light != NULL))
        {
            float PrefabSays = Light->_intensity;
            DataValue Three = DataValue_FromFloat((double)PrefabSays + 3.0);
            TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetObjectRow(GetCopy(&S, Root), (const unsigned char*)u8"lamp", (const unsigned char*)u8"light", Intensity, &Three));
            WorldPrefabs_Update(S.System);
            TEST_CHECK(run, NEAR(Light->_intensity, (double)PrefabSays + 3.0, 1e-5));
            // taken off, it comes back
            TEST_CHECK_SUCCESS(run, WorldPrefabComponent_ClearObjectOverride(GetCopy(&S, Root), (const unsigned char*)u8"lamp"));
            WorldPrefabs_Update(S.System);
            TEST_CHECK(run, NEAR(Light->_intensity, (double)PrefabSays, 1e-5));
            // a script changes it: another value changing writes what it writes, and this stays
            PropertyValue Script = PropertyValue_FromFloat(PrefabSays + 7.0f);
            TEST_CHECK_SUCCESS(run, Properties_Set(WorldLightComponent_GetPropertyTable(), Light, Intensity, &Script));
            SetNumberValue(run, &S, Root, "lamp_range", 9.0);
            WorldPrefabs_Update(S.System);
            TEST_CHECK(run, NEAR(GetLampRange(&S, Root, "lamp"), 9.0, 1e-6));
            TEST_CHECK(run, NEAR(Light->_intensity, (double)PrefabSays + 7.0, 1e-5));
        }
    }
    Scene_Deconstruct(run, &S);
}

/* Rows count as work too, with every object made. */
static void WritesWaitForTheNextUpdateToo(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Roots[3];
        for (size_t Index = 0U; Index < 3U; Index++)
        {
            Roots[Index] = AddCopy(run, &S, "a", "stop", (Vector3){ (float)Index, 0.0f, 0.0f });
        }
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_IsSettled(S.System));
        WorldPrefabsLimits Limits = WorldPrefabsLimits_CreateDefault();
        Limits.MaxUpdateWork = 10U;
        WorldPrefabs_SetLimits(S.System, &Limits);
        for (size_t Index = 0U; Index < 3U; Index++)
        {
            SetNumberValue(run, &S, Roots[Index], "lamp_range", 9.0);
        }
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, !WorldPrefabs_IsSettled(S.System));
        TEST_CHECK(run, NEAR(GetLampRange(&S, Roots[0], "lamp"), 9.0, 1e-6)); // the first always goes
        TEST_CHECK(run, NEAR(GetLampRange(&S, Roots[2], "lamp"), 5.0, 1e-6)); // the last has to wait
        size_t Updates = 1U;
        while (!WorldPrefabs_IsSettled(S.System) && (Updates < 20U))
        {
            WorldPrefabs_Update(S.System);
            Updates++;
        }
        TEST_CHECK(run, WorldPrefabs_IsSettled(S.System));
        TEST_CHECK(run, NEAR(GetLampRange(&S, Roots[2], "lamp"), 9.0, 1e-6));
        // Bytes count too: a long value is more work than its one row.
        AddPrefab(run, &S, "{ \"name\": \"long\", \"format_version\": 3, \"properties\": { \"note\": { \"type\": \"string\", \"default\": \"\", "
            "\"targets\": [ { \"object\": \"held\", \"property\": \"note\" } ] } }, \"objects\": [ { \"id\": 1, \"name\": \"root\" }, "
            "{ \"id\": 2, \"parent\": 1, \"name\": \"held\", \"components\": { \"prefab\": { } } } ] }");
        WorldPrefabs_SetLimits(S.System, &(WorldPrefabsLimits){ .MaxObjects = 100U, .MaxBytes = WORLD_PREFABS_MAX_TOTAL_BYTES, .MaxUpdateWork = WORLD_PREFABS_MAX_UPDATE_WORK, .MaxWarnings = 64U });
        uint64_t Long[2] = { AddCopy(run, &S, "long_a", "long", (Vector3){ 0.0f, 0.0f, 0.0f }), AddCopy(run, &S, "long_b", "long", (Vector3){ 0.0f, 0.0f, 0.0f }) };
        WorldPrefabs_Update(S.System);
        char Text[4001];
        memset(Text, 'x', 4000U);
        Text[4000] = 0;
        // The first goes whatever it costs; the second starts, finds it can pay for its objects and not for its row, and that waits.
        WorldPrefabs_SetLimits(S.System, &(WorldPrefabsLimits){ .MaxObjects = 100U, .MaxBytes = WORLD_PREFABS_MAX_TOTAL_BYTES, .MaxUpdateWork = 100U, .MaxWarnings = 64U });
        SetStringValue(run, &S, Long[0], "note", Text);
        SetStringValue(run, &S, Long[1], "note", Text);
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, !WorldPrefabs_IsSettled(S.System));
        const DataValue* Note = WorldPrefabComponent_GetValue(WorldPrefabComponent_FromComponent(WorldObject_GetComponent(FindPart(&S, Long[0], "held"), WorldComponentType_Prefab)), (const unsigned char*)u8"note");
        TEST_CHECK(run, (Note != NULL) && (strlen((const char*)Note->StringValue) == 4000U));
        Note = WorldPrefabComponent_GetValue(WorldPrefabComponent_FromComponent(WorldObject_GetComponent(FindPart(&S, Long[1], "held"), WorldComponentType_Prefab)), (const unsigned char*)u8"note");
        TEST_CHECK(run, (Note == NULL) || (strlen((const char*)Note->StringValue) != 4000U));
        WorldPrefabs_SetLimits(S.System, &(WorldPrefabsLimits){ .MaxObjects = 100U, .MaxBytes = WORLD_PREFABS_MAX_TOTAL_BYTES, .MaxUpdateWork = WORLD_PREFABS_MAX_UPDATE_WORK, .MaxWarnings = 64U });
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_IsSettled(S.System));
        Note = WorldPrefabComponent_GetValue(WorldPrefabComponent_FromComponent(WorldObject_GetComponent(FindPart(&S, Long[1], "held"), WorldComponentType_Prefab)), (const unsigned char*)u8"note");
        TEST_CHECK(run, (Note != NULL) && (strlen((const char*)Note->StringValue) == 4000U));
        // So do the objects a copy looks at, with no row to write.
        AddPrefab(run, &S, "{ \"name\": \"many\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"root\" }, "
            "{ \"id\": 2, \"parent\": 1, \"name\": \"a\" }, { \"id\": 3, \"parent\": 1, \"name\": \"b\" }, { \"id\": 4, \"parent\": 1, \"name\": \"c\" }, "
            "{ \"id\": 5, \"parent\": 1, \"name\": \"d\" }, { \"id\": 6, \"parent\": 1, \"name\": \"e\" }, { \"id\": 7, \"parent\": 1, \"name\": \"f\" } ] }");
        size_t ObjectsBefore = WorldPrefabs_GetStats(S.System).ObjectCount;
        WorldPrefabs_SetLimits(S.System, &(WorldPrefabsLimits){ .MaxObjects = 100U, .MaxBytes = WORLD_PREFABS_MAX_TOTAL_BYTES, .MaxUpdateWork = 8U, .MaxWarnings = 64U });
        AddCopy(run, &S, "many_a", "many", (Vector3){ 0.0f, 0.0f, 0.0f });
        AddCopy(run, &S, "many_b", "many", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, !WorldPrefabs_IsSettled(S.System));
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == (ObjectsBefore + 7U));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_IsSettled(S.System));
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == (ObjectsBefore + 14U));
        Limits.MaxObjects = 100U;
        // A copy with more to do than one update allows still goes: it never starves.
        Limits.MaxUpdateWork = 1U;
        WorldPrefabs_SetLimits(S.System, &Limits);
        SetNumberValue(run, &S, Roots[1], "lamp_range", 4.0);
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_IsSettled(S.System));
        TEST_CHECK(run, NEAR(GetLampRange(&S, Roots[1], "lamp"), 4.0, 1e-6));
    }
    Scene_Deconstruct(run, &S);
}

/* Every way an axis writes, and that it leaves the rest. */
static void AnAxisWritesEveryKindOfVector(TestRun* run)
{
    Scene S;
    if (Scene_ConstructBare(run, &S))
    {
        AddPrefab(run, &S, "{ \"name\": \"axes\", \"format_version\": 3, \"properties\": {"
            "  \"whole\": { \"type\": \"float\", \"default\": 2, \"targets\": [ { \"object\": \"a\", \"row\": \"scale\", \"axis\": \"all\" } ] },"
            "  \"far\": { \"type\": \"float\", \"default\": 7.5, \"targets\": [ { \"object\": \"a\", \"row\": \"position\", \"axis\": \"x\" },"
            "      { \"object\": \"b\", \"row\": \"position\", \"axis\": \"z\" } ] },"
            "  \"turn\": { \"type\": \"angle\", \"default\": 90, \"targets\": [ { \"object\": \"a\", \"row\": \"rotation\", \"axis\": \"x\" },"
            "      { \"object\": \"b\", \"row\": \"rotation\", \"axis\": \"z\" }, { \"object\": \"c\", \"row\": \"rotation\", \"axis\": \"all\" } ] },"
            "  \"pivot_x\": { \"type\": \"float\", \"default\": 0.25, \"targets\": [ { \"object\": \"c\", \"component\": \"sprite\", \"row\": \"pivot\", \"axis\": \"x\" } ] },"
            "  \"pivot_y\": { \"type\": \"float\", \"default\": 0.75, \"targets\": [ { \"object\": \"b\", \"component\": \"sprite\", \"row\": \"pivot\", \"axis\": \"y\" } ] }"
            "  }, \"objects\": [ { \"id\": 1, \"name\": \"a\", \"position\": [1, 2, 3], \"rotation\": [0, 30, 40] },"
            "  { \"id\": 2, \"parent\": 1, \"name\": \"b\", \"position\": [1, 2, 3], \"rotation\": [10, 20, 0], \"components\": { \"sprite\": { } } },"
            "  { \"id\": 3, \"parent\": 1, \"name\": \"c\", \"components\": { \"sprite\": { } } } ] }");
        uint64_t Root = AddCopy(run, &S, "a", "axes", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        WorldObject* A = FindPart(&S, Root, "a");
        WorldObject* B = FindPart(&S, Root, "b");
        WorldObject* C = FindPart(&S, Root, "c");
        TEST_CHECK(run, (A != NULL) && (B != NULL) && (C != NULL));
        if ((A != NULL) && (B != NULL) && (C != NULL))
        {
            Vector3 Scale = WorldObject_GetScale(A);
            TEST_CHECK(run, NEAR(Scale.x, 2.0, 1e-6) && NEAR(Scale.y, 2.0, 1e-6) && NEAR(Scale.z, 2.0, 1e-6));
            Vector3Double Place = WorldObject_GetPosition(A);
            TEST_CHECK(run, NEAR(Place.X, 7.5, 1e-9) && NEAR(Place.Y, 2.0, 1e-9) && NEAR(Place.Z, 3.0, 1e-9));
            Place = WorldObject_GetPosition(B);
            TEST_CHECK(run, NEAR(Place.X, 1.0, 1e-9) && NEAR(Place.Y, 2.0, 1e-9) && NEAR(Place.Z, 7.5, 1e-9));
            Vector3 Turn = WorldObject_GetRotation(A);
            TEST_CHECK(run, NEAR(Turn.x, 90.0 * DEGREES_TO_RADIANS, 1e-5) && NEAR(Turn.y, 30.0 * DEGREES_TO_RADIANS, 1e-5) && NEAR(Turn.z, 40.0 * DEGREES_TO_RADIANS, 1e-5));
            Turn = WorldObject_GetRotation(B);
            TEST_CHECK(run, NEAR(Turn.x, 10.0 * DEGREES_TO_RADIANS, 1e-5) && NEAR(Turn.z, 90.0 * DEGREES_TO_RADIANS, 1e-5));
            Turn = WorldObject_GetRotation(C);
            TEST_CHECK(run, NEAR(Turn.x, 90.0 * DEGREES_TO_RADIANS, 1e-5) && NEAR(Turn.y, 90.0 * DEGREES_TO_RADIANS, 1e-5) && NEAR(Turn.z, 90.0 * DEGREES_TO_RADIANS, 1e-5));
            PropertyValue Pivot;
            const PropertyTable* SpriteTable = WorldSpriteComponent_GetPropertyTable();
            TEST_CHECK_SUCCESS(run, Properties_Get(SpriteTable, WorldObject_GetComponent(C, WorldComponentType_Sprite), (const unsigned char*)u8"pivot", &Pivot));
            TEST_CHECK(run, NEAR(Pivot.Data.Vector2Value.x, 0.25, 1e-6) && NEAR(Pivot.Data.Vector2Value.y, 0.5, 1e-6));
            TEST_CHECK_SUCCESS(run, Properties_Get(SpriteTable, WorldObject_GetComponent(B, WorldComponentType_Sprite), (const unsigned char*)u8"pivot", &Pivot));
            TEST_CHECK(run, NEAR(Pivot.Data.Vector2Value.x, 0.5, 1e-6) && NEAR(Pivot.Data.Vector2Value.y, 0.75, 1e-6));
        }
    }
    Scene_Deconstruct(run, &S);
}

/* The setters go through the table, say what they refuse and leave what is empty out of the file. */
static void TheComponentsSettersKeepItsRows(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabComponent* Copy = GetCopy(&S, Root);
        DataValue One = DataValue_FromInt(1);
        uint32_t Revision = WorldComponent_GetRevision(WorldPrefabComponent_AsComponent(Copy));
        TEST_CHECK_ERROR(run, WorldPrefabComponent_SetValue(Copy, (const unsigned char*)u8"Not A Name", &One), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, WorldPrefabComponent_SetValue(NULL, (const unsigned char*)u8"a", &One), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, WorldPrefabComponent_SetValue(Copy, NULL, &One), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, WorldPrefabComponent_SetObjectRow(Copy, NULL, NULL, (const unsigned char*)u8"a", &One), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, WorldPrefabComponent_SetObjectRow(Copy, (const unsigned char*)u8"a", NULL, NULL, &One), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, WorldPrefabComponent_SetObjectRemoved(NULL, (const unsigned char*)u8"a", true), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, WorldPrefabComponent_ClearObjectOverride(Copy, NULL), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, WorldPrefabComponent_SetPrefab(NULL, NULL), ErrorCode_IllegalArgument);
        TEST_CHECK(run, WorldComponent_GetRevision(WorldPrefabComponent_AsComponent(Copy)) == Revision); // nothing of it changed

        TEST_CHECK(run, WorldPrefabComponent_GetValue(Copy, (const unsigned char*)u8"a") == NULL);
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetValue(Copy, (const unsigned char*)u8"a", &One));
        TEST_CHECK(run, WorldComponent_GetRevision(WorldPrefabComponent_AsComponent(Copy)) != Revision);
        const DataValue* Read = WorldPrefabComponent_GetValue(Copy, (const unsigned char*)u8"a");
        TEST_CHECK(run, (Read != NULL) && (Read->IntValue == 1));
        // the last one going leaves none at all, not an empty compound
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetValue(Copy, (const unsigned char*)u8"a", NULL));
        TEST_CHECK(run, Copy->_values == NULL);

        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetObjectRemoved(Copy, (const unsigned char*)u8"a", true));
        const DataValue* Block = WorldPrefabComponent_GetObjectOverride(Copy, (const unsigned char*)u8"a");
        TEST_CHECK(run, (Block != NULL) && DataValue_FindCompoundEntry(Block, WORLD_PREFAB_OVERRIDE_IS_REMOVED_KEY)->BoolValue);
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetObjectRow(Copy, (const unsigned char*)u8"a", (const unsigned char*)u8"light", (const unsigned char*)u8"range", &One));
        Block = WorldPrefabComponent_GetObjectOverride(Copy, (const unsigned char*)u8"a");
        const DataValue* Components = DataValue_FindCompoundEntry(Block, WORLD_PREFAB_OVERRIDE_COMPONENTS_KEY);
        TEST_CHECK(run, (Components != NULL) && (DataValue_FindCompoundEntry(Components, (const unsigned char*)u8"light") != NULL));
        // taken off piece by piece: the emptied levels go, and with them the block and the row
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetObjectRow(Copy, (const unsigned char*)u8"a", (const unsigned char*)u8"light", (const unsigned char*)u8"range", NULL));
        Block = WorldPrefabComponent_GetObjectOverride(Copy, (const unsigned char*)u8"a");
        TEST_CHECK(run, (Block != NULL) && (DataValue_FindCompoundEntry(Block, WORLD_PREFAB_OVERRIDE_COMPONENTS_KEY) == NULL));
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetObjectRemoved(Copy, (const unsigned char*)u8"a", false));
        TEST_CHECK(run, WorldPrefabComponent_GetObjectOverride(Copy, (const unsigned char*)u8"a") == NULL);
        TEST_CHECK(run, Copy->_objectOverrides == NULL);
        // clearing what is not there changes nothing
        Revision = WorldComponent_GetRevision(WorldPrefabComponent_AsComponent(Copy));
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_ClearObjectOverride(Copy, (const unsigned char*)u8"a"));
        TEST_CHECK(run, WorldComponent_GetRevision(WorldPrefabComponent_AsComponent(Copy)) == Revision);
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetObjectRow(Copy, (const unsigned char*)u8"a", NULL, (const unsigned char*)u8"scale", &One));
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_ClearObjectOverride(Copy, (const unsigned char*)u8"a"));
        TEST_CHECK(run, Copy->_objectOverrides == NULL);
    }
    Scene_Deconstruct(run, &S);
}

static void WhatOneUpdateCannotDoWaitsForTheNext(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        WorldPrefabsLimits Limits = WorldPrefabsLimits_CreateDefault();
        Limits.MaxUpdateWork = 8U;
        WorldPrefabs_SetLimits(S.System, &Limits);
        uint64_t Roots[6];
        for (size_t Index = 0U; Index < 6U; Index++)
        {
            Roots[Index] = AddCopy(run, &S, "a", "stop", (Vector3){ (float)Index, 0.0f, 0.0f });
        }
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, !WorldPrefabs_IsSettled(S.System));
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount < 42U);
        size_t Updates = 1U;
        while (!WorldPrefabs_IsSettled(S.System) && (Updates < 100U))
        {
            WorldPrefabs_Update(S.System);
            Updates++;
        }
        TEST_CHECK(run, WorldPrefabs_IsSettled(S.System));
        TEST_CHECK(run, Updates > 1U);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 42U);
        for (size_t Index = 0U; Index < 6U; Index++)
        {
            TEST_CHECK(run, NEAR(GetLampRange(&S, Roots[Index], "lamp"), 5.0, 1e-6));
        }
    }
    Scene_Deconstruct(run, &S);
}


// Tests: going.
static void TheComponentGoingTakesItsObjects(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "shelter", (Vector3){ 0.0f, 0.0f, 0.0f });
        uint64_t Other = AddCopy(run, &S, "b", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        size_t ObjectsBefore = World_GetObjectCount(&S.World);
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&S.World, Root, WorldComponentType_Prefab));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, World_GetObjectCount(&S.World) == (ObjectsBefore - 17U));
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 7U);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).CopyCount == 1U);
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&S.World, Other));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 0U);
        TEST_CHECK(run, World_GetObjectCount(&S.World) == 1U);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).CopyCount == 0U);
    }
    Scene_Deconstruct(run, &S);
}

static void TheSystemGoingTakesWhatItMade(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        AddCopy(run, &S, "a", "shelter", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, World_GetObjectCount(&S.World) == 18U);
        WorldPrefabs_Destroy(S.System);
        S.System = NULL;
        TEST_CHECK(run, World_GetObjectCount(&S.World) == 1U);
        // a new one over the same world starts clean: nothing of the old stands on its ids
        TEST_CHECK_SUCCESS(run, WorldPrefabs_Create(&S.World, S.Fixture.Manager, &S.Log, &S.System));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, World_GetObjectCount(&S.World) == 18U);
        TEST_CHECK(run, CountInLog(run, &S, "takes another") == 0U);
    }
    Scene_Deconstruct(run, &S);
}

static void RebuildMakesCopiesFromTheDefinitionAsItIsNow(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        SetVectorRow(run, &S, Root, "bulb", "position", 1.0, 2.0, 3.0);
        SetVectorRow(run, &S, Root, "lamp", "position", 4.0, 5.0, 6.0);
        WorldPrefabs_Update(S.System);
        uint64_t LampId = WorldObject_GetId(FindPart(&S, Root, "lamp"));
        // the same name, the bulb gone and a new object
        AddPrefab(run, &S, "{ \"name\": \"stop\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"root\" }, "
            "{ \"id\": 2, \"parent\": 1, \"name\": \"lamp\" }, { \"id\": 3, \"parent\": 1, \"name\": \"sign\" } ] }");
        // an asset never changes while it is loaded
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, FindPart(&S, Root, "sign") == NULL);
        WorldPrefabs_Rebuild(S.System);
        TEST_CHECK(run, FindPart(&S, Root, "sign") != NULL);
        TEST_CHECK(run, FindPart(&S, Root, "bulb") == NULL);
        WorldObject* Lamp = FindPart(&S, Root, "lamp");
        TEST_CHECK(run, (Lamp != NULL) && (WorldObject_GetId(Lamp) == LampId));
        TEST_CHECK(run, (Lamp != NULL) && NEAR(WorldObject_GetPosition(Lamp).X, 4.0, 1e-9)); // its override stays
        TEST_CHECK(run, CountInLog(run, &S, "no longer has: \"bulb\"") == 1U);
        TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 3U);
    }
    Scene_Deconstruct(run, &S);
}


// Tests: data.
static void TagsAndDataReachTheWorldsOwnRegistry(TestRun* run)
{
    Scene S;
    if (Scene_ConstructBare(run, &S))
    {
        uint32_t Filler = 0U;
        TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(&S.Keys, (const unsigned char*)u8"tbp:something_first", &Filler));
        AddPrefab(run, &S, "{ \"name\": \"tagged\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"root\", "
            "\"tags\": [\"town:lamp\"], \"data\": { \"town:visits\": 3, \"town:next\": { \"$key\": \"town:elsewhere\" }, "
            "\"town:deep\": { \"list\": [ { \"$key\": \"town:inner\" } ] } } } ] }");
        uint64_t Root = AddCopy(run, &S, "a", "tagged", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        WorldObject* Part = FindPart(&S, Root, "root");
        TEST_CHECK(run, Part != NULL);
        uint32_t Lamp = 0U;
        uint32_t Visits = 0U;
        uint32_t Next = 0U;
        uint32_t Elsewhere = 0U;
        uint32_t Deep = 0U;
        uint32_t Inner = 0U;
        TEST_CHECK(run, KeyRegistry_TryFind(&S.Keys, (const unsigned char*)u8"town:lamp", &Lamp));
        TEST_CHECK(run, KeyRegistry_TryFind(&S.Keys, (const unsigned char*)u8"town:visits", &Visits));
        TEST_CHECK(run, KeyRegistry_TryFind(&S.Keys, (const unsigned char*)u8"town:next", &Next));
        TEST_CHECK(run, KeyRegistry_TryFind(&S.Keys, (const unsigned char*)u8"town:elsewhere", &Elsewhere));
        TEST_CHECK(run, KeyRegistry_TryFind(&S.Keys, (const unsigned char*)u8"town:deep", &Deep));
        TEST_CHECK(run, KeyRegistry_TryFind(&S.Keys, (const unsigned char*)u8"town:inner", &Inner));
        if (Part != NULL)
        {
            uint64_t Id = WorldObject_GetId(Part);
            TEST_CHECK(run, World_HasTag(&S.World, Id, Lamp));
            const DataValue* Value = NULL;
            TEST_CHECK(run, World_TryGetData(&S.World, Id, Visits, &Value) && (Value->Type == DataType_Int) && (Value->IntValue == 3));
            TEST_CHECK(run, World_TryGetData(&S.World, Id, Next, &Value) && (Value->Type == DataType_Key) && (Value->KeyValue == Elsewhere));
            TEST_CHECK(run, World_TryGetData(&S.World, Id, Deep, &Value));
            const DataValue* List = DataValue_FindCompoundEntry(Value, (const unsigned char*)u8"list");
            TEST_CHECK(run, (List != NULL) && (DataValue_GetListElement(List, 0U)->KeyValue == Inner));
        }
    }
    Scene_Deconstruct(run, &S);
}


// Tests: world files.
static void CheckCopyAfterLoad(TestRun* run, Scene* scene, World* loaded, uint64_t rootId)
{
    WorldPrefabs* System = NULL;
    if (!TEST_CHECK_SUCCESS(run, WorldPrefabs_Create(loaded, scene->Fixture.Manager, NULL, &System)))
    {
        return;
    }
    TEST_CHECK(run, World_GetObjectCount(loaded) == 1U); // the file holds the root alone
    WorldPrefabs_Update(System);
    TEST_CHECK(run, World_GetObjectCount(loaded) == 7U); // the root and the six left of the seven
    uint64_t Id = 0U;
    WorldObject* Object = NULL;
    TEST_CHECK(run, WorldPrefabs_TryFindObject(System, rootId, (const unsigned char*)u8"roof", &Id) && World_TryGetObject(loaded, Id, &Object));
    TEST_CHECK(run, (Object != NULL) && (WorldObject_GetTint(Object).Tint.r == 0x2E));
    TEST_CHECK(run, WorldPrefabs_TryFindObject(System, rootId, (const unsigned char*)u8"door", &Id) && World_TryGetObject(loaded, Id, &Object));
    TEST_CHECK(run, !WorldObject_IsEnabled(Object));
    TEST_CHECK(run, NEAR(WorldObject_GetRotation(Object).y, 45.0 * DEGREES_TO_RADIANS, 1e-6));
    TEST_CHECK(run, WorldPrefabs_TryFindObject(System, rootId, (const unsigned char*)u8"bulb", &Id));
    TEST_CHECK(run, Id == WorldPrefabs_DeriveObjectId(rootId, (const unsigned char*)u8"bulb", 0U));
    TEST_CHECK(run, World_TryGetObject(loaded, Id, &Object) && NEAR(WorldObject_GetRotation(Object).y, 35.0 * DEGREES_TO_RADIANS, 1e-6));
    TEST_CHECK(run, !WorldPrefabs_TryFindObject(System, rootId, (const unsigned char*)u8"garage", &Id));
    WorldPrefabs_Destroy(System);
}

static void OnlyTheRootGoesIntoWorldFiles(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "stop", (Vector3){ 2.0f, 0.0f, 0.0f });
        SetStringValue(run, &S, Root, "roof_color", "#2e4a7a");
        SetStringValue(run, &S, Root, "layout", "garage");
        SetNumberValue(run, &S, Root, "door_angle", 45.0);
        SetVectorRow(run, &S, Root, "bulb", "rotation", 0.0, 35.0, 0.0);
        TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetObjectRemoved(GetCopy(&S, Root), (const unsigned char*)u8"garage", true));
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, World_GetObjectCount(&S.World) == 7U);
        uint64_t NextId = World_GetNextObjectId(&S.World);

        WorldFileReadOptions Options = WorldFileReadOptions_Create(&S.Keys, S.Fixture.Manager, NULL);
        GenericBuffer Json;
        GenericBuffer_AllocateVariable(&Json, 0U, sizeof(unsigned char));
        if (TEST_CHECK_SUCCESS(run, WorldFile_WriteJSON(&S.World, &Json)))
        {
            GenericBuffer_AppendByte(&Json, 0U);
            TEST_CHECK(run, strstr((const char*)Json._data, "\"roof_color\": \"#2e4a7a\"") != NULL);
            TEST_CHECK(run, strstr((const char*)Json._data, "\"is_removed\": true") != NULL);
            TEST_CHECK(run, strstr((const char*)Json._data, "roof_cap") == NULL);
            World Loaded;
            if (TEST_CHECK_SUCCESS(run, WorldFile_ReadJSON(Json._data, Json._count - 1U, &Options, &Loaded)))
            {
                TEST_CHECK(run, World_GetNextObjectId(&Loaded) == NextId);
                CheckCopyAfterLoad(run, &S, &Loaded, Root);
                TEST_CHECK_SUCCESS(run, World_Deconstruct(&Loaded));
            }
        }
        Memory_Free(Json._data);

        GenericBuffer Binary;
        GenericBuffer_AllocateVariable(&Binary, 0U, sizeof(unsigned char));
        if (TEST_CHECK_SUCCESS(run, WorldFile_WriteGHDF(&S.World, &Binary)))
        {
            World Loaded;
            if (TEST_CHECK_SUCCESS(run, WorldFile_ReadGHDF(Binary._data, Binary._count, &Options, &Loaded)))
            {
                CheckCopyAfterLoad(run, &S, &Loaded, Root);
                TEST_CHECK_SUCCESS(run, World_Deconstruct(&Loaded));
            }
        }
        Memory_Free(Binary._data);
    }
    Scene_Deconstruct(run, &S);
}

static void AWorldFileReadsAndWritesACopysBlockAsItIs(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        // as a person writes one: mixed numbers in a vector, nested paths, and it comes back the same
        const char* const Text =
            "{\n"
            "  \"format_version\": 3,\n"
            "  \"next_object_id\": 2,\n"
            "  \"objects\": [\n"
            "    {\n"
            "      \"id\": 1,\n"
            "      \"position\": [0.0, 1.0, 0.0],\n"
            "      \"components\": {\n"
            "        \"prefab\": {\n"
            "          \"prefab\": \"tbp:shelter\",\n"
            "          \"values\": { \"stop_roof\": \"#0a0b0c\" },\n"
            "          \"object_overrides\": {\n"
            "            \"stop_a/bulb\": { \"position\": [0.5, 2, 0] },\n"
            "            \"stop_b\": { \"is_removed\": true }\n"
            "          }\n"
            "        }\n"
            "      }\n"
            "    }\n"
            "  ]\n"
            "}\n";
        WorldFileReadOptions Options = WorldFileReadOptions_Create(&S.Keys, S.Fixture.Manager, NULL);
        World Loaded;
        if (TEST_CHECK_SUCCESS(run, WorldFile_ReadJSON((const unsigned char*)Text, strlen(Text), &Options, &Loaded)))
        {
            WorldPrefabs* System = NULL;
            if (TEST_CHECK_SUCCESS(run, WorldPrefabs_Create(&Loaded, S.Fixture.Manager, NULL, &System)))
            {
                WorldPrefabs_Update(System);
                uint64_t Id = 0U;
                WorldObject* Bulb = NULL;
                TEST_CHECK(run, WorldPrefabs_TryFindObject(System, 1U, (const unsigned char*)u8"stop_a/bulb", &Id)
                    && World_TryGetObject(&Loaded, Id, &Bulb));
                TEST_CHECK(run, (Bulb != NULL) && NEAR(WorldObject_GetPosition(Bulb).X, 0.5, 1e-9) && NEAR(WorldObject_GetPosition(Bulb).Y, 2.0, 1e-9));
                TEST_CHECK(run, !WorldPrefabs_TryFindObject(System, 1U, (const unsigned char*)u8"stop_b", &Id));
                WorldPrefabs_Destroy(System);
            }
            GenericBuffer Written;
            GenericBuffer_AllocateVariable(&Written, 0U, sizeof(unsigned char));
            TEST_CHECK_SUCCESS(run, WorldFile_WriteJSON(&Loaded, &Written));
            GenericBuffer_AppendByte(&Written, 0U);
            TEST_CHECK(run, strstr((const char*)Written._data, "\"stop_roof\": \"#0a0b0c\"") != NULL);
            TEST_CHECK(run, strstr((const char*)Written._data, "\"position\": [0.5, 2.0, 0.0]") != NULL);
            Memory_Free(Written._data);
            TEST_CHECK_SUCCESS(run, World_Deconstruct(&Loaded));
        }
        // a block that is no object is refused by the table
        const char* const Bad = "{ \"format_version\": 3, \"objects\": [ { \"id\": 1, \"components\": { \"prefab\": { \"values\": 3 } } } ] }";
        TEST_CHECK_ERROR(run, WorldFile_ReadJSON((const unsigned char*)Bad, strlen(Bad), &Options, &Loaded), ErrorCode_InvalidJSON);
    }
    Scene_Deconstruct(run, &S);
}


// Tests: selection.
static void AClickWalksUpToACopysRoot(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a", "shelter", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldObject* Loose = NULL;
        TEST_CHECK_SUCCESS(run, World_CreateObject(&S.World, (const unsigned char*)u8"loose", &Loose));
        WorldPrefabs_Update(S.System);
        WorldObject* Bulb = FindPart(&S, Root, "stop_a/bulb");
        WorldObject* Nested = FindPart(&S, Root, "stop_a");
        WorldObject* Own = FindPart(&S, Root, "root");
        uint64_t Found = 0U;
        TEST_CHECK(run, WorldPrefabs_TryGetCopyRoot(&S.World, WorldObject_GetId(Bulb), &Found) && (Found == WorldObject_GetId(Nested)));
        TEST_CHECK(run, WorldPrefabs_TryGetOutermostCopyRoot(&S.World, WorldObject_GetId(Bulb), &Found) && (Found == Root));
        TEST_CHECK(run, WorldPrefabs_TryGetCopyRoot(&S.World, WorldObject_GetId(Nested), &Found) && (Found == WorldObject_GetId(Nested)));
        TEST_CHECK(run, WorldPrefabs_TryGetOutermostCopyRoot(&S.World, WorldObject_GetId(Nested), &Found) && (Found == Root));
        TEST_CHECK(run, WorldPrefabs_TryGetCopyRoot(&S.World, WorldObject_GetId(Own), &Found) && (Found == Root));
        TEST_CHECK(run, WorldPrefabs_TryGetCopyRoot(&S.World, Root, &Found) && (Found == Root));
        TEST_CHECK(run, WorldPrefabs_TryGetOutermostCopyRoot(&S.World, Root, &Found) && (Found == Root));
        TEST_CHECK(run, !WorldPrefabs_TryGetCopyRoot(&S.World, WorldObject_GetId(Loose), &Found));
        TEST_CHECK(run, !WorldPrefabs_TryGetOutermostCopyRoot(&S.World, WorldObject_GetId(Loose), &Found));
        TEST_CHECK(run, !WorldPrefabs_TryGetCopyRoot(&S.World, 999999U, &Found));

        GenericBuffer Ids;
        GenericBuffer_AllocateVariable(&Ids, 0U, sizeof(uint64_t));
        TEST_CHECK(run, WorldPrefabs_GetCopyObjects(S.System, Root, &Ids));
        TEST_CHECK(run, Ids._count == 3U); // its own, not the nested copies'
        TEST_CHECK(run, ((const uint64_t*)Ids._data)[0] == WorldObject_GetId(Own));
        TEST_CHECK(run, !WorldPrefabs_GetCopyObjects(S.System, WorldObject_GetId(Loose), &Ids));
        Memory_Free(Ids._data);
        uint64_t Unused = 0U;
        TEST_CHECK(run, !WorldPrefabs_TryFindObject(S.System, Root, (const unsigned char*)u8"stop_a/no_such", &Unused));
        TEST_CHECK(run, !WorldPrefabs_TryFindObject(S.System, Root, (const unsigned char*)u8"", &Unused));
        TEST_CHECK(run, !WorldPrefabs_TryFindObject(S.System, Root, (const unsigned char*)u8"root/bulb", &Unused));
        TEST_CHECK(run, !WorldPrefabs_TryFindObject(S.System, WorldObject_GetId(Loose), (const unsigned char*)u8"root", &Unused));
    }
    Scene_Deconstruct(run, &S);
}


// Tests: with path objects.
/* A path object over a path of @p length metres along X, of an asset, at a place: its id. */
static uint64_t AddRow(TestRun* run, Scene* scene, const char* asset, double length, Vector3 at)
{
    WorldObject* Object = NULL;
    WorldComponent* PathPart = NULL;
    WorldComponent* RowPart = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(&scene->World, (const unsigned char*)u8"row", &Object)))
    {
        return 0U;
    }
    uint64_t Id = WorldObject_GetId(Object);
    TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, TestWorld_At(at)));
    if (TEST_CHECK_SUCCESS(run, World_AddComponent(&scene->World, Id, WorldComponentType_Path, &PathPart))
        && TEST_CHECK_SUCCESS(run, World_AddComponent(&scene->World, Id, WorldComponentType_PathObject, &RowPart)))
    {
        PathNode Nodes[2] = { PathNode_CreateDefault(), PathNode_CreateDefault() };
        Nodes[0].Mode = PathNodeMode_Corner;
        Nodes[1].Mode = PathNodeMode_Corner;
        Nodes[1].Position = Vector3Double_Create(length, 0.0, 0.0);
        TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(WorldPathComponent_FromComponent(PathPart), Nodes, 2U));
        TEST_CHECK_SUCCESS(run, WorldPathObjectComponent_SetPathObject(WorldPathObjectComponent_FromComponent(RowPart), (const unsigned char*)asset));
    }
    return Id;
}

static bool AddPathObject(TestRun* run, Scene* scene, const char* json)
{
    GenericBuffer Raw;
    GenericBuffer_AllocateVariable(&Raw, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Raw, (const unsigned char*)json);
    AssetDefinition* Definition = NULL;
    bool IsBuilt = TEST_CHECK_SUCCESS(run, AssetManager_BuildDefinition(scene->Fixture.Manager, scene->Fixture.Types.PathObject, &Raw,
        (const unsigned char*)u8"test path object", &Definition));
    Memory_Free(Raw._data);
    return IsBuilt && TEST_CHECK_SUCCESS(run, AssetManager_SetDefinition(scene->Fixture.Manager, Definition));
}

/* The prefab components under an object, in attach order, their roots' ids; how many. */
static size_t ListPieces(Scene* scene, uint64_t ownerId, uint64_t* outIds, size_t capacity)
{
    WorldObject* Owner = NULL;
    size_t Count = 0U;
    if (!World_TryGetObject(&scene->World, ownerId, &Owner))
    {
        return 0U;
    }
    for (WorldObject* Child = WorldObject_GetFirstChild(Owner); Child != NULL; Child = WorldObject_GetNextSibling(Child))
    {
        if (WorldObject_GetComponent(Child, WorldComponentType_Prefab) == NULL)
        {
            continue;
        }
        if (Count < capacity)
        {
            outIds[Count] = WorldObject_GetId(Child);
        }
        Count++;
    }
    return Count;
}

static const char* const LampRow =
    "{ \"name\": \"lamp_row\", \"models\": [ { \"slot\": \"middle\", \"prefab\": \"stop\", \"values\": { \"lamp_range\": 9 } } ], "
    "\"snaps_to_terrain\": false, \"fit\": \"fixed\", \"align\": \"between\", \"uses_gap\": true, \"gap\": 5.0, "
    "\"slots\": [ { \"slot\": \"middle\", \"length\": 0, \"side\": \"right\", \"side_offset\": 2.0 } ] }";

static void APathObjectsSlotHoldsAPrefab(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        WorldPathObjects* Rows = NULL;
        if (AddPathObject(run, &S, LampRow) && TEST_CHECK_SUCCESS(run, WorldPathObjects_Create(&S.World, NULL, S.Fixture.Manager, NULL, &Rows)))
        {
            uint64_t Row = AddRow(run, &S, "lamp_row", 20.0, (Vector3){ 3.0f, 0.0f, 0.0f });
            uint64_t NextId = 0U;
            WorldPathObjects_Update(Rows);
            NextId = World_GetNextObjectId(&S.World);
            WorldPrefabs_Update(S.System);
            uint64_t Pieces[8];
            TEST_CHECK(run, ListPieces(&S, Row, Pieces, 8U) == 5U);
            TEST_CHECK(run, World_GetNextObjectId(&S.World) == NextId); // no piece uses the counter up
            WorldObject* Piece = NULL;
            TEST_CHECK(run, World_TryGetObject(&S.World, Pieces[1], &Piece));
            TEST_CHECK(run, Pieces[1] >= WORLD_OBJECT_DERIVED_ID_FIRST);
            TEST_CHECK(run, !WorldObject_IsPersistent(Piece));
            Vector3 InScene = TestWorld_ToScene(WorldObject_GetWorldPosition(Piece));
            TEST_CHECK(run, NEAR(InScene.x, 8.0, 1e-3) && NEAR(InScene.z, 2.0, 1e-3));
            TEST_CHECK(run, NEAR(GetLampRange(&S, Pieces[1], "lamp"), 9.0, 1e-6)); // the record's value
            TEST_CHECK(run, WorldPrefabComponent_GetValue(GetCopy(&S, Pieces[1]), (const unsigned char*)u8"layout") == NULL); // it picks only when told to
            TEST_CHECK(run, GetTint(FindPart(&S, Pieces[3], "roof")).Tint.r == 0x7A); // a default of its own
            TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 35U);

            // A rebuild that changes nothing writes nothing, and the same ids stand.
            WorldPrefabsStats Before = WorldPrefabs_GetStats(S.System);
            WorldComponent* RowComponent = NULL;
            World_TryGetComponent(&S.World, Row, WorldComponentType_PathObject, &RowComponent);
            PropertyValue Seed = PropertyValue_FromBool(true);
            TEST_CHECK_SUCCESS(run, Properties_Set(WorldPathObjectComponent_GetPropertyTable(), RowComponent, (const unsigned char*)u8"overrides_seed", &Seed));
            WorldPathObjects_Update(Rows);
            WorldPrefabs_Update(S.System);
            uint64_t Again[8];
            TEST_CHECK(run, ListPieces(&S, Row, Again, 8U) == 5U);
            TEST_CHECK(run, Again[1] == Pieces[1]);
            TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ApplyCount == Before.ApplyCount);

            // A shorter path: the pieces past its end go, with their objects.
            WorldComponent* PathPart = NULL;
            World_TryGetComponent(&S.World, Row, WorldComponentType_Path, &PathPart);
            PathNode Nodes[2] = { PathNode_CreateDefault(), PathNode_CreateDefault() };
            Nodes[0].Mode = PathNodeMode_Corner;
            Nodes[1].Mode = PathNodeMode_Corner;
            Nodes[1].Position = Vector3Double_Create(10.0, 0.0, 0.0);
            TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(WorldPathComponent_FromComponent(PathPart), Nodes, 2U));
            WorldPathObjects_Update(Rows);
            WorldPrefabs_Update(S.System);
            TEST_CHECK(run, ListPieces(&S, Row, Again, 8U) == 3U);
            TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 21U);
            TEST_CHECK(run, World_TryGetObject(&S.World, Pieces[1], &Piece)); // the second is still at 5 m
            InScene = TestWorld_ToScene(WorldObject_GetWorldPosition(Piece));
            TEST_CHECK(run, NEAR(InScene.x, 8.0, 1e-3));
            WorldPathObjects_Destroy(Rows);
            WorldPrefabs_Update(S.System);
            TEST_CHECK(run, WorldPrefabs_GetStats(S.System).ObjectCount == 0U);
        }
    }
    Scene_Deconstruct(run, &S);
}

static void APiecesEnumsArePickedFromTheSeed(TestRun* run)
{
    size_t Plain[2] = { 0U, 0U };
    for (size_t Trial = 0U; Trial < 2U; Trial++)
    {
        Scene S;
        if (Scene_Construct(run, &S))
        {
            WorldPathObjects* Rows = NULL;
            AddPathObject(run, &S, "{ \"name\": \"random_row\", \"models\": [ { \"slot\": \"middle\", \"prefab\": \"stop\", \"randomizes_enums\": true } ], "
                "\"snaps_to_terrain\": false, \"seed\": 11, \"fit\": \"fixed\", \"align\": \"between\", \"uses_gap\": true, \"gap\": 1.0, "
                "\"slots\": [ { \"slot\": \"middle\", \"length\": 0 } ] }");
            if (TEST_CHECK_SUCCESS(run, WorldPathObjects_Create(&S.World, NULL, S.Fixture.Manager, NULL, &Rows)))
            {
                uint64_t Row = AddRow(run, &S, "random_row", 40.0, (Vector3){ 0.0f, 0.0f, 0.0f });
                WorldPathObjects_Update(Rows);
                WorldPrefabs_Update(S.System);
                uint64_t Pieces[64];
                size_t Count = ListPieces(&S, Row, Pieces, 64U);
                TEST_CHECK(run, Count == 41U);
                for (size_t Index = 0U; Index < Count; Index++)
                {
                    const DataValue* Layout = WorldPrefabComponent_GetValue(GetCopy(&S, Pieces[Index]), (const unsigned char*)u8"layout");
                    TEST_CHECK(run, (Layout != NULL) && (Layout->Type == DataType_String));
                    bool IsPlain = (Layout != NULL) && (strcmp((const char*)Layout->StringValue, "plain") == 0);
                    Plain[Trial] += IsPlain ? 1U : 0U;
                    // and its objects follow what it picked
                    TEST_CHECK(run, WorldObject_IsEnabled(FindPart(&S, Pieces[Index], "garage")) == !IsPlain);
                }
                WorldPathObjects_Destroy(Rows);
            }
        }
        Scene_Deconstruct(run, &S);
    }
    TEST_CHECK(run, Plain[0] == Plain[1]); // the same on every load
    TEST_CHECK(run, (Plain[0] > 5U) && (Plain[0] < 36U)); // and not all alike
}

/* Makes a row of pieces 5 m long on a path rising 4 m in 20. */
static uint64_t AddSlopedRow(TestRun* run, Scene* scene, const char* asset)
{
    uint64_t Row = AddRow(run, scene, asset, 20.0, (Vector3){ 0.0f, 0.0f, 0.0f });
    WorldComponent* PathPart = NULL;
    World_TryGetComponent(&scene->World, Row, WorldComponentType_Path, &PathPart);
    PathNode Nodes[2] = { PathNode_CreateDefault(), PathNode_CreateDefault() };
    Nodes[0].Mode = PathNodeMode_Corner;
    Nodes[1].Mode = PathNodeMode_Corner;
    Nodes[1].Position = Vector3Double_Create(20.0, 4.0, 0.0);
    Nodes[0].Width = 2.0f; // and wide: a piece that took it would stand uneven
    Nodes[1].Width = 2.0f;
    TEST_CHECK_SUCCESS(run, WorldPathComponent_SetNodes(WorldPathComponent_FromComponent(PathPart), Nodes, 2U));
    return Row;
}

#define SLOPED_ROW(name, settings) \
    "{ \"name\": \"" name "\", \"models\": [ { \"slot\": \"middle\", \"prefab\": \"stop\" } ], \"snaps_to_terrain\": false, \"fit\": \"fixed\", " \
    "\"align\": \"between\", \"uses_gap\": true, \"gap\": 1.0, \"slots\": [ { \"slot\": \"middle\", \"length\": 5, \"step_anchor\": \"higher\", " settings " } ] }"

/* An object has no shear and bends no mesh, so a prefab's piece stands level at an even scale whatever its slot says of slopes and curves. */
static void APiecesStandsLevelWhateverItsSlotSays(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        WorldPathObjects* Rows = NULL;
        AddPathObject(run, &S, SLOPED_ROW("stepped", "\"slope\": \"step\""));
        AddPathObject(run, &S, SLOPED_ROW("sheared", "\"slope\": \"shear\""));
        AddPathObject(run, &S, SLOPED_ROW("bent", "\"slope\": \"step\", \"curve\": \"bent\""));
        if (TEST_CHECK_SUCCESS(run, WorldPathObjects_Create(&S.World, NULL, S.Fixture.Manager, NULL, &Rows)))
        {
            uint64_t Stepped = AddSlopedRow(run, &S, "stepped");
            uint64_t Sheared = AddSlopedRow(run, &S, "sheared");
            uint64_t Bent = AddSlopedRow(run, &S, "bent");
            WorldPathObjects_Update(Rows);
            WorldPrefabs_Update(S.System);
            uint64_t Steps[8];
            uint64_t Shears[8];
            uint64_t Bends[8];
            size_t Count = ListPieces(&S, Stepped, Steps, 8U);
            TEST_CHECK(run, (Count >= 3U) && (Count <= 4U));
            TEST_CHECK(run, ListPieces(&S, Sheared, Shears, 8U) == Count);
            TEST_CHECK(run, ListPieces(&S, Bent, Bends, 8U) == Count);
            for (size_t Index = 0U; (Index < Count) && (Index < 8U); Index++)
            {
                WorldObject* Step = NULL;
                WorldObject* Other[2] = { NULL, NULL };
                TEST_CHECK(run, World_TryGetObject(&S.World, Steps[Index], &Step) && World_TryGetObject(&S.World, Shears[Index], &Other[0])
                    && World_TryGetObject(&S.World, Bends[Index], &Other[1]));
                Vector3 StepScale = WorldObject_GetScale(Step);
                TEST_CHECK(run, NEAR(StepScale.x, 1.0, 1e-5) && NEAR(StepScale.y, 1.0, 1e-5) && NEAR(StepScale.z, 1.0, 1e-5)); // even, however it stretches
                Vector3 StepTurn = WorldObject_GetRotation(Step);
                TEST_CHECK(run, NEAR(StepTurn.x, 0.0, 1e-5) && NEAR(StepTurn.z, 0.0, 1e-5)); // and level
                for (size_t Kind = 0U; Kind < 2U; Kind++)
                {
                    Vector3Double Place = WorldObject_GetPosition(Step);
                    Vector3Double OtherPlace = WorldObject_GetPosition(Other[Kind]);
                    TEST_CHECK(run, NEAR(Place.X, OtherPlace.X, 1e-9) && NEAR(Place.Y, OtherPlace.Y, 1e-9) && NEAR(Place.Z, OtherPlace.Z, 1e-9));
                    Vector3 OtherTurn = WorldObject_GetRotation(Other[Kind]);
                    TEST_CHECK(run, NEAR(StepTurn.x, OtherTurn.x, 1e-6) && NEAR(StepTurn.y, OtherTurn.y, 1e-6) && NEAR(StepTurn.z, OtherTurn.z, 1e-6));
                    Vector3 OtherScale = WorldObject_GetScale(Other[Kind]);
                    TEST_CHECK(run, NEAR(StepScale.x, OtherScale.x, 1e-6) && NEAR(StepScale.y, OtherScale.y, 1e-6) && NEAR(StepScale.z, OtherScale.z, 1e-6));
                }
            }
            WorldPathObjects_Destroy(Rows);
        }
    }
    Scene_Deconstruct(run, &S);
}

/* Two enums of one piece choose apart: every pair of options comes up, with enums of the same size too. */
static void TwoEnumsOfAPiecePickApart(TestRun* run)
{
    Scene S;
    if (Scene_ConstructBare(run, &S))
    {
        WorldPathObjects* Rows = NULL;
        AddPrefab(run, &S, "{ \"name\": \"twin\", \"format_version\": 3, \"properties\": { "
            "\"first\": { \"type\": \"enum\", \"default\": \"a\", \"options\": { \"a\": [], \"b\": [] } }, "
            "\"second\": { \"type\": \"enum\", \"default\": \"a\", \"options\": { \"a\": [], \"b\": [] } } }, "
            "\"objects\": [ { \"id\": 1, \"name\": \"root\" } ] }");
        AddPathObject(run, &S, "{ \"name\": \"twins\", \"models\": [ { \"slot\": \"middle\", \"prefab\": \"twin\", \"randomizes_enums\": true } ], "
            "\"snaps_to_terrain\": false, \"seed\": 11, \"fit\": \"fixed\", \"align\": \"between\", \"uses_gap\": true, \"gap\": 1.0, "
            "\"slots\": [ { \"slot\": \"middle\", \"length\": 0 } ] }");
        if (TEST_CHECK_SUCCESS(run, WorldPathObjects_Create(&S.World, NULL, S.Fixture.Manager, NULL, &Rows)))
        {
            uint64_t Row = AddRow(run, &S, "twins", 40.0, (Vector3){ 0.0f, 0.0f, 0.0f });
            WorldPathObjects_Update(Rows);
            WorldPrefabs_Update(S.System);
            uint64_t Pieces[64];
            size_t Count = ListPieces(&S, Row, Pieces, 64U);
            TEST_CHECK(run, Count == 41U);
            size_t Pairs[4] = { 0U, 0U, 0U, 0U };
            for (size_t Index = 0U; (Index < Count) && (Index < 64U); Index++)
            {
                const DataValue* First = WorldPrefabComponent_GetValue(GetCopy(&S, Pieces[Index]), (const unsigned char*)u8"first");
                const DataValue* Second = WorldPrefabComponent_GetValue(GetCopy(&S, Pieces[Index]), (const unsigned char*)u8"second");
                TEST_CHECK(run, (First != NULL) && (Second != NULL));
                if ((First != NULL) && (Second != NULL))
                {
                    Pairs[((First->StringValue[0] == 'b') ? 2U : 0U) + ((Second->StringValue[0] == 'b') ? 1U : 0U)]++;
                }
            }
            TEST_CHECK(run, (Pairs[0] > 0U) && (Pairs[1] > 0U) && (Pairs[2] > 0U) && (Pairs[3] > 0U));
            WorldPathObjects_Destroy(Rows);
        }
    }
    Scene_Deconstruct(run, &S);
}

static void AModelRecordNamesOneOfAWorldModelAndAPrefab(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        GenericBuffer Raw;
        GenericBuffer_AllocateVariable(&Raw, 0U, sizeof(unsigned char));
        GenericBuffer_AppendString(&Raw, (const unsigned char*)u8"{ \"name\": \"both\", \"models\": [ { \"slot\": \"middle\", \"world_model\": \"x\", \"prefab\": \"stop\" } ] }");
        AssetDefinition* Definition = NULL;
        TEST_CHECK_ERROR(run, AssetManager_BuildDefinition(S.Fixture.Manager, S.Fixture.Types.PathObject, &Raw, (const unsigned char*)u8"t", &Definition),
            ErrorCode_InvalidAssetDefinition);
        GenericBuffer_Clear(&Raw);
        GenericBuffer_AppendString(&Raw, (const unsigned char*)u8"{ \"name\": \"neither\", \"models\": [ { \"slot\": \"middle\" } ] }");
        TEST_CHECK_ERROR(run, AssetManager_BuildDefinition(S.Fixture.Manager, S.Fixture.Types.PathObject, &Raw, (const unsigned char*)u8"t", &Definition),
            ErrorCode_InvalidAssetDefinition);
        Memory_Free(Raw._data);
    }
    Scene_Deconstruct(run, &S);
}

/* Path objects a prefab can multiply: a warning each, up to the same kind of end; what a file names reaches the log as text only. */
static void PathObjectsKeepTheLogShort(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        WorldPathObjects* Rows = NULL;
        if (TEST_CHECK_SUCCESS(run, WorldPathObjects_Create(&S.World, NULL, S.Fixture.Manager, &S.Log, &Rows)))
        {
            uint64_t First = AddRow(run, &S, "no_such_row", 5.0, (Vector3){ 0.0f, 0.0f, 0.0f });
            for (size_t Index = 0U; Index < (WORLD_PATH_OBJECTS_MAX_WARNINGS + 5U); Index++)
            {
                AddRow(run, &S, "no_such_row", 5.0, (Vector3){ 0.0f, 0.0f, 0.0f });
            }
            WorldPathObjects_Update(Rows);
            TEST_CHECK(run, CountInLog(run, &S, "generates nothing") == WORLD_PATH_OBJECTS_MAX_WARNINGS);
            TEST_CHECK(run, CountInLog(run, &S, "stays quiet from here on") == 1U);
            // What a file names reaches the log as text only: a rebuild may say it all again, and it says the object's name.
            WorldObject* Odd = NULL;
            TEST_CHECK(run, World_TryGetObject(&S.World, First, &Odd));
            TEST_CHECK_SUCCESS(run, WorldObject_SetName(Odd, (const unsigned char*)"\x1b[31mred\nline"));
            WorldPathObjects_Rebuild(Rows);
            WorldPathObjects_Update(Rows);
            TEST_CHECK(run, CountInLog(run, &S, "red?line") == 1U); // the first warned about, its name as text
            TEST_CHECK(run, CountInLog(run, &S, "\x1b[31m") == 0U);
            TEST_CHECK(run, CountInLog(run, &S, "stays quiet from here on") == 2U);
            WorldPathObjects_Destroy(Rows);
        }
    }
    Scene_Deconstruct(run, &S);
}

/* A prefab holding a fence with its own path: a property drives its count, and a missing prefab in a slot leaves the rest standing. */
static void APrefabHoldsAPathObject(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        BoundingBox Bounds = { { 0.0f, 0.0f, -0.1f }, { 2.0f, 1.0f, 0.1f } };
        WorldPathObjects* Rows = NULL;
        bool IsReady = TestAssets_AddModel(run, &S.Fixture, "panel", Bounds, NULL)
            && TestAssets_AddWorldModelOf(run, &S.Fixture, "panel", "panel")
            && AddPathObject(run, &S, "{ \"name\": \"fence\", \"snaps_to_terrain\": false, \"models\": [ { \"slot\": \"middle\", \"world_model\": \"panel\" } ], "
                "\"fit\": \"fixed\", \"align\": \"start\", \"uses_count\": true, \"count\": 1 }")
            && AddPrefab(run, &S, "{ \"name\": \"yard\", \"format_version\": 3, \"properties\": {"
                "  \"panels\": { \"type\": \"int\", \"default\": 2, \"minimum\": 1, \"maximum\": 5, \"targets\": ["
                "    { \"object\": \"fence\", \"component\": \"path_object\", \"row\": \"count\" } ] } },"
                "  \"objects\": [ { \"id\": 1, \"name\": \"root\", \"components\": { \"path\": { \"nodes\": ["
                "      { \"position\": [0, 0, 0], \"mode\": \"corner\" }, { \"position\": [10, 0, 0], \"mode\": \"corner\" } ] } } },"
                "    { \"id\": 2, \"parent\": 1, \"name\": \"fence\", \"components\": { \"path_object\": { \"path_object\": \"fence\", "
                "      \"overrides_count\": true, \"count\": 1 } } } ] }")
            && TEST_CHECK_SUCCESS(run, WorldPathObjects_Create(&S.World, NULL, S.Fixture.Manager, NULL, &Rows));
        if (IsReady)
        {
            uint64_t Root = AddCopy(run, &S, "a", "yard", (Vector3){ 0.0f, 0.0f, 0.0f });
            WorldPrefabs_Update(S.System);
            WorldPathObjects_Update(Rows);
            TEST_CHECK(run, WorldPathObjects_GetStats(Rows).PieceCount == 2U);
            SetNumberValue(run, &S, Root, "panels", 4.0);
            WorldPrefabs_Update(S.System);
            WorldPathObjects_Update(Rows);
            TEST_CHECK(run, WorldPathObjects_GetStats(Rows).PieceCount == 2U); // a float is no int: it keeps its default
            DataValue Four = DataValue_FromInt(4);
            TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetValue(GetCopy(&S, Root), (const unsigned char*)u8"panels", &Four));
            WorldPrefabs_Update(S.System);
            WorldPathObjects_Update(Rows);
            TEST_CHECK(run, WorldPathObjects_GetStats(Rows).PieceCount == 4U);
        }
        WorldPathObjects_Destroy(Rows);
    }
    Scene_Deconstruct(run, &S);
}


/* A prefab a path object's piece names reloads like any other: the path objects system keeps no hold on it. */
static void ARebuildReachesAPathObjectsPieces(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        WorldPathObjects* Rows = NULL;
        if (AddPathObject(run, &S, LampRow) && TEST_CHECK_SUCCESS(run, WorldPathObjects_Create(&S.World, NULL, S.Fixture.Manager, NULL, &Rows)))
        {
            uint64_t Row = AddRow(run, &S, "lamp_row", 5.0, (Vector3){ 0.0f, 0.0f, 0.0f });
            WorldPathObjects_Update(Rows);
            WorldPrefabs_Update(S.System);
            uint64_t Pieces[2];
            TEST_CHECK(run, ListPieces(&S, Row, Pieces, 2U) == 2U);
            TEST_CHECK(run, FindPart(&S, Pieces[0], "sign") == NULL);
            AddPrefab(run, &S, "{ \"name\": \"stop\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"root\" }, "
                "{ \"id\": 2, \"parent\": 1, \"name\": \"sign\" } ] }");
            WorldPrefabs_Rebuild(S.System);
            TEST_CHECK(run, FindPart(&S, Pieces[0], "sign") != NULL);
            // A piece picks its enums when its path object builds: both rebuilds, and the picks are of the prefab as it is now.
            AddPrefab(run, &S, "{ \"name\": \"pick\", \"format_version\": 3, \"properties\": { \"how\": { \"type\": \"enum\", \"default\": \"a\", \"options\": { \"a\": [], \"b\": [] } } }, "
                "\"objects\": [ { \"id\": 1, \"name\": \"root\" } ] }");
            AddPathObject(run, &S, "{ \"name\": \"picks\", \"models\": [ { \"slot\": \"middle\", \"prefab\": \"pick\", \"randomizes_enums\": true } ], "
                "\"snaps_to_terrain\": false, \"fit\": \"fixed\", \"align\": \"between\", \"uses_gap\": true, \"gap\": 5.0, \"slots\": [ { \"slot\": \"middle\", \"length\": 0 } ] }");
            uint64_t Picks = AddRow(run, &S, "picks", 20.0, (Vector3){ 0.0f, 0.0f, 9.0f });
            WorldPathObjects_Update(Rows);
            WorldPrefabs_Update(S.System);
            uint64_t PickPieces[8];
            size_t PickCount = ListPieces(&S, Picks, PickPieces, 8U);
            TEST_CHECK(run, (PickCount == 5U) && (WorldPrefabComponent_GetValue(GetCopy(&S, PickPieces[0]), (const unsigned char*)u8"how") != NULL));
            AddPrefab(run, &S, "{ \"name\": \"pick\", \"format_version\": 3, \"properties\": { \"how\": { \"type\": \"enum\", \"default\": \"c\", \"options\": { \"c\": [], \"d\": [] } }, "
                "\"more\": { \"type\": \"enum\", \"default\": \"e\", \"options\": { \"e\": [], \"f\": [] } } }, "
                "\"objects\": [ { \"id\": 1, \"name\": \"root\" } ] }");
            WorldPathObjects_Rebuild(Rows);
            WorldPrefabs_Rebuild(S.System);
            WorldPathObjects_Update(Rows);
            WorldPrefabs_Update(S.System);
            PickCount = ListPieces(&S, Picks, PickPieces, 8U);
            for (size_t Index = 0U; (Index < PickCount) && (Index < 8U); Index++)
            {
                const DataValue* How = WorldPrefabComponent_GetValue(GetCopy(&S, PickPieces[Index]), (const unsigned char*)u8"how");
                const DataValue* More = WorldPrefabComponent_GetValue(GetCopy(&S, PickPieces[Index]), (const unsigned char*)u8"more");
                TEST_CHECK(run, (How != NULL) && (More != NULL));
                TEST_CHECK(run, (How != NULL) && ((How->StringValue[0] == 'c') || (How->StringValue[0] == 'd')));
                TEST_CHECK(run, (More != NULL) && ((More->StringValue[0] == 'e') || (More->StringValue[0] == 'f')));
            }
            WorldPathObjects_Destroy(Rows);
        }
    }
    Scene_Deconstruct(run, &S);
}

/* An asset property chooses a nested prefab's name, which no file can check for a cycle. */
static void APropertyChoosesANestedPrefab(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        AddPrefab(run, &S, "{ \"name\": \"chooser\", \"format_version\": 3, \"properties\": { \"what\": { \"type\": \"asset\", \"default\": \"stop\", "
            "\"targets\": [ { \"object\": \"held\", \"component\": \"prefab\", \"row\": \"prefab\" } ] } }, "
            "\"objects\": [ { \"id\": 1, \"name\": \"root\" }, { \"id\": 2, \"parent\": 1, \"name\": \"held\", \"components\": { \"prefab\": { } } } ] }");
        uint64_t Root = AddCopy(run, &S, "a", "chooser", (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, FindPart(&S, Root, "held/lamp") != NULL);
        SetStringValue(run, &S, Root, "what", "shelter");
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, FindPart(&S, Root, "held/lamp") == NULL);
        TEST_CHECK(run, FindPart(&S, Root, "held/stop_a/lamp") != NULL);
        SetStringValue(run, &S, Root, "what", "chooser");
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, FindPart(&S, Root, "held/root") == NULL);
        TEST_CHECK(run, CountInLog(run, &S, "would hold itself") == 1U);
        // and back again
        SetStringValue(run, &S, Root, "what", "stop");
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, FindPart(&S, Root, "held/lamp") != NULL);
    }
    Scene_Deconstruct(run, &S);
}

/* What a hostile file names cannot break a log line or move a terminal's cursor. */
static void AHostileNameCannotBreakALogLine(TestRun* run)
{
    Scene S;
    if (Scene_Construct(run, &S))
    {
        uint64_t Root = AddCopy(run, &S, "a\nb", "stop", (Vector3){ 0.0f, 0.0f, 0.0f });
        SetVectorRow(run, &S, Root, "\x1b[31mred\nline", "position", 1.0, 2.0, 3.0);
        WorldPrefabs_Update(S.System);
        TEST_CHECK(run, CountInLog(run, &S, "\x1b[31m") == 0U);
        TEST_CHECK(run, CountInLog(run, &S, "\nline") == 0U);
        TEST_CHECK(run, CountInLog(run, &S, "red") == 1U);
    }
    Scene_Deconstruct(run, &S);
}


void WorldPrefabTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "a_copy_makes_its_objects_under_its_root", ACopyMakesItsObjectsUnderItsRoot },
        { "its_ids_come_from_the_root_and_the_name", ItsIdsComeFromTheRootAndTheName },
        { "a_taken_id_takes_the_next_salt", ATakenIdTakesTheNextSalt },
        { "values_reach_every_target_and_defaults_apply", ValuesReachEveryTargetAndDefaultsApply },
        { "an_axis_writes_one_axis_in_degrees", AnAxisWritesOneAxisInDegrees },
        { "an_enums_next_option_undoes_the_last", AnEnumsNextOptionUndoesTheLast },
        { "a_bad_value_takes_the_default_and_warns_once", ABadValueTakesTheDefaultAndWarnsOnce },
        { "a_quiet_update_writes_nothing", AQuietUpdateWritesNothing },
        { "what_it_does_not_write_stays_as_changed", WhatItDoesNotWriteStaysAsChanged },
        { "an_override_changes_a_row_of_an_object", AnOverrideChangesARowOfAnObject },
        { "an_override_beats_a_property_and_a_component_row_works", AnOverrideBeatsAPropertyAndAComponentRowWorks },
        { "an_override_removes_an_object_and_brings_it_back", AnOverrideRemovesAnObjectAndBringsItBack },
        { "a_stale_name_warns_once_and_is_ignored", AStaleNameWarnsOnceAndIsIgnored },
        { "a_value_a_row_refuses_warns_and_leaves_its_row", AValueARowRefusesWarnsAndLeavesItsRow },
        { "what_a_copy_adds_is_an_ordinary_saved_child", WhatACopyAddsIsAnOrdinarySavedChild },
        { "an_object_removed_from_outside_comes_back_at_the_next_change", AnObjectRemovedFromOutsideComesBackAtTheNextChange },
        { "a_nested_copy_makes_its_objects_within_one_update", ANestedCopyMakesItsObjectsWithinOneUpdate },
        { "a_property_passes_through_to_a_nested_copy", APropertyPassesThroughToANestedCopy },
        { "an_overrides_path_reaches_through_a_nested_copy", AnOverridesPathReachesThroughANestedCopy },
        { "a_prefab_that_would_hold_itself_makes_nothing", APrefabThatWouldHoldItselfMakesNothing },
        { "copies_nest_so_deep", CopiesNestSoDeep },
        { "a_missing_prefab_makes_nothing_and_waits_for_a_new_name", AMissingPrefabMakesNothingAndWaitsForANewName },
        { "the_worlds_objects_have_a_budget", TheWorldsObjectsHaveABudget },
        { "what_one_update_cannot_do_waits_for_the_next", WhatOneUpdateCannotDoWaitsForTheNext },
        { "what_is_left_out_is_made_when_an_update_finds_room", WhatIsLeftOutIsMadeWhenAnUpdateFindsRoom },
        { "a_copy_past_the_budgets_costs_nothing", ACopyPastTheBudgetsCostsNothing },
        { "what_copies_hold_has_a_budget_in_bytes", WhatCopiesHoldHasABudgetInBytes },
        { "a_nested_row_takes_many_writes", ANestedRowTakesManyWrites },
        { "no_copy_starves", NoCopyStarves },
        { "an_id_its_object_left_is_not_the_copys", AnIdItsObjectLeftIsNotTheCopys },
        { "object_ids_in_data_name_the_copys_objects", ObjectIdsInDataNameTheCopysObjects },
        { "a_false_through_a_nested_copy_brings_back_what_it_removes", AFalseThroughANestedCopyBringsBackWhatItRemoves },
        { "a_bad_components_section_is_warned_about", ABadComponentsSectionIsWarnedAbout },
        { "a_nesting_asking_for_too_much_gets_the_budget", ANestingAskingForTooMuchGetsTheBudget },
        { "path_objects_keep_the_log_short", PathObjectsKeepTheLogShort },
        { "copies_come_and_go_in_one_update", CopiesComeAndGoInOneUpdate },
        { "a_row_once_restored_is_left_alone", ARowOnceRestoredIsLeftAlone },
        { "a_piece_stands_level_whatever_its_slot_says", APiecesStandsLevelWhateverItsSlotSays },
        { "two_enums_of_a_piece_pick_apart", TwoEnumsOfAPiecePickApart },
        { "writes_wait_for_the_next_update_too", WritesWaitForTheNextUpdateToo },
        { "an_axis_writes_every_kind_of_vector", AnAxisWritesEveryKindOfVector },
        { "the_components_setters_keep_its_rows", TheComponentsSettersKeepItsRows },
        { "the_component_going_takes_its_objects", TheComponentGoingTakesItsObjects },
        { "the_system_going_takes_what_it_made", TheSystemGoingTakesWhatItMade },
        { "rebuild_makes_copies_from_the_definition_as_it_is_now", RebuildMakesCopiesFromTheDefinitionAsItIsNow },
        { "tags_and_data_reach_the_worlds_own_registry", TagsAndDataReachTheWorldsOwnRegistry },
        { "only_the_root_goes_into_world_files", OnlyTheRootGoesIntoWorldFiles },
        { "a_world_file_reads_and_writes_a_copys_block_as_it_is", AWorldFileReadsAndWritesACopysBlockAsItIs },
        { "a_click_walks_up_to_a_copys_root", AClickWalksUpToACopysRoot },
        { "a_path_objects_slot_holds_a_prefab", APathObjectsSlotHoldsAPrefab },
        { "a_pieces_enums_are_picked_from_the_seed", APiecesEnumsArePickedFromTheSeed },
        { "a_model_record_names_one_of_a_world_model_and_a_prefab", AModelRecordNamesOneOfAWorldModelAndAPrefab },
        { "a_prefab_holds_a_path_object", APrefabHoldsAPathObject },
        { "a_rebuild_reaches_a_path_objects_pieces", ARebuildReachesAPathObjectsPieces },
        { "a_property_chooses_a_nested_prefab", APropertyChoosesANestedPrefab },
        { "a_hostile_name_cannot_break_a_log_line", AHostileNameCannotBreakALogLine },
    };
    TestWorld_RunSuite(run, "WorldPrefab", "WorldPrefabFar", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
