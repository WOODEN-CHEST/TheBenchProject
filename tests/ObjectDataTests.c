#include "TestSuites.h"
#include "TestText.h"
#include "TestWorld.h"
#include "DataMap.h"
#include "DataValue.h"
#include "DataValueInternal.h"
#include "GameCalendar.h"
#include "Logger.h"
#include "World.h"
#include "WorldObject.h"
#include "WorldQuery.h"
#include "wr/WRFileSystem.h"
#include "wr/WRMemoryStream.h"
#include "wr/WRPath.h"
#include "wr/WRRandom.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** Initial capacity of text buffers; they grow as needed. */
#define DATA_TEST_TEXT_CAPACITY ((size_t)1024)
/** The random world of the brute-force comparison: its objects, tags, removed objects and queries. */
#define RANDOM_OBJECT_COUNT 300U
#define RANDOM_TAG_COUNT 8U
#define RANDOM_REMOVED_COUNT 30U
#define RANDOM_QUERY_COUNT 400U
/** The most tags or keys one random filter uses. */
#define RANDOM_FILTER_MAX 3U


// Types.
/* A world, its key registry and the keys the tests use. */
typedef struct DataFixtureStruct
{
    KeyRegistry Keys;
    World World;
} DataFixture;

/* One match of the brute-force oracle. */
typedef struct OracleMatchStruct
{
    uint64_t Id;
    float DistanceSquared;
} OracleMatch;


// Static functions: helpers.
static bool Fixture_Construct(TestRun* run, DataFixture* self, Logger* logger)
{
    return TestWorld_Construct(run, &self->World, &self->Keys, NULL, logger);
}

static void Fixture_Deconstruct(TestRun* run, DataFixture* self)
{
    TestWorld_Deconstruct(run, &self->World, &self->Keys);
}

static uint32_t Key(TestRun* run, DataFixture* fixture, const char* text)
{
    uint32_t Id = NAMESPACED_KEY_ID_NONE;
    TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(&fixture->Keys, (const unsigned char*)text, &Id));
    return Id;
}

/* Creates an object at @p position; returns its id (0 with a failed check). */
static uint64_t AddObject(TestRun* run, World* world, const char* name, Vector3 position)
{
    WorldObject* Object = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(world, (const unsigned char*)name, &Object))
        || !TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, position)))
    {
        return 0U;
    }
    return WorldObject_GetId(Object);
}

static WorldObject* GetObject(World* world, uint64_t id)
{
    WorldObject* Object = NULL;
    World_TryGetObject(world, id, &Object);
    return Object;
}

static void SetInt(TestRun* run, World* world, uint64_t id, uint32_t key, int64_t value)
{
    DataValue Value = DataValue_FromInt(value);
    TEST_CHECK_SUCCESS(run, World_SetData(world, id, key, &Value));
}

static DataValue MakeString(TestRun* run, const char* text)
{
    DataValue Value = DataValue_CreateTag();
    TEST_CHECK_SUCCESS(run, DataValue_CreateString((const unsigned char*)text, &Value));
    return Value;
}

/* Runs a query and checks it returns exactly @p expected (in order). */
static void CheckQuery(TestRun* run, World* world, const WorldQuery* query, const uint64_t* expected, size_t count)
{
    GenericBuffer Ids;
    GenericBuffer_AllocateVariable(&Ids, 0U, sizeof(uint64_t));
    if (TEST_CHECK_SUCCESS(run, World_Query(world, query, &Ids)) && TEST_CHECK(run, Ids._count == count))
    {
        TEST_CHECK(run, (count == 0U) || Memory_IsEqual(Ids._data, expected, count * sizeof(uint64_t)));
    }
    Memory_Free(Ids._data);
}

/* The sum of every object's DataMap_GetByteSize: what the world's count must equal. */
static size_t SumObjectBytes(World* world)
{
    size_t Sum = 0U;
    for (size_t Index = 0U; Index < World_GetObjectCount(world); Index++)
    {
        WorldObject* Object = NULL;
        World_GetObjectByIndex(world, Index, &Object);
        Sum += DataMap_GetByteSize(WorldObject_GetData(Object));
    }
    return Sum;
}

/* Counts the occurrences of @p needle in "<directory>/logs/latest.log". */
static size_t CountInLatestLog(TestRun* run, const unsigned char* directory, const char* needle)
{
    size_t Count = 0U;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, DATA_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, DATA_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, Path_Append(directory, (const unsigned char*)u8"logs/latest.log", &Path))
        && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, &Text))
        && TEST_CHECK(run, GenericBuffer_NullTerminate(&Text)))
    {
        for (const char* Found = strstr((const char*)Text._data, needle); Found != NULL; Found = strstr(Found + 1, needle))
        {
            Count++;
        }
    }
    Memory_Free(Text._data);
    Memory_Free(Path._data);
    return Count;
}

/* A string of @p length 's' bytes. */
static DataValue MakeLongString(TestRun* run, size_t length)
{
    unsigned char Text[DATA_VALUE_MAX_STRING_LENGTH + 1U];
    memset(Text, 's', length);
    Text[length] = (unsigned char)0;
    DataValue Value = DataValue_CreateTag();
    TEST_CHECK_SUCCESS(run, DataValue_CreateString(Text, &Value));
    return Value;
}


// Static functions: the data functions.
static void SetGetOverwriteAndRemove(TestRun* run)
{
    DataFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, NULL))
    {
        return;
    }
    World* TestWorld = &Fixture.World;
    uint64_t Desk = AddObject(run, TestWorld, "desk", (Vector3){ 0.0f, 0.0f, 0.0f });
    uint32_t Visits = Key(run, &Fixture, "town:visits");
    TEST_CHECK(run, WorldObject_GetData(GetObject(TestWorld, Desk)) == NULL); // NULL while empty

    SetInt(run, TestWorld, Desk, Visits, 3);
    const DataValue* Found = NULL;
    TEST_CHECK(run, World_TryGetData(TestWorld, Desk, Visits, &Found) && (Found->Type == DataType_Int)
        && (Found->IntValue == 3));

    // Another type replaces the value; the caller keeps its own copy.
    DataValue Name = MakeString(run, "Bob");
    TEST_CHECK_SUCCESS(run, World_SetData(TestWorld, Desk, Visits, &Name));
    TEST_CHECK(run, World_TryGetData(TestWorld, Desk, Visits, &Found) && (Found->Type == DataType_String)
        && (Found->StringValue != Name.StringValue) && (strcmp((const char*)Found->StringValue, "Bob") == 0));
    DataValue_Deconstruct(&Name);
    TEST_CHECK(run, DataMap_GetCount(WorldObject_GetData(GetObject(TestWorld, Desk))) == 1U);

    bool WasRemoved = false;
    TEST_CHECK_SUCCESS(run, World_RemoveData(TestWorld, Desk, Visits, &WasRemoved));
    TEST_CHECK(run, WasRemoved && !World_TryGetData(TestWorld, Desk, Visits, &Found) && (Found == NULL));
    TEST_CHECK(run, WorldObject_GetData(GetObject(TestWorld, Desk)) == NULL);
    TEST_CHECK_SUCCESS(run, World_RemoveData(TestWorld, Desk, Visits, &WasRemoved));
    TEST_CHECK(run, !WasRemoved);

    // Refusals: a key the registry does not know, an object that does not exist, an invalid value.
    DataValue Number = DataValue_FromInt(1);
    TEST_CHECK_ERROR(run, World_SetData(TestWorld, Desk, 999U, &Number), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, World_SetData(TestWorld, Desk, NAMESPACED_KEY_ID_NONE, &Number), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, World_SetData(TestWorld, 77U, Visits, &Number), ErrorCode_InvalidOperation);
    TEST_CHECK_ERROR(run, World_RemoveData(TestWorld, 77U, Visits, NULL), ErrorCode_InvalidOperation);
    DataValue NaN = DataValue_FromFloat(NAN);
    TEST_CHECK_ERROR(run, World_SetData(TestWorld, Desk, Visits, &NaN), ErrorCode_ArgumentOutOfRange);
    DataValue Unknown = DataValue_FromKey(999U);
    TEST_CHECK_ERROR(run, World_SetData(TestWorld, Desk, Visits, &Unknown), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, WorldObject_GetData(GetObject(TestWorld, Desk)) == NULL);
    Fixture_Deconstruct(run, &Fixture);
}

static void TagsAreEntriesWithoutAValue(TestRun* run)
{
    DataFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, NULL))
    {
        return;
    }
    World* TestWorld = &Fixture.World;
    uint64_t Lamp = AddObject(run, TestWorld, "lamp", (Vector3){ 0.0f, 0.0f, 0.0f });
    uint32_t StreetLamp = Key(run, &Fixture, "town:street_lamp");
    uint32_t Power = Key(run, &Fixture, "town:power");

    TEST_CHECK_SUCCESS(run, World_AddTag(TestWorld, Lamp, StreetLamp));
    TEST_CHECK(run, World_HasTag(TestWorld, Lamp, StreetLamp) && WorldObject_HasTag(GetObject(TestWorld, Lamp), StreetLamp));
    const DataValue* Found = NULL;
    TEST_CHECK(run, World_TryGetData(TestWorld, Lamp, StreetLamp, &Found) && (Found->Type == DataType_Tag));

    // A value under a key is not a tag: RemoveTag leaves it, AddTag replaces it.
    SetInt(run, TestWorld, Lamp, Power, 5);
    TEST_CHECK(run, !World_HasTag(TestWorld, Lamp, Power));
    bool WasRemoved = true;
    TEST_CHECK_SUCCESS(run, World_RemoveTag(TestWorld, Lamp, Power, &WasRemoved));
    TEST_CHECK(run, !WasRemoved && World_TryGetData(TestWorld, Lamp, Power, &Found));
    TEST_CHECK_SUCCESS(run, World_AddTag(TestWorld, Lamp, Power));
    TEST_CHECK(run, World_HasTag(TestWorld, Lamp, Power));
    // ... and a value replaces a tag.
    SetInt(run, TestWorld, Lamp, StreetLamp, 1);
    TEST_CHECK(run, !World_HasTag(TestWorld, Lamp, StreetLamp));

    TEST_CHECK_SUCCESS(run, World_RemoveTag(TestWorld, Lamp, Power, &WasRemoved));
    TEST_CHECK(run, WasRemoved && !World_HasTag(TestWorld, Lamp, Power));
    TEST_CHECK(run, DataMap_GetCount(WorldObject_GetData(GetObject(TestWorld, Lamp))) == 1U);
    TEST_CHECK(run, !World_HasTag(TestWorld, 77U, Power));
    Fixture_Deconstruct(run, &Fixture);
}

