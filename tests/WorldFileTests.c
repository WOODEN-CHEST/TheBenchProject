#include "TestSuites.h"
#include "TestText.h"
#include "DataMap.h"
#include "DataValue.h"
#include "GameGHDF.h"
#include "Logger.h"
#include "NamespacedKey.h"
#include "PropertyDescriptor.h"
#include "World.h"
#include "WorldComponent.h"
#include "WorldEnvironment.h"
#include "WorldFile.h"
#include "WorldObject.h"
#include "wr/WRFileSystem.h"
#include "wr/WRGHDF.h"
#include "wr/WRMemoryStream.h"
#include "wr/WRPath.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** Initial capacity of text buffers; they grow as needed. */
#define WORLD_FILE_TEST_TEXT_CAPACITY ((size_t)256)
/** How many candidate values the filler tries for a property before giving up. */
#define WORLD_FILE_TEST_CANDIDATE_COUNT ((size_t)4)
/** The id counter the full world is raised to, so the file must carry it. */
#define WORLD_FILE_TEST_NEXT_ID ((uint64_t)1000)
/** The GHDF signature (references/ghdf.txt), for documents written by hand. */
#define WORLD_FILE_TEST_GHDF_SIGNATURE_LENGTH ((size_t)16)


// Fields.
static const unsigned char GHDFSignature[WORLD_FILE_TEST_GHDF_SIGNATURE_LENGTH] =
{
    102, 37, 143, 181, 3, 205, 123, 185, 148, 157, 98, 177, 178, 151, 43, 170
};


// Static functions: filling tables.
/* A candidate number for a property (or one vector component) whose default is @p current: a step up, a step down, the
 * middle of its bounds, then something further off. Properties_Set decides which one passes the rules. */
static double CandidateNumber(double current, size_t attempt, const PropertyRules* rules)
{
    switch (attempt)
    {
        case 0U:
            return current + 0.25;
        case 1U:
            return current - 0.25;
        case 2U:
            return (rules->HasMinimum && rules->HasMaximum) ? ((rules->Minimum + rules->Maximum) / 2.0) : (current + 2.5);
        default:
            return (current * 0.5) + 0.125;
    }
}

static int64_t CandidateInteger(int64_t current, size_t attempt)
{
    return (attempt == 0U) ? (current + 1) : ((attempt == 1U) ? (current - 1) : (current + 3));
}

static Vector3 CandidateVector3(Vector3 current, size_t attempt, const PropertyRules* rules)
{
    return (Vector3)
    {
        .x = (float)CandidateNumber((double)current.x, attempt, rules),
        .y = (float)CandidateNumber((double)current.y, attempt, rules),
        .z = (float)CandidateNumber((double)current.z, attempt, rules)
    };
}

/* Candidate @p attempt for a row: a value of its type that differs from its default. */
static PropertyValue MakeCandidate(const PropertyDescriptor* row, size_t attempt)
{
    PropertyValue Value = { .Type = row->Type, .Data = row->Default };
    const PropertyRules* Rules = &row->Rules;
    switch (row->Type)
    {
        case PropertyType_Bool:
            Value.Data.BoolValue = !row->Default.BoolValue;
            break;
        case PropertyType_Int32:
            Value.Data.Int32Value = (int32_t)CandidateInteger(row->Default.Int32Value, attempt);
            break;
        case PropertyType_UInt32:
            Value.Data.UInt32Value = (uint32_t)CandidateInteger((int64_t)row->Default.UInt32Value, attempt + 2U);
            break;
        case PropertyType_UInt64:
        case PropertyType_ObjectId:
            // Above UINT32_MAX, so GHDF needs its widest unsigned type.
            Value.Data.UInt64Value = row->Default.UInt64Value + 0x100000007ULL + (uint64_t)attempt;
            break;
        case PropertyType_Float:
        case PropertyType_Angle:
            Value.Data.FloatValue = (float)CandidateNumber((double)row->Default.FloatValue, attempt, Rules);
            break;
        case PropertyType_Double:
            Value.Data.DoubleValue = CandidateNumber(row->Default.DoubleValue, attempt, Rules) + (1.0 / 3.0);
            break;
        case PropertyType_Vector2:
        {
            Vector3 Candidate = CandidateVector3((Vector3){ row->Default.Vector2Value.x, row->Default.Vector2Value.y, 0.0f },
                attempt, Rules);
            Value.Data.Vector2Value = (Vector2){ Candidate.x, Candidate.y };
            break;
        }
        case PropertyType_Vector3:
        case PropertyType_AngleVector3:
            Value.Data.Vector3Value = CandidateVector3(row->Default.Vector3Value, attempt, Rules);
            break;
        case PropertyType_Color:
        {
            Color Default = row->Default.ColorValue;
            Value.Data.ColorValue = (Color){ (unsigned char)(255U - Default.r), (unsigned char)(255U - Default.g),
                (unsigned char)(255U - Default.b), (unsigned char)((Default.a == 255U) ? 128U : 255U) };
            break;
        }
        case PropertyType_RenderColor:
        {
            Color Tint = row->Default.RenderColorValue.Tint;
            Value.Data.RenderColorValue = (RenderColor){ { (unsigned char)(255U - Tint.r), (unsigned char)(255U - Tint.g),
                (unsigned char)(255U - Tint.b), 255U }, 0.5f, 0.75f };
            break;
        }
        case PropertyType_String:
            Value.Data.StringValue = u8"Galds ar lampu ā";
            break;
        case PropertyType_AssetName:
            Value.Data.StringValue = u8"test_asset.v2";
            break;
        case PropertyType_Enum:
            for (size_t Index = 0U; Index < row->EnumTable->EntryCount; Index++)
            {
                if (row->EnumTable->Entries[Index].Value != row->Default.EnumValue)
                {
                    Value.Data.EnumValue = row->EnumTable->Entries[Index].Value;
                    break;
                }
            }
            break;
        case PropertyType_Date:
            Value.Data.DateValue = row->Default.DateValue + 40 + (int64_t)attempt;
            break;
        case PropertyType_DataMap:
            break;
    }
    return Value;
}

/* Sets every settable row of @p table (all but the ReadOnly ones, which go through the world) to a value that is not
 * its default and passes its rules. */
static void FillTable(TestRun* run, const PropertyTable* table, void* instance)
{
    for (size_t Row = 0U; Row < table->DescriptorCount; Row++)
    {
        const PropertyDescriptor* Descriptor = &table->Descriptors[Row];
        if ((Descriptor->Flags & PropertyFlags_ReadOnly) != 0U)
        {
            continue;
        }
        bool IsSet = false;
        for (size_t Attempt = 0U; (Attempt < WORLD_FILE_TEST_CANDIDATE_COUNT) && !IsSet; Attempt++)
        {
            PropertyValue Candidate = MakeCandidate(Descriptor, Attempt);
            Error Result = Properties_Set(table, instance, Descriptor->Key, &Candidate);
            IsSet = Result.Code == ErrorCode_Success;
            Error_Deconstruct(&Result);
        }
        if (!TEST_CHECK(run, IsSet))
        {
            printf("      no non-default value found for %s.%s\n", (const char*)table->Name, (const char*)Descriptor->Key);
        }
    }
}

/* How many JSON keys an instance of @p table with every persistent row away from its default writes. */
static size_t CountPersistentJSONKeys(const PropertyTable* table)
{
    size_t Count = 0U;
    for (size_t Row = 0U; Row < table->DescriptorCount; Row++)
    {
        const PropertyDescriptor* Descriptor = &table->Descriptors[Row];
        if ((Descriptor->Flags & PropertyFlags_Persistent) != 0U)
        {
            Count += (Descriptor->TagsKey != NULL) ? 2U : 1U;
        }
    }
    return Count;
}

static uint32_t InternKey(TestRun* run, KeyRegistry* keys, const char* text)
{
    uint32_t Id = NAMESPACED_KEY_ID_NONE;
    TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(keys, (const unsigned char*)text, &Id));
    return Id;
}

static void SetData(TestRun* run, World* world, uint64_t id, KeyRegistry* keys, const char* key, DataValue* value)
{
    TEST_CHECK_SUCCESS(run, World_SetData(world, id, InternKey(run, keys, key), value));
    DataValue_Deconstruct(value);
}

/* Tags and a value of every data type on one object (lists and compounds with something inside). */
static void FillData(TestRun* run, World* world, uint64_t id, uint64_t otherId, KeyRegistry* keys)
{
    TEST_CHECK_SUCCESS(run, World_AddTag(world, id, InternKey(run, keys, "town:furniture")));
    TEST_CHECK_SUCCESS(run, World_AddTag(world, id, InternKey(run, keys, "tbp:office")));
    DataValue Value = DataValue_FromBool(true);
    SetData(run, world, id, keys, "town:open", &Value);
    Value = DataValue_FromInt(-1234567890123LL);
    SetData(run, world, id, keys, "town:count", &Value);
    Value = DataValue_FromFloat(0.1);
    SetData(run, world, id, keys, "town:rate", &Value);
    Value = DataValue_FromKey(InternKey(run, keys, "town:desk"));
    SetData(run, world, id, keys, "town:kind", &Value);
    Value = DataValue_FromObject(otherId);
    SetData(run, world, id, keys, "town:switch", &Value);
    Value = DataValue_FromVector3((Vector3){ 1.5f, -2.0f, 1.0f / 3.0f });
    SetData(run, world, id, keys, "town:spawn", &Value);
    Value = DataValue_FromColor((Color){ 1, 2, 3, 4 });
    SetData(run, world, id, keys, "town:shade", &Value);
    Value = DataValue_FromDateTime(63900000000123456LL);
    SetData(run, world, id, keys, "town:opened", &Value);
    if (TEST_CHECK_SUCCESS(run, DataValue_CreateString((const unsigned char*)u8"Žvaigzne", &Value)))
    {
        SetData(run, world, id, keys, "town:owner", &Value);
    }
    if (TEST_CHECK_SUCCESS(run, DataValue_CreateList(DataType_Int, &Value)))
    {
        const int64_t Numbers[] = { 1, 300, -70000, 5000000000LL };
        for (size_t Index = 0U; Index < (sizeof(Numbers) / sizeof(Numbers[0])); Index++)
        {
            DataValue Element = DataValue_FromInt(Numbers[Index]);
            TEST_CHECK_SUCCESS(run, DataValue_ListAdd(&Value, &Element));
        }
        SetData(run, world, id, keys, "town:numbers", &Value);
    }
    if (TEST_CHECK_SUCCESS(run, DataValue_CreateList(DataType_String, &Value)))
    {
        SetData(run, world, id, keys, "town:empty", &Value);
    }
    DataValue Inner;
    if (TEST_CHECK_SUCCESS(run, DataValue_CreateCompound(&Value))
        && TEST_CHECK_SUCCESS(run, DataValue_CreateList(DataType_Float, &Inner)))
    {
        DataValue Element = DataValue_FromFloat(2.5);
        TEST_CHECK_SUCCESS(run, DataValue_ListAdd(&Inner, &Element));
        TEST_CHECK_SUCCESS(run, DataValue_CompoundSet(&Value, (const unsigned char*)u8"speeds", &Inner));
        Element = DataValue_FromInt(3);
        TEST_CHECK_SUCCESS(run, DataValue_CompoundSet(&Value, (const unsigned char*)u8"days", &Element));
        SetData(run, world, id, keys, "town:delivery", &Value);
        DataValue_Deconstruct(&Inner);
    }
}

