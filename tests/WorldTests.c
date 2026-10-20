#include "TestSuites.h"
#include "TestAssets.h"
#include "TestText.h"
#include "TestWorld.h"
#include "World.h"
#include "WorldObject.h"
#include "WorldComponent.h"
#include "WorldModelComponent.h"
#include "WorldSpriteComponent.h"
#include "WorldLightComponent.h"
#include "WorldInternal.h"
#include "SpriteAnimation.h"
#include "Logger.h"
#include "wr/WRFileSystem.h"
#include "wr/WRPath.h"
#include <math.h>
#include <string.h>


// Macros.
/** Initial capacity of path and text buffers; they grow as needed. */
#define WORLD_TEST_TEXT_CAPACITY ((size_t)256)
/** The most components one consistency check compares. */
#define WORLD_TEST_MAX_EXPECTED ((size_t)16)


// Types.
/* A test-only component type: a transform driver with one property, so the one-driver rule can be tested before the
 * game has a real driver (the character controller). */
typedef struct TestDriverComponentStruct
{
    WorldComponent Base;
    float Speed;
} TestDriverComponent;


// Fields.
static const PropertyDescriptor TestDriverProperties[] =
{
    { .Key = u8"speed", .WireId = 1U, .Type = PropertyType_Float, PROPERTY_MEMBER(TestDriverComponent, Speed),
        .Default.FloatValue = 1.0f, .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING,
        .SinceVersion = 1U },
};

static const PropertyTable TestDriverTable =
{
    .Name = u8"test_driver", .Version = 1U, .StructSize = sizeof(TestDriverComponent),
    .Descriptors = TestDriverProperties,
    .DescriptorCount = sizeof(TestDriverProperties) / sizeof(TestDriverProperties[0]),
    .RetiredWireIds = NULL, .RetiredWireIdCount = 0U, .AfterSet = WorldInternal_ComponentAfterSet, .Migrate = NULL
};

/* The same table without the world's hook: an entry using it must be refused. */
static const PropertyTable TestUnhookedTable =
{
    .Name = u8"test_unhooked", .Version = 1U, .StructSize = sizeof(TestDriverComponent),
    .Descriptors = TestDriverProperties,
    .DescriptorCount = sizeof(TestDriverProperties) / sizeof(TestDriverProperties[0]),
    .RetiredWireIds = NULL, .RetiredWireIdCount = 0U, .AfterSet = NULL, .Migrate = NULL
};


// Static functions: the test component type.
static Error ConstructTestDriver(WorldComponent* component)
{
    return Properties_Construct(&TestDriverTable, component);
}

static void DeconstructTestDriver(WorldComponent* component)
{
    Properties_Deconstruct(&TestDriverTable, component);
}


// Fields: the test registry entries (they refer to the functions above).
static const WorldComponentTypeInfo TestDriverA =
{
    .Type = (WorldComponentType)WorldComponentType_Count, .Name = u8"test_driver_a",
    .Size = sizeof(TestDriverComponent), .Table = &TestDriverTable, .IsTransformDriver = true,
    .Construct = ConstructTestDriver, .Deconstruct = DeconstructTestDriver
};

static const WorldComponentTypeInfo TestDriverB =
{
    .Type = (WorldComponentType)(WorldComponentType_Count + 1), .Name = u8"test_driver_b",
    .Size = sizeof(TestDriverComponent), .Table = &TestDriverTable, .IsTransformDriver = true,
    .Construct = ConstructTestDriver, .Deconstruct = DeconstructTestDriver
};

static const WorldComponentTypeInfo TestUnhooked =
{
    .Type = (WorldComponentType)WorldComponentType_Count, .Name = u8"test_unhooked",
    .Size = sizeof(TestDriverComponent), .Table = &TestUnhookedTable, .IsTransformDriver = false,
    .Construct = ConstructTestDriver, .Deconstruct = DeconstructTestDriver
};


// Static functions: helpers.
/* Creates an object; returns it (world-owned), or NULL with a failed check. */
static WorldObject* CreateObject(TestRun* run, World* world, const char* name)
{
    WorldObject* Object = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(world, (const unsigned char*)name, &Object)))
    {
        return NULL;
    }
    return Object;
}

/* Adds a component of @p type to @p object; returns it (world-owned), or NULL with a failed check. */
static WorldComponent* AddComponent(TestRun* run, World* world, const WorldObject* object, WorldComponentType type)
{
    WorldComponent* Component = NULL;
    if ((object == NULL)
        || !TEST_CHECK_SUCCESS(run, World_AddComponent(world, WorldObject_GetId(object), type, &Component)))
    {
        return NULL;
    }
    return Component;
}

/* Creates an object with a sprite component naming the world sprite @p worldSpriteName; returns the sprite, or NULL with
 * a failed check. */
static WorldSpriteComponent* AddSprite(TestRun* run, World* world, const char* worldSpriteName)
{
    WorldSpriteComponent* Sprite = WorldSpriteComponent_FromComponent(AddComponent(run, world,
        CreateObject(run, world, "sprite"), WorldComponentType_Sprite));
    if ((Sprite == NULL)
        || !TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetWorldSprite(Sprite, (const unsigned char*)worldSpriteName)))
    {
        return NULL;
    }
    return Sprite;
}

static bool IsBound(WorldSpriteComponent* sprite)
{
    SpriteAnimationInstance* Instance = WorldSpriteComponent_GetAnimationInstance(sprite);
    return SpriteAnimationInstance_HasSource(Instance) && (SpriteAnimationInstance_GetFrameCount(Instance) > 0);
}

/* Checks that the world's components of @p type belong to the objects @p expectedIds, in that order. */
static void CheckComponentOwners(TestRun* run, const World* world, WorldComponentType type, const uint64_t* expectedIds,
    size_t expectedCount)
{
    size_t Count = 0;
    WorldComponent* const* Components = World_GetComponents(world, type, &Count);
    if (!TEST_CHECK(run, Count == expectedCount))
    {
        return;
    }
    for (size_t Index = 0; Index < Count; Index++)
    {
        const WorldObject* Owner = WorldComponent_GetOwner(Components[Index]);
        TEST_CHECK(run, WorldComponent_GetType(Components[Index]) == type);
        TEST_CHECK(run, WorldObject_GetId(Owner) == expectedIds[Index]);
        TEST_CHECK(run, WorldObject_GetComponent(Owner, type) == Components[Index]);
    }
}

/* Counts the occurrences of @p needle in "<directory>/logs/latest.log". */
static size_t CountInLatestLog(TestRun* run, const unsigned char* directory, const char* needle)
{
    size_t Count = 0;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, WORLD_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, WORLD_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, Path_Append(directory, (const unsigned char*)u8"logs/latest.log", &Path))
        && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, &Text))
        && TEST_CHECK(run, GenericBuffer_NullTerminate(&Text)))
    {
        for (const char* Found = strstr((const char*)Text._data, needle); Found != NULL;
            Found = strstr(Found + 1, needle))
        {
            Count++;
        }
    }
    Memory_Free(Text._data);
    Memory_Free(Path._data);
    return Count;
}

/* Reads @p text (a JSON object) into an attached component through its table. */
static Error ReadComponentJSON(TestRun* run, WorldComponent* component, const PropertyTable* table, const char* text)
{
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return Error_Construct2(ErrorCode_InvalidState, "the test's JSON pool could not be created");
    }
    Error Result = Error_Construct2(ErrorCode_InvalidJSON, "the test's JSON did not parse");
    JSONObjectValue Value;
    if (TestText_ParseJSON(run, Pool, text, &Value))
    {
        Error_Deconstruct(&Result);
        Result = Properties_ReadJSON(table, Value.Value.Compound, component, NULL);
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
    }
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    return Result;
}