static void TheDataRevisionMovesAlone(TestRun* run)
{
    DataFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, NULL))
    {
        return;
    }
    World* TestWorld = &Fixture.World;
    uint64_t Desk = AddObject(run, TestWorld, "desk", (Vector3){ 0.0f, 0.0f, 0.0f });
    WorldObject* Object = GetObject(TestWorld, Desk);
    uint32_t Visits = Key(run, &Fixture, "town:visits");
    uint32_t Revision = WorldObject_GetRevision(Object);
    uint32_t DataRevision = WorldObject_GetDataRevision(Object);

    SetInt(run, TestWorld, Desk, Visits, 1);
    TEST_CHECK(run, (WorldObject_GetDataRevision(Object) == (DataRevision + 1U)) && (WorldObject_GetRevision(Object) == Revision));
    SetInt(run, TestWorld, Desk, Visits, 1); // the same value: nothing changes
    TEST_CHECK(run, WorldObject_GetDataRevision(Object) == (DataRevision + 1U));
    TEST_CHECK_SUCCESS(run, World_AddTag(TestWorld, Desk, Key(run, &Fixture, "town:office")));
    TEST_CHECK_SUCCESS(run, World_RemoveData(TestWorld, Desk, Visits, NULL));
    TEST_CHECK(run, (WorldObject_GetDataRevision(Object) == (DataRevision + 3U)) && (WorldObject_GetRevision(Object) == Revision));

    // A property change is the render revision's alone.
    TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, (Vector3){ 1.0f, 0.0f, 0.0f }));
    TEST_CHECK(run, (WorldObject_GetRevision(Object) == (Revision + 1U)) && (WorldObject_GetDataRevision(Object) == (DataRevision + 3U)));
    Fixture_Deconstruct(run, &Fixture);
}

static void ObjectLimitsAreRefusedAndLoggedOnce(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, DATA_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    Logger Log;
    if (!TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "object_data_limits", &Directory))
        || !TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Directory._data)))
    {
        Memory_Free(Directory._data);
        return;
    }
    DataFixture Fixture;
    if (Fixture_Construct(run, &Fixture, &Log))
    {
        World* TestWorld = &Fixture.World;
        uint64_t Crate = AddObject(run, TestWorld, "crate", (Vector3){ 0.0f, 0.0f, 0.0f });
        char Text[64];
        bool AreAllSet = true;
        for (size_t Index = 0U; Index < WORLD_OBJECT_DATA_MAX_ENTRIES; Index++)
        {
            snprintf(Text, sizeof(Text), "test:entry_%zu", Index);
            DataValue Value = DataValue_FromInt((int64_t)Index);
            Error Result = World_SetData(TestWorld, Crate, Key(run, &Fixture, Text), &Value);
            AreAllSet = AreAllSet && (Result.Code == ErrorCode_Success);
            Error_Deconstruct(&Result);
        }
        TEST_CHECK(run, AreAllSet);
        uint32_t Extra = Key(run, &Fixture, "test:one_too_many");
        TEST_CHECK_ERROR(run, World_AddTag(TestWorld, Crate, Extra), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, World_AddTag(TestWorld, Crate, Extra), ErrorCode_InvalidOperation); // logged once
        SetInt(run, TestWorld, Crate, Key(run, &Fixture, "test:entry_0"), 99); // replacing stays within the limit
        TEST_CHECK(run, DataMap_GetCount(WorldObject_GetData(GetObject(TestWorld, Crate))) == WORLD_OBJECT_DATA_MAX_ENTRIES);

        // Bytes: sixteen 4096-byte strings pass 65536 bytes (each entry also counts its key and a type byte).
        uint64_t Shelf = AddObject(run, TestWorld, "shelf", (Vector3){ 0.0f, 0.0f, 0.0f });
        DataValue Long = MakeLongString(run, DATA_VALUE_MAX_STRING_LENGTH);
        size_t Stored = 0U;
        for (size_t Index = 0U; Index < 16U; Index++)
        {
            snprintf(Text, sizeof(Text), "test:page_%zu", Index);
            Error Result = World_SetData(TestWorld, Shelf, Key(run, &Fixture, Text), &Long);
            Stored += (Result.Code == ErrorCode_Success) ? 1U : 0U;
            TEST_CHECK(run, (Result.Code == ErrorCode_Success) || (Result.Code == ErrorCode_InvalidOperation));
            Error_Deconstruct(&Result);
        }
        TEST_CHECK(run, Stored == 15U);
        TEST_CHECK(run, DataMap_GetByteSize(WorldObject_GetData(GetObject(TestWorld, Shelf))) <= WORLD_OBJECT_DATA_MAX_BYTES);
        TEST_CHECK(run, World_GetDataByteSize(TestWorld) == SumObjectBytes(TestWorld));
        DataValue_Deconstruct(&Long);
        Fixture_Deconstruct(run, &Fixture);
    }
    TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "cannot hold more than 256 data entries") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "cannot count more than 65536 bytes") == 1U);
    Memory_Free(Directory._data);
}

static void TheWorldLimitCountsEveryObject(TestRun* run)
{
    DataFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, NULL))
    {
        return;
    }
    World* TestWorld = &Fixture.World;
    DataValue Long = MakeLongString(run, DATA_VALUE_MAX_STRING_LENGTH);
    char Text[64];
    uint32_t Pages[15];
    for (size_t Index = 0U; Index < 15U; Index++)
    {
        snprintf(Text, sizeof(Text), "test:page_%zu", Index);
        Pages[Index] = Key(run, &Fixture, Text);
    }

    // Fill objects of about 61 KiB each until the world refuses.
    bool IsRefused = false;
    size_t ObjectCount = 0U;
    uint64_t Last = 0U;
    while (!IsRefused && (ObjectCount < 300U))
    {
        Last = AddObject(run, TestWorld, "box", (Vector3){ 0.0f, 0.0f, 0.0f });
        ObjectCount++;
        for (size_t Index = 0U; (Index < 15U) && !IsRefused; Index++)
        {
            Error Result = World_SetData(TestWorld, Last, Pages[Index], &Long);
            IsRefused = Result.Code == ErrorCode_InvalidOperation;
            TEST_CHECK(run, (Result.Code == ErrorCode_Success) || IsRefused);
            Error_Deconstruct(&Result);
        }
    }
    TEST_CHECK(run, IsRefused && (ObjectCount > 250U));
    TEST_CHECK(run, (World_GetDataByteSize(TestWorld) <= WORLD_DATA_MAX_BYTES)
        && (World_GetDataByteSize(TestWorld) > (WORLD_DATA_MAX_BYTES - WORLD_OBJECT_DATA_MAX_BYTES)));
    TEST_CHECK(run, World_GetDataByteSize(TestWorld) == SumObjectBytes(TestWorld));

    // Removing an object gives its bytes back, so the refused value now fits.
    WorldObject* First = NULL;
    TEST_CHECK_SUCCESS(run, World_GetObjectByIndex(TestWorld, 0U, &First));
    TEST_CHECK_SUCCESS(run, World_RemoveObject(TestWorld, WorldObject_GetId(First)));
    TEST_CHECK(run, World_GetDataByteSize(TestWorld) == SumObjectBytes(TestWorld));
    TEST_CHECK_SUCCESS(run, World_SetData(TestWorld, Last, Pages[14], &Long));
    DataValue_Deconstruct(&Long);
    Fixture_Deconstruct(run, &Fixture);
}


// Static functions: queries.
/* Five objects with tags, data, components, flags and places, for the filter tests. */
typedef struct QueryWorldStruct
{
    uint64_t Lamp;
    uint64_t Bench;
    uint64_t Desk;
    uint64_t Clock;
    uint64_t Ghost;
    uint32_t Town;
    uint32_t StreetLamp;
    uint32_t Office;
    uint32_t Power;
    uint32_t Opened;
} QueryWorld;

static void BuildQueryWorld(TestRun* run, DataFixture* fixture, QueryWorld* out)
{
    World* TestWorld = &fixture->World;
    out->Town = Key(run, fixture, "town:town");
    out->StreetLamp = Key(run, fixture, "town:street_lamp");
    out->Office = Key(run, fixture, "game:office");
    out->Power = Key(run, fixture, "town:power");
    out->Opened = Key(run, fixture, "town:opened");
    out->Lamp = AddObject(run, TestWorld, "lamp", (Vector3){ 10.0f, 0.0f, 0.0f });
    out->Bench = AddObject(run, TestWorld, "bench", (Vector3){ 2.0f, 0.0f, 0.0f });
    out->Desk = AddObject(run, TestWorld, "desk", (Vector3){ 0.0f, 0.0f, 3.0f });
    out->Clock = AddObject(run, TestWorld, "clock", (Vector3){ -4.0f, 5.0f, 0.0f });
    out->Ghost = AddObject(run, TestWorld, "ghost", (Vector3){ 1.0f, 0.0f, 0.0f });

    TEST_CHECK_SUCCESS(run, World_AddTag(TestWorld, out->Lamp, out->Town));
    TEST_CHECK_SUCCESS(run, World_AddTag(TestWorld, out->Lamp, out->StreetLamp));
    TEST_CHECK_SUCCESS(run, World_AddTag(TestWorld, out->Bench, out->Town));
    TEST_CHECK_SUCCESS(run, World_AddTag(TestWorld, out->Desk, out->Office));
    TEST_CHECK_SUCCESS(run, World_AddTag(TestWorld, out->Ghost, out->Town));
    SetInt(run, TestWorld, out->Lamp, out->Power, 5);
    DataValue Half = DataValue_FromFloat(2.5);
    TEST_CHECK_SUCCESS(run, World_SetData(TestWorld, out->Bench, out->Power, &Half));
    DataValue Name = MakeString(run, "high");
    TEST_CHECK_SUCCESS(run, World_SetData(TestWorld, out->Clock, out->Power, &Name));
    DataValue_Deconstruct(&Name);
    DataValue Morning = DataValue_FromDateTime(GAME_CALENDAR_MICROSECONDS_PER_DAY * 739906LL);
    DataValue Evening = DataValue_FromDateTime((GAME_CALENDAR_MICROSECONDS_PER_DAY * 739906LL)
        + (GAME_CALENDAR_MICROSECONDS_PER_SECOND * 72000LL));
    TEST_CHECK_SUCCESS(run, World_SetData(TestWorld, out->Desk, out->Opened, &Morning));
    TEST_CHECK_SUCCESS(run, World_SetData(TestWorld, out->Clock, out->Opened, &Evening));

    WorldComponent* Component = NULL;
    TEST_CHECK_SUCCESS(run, World_AddComponent(TestWorld, out->Lamp, WorldComponentType_Light, &Component));
    TEST_CHECK_SUCCESS(run, World_AddComponent(TestWorld, out->Lamp, WorldComponentType_Model, &Component));
    TEST_CHECK_SUCCESS(run, World_AddComponent(TestWorld, out->Desk, WorldComponentType_Model, &Component));
    TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(GetObject(TestWorld, out->Ghost), false));
    TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(GetObject(TestWorld, out->Bench), false));
}