/* Fills an object's table and adds every component type, each filled. */
static void FillObject(TestRun* run, World* world, WorldObject* object)
{
    FillTable(run, WorldObject_GetPropertyTable(), object);
    for (size_t Type = 0U; Type < World_GetComponentTypeCount(world); Type++)
    {
        WorldComponent* Component = NULL;
        if (TEST_CHECK_SUCCESS(run, World_AddComponent(world, WorldObject_GetId(object), (WorldComponentType)Type,
            &Component)))
        {
            FillTable(run, World_GetComponentTypeInfo(world, (WorldComponentType)Type)->Table, Component);
        }
    }
}

static WorldObject* CreateObject(TestRun* run, World* world)
{
    WorldObject* Object = NULL;
    TEST_CHECK_SUCCESS(run, World_CreateObject(world, NULL, &Object));
    return Object;
}

/* A world that uses every property of every table with values that are not their defaults: the environment, a name, a
 * raised id counter, a hierarchy three levels deep whose file order is not the id order (an older object under a newer
 * one, children not attached in id order), every component type, and tags and data of every value type. Ids: 1 base, 2
 * middle (under 1), 3 leaf (under 2), 4 older (under 5), 5 newer, 6 plain (all defaults), 7 late (under 1, before 2). */
static bool BuildFullWorld(TestRun* run, World* world, KeyRegistry* keys)
{
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(keys)))
    {
        return false;
    }
    if (!TEST_CHECK_SUCCESS(run, World_Construct1(world, keys)))
    {
        KeyRegistry_Deconstruct(keys);
        return false;
    }
    TEST_CHECK_SUCCESS(run, World_SetName(world, (const unsigned char*)u8"Pasaule ar visu"));
    FillTable(run, WorldEnvironment_GetPropertyTable(), World_GetEnvironment(world));

    WorldObject* Objects[7] = { NULL };
    for (size_t Index = 0U; Index < 7U; Index++)
    {
        Objects[Index] = CreateObject(run, world);
        if (Objects[Index] == NULL)
        {
            return true; // the checks recorded the failure; the caller tears the world down
        }
    }
    for (size_t Index = 0U; Index < 7U; Index++)
    {
        if (Index != 5U)
        {
            FillObject(run, world, Objects[Index]);
        }
    }
    TEST_CHECK_SUCCESS(run, World_SetParent(world, 7U, 1U, false));
    TEST_CHECK_SUCCESS(run, World_SetParent(world, 2U, 1U, false));
    TEST_CHECK_SUCCESS(run, World_SetParent(world, 3U, 2U, false));
    TEST_CHECK_SUCCESS(run, World_SetParent(world, 4U, 5U, false));
    FillData(run, world, 1U, 5U, keys);
    FillData(run, world, 3U, 1U, keys);
    World_EnsureNextObjectIdAtLeast(world, WORLD_FILE_TEST_NEXT_ID);
    return true;
}

static void DestroyWorld(TestRun* run, World* world, KeyRegistry* keys)
{
    TEST_CHECK_SUCCESS(run, World_Deconstruct(world));
    KeyRegistry_Deconstruct(keys);
}


// Static functions: comparing worlds.
static bool AreNamesEqual(const unsigned char* a, const unsigned char* b)
{
    if ((a == NULL) || (b == NULL))
    {
        return a == b;
    }
    size_t Length = strlen((const char*)a);
    return (Length == strlen((const char*)b)) && (memcmp(a, b, Length) == 0);
}

/* Checks that two worlds hold the same thing: name, id counter, environment, and every object with its table, its
 * components' tables and its children in the same order. */
static void CheckWorldsEqual(TestRun* run, World* expected, World* actual)
{
    TEST_CHECK(run, AreNamesEqual(World_GetName(expected), World_GetName(actual)));
    TEST_CHECK(run, World_GetNextObjectId(expected) == World_GetNextObjectId(actual));
    TEST_CHECK(run, Properties_Equals(WorldEnvironment_GetPropertyTable(), World_GetEnvironment(expected),
        World_GetEnvironment(actual)));
    if (!TEST_CHECK(run, World_GetObjectCount(expected) == World_GetObjectCount(actual)))
    {
        return;
    }
    for (size_t Index = 0U; Index < World_GetObjectCount(expected); Index++)
    {
        WorldObject* Expected = NULL;
        WorldObject* Actual = NULL;
        if (!TEST_CHECK_SUCCESS(run, World_GetObjectByIndex(expected, Index, &Expected))
            || !TEST_CHECK(run, World_TryGetObject(actual, WorldObject_GetId(Expected), &Actual)))
        {
            continue;
        }
        if (!TEST_CHECK(run, Properties_Equals(WorldObject_GetPropertyTable(), Expected, Actual)))
        {
            printf("      object %" PRIu64 " differs\n", WorldObject_GetId(Expected));
        }
        for (size_t Type = 0U; Type < World_GetComponentTypeCount(expected); Type++)
        {
            WorldComponent* ExpectedComponent = WorldObject_GetComponent(Expected, (WorldComponentType)Type);
            WorldComponent* ActualComponent = WorldObject_GetComponent(Actual, (WorldComponentType)Type);
            if (TEST_CHECK(run, (ExpectedComponent == NULL) == (ActualComponent == NULL)) && (ExpectedComponent != NULL))
            {
                TEST_CHECK(run, Properties_Equals(World_GetComponentTypeInfo(expected, (WorldComponentType)Type)->Table,
                    ExpectedComponent, ActualComponent));
            }
        }
        const WorldObject* ExpectedChild = WorldObject_GetFirstChild(Expected);
        const WorldObject* ActualChild = WorldObject_GetFirstChild(Actual);
        while ((ExpectedChild != NULL) && (ActualChild != NULL))
        {
            TEST_CHECK(run, WorldObject_GetId(ExpectedChild) == WorldObject_GetId(ActualChild));
            ExpectedChild = WorldObject_GetNextSibling(ExpectedChild);
            ActualChild = WorldObject_GetNextSibling(ActualChild);
        }
        TEST_CHECK(run, (ExpectedChild == NULL) && (ActualChild == NULL));
    }
}


// Static functions: helpers.
static bool BytesEqual(const GenericBuffer* a, const GenericBuffer* b)
{
    return (a->_count == b->_count) && ((a->_count == 0U) || (memcmp(a->_data, b->_data, a->_count) == 0));
}

static Error ReadJSONText(const char* text, KeyRegistry* keys, Logger* logger, World* outWorld)
{
    WorldFileReadOptions Options = WorldFileReadOptions_Create(keys, NULL, logger);
    return WorldFile_ReadJSON((const unsigned char*)text, strlen(text), &Options, outWorld);
}

static Error ReadGHDFBytes(const GenericBuffer* bytes, KeyRegistry* keys, Logger* logger, World* outWorld)
{
    WorldFileReadOptions Options = WorldFileReadOptions_Create(keys, NULL, logger);
    return WorldFile_ReadGHDF(bytes->_data, bytes->_count, &Options, outWorld);
}

/* Reading @p text must fail with every fragment in its message and leave nothing: no world (the run's leak check) and
 * no key interned by the read. */
static void CheckRefusedJSON(TestRun* run, KeyRegistry* keys, const char* text, const char* const* fragments)
{
    size_t KeysBefore = KeyRegistry_GetCount(keys);
    World Target;
    Error Result = ReadJSONText(text, keys, NULL, &Target);
    bool HasFailed = Result.Code != ErrorCode_Success;
    if (!TestText_CheckErrorMessage(run, Result, ErrorCode_InvalidJSON, fragments))
    {
        printf("      for: %.200s\n", text);
    }
    if (!HasFailed)
    {
        TEST_CHECK_SUCCESS(run, World_Deconstruct(&Target));
    }
    TEST_CHECK(run, KeyRegistry_GetCount(keys) == KeysBefore);
}

/* As CheckRefusedJSON for GHDF bytes; @p fragments may be NULL (only the code is checked then). */
static void CheckRefusedGHDF(TestRun* run, KeyRegistry* keys, const GenericBuffer* bytes, const char* const* fragments)
{
    size_t KeysBefore = KeyRegistry_GetCount(keys);
    World Target;
    Error Result = ReadGHDFBytes(bytes, keys, NULL, &Target);
    bool HasFailed = Result.Code != ErrorCode_Success;
    const char* const NoFragments[] = { NULL };
    TestText_CheckErrorMessage(run, Result, ErrorCode_Deserialize, (fragments != NULL) ? fragments : NoFragments);
    if (!HasFailed)
    {
        TEST_CHECK_SUCCESS(run, World_Deconstruct(&Target));
    }
    TEST_CHECK(run, KeyRegistry_GetCount(keys) == KeysBefore);
}