/* Constructs a key registry and a world holding the standard types plus the two test drivers (release with
 * TestWorld_Deconstruct). */
static bool ConstructDriverWorld(TestRun* run, World* world, KeyRegistry* keys)
{
    const WorldComponentTypeInfo* Types[WorldComponentType_Count + 2] =
    {
        WorldComponent_GetStandardTypeInfo(WorldComponentType_Model),
        WorldComponent_GetStandardTypeInfo(WorldComponentType_Sprite),
        WorldComponent_GetStandardTypeInfo(WorldComponentType_Light),
        &TestDriverA,
        &TestDriverB,
    };
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(keys)))
    {
        return false;
    }
    if (!TEST_CHECK_SUCCESS(run, WorldInternal_ConstructWithTypes(world, keys, NULL, NULL, Types,
        sizeof(Types) / sizeof(Types[0]))))
    {
        KeyRegistry_Deconstruct(keys);
        return false;
    }
    return true;
}


// Static functions: objects.
static void ObjectsGetUniqueIdsThatAreNeverZero(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }

    WorldObject* First = CreateObject(run, &TestWorld, "first");
    WorldObject* Second = CreateObject(run, &TestWorld, "second");
    WorldObject* Third = CreateObject(run, &TestWorld, NULL);
    if ((First != NULL) && (Second != NULL) && (Third != NULL))
    {
        TEST_CHECK(run, (WorldObject_GetId(First) == 1U) && (WorldObject_GetId(Second) == 2U)
            && (WorldObject_GetId(Third) == 3U));
        TEST_CHECK(run, strcmp((const char*)WorldObject_GetName(Second), "second") == 0);
        TEST_CHECK(run, WorldObject_GetName(Third) == NULL);
        TEST_CHECK(run, World_GetObjectCount(&TestWorld) == 3U);

        WorldObject* Found = NULL;
        TEST_CHECK(run, World_TryGetObject(&TestWorld, 2U, &Found) && (Found == Second));
        TEST_CHECK(run, !World_TryGetObject(&TestWorld, 0U, &Found) && (Found == NULL));
        TEST_CHECK(run, !World_TryGetObject(&TestWorld, 99U, &Found) && (Found == NULL));

        // Removing keeps the others in creation order; ids are never handed out twice.
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&TestWorld, 2U));
        TEST_CHECK(run, !World_TryGetObject(&TestWorld, 2U, &Found));
        WorldObject* AtOne = NULL;
        TEST_CHECK(run, (World_GetObjectCount(&TestWorld) == 2U)
            && TEST_CHECK_SUCCESS(run, World_GetObjectByIndex(&TestWorld, 1U, &AtOne)) && (AtOne == Third));
        TEST_CHECK_ERROR(run, World_GetObjectByIndex(&TestWorld, 2U, &AtOne), ErrorCode_IndexOutOfBounds);
        WorldObject* Fourth = CreateObject(run, &TestWorld, "fourth");
        TEST_CHECK(run, (Fourth != NULL) && (WorldObject_GetId(Fourth) == 4U));
    }
    TEST_CHECK_ERROR(run, World_RemoveObject(&TestWorld, 2U), ErrorCode_InvalidOperation);
    TEST_CHECK_ERROR(run, World_RemoveObject(&TestWorld, 0U), ErrorCode_IllegalArgument);
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

static void LoaderIdsAdvanceTheCounter(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }

    WorldObject* Object = NULL;
    TEST_CHECK_SUCCESS(run, World_CreateObjectWithId(&TestWorld, (const unsigned char*)u8"loaded", 10U, &Object));
    TEST_CHECK(run, (Object != NULL) && (WorldObject_GetId(Object) == 10U)
        && (World_GetNextObjectId(&TestWorld) == 11U));
    TEST_CHECK_SUCCESS(run, World_CreateObjectWithId(&TestWorld, NULL, 5U, &Object)); // below the counter: kept
    TEST_CHECK(run, World_GetNextObjectId(&TestWorld) == 11U);
    TEST_CHECK_ERROR(run, World_CreateObjectWithId(&TestWorld, NULL, 10U, &Object), ErrorCode_InvalidOperation);
    TEST_CHECK(run, Object == NULL);
    TEST_CHECK_ERROR(run, World_CreateObjectWithId(&TestWorld, NULL, 0U, &Object), ErrorCode_IllegalArgument);
    Object = CreateObject(run, &TestWorld, NULL);
    TEST_CHECK(run, (Object != NULL) && (WorldObject_GetId(Object) == 11U));

    World_EnsureNextObjectIdAtLeast(&TestWorld, 100U);
    World_EnsureNextObjectIdAtLeast(&TestWorld, 50U); // never lowers it
    Object = CreateObject(run, &TestWorld, NULL);
    TEST_CHECK(run, (Object != NULL) && (WorldObject_GetId(Object) == 100U));

    // UINT64_MAX is never an id (no world file could hold a next id past it); an object with the last id uses the
    // counter up, so no id can be handed out after it.
    TEST_CHECK_ERROR(run, World_CreateObjectWithId(&TestWorld, NULL, UINT64_MAX, &Object), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_SUCCESS(run, World_CreateObjectWithId(&TestWorld, NULL, WORLD_OBJECT_ID_MAX, &Object));
    TEST_CHECK(run, World_GetNextObjectId(&TestWorld) == 0U);
    TEST_CHECK_ERROR(run, World_CreateObject(&TestWorld, NULL, &Object), ErrorCode_InvalidOperation);
    TEST_CHECK(run, World_GetObjectCount(&TestWorld) == 5U);
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);

    // The counter hands out the last id itself, then stops; raising it past the last id uses it up too.
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }
    World_EnsureNextObjectIdAtLeast(&TestWorld, WORLD_OBJECT_ID_MAX);
    Object = CreateObject(run, &TestWorld, NULL);
    TEST_CHECK(run, (Object != NULL) && (WorldObject_GetId(Object) == WORLD_OBJECT_ID_MAX)
        && (World_GetNextObjectId(&TestWorld) == 0U));
    TEST_CHECK_ERROR(run, World_CreateObject(&TestWorld, NULL, &Object), ErrorCode_InvalidOperation);
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }
    World_EnsureNextObjectIdAtLeast(&TestWorld, UINT64_MAX);
    TEST_CHECK(run, World_GetNextObjectId(&TestWorld) == 0U);
    TEST_CHECK_ERROR(run, World_CreateObject(&TestWorld, NULL, &Object), ErrorCode_InvalidOperation);
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

static void ObjectSetVersionChangesOnCreateAndRemove(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }

    uint64_t Initial = World_GetObjectSetVersion(&TestWorld);
    WorldObject* Object = CreateObject(run, &TestWorld, "object");
    if (Object != NULL)
    {
        uint64_t AfterCreate = World_GetObjectSetVersion(&TestWorld);
        TEST_CHECK(run, AfterCreate != Initial);

        // Properties and components are not the object set.
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, (Vector3){ 1.0f, 2.0f, 3.0f }));
        TEST_CHECK(run, AddComponent(run, &TestWorld, Object, WorldComponentType_Light) != NULL);
        TEST_CHECK(run, World_GetObjectSetVersion(&TestWorld) == AfterCreate);

        TEST_CHECK_SUCCESS(run, World_RemoveObject(&TestWorld, WorldObject_GetId(Object)));
        TEST_CHECK(run, World_GetObjectSetVersion(&TestWorld) != AfterCreate);
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