static void QueriesFilterByEachField(TestRun* run)
{
    DataFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, NULL))
    {
        return;
    }
    QueryWorld Q;
    BuildQueryWorld(run, &Fixture, &Q);
    World* TestWorld = &Fixture.World;

    WorldQuery Query = WorldQuery_Create();
    const uint64_t Everything[] = { Q.Lamp, Q.Bench, Q.Desk, Q.Clock, Q.Ghost };
    CheckQuery(run, TestWorld, &Query, Everything, 5U);

    // Tags: all of, any of, none of.
    Query.AllOfTags = &Q.Town;
    Query.AllOfTagCount = 1U;
    const uint64_t InTown[] = { Q.Lamp, Q.Bench, Q.Ghost };
    CheckQuery(run, TestWorld, &Query, InTown, 3U);
    const uint32_t TownLamp[] = { Q.Town, Q.StreetLamp };
    Query.AllOfTags = TownLamp;
    Query.AllOfTagCount = 2U;
    CheckQuery(run, TestWorld, &Query, &Q.Lamp, 1U);
    Query = WorldQuery_Create();
    const uint32_t LampOrOffice[] = { Q.StreetLamp, Q.Office };
    Query.AnyOfTags = LampOrOffice;
    Query.AnyOfTagCount = 2U;
    const uint64_t LampAndDesk[] = { Q.Lamp, Q.Desk };
    CheckQuery(run, TestWorld, &Query, LampAndDesk, 2U);
    Query = WorldQuery_Create();
    Query.NoneOfTags = &Q.Town;
    Query.NoneOfTagCount = 1U;
    const uint64_t OutOfTown[] = { Q.Desk, Q.Clock };
    CheckQuery(run, TestWorld, &Query, OutOfTown, 2U);

    // Data present, and comparisons: numbers across int and float, date_times, other types never ordered.
    Query = WorldQuery_Create();
    Query.DataKeys = &Q.Power;
    Query.DataKeyCount = 1U;
    const uint64_t Powered[] = { Q.Lamp, Q.Bench, Q.Clock };
    CheckQuery(run, TestWorld, &Query, Powered, 3U);
    Query = WorldQuery_Create();
    WorldDataComparison Above = { .Key = Q.Power, .Operator = WorldDataOperator_Greater, .Value = DataValue_FromInt(3) };
    Query.Comparisons = &Above;
    Query.ComparisonCount = 1U;
    CheckQuery(run, TestWorld, &Query, &Q.Lamp, 1U);
    Above.Value = DataValue_FromFloat(2.0);
    const uint64_t AboveTwo[] = { Q.Lamp, Q.Bench };
    CheckQuery(run, TestWorld, &Query, AboveTwo, 2U);
    WorldDataComparison Equal = { .Key = Q.Power, .Operator = WorldDataOperator_Equal, .Value = DataValue_FromFloat(5.0) };
    Query.Comparisons = &Equal;
    CheckQuery(run, TestWorld, &Query, &Q.Lamp, 1U); // 5 equals 5.0
    Equal.Value = MakeString(run, "high");
    CheckQuery(run, TestWorld, &Query, &Q.Clock, 1U);
    Equal.Operator = WorldDataOperator_NotEqual;
    CheckQuery(run, TestWorld, &Query, AboveTwo, 2U); // the key must be there
    DataValue_Deconstruct(&Equal.Value);
    WorldDataComparison Before = { .Key = Q.Opened, .Operator = WorldDataOperator_LessOrEqual,
        .Value = DataValue_FromDateTime((GAME_CALENDAR_MICROSECONDS_PER_DAY * 739906LL) + 1) };
    Query.Comparisons = &Before;
    CheckQuery(run, TestWorld, &Query, &Q.Desk, 1U);

    // Components, flags.
    Query = WorldQuery_Create();
    const WorldComponentType Types[] = { WorldComponentType_Model, WorldComponentType_Light };
    Query.ComponentTypes = Types;
    Query.ComponentTypeCount = 1U;
    CheckQuery(run, TestWorld, &Query, LampAndDesk, 2U);
    Query.ComponentTypeCount = 2U;
    CheckQuery(run, TestWorld, &Query, &Q.Lamp, 1U);
    Query = WorldQuery_Create();
    Query.IsEnabledOnly = true;
    Query.IsVisibleOnly = true;
    const uint64_t Shown[] = { Q.Lamp, Q.Desk, Q.Clock };
    CheckQuery(run, TestWorld, &Query, Shown, 3U);

    // Places: a distance, a box (faces inside), then everything at once.
    Query = WorldQuery_Create();
    Query.HasMaximumDistance = true;
    Query.MaximumDistance = 3.0f;
    const uint64_t Near[] = { Q.Bench, Q.Desk, Q.Ghost };
    CheckQuery(run, TestWorld, &Query, Near, 3U);
    Query = WorldQuery_Create();
    Query.HasBox = true;
    Query.BoxMinimum = (Vector3){ 1.0f, -1.0f, -1.0f };
    Query.BoxMaximum = (Vector3){ 10.0f, 1.0f, 1.0f };
    const uint64_t InBox[] = { Q.Lamp, Q.Bench, Q.Ghost };
    CheckQuery(run, TestWorld, &Query, InBox, 3U);
    Query.AllOfTags = &Q.Town;
    Query.AllOfTagCount = 1U;
    Query.IsEnabledOnly = true;
    Query.ComponentTypes = Types;
    Query.ComponentTypeCount = 1U;
    CheckQuery(run, TestWorld, &Query, &Q.Lamp, 1U);

    bool IsMatch = false;
    TEST_CHECK(run, TEST_CHECK_SUCCESS(run, World_DoesObjectMatch(TestWorld, &Query, Q.Lamp, &IsMatch)) && IsMatch);
    TEST_CHECK(run, TEST_CHECK_SUCCESS(run, World_DoesObjectMatch(TestWorld, &Query, Q.Ghost, &IsMatch)) && !IsMatch);
    TEST_CHECK(run, TEST_CHECK_SUCCESS(run, World_DoesObjectMatch(TestWorld, &Query, 999U, &IsMatch)) && !IsMatch);
    Fixture_Deconstruct(run, &Fixture);
}

static void QueriesSortByDistanceAndLimit(TestRun* run)
{
    DataFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, NULL))
    {
        return;
    }
    QueryWorld Q;
    BuildQueryWorld(run, &Fixture, &Q);
    uint64_t Twin = AddObject(run, &Fixture.World, "twin", (Vector3){ -1.0f, 0.0f, 0.0f }); // as near as the ghost
    World* TestWorld = &Fixture.World;

    WorldQuery Query = WorldQuery_Create();
    Query.IsSortedByDistance = true;
    const uint64_t Nearest[] = { Q.Ghost, Twin, Q.Bench, Q.Desk, Q.Clock, Q.Lamp };
    CheckQuery(run, TestWorld, &Query, Nearest, 6U);
    Query.Limit = 2U;
    CheckQuery(run, TestWorld, &Query, Nearest, 2U);
    Query.Origin = (Vector3){ 10.0f, 0.0f, 0.0f };
    Query.Limit = 1U;
    CheckQuery(run, TestWorld, &Query, &Q.Lamp, 1U);

    // Without the sort: ids ascending, the first few; the tag index gives the same order.
    Query = WorldQuery_Create();
    Query.Limit = 2U;
    const uint64_t First[] = { Q.Lamp, Q.Bench };
    CheckQuery(run, TestWorld, &Query, First, 2U);
    Query.AnyOfTags = &Q.Town;
    Query.AnyOfTagCount = 1U;
    CheckQuery(run, TestWorld, &Query, First, 2U);

    // The results are appended.
    GenericBuffer Ids;
    GenericBuffer_AllocateVariable(&Ids, 0U, sizeof(uint64_t));
    uint64_t Marker = 77U;
    GenericBuffer_AddLast(&Ids, &Marker);
    TEST_CHECK_SUCCESS(run, World_Query(TestWorld, &Query, &Ids));
    TEST_CHECK(run, (Ids._count == 3U) && (((const uint64_t*)Ids._data)[0] == 77U));
    Memory_Free(Ids._data);
    Fixture_Deconstruct(run, &Fixture);
}

static void MalformedQueriesAreRefused(TestRun* run)
{
    DataFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, NULL))
    {
        return;
    }
    World* TestWorld = &Fixture.World;
    uint32_t Power = Key(run, &Fixture, "town:power");
    GenericBuffer Ids;
    GenericBuffer_AllocateVariable(&Ids, 0U, sizeof(uint64_t));

    WorldQuery Query = WorldQuery_Create();
    Query.AllOfTagCount = 1U; // NULL with a count
    TEST_CHECK_ERROR(run, World_Query(TestWorld, &Query, &Ids), ErrorCode_IllegalArgument);
    const uint32_t None = NAMESPACED_KEY_ID_NONE;
    Query = WorldQuery_Create();
    Query.NoneOfTags = &None;
    Query.NoneOfTagCount = 1U;
    TEST_CHECK_ERROR(run, World_Query(TestWorld, &Query, &Ids), ErrorCode_IllegalArgument);
    Query = WorldQuery_Create();
    DataValue Text = MakeString(run, "x");
    WorldDataComparison Ordered = { .Key = Power, .Operator = WorldDataOperator_Less, .Value = Text };
    Query.Comparisons = &Ordered;
    Query.ComparisonCount = 1U;
    TEST_CHECK_ERROR(run, World_Query(TestWorld, &Query, &Ids), ErrorCode_IllegalArgument);
    Ordered.Value = DataValue_FromFloat(NAN);
    TEST_CHECK_ERROR(run, World_Query(TestWorld, &Query, &Ids), ErrorCode_IllegalArgument);
    Ordered.Value = DataValue_FromInt(1);
    Ordered.Operator = (WorldDataOperator)9;
    TEST_CHECK_ERROR(run, World_Query(TestWorld, &Query, &Ids), ErrorCode_IllegalArgument);
    DataValue_Deconstruct(&Text);

    Query = WorldQuery_Create();
    const WorldComponentType Unknown = (WorldComponentType)12;
    Query.ComponentTypes = &Unknown;
    Query.ComponentTypeCount = 1U;
    TEST_CHECK_ERROR(run, World_Query(TestWorld, &Query, &Ids), ErrorCode_IllegalArgument);
    Query = WorldQuery_Create();
    Query.Origin = (Vector3){ NAN, 0.0f, 0.0f };
    TEST_CHECK_ERROR(run, World_Query(TestWorld, &Query, &Ids), ErrorCode_IllegalArgument);
    Query = WorldQuery_Create();
    Query.HasMaximumDistance = true;
    Query.MaximumDistance = -1.0f;
    TEST_CHECK_ERROR(run, World_Query(TestWorld, &Query, &Ids), ErrorCode_IllegalArgument);
    Query = WorldQuery_Create();
    Query.HasBox = true;
    Query.BoxMinimum = (Vector3){ 1.0f, 0.0f, 0.0f };
    TEST_CHECK_ERROR(run, World_Query(TestWorld, &Query, &Ids), ErrorCode_IllegalArgument);

    GenericBuffer Wrong;
    GenericBuffer_AllocateVariable(&Wrong, 0U, sizeof(uint32_t));
    Query = WorldQuery_Create();
    TEST_CHECK_ERROR(run, World_Query(TestWorld, &Query, &Wrong), ErrorCode_IllegalArgument);
    TEST_CHECK(run, Ids._count == 0U);
    Memory_Free(Wrong._data);
    Memory_Free(Ids._data);
    Fixture_Deconstruct(run, &Fixture);
}