/* Counts the occurrences of @p needle in "<directory>/logs/latest.log". */
static size_t CountInLatestLog(TestRun* run, const unsigned char* directory, const char* needle)
{
    size_t Count = 0U;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
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

static void AppendVarint(GenericBuffer* bytes, uint64_t value)
{
    do
    {
        unsigned char Byte = (unsigned char)(value & 0x7FU);
        value >>= 7U;
        if (value != 0U)
        {
            Byte |= 0x80U;
        }
        GenericBuffer_AppendByte(bytes, Byte);
    }
    while (value != 0U);
}

/* A GHDF document whose root holds one string entry (id 2, the world's name) declaring @p declaredLength bytes, followed
 * by @p presentLength bytes of text. */
static void BuildDeclaredStringDocument(GenericBuffer* bytes, uint64_t declaredLength, size_t presentLength)
{
    GenericBuffer_AppendRangeBytes(bytes, (unsigned char*)GHDFSignature, sizeof(GHDFSignature));
    AppendVarint(bytes, 1U);                // the GHDF version
    AppendVarint(bytes, 1U);                // one root entry
    AppendVarint(bytes, (uint64_t)WorldFileRootEntry_Name);
    GenericBuffer_AppendByte(bytes, 12U);   // String
    AppendVarint(bytes, declaredLength);
    for (size_t Index = 0U; Index < presentLength; Index++)
    {
        GenericBuffer_AppendByte(bytes, (unsigned char)'a');
    }
}

/* Writes a GHDF tree into @p outBytes. */
static bool WriteGHDFTree(TestRun* run, GHDFCompound* root, GenericBuffer* outBytes)
{
    MemoryStream Stream;
    if (!TEST_CHECK_SUCCESS(run, MemoryStream_Construct1(&Stream, IOStreamFlags_CanWrite)))
    {
        return false;
    }
    bool IsWritten = TEST_CHECK_SUCCESS(run, GHDF_Write(root, MemoryStream_AsIOStream(&Stream)));
    GenericBuffer* Written = MemoryStream_GetBuffer(&Stream);
    if (IsWritten)
    {
        GenericBuffer_AppendRangeBytes(outBytes, (unsigned char*)Written->_data, Written->_count);
    }
    MemoryStream_Deconstruct(&Stream);
    return IsWritten;
}

/* Reads GHDF bytes into a tree from @p pool. */
static bool ReadGHDFTree(TestRun* run, GHDFObjectPool* pool, const GenericBuffer* bytes, GHDFCompound** outRoot)
{
    GenericBuffer Source;
    GenericBuffer_CreateReadOnly(&Source, bytes->_data, bytes->_count, sizeof(unsigned char), bytes->_count);
    MemoryStream Stream;
    if (!TEST_CHECK_SUCCESS(run, MemoryStream_Construct2(&Stream, &Source, IOStreamFlags_CanRead)))
    {
        return false;
    }
    bool IsRead = TEST_CHECK_SUCCESS(run, GHDF_Read(MemoryStream_AsIOStream(&Stream), pool, outRoot));
    MemoryStream_Deconstruct(&Stream);
    return IsRead;
}

/* The objects array of a GHDF world tree, and its first object's compound. */
static GHDFCompound* GetFirstGHDFObject(TestRun* run, GHDFCompound* root)
{
    GHDFObjectValue Objects;
    GHDFObjectValue First;
    if (!TEST_CHECK_SUCCESS(run, GHDFCompound_Get(root, WorldFileRootEntry_Objects, &Objects))
        || !TEST_CHECK_SUCCESS(run, GHDFArray_Get(Objects.Value.Array, 0U, &First)))
    {
        return NULL;
    }
    return First.Value.Compound;
}


// Test cases: round trips.
static void AFullWorldRoundTripsThroughJSON(TestRun* run)
{
    World Source;
    KeyRegistry Keys;
    if (!BuildFullWorld(run, &Source, &Keys))
    {
        return;
    }
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Again;
    GenericBuffer_AllocateVariable(&Again, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, WorldFile_WriteJSON(&Source, &Text)) && TEST_CHECK(run, GenericBuffer_NullTerminate(&Text)))
    {
        // Every persistent property of the filled object is written: the filler left none at its default.
        JSONObjectPool* Pool = NULL;
        JSONObjectValue Root;
        if (TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool))
            && TestText_ParseJSON(run, Pool, (const char*)Text._data, &Root))
        {
            // The leaf (id 3, fourth in the file) has a parent, tags and data as well as every other property.
            JSONObjectValue Objects;
            JSONObjectValue Leaf;
            JSONObjectValue Environment;
            if (TEST_CHECK_SUCCESS(run, JSONCompound_Get(Root.Value.Compound, u8"objects", &Objects))
                && TEST_CHECK_SUCCESS(run, JSONArray_Get(Objects.Value.Array, 3U, &Leaf))
                && TEST_CHECK_SUCCESS(run, JSONCompound_Get(Root.Value.Compound, u8"environment", &Environment)))
            {
                TEST_CHECK(run, JSONCompound_GetEntryCount(Leaf.Value.Compound)
                    == (CountPersistentJSONKeys(WorldObject_GetPropertyTable()) + 2U));
                TEST_CHECK(run, JSONCompound_GetEntryCount(Environment.Value.Compound)
                    == CountPersistentJSONKeys(WorldEnvironment_GetPropertyTable()));
            }
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Root));
        }
        if (Pool != NULL)
        {
            TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
        }
        GenericBuffer_SetCount(&Text, Text._count - 1U); // the terminator added for the parse

        World Loaded;
        WorldFileReadOptions Options = WorldFileReadOptions_Create(&Keys, NULL, NULL);
        if (TEST_CHECK_SUCCESS(run, WorldFile_ReadJSON(Text._data, Text._count, &Options, &Loaded)))
        {
            CheckWorldsEqual(run, &Source, &Loaded);
            // Written again, the text is the same: the format is stable.
            TEST_CHECK_SUCCESS(run, WorldFile_WriteJSON(&Loaded, &Again));
            TEST_CHECK(run, BytesEqual(&Text, &Again));
            TEST_CHECK_SUCCESS(run, World_Deconstruct(&Loaded));
        }
    }
    Memory_Free(Again._data);
    Memory_Free(Text._data);
    DestroyWorld(run, &Source, &Keys);
}

static void AFullWorldRoundTripsThroughGHDF(TestRun* run)
{
    World Source;
    KeyRegistry Keys;
    if (!BuildFullWorld(run, &Source, &Keys))
    {
        return;
    }
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Again;
    GenericBuffer_AllocateVariable(&Again, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    World Loaded;
    if (TEST_CHECK_SUCCESS(run, WorldFile_WriteGHDF(&Source, &Bytes))
        && TEST_CHECK_SUCCESS(run, ReadGHDFBytes(&Bytes, &Keys, NULL, &Loaded)))
    {
        CheckWorldsEqual(run, &Source, &Loaded);
        TEST_CHECK_SUCCESS(run, WorldFile_WriteGHDF(&Loaded, &Again));
        TEST_CHECK(run, Again._count == Bytes._count);
        TEST_CHECK_SUCCESS(run, World_Deconstruct(&Loaded));
    }
    Memory_Free(Again._data);
    Memory_Free(Bytes._data);
    DestroyWorld(run, &Source, &Keys);
}

static void JSONAndGHDFHoldTheSameWorld(TestRun* run)
{
    World Source;
    KeyRegistry Keys;
    if (!BuildFullWorld(run, &Source, &Keys))
    {
        return;
    }
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer FromGHDF;
    GenericBuffer_AllocateVariable(&FromGHDF, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    World Loaded;
    if (TEST_CHECK_SUCCESS(run, WorldFile_WriteGHDF(&Source, &Bytes))
        && TEST_CHECK_SUCCESS(run, WorldFile_WriteJSON(&Source, &Text))
        && TEST_CHECK_SUCCESS(run, ReadGHDFBytes(&Bytes, &Keys, NULL, &Loaded)))
    {
        // A world through GHDF writes the same JSON as the original: nothing is lost on the binary path.
        TEST_CHECK_SUCCESS(run, WorldFile_WriteJSON(&Loaded, &FromGHDF));
        TEST_CHECK(run, BytesEqual(&Text, &FromGHDF));
        TEST_CHECK(run, Bytes._count < Text._count);
        TEST_CHECK_SUCCESS(run, World_Deconstruct(&Loaded));
    }
    Memory_Free(FromGHDF._data);
    Memory_Free(Text._data);
    Memory_Free(Bytes._data);
    DestroyWorld(run, &Source, &Keys);
}


// Test cases: written output.
static void WrittenJSONLeavesOutDefaultsInTableOrder(TestRun* run)
{
    KeyRegistry Keys;
    World Source;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }
    if (!TEST_CHECK_SUCCESS(run, World_Construct1(&Source, &Keys)))
    {
        KeyRegistry_Deconstruct(&Keys);
        return;
    }
    WorldObject* Plain = CreateObject(run, &Source);
    WorldObject* Lamp = CreateObject(run, &Source);
    WorldComponent* Component = NULL;
    if ((Plain != NULL) && (Lamp != NULL))
    {
        TEST_CHECK_SUCCESS(run, World_AddComponent(&Source, 1U, WorldComponentType_Light, &Component));
        // Set out of table order: the file still lists name, position, scale.
        TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Lamp, (Vector3){ 2.0f, 2.0f, 2.0f }));
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Lamp, (Vector3){ 1.0f, 2.0f, 3.0f }));
        TEST_CHECK_SUCCESS(run, Properties_Set(WorldObject_GetPropertyTable(), Lamp, u8"name",
            &(PropertyValue){ .Type = PropertyType_String, .Data.StringValue = u8"lamp" }));
        if (TEST_CHECK_SUCCESS(run, World_AddComponent(&Source, 2U, WorldComponentType_Model, &Component)))
        {
            TEST_CHECK_SUCCESS(run, Properties_Set(World_GetComponentTypeInfo(&Source, WorldComponentType_Model)->Table,
                Component, u8"model", &(PropertyValue){ .Type = PropertyType_AssetName, .Data.StringValue = u8"desk" }));
        }
    }
    const char* Expected =
        "{\n"
        "    \"format_version\": 1,\n"
        "    \"next_object_id\": 3,\n"
        "    \"environment\": {},\n"
        "    \"objects\": [\n"
        "        {\n"
        "            \"id\": 1,\n"
        "            \"components\": {\n"
        "                \"light\": {}\n"
        "            }\n"
        "        },\n"
        "        {\n"
        "            \"id\": 2,\n"
        "            \"name\": \"lamp\",\n"
        "            \"position\": [1.0, 2.0, 3.0],\n"
        "            \"scale\": [2.0, 2.0, 2.0],\n"
        "            \"components\": {\n"
        "                \"model\": {\n"
        "                    \"model\": \"desk\"\n"
        "                }\n"
        "            }\n"
        "        }\n"
        "    ]\n"
        "}\n";
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, WorldFile_WriteJSON(&Source, &Text)))
    {
        TEST_CHECK(run, (Text._count == strlen(Expected)) && (memcmp(Text._data, Expected, Text._count) == 0));
        if ((Text._count != strlen(Expected)) || (memcmp(Text._data, Expected, Text._count) != 0))
        {
            printf("      written:\n%.*s\n", (int)Text._count, (const char*)Text._data);
        }
    }

    // A change adds its own line and moves nothing else: hiding the lamp writes "is_visible" after "scale".
    if (Lamp != NULL)
    {
        TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(Lamp, false));
        GenericBuffer Changed;
        GenericBuffer_AllocateVariable(&Changed, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
        const char* ScaleLine = "            \"scale\": [2.0, 2.0, 2.0],\n";
        const char* AddedLine = "            \"is_visible\": false,\n";
        const char* InsertAt = strstr(Expected, ScaleLine);
        if (TEST_CHECK(run, InsertAt != NULL) && TEST_CHECK_SUCCESS(run, WorldFile_WriteJSON(&Source, &Changed)))
        {
            size_t Prefix = (size_t)(InsertAt - Expected) + strlen(ScaleLine);
            TEST_CHECK(run, Changed._count == (strlen(Expected) + strlen(AddedLine)));
            TEST_CHECK(run, memcmp(Changed._data, Expected, Prefix) == 0);
            TEST_CHECK(run, memcmp((const unsigned char*)Changed._data + Prefix, AddedLine, strlen(AddedLine)) == 0);
            TEST_CHECK(run, memcmp((const unsigned char*)Changed._data + Prefix + strlen(AddedLine), Expected + Prefix,
                strlen(Expected) - Prefix) == 0);
        }
        Memory_Free(Changed._data);
    }
    Memory_Free(Text._data);
    TEST_CHECK_SUCCESS(run, World_Deconstruct(&Source));
    KeyRegistry_Deconstruct(&Keys);
}