static void ObjectPropertiesValidateAndAdvanceTheRevision(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }

    WorldObject* Object = CreateObject(run, &TestWorld, "object");
    if (Object != NULL)
    {
        Vector3 Scale = WorldObject_GetScale(Object);
        TEST_CHECK(run, (Scale.x == 1.0f) && (Scale.y == 1.0f) && (Scale.z == 1.0f));
        TEST_CHECK(run, WorldObject_IsEnabled(Object) && WorldObject_IsVisible(Object));
        TEST_CHECK(run, WorldObject_GetMobility(Object) == WorldMobility_Static);
        TEST_CHECK(run, (WorldObject_GetTint(Object).Opacity == 1.0f) && (WorldObject_GetTint(Object).Tint.r == 255));

        uint32_t Revision = WorldObject_GetRevision(Object);
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, (Vector3){ 1.0f, 2.0f, 3.0f }));
        TEST_CHECK(run, WorldObject_GetRevision(Object) != Revision);
        Revision = WorldObject_GetRevision(Object);
        TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(Object, (Vector3){ 0.5f, 0.0f, 0.0f }));
        TEST_CHECK_SUCCESS(run, WorldObject_SetName(Object, (const unsigned char*)u8"renamed"));
        TEST_CHECK_SUCCESS(run, WorldObject_SetMobility(Object, WorldMobility_Movable));
        TEST_CHECK(run, WorldObject_GetRevision(Object) == (Revision + 3U));
        TEST_CHECK(run, strcmp((const char*)WorldObject_GetName(Object), "renamed") == 0);

        // The table's rules: nothing changes on a refusal.
        Revision = WorldObject_GetRevision(Object);
        TEST_CHECK_ERROR(run, WorldObject_SetPosition(Object, (Vector3){ NAN, 0.0f, 0.0f }),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldObject_SetScale(Object, (Vector3){ 1.0f, INFINITY, 1.0f }),
            ErrorCode_ArgumentOutOfRange);
        RenderColor TooOpaque = RenderColor_White();
        TooOpaque.Opacity = 2.0f;
        TEST_CHECK_ERROR(run, WorldObject_SetTint(Object, TooOpaque), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldObject_SetMobility(Object, (WorldMobility)7), ErrorCode_ArgumentOutOfRange);
        unsigned char LongName[WORLD_OBJECT_NAME_MAX_LENGTH + 2U];
        memset(LongName, 'a', sizeof(LongName) - 1U);
        LongName[sizeof(LongName) - 1U] = 0;
        TEST_CHECK_ERROR(run, WorldObject_SetName(Object, LongName), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, WorldObject_GetRevision(Object) == Revision);
        TEST_CHECK(run, WorldObject_GetPosition(Object).x == 1.0f);
        TEST_CHECK(run, strcmp((const char*)WorldObject_GetName(Object), "renamed") == 0);

        // The table reaches the same fields, and its writes advance the revision too.
        PropertyValue Value = PropertyValue_FromVector3((Vector3){ 4.0f, 5.0f, 6.0f });
        TEST_CHECK_SUCCESS(run, Properties_Set(WorldObject_GetPropertyTable(), Object, u8"position", &Value));
        TEST_CHECK(run, (WorldObject_GetPosition(Object).y == 5.0f) && (WorldObject_GetRevision(Object) != Revision));
        Value = PropertyValue_FromBool(false);
        TEST_CHECK_SUCCESS(run, Properties_Set(WorldObject_GetPropertyTable(), Object, u8"is_visible", &Value));
        TEST_CHECK(run, !WorldObject_IsVisible(Object));
    }
    TEST_CHECK_ERROR(run, World_CreateObject(&TestWorld, (const unsigned char*)u8"\xff", &Object),
        ErrorCode_ArgumentOutOfRange);
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

static void RotationIsDegreesInJSON(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    JSONObjectPool* Pool = NULL;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }
    WorldObject* Object = CreateObject(run, &TestWorld, "turned");
    if ((Object != NULL) && TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        JSONObjectValue Value;
        if (TestText_ParseJSON(run, Pool, "{\"rotation\": [0, 90, 180], \"mobility\": \"movable\"}", &Value))
        {
            TEST_CHECK_SUCCESS(run, Properties_ReadJSON(WorldObject_GetPropertyTable(), Value.Value.Compound, Object,
                NULL));
            Vector3 Rotation = WorldObject_GetRotation(Object);
            TEST_CHECK(run, (Rotation.x == 0.0f) && (fabsf(Rotation.y - 1.5707964f) < 1e-6f)
                && (fabsf(Rotation.z - 3.1415927f) < 1e-6f));
            TEST_CHECK(run, WorldObject_GetMobility(Object) == WorldMobility_Movable);
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
        }
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}


// Static functions: components.
static void ComponentsAreAddedFoundAndRemoved(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }

    WorldObject* Object = CreateObject(run, &TestWorld, "lamp");
    uint64_t Version = World_GetComponentSetVersion(&TestWorld);
    WorldComponent* ModelComponent = AddComponent(run, &TestWorld, Object, WorldComponentType_Model);
    WorldComponent* Light = AddComponent(run, &TestWorld, Object, WorldComponentType_Light);
    if ((ModelComponent != NULL) && (Light != NULL))
    {
        uint64_t Id = WorldObject_GetId(Object);
        TEST_CHECK(run, World_GetComponentSetVersion(&TestWorld) != Version);
        TEST_CHECK(run, (WorldComponent_GetType(ModelComponent) == WorldComponentType_Model)
            && (WorldComponent_GetOwner(ModelComponent) == Object));
        TEST_CHECK(run, (WorldModelComponent_FromComponent(ModelComponent) != NULL)
            && (WorldLightComponent_FromComponent(ModelComponent) == NULL));
        TEST_CHECK(run, WorldObject_GetComponent(Object, WorldComponentType_Light) == Light);
        TEST_CHECK(run, WorldObject_GetComponent(Object, WorldComponentType_Sprite) == NULL);

        WorldComponent* Found = NULL;
        TEST_CHECK(run, World_TryGetComponent(&TestWorld, Id, WorldComponentType_Model, &Found)
            && (Found == ModelComponent));
        TEST_CHECK(run, !World_TryGetComponent(&TestWorld, Id, WorldComponentType_Sprite, &Found) && (Found == NULL));
        TEST_CHECK(run, !World_TryGetComponent(&TestWorld, 99U, WorldComponentType_Model, &Found));

        // At most one of each type.
        WorldComponent* Second = NULL;
        TEST_CHECK_ERROR(run, World_AddComponent(&TestWorld, Id, WorldComponentType_Model, &Second),
            ErrorCode_InvalidOperation);
        TEST_CHECK(run, Second == NULL);

        Version = World_GetComponentSetVersion(&TestWorld);
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&TestWorld, Id, WorldComponentType_Model));
        TEST_CHECK(run, World_GetComponentSetVersion(&TestWorld) != Version);
        TEST_CHECK(run, WorldObject_GetComponent(Object, WorldComponentType_Model) == NULL);
        TEST_CHECK_ERROR(run, World_RemoveComponent(&TestWorld, Id, WorldComponentType_Model),
            ErrorCode_InvalidOperation);
        size_t Count = 1;
        TEST_CHECK(run, (World_GetComponents(&TestWorld, WorldComponentType_Model, &Count) == NULL) && (Count == 0U));

        // Bad arguments.
        TEST_CHECK_ERROR(run, World_AddComponent(&TestWorld, Id, (WorldComponentType)WorldComponentType_Count, &Second),
            ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, World_AddComponent(&TestWorld, 0U, WorldComponentType_Model, &Second),
            ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, World_AddComponent(&TestWorld, 99U, WorldComponentType_Model, &Second),
            ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, World_RemoveComponent(&TestWorld, 99U, WorldComponentType_Light),
            ErrorCode_InvalidOperation);
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