/* Checks that the objects carrying @p tag are exactly @p expected (ascending). */
static void CheckTagged(TestRun* run, World* world, uint32_t tag, const uint64_t* expected, size_t count)
{
    WorldQuery Query = WorldQuery_Create();
    Query.AllOfTags = &tag;
    Query.AllOfTagCount = 1U;
    CheckQuery(run, world, &Query, expected, count);
}

static void TheTagIndexFollowsEveryChange(TestRun* run)
{
    DataFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, NULL))
    {
        return;
    }
    World* TestWorld = &Fixture.World;
    uint32_t Lamp = Key(run, &Fixture, "town:street_lamp");
    uint64_t A = AddObject(run, TestWorld, "a", (Vector3){ 0.0f, 0.0f, 0.0f });
    uint64_t B = AddObject(run, TestWorld, "b", (Vector3){ 0.0f, 0.0f, 0.0f });
    uint64_t C = AddObject(run, TestWorld, "c", (Vector3){ 0.0f, 0.0f, 0.0f });

    TEST_CHECK_SUCCESS(run, World_AddTag(TestWorld, C, Lamp));
    TEST_CHECK_SUCCESS(run, World_AddTag(TestWorld, A, Lamp));
    TEST_CHECK_SUCCESS(run, World_AddTag(TestWorld, A, Lamp)); // twice: still once
    const uint64_t AC[] = { A, C };
    CheckTagged(run, TestWorld, Lamp, AC, 2U);
    SetInt(run, TestWorld, A, Lamp, 1); // a value under the key: no longer a tag
    CheckTagged(run, TestWorld, Lamp, &C, 1U);
    TEST_CHECK_SUCCESS(run, World_AddTag(TestWorld, B, Lamp));
    TEST_CHECK_SUCCESS(run, World_RemoveTag(TestWorld, C, Lamp, NULL));
    CheckTagged(run, TestWorld, Lamp, &B, 1U);
    TEST_CHECK_SUCCESS(run, World_AddTag(TestWorld, C, Lamp));
    TEST_CHECK_SUCCESS(run, World_RemoveObject(TestWorld, B));
    CheckTagged(run, TestWorld, Lamp, &C, 1U);
    TEST_CHECK_SUCCESS(run, World_RemoveObject(TestWorld, C));
    CheckTagged(run, TestWorld, Lamp, NULL, 0U);

    // Table writes replace the whole data behind the world's back: copies and reads re-index.
    TEST_CHECK_SUCCESS(run, World_AddTag(TestWorld, A, Lamp));
    uint64_t D = AddObject(run, TestWorld, "d", (Vector3){ 0.0f, 0.0f, 0.0f });
    const PropertyTable* Table = WorldObject_GetPropertyTable();
    TEST_CHECK_SUCCESS(run, Properties_CopyTo(Table, GetObject(TestWorld, A), GetObject(TestWorld, D)));
    const uint64_t AD[] = { A, D };
    CheckTagged(run, TestWorld, Lamp, AD, 2U);
    TEST_CHECK(run, World_GetDataByteSize(TestWorld) == SumObjectBytes(TestWorld));
    JSONObjectPool* Pool = NULL;
    if (TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        JSONObjectValue JSON;
        if (TestText_ParseJSON(run, Pool, "{\"name\": \"a\"}", &JSON))
        {
            PropertyReadOptions Options = PropertyReadOptions_CreateDefault();
            Options.Keys = &Fixture.Keys;
            TEST_CHECK_SUCCESS(run, Properties_ReadJSON(Table, JSON.Value.Compound, GetObject(TestWorld, A), &Options));
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &JSON));
        }
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    }
    CheckTagged(run, TestWorld, Lamp, &D, 1U); // the read gave A its default: no data
    TEST_CHECK(run, (WorldObject_GetData(GetObject(TestWorld, A)) == NULL)
        && (World_GetDataByteSize(TestWorld) == SumObjectBytes(TestWorld)));
    Fixture_Deconstruct(run, &Fixture);
}


// Static functions: the brute-force comparison.
/* An object's own flag and its ancestors', walked here by hand. */
static bool AreAllEnabled(const WorldObject* object)
{
    return (object == NULL) || (WorldObject_IsEnabled(object) && AreAllEnabled(WorldObject_GetParent(object)));
}

static bool AreAllVisible(const WorldObject* object)
{
    return (object == NULL) || (WorldObject_IsVisible(object) && AreAllVisible(WorldObject_GetParent(object)));
}

static bool OracleMatches(const WorldObject* object, const WorldQuery* query)
{
    if ((query->IsEnabledOnly && !AreAllEnabled(object)) || (query->IsVisibleOnly && !AreAllVisible(object)))
    {
        return false;
    }
    bool IsMatch = true;
    for (size_t Index = 0U; Index < query->ComponentTypeCount; Index++)
    {
        IsMatch = IsMatch && (WorldObject_GetComponent(object, query->ComponentTypes[Index]) != NULL);
    }
    for (size_t Index = 0U; Index < query->AllOfTagCount; Index++)
    {
        IsMatch = IsMatch && WorldObject_HasTag(object, query->AllOfTags[Index]);
    }
    for (size_t Index = 0U; Index < query->NoneOfTagCount; Index++)
    {
        IsMatch = IsMatch && !WorldObject_HasTag(object, query->NoneOfTags[Index]);
    }
    bool HasAny = query->AnyOfTagCount == 0U;
    for (size_t Index = 0U; Index < query->AnyOfTagCount; Index++)
    {
        HasAny = HasAny || WorldObject_HasTag(object, query->AnyOfTags[Index]);
    }
    for (size_t Index = 0U; Index < query->DataKeyCount; Index++)
    {
        IsMatch = IsMatch && (DataMap_Find(WorldObject_GetData(object), query->DataKeys[Index]) != NULL);
    }
    for (size_t Index = 0U; Index < query->ComparisonCount; Index++)
    {
        // The random data compares numbers only: small ones, exact as doubles.
        const WorldDataComparison* Comparison = &query->Comparisons[Index];
        const DataValue* Value = DataMap_Find(WorldObject_GetData(object), Comparison->Key);
        bool IsNumber = (Value != NULL) && ((Value->Type == DataType_Int) || (Value->Type == DataType_Float));
        double Left = !IsNumber ? 0.0 : ((Value->Type == DataType_Int) ? (double)Value->IntValue : Value->FloatValue);
        double Right = (Comparison->Value.Type == DataType_Int) ? (double)Comparison->Value.IntValue
            : Comparison->Value.FloatValue;
        bool Holds = false;
        switch (Comparison->Operator)
        {
            case WorldDataOperator_Equal: Holds = IsNumber && (Left == Right); break;
            case WorldDataOperator_NotEqual: Holds = (Value != NULL) && (!IsNumber || (Left != Right)); break;
            case WorldDataOperator_Less: Holds = IsNumber && (Left < Right); break;
            case WorldDataOperator_LessOrEqual: Holds = IsNumber && (Left <= Right); break;
            case WorldDataOperator_Greater: Holds = IsNumber && (Left > Right); break;
            case WorldDataOperator_GreaterOrEqual: Holds = IsNumber && (Left >= Right); break;
        }
        IsMatch = IsMatch && Holds;
    }
    Matrix Placement = WorldObject_GetWorldMatrix(object);
    Vector3 Position = { Placement.m12, Placement.m13, Placement.m14 };
    float X = Position.x - query->Origin.x;
    float Y = Position.y - query->Origin.y;
    float Z = Position.z - query->Origin.z;
    if (query->HasMaximumDistance)
    {
        IsMatch = IsMatch && (((X * X) + (Y * Y) + (Z * Z)) <= (query->MaximumDistance * query->MaximumDistance));
    }
    if (query->HasBox)
    {
        IsMatch = IsMatch && (Position.x >= query->BoxMinimum.x) && (Position.x <= query->BoxMaximum.x)
            && (Position.y >= query->BoxMinimum.y) && (Position.y <= query->BoxMaximum.y)
            && (Position.z >= query->BoxMinimum.z) && (Position.z <= query->BoxMaximum.z);
    }
    return IsMatch && HasAny;
}

/* Every object, filtered, ordered by a plain insertion sort, cut to the limit. */
static void RunOracle(World* world, const WorldQuery* query, GenericBuffer* outIds)
{
    OracleMatch Matches[RANDOM_OBJECT_COUNT];
    size_t Count = 0U;
    for (size_t Index = 0U; Index < World_GetObjectCount(world); Index++)
    {
        WorldObject* Object = NULL;
        World_GetObjectByIndex(world, Index, &Object);
        if (!OracleMatches(Object, query))
        {
            continue;
        }
        Matrix Placement = WorldObject_GetWorldMatrix(Object);
        Vector3 Position = { Placement.m12, Placement.m13, Placement.m14 };
        float X = Position.x - query->Origin.x;
        float Y = Position.y - query->Origin.y;
        float Z = Position.z - query->Origin.z;
        OracleMatch Match = { .Id = WorldObject_GetId(Object),
            .DistanceSquared = query->IsSortedByDistance ? ((X * X) + (Y * Y) + (Z * Z)) : 0.0f };
        size_t Slot = Count++;
        while ((Slot > 0U) && ((Matches[Slot - 1U].DistanceSquared > Match.DistanceSquared)
            || ((Matches[Slot - 1U].DistanceSquared == Match.DistanceSquared) && (Matches[Slot - 1U].Id > Match.Id))))
        {
            Matches[Slot] = Matches[Slot - 1U];
            Slot--;
        }
        Matches[Slot] = Match;
    }
    size_t Kept = ((query->Limit > 0U) && (query->Limit < Count)) ? query->Limit : Count;
    for (size_t Index = 0U; Index < Kept; Index++)
    {
        GenericBuffer_AddLast(outIds, &Matches[Index].Id);
    }
}