static void ObjectsAreWrittenParentsFirst(TestRun* run)
{
    World Source;
    KeyRegistry Keys;
    if (!BuildFullWorld(run, &Source, &Keys))
    {
        return;
    }
    // The roots by id (1, 5, 6), each followed by its subtree depth first in attach order: 1's children are 7 then 2.
    const uint64_t ExpectedOrder[] = { 1U, 7U, 2U, 3U, 5U, 4U, 6U };
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    JSONObjectPool* Pool = NULL;
    JSONObjectValue Root;
    if (TEST_CHECK_SUCCESS(run, WorldFile_WriteJSON(&Source, &Text)) && TEST_CHECK(run, GenericBuffer_NullTerminate(&Text))
        && TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool))
        && TestText_ParseJSON(run, Pool, (const char*)Text._data, &Root))
    {
        JSONObjectValue Objects;
        if (TEST_CHECK_SUCCESS(run, JSONCompound_Get(Root.Value.Compound, u8"objects", &Objects))
            && TEST_CHECK(run, JSONArray_GetElementCount(Objects.Value.Array) == 7U))
        {
            for (size_t Index = 0U; Index < 7U; Index++)
            {
                JSONObjectValue Object;
                JSONObjectValue Id;
                TEST_CHECK(run, (JSONArray_Get(Objects.Value.Array, Index, &Object).Code == ErrorCode_Success)
                    && (JSONCompound_Get(Object.Value.Compound, u8"id", &Id).Code == ErrorCode_Success)
                    && (Id.Value.Integer == (int64_t)ExpectedOrder[Index]));
            }
        }
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Root));
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    }
    Memory_Free(Text._data);
    DestroyWorld(run, &Source, &Keys);
}

static void AFailedWriteAppendsNothing(TestRun* run)
{
    KeyRegistry Keys;
    World Source;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }
    if (TEST_CHECK_SUCCESS(run, World_Construct1(&Source, &Keys)))
    {
        // The writers validate every table first: a position that is not finite (only a bug writing the field directly
        // could leave one; the setters refuse it) is refused before anything is appended.
        WorldObject* Broken = CreateObject(run, &Source);
        if (Broken != NULL)
        {
            Broken->_position.x = NAN;
        }
        GenericBuffer Bytes;
        GenericBuffer_AllocateVariable(&Bytes, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
        GenericBuffer_AppendString(&Bytes, (const unsigned char*)u8"kept");
        TEST_CHECK_ERROR(run, WorldFile_WriteJSON(&Source, &Bytes), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldFile_WriteGHDF(&Source, &Bytes), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, (Bytes._count == 4U) && (memcmp(Bytes._data, "kept", 4U) == 0));
        Memory_Free(Bytes._data);
        TEST_CHECK_SUCCESS(run, World_Deconstruct(&Source));
    }
    KeyRegistry_Deconstruct(&Keys);
}


// Test cases: loading rules.
static void HostileJSONIsRefusedWithNothingLeft(TestRun* run)
{
    KeyRegistry Keys;
    World Live;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }
    if (!TEST_CHECK_SUCCESS(run, World_Construct1(&Live, &Keys)))
    {
        KeyRegistry_Deconstruct(&Keys);
        return;
    }
    // A live world the refused loads must not touch.
    WorldObject* LiveObject = CreateObject(run, &Live);

    const char* const NotJSON[] = { NULL };
    CheckRefusedJSON(run, &Keys, "{\"format_version\": 1,", NotJSON);
    const char* const NotObject[] = { "must be a JSON object", NULL };
    CheckRefusedJSON(run, &Keys, "[1, 2]", NotObject);
    const char* const NoVersion[] = { "\"format_version\" is missing", NULL };
    CheckRefusedJSON(run, &Keys, "{\"objects\": []}", NoVersion);
    const char* const ZeroVersion[] = { "at least 1", NULL };
    CheckRefusedJSON(run, &Keys, "{\"format_version\": 0}", ZeroVersion);
    const char* const NewerVersion[] = { "format version 2 is newer than this game reads (1)", NULL };
    CheckRefusedJSON(run, &Keys, "{\"format_version\": 2}", NewerVersion);
    const char* const TextVersion[] = { "format_version", NULL };
    CheckRefusedJSON(run, &Keys, "{\"format_version\": \"one\"}", TextVersion);
    const char* const LongName[] = { "name is longer than the 256 bytes", NULL };
    char NameText[512];
    snprintf(NameText, sizeof(NameText), "{\"format_version\": 1, \"name\": \"%0300d\"}", 0);
    CheckRefusedJSON(run, &Keys, NameText, LongName);
    const char* const NumberName[] = { "\"name\" must be a string", NULL };
    CheckRefusedJSON(run, &Keys, "{\"format_version\": 1, \"name\": 3}", NumberName);
    const char* const ObjectsNotArray[] = { "\"objects\" must be an array", NULL };
    CheckRefusedJSON(run, &Keys, "{\"format_version\": 1, \"objects\": {}}", ObjectsNotArray);
    const char* const ObjectNotObject[] = { "objects[1] must be an object", NULL };
    CheckRefusedJSON(run, &Keys, "{\"format_version\": 1, \"objects\": [{\"id\": 1}, 2]}", ObjectNotObject);
    const char* const NoId[] = { "objects[0] has no \"id\"", NULL };
    CheckRefusedJSON(run, &Keys, "{\"format_version\": 1, \"objects\": [{\"name\": \"a\"}]}", NoId);
    const char* const ZeroId[] = { "objects[0]: the id must not be 0", NULL };
    CheckRefusedJSON(run, &Keys, "{\"format_version\": 1, \"objects\": [{\"id\": 0}]}", ZeroId);
    const char* const LastId[] = { "objects[0]: the id must be at most 18446744073709551614", NULL };
    CheckRefusedJSON(run, &Keys, "{\"format_version\": 1, \"objects\": [{\"id\": \"18446744073709551615\"}]}", LastId);
    const char* const DuplicateId[] = { "objects[2]: the id 1 is used twice", NULL };
    CheckRefusedJSON(run, &Keys, "{\"format_version\": 1, \"objects\": [{\"id\": 1}, {\"id\": 2}, {\"id\": 1}]}",
        DuplicateId);

    // Failures after earlier objects interned keys: the registry is given those back too (CheckRefusedJSON checks).
    const char* const MissingParent[] = { "object 2: its parent 9 was refused: no object has that id", NULL };
    CheckRefusedJSON(run, &Keys, "{\"format_version\": 1, \"objects\": [{\"id\": 1, \"tags\": [\"town:first\"], "
        "\"data\": {\"town:visits\": 3}}, {\"id\": 2, \"parent\": 9}]}", MissingParent);
    const char* const Cycle[] = { "object 2: its parent 1 was refused: it is inside the object's own subtree (a cycle)",
        NULL };
    CheckRefusedJSON(run, &Keys, "{\"format_version\": 1, \"objects\": [{\"id\": 1, \"parent\": 2, \"tags\": "
        "[\"town:cycle\"]}, {\"id\": 2, \"parent\": 1}]}", Cycle);
    const char* const Self[] = { "object 1: its parent 1 was refused: an object cannot be its own parent", NULL };
    CheckRefusedJSON(run, &Keys, "{\"format_version\": 1, \"objects\": [{\"id\": 1, \"parent\": 1}]}", Self);
    const char* const WrongType[] = { "object 1: world_object.position", NULL };
    CheckRefusedJSON(run, &Keys, "{\"format_version\": 1, \"objects\": [{\"id\": 1, \"tags\": [\"town:wrong\"], "
        "\"position\": \"up\"}]}", WrongType);
    const char* const BadData[] = { "object 1: world_object", "data.town:list", NULL };
    CheckRefusedJSON(run, &Keys, "{\"format_version\": 1, \"objects\": [{\"id\": 1, \"data\": {\"town:list\": [1, "
        "\"two\"]}}]}", BadData);
    const char* const BadEnvironment[] = { "environment: world_environment.light_pollution", NULL };
    CheckRefusedJSON(run, &Keys, "{\"format_version\": 1, \"environment\": {\"light_pollution\": 2}}", BadEnvironment);
    const char* const EnvironmentNotObject[] = { "\"environment\" must be an object", NULL };
    CheckRefusedJSON(run, &Keys, "{\"format_version\": 1, \"environment\": 5}", EnvironmentNotObject);
    const char* const ComponentsNotObject[] = { "object 1: \"components\" must be an object", NULL };
    CheckRefusedJSON(run, &Keys, "{\"format_version\": 1, \"objects\": [{\"id\": 1, \"components\": []}]}",
        ComponentsNotObject);
    const char* const ComponentNotObject[] = { "object 1: the component \"light\" must be an object", NULL };
    CheckRefusedJSON(run, &Keys, "{\"format_version\": 1, \"objects\": [{\"id\": 1, \"components\": {\"light\": 4}}]}",
        ComponentNotObject);
    const char* const BadComponent[] = { "object 1: light_component.range", NULL };
    CheckRefusedJSON(run, &Keys, "{\"format_version\": 1, \"objects\": [{\"id\": 1, \"components\": {\"light\": "
        "{\"range\": -1}}}]}", BadComponent);

    TEST_CHECK(run, (World_GetObjectCount(&Live) == 1U) && (LiveObject != NULL)
        && World_TryGetObject(&Live, WorldObject_GetId(LiveObject), &LiveObject));
    TEST_CHECK_SUCCESS(run, World_Deconstruct(&Live));
    KeyRegistry_Deconstruct(&Keys);
}