static void RemovingAnObjectRemovesItsComponents(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }

    WorldObject* Kept = CreateObject(run, &TestWorld, "kept");
    WorldObject* Removed = CreateObject(run, &TestWorld, "removed");
    if ((AddComponent(run, &TestWorld, Kept, WorldComponentType_Model) != NULL)
        && (AddComponent(run, &TestWorld, Removed, WorldComponentType_Model) != NULL)
        && (AddComponent(run, &TestWorld, Removed, WorldComponentType_Sprite) != NULL)
        && (AddComponent(run, &TestWorld, Removed, WorldComponentType_Light) != NULL))
    {
        uint64_t KeptId = WorldObject_GetId(Kept);
        uint64_t Version = World_GetComponentSetVersion(&TestWorld);
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&TestWorld, WorldObject_GetId(Removed)));
        TEST_CHECK(run, World_GetComponentSetVersion(&TestWorld) != Version);
        CheckComponentOwners(run, &TestWorld, WorldComponentType_Model, &KeptId, 1U);
        CheckComponentOwners(run, &TestWorld, WorldComponentType_Sprite, NULL, 0U);
        CheckComponentOwners(run, &TestWorld, WorldComponentType_Light, NULL, 0U);
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

static void PerTypeArraysStayConsistent(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }

    WorldObject* Objects[6] = { NULL };
    bool IsBuilt = true;
    for (size_t Index = 0; Index < 6U; Index++)
    {
        Objects[Index] = CreateObject(run, &TestWorld, "object");
        IsBuilt = IsBuilt && (AddComponent(run, &TestWorld, Objects[Index], WorldComponentType_Model) != NULL);
        if ((Index % 2U) == 0U)
        {
            IsBuilt = IsBuilt && (AddComponent(run, &TestWorld, Objects[Index], WorldComponentType_Light) != NULL);
        }
    }
    if (IsBuilt)
    {
        const uint64_t ModelsAll[] = { 1, 2, 3, 4, 5, 6 };
        const uint64_t LightsAll[] = { 1, 3, 5 };
        CheckComponentOwners(run, &TestWorld, WorldComponentType_Model, ModelsAll, 6U);
        CheckComponentOwners(run, &TestWorld, WorldComponentType_Light, LightsAll, 3U);

        // Interleaved: remove a model, remove a whole object, give the first object its model back (it goes last),
        // add a light to an object that had none.
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&TestWorld, 2U, WorldComponentType_Model));
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&TestWorld, 3U));
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&TestWorld, 1U, WorldComponentType_Model));
        TEST_CHECK(run, AddComponent(run, &TestWorld, Objects[0], WorldComponentType_Model) != NULL);
        TEST_CHECK(run, AddComponent(run, &TestWorld, Objects[3], WorldComponentType_Light) != NULL);
        TEST_CHECK(run, AddComponent(run, &TestWorld, Objects[1], WorldComponentType_Model) != NULL);
        const uint64_t ModelsAfter[] = { 4, 5, 6, 1, 2 };
        const uint64_t LightsAfter[] = { 1, 5, 4 };
        CheckComponentOwners(run, &TestWorld, WorldComponentType_Model, ModelsAfter, 5U);
        CheckComponentOwners(run, &TestWorld, WorldComponentType_Light, LightsAfter, 3U);
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

static void ComponentTypesAreFoundByName(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }

    TEST_CHECK(run, World_GetComponentTypeCount(&TestWorld) == (size_t)WorldComponentType_Count);
    const char* const Names[WorldComponentType_Count] = { "model", "sprite", "light" };
    for (size_t Type = 0; Type < (size_t)WorldComponentType_Count; Type++)
    {
        const WorldComponentTypeInfo* Info = World_GetComponentTypeInfo(&TestWorld, (WorldComponentType)Type);
        TEST_CHECK(run, (Info != NULL) && (Info == WorldComponent_GetStandardTypeInfo((WorldComponentType)Type)));
        TEST_CHECK(run, (Info != NULL) && ((size_t)Info->Type == Type)
            && (strcmp((const char*)Info->Name, Names[Type]) == 0));
        TEST_CHECK_SUCCESS(run, WorldComponentTypeInfo_Check(Info));

        WorldComponentType Found = WorldComponentType_Count;
        TEST_CHECK(run, World_TryFindComponentType(&TestWorld, (const unsigned char*)Names[Type], &Found)
            && ((size_t)Found == Type));
    }
    WorldComponentType Found = WorldComponentType_Model;
    TEST_CHECK(run, !World_TryFindComponentType(&TestWorld, (const unsigned char*)u8"collider", &Found));
    TEST_CHECK(run, World_GetComponentTypeInfo(&TestWorld, WorldComponentType_Count) == NULL);
    TEST_CHECK(run, WorldComponent_GetStandardTypeInfo(WorldComponentType_Count) == NULL);
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

static void ComponentRevisionsAdvanceOnEveryChange(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }

    WorldObject* Object = CreateObject(run, &TestWorld, "object");
    WorldModelComponent* ModelComponent = WorldModelComponent_FromComponent(AddComponent(run, &TestWorld, Object,
        WorldComponentType_Model));
    WorldLightComponent* Light = WorldLightComponent_FromComponent(AddComponent(run, &TestWorld, Object,
        WorldComponentType_Light));
    if ((ModelComponent != NULL) && (Light != NULL))
    {
        TEST_CHECK(run, (WorldModelComponent_GetWorldModel(ModelComponent) == NULL)
            && WorldModelComponent_HasOutline(ModelComponent)
            && !WorldModelComponent_OmitsPixelation(ModelComponent) && WorldModelComponent_CastsShadows(ModelComponent)
            && (WorldModelComponent_GetShadowTier(ModelComponent) == WorldShadowTier_Both));
        TEST_CHECK(run, (WorldLightComponent_GetIntensity(Light) == WORLD_LIGHT_COMPONENT_DEFAULT_INTENSITY)
            && (WorldLightComponent_GetRange(Light) == WORLD_LIGHT_COMPONENT_DEFAULT_RANGE)
            && !WorldLightComponent_CastsShadows(Light));

        uint32_t ObjectRevision = WorldObject_GetRevision(Object);
        uint32_t Revision = WorldComponent_GetRevision(&ModelComponent->Base);
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(ModelComponent, (const unsigned char*)u8"crate"));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetHasOutline(ModelComponent, false));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOmitsPixelation(ModelComponent, true));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetShadowTier(ModelComponent, WorldShadowTier_Far));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetCastsShadows(ModelComponent, false));
        TEST_CHECK(run, WorldComponent_GetRevision(&ModelComponent->Base) == (Revision + 5U));
        PropertyValue Tier = PropertyValue_FromEnum(WorldShadowTier_Near);
        TEST_CHECK_SUCCESS(run, Properties_Set(WorldModelComponent_GetPropertyTable(), ModelComponent, u8"shadow_tier",
            &Tier));
        TEST_CHECK(run, (WorldModelComponent_GetShadowTier(ModelComponent) == WorldShadowTier_Near)
            && (WorldComponent_GetRevision(&ModelComponent->Base) == (Revision + 6U)));
        TEST_CHECK(run, WorldObject_GetRevision(Object) == ObjectRevision); // a component is not its object

        // Refused values change nothing.
        Revision = WorldComponent_GetRevision(&ModelComponent->Base);
        TEST_CHECK_ERROR(run, WorldModelComponent_SetWorldModel(ModelComponent, (const unsigned char*)u8"Not A Name"),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldModelComponent_SetShadowTier(ModelComponent, (WorldShadowTier)9),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldLightComponent_SetRange(Light, -1.0f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldLightComponent_SetIntensity(Light, NAN), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, WorldComponent_GetRevision(&ModelComponent->Base) == Revision);
        TEST_CHECK(run, strcmp((const char*)WorldModelComponent_GetWorldModel(ModelComponent), "crate") == 0);

        Revision = WorldComponent_GetRevision(&Light->Base);
        TEST_CHECK_SUCCESS(run, WorldLightComponent_SetRange(Light, 6.0f));
        TEST_CHECK_SUCCESS(run, WorldLightComponent_SetColor(Light, (Color){ 255, 200, 100, 255 }));
        TEST_CHECK(run, (WorldLightComponent_GetRange(Light) == 6.0f)
            && (WorldComponent_GetRevision(&Light->Base) == (Revision + 2U)));
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}