/* Picks up to RANDOM_FILTER_MAX distinct tags. */
static size_t PickTags(Random* random, const uint32_t* tags, uint32_t* outTags)
{
    size_t Count = (size_t)Random_NextInt32InLimit(random, (int32_t)RANDOM_FILTER_MAX + 1);
    size_t Picked = 0U;
    for (size_t Attempt = 0U; Attempt < Count; Attempt++)
    {
        uint32_t Tag = tags[Random_NextInt32InLimit(random, (int32_t)RANDOM_TAG_COUNT)];
        bool IsNew = true;
        for (size_t Index = 0U; Index < Picked; Index++)
        {
            IsNew = IsNew && (outTags[Index] != Tag);
        }
        if (IsNew)
        {
            outTags[Picked++] = Tag;
        }
    }
    return Picked;
}

static float RandomCoordinate(Random* random)
{
    return (float)Random_NextInt32InRange(random, -500, 501) / 10.0f;
}

static void BuildRandomWorld(TestRun* run, Random* random, DataFixture* fixture, const uint32_t* tags, uint32_t power,
    uint32_t rate, uint32_t label)
{
    World* TestWorld = &fixture->World;
    for (size_t Index = 0U; Index < RANDOM_OBJECT_COUNT; Index++)
    {
        uint64_t Id = AddObject(run, TestWorld, "random", (Vector3){ RandomCoordinate(random), RandomCoordinate(random),
            RandomCoordinate(random) });
        for (size_t Tag = 0U; Tag < RANDOM_TAG_COUNT; Tag++)
        {
            if (Random_NextInt32InLimit(random, 10) < 3)
            {
                TEST_CHECK_SUCCESS(run, World_AddTag(TestWorld, Id, tags[Tag]));
            }
        }
        if (Random_NextInt32InLimit(random, 10) < 7)
        {
            SetInt(run, TestWorld, Id, power, Random_NextInt32InRange(random, -20, 21));
        }
        if (Random_NextBool(random))
        {
            DataValue Rate = DataValue_FromFloat((double)Random_NextInt32InRange(random, -80, 81) / 4.0);
            TEST_CHECK_SUCCESS(run, World_SetData(TestWorld, Id, rate, &Rate));
        }
        if (Random_NextInt32InLimit(random, 10) < 2)
        {
            DataValue Label = MakeString(run, "label");
            TEST_CHECK_SUCCESS(run, World_SetData(TestWorld, Id, (Random_NextBool(random) ? label : power), &Label));
            DataValue_Deconstruct(&Label);
        }
        WorldComponent* Component = NULL;
        if (Random_NextInt32InLimit(random, 10) < 3)
        {
            TEST_CHECK_SUCCESS(run, World_AddComponent(TestWorld, Id, WorldComponentType_Model, &Component));
        }
        if (Random_NextInt32InLimit(random, 10) < 3)
        {
            TEST_CHECK_SUCCESS(run, World_AddComponent(TestWorld, Id, WorldComponentType_Light, &Component));
        }
        WorldObject* Object = GetObject(TestWorld, Id);
        TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(Object, Random_NextInt32InLimit(random, 10) < 9));
        TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(Object, Random_NextInt32InLimit(random, 10) < 9));

        // A third of the objects sit on an earlier one (turned and scaled), so places and flags come through parents.
        if ((Index > 0U) && (Random_NextInt32InLimit(random, 3) == 0))
        {
            WorldObject* Parent = NULL;
            size_t At = (size_t)Random_NextInt32InLimit(random, (int32_t)World_GetObjectCount(TestWorld) - 1);
            if (TEST_CHECK_SUCCESS(run, World_GetObjectByIndex(TestWorld, At, &Parent)))
            {
                TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(Parent, (Vector3){ 0.0f,
                    (float)Random_NextInt32InLimit(random, 628) / 100.0f, 0.0f }));
                float Scale = (float)Random_NextInt32InRange(random, 5, 21) / 10.0f;
                TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Parent, (Vector3){ Scale, Scale, Scale }));
                // Never refused: parents picked among earlier objects stay far below the depth limit.
                TEST_CHECK_SUCCESS(run, World_SetParent(TestWorld, Id, WorldObject_GetId(Parent), false));
            }
        }
    }
    // Some places are computed on the fly, the rest cached.
    World_UpdateTransforms(TestWorld);
    for (size_t Index = 0U; Index < RANDOM_TAG_COUNT; Index++)
    {
        WorldObject* Object = NULL;
        if (TEST_CHECK_SUCCESS(run, World_GetObjectByIndex(TestWorld, Index * 7U, &Object)))
        {
            TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, (Vector3){ RandomCoordinate(random),
                RandomCoordinate(random), RandomCoordinate(random) }));
        }
    }

    // Shake the index: remove some objects, and drop or overwrite some tags.
    for (size_t Index = 0U; Index < RANDOM_REMOVED_COUNT; Index++)
    {
        WorldObject* Object = NULL;
        size_t At = (size_t)Random_NextInt32InLimit(random, (int32_t)World_GetObjectCount(TestWorld));
        if (TEST_CHECK_SUCCESS(run, World_GetObjectByIndex(TestWorld, At, &Object)))
        {
            uint64_t Id = WorldObject_GetId(Object);
            TEST_CHECK_SUCCESS(run, World_RemoveTag(TestWorld, Id, tags[Index % RANDOM_TAG_COUNT], NULL));
            SetInt(run, TestWorld, Id, tags[(Index + 1U) % RANDOM_TAG_COUNT], 1);
            if ((Index % 2U) == 0U)
            {
                TEST_CHECK_SUCCESS(run, World_RemoveObject(TestWorld, Id));
            }
        }
    }
}

static void QueriesEqualABruteForceScan(TestRun* run)
{
    DataFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, NULL))
    {
        return;
    }
    RandomState Seed = { .Values = { 0x5EED0024U, 0x7A65U, 0x1234ABCDU, 0x42U } };
    Random Generator;
    Random_Construct2(&Generator, Seed);
    uint32_t Tags[RANDOM_TAG_COUNT];
    char Text[32];
    for (size_t Index = 0U; Index < RANDOM_TAG_COUNT; Index++)
    {
        snprintf(Text, sizeof(Text), "test:tag_%zu", Index);
        Tags[Index] = Key(run, &Fixture, Text);
    }
    uint32_t Power = Key(run, &Fixture, "test:power");
    uint32_t Rate = Key(run, &Fixture, "test:rate");
    uint32_t Label = Key(run, &Fixture, "test:label");
    BuildRandomWorld(run, &Generator, &Fixture, Tags, Power, Rate, Label);
    TEST_CHECK(run, World_GetDataByteSize(&Fixture.World) == SumObjectBytes(&Fixture.World));

    GenericBuffer Expected;
    GenericBuffer Actual;
    GenericBuffer_AllocateVariable(&Expected, 0U, sizeof(uint64_t));
    GenericBuffer_AllocateVariable(&Actual, 0U, sizeof(uint64_t));
    size_t Agreements = 0U;
    size_t NonEmpty = 0U;
    const uint32_t DataKeys[] = { Power, Rate, Label };
    const WorldComponentType Types[] = { WorldComponentType_Model, WorldComponentType_Light };
    for (size_t Round = 0U; Round < RANDOM_QUERY_COUNT; Round++)
    {
        uint32_t AllOf[RANDOM_FILTER_MAX];
        uint32_t AnyOf[RANDOM_FILTER_MAX];
        uint32_t NoneOf[RANDOM_FILTER_MAX];
        WorldQuery Query = WorldQuery_Create();
        Query.AllOfTags = AllOf;
        Query.AllOfTagCount = (Random_NextInt32InLimit(&Generator, 3) == 0) ? PickTags(&Generator, Tags, AllOf) : 0U;
        Query.AnyOfTags = AnyOf;
        Query.AnyOfTagCount = (Random_NextInt32InLimit(&Generator, 3) == 0) ? PickTags(&Generator, Tags, AnyOf) : 0U;
        Query.NoneOfTags = NoneOf;
        Query.NoneOfTagCount = (Random_NextInt32InLimit(&Generator, 3) == 0) ? PickTags(&Generator, Tags, NoneOf) : 0U;
        Query.DataKeys = &DataKeys[Random_NextInt32InLimit(&Generator, 3)];
        Query.DataKeyCount = (Random_NextInt32InLimit(&Generator, 4) == 0) ? 1U : 0U;
        WorldDataComparison Comparison = { .Key = Random_NextBool(&Generator) ? Power : Rate,
            .Operator = (WorldDataOperator)Random_NextInt32InLimit(&Generator, 6),
            .Value = Random_NextBool(&Generator) ? DataValue_FromInt(Random_NextInt32InRange(&Generator, -20, 21))
                : DataValue_FromFloat((double)Random_NextInt32InRange(&Generator, -80, 81) / 4.0) };
        Query.Comparisons = &Comparison;
        Query.ComparisonCount = (Random_NextInt32InLimit(&Generator, 3) == 0) ? 1U : 0U;
        Query.ComponentTypes = &Types[Random_NextInt32InLimit(&Generator, 2)];
        Query.ComponentTypeCount = (Random_NextInt32InLimit(&Generator, 4) == 0) ? 1U : 0U;
        Query.IsEnabledOnly = Random_NextBool(&Generator);
        Query.IsVisibleOnly = Random_NextInt32InLimit(&Generator, 4) == 0;
        Query.Origin = (Vector3){ RandomCoordinate(&Generator), RandomCoordinate(&Generator), RandomCoordinate(&Generator) };
        Query.HasMaximumDistance = Random_NextInt32InLimit(&Generator, 3) == 0;
        Query.MaximumDistance = (float)Random_NextInt32InLimit(&Generator, 80);
        Query.HasBox = Random_NextInt32InLimit(&Generator, 5) == 0;
        Vector3 Corner = { RandomCoordinate(&Generator), RandomCoordinate(&Generator), RandomCoordinate(&Generator) };
        Query.BoxMinimum = (Vector3){ Corner.x - 30.0f, Corner.y - 30.0f, Corner.z - 30.0f };
        Query.BoxMaximum = (Vector3){ Corner.x + 30.0f, Corner.y + 30.0f, Corner.z + 30.0f };
        Query.IsSortedByDistance = Random_NextInt32InLimit(&Generator, 3) == 0;
        Query.Limit = Random_NextBool(&Generator) ? 0U : (size_t)Random_NextInt32InRange(&Generator, 1, 20);

        GenericBuffer_Clear(&Expected);
        GenericBuffer_Clear(&Actual);
        RunOracle(&Fixture.World, &Query, &Expected);
        Error Result = World_Query(&Fixture.World, &Query, &Actual);
        bool IsSame = (Result.Code == ErrorCode_Success) && (Expected._count == Actual._count)
            && ((Expected._count == 0U) || Memory_IsEqual(Expected._data, Actual._data, Expected._count * sizeof(uint64_t)));
        Error_Deconstruct(&Result);
        Agreements += IsSame ? 1U : 0U;
        NonEmpty += (Expected._count > 0U) ? 1U : 0U;
        if (!IsSame)
        {
            printf("    query %zu differs: %zu expected, %zu found\n", Round, Expected._count, Actual._count);
        }
    }
    TEST_CHECK(run, Agreements == RANDOM_QUERY_COUNT);
    TEST_CHECK(run, NonEmpty > (RANDOM_QUERY_COUNT / 4U)); // the queries are not all trivially empty
    Memory_Free(Expected._data);
    Memory_Free(Actual._data);
    Random_Deconstruct1(&Generator);
    Fixture_Deconstruct(run, &Fixture);
}