static void DeepHierarchiesAndLargeFilesAreRefused(TestRun* run)
{
    KeyRegistry Keys;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    char Item[96];

    // 64 levels are a hierarchy; a 65th is refused (a root is level 1).
    for (int Levels = 64; Levels <= 65; Levels++)
    {
        GenericBuffer_Clear(&Text);
        GenericBuffer_AppendString(&Text, (const unsigned char*)u8"{\"format_version\": 1, \"objects\": [{\"id\": 1}");
        for (int Id = 2; Id <= Levels; Id++)
        {
            snprintf(Item, sizeof(Item), ", {\"id\": %d, \"parent\": %d}", Id, Id - 1);
            GenericBuffer_AppendString(&Text, (const unsigned char*)Item);
        }
        GenericBuffer_AppendString(&Text, (const unsigned char*)u8"]}");
        GenericBuffer_NullTerminate(&Text);
        if (Levels == 64)
        {
            World Loaded;
            if (TEST_CHECK_SUCCESS(run, ReadJSONText((const char*)Text._data, &Keys, NULL, &Loaded)))
            {
                TEST_CHECK_SUCCESS(run, World_Deconstruct(&Loaded));
            }
        }
        else
        {
            const char* const TooDeep[] = { "object 65: its parent 64 was refused: the hierarchy would be deeper than 64 "
                "levels", NULL };
            CheckRefusedJSON(run, &Keys, (const char*)Text._data, TooDeep);
        }
    }

    // One object too many is refused before any object is made.
    GenericBuffer_Clear(&Text);
    GenericBuffer_AppendString(&Text, (const unsigned char*)u8"{\"format_version\": 1, \"objects\": [");
    for (size_t Id = 1U; Id <= (WORLD_FILE_MAX_OBJECT_COUNT + 1U); Id++)
    {
        snprintf(Item, sizeof(Item), "%s{\"id\": %" PRIu64 "}", (Id > 1U) ? ", " : "", (uint64_t)Id);
        GenericBuffer_AppendString(&Text, (const unsigned char*)Item);
    }
    GenericBuffer_AppendString(&Text, (const unsigned char*)u8"]}");
    GenericBuffer_NullTerminate(&Text);
    const char* const TooMany[] = { "100001 objects; a world file holds at most 100000", NULL };
    CheckRefusedJSON(run, &Keys, (const char*)Text._data, TooMany);

    // The objects' data together may count at most 16 MiB: each object here counts 64214 bytes (16 entries of a key, its
    // end and a 4000-byte string with its end), so the 262nd passes the limit and the load stops there.
    GenericBuffer_Clear(&Text);
    GenericBuffer_AppendString(&Text, (const unsigned char*)u8"{\"format_version\": 1, \"objects\": [");
    char Filler[4001];
    memset(Filler, 'x', sizeof(Filler) - 1U);
    Filler[sizeof(Filler) - 1U] = '\0';
    for (int Id = 1; Id <= 270; Id++)
    {
        snprintf(Item, sizeof(Item), "%s{\"id\": %d, \"data\": {", (Id > 1) ? ", " : "", Id);
        GenericBuffer_AppendString(&Text, (const unsigned char*)Item);
        for (int Entry = 0; Entry < 16; Entry++)
        {
            snprintf(Item, sizeof(Item), "%s\"town:part_%d\": \"", (Entry > 0) ? ", " : "", Entry);
            GenericBuffer_AppendString(&Text, (const unsigned char*)Item);
            GenericBuffer_AppendString(&Text, (const unsigned char*)Filler);
            GenericBuffer_AppendString(&Text, (const unsigned char*)u8"\"");
        }
        GenericBuffer_AppendString(&Text, (const unsigned char*)u8"}}");
    }
    GenericBuffer_AppendString(&Text, (const unsigned char*)u8"]}");
    GenericBuffer_NullTerminate(&Text);
    const char* const TooMuchData[] = { "object 262: the objects' tags and data pass the 16777216 bytes", NULL };
    CheckRefusedJSON(run, &Keys, (const char*)Text._data, TooMuchData);

    // More bytes than a world file may have are refused before parsing.
    World Target;
    WorldFileReadOptions Options = WorldFileReadOptions_Create(&Keys, NULL, NULL);
    TEST_CHECK_ERROR(run, WorldFile_ReadJSON(Text._data, WORLD_FILE_MAX_BYTE_COUNT + 1U, &Options, &Target),
        ErrorCode_InvalidJSON);
    Memory_Free(Text._data);
    KeyRegistry_Deconstruct(&Keys);
}

static void HostileGHDFIsRefusedWithNothingLeft(TestRun* run)
{
    World Source;
    KeyRegistry Keys;
    if (!BuildFullWorld(run, &Source, &Keys))
    {
        return;
    }
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Cut;
    GenericBuffer_AllocateVariable(&Cut, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, WorldFile_WriteGHDF(&Source, &Bytes)))
    {
        // Every truncation of a valid document fails cleanly, wherever it cuts.
        for (size_t Length = 0U; Length < Bytes._count; Length++)
        {
            GenericBuffer_Clear(&Cut);
            GenericBuffer_AppendRangeBytes(&Cut, (unsigned char*)Bytes._data, Length);
            CheckRefusedGHDF(run, &Keys, &Cut, NULL);
        }
        // So does a byte after the document.
        GenericBuffer_Clear(&Cut);
        GenericBuffer_AppendRangeBytes(&Cut, (unsigned char*)Bytes._data, Bytes._count);
        GenericBuffer_AppendByte(&Cut, 0U);
        const char* const Trailing[] = { "1 bytes after its document", NULL };
        CheckRefusedGHDF(run, &Keys, &Cut, Trailing);
    }

    // Strings declaring more than the data holds are refused before anything is reserved for them: 2^50 bytes (once an
    // abort), more than GHDF reads at all, and 1000 bytes with 3 present.
    const char* const Declared[] = { "declares", NULL };
    GenericBuffer_Clear(&Cut);
    BuildDeclaredStringDocument(&Cut, (uint64_t)1 << 50U, 0U);
    CheckRefusedGHDF(run, &Keys, &Cut, Declared);
    GenericBuffer_Clear(&Cut);
    BuildDeclaredStringDocument(&Cut, GHDF_MAX_STRING_LENGTH + 1U, 3U);
    CheckRefusedGHDF(run, &Keys, &Cut, Declared);
    GenericBuffer_Clear(&Cut);
    BuildDeclaredStringDocument(&Cut, 1000U, 3U);
    const char* const Remaining[] = { "declares 1000 bytes, but only 3 remain", NULL };
    CheckRefusedGHDF(run, &Keys, &Cut, Remaining);

    // Broken trees: each starts from the valid document and breaks one thing.
    GHDFObjectPool* Pool = NULL;
    GHDFCompound* Root = NULL;
    if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Pool)) && ReadGHDFTree(run, Pool, &Bytes, &Root))
    {
        GHDFCompound* First = GetFirstGHDFObject(run, Root);
        if (First != NULL)
        {
            // A negative id, then a parent that is missing, then a position that is not three floats.
            GHDFObjectValue Negative = GHDFObjectValue_CreateInt8(-1);
            TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(First, WorldFileObjectEntry_Id,
                GHDF_CreateRegularType(GHDFValueType_Int8), &Negative));
            GenericBuffer_Clear(&Cut);
            const char* const NegativeId[] = { "objects[0].id", "from 0 to", NULL };
            if (WriteGHDFTree(run, Root, &Cut))
            {
                CheckRefusedGHDF(run, &Keys, &Cut, NegativeId);
            }
            TEST_CHECK_SUCCESS(run, GameGHDF_PutUnsigned(First, WorldFileObjectEntry_Id, 1U));
            TEST_CHECK_SUCCESS(run, GameGHDF_PutUnsigned(First, 12U, 999U));
            GenericBuffer_Clear(&Cut);
            const char* const MissingParent[] = { "object 1: its parent 999 was refused: no object has that id", NULL };
            if (WriteGHDFTree(run, Root, &Cut))
            {
                CheckRefusedGHDF(run, &Keys, &Cut, MissingParent);
            }
            TEST_CHECK_SUCCESS(run, GHDFCompound_Remove(First, 12U));
            TEST_CHECK_SUCCESS(run, GameGHDF_PutUnsigned(First, 4U, 7U));
            GenericBuffer_Clear(&Cut);
            const char* const WrongPosition[] = { "object 1: world_object.position", NULL };
            if (WriteGHDFTree(run, Root, &Cut))
            {
                CheckRefusedGHDF(run, &Keys, &Cut, WrongPosition);
            }
            // A Bool row (8: is_enabled) holding another type: refused without reading it as a Bool (146 is no valid
            // bool; the sanitizer build halted on that load).
            TEST_CHECK_SUCCESS(run, GHDFCompound_Remove(First, 4U));
            GHDFObjectValue NotBool = GHDFObjectValue_CreateUInt8(146U);
            TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(First, 8U, GHDF_CreateRegularType(GHDFValueType_UInt8),
                &NotBool));
            GenericBuffer_Clear(&Cut);
            const char* const WrongBool[] = { "object 1: world_object.is_enabled", NULL };
            if (WriteGHDFTree(run, Root, &Cut))
            {
                CheckRefusedGHDF(run, &Keys, &Cut, WrongBool);
            }
            TEST_CHECK_SUCCESS(run, GHDFCompound_Remove(First, 8U));
        }
        // Objects that are not an array of compounds.
        TEST_CHECK_SUCCESS(run, GameGHDF_PutUnsigned(Root, WorldFileRootEntry_Objects, 5U));
        GenericBuffer_Clear(&Cut);
        const char* const ObjectsNotArray[] = { "The objects (entry 5) must be an array of compounds", NULL };
        if (WriteGHDFTree(run, Root, &Cut))
        {
            CheckRefusedGHDF(run, &Keys, &Cut, ObjectsNotArray);
        }
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, Root, true));
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
    }
    Memory_Free(Cut._data);
    Memory_Free(Bytes._data);
    DestroyWorld(run, &Source, &Keys);
}