/* A placement's own sprite settings start at their documented defaults with every override flag off, refuse what their
 * rules refuse (changing nothing), read their enum names and the pivot from JSON, and round-trip through it. */
static void SpriteSettingsHaveDefaultsRulesAndNames(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    JSONObjectPool* Pool = NULL;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }
    WorldSpriteComponent* Sprite = WorldSpriteComponent_FromComponent(AddComponent(run, &TestWorld,
        CreateObject(run, &TestWorld, "sprite"), WorldComponentType_Sprite));
    WorldSpriteComponent* Copy = WorldSpriteComponent_FromComponent(AddComponent(run, &TestWorld,
        CreateObject(run, &TestWorld, "copy"), WorldComponentType_Sprite));
    if ((Sprite == NULL) || (Copy == NULL) || !TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
        return;
    }

    Vector2 Pivot = WorldSpriteComponent_GetPivot(Sprite);
    TEST_CHECK(run, (WorldSpriteComponent_GetWorldSprite(Sprite) == NULL)
        && (WorldSpriteComponent_GetBoundWorldSprite(Sprite) == NULL));
    TEST_CHECK(run, (WorldSpriteComponent_GetOrientation(Sprite) == WorldSpriteOrientation_Billboard)
        && (Pivot.x == 0.5f) && (Pivot.y == 0.5f) && (WorldSpriteComponent_GetPixelsPerUnit(Sprite) == 32.0f)
        && !WorldSpriteComponent_IsFlippedX(Sprite) && !WorldSpriteComponent_IsFlippedY(Sprite));
    TEST_CHECK(run, (WorldSpriteComponent_GetAlphaMode(Sprite) == WorldSpriteAlphaMode_Cutout)
        && (WorldSpriteComponent_GetAlphaCutoff(Sprite) == 0.5f) && WorldSpriteComponent_IsLit(Sprite)
        && (WorldSpriteComponent_GetEmission(Sprite) == 1.0f) && (WorldSpriteComponent_GetLightWrap(Sprite) == 1.0f)
        && WorldSpriteComponent_CastsShadows(Sprite));
    TEST_CHECK(run, !WorldSpriteComponent_IsDecal(Sprite) && (WorldSpriteComponent_GetDecalOffset(Sprite) == 0.01f)
        && WorldSpriteComponent_HasOutline(Sprite) && !WorldSpriteComponent_OmitsPixelation(Sprite));
    TEST_CHECK(run, (WorldSpriteComponent_GetDrawDistance(Sprite) == 150.0f)
        && (WorldSpriteComponent_GetDrawDistanceMultiplier(Sprite) == 1.0f)
        && !WorldSpriteComponent_IsDrawDistanceFixed(Sprite));
    bool IsAnyOverridden = false;
    for (int Setting = 0; Setting < (int)WorldSpriteSetting_Count; Setting++)
    {
        IsAnyOverridden = IsAnyOverridden || WorldSpriteComponent_IsOverridden(Sprite, (WorldSpriteSetting)Setting);
    }
    TEST_CHECK(run, !IsAnyOverridden);

    // Every setter advances the revision once.
    uint32_t Revision = WorldComponent_GetRevision(&Sprite->Base);
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOrientation(Sprite, WorldSpriteOrientation_AxisBillboard));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetPivot(Sprite, (Vector2){ 0.5f, 0.0f }));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetPixelsPerUnit(Sprite, 16.0f));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetFlippedX(Sprite, true));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetFlippedY(Sprite, true));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetAlphaMode(Sprite, WorldSpriteAlphaMode_Blend));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetAlphaCutoff(Sprite, 0.25f));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetLit(Sprite, false));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetEmission(Sprite, 3.0f));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetLightWrap(Sprite, 0.5f));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetCastsShadows(Sprite, false));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetDecal(Sprite, true));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetDecalOffset(Sprite, 0.05f));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetHasOutline(Sprite, false));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOmitsPixelation(Sprite, true));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetDrawDistance(Sprite, 40.0f));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetDrawDistanceMultiplier(Sprite, 2.0f));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetIsDrawDistanceFixed(Sprite, true));
    TEST_CHECK(run, WorldComponent_GetRevision(&Sprite->Base) == (Revision + 18U));
    TEST_CHECK(run, (WorldSpriteComponent_GetPixelsPerUnit(Sprite) == 16.0f)
        && (WorldSpriteComponent_GetEmission(Sprite) == 3.0f) && WorldSpriteComponent_IsFlippedY(Sprite)
        && (WorldSpriteComponent_GetDrawDistance(Sprite) == 40.0f)
        && (WorldSpriteComponent_GetDrawDistanceMultiplier(Sprite) == 2.0f)
        && WorldSpriteComponent_IsDrawDistanceFixed(Sprite));

    // Every override flag, on and off, advances it too.
    Revision = WorldComponent_GetRevision(&Sprite->Base);
    for (int Setting = 0; Setting < (int)WorldSpriteSetting_Count; Setting++)
    {
        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Sprite, (WorldSpriteSetting)Setting, true));
        TEST_CHECK(run, WorldSpriteComponent_IsOverridden(Sprite, (WorldSpriteSetting)Setting));
        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Sprite, (WorldSpriteSetting)Setting, false));
    }
    TEST_CHECK(run, WorldComponent_GetRevision(&Sprite->Base) == (Revision + (2U * (uint32_t)WorldSpriteSetting_Count)));

    // Refused values change nothing.
    Revision = WorldComponent_GetRevision(&Sprite->Base);
    const float BadPixelsPerUnit[3] = { 0.0f, -32.0f, NAN };
    for (size_t Index = 0; Index < 3; Index++)
    {
        TEST_CHECK_ERROR(run, WorldSpriteComponent_SetPixelsPerUnit(Sprite, BadPixelsPerUnit[Index]),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldSpriteComponent_SetDrawDistance(Sprite, BadPixelsPerUnit[Index]),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldSpriteComponent_SetDrawDistanceMultiplier(Sprite, BadPixelsPerUnit[Index]),
            ErrorCode_ArgumentOutOfRange);
    }
    TEST_CHECK_ERROR(run, WorldSpriteComponent_SetAlphaCutoff(Sprite, 1.5f), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, WorldSpriteComponent_SetAlphaCutoff(Sprite, -0.1f), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, WorldSpriteComponent_SetEmission(Sprite, -1.0f), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, WorldSpriteComponent_SetEmission(Sprite, INFINITY), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, WorldSpriteComponent_SetLightWrap(Sprite, 2.0f), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, WorldSpriteComponent_SetDecalOffset(Sprite, -0.01f), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, WorldSpriteComponent_SetPivot(Sprite, (Vector2){ NAN, 0.0f }), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, WorldSpriteComponent_SetOrientation(Sprite, (WorldSpriteOrientation)7),
        ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, WorldSpriteComponent_SetAlphaMode(Sprite, (WorldSpriteAlphaMode)9),
        ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, WorldSpriteComponent_SetOverridden(Sprite, WorldSpriteSetting_Count, true),
        ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, WorldSpriteComponent_SetWorldSprite(Sprite, (const unsigned char*)u8"Bad Name"),
        ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, !WorldSpriteComponent_IsOverridden(Sprite, WorldSpriteSetting_Count));
    TEST_CHECK(run, (WorldComponent_GetRevision(&Sprite->Base) == Revision)
        && (WorldSpriteComponent_GetPixelsPerUnit(Sprite) == 16.0f)
        && (WorldSpriteComponent_GetDrawDistance(Sprite) == 40.0f)
        && (WorldSpriteComponent_GetOrientation(Sprite) == WorldSpriteOrientation_AxisBillboard));

    // JSON: the enums by name, the pivot as an array, the flags next to their values; a bad value refuses the whole
    // read.
    const PropertyTable* Table = WorldSpriteComponent_GetPropertyTable();
    JSONObjectValue Value;
    if (TestText_ParseJSON(run, Pool, "{\"orientation\": \"fixed\", \"pivot\": [0.5, 0], \"pixels_per_unit\": 64, "
        "\"flip_x\": true, \"alpha_mode\": \"additive\", \"is_lit\": false, \"emission\": 2.5, \"light_wrap\": 0.25, "
        "\"is_decal\": true, \"decal_offset\": 0.02, \"casts_shadows\": false, \"overrides_flip_x\": true, "
        "\"overrides_draw_distance\": true, \"draw_distance\": 12}", &Value))
    {
        TEST_CHECK_SUCCESS(run, Properties_ReadJSON(Table, Value.Value.Compound, Sprite, NULL));
        Pivot = WorldSpriteComponent_GetPivot(Sprite);
        TEST_CHECK(run, (WorldSpriteComponent_GetOrientation(Sprite) == WorldSpriteOrientation_Fixed)
            && (Pivot.x == 0.5f) && (Pivot.y == 0.0f) && (WorldSpriteComponent_GetPixelsPerUnit(Sprite) == 64.0f)
            && WorldSpriteComponent_IsFlippedX(Sprite) && !WorldSpriteComponent_IsFlippedY(Sprite));
        TEST_CHECK(run, (WorldSpriteComponent_GetAlphaMode(Sprite) == WorldSpriteAlphaMode_Additive)
            && !WorldSpriteComponent_IsLit(Sprite) && (WorldSpriteComponent_GetEmission(Sprite) == 2.5f)
            && (WorldSpriteComponent_GetLightWrap(Sprite) == 0.25f) && WorldSpriteComponent_IsDecal(Sprite)
            && !WorldSpriteComponent_CastsShadows(Sprite));
        TEST_CHECK(run, WorldSpriteComponent_IsOverridden(Sprite, WorldSpriteSetting_FlipX)
            && WorldSpriteComponent_IsOverridden(Sprite, WorldSpriteSetting_DrawDistance)
            && !WorldSpriteComponent_IsOverridden(Sprite, WorldSpriteSetting_Orientation)
            && (WorldSpriteComponent_GetDrawDistance(Sprite) == 12.0f)
            && (WorldSpriteComponent_GetDrawDistanceMultiplier(Sprite) == 1.0f));
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
    }
    if (TestText_ParseJSON(run, Pool, "{\"alpha_mode\": \"glow\", \"pixels_per_unit\": 8}", &Value))
    {
        TEST_CHECK_ERROR(run, Properties_ReadJSON(Table, Value.Value.Compound, Sprite, NULL), ErrorCode_InvalidJSON);
        TEST_CHECK(run, WorldSpriteComponent_GetPixelsPerUnit(Sprite) == 64.0f);
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
    }

    // Written and read back into another sprite, every setting and flag survives.
    JSONCompound* Written = NULL;
    if (TEST_CHECK_SUCCESS(run, JSONObjectPool_BorrowCompound(Pool, &Written)))
    {
        TEST_CHECK_SUCCESS(run, Properties_WriteJSON(Table, Sprite, Pool, Written, NULL));
        TEST_CHECK_SUCCESS(run, Properties_ReadJSON(Table, Written, Copy, NULL));
        TEST_CHECK(run, Properties_Equals(Table, Sprite, Copy));
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnCompound(Pool, Written, true));
    }

    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}