// Static functions: saving through the object table.
/* Gives @p id tags and data of every type. */
static void FillObjectData(TestRun* run, DataFixture* fixture, uint64_t id)
{
    World* TestWorld = &fixture->World;
    TEST_CHECK_SUCCESS(run, World_AddTag(TestWorld, id, Key(run, fixture, "town:street_lamp")));
    TEST_CHECK_SUCCESS(run, World_AddTag(TestWorld, id, Key(run, fixture, "game:office")));
    SetInt(run, TestWorld, id, Key(run, fixture, "town:visits"), 3);
    DataValue Values[] =
    {
        MakeString(run, "Bob"), DataValue_FromFloat(1.5), DataValue_FromFloat(2.0), DataValue_FromBool(true),
        DataValue_FromKey(Key(run, fixture, "town:desk")), DataValue_FromObject(id),
        DataValue_FromVector3((Vector3){ 1.0f, 2.0f, 3.0f }), DataValue_FromColor((Color){ 255, 204, 0, 255 }),
        DataValue_FromDateTime(GAME_CALENDAR_MICROSECONDS_PER_DAY * 739906LL), DataValue_CreateTag(),
        DataValue_CreateTag(),
    };
    TEST_CHECK_SUCCESS(run, DataValue_CreateList(DataType_Int, &Values[9]));
    DataValue Element = DataValue_FromInt(7);
    TEST_CHECK_SUCCESS(run, DataValue_ListAdd(&Values[9], &Element));
    TEST_CHECK_SUCCESS(run, DataValue_CreateCompound(&Values[10]));
    DataValue Inner = MakeString(run, "inner");
    TEST_CHECK_SUCCESS(run, DataValue_CompoundSet(&Values[10], (const unsigned char*)u8"note", &Inner));
    const char* const Names[] = { "town:owner", "town:rate", "town:whole", "town:open", "town:kind", "town:self",
        "town:spawn", "town:shade", "town:opened", "town:list", "town:record" };
    for (size_t Index = 0U; Index < (sizeof(Names) / sizeof(Names[0])); Index++)
    {
        TEST_CHECK_SUCCESS(run, World_SetData(TestWorld, id, Key(run, fixture, Names[Index]), &Values[Index]));
        DataValue_Deconstruct(&Values[Index]);
    }
}

/* Writes an object with the table, as JSON text. */
static bool WriteObjectJSON(TestRun* run, WorldObject* object, GenericBuffer* outText)
{
    JSONObjectPool* Pool = NULL;
    JSONCompound* Compound = NULL;
    bool IsWritten = TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool))
        && TEST_CHECK_SUCCESS(run, JSONObjectPool_BorrowCompound(Pool, &Compound))
        && TEST_CHECK_SUCCESS(run, Properties_WriteJSON(WorldObject_GetPropertyTable(), object, Pool, Compound, NULL));
    if (IsWritten)
    {
        JSONObjectValue Value = JSONObjectValue_CreateCompound(Compound);
        IsWritten = TEST_CHECK_SUCCESS(run, JSON_Serialize(&Value, outText, JSONSerializeFlags_NullTerminated));
    }
    if (Compound != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnCompound(Pool, Compound, true));
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    }
    return IsWritten;
}

/* Reads JSON text into an object with the table (the read's error is returned). */
static Error ReadObjectJSON(TestRun* run, WorldObject* object, KeyRegistry* keys, const char* text,
    const unsigned char* defaultNamespace)
{
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return Error_Construct2(ErrorCode_InvalidState, "the test's pool could not be created");
    }
    Error Result = Error_Construct2(ErrorCode_InvalidJSON, "the test's JSON did not parse");
    JSONObjectValue JSON;
    if (TestText_ParseJSON(run, Pool, text, &JSON))
    {
        Error_Deconstruct(&Result);
        PropertyReadOptions Options = PropertyReadOptions_CreateDefault();
        Options.Keys = keys;
        Options.DefaultNamespace = defaultNamespace;
        Result = Properties_ReadJSON(WorldObject_GetPropertyTable(), JSON.Value.Compound, object, &Options);
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &JSON));
    }
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    return Result;
}

static void ObjectTablesRoundTripTagsAndData(TestRun* run)
{
    DataFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, NULL))
    {
        return;
    }
    World* TestWorld = &Fixture.World;
    uint64_t Lamp = AddObject(run, TestWorld, "lamp", (Vector3){ 0.0f, 0.0f, 0.0f });
    FillObjectData(run, &Fixture, Lamp);
    WorldObject* Source = GetObject(TestWorld, Lamp);

    // JSON: the tags as a sorted list, the values in their forms, and back into an object of another world.
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, DATA_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    World Other;
    if (WriteObjectJSON(run, Source, &Text) && TEST_CHECK_SUCCESS(run, World_Construct1(&Other, &Fixture.Keys)))
    {
        const char* Written = (const char*)Text._data;
        TEST_CHECK(run, strstr(Written, "\"tags\":[\"game:office\",\"town:street_lamp\"]") != NULL);
        TEST_CHECK(run, strstr(Written, "\"town:visits\":3") != NULL);
        TEST_CHECK(run, strstr(Written, "\"town:whole\":2.0") != NULL);
        TEST_CHECK(run, strstr(Written, "\"town:kind\":{\"$key\":\"town:desk\"}") != NULL);
        TEST_CHECK(run, strstr(Written, "\"town:list\":[7]") != NULL);

        WorldObject* Copy = NULL;
        TEST_CHECK_SUCCESS(run, World_CreateObject(&Other, NULL, &Copy));
        uint32_t DataRevision = WorldObject_GetDataRevision(Copy);
        if (TEST_CHECK_SUCCESS(run, ReadObjectJSON(run, Copy, &Fixture.Keys, Written, NULL)))
        {
            TEST_CHECK(run, DataMap_Equals(WorldObject_GetData(Copy), WorldObject_GetData(Source)));
            TEST_CHECK(run, WorldObject_GetDataRevision(Copy) != DataRevision);
            uint32_t StreetLamp = Key(run, &Fixture, "town:street_lamp");
            uint64_t CopyId = WorldObject_GetId(Copy);
            CheckTagged(run, &Other, StreetLamp, &CopyId, 1U);
            TEST_CHECK(run, World_GetDataByteSize(&Other) == World_GetDataByteSize(TestWorld));
        }
        TEST_CHECK_SUCCESS(run, World_Deconstruct(&Other));
    }

    // GHDF, through bytes.
    GHDFObjectPool* Pool = NULL;
    GHDFCompound* Compound = NULL;
    MemoryStream Stream;
    if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Pool)) && TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Pool, &Compound))
        && TEST_CHECK_SUCCESS(run, Properties_WriteGHDF(WorldObject_GetPropertyTable(), Source, Pool, Compound, NULL))
        && TEST_CHECK_SUCCESS(run, MemoryStream_Construct1(&Stream, (IOStreamFlags)(IOStreamFlags_CanRead | IOStreamFlags_CanWrite))))
    {
        IOStream* StreamInterface = MemoryStream_AsIOStream(&Stream);
        GHDFCompound* ReadBack = NULL;
        uint64_t Bench = AddObject(run, TestWorld, "bench", (Vector3){ 0.0f, 0.0f, 0.0f });
        PropertyReadOptions Options = PropertyReadOptions_CreateDefault();
        Options.Keys = &Fixture.Keys;
        if (TEST_CHECK_SUCCESS(run, GHDF_Write(Compound, StreamInterface))
            && TEST_CHECK_SUCCESS(run, IOStream_SetPosition(StreamInterface, 0U))
            && TEST_CHECK_SUCCESS(run, GHDF_Read(StreamInterface, Pool, &ReadBack))
            && TEST_CHECK_SUCCESS(run, Properties_ReadGHDF(WorldObject_GetPropertyTable(), ReadBack,
                GetObject(TestWorld, Bench), &Options)))
        {
            TEST_CHECK(run, DataMap_Equals(WorldObject_GetData(GetObject(TestWorld, Bench)), WorldObject_GetData(Source)));
            const uint64_t Both[] = { Lamp, Bench };
            CheckTagged(run, TestWorld, Key(run, &Fixture, "game:office"), Both, 2U);
            TEST_CHECK(run, World_GetDataByteSize(TestWorld) == SumObjectBytes(TestWorld));
        }
        if (ReadBack != NULL)
        {
            TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, ReadBack, true));
        }
        MemoryStream_Deconstruct(&Stream);
    }
    if (Compound != NULL)
    {
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, Compound, true));
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
    }

    // Keys written without a namespace get the reader's default one.
    uint64_t Sign = AddObject(run, TestWorld, "sign", (Vector3){ 0.0f, 0.0f, 0.0f });
    TEST_CHECK_SUCCESS(run, ReadObjectJSON(run, GetObject(TestWorld, Sign), &Fixture.Keys,
        "{\"tags\": [\"lamp\"], \"data\": {\"visits\": 1}}", (const unsigned char*)u8"town"));
    TEST_CHECK(run, World_HasTag(TestWorld, Sign, Key(run, &Fixture, "town:lamp")));
    const DataValue* Found = NULL;
    TEST_CHECK(run, World_TryGetData(TestWorld, Sign, Key(run, &Fixture, "town:visits"), &Found) && (Found->IntValue == 1));
    Memory_Free(Text._data);
    Fixture_Deconstruct(run, &Fixture);
}

/* Reads @p text into a fresh object and checks the read fails naming every fragment, leaving the object as it was and
 * the key registry without the keys the read interned. */