static void UnknownNamesAreSkippedWithOneWarningEach(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    Logger Log;
    KeyRegistry Keys;
    if (!TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "world_file_unknown_names", &Directory))
        || !TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Directory._data)))
    {
        Memory_Free(Directory._data);
        return;
    }
    if (TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        const char* Text =
            "{\"format_version\": 1, \"flavour\": \"vanilla\",\n"
            " \"environment\": {\"fog_colour\": 3, \"light_pollution\": 0.5},\n"
            " \"objects\": [\n"
            "  {\"id\": 1, \"positon\": [1, 2, 3], \"components\": {\"collider\": {}, \"light\": {\"intensty\": 4, "
            "\"range\": 2}}},\n"
            "  {\"id\": 2, \"positon\": [1, 2, 3], \"components\": {\"collider\": {\"size\": 1}}},\n"
            "  {\"id\": 3, \"positon\": [1, 2, 3], \"name\": \"kept\"}]}";
        World Loaded;
        if (TEST_CHECK_SUCCESS(run, ReadJSONText(Text, &Keys, &Log, &Loaded)))
        {
            // What it knows is read: the light's range, the environment, the name.
            WorldComponent* Light = NULL;
            WorldObject* Third = NULL;
            TEST_CHECK(run, World_TryGetComponent(&Loaded, 1U, WorldComponentType_Light, &Light));
            TEST_CHECK(run, World_GetEnvironment(&Loaded)->LightPollution == 0.5f);
            TEST_CHECK(run, World_TryGetObject(&Loaded, 3U, &Third)
                && AreNamesEqual(WorldObject_GetName(Third), (const unsigned char*)u8"kept"));
            TEST_CHECK_SUCCESS(run, World_Deconstruct(&Loaded));
        }

        // The same in GHDF: an unknown root entry, an unknown object entry, an unknown component type; the retired
        // environment ids 2 and 3 (T23) are skipped silently.
        World Source;
        GenericBuffer Bytes;
        GenericBuffer_AllocateVariable(&Bytes, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
        GenericBuffer Edited;
        GenericBuffer_AllocateVariable(&Edited, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
        GHDFObjectPool* Pool = NULL;
        GHDFCompound* Root = NULL;
        if (TEST_CHECK_SUCCESS(run, World_Construct1(&Source, &Keys)))
        {
            CreateObject(run, &Source);
            if (TEST_CHECK_SUCCESS(run, WorldFile_WriteGHDF(&Source, &Bytes)) && TEST_CHECK_SUCCESS(run,
                GHDFObjectPool_Create(&Pool)) && ReadGHDFTree(run, Pool, &Bytes, &Root))
            {
                GHDFCompound* First = GetFirstGHDFObject(run, Root);
                GHDFCompound* Components = NULL;
                GHDFObjectValue Environment;
                TEST_CHECK_SUCCESS(run, GameGHDF_PutUnsigned(Root, 9U, 1U));
                TEST_CHECK_SUCCESS(run, GHDFCompound_Get(Root, WorldFileRootEntry_Environment, &Environment));
                TEST_CHECK_SUCCESS(run, GameGHDF_PutUnsigned(Environment.Value.Compound, 999U, 1U));
                TEST_CHECK_SUCCESS(run, GameGHDF_PutUnsigned(Environment.Value.Compound, 2U, 1U));
                if ((First != NULL) && TEST_CHECK_SUCCESS(run, GameGHDF_PutUnsigned(First, 99U, 1U))
                    && TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Pool, &Components)))
                {
                    GHDFCompound* Unknown = NULL;
                    TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Pool, &Unknown));
                    GHDFObjectValue UnknownValue = GHDFObjectValue_CreateCompound(Unknown);
                    TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Components, 11U,
                        GHDF_CreateRegularType(GHDFValueType_Compound), &UnknownValue));
                    GHDFObjectValue ComponentsValue = GHDFObjectValue_CreateCompound(Components);
                    TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(First, WorldFileObjectEntry_Components,
                        GHDF_CreateRegularType(GHDFValueType_Compound), &ComponentsValue));
                }
                World FromGHDF;
                if (WriteGHDFTree(run, Root, &Edited) && TEST_CHECK_SUCCESS(run, ReadGHDFBytes(&Edited, &Keys, &Log,
                    &FromGHDF)))
                {
                    TEST_CHECK(run, World_GetObjectCount(&FromGHDF) == 1U);
                    TEST_CHECK_SUCCESS(run, World_Deconstruct(&FromGHDF));
                }
                TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, Root, true));
            }
            TEST_CHECK_SUCCESS(run, World_Deconstruct(&Source));
        }
        if (Pool != NULL)
        {
            TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
        }
        Memory_Free(Edited._data);
        Memory_Free(Bytes._data);
        KeyRegistry_Deconstruct(&Keys);
    }
    TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));

    // One warning per name for the whole file, however often it came.
    const unsigned char* Base = Directory._data;
    TEST_CHECK(run, CountInLatestLog(run, Base, "skipped the unknown key \"flavour\" in the root.") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Base, "skipped the unknown key \"fog_colour\" in world_environment.") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Base,
        "skipped the unknown key \"positon\" in world_object (3 times; first in object 1).") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Base, "skipped the unknown component \"collider\" (2 times; first in object 1).")
        == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Base, "skipped the unknown key \"intensty\" in light_component (in object 1).")
        == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Base, "skipped the unknown entry 9 in the root.") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Base, "skipped the unknown entry 999 in world_environment.") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Base, "skipped the unknown entry 99 in world_object (in object 1).") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Base, "skipped the unknown component type 10 (in object 1).") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Base, "skipped the unknown") == 9U);
    Memory_Free(Directory._data);
}

static void NextObjectIdIsRaisedWhenTooLow(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    Logger Log;
    KeyRegistry Keys;
    if (!TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "world_file_next_id", &Directory))
        || !TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Directory._data)))
    {
        Memory_Free(Directory._data);
        return;
    }
    if (TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        struct { const char* Text; uint64_t Expected; } Cases[] =
        {
            { "{\"format_version\": 1, \"next_object_id\": 3, \"objects\": [{\"id\": 5}, {\"id\": 9}]}", 10U },
            { "{\"format_version\": 1, \"next_object_id\": 100, \"objects\": [{\"id\": 5}, {\"id\": 9}]}", 100U },
            { "{\"format_version\": 1, \"objects\": [{\"id\": 5}, {\"id\": 9}]}", 10U },
            { "{\"format_version\": 1, \"next_object_id\": 0}", 1U },
        };
        for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
        {
            World Loaded;
            if (TEST_CHECK_SUCCESS(run, ReadJSONText(Cases[Index].Text, &Keys, &Log, &Loaded)))
            {
                TEST_CHECK(run, World_GetNextObjectId(&Loaded) == Cases[Index].Expected);
                // New objects never take an id a file had.
                WorldObject* Fresh = CreateObject(run, &Loaded);
                TEST_CHECK(run, (Fresh != NULL) && (WorldObject_GetId(Fresh) == Cases[Index].Expected));
                TEST_CHECK_SUCCESS(run, World_Deconstruct(&Loaded));
            }
        }
        KeyRegistry_Deconstruct(&Keys);
    }
    TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));
    TEST_CHECK(run, CountInLatestLog(run, Directory._data,
        "the next object id 3 is not above every object id; raised to 10.") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "is not above every object id") == 2U);
    Memory_Free(Directory._data);
}