// Static functions: the sprite binding.
static void WorldWithoutAssetsLeavesSpritesEmpty(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }
    WorldSpriteComponent* Sprite = AddSprite(run, &TestWorld, "walk");
    if (Sprite != NULL)
    {
        TEST_CHECK(run, !SpriteAnimationInstance_HasSource(WorldSpriteComponent_GetAnimationInstance(Sprite)));
        TEST_CHECK(run, WorldSpriteComponent_GetBoundWorldSprite(Sprite) == NULL);
        TEST_CHECK(run, strcmp((const char*)WorldSpriteComponent_GetWorldSprite(Sprite), "walk") == 0);
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

static void SpriteBindsAndTheWorldReleasesTheAnimationLast(TestRun* run)
{
    TestAssetFixture Fixture;
    TestAssetCounters Counters = { 0 };
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestAssetFixture_Construct(run, &Fixture)
        || !TestAssets_AddSpriteAnimation(run, &Fixture, "walk_animation", &Counters)
        || !TestAssets_AddWorldSpriteOf(run, &Fixture, "walk", "walk_animation")
        || !TestWorld_Construct(run, &TestWorld, &TestKeys, Fixture.Manager, NULL))
    {
        TestAssetFixture_Deconstruct(run, &Fixture);
        return;
    }

    WorldSpriteComponent* First = AddSprite(run, &TestWorld, "walk");
    WorldSpriteComponent* Second = AddSprite(run, &TestWorld, "walk");
    if ((First != NULL) && (Second != NULL))
    {
        const WorldSprite* Bound = WorldSpriteComponent_GetBoundWorldSprite(First);
        TEST_CHECK(run, IsBound(First) && IsBound(Second) && (Bound != NULL)
            && (WorldSpriteComponent_GetBoundWorldSprite(Second) == Bound));
        // Both play the world sprite's animation.
        TEST_CHECK(run, (Bound != NULL)
            && (SpriteAnimationInstance_GetSource(WorldSpriteComponent_GetAnimationInstance(First))
                == WorldSprite_GetAnimation(Bound))
            && (SpriteAnimationInstance_GetSource(WorldSpriteComponent_GetAnimationInstance(Second))
                == WorldSprite_GetAnimation(Bound)));
        TEST_CHECK(run, Counters.LoadCount == 1); // shared: one load, a dependency of the one world sprite

        // Removing a sprite does not release anything: the world's user holds the world sprite until the world goes.
        uint64_t FirstId = WorldObject_GetId(WorldComponent_GetOwner(&First->Base));
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&TestWorld, FirstId, WorldComponentType_Sprite));
    }

    TEST_CHECK(run, Counters.UnloadCount == 0);
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
    TEST_CHECK(run, Counters.UnloadCount == 1); // the animation unloaded with its world sprite
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* A world sprite that does not exist, and one whose animation does not, leave their sprites empty, each logged once. */
static void MissingWorldSpriteIsLoggedOnceAndLeavesTheSpriteEmpty(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, WORLD_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    Logger Log;
    TestAssetFixture Fixture;
    if (!TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "world_missing_sprite", &Directory))
        || !TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Directory._data)))
    {
        Memory_Free(Directory._data);
        return;
    }

    World TestWorld;
    KeyRegistry TestKeys;
    if (TestAssetFixture_Construct(run, &Fixture) && TestAssets_AddWorldSpriteOf(run, &Fixture, "broken", "nowhere")
        && TestWorld_Construct(run, &TestWorld, &TestKeys, Fixture.Manager, &Log))
    {
        WorldSpriteComponent* Sprite = AddSprite(run, &TestWorld, "missing");
        WorldSpriteComponent* Broken = AddSprite(run, &TestWorld, "broken");
        if ((Sprite != NULL) && (Broken != NULL))
        {
            TEST_CHECK(run, !SpriteAnimationInstance_HasSource(WorldSpriteComponent_GetAnimationInstance(Sprite))
                && (WorldSpriteComponent_GetBoundWorldSprite(Sprite) == NULL));
            TEST_CHECK(run, !IsBound(Broken) && (WorldSpriteComponent_GetBoundWorldSprite(Broken) == NULL));
            // The same name again changes nothing, so it is not loaded or reported again.
            TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetWorldSprite(Sprite, (const unsigned char*)u8"missing"));
            TEST_CHECK(run, !IsBound(Sprite));
        }
        TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
    TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "shows nothing: world sprite \"missing\"") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "shows nothing: world sprite \"broken\"") == 1U);
    Memory_Free(Directory._data);
}