static void CheckReadRefused(TestRun* run, DataFixture* fixture, const char* text, const char* const* fragments)
{
    uint64_t Id = AddObject(run, &fixture->World, "target", (Vector3){ 0.0f, 0.0f, 0.0f });
    TEST_CHECK_SUCCESS(run, World_AddTag(&fixture->World, Id, Key(run, fixture, "test:kept")));
    size_t KeyCount = KeyRegistry_GetCount(&fixture->Keys);
    TestText_CheckErrorMessage(run, ReadObjectJSON(run, GetObject(&fixture->World, Id), &fixture->Keys, text, NULL),
        ErrorCode_InvalidJSON, fragments);
    TEST_CHECK(run, KeyRegistry_GetCount(&fixture->Keys) == KeyCount);
    TEST_CHECK(run, World_HasTag(&fixture->World, Id, Key(run, fixture, "test:kept"))); // all or nothing
}

static void ObjectFilesRefuseHostileData(TestRun* run)
{
    DataFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, NULL))
    {
        return;
    }
    const char* const BadTag[] = { "world_object", "tags[1]", "uppercase", NULL };
    CheckReadRefused(run, &Fixture, "{\"tags\": [\"town:a\", \"Town:B\"]}", BadTag);
    const char* const Twice[] = { "tags[1]", "twice", NULL };
    char TwiceText[64];
    snprintf(TwiceText, sizeof(TwiceText), "{\"tags\": [\"office\", \"%s:office\"]}",
        (const char*)NAMESPACED_KEY_DEFAULT_NAMESPACE); // the same tag, once without the default namespace
    CheckReadRefused(run, &Fixture, TwiceText, Twice);
    const char* const TagAndValue[] = { "data.town:x", "twice", NULL };
    CheckReadRefused(run, &Fixture, "{\"tags\": [\"town:x\"], \"data\": {\"town:x\": 1}}", TagAndValue);
    const char* const WrongElement[] = { "data.town:list[1]", NULL };
    CheckReadRefused(run, &Fixture, "{\"data\": {\"town:list\": [1, \"a\"]}}", WrongElement);
    const char* const NotAList[] = { "tags", "list of keys", NULL };
    CheckReadRefused(run, &Fixture, "{\"tags\": \"town:x\"}", NotAList);
    const char* const NotAnObject[] = { "data", "object of keys", NULL };
    CheckReadRefused(run, &Fixture, "{\"data\": [1]}", NotAnObject);
    const char* const TooDeep[] = { "data.town:deep", "16 levels", NULL };
    CheckReadRefused(run, &Fixture, "{\"data\": {\"town:deep\": [[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]}}", TooDeep);
    // Refused after the data was read whole (an unknown key): its keys are forgotten all the same.
    const char* const UnknownKey[] = { "unknown key", "bogus", NULL };
    CheckReadRefused(run, &Fixture, "{\"tags\": [\"town:fine\"], \"data\": {\"town:also\": 1}, \"bogus\": 1}",
        UnknownKey);

    // Too many entries, too many bytes.
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, DATA_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer_AppendString(&Text, (const unsigned char*)"{\"tags\": [");
    char Entry[64];
    for (size_t Index = 0U; Index <= WORLD_OBJECT_DATA_MAX_ENTRIES; Index++)
    {
        snprintf(Entry, sizeof(Entry), "%s\"test:t%zu\"", (Index == 0U) ? "" : ", ", Index);
        GenericBuffer_AppendString(&Text, (const unsigned char*)Entry);
    }
    GenericBuffer_AppendString(&Text, (const unsigned char*)"]}");
    GenericBuffer_NullTerminate(&Text);
    const char* const TooMany[] = { "tags", "more tags", NULL };
    CheckReadRefused(run, &Fixture, (const char*)Text._data, TooMany);
    GenericBuffer_Clear(&Text);
    GenericBuffer_AppendString(&Text, (const unsigned char*)"{\"data\": {");
    for (size_t Index = 0U; Index < 17U; Index++)
    {
        snprintf(Entry, sizeof(Entry), "%s\"test:page%zu\": \"", (Index == 0U) ? "" : ", ", Index);
        GenericBuffer_AppendString(&Text, (const unsigned char*)Entry);
        for (size_t Byte = 0U; Byte < DATA_VALUE_MAX_STRING_LENGTH; Byte++)
        {
            GenericBuffer_AppendByte(&Text, (unsigned char)'p');
        }
        GenericBuffer_AppendString(&Text, (const unsigned char*)"\"");
    }
    GenericBuffer_AppendString(&Text, (const unsigned char*)"}}");
    GenericBuffer_NullTerminate(&Text);
    const char* const TooLarge[] = { "65536 bytes", NULL };
    CheckReadRefused(run, &Fixture, (const char*)Text._data, TooLarge);
    Memory_Free(Text._data);

    // Without the key registry, data cannot be read; GHDF refuses keys that are not full, and keys listed twice.
    uint64_t Plain = AddObject(run, &Fixture.World, "plain", (Vector3){ 0.0f, 0.0f, 0.0f });
    const char* const NoKeys[] = { "PropertyReadOptions.Keys", NULL };
    TestText_CheckErrorMessage(run, ReadObjectJSON(run, GetObject(&Fixture.World, Plain), NULL,
        "{\"tags\": [\"town:x\"]}", NULL), ErrorCode_InvalidJSON, NoKeys);
    GHDFObjectPool* Pool = NULL;
    GHDFArray* Array = NULL;
    if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Pool))
        && TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowArray(Pool, GHDFValueType_Compound, &Array)))
    {
        const char* const Names[] = { "town:a", "town:a", "lamp" };
        for (size_t Index = 0U; Index < 3U; Index++)
        {
            GHDFCompound* Compound = NULL;
            GHDFObjectValue Type = GHDFObjectValue_CreateUInt8((uint8_t)DataType_Tag);
            TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Pool, &Compound));
            TEST_CHECK_SUCCESS(run, DataValueInternal_PutGHDFString(Pool, Compound, (GHDFEntryID)1,
                (const unsigned char*)Names[Index]));
            TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Compound, (GHDFEntryID)2,
                GHDF_CreateRegularType(GHDFValueType_UInt8), &Type));
            GHDFObjectValue Element = GHDFObjectValue_CreateCompound(Compound);
            TEST_CHECK_SUCCESS(run, GHDFArray_AddValue(Array, &Element));
        }
        DataMap* Map = NULL;
        size_t KeyCount = KeyRegistry_GetCount(&Fixture.Keys);
        const char* const Duplicate[] = { "town:a", "twice", NULL };
        TestText_CheckErrorMessage(run, DataMap_ReadGHDF(Array, &Fixture.Keys, NULL, &Map), ErrorCode_Deserialize,
            Duplicate);
        TEST_CHECK(run, KeyRegistry_GetCount(&Fixture.Keys) == KeyCount);
        TEST_CHECK_SUCCESS(run, GHDFArray_RemoveAt(Array, 0U));
        const char* const NotFull[] = { "lamp", "namespace:path", NULL };
        TestText_CheckErrorMessage(run, DataMap_ReadGHDF(Array, &Fixture.Keys, NULL, &Map), ErrorCode_Deserialize,
            NotFull);
        TEST_CHECK(run, (Map == NULL) && (KeyRegistry_GetCount(&Fixture.Keys) == KeyCount));
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnArray(Pool, Array, true));
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
    }
    Fixture_Deconstruct(run, &Fixture);
}

static void TheDataRowIsReadOnly(TestRun* run)
{
    DataFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, NULL))
    {
        return;
    }
    World* TestWorld = &Fixture.World;
    const PropertyTable* Table = WorldObject_GetPropertyTable();
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(Table));
    uint64_t Lamp = AddObject(run, TestWorld, "lamp", (Vector3){ 0.0f, 0.0f, 0.0f });
    TEST_CHECK_SUCCESS(run, World_AddTag(TestWorld, Lamp, Key(run, &Fixture, "town:lamp")));
    WorldObject* Object = GetObject(TestWorld, Lamp);

    PropertyValue Value;
    TEST_CHECK(run, TEST_CHECK_SUCCESS(run, Properties_Get(Table, Object, u8"data", &Value))
        && (Value.Type == PropertyType_DataMap) && (Value.Data.DataMapValue == WorldObject_GetData(Object)));
    PropertyValue Empty = PropertyValue_FromDataMap(NULL);
    TEST_CHECK_ERROR(run, Properties_Set(Table, Object, u8"data", &Empty), ErrorCode_InvalidOperation);
    TEST_CHECK(run, World_HasTag(TestWorld, Lamp, Key(run, &Fixture, "town:lamp")));

    // "tags" is a key of the table, not an unknown one; a read with tags only is fine.
    TEST_CHECK_SUCCESS(run, ReadObjectJSON(run, Object, &Fixture.Keys, "{\"tags\": [\"town:post\"]}", NULL));
    TEST_CHECK(run, World_HasTag(TestWorld, Lamp, Key(run, &Fixture, "town:post"))
        && !World_HasTag(TestWorld, Lamp, Key(run, &Fixture, "town:lamp")));
    Fixture_Deconstruct(run, &Fixture);
}