// Test cases: files.
static void FilesAndMemoryBuffersHoldTheSameWorld(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    World Source;
    KeyRegistry Keys;
    if (!TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "world_file_files", &Directory))
        || !BuildFullWorld(run, &Source, &Keys))
    {
        Memory_Free(Directory._data);
        return;
    }
    GenericBuffer WorldsDirectory;
    GenericBuffer_AllocateVariable(&WorldsDirectory, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    TEST_CHECK_SUCCESS(run, Path_Append(Directory._data, WORLD_FILE_DIRECTORY, &WorldsDirectory));
    const WorldFileFormat Formats[] = { WorldFileFormat_JSON, WorldFileFormat_GHDF };
    for (size_t Index = 0U; Index < 2U; Index++)
    {
        GenericBuffer Path;
        GenericBuffer_AllocateVariable(&Path, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
        GenericBuffer Memory;
        GenericBuffer_AllocateVariable(&Memory, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
        GenericBuffer OnDisk;
        GenericBuffer_AllocateVariable(&OnDisk, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
        bool IsJSON = Formats[Index] == WorldFileFormat_JSON;
        // The worlds directory does not exist yet: saving makes it.
        if (TEST_CHECK_SUCCESS(run, WorldFile_BuildPath(Directory._data, (const unsigned char*)u8"town", Formats[Index],
                &Path))
            && TEST_CHECK_SUCCESS(run, IsJSON ? WorldFile_SaveJSON(&Source, Path._data)
                : WorldFile_SaveGHDF(&Source, Path._data))
            && TEST_CHECK_SUCCESS(run, IsJSON ? WorldFile_WriteJSON(&Source, &Memory)
                : WorldFile_WriteGHDF(&Source, &Memory))
            && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, &OnDisk)))
        {
            TEST_CHECK(run, BytesEqual(&Memory, &OnDisk));
            TEST_CHECK(run, strstr((const char*)Path._data, IsJSON ? "town.world.json" : "town.world.ghdf") != NULL);
            World Loaded;
            WorldFileReadOptions Options = WorldFileReadOptions_Create(&Keys, NULL, NULL);
            if (TEST_CHECK_SUCCESS(run, IsJSON ? WorldFile_LoadJSON(Path._data, &Options, &Loaded)
                : WorldFile_LoadGHDF(Path._data, &Options, &Loaded)))
            {
                CheckWorldsEqual(run, &Source, &Loaded);
                // Saving again replaces the file (atomically: no temporary file stays behind).
                TEST_CHECK_SUCCESS(run, World_SetName(&Loaded, (const unsigned char*)u8"replaced"));
                TEST_CHECK_SUCCESS(run, IsJSON ? WorldFile_SaveJSON(&Loaded, Path._data)
                    : WorldFile_SaveGHDF(&Loaded, Path._data));
                TEST_CHECK_SUCCESS(run, World_Deconstruct(&Loaded));
            }
            if (TEST_CHECK_SUCCESS(run, IsJSON ? WorldFile_LoadJSON(Path._data, &Options, &Loaded)
                : WorldFile_LoadGHDF(Path._data, &Options, &Loaded)))
            {
                TEST_CHECK(run, AreNamesEqual(World_GetName(&Loaded), (const unsigned char*)u8"replaced"));
                TEST_CHECK_SUCCESS(run, World_Deconstruct(&Loaded));
            }
        }
        Memory_Free(OnDisk._data);
        Memory_Free(Memory._data);
        Memory_Free(Path._data);
    }
    size_t FileCount = 0U;
    TEST_CHECK_SUCCESS(run, Test_CountFiles(WorldsDirectory._data, &FileCount));
    TEST_CHECK(run, FileCount == 2U);

    // A missing file is a file error, and nothing is constructed.
    GenericBuffer Missing;
    GenericBuffer_AllocateVariable(&Missing, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    World Target;
    WorldFileReadOptions Options = WorldFileReadOptions_Create(&Keys, NULL, NULL);
    if (TEST_CHECK_SUCCESS(run, WorldFile_BuildPath(Directory._data, (const unsigned char*)u8"absent",
        WorldFileFormat_JSON, &Missing)))
    {
        Error Result = WorldFile_LoadJSON(Missing._data, &Options, &Target);
        TEST_CHECK(run, (Result.Code == ErrorCode_FileNotFound) || (Result.Code == ErrorCode_DirectoryNotFound));
        Error_Deconstruct(&Result);
    }
    Memory_Free(Missing._data);
    Memory_Free(WorldsDirectory._data);
    Memory_Free(Directory._data);
    DestroyWorld(run, &Source, &Keys);
}

static void TheShippedTestWorldLoadsAndIsWrittenTheSame(TestRun* run)
{
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Shipped;
    GenericBuffer_AllocateVariable(&Shipped, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Written;
    GenericBuffer_AllocateVariable(&Written, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    KeyRegistry Keys;
    World Loaded;
    if (TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, "compile/out/worlds/test.world.json", &Path))
        && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, &Shipped))
        && TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        WorldFileReadOptions Options = WorldFileReadOptions_Create(&Keys, NULL, NULL);
        if (TEST_CHECK_SUCCESS(run, WorldFile_LoadJSON(Path._data, &Options, &Loaded)))
        {
            TEST_CHECK(run, World_GetObjectCount(&Loaded) == 21U);
            TEST_CHECK(run, AreNamesEqual(World_GetName(&Loaded), (const unsigned char*)u8"test"));
            // The desk holds five parts, the lamp its light.
            WorldObject* Desk = NULL;
            WorldObject* LampLight = NULL;
            TEST_CHECK(run, World_TryGetObject(&Loaded, 15U, &Desk) && (WorldObject_GetChildCount(Desk) == 5U));
            TEST_CHECK(run, World_TryGetObject(&Loaded, 21U, &LampLight)
                && (WorldObject_GetParentId(LampLight) == 20U));
            // The file is in the writer's own form, so a save of the unchanged world diffs empty against it.
            TEST_CHECK_SUCCESS(run, WorldFile_WriteJSON(&Loaded, &Written));
            TEST_CHECK(run, BytesEqual(&Shipped, &Written));
            TEST_CHECK_SUCCESS(run, World_Deconstruct(&Loaded));
        }
        KeyRegistry_Deconstruct(&Keys);
    }
    Memory_Free(Written._data);
    Memory_Free(Shipped._data);
    Memory_Free(Path._data);
}

/* Appends the text of the @p index'th ```json block of a Markdown text to @p outText, NUL-terminated. */
static bool ExtractJSONBlock(const char* markdown, size_t index, GenericBuffer* outText)
{
    const char* Opening = "```json\n";
    const char* Start = markdown;
    for (size_t Block = 0U; Block <= index; Block++)
    {
        Start = strstr(Start, Opening);
        if (Start == NULL)
        {
            return false;
        }
        Start += strlen(Opening);
    }
    const char* End = strstr(Start, "\n```");
    return (End != NULL) && GenericBuffer_AppendRangeBytes(outText, (unsigned char*)Start, (size_t)(End - Start))
        && GenericBuffer_NullTerminate(outText);
}

/* The examples of references/world_structure.md load: the world, and the tags-and-data object inside a world. */
static void TheReferenceExamplesLoad(TestRun* run)
{
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Markdown;
    GenericBuffer_AllocateVariable(&Markdown, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Example;
    GenericBuffer_AllocateVariable(&Example, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    KeyRegistry Keys;
    if (TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, "references/world_structure.md", &Path))
        && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, &Markdown))
        && TEST_CHECK(run, GenericBuffer_NullTerminate(&Markdown))
        && TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        World Loaded;
        WorldObject* Cup = NULL;
        if (TEST_CHECK(run, ExtractJSONBlock((const char*)Markdown._data, 0U, &Example))
            && TEST_CHECK_SUCCESS(run, ReadJSONText((const char*)Example._data, &Keys, NULL, &Loaded)))
        {
            TEST_CHECK(run, World_GetObjectCount(&Loaded) == 3U);
            TEST_CHECK(run, World_TryGetObject(&Loaded, 19U, &Cup) && (WorldObject_GetParentId(Cup) == 15U));
            TEST_CHECK(run, World_GetEnvironment(&Loaded)->LightPollution == 0.7f);
            TEST_CHECK_SUCCESS(run, World_Deconstruct(&Loaded));
        }
        GenericBuffer_Clear(&Example);
        GenericBuffer_AppendString(&Example, (const unsigned char*)u8"{\"format_version\": 1, \"objects\": [");
        WorldObject* Lamp = NULL;
        if (TEST_CHECK(run, ExtractJSONBlock((const char*)Markdown._data, 1U, &Example)))
        {
            GenericBuffer_SetCount(&Example, Example._count - 1U); // the block's terminator: the wrapper goes on
            GenericBuffer_AppendString(&Example, (const unsigned char*)u8"]}");
            GenericBuffer_NullTerminate(&Example);
            if (TEST_CHECK_SUCCESS(run, ReadJSONText((const char*)Example._data, &Keys, NULL, &Loaded)))
            {
                TEST_CHECK(run, World_TryGetObject(&Loaded, 7U, &Lamp)
                    && (DataMap_GetCount(WorldObject_GetData(Lamp)) == 15U));
                TEST_CHECK_SUCCESS(run, World_Deconstruct(&Loaded));
            }
        }
        KeyRegistry_Deconstruct(&Keys);
    }
    Memory_Free(Example._data);
    Memory_Free(Markdown._data);
    Memory_Free(Path._data);
}

static void EveryTableInAWorldFileIsAtItsFirstVersion(TestRun* run)
{
    // World files map their format version to each table's data version (WorldFile.c, GetTableVersion): format 1
    // wrote every table at version 1. A table that raises its version needs a new format version and that mapping.
    KeyRegistry Keys;
    World Probe;
    TEST_CHECK(run, WorldEnvironment_GetPropertyTable()->Version == 1U);
    TEST_CHECK(run, WorldObject_GetPropertyTable()->Version == 1U);
    if (TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        if (TEST_CHECK_SUCCESS(run, World_Construct1(&Probe, &Keys)))
        {
            for (size_t Type = 0U; Type < World_GetComponentTypeCount(&Probe); Type++)
            {
                TEST_CHECK(run, World_GetComponentTypeInfo(&Probe, (WorldComponentType)Type)->Table->Version == 1U);
            }
            TEST_CHECK_SUCCESS(run, World_Deconstruct(&Probe));
        }
        KeyRegistry_Deconstruct(&Keys);
    }
}

static void WorldNamesAreValidated(TestRun* run)
{
    KeyRegistry Keys;
    World Named;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }
    if (TEST_CHECK_SUCCESS(run, World_Construct1(&Named, &Keys)))
    {
        unsigned char Long[WORLD_NAME_MAX_LENGTH + 2U];
        memset(Long, 'a', sizeof(Long) - 1U);
        Long[sizeof(Long) - 1U] = 0U;
        const unsigned char Broken[] = { 'a', 0xC3U, 0U };
        TEST_CHECK_SUCCESS(run, World_SetName(&Named, (const unsigned char*)u8"Pilsēta"));
        TEST_CHECK_ERROR(run, World_SetName(&Named, Long), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, World_SetName(&Named, Broken), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, AreNamesEqual(World_GetName(&Named), (const unsigned char*)u8"Pilsēta"));
        Long[WORLD_NAME_MAX_LENGTH] = 0U;
        TEST_CHECK_SUCCESS(run, World_SetName(&Named, Long));
        TEST_CHECK_SUCCESS(run, World_SetName(&Named, NULL));
        TEST_CHECK(run, World_GetName(&Named) == NULL);
        TEST_CHECK_SUCCESS(run, World_Deconstruct(&Named));
    }

    // An empty name reads: an empty JSON string has no storage, which the loader must not copy from.
    World Empty;
    if (TEST_CHECK_SUCCESS(run, ReadJSONText("{\"format_version\": 1, \"name\": \"\"}", &Keys, NULL, &Empty)))
    {
        const unsigned char* Name = World_GetName(&Empty);
        TEST_CHECK(run, (Name == NULL) || (Name[0] == 0U));
        TEST_CHECK_SUCCESS(run, World_Deconstruct(&Empty));
    }
    KeyRegistry_Deconstruct(&Keys);
}