static void RenamingRebindsAndTheSameNameKeepsPlaying(TestRun* run)
{
    TestAssetFixture Fixture;
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestAssetFixture_Construct(run, &Fixture)
        || !TestAssets_AddPlayingSpriteAnimation(run, &Fixture, "walk_animation", 4U, 10.0, NULL)
        || !TestAssets_AddSpriteAnimation(run, &Fixture, "idle_animation", NULL)
        || !TestAssets_AddWorldSpriteOf(run, &Fixture, "walk", "walk_animation")
        || !TestAssets_AddWorldSpriteOf(run, &Fixture, "idle", "idle_animation")
        || !TestWorld_Construct(run, &TestWorld, &TestKeys, Fixture.Manager, NULL))
    {
        TestAssetFixture_Deconstruct(run, &Fixture);
        return;
    }

    WorldSpriteComponent* Sprite = AddSprite(run, &TestWorld, "missing");
    if (Sprite != NULL)
    {
        SpriteAnimationInstance* Instance = WorldSpriteComponent_GetAnimationInstance(Sprite);
        TEST_CHECK(run, !IsBound(Sprite));
        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetWorldSprite(Sprite, (const unsigned char*)u8"walk"));
        TEST_CHECK(run, IsBound(Sprite) && (SpriteAnimationInstance_GetFrameCount(Instance) == 4U));
        const WorldSprite* Walk = WorldSpriteComponent_GetBoundWorldSprite(Sprite);

        // Playing on: the same name keeps the playback, another name restarts it.
        TEST_CHECK_SUCCESS(run, SpriteAnimationInstance_Update(Instance, 0.15));
        TEST_CHECK(run, SpriteAnimationInstance_GetFrameIndex(Instance) == 1U);
        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetWorldSprite(Sprite, (const unsigned char*)u8"walk"));
        TEST_CHECK(run, SpriteAnimationInstance_GetFrameIndex(Instance) == 1U);
        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetWorldSprite(Sprite, (const unsigned char*)u8"idle"));
        TEST_CHECK(run, IsBound(Sprite) && (SpriteAnimationInstance_GetFrameCount(Instance) == 1U)
            && (WorldSpriteComponent_GetBoundWorldSprite(Sprite) != Walk));
        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetWorldSprite(Sprite, (const unsigned char*)u8"walk"));
        TEST_CHECK(run, (SpriteAnimationInstance_GetFrameIndex(Instance) == 0U)
            && (WorldSpriteComponent_GetBoundWorldSprite(Sprite) == Walk));

        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetWorldSprite(Sprite, NULL));
        TEST_CHECK(run, !SpriteAnimationInstance_HasSource(Instance)
            && (WorldSpriteComponent_GetBoundWorldSprite(Sprite) == NULL));
        TEST_CHECK(run, WorldSpriteComponent_GetWorldSprite(Sprite) == NULL);
        TEST_CHECK_ERROR(run, WorldSpriteComponent_SetWorldSprite(Sprite, (const unsigned char*)u8"Bad Name"),
            ErrorCode_ArgumentOutOfRange);
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void TableWritesRebindTheSprite(TestRun* run)
{
    TestAssetFixture Fixture;
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestAssetFixture_Construct(run, &Fixture) || !TestAssets_AddSpriteAnimation(run, &Fixture, "walk_animation", NULL)
        || !TestAssets_AddWorldSpriteOf(run, &Fixture, "walk", "walk_animation")
        || !TestWorld_Construct(run, &TestWorld, &TestKeys, Fixture.Manager, NULL))
    {
        TestAssetFixture_Deconstruct(run, &Fixture);
        return;
    }

    const PropertyTable* Table = WorldSpriteComponent_GetPropertyTable();
    WorldSpriteComponent* Sprite = AddSprite(run, &TestWorld, "missing");
    WorldSpriteComponent* Other = AddSprite(run, &TestWorld, "walk");
    if ((Sprite != NULL) && (Other != NULL))
    {
        PropertyValue Name = PropertyValue_FromAssetName((const unsigned char*)u8"walk");
        TEST_CHECK_SUCCESS(run, Properties_Set(Table, Sprite, u8"world_sprite", &Name));
        TEST_CHECK(run, IsBound(Sprite));

        // A read may change anything (its hook gets no row): the name is compared with the bound one.
        TEST_CHECK_SUCCESS(run, ReadComponentJSON(run, &Sprite->Base, Table, "{\"world_sprite\": \"missing\"}"));
        TEST_CHECK(run, !IsBound(Sprite) && (WorldSpriteComponent_GetBoundWorldSprite(Sprite) == NULL));
        // An absent key reads as its default: no world sprite.
        TEST_CHECK_SUCCESS(run, ReadComponentJSON(run, &Sprite->Base, Table, "{\"has_outline\": false}"));
        TEST_CHECK(run, (WorldSpriteComponent_GetWorldSprite(Sprite) == NULL) && !IsBound(Sprite));
        TEST_CHECK(run, !WorldSpriteComponent_HasOutline(Sprite));

        TEST_CHECK_SUCCESS(run, Properties_CopyTo(Table, Other, Sprite));
        TEST_CHECK(run, IsBound(Sprite) && WorldSpriteComponent_HasOutline(Sprite)
            && (WorldSpriteComponent_GetBoundWorldSprite(Sprite) == WorldSpriteComponent_GetBoundWorldSprite(Other)));
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Static functions: flags, mobility and drivers.
static void FlagsAdvanceTheRevision(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }

    WorldObject* Object = CreateObject(run, &TestWorld, "object");
    if (Object != NULL)
    {
        uint32_t Revision = WorldObject_GetRevision(Object);
        TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(Object, false));
        TEST_CHECK(run, !WorldObject_IsEnabled(Object) && (WorldObject_GetRevision(Object) == (Revision + 1U)));
        TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(Object, false));
        TEST_CHECK(run, !WorldObject_IsVisible(Object) && (WorldObject_GetRevision(Object) == (Revision + 2U)));
        TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(Object, true));
        TEST_CHECK(run, WorldObject_IsEnabled(Object) && !WorldObject_IsVisible(Object));
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