/* A value far over the byte limit is refused while it is read, where the bytes run out, not once it is built. */
static void OversizedValuesStopWhereTheBytesRunOut(TestRun* run)
{
    DataFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, NULL))
    {
        return;
    }
    // 20 lists of 4096 empty strings count 20 * 4097 bytes (each string its end, each list its own byte).
    uint32_t Big = Key(run, &Fixture, "test:big");
    DataValue Outer = DataValue_CreateTag();
    TEST_CHECK_SUCCESS(run, DataValue_CreateList(DataType_List, &Outer));
    for (size_t List = 0U; List < 20U; List++)
    {
        DataValue Inner = DataValue_CreateTag();
        Error Result = DataValue_CreateList(DataType_String, &Inner);
        for (size_t Index = 0U; (Index < DATA_VALUE_MAX_ELEMENT_COUNT) && (Result.Code == ErrorCode_Success); Index++)
        {
            DataValue Empty = DataValue_CreateTag();
            Result = DataValue_CreateString((const unsigned char*)u8"", &Empty);
            if (Result.Code == ErrorCode_Success)
            {
                Result = DataValue_ListAdd(&Inner, &Empty);
            }
            DataValue_Deconstruct(&Empty);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = DataValue_ListAdd(&Outer, &Inner);
        }
        DataValue_Deconstruct(&Inner);
        TEST_CHECK_SUCCESS(run, Result);
    }
    DataMap* Source = NULL;
    TEST_CHECK_SUCCESS(run, DataMap_Create(&Fixture.Keys, &Source));
    TEST_CHECK_SUCCESS(run, DataMap_Adopt(Source, Big, &Outer, NULL)); // no limits
    TEST_CHECK(run, DataMap_GetByteSize(Source) == (8U + 1U + 1U + (20U * 4097U)));

    // Under the object limits, the key takes 9 bytes and the outer list 1: 15 inner lists fit (61455 bytes), then the
    // 16th list's own byte and 4070 of its strings.
    const DataMapLimits Limits = { .MaxEntryCount = WORLD_OBJECT_DATA_MAX_ENTRIES,
        .MaxByteSize = WORLD_OBJECT_DATA_MAX_BYTES };
    const char* const RunsOut[] = { "test:big[15][4070]", "65536 bytes", NULL };
    DataMap* Read = NULL;
    JSONObjectPool* JSONPool = NULL;
    JSONCompound* Parent = NULL;
    if (TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&JSONPool))
        && TEST_CHECK_SUCCESS(run, JSONObjectPool_BorrowCompound(JSONPool, &Parent))
        && TEST_CHECK_SUCCESS(run, DataMap_WriteJSON(Source, JSONPool, Parent, (const unsigned char*)u8"tags",
            (const unsigned char*)u8"data", false)))
    {
        TestText_CheckErrorMessage(run, DataMap_ReadJSON(Parent, (const unsigned char*)u8"tags",
            (const unsigned char*)u8"data", &Fixture.Keys, NULL, &Limits, &Read, NULL), ErrorCode_InvalidJSON, RunsOut);
        TEST_CHECK(run, Read == NULL);
        TEST_CHECK(run, TEST_CHECK_SUCCESS(run, DataMap_ReadJSON(Parent, (const unsigned char*)u8"tags",
            (const unsigned char*)u8"data", &Fixture.Keys, NULL, NULL, &Read, NULL)) && DataMap_Equals(Read, Source));
        DataMap_Destroy(Read);
        Read = NULL;
    }
    if (Parent != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnCompound(JSONPool, Parent, true));
    }
    if (JSONPool != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(JSONPool));
    }

    GHDFObjectPool* GHDFPool = NULL;
    GHDFArray* Array = NULL;
    if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&GHDFPool))
        && TEST_CHECK_SUCCESS(run, DataMap_WriteGHDF(Source, GHDFPool, &Array)))
    {
        TestText_CheckErrorMessage(run, DataMap_ReadGHDF(Array, &Fixture.Keys, &Limits, &Read), ErrorCode_Deserialize,
            RunsOut);
        TEST_CHECK(run, Read == NULL);
        TEST_CHECK(run, TEST_CHECK_SUCCESS(run, DataMap_ReadGHDF(Array, &Fixture.Keys, NULL, &Read))
            && DataMap_Equals(Read, Source));
        DataMap_Destroy(Read);
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnArray(GHDFPool, Array, true));
    }
    if (GHDFPool != NULL)
    {
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(GHDFPool));
    }
    DataMap_Destroy(Source);
    Fixture_Deconstruct(run, &Fixture);
}

/* World_SetData with a value that points into the same object's data: it is copied before the data changes. */
static void ValuesFromTheSameDataAreCopiedFirst(TestRun* run)
{
    DataFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, NULL))
    {
        return;
    }
    World* TestWorld = &Fixture.World;
    uint64_t Desk = AddObject(run, TestWorld, "desk", (Vector3){ 0.0f, 0.0f, 0.0f });
    const uint64_t Only[] = { Desk };

    // Four tags fill the data's first allocation, so a fifth entry moves every entry, the one copied from included.
    const char* const Names[] = { "test:a", "test:b", "test:c", "test:d" };
    uint32_t Tags[4];
    for (size_t Index = 0U; Index < 4U; Index++)
    {
        Tags[Index] = Key(run, &Fixture, Names[Index]);
        TEST_CHECK_SUCCESS(run, World_AddTag(TestWorld, Desk, Tags[Index]));
    }
    uint32_t Copied = Key(run, &Fixture, "test:copied");
    const DataValue* Found = NULL;
    TEST_CHECK(run, World_TryGetData(TestWorld, Desk, Tags[0], &Found));
    TEST_CHECK_SUCCESS(run, World_SetData(TestWorld, Desk, Copied, Found));
    TEST_CHECK(run, World_HasTag(TestWorld, Desk, Copied));
    CheckTagged(run, TestWorld, Copied, Only, 1U);

    // A list's own element replaces the list (the change frees the old value, the element with it).
    uint32_t Shelf = Key(run, &Fixture, "test:shelf");
    DataValue List = DataValue_CreateTag();
    TEST_CHECK_SUCCESS(run, DataValue_CreateList(DataType_String, &List));
    DataValue Pen = MakeString(run, "pen");
    DataValue Stamp = MakeString(run, "stamp");
    TEST_CHECK_SUCCESS(run, DataValue_ListAdd(&List, &Pen));
    TEST_CHECK_SUCCESS(run, DataValue_ListAdd(&List, &Stamp));
    TEST_CHECK_SUCCESS(run, World_SetData(TestWorld, Desk, Shelf, &List));
    DataValue_Deconstruct(&List);
    TEST_CHECK(run, World_TryGetData(TestWorld, Desk, Shelf, &Found));
    TEST_CHECK_SUCCESS(run, World_SetData(TestWorld, Desk, Shelf, DataValue_GetListElement(Found, 1U)));
    TEST_CHECK(run, World_TryGetData(TestWorld, Desk, Shelf, &Found) && (Found->Type == DataType_String)
        && (strcmp((const char*)Found->StringValue, "stamp") == 0));

    // A value from another entry replaces a tag: the tag leaves the index.
    TEST_CHECK_SUCCESS(run, World_SetData(TestWorld, Desk, Tags[1], Found));
    TEST_CHECK(run, !World_HasTag(TestWorld, Desk, Tags[1]));
    CheckTagged(run, TestWorld, Tags[1], NULL, 0U);
    CheckTagged(run, TestWorld, Tags[2], Only, 1U);
    TEST_CHECK(run, World_GetDataByteSize(TestWorld) == SumObjectBytes(TestWorld));
    Fixture_Deconstruct(run, &Fixture);
}


/* The tags and data example T24 documented (references/world_structure.md has it now, in a world file), which must
 * keep reading (and writing back unchanged). */
static const char* const DocumentedExample =
    "{\"name\": \"street lamp\", \"tags\": [\"game:office\", \"town:street_lamp\"], \"data\": {"
    "\"town:visits\": 3, \"town:owner\": \"Bob\", \"town:rate\": 1.5, \"town:full\": 2.0, "
    "\"town:open\": true, \"town:kind\": {\"$key\": \"town:desk\"}, \"town:switch\": {\"$object\": 12}, "
    "\"town:spawn\": {\"$vector3\": [1, 2, 3]}, \"town:shade\": {\"$color\": \"#ffcc00\"}, "
    "\"town:opened\": {\"$date_time\": \"2026-10-18T09:30:00\"}, \"town:inventory\": [\"pen\", \"stamp\"], "
    "\"town:visitors\": {\"$list\": \"string\"}, \"town:delivery\": {\"$type\": \"town:delivery\", "
    "\"form\": {\"$key\": \"town:tax_form\"}, \"days\": 3}}}";

static void TheDocumentedExampleReads(TestRun* run)
{
    DataFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, NULL))
    {
        return;
    }
    World* TestWorld = &Fixture.World;
    uint64_t Lamp = AddObject(run, TestWorld, "lamp", (Vector3){ 0.0f, 0.0f, 0.0f });
    uint64_t Copy = AddObject(run, TestWorld, "copy", (Vector3){ 0.0f, 0.0f, 0.0f });
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, DATA_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, ReadObjectJSON(run, GetObject(TestWorld, Lamp), &Fixture.Keys, DocumentedExample, NULL)))
    {
        const DataMap* Data = WorldObject_GetData(GetObject(TestWorld, Lamp));
        TEST_CHECK(run, DataMap_GetCount(Data) == 15U);
        const DataValue* Full = DataMap_Find(Data, Key(run, &Fixture, "town:full"));
        const DataValue* Delivery = DataMap_Find(Data, Key(run, &Fixture, "town:delivery"));
        TEST_CHECK(run, (Full != NULL) && (Full->Type == DataType_Float) && (Full->FloatValue == 2.0));
        TEST_CHECK(run, (Delivery != NULL) && (DataValue_GetCompoundCount(Delivery) == 3U)
            && (DataValue_FindCompoundEntry(Delivery, DATA_VALUE_TYPE_ENTRY_NAME)->Type == DataType_Key));
        TEST_CHECK(run, World_HasTag(TestWorld, Lamp, Key(run, &Fixture, "town:street_lamp")));
        if (WriteObjectJSON(run, GetObject(TestWorld, Lamp), &Text)
            && TEST_CHECK_SUCCESS(run, ReadObjectJSON(run, GetObject(TestWorld, Copy), &Fixture.Keys,
                (const char*)Text._data, NULL)))
        {
            TEST_CHECK(run, DataMap_Equals(WorldObject_GetData(GetObject(TestWorld, Copy)), Data));
        }
    }
    Memory_Free(Text._data);
    Fixture_Deconstruct(run, &Fixture);
}


// Public functions.
void ObjectDataTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "set_get_overwrite_and_remove", SetGetOverwriteAndRemove },
        { "tags_are_entries_without_a_value", TagsAreEntriesWithoutAValue },
        { "the_data_revision_moves_alone", TheDataRevisionMovesAlone },
        { "object_limits_are_refused_and_logged_once", ObjectLimitsAreRefusedAndLoggedOnce },
        { "the_world_limit_counts_every_object", TheWorldLimitCountsEveryObject },
        { "queries_filter_by_each_field", QueriesFilterByEachField },
        { "queries_sort_by_distance_and_limit", QueriesSortByDistanceAndLimit },
        { "malformed_queries_are_refused", MalformedQueriesAreRefused },
        { "the_tag_index_follows_every_change", TheTagIndexFollowsEveryChange },
        { "queries_equal_a_brute_force_scan", QueriesEqualABruteForceScan },
        { "object_tables_round_trip_tags_and_data", ObjectTablesRoundTripTagsAndData },
        { "object_files_refuse_hostile_data", ObjectFilesRefuseHostileData },
        { "oversized_values_stop_where_the_bytes_run_out", OversizedValuesStopWhereTheBytesRunOut },
        { "values_from_the_same_data_are_copied_first", ValuesFromTheSameDataAreCopiedFirst },
        { "the_data_row_is_read_only", TheDataRowIsReadOnly },
        { "the_documented_example_reads", TheDocumentedExampleReads },
    };
    TestRun_RunSuite(run, "ObjectData", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