// Test cases: limits.
static void AUsedUpIdCounterStillSaves(TestRun* run)
{
    KeyRegistry Keys;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));

    // An object one below the last id: the next object takes the last id and uses the counter up. The world still
    // saves (the next id is written as UINT64_MAX, which no id is), and reads back with its counter used up.
    const char* const Files[] =
    {
        "{\"format_version\": 1, \"objects\": [{\"id\": \"18446744073709551613\"}]}",
        "{\"format_version\": 1, \"next_object_id\": \"18446744073709551615\", \"objects\": [{\"id\": 4}]}",
    };
    for (size_t Index = 0U; Index < (sizeof(Files) / sizeof(Files[0])); Index++)
    {
        World Loaded;
        if (!TEST_CHECK_SUCCESS(run, ReadJSONText(Files[Index], &Keys, NULL, &Loaded)))
        {
            continue;
        }
        if (Index == 0U)
        {
            WorldObject* Last = CreateObject(run, &Loaded);
            TEST_CHECK(run, (Last != NULL) && (WorldObject_GetId(Last) == WORLD_OBJECT_ID_MAX));
        }
        WorldObject* None = NULL;
        TEST_CHECK(run, World_GetNextObjectId(&Loaded) == 0U);
        TEST_CHECK_ERROR(run, World_CreateObject(&Loaded, NULL, &None), ErrorCode_InvalidOperation);
        GenericBuffer_Clear(&Text);
        GenericBuffer_Clear(&Bytes);
        if (TEST_CHECK_SUCCESS(run, WorldFile_WriteJSON(&Loaded, &Text)) && TEST_CHECK(run, GenericBuffer_NullTerminate(&Text))
            && TEST_CHECK_SUCCESS(run, WorldFile_WriteGHDF(&Loaded, &Bytes)))
        {
            TEST_CHECK(run, strstr((const char*)Text._data, "\"next_object_id\": \"18446744073709551615\"") != NULL);
            World FromJSON;
            if (TEST_CHECK_SUCCESS(run, ReadJSONText((const char*)Text._data, &Keys, NULL, &FromJSON)))
            {
                TEST_CHECK(run, World_GetNextObjectId(&FromJSON) == 0U);
                TEST_CHECK_SUCCESS(run, World_Deconstruct(&FromJSON));
            }
            World FromGHDF;
            if (TEST_CHECK_SUCCESS(run, ReadGHDFBytes(&Bytes, &Keys, NULL, &FromGHDF)))
            {
                TEST_CHECK(run, World_GetNextObjectId(&FromGHDF) == 0U);
                TEST_CHECK_SUCCESS(run, World_Deconstruct(&FromGHDF));
            }
        }
        TEST_CHECK_SUCCESS(run, World_Deconstruct(&Loaded));
    }
    Memory_Free(Bytes._data);
    Memory_Free(Text._data);
    KeyRegistry_Deconstruct(&Keys);
}

static void TheTreeLimitRefusesLargeFiles(TestRun* run)
{
    KeyRegistry Keys;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }
    // 200 small objects: well under the default limit, well over 64 KiB as the readers estimate them.
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer_AppendString(&Text, (const unsigned char*)u8"{\"format_version\": 1, \"objects\": [");
    for (int Id = 1; Id <= 200; Id++)
    {
        char Item[96];
        snprintf(Item, sizeof(Item), "%s{\"id\": %d, \"name\": \"crate_%d\", \"position\": [1, 2, 3]}", (Id > 1) ? ", " : "",
            Id, Id);
        GenericBuffer_AppendString(&Text, (const unsigned char*)Item);
    }
    GenericBuffer_AppendString(&Text, (const unsigned char*)u8"]}");

    WorldFileReadOptions Options = WorldFileReadOptions_Create(&Keys, NULL, NULL);
    TEST_CHECK(run, Options.MaxTreeByteCount == WORLD_FILE_MAX_TREE_BYTES);
    World Loaded;
    if (TEST_CHECK_SUCCESS(run, WorldFile_ReadJSON(Text._data, Text._count, &Options, &Loaded)))
    {
        TEST_CHECK(run, World_GetObjectCount(&Loaded) == 200U);
        TEST_CHECK_SUCCESS(run, WorldFile_WriteGHDF(&Loaded, &Bytes));
        TEST_CHECK_SUCCESS(run, World_Deconstruct(&Loaded));
    }
    size_t KeysBefore = KeyRegistry_GetCount(&Keys);
    World Target;
    Options.MaxTreeByteCount = 65536U;
    const char* const TooLarge[] = { "would take more than 65536 bytes", NULL };
    TestText_CheckErrorMessage(run, WorldFile_ReadJSON(Text._data, Text._count, &Options, &Target), ErrorCode_InvalidJSON,
        TooLarge);
    TestText_CheckErrorMessage(run, WorldFile_ReadGHDF(Bytes._data, Bytes._count, &Options, &Target),
        ErrorCode_Deserialize, TooLarge);
    TEST_CHECK(run, KeyRegistry_GetCount(&Keys) == KeysBefore);
    Options.MaxTreeByteCount = 0U;
    TEST_CHECK_ERROR(run, WorldFile_ReadJSON(Text._data, Text._count, &Options, &Target), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, WorldFile_ReadGHDF(Bytes._data, Bytes._count, &Options, &Target), ErrorCode_IllegalArgument);
    Memory_Free(Bytes._data);
    Memory_Free(Text._data);
    KeyRegistry_Deconstruct(&Keys);
}

static void AWorldAtTheObjectLimitSavesAndLoads(TestRun* run)
{
    // The most objects a file holds, all roots, created in descending id order: the writer sorts the roots by id, and
    // the old quicksort recursed once per element on such input (a 100 000-object save overflowed the stack). Reading
    // them back borrows and returns a node per value from the readers' pools, which grew quadratic before T09.
    KeyRegistry Keys;
    World Source;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }
    if (!TEST_CHECK_SUCCESS(run, World_Construct1(&Source, &Keys)))
    {
        KeyRegistry_Deconstruct(&Keys);
        return;
    }
    bool IsEveryObjectMade = true;
    for (size_t Index = 0U; (Index < WORLD_FILE_MAX_OBJECT_COUNT) && IsEveryObjectMade; Index++)
    {
        WorldObject* Object = NULL;
        Error Result = World_CreateObjectWithId(&Source, NULL, (uint64_t)(WORLD_FILE_MAX_OBJECT_COUNT - Index), &Object);
        IsEveryObjectMade = Result.Code == ErrorCode_Success;
        Error_Deconstruct(&Result);
    }
    TEST_CHECK(run, IsEveryObjectMade);
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, WORLD_FILE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, WorldFile_WriteJSON(&Source, &Text)) && TEST_CHECK_SUCCESS(run, WorldFile_WriteGHDF(&Source,
        &Bytes)))
    {
        // Written in id order: the first object is 1.
        size_t TextLength = Text._count;
        TEST_CHECK(run, GenericBuffer_NullTerminate(&Text)
            && (strstr((const char*)Text._data, "\"objects\": [\n        {\n            \"id\": 1\n") != NULL));
        WorldFileReadOptions Options = WorldFileReadOptions_Create(&Keys, NULL, NULL);
        World FromJSON;
        if (TEST_CHECK_SUCCESS(run, WorldFile_ReadJSON(Text._data, TextLength, &Options, &FromJSON)))
        {
            TEST_CHECK(run, World_GetObjectCount(&FromJSON) == WORLD_FILE_MAX_OBJECT_COUNT);
            TEST_CHECK_SUCCESS(run, World_Deconstruct(&FromJSON));
        }
        World FromGHDF;
        if (TEST_CHECK_SUCCESS(run, WorldFile_ReadGHDF(Bytes._data, Bytes._count, &Options, &FromGHDF)))
        {
            TEST_CHECK(run, World_GetObjectCount(&FromGHDF) == WORLD_FILE_MAX_OBJECT_COUNT);
            TEST_CHECK_SUCCESS(run, World_Deconstruct(&FromGHDF));
        }
    }
    Memory_Free(Bytes._data);
    Memory_Free(Text._data);
    TEST_CHECK_SUCCESS(run, World_Deconstruct(&Source));
    KeyRegistry_Deconstruct(&Keys);
}


// Fields: the suite.
static const TestCase WorldFileTestCases[] =
{
    { "a_full_world_round_trips_through_json", AFullWorldRoundTripsThroughJSON },
    { "a_full_world_round_trips_through_ghdf", AFullWorldRoundTripsThroughGHDF },
    { "json_and_ghdf_hold_the_same_world", JSONAndGHDFHoldTheSameWorld },
    { "written_json_leaves_out_defaults_in_table_order", WrittenJSONLeavesOutDefaultsInTableOrder },
    { "objects_are_written_parents_first", ObjectsAreWrittenParentsFirst },
    { "a_failed_write_appends_nothing", AFailedWriteAppendsNothing },
    { "hostile_json_is_refused_with_nothing_left", HostileJSONIsRefusedWithNothingLeft },
    { "deep_hierarchies_and_large_files_are_refused", DeepHierarchiesAndLargeFilesAreRefused },
    { "hostile_ghdf_is_refused_with_nothing_left", HostileGHDFIsRefusedWithNothingLeft },
    { "unknown_names_are_skipped_with_one_warning_each", UnknownNamesAreSkippedWithOneWarningEach },
    { "next_object_id_is_raised_when_too_low", NextObjectIdIsRaisedWhenTooLow },
    { "files_and_memory_buffers_hold_the_same_world", FilesAndMemoryBuffersHoldTheSameWorld },
    { "the_shipped_test_world_loads_and_is_written_the_same", TheShippedTestWorldLoadsAndIsWrittenTheSame },
    { "the_reference_examples_load", TheReferenceExamplesLoad },
    { "every_table_in_a_world_file_is_at_its_first_version", EveryTableInAWorldFileIsAtItsFirstVersion },
    { "world_names_are_validated", WorldNamesAreValidated },
    { "a_used_up_id_counter_still_saves", AUsedUpIdCounterStillSaves },
    { "the_tree_limit_refuses_large_files", TheTreeLimitRefusesLargeFiles },
    { "a_world_at_the_object_limit_saves_and_loads", AWorldAtTheObjectLimitSavesAndLoads },
};


// Public functions.
void WorldFileTests_Run(TestRun* run)
{
    TestRun_RunSuite(run, "WorldFile", WorldFileTestCases, sizeof(WorldFileTestCases) / sizeof(WorldFileTestCases[0]));
}