static void AddingADriverMakesTheObjectMovable(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!ConstructDriverWorld(run, &TestWorld, &TestKeys))
    {
        return;
    }

    TEST_CHECK(run, World_GetComponentTypeCount(&TestWorld) == ((size_t)WorldComponentType_Count + 2U));
    WorldComponentType Found = WorldComponentType_Model;
    TEST_CHECK(run, World_TryFindComponentType(&TestWorld, (const unsigned char*)u8"test_driver_b", &Found)
        && (Found == TestDriverB.Type));
    WorldObject* Object = CreateObject(run, &TestWorld, "walker");
    if ((Object != NULL) && (AddComponent(run, &TestWorld, Object, WorldComponentType_Model) != NULL))
    {
        TEST_CHECK(run, WorldObject_GetMobility(Object) == WorldMobility_Static); // a model is no driver
        uint32_t Revision = WorldObject_GetRevision(Object);
        WorldComponent* Driver = AddComponent(run, &TestWorld, Object, TestDriverA.Type);
        TEST_CHECK(run, (Driver != NULL) && (WorldObject_GetMobility(Object) == WorldMobility_Movable));
        TEST_CHECK(run, WorldObject_GetRevision(Object) != Revision);

        // A hint, never a lock: a driven object may still be marked static.
        TEST_CHECK_SUCCESS(run, WorldObject_SetMobility(Object, WorldMobility_Static));
        TEST_CHECK(run, WorldObject_GetMobility(Object) == WorldMobility_Static);
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

static void ASecondTransformDriverIsRefused(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!ConstructDriverWorld(run, &TestWorld, &TestKeys))
    {
        return;
    }

    WorldObject* Object = CreateObject(run, &TestWorld, "car");
    WorldObject* Other = CreateObject(run, &TestWorld, "player");
    if ((Object != NULL) && (Other != NULL) && (AddComponent(run, &TestWorld, Object, TestDriverA.Type) != NULL))
    {
        uint64_t Id = WorldObject_GetId(Object);
        WorldComponent* Second = NULL;
        TEST_CHECK_ERROR(run, World_AddComponent(&TestWorld, Id, TestDriverB.Type, &Second),
            ErrorCode_InvalidOperation);
        TEST_CHECK(run, (Second == NULL) && (WorldObject_GetComponent(Object, TestDriverB.Type) == NULL));
        // Other components and other objects are not affected.
        TEST_CHECK(run, AddComponent(run, &TestWorld, Object, WorldComponentType_Light) != NULL);
        TEST_CHECK(run, AddComponent(run, &TestWorld, Other, TestDriverB.Type) != NULL);

        // Once the first driver is gone, another may take over.
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&TestWorld, Id, TestDriverA.Type));
        TEST_CHECK(run, AddComponent(run, &TestWorld, Object, TestDriverB.Type) != NULL);
        const uint64_t DriverBOwners[] = { WorldObject_GetId(Other), Id };
        CheckComponentOwners(run, &TestWorld, TestDriverB.Type, DriverBOwners, 2U);
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

static void CustomComponentTypesAreChecked(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&TestKeys)))
    {
        return;
    }
    const WorldComponentTypeInfo* Shifted[2] =
    {
        WorldComponent_GetStandardTypeInfo(WorldComponentType_Sprite), // type 1 at index 0
        WorldComponent_GetStandardTypeInfo(WorldComponentType_Model),
    };
    TEST_CHECK_ERROR(run, WorldInternal_ConstructWithTypes(&TestWorld, &TestKeys, NULL, NULL, Shifted, 2U),
        ErrorCode_IllegalArgument);

    // A test type may not take a standard type's slot: the standard downcasts trust the slot.
    const WorldComponentTypeInfo Impostor = { .Type = WorldComponentType_Model, .Name = u8"impostor",
        .Size = sizeof(TestDriverComponent), .Table = &TestDriverTable, .IsTransformDriver = false,
        .Construct = ConstructTestDriver, .Deconstruct = DeconstructTestDriver };
    const WorldComponentTypeInfo* Impostors[1] = { &Impostor };
    TEST_CHECK_ERROR(run, WorldInternal_ConstructWithTypes(&TestWorld, &TestKeys, NULL, NULL, Impostors, 1U),
        ErrorCode_IllegalArgument);

    const WorldComponentTypeInfo* Unhooked[WorldComponentType_Count + 1] =
    {
        WorldComponent_GetStandardTypeInfo(WorldComponentType_Model),
        WorldComponent_GetStandardTypeInfo(WorldComponentType_Sprite),
        WorldComponent_GetStandardTypeInfo(WorldComponentType_Light),
        &TestUnhooked,
    };
    const char* const Fragments[] = { "test_unhooked", "AfterSet", NULL };
    TestText_CheckErrorMessage(run, WorldInternal_ConstructWithTypes(&TestWorld, &TestKeys, NULL, NULL, Unhooked,
        sizeof(Unhooked) / sizeof(Unhooked[0])), ErrorCode_IllegalArgument, Fragments);

    // Every world needs a key registry.
    TEST_CHECK_ERROR(run, World_Construct1(&TestWorld, NULL), ErrorCode_IllegalArgument);
    KeyRegistry_Deconstruct(&TestKeys);
}


// Public functions.
void WorldTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "objects_get_unique_ids_that_are_never_zero", ObjectsGetUniqueIdsThatAreNeverZero },
        { "loader_ids_advance_the_counter", LoaderIdsAdvanceTheCounter },
        { "object_set_version_changes_on_create_and_remove", ObjectSetVersionChangesOnCreateAndRemove },
        { "object_properties_validate_and_advance_the_revision", ObjectPropertiesValidateAndAdvanceTheRevision },
        { "rotation_is_degrees_in_json", RotationIsDegreesInJSON },
        { "components_are_added_found_and_removed", ComponentsAreAddedFoundAndRemoved },
        { "removing_an_object_removes_its_components", RemovingAnObjectRemovesItsComponents },
        { "per_type_arrays_stay_consistent", PerTypeArraysStayConsistent },
        { "component_types_are_found_by_name", ComponentTypesAreFoundByName },
        { "component_revisions_advance_on_every_change", ComponentRevisionsAdvanceOnEveryChange },
        { "sprite_settings_have_defaults_rules_and_names", SpriteSettingsHaveDefaultsRulesAndNames },
        { "world_without_assets_leaves_sprites_empty", WorldWithoutAssetsLeavesSpritesEmpty },
        { "sprite_binds_and_the_world_releases_the_animation_last", SpriteBindsAndTheWorldReleasesTheAnimationLast },
        { "missing_world_sprite_is_logged_once_and_leaves_the_sprite_empty",
            MissingWorldSpriteIsLoggedOnceAndLeavesTheSpriteEmpty },
        { "renaming_rebinds_and_the_same_name_keeps_playing", RenamingRebindsAndTheSameNameKeepsPlaying },
        { "table_writes_rebind_the_sprite", TableWritesRebindTheSprite },
        { "flags_advance_the_revision", FlagsAdvanceTheRevision },
        { "adding_a_driver_makes_the_object_movable", AddingADriverMakesTheObjectMovable },
        { "a_second_transform_driver_is_refused", ASecondTransformDriverIsRefused },
        { "custom_component_types_are_checked", CustomComponentTypesAreChecked },
    };
    TestRun_RunSuite(run, "World", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
