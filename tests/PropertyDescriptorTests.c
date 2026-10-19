#include "TestSuites.h"
#include "TestText.h"
#include "PropertyDescriptor.h"
#include "GameClock.h"
#include "WorldEnvironment.h"
#include "Logger.h"
#include "wr/WRFileSystem.h"
#include "wr/WRPath.h"
#include <math.h>
#include <string.h>


// Macros.
#define TEST_PI 3.14159265358979323846
/** The test record's string limits. */
#define TEST_TITLE_MAX_LENGTH ((size_t)16)
#define TEST_NOTE_MAX_LENGTH ((size_t)64)
#define TEST_MODEL_MAX_LENGTH ((size_t)32)
/** Bytes of padding that push the big record past the readers' inline scratch storage. */
#define TEST_BIG_RECORD_PADDING ((size_t)2048)
/** Day numbers of the record's default due date, 2026-10-18, and of a later one, 2027-01-20. */
#define TEST_DUE_DEFAULT ((int64_t)739906)
#define TEST_DUE_LATER ((int64_t)740000)
/** Initial capacity of text buffers; they grow as needed. */
#define TEST_TEXT_CAPACITY ((size_t)256)


// Types.
typedef enum TestTierEnum
{
    TestTier_Both = 0,
    TestTier_Near = 1,
    TestTier_Far = 2
} TestTier;

/* One field of every property type, a read-only one, a runtime-only one, and two fields the table does not describe
 * (the AfterSet hook's record). */
typedef struct TestRecordStruct
{
    bool IsEnabled;
    int32_t Count;
    uint32_t Mask;
    uint64_t Seed;
    float Weight;
    float Heading;
    Vector2 Pivot;
    Vector3 Position;
    Color Tint;
    RenderColor Shade;
    unsigned char* Title;
    unsigned char* Note;
    unsigned char* Model;
    TestTier Tier;
    uint8_t SmallTier;
    uint64_t Target;
    uint64_t Serial;
    double Elapsed;
    int64_t Due;
    int32_t Revision;
    int32_t AfterSetCount;
    const PropertyDescriptor* LastChanged;
} TestRecord;

/* A struct bigger than the readers' inline scratch, so reads use a heap scratch. */
typedef struct TestBigRecordStruct
{
    unsigned char Padding[TEST_BIG_RECORD_PADDING];
    float Value;
    unsigned char* Name;
} TestBigRecord;


// Fields.
static const PropertyEnumEntry TierEntries[] =
{
    { .Name = u8"both", .Value = TestTier_Both },
    { .Name = u8"near", .Value = TestTier_Near },
    { .Name = u8"far", .Value = TestTier_Far },
};

static const PropertyEnumTable TierEnum =
{
    .Name = u8"test_tier", .Entries = TierEntries, .EntryCount = sizeof(TierEntries) / sizeof(TierEntries[0])
};

static void RecordAfterSet(void* instance, const PropertyDescriptor* descriptor)
{
    TestRecord* Record = instance;
    Record->AfterSetCount++;
    Record->LastChanged = descriptor;
}

static const PropertyDescriptor RecordProperties[] =
{
    { .Key = u8"is_enabled", .WireId = 1U, .Type = PropertyType_Bool, PROPERTY_MEMBER(TestRecord, IsEnabled),
        .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING, .SinceVersion = 1U },
    { .Key = u8"count", .WireId = 2U, .Type = PropertyType_Int32, PROPERTY_MEMBER(TestRecord, Count),
        .Default.Int32Value = 5, .Rules = PROPERTY_RULES_BETWEEN(0.0, 100.0), .Flags = PROPERTY_FLAGS_SETTING,
        .SinceVersion = 1U },
    { .Key = u8"mask", .WireId = 3U, .Type = PropertyType_UInt32, PROPERTY_MEMBER(TestRecord, Mask),
        .Default.UInt32Value = 0xFFU, .Flags = PROPERTY_FLAGS_SETTING, .SinceVersion = 1U },
    { .Key = u8"seed", .WireId = 4U, .Type = PropertyType_UInt64, PROPERTY_MEMBER(TestRecord, Seed),
        .Default.UInt64Value = 7U, .Flags = PROPERTY_FLAGS_SETTING, .SinceVersion = 1U },
    { .Key = u8"weight", .WireId = 5U, .Type = PropertyType_Float, PROPERTY_MEMBER(TestRecord, Weight),
        .Default.FloatValue = 1.5f, .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING,
        .SinceVersion = 1U },
    { .Key = u8"heading", .WireId = 6U, .Type = PropertyType_Angle, PROPERTY_MEMBER(TestRecord, Heading),
        .Default.FloatValue = 0.25f, .Flags = PROPERTY_FLAGS_SETTING, .SinceVersion = 1U },
    { .Key = u8"pivot", .WireId = 7U, .Type = PropertyType_Vector2, PROPERTY_MEMBER(TestRecord, Pivot),
        .Default.Vector2Value = { 0.5f, 0.0f }, .Rules = PROPERTY_RULES_BETWEEN(-1.0, 1.0),
        .Flags = PROPERTY_FLAGS_SETTING, .SinceVersion = 1U },
    { .Key = u8"position", .WireId = 8U, .Type = PropertyType_Vector3, PROPERTY_MEMBER(TestRecord, Position),
        .Default.Vector3Value = { 0.0f, 0.0f, 0.0f }, .Flags = PROPERTY_FLAGS_SETTING, .SinceVersion = 1U },
    { .Key = u8"tint", .WireId = 9U, .Type = PropertyType_Color, PROPERTY_MEMBER(TestRecord, Tint),
        .Default.ColorValue = { 255, 255, 255, 255 }, .Flags = PROPERTY_FLAGS_SETTING, .SinceVersion = 1U },
    { .Key = u8"shade", .WireId = 10U, .Type = PropertyType_RenderColor, PROPERTY_MEMBER(TestRecord, Shade),
        .Default.RenderColorValue = { { 200, 100, 50, 255 }, 1.0f, 1.0f }, .Flags = PROPERTY_FLAGS_SETTING,
        .SinceVersion = 1U },
    { .Key = u8"title", .WireId = 11U, .Type = PropertyType_String, PROPERTY_MEMBER(TestRecord, Title),
        .Default.StringValue = u8"untitled", .Rules = PROPERTY_RULES_STRING(TEST_TITLE_MAX_LENGTH, false),
        .Flags = PROPERTY_FLAGS_SETTING, .SinceVersion = 1U },
    { .Key = u8"note", .WireId = 12U, .Type = PropertyType_String, PROPERTY_MEMBER(TestRecord, Note),
        .Default.StringValue = NULL, .Rules = PROPERTY_RULES_STRING(TEST_NOTE_MAX_LENGTH, true),
        .Flags = PROPERTY_FLAGS_SETTING, .SinceVersion = 1U },
    { .Key = u8"model", .WireId = 13U, .Type = PropertyType_AssetName, PROPERTY_MEMBER(TestRecord, Model),
        .Default.StringValue = NULL, .Rules = PROPERTY_RULES_STRING(TEST_MODEL_MAX_LENGTH, true),
        .Flags = PROPERTY_FLAGS_SETTING, .SinceVersion = 1U },
    { .Key = u8"tier", .WireId = 14U, .Type = PropertyType_Enum, PROPERTY_MEMBER(TestRecord, Tier),
        .Default.EnumValue = TestTier_Near, .EnumTable = &TierEnum, .Flags = PROPERTY_FLAGS_SETTING, .SinceVersion = 1U },
    { .Key = u8"small_tier", .WireId = 15U, .Type = PropertyType_Enum, PROPERTY_MEMBER(TestRecord, SmallTier),
        .Default.EnumValue = TestTier_Far, .EnumTable = &TierEnum, .Flags = PROPERTY_FLAGS_SETTING, .SinceVersion = 1U },
    { .Key = u8"target", .WireId = 16U, .Type = PropertyType_ObjectId, PROPERTY_MEMBER(TestRecord, Target),
        .Default.ObjectIdValue = 0U, .Flags = PROPERTY_FLAGS_SETTING, .SinceVersion = 1U },
    { .Key = u8"serial", .WireId = 17U, .Type = PropertyType_UInt64, PROPERTY_MEMBER(TestRecord, Serial),
        .Default.UInt64Value = 0U, .Flags = PropertyFlags_Persistent | PropertyFlags_ReadOnly, .SinceVersion = 1U },
    { .Key = u8"revision", .WireId = 18U, .Type = PropertyType_Int32, PROPERTY_MEMBER(TestRecord, Revision),
        .Default.Int32Value = 0, .Flags = PropertyFlags_None, .SinceVersion = 1U },
    { .Key = u8"elapsed", .WireId = 19U, .Type = PropertyType_Double, PROPERTY_MEMBER(TestRecord, Elapsed),
        .Default.DoubleValue = 0.5, .Rules = PROPERTY_RULES_HALF_OPEN(0.0, 86400.0), .Flags = PROPERTY_FLAGS_SETTING,
        .SinceVersion = 1U },
    { .Key = u8"due", .WireId = 20U, .Type = PropertyType_Date, PROPERTY_MEMBER(TestRecord, Due),
        .Default.DateValue = TEST_DUE_DEFAULT, .Flags = PROPERTY_FLAGS_SETTING, .SinceVersion = 1U },
};

#define RECORD_PROPERTY_COUNT (sizeof(RecordProperties) / sizeof(RecordProperties[0]))

static const uint16_t RecordRetiredWireIds[] = { 40U, 41U };

static const PropertyTable RecordTable =
{
    .Name = u8"test_record",
    .Version = 1U,
    .StructSize = sizeof(TestRecord),
    .Descriptors = RecordProperties,
    .DescriptorCount = RECORD_PROPERTY_COUNT,
    .RetiredWireIds = RecordRetiredWireIds,
    .RetiredWireIdCount = sizeof(RecordRetiredWireIds) / sizeof(RecordRetiredWireIds[0]),
    .AfterSet = RecordAfterSet,
    .Migrate = NULL
};

static const PropertyDescriptor BigRecordProperties[] =
{
    { .Key = u8"value", .WireId = 1U, .Type = PropertyType_Float, PROPERTY_MEMBER(TestBigRecord, Value),
        .Default.FloatValue = 2.0f, .Flags = PROPERTY_FLAGS_SETTING, .SinceVersion = 1U },
    { .Key = u8"name", .WireId = 2U, .Type = PropertyType_String, PROPERTY_MEMBER(TestBigRecord, Name),
        .Default.StringValue = u8"big", .Rules = PROPERTY_RULES_STRING(TEST_NOTE_MAX_LENGTH, false),
        .Flags = PROPERTY_FLAGS_SETTING, .SinceVersion = 1U },
};

static const PropertyTable BigRecordTable =
{
    .Name = u8"test_big_record", .Version = 1U, .StructSize = sizeof(TestBigRecord), .Descriptors = BigRecordProperties,
    .DescriptorCount = sizeof(BigRecordProperties) / sizeof(BigRecordProperties[0])
};


// Static functions: helpers.
static bool AreStringsEqual(const unsigned char* a, const unsigned char* b)
{
    if ((a == NULL) || (b == NULL))
    {
        return a == b;
    }
    return strcmp((const char*)a, (const char*)b) == 0;
}

static bool AreFloatsIdentical(float a, float b)
{
    return memcmp(&a, &b, sizeof(float)) == 0;
}

static bool CheckConstruct(TestRun* run, TestRecord* record)
{
    return TEST_CHECK_SUCCESS(run, Properties_Construct(&RecordTable, record));
}

/* Sets every described field to a value that is not its default. */
static void FillRecord(TestRun* run, TestRecord* record)
{
    record->IsEnabled = false;
    record->Count = 42;
    record->Mask = 0xDEADBEEFU;
    record->Seed = 123456789012345ULL;
    record->Weight = 0.1f;
    record->Heading = -2.5f;
    record->Pivot = (Vector2){ -0.25f, 0.75f };
    record->Position = (Vector3){ 1.0f / 3.0f, -1234.5f, 1e-7f };
    record->Tint = (Color){ 10, 20, 30, 40 };
    record->Shade = (RenderColor){ { 1, 2, 3, 255 }, 0.5f, 0.25f };
    TEST_CHECK_SUCCESS(run, Properties_Set(&RecordTable, record, u8"title",
        &(PropertyValue){ .Type = PropertyType_String, .Data.StringValue = u8"Café" }));
    TEST_CHECK_SUCCESS(run, Properties_Set(&RecordTable, record, u8"note",
        &(PropertyValue){ .Type = PropertyType_String, .Data.StringValue = u8"a note" }));
    TEST_CHECK_SUCCESS(run, Properties_Set(&RecordTable, record, u8"model",
        &(PropertyValue){ .Type = PropertyType_AssetName, .Data.StringValue = u8"desk_model" }));
    record->Tier = TestTier_Far;
    record->SmallTier = TestTier_Both;
    record->Target = 99U;
    record->Serial = 5U;
    record->Elapsed = 1.0 / 3.0; // every bit of a double must survive JSON text
    record->Due = TEST_DUE_LATER;
    record->Revision = 3;
}

/* Serializes a compound to text and parses it back, so a test goes through real JSON text. */
static bool ReparseCompound(TestRun* run, JSONObjectPool* pool, JSONCompound* compound, JSONObjectValue* outValue)
{
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, TEST_TEXT_CAPACITY, sizeof(unsigned char));
    JSONObjectValue Value = JSONObjectValue_CreateCompound(compound);
    bool IsDone = TEST_CHECK_SUCCESS(run, JSON_Serialize(&Value, &Text, JSONSerializeFlags_None))
        && TEST_CHECK_SUCCESS(run, JSON_Deserialize(pool, &Text, outValue))
        && TEST_CHECK(run, outValue->Type == JSONValueType_Compound);
    Memory_Free(Text._data);
    return IsDone;
}

/* Parses @p text into @p record with @p options. */
static Error ReadRecordFromText(TestRun* run, JSONObjectPool* pool, const char* text, TestRecord* record,
    const PropertyReadOptions* options)
{
    JSONObjectValue Value;
    if (!TestText_ParseJSON(run, pool, text, &Value))
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "the test's JSON did not parse");
    }
    Error Result = Properties_ReadJSON(&RecordTable, Value.Value.Compound, record, options);
    TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(pool, &Value));
    return Result;
}

static Error ReadTextFile(const unsigned char* directory, const char* relativePath, GenericBuffer* outText)
{
    GenericBuffer FilePath;
    GenericBuffer_AllocateVariable(&FilePath, TEST_TEXT_CAPACITY, sizeof(unsigned char));
    Error Result = Path_Append(directory, (const unsigned char*)relativePath, &FilePath);
    if (Result.Code == ErrorCode_Success)
    {
        Result = FileSystem_ReadAllBytes(FilePath._data, outText);
    }
    if ((Result.Code == ErrorCode_Success) && !GenericBuffer_NullTerminate(outText))
    {
        Result = Error_Construct2(ErrorCode_BufferTooSmall, "Could not terminate the text.");
    }
    Memory_Free(FilePath._data);
    return Result;
}


// Static functions: GHDF tree comparison.
static bool AreGHDFCompoundsEqual(GHDFCompound* a, GHDFCompound* b, size_t depth);

static bool AreGHDFScalarsEqual(GHDFValueType type, const GHDFObjectValue* a, const GHDFObjectValue* b, size_t depth)
{
    switch (type)
    {
        case GHDFValueType_UInt8: return a->Value.UInt8 == b->Value.UInt8;
        case GHDFValueType_Int8: return a->Value.Int8 == b->Value.Int8;
        case GHDFValueType_Int16: return a->Value.Int16 == b->Value.Int16;
        case GHDFValueType_UInt16: return a->Value.UInt16 == b->Value.UInt16;
        case GHDFValueType_Int32: return a->Value.Int32 == b->Value.Int32;
        case GHDFValueType_UInt32: return a->Value.UInt32 == b->Value.UInt32;
        case GHDFValueType_Int64: return a->Value.Int64 == b->Value.Int64;
        case GHDFValueType_UInt64: return a->Value.UInt64 == b->Value.UInt64;
        case GHDFValueType_Float: return AreFloatsIdentical(a->Value.Float, b->Value.Float);
        case GHDFValueType_Double: return memcmp(&a->Value.Double, &b->Value.Double, sizeof(double)) == 0;
        case GHDFValueType_Boolean: return a->Value.Boolean == b->Value.Boolean;
        case GHDFValueType_EncodedInteger: return a->Value.EncodedInteger == b->Value.EncodedInteger;
        case GHDFValueType_String:
            return (a->Value.String->_count == b->Value.String->_count)
                && (memcmp(a->Value.String->_data, b->Value.String->_data, a->Value.String->_count) == 0);
        case GHDFValueType_Compound:
            return AreGHDFCompoundsEqual(a->Value.Compound, b->Value.Compound, depth + 1U);
        case GHDFValueType_None:
            return false;
    }
    return false;
}

static bool AreGHDFArraysEqual(GHDFArray* a, GHDFArray* b, size_t depth)
{
    GHDFValueType Type = GHDFArray_GetElementType(a);
    size_t Count = GHDFArray_GetElementCount(a);
    if ((Type != GHDFArray_GetElementType(b)) || (Count != GHDFArray_GetElementCount(b)))
    {
        return false;
    }
    for (size_t Index = 0; Index < Count; Index++)
    {
        GHDFObjectValue First = { .Type = GHDFValueType_None };
        GHDFObjectValue Second = { .Type = GHDFValueType_None };
        Error FirstResult = GHDFArray_Get(a, Index, &First);
        Error SecondResult = GHDFArray_Get(b, Index, &Second);
        bool AreRead = (FirstResult.Code == ErrorCode_Success) && (SecondResult.Code == ErrorCode_Success);
        Error_Deconstruct(&FirstResult);
        Error_Deconstruct(&SecondResult);
        if (!AreRead || !AreGHDFScalarsEqual(Type, &First, &Second, depth))
        {
            return false;
        }
    }
    return true;
}

/* Same entry ids with the same types and values, recursively (a semantic comparison: compounds are hash maps). */
static bool AreGHDFCompoundsEqual(GHDFCompound* a, GHDFCompound* b, size_t depth)
{
    if ((depth > 8U) || (GHDFCompound_GetEntryCount(a) != GHDFCompound_GetEntryCount(b)))
    {
        return false;
    }
    CollectionEnumerator* Enumerator = ICollection_CreateEnumerator(GHDFCompound_GetEntryCollection(a));
    if (Enumerator == NULL)
    {
        return false;
    }
    bool AreEqual = true;
    bool HasNext = false;
    Error Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    while ((Result.Code == ErrorCode_Success) && HasNext && AreEqual)
    {
        GHDFCompoundEntry Entry;
        Result = CollectionEnumerator_NextByValue(Enumerator, &Entry);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        GHDFObjectValue Other = { .Type = GHDFValueType_None };
        Error GetResult = GHDFCompound_GetVerified(b, Entry.Id, Entry.EntryType, &Other);
        AreEqual = (GetResult.Code == ErrorCode_Success) && (Entry.EntryType.IsArray
            ? AreGHDFArraysEqual(Entry.Value.Value.Array, Other.Value.Array, depth)
            : AreGHDFScalarsEqual(Entry.EntryType.ValueType, &Entry.Value, &Other, depth));
        Error_Deconstruct(&GetResult);
        Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    }
    CollectionEnumerator_Destroy(Enumerator);
    bool IsWalked = Result.Code == ErrorCode_Success;
    Error_Deconstruct(&Result);
    return AreEqual && IsWalked;
}


// Static functions: the hand-written environment encoder before T04, frozen as the reference for the GHDF output.
static Error ReferencePutFloat(GHDFCompound* compound, GHDFEntryID id, float value)
{
    GHDFObjectValue Value = GHDFObjectValue_CreateFloat(value);
    return GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(GHDFValueType_Float), &Value);
}

static Error ReferencePutBool(GHDFCompound* compound, GHDFEntryID id, bool value)
{
    GHDFObjectValue Value = GHDFObjectValue_CreateBoolean(value);
    return GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(GHDFValueType_Boolean), &Value);
}

static Error ReferencePutUInt64(GHDFCompound* compound, GHDFEntryID id, uint64_t value)
{
    GHDFObjectValue Value = GHDFObjectValue_CreateUInt64(value);
    return GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(GHDFValueType_UInt64), &Value);
}

static Error ReferencePutColor(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id, Color color)
{
    GHDFArray* Array = NULL;
    Error Result = GHDFObjectPool_BorrowArray(pool, GHDFValueType_UInt8, &Array);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const unsigned char Components[4] = { color.r, color.g, color.b, color.a };
    for (size_t Index = 0; (Index < 4U) && (Result.Code == ErrorCode_Success); Index++)
    {
        GHDFObjectValue Value = GHDFObjectValue_CreateUInt8(Components[Index]);
        Result = GHDFArray_AddValue(Array, &Value);
    }
    if (Result.Code == ErrorCode_Success)
    {
        GHDFObjectValue ArrayValue = GHDFObjectValue_CreateArray(Array, GHDFValueType_UInt8);
        Result = GHDFCompound_SetValue(compound, id, GHDF_CreateArrayType(GHDFValueType_UInt8), &ArrayValue);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnArray(pool, Array, true);
        Error_Deconstruct(&ReturnResult);
    }
    return Result;
}

/* The pre-T04 EncodeEnvironment of source/WorldEncoder.c, with its field ids as literals. Ids 2 and 3 held the
 * day-night cycle switch and day length that T23 retired: they are written with their old defaults, as data saved
 * before T23 would hold them. The old encoder had no date (T23's id 30). */
static Error ReferenceEncodeEnvironment(GHDFObjectPool* pool, GHDFCompound* compound, const WorldEnvironment* environment)
{
    Error Result = ReferencePutFloat(compound, 1, environment->TimeOfDay);
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutBool(compound, 2, true); }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutFloat(compound, 3, 1200.0f); }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutFloat(compound, 23, environment->SunAngle); }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutFloat(compound, 4, environment->SkyTurbidity); }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutColor(pool, compound, 5, environment->SkyGroundAlbedo); }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutColor(pool, compound, 6, environment->SkyTint); }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutColor(pool, compound, 7, environment->SunColor); }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutFloat(compound, 8, environment->SunIntensity); }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutFloat(compound, 9, environment->SunSizeMultiplier); }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutUInt64(compound, 10, environment->StarSeed); }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutFloat(compound, 11, environment->StarDensity); }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutFloat(compound, 12, environment->StarBrightness); }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReferencePutColor(pool, compound, 13, environment->AmbientSkylightColor);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReferencePutFloat(compound, 14, environment->AmbientSkylightIntensity);
    }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutColor(pool, compound, 15, environment->FogColor); }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutFloat(compound, 16, environment->FogStrength); }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutBool(compound, 17, environment->IsBloomEnabled); }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutFloat(compound, 18, environment->BloomStrength); }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutBool(compound, 19, environment->AreSunshaftsEnabled); }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutFloat(compound, 20, environment->SunshaftStrength); }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutBool(compound, 21, environment->AreShadowsEnabled); }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutFloat(compound, 22, environment->ShadowStrength); }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReferencePutBool(compound, 24, environment->IsAmbientOcclusionEnabled);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReferencePutFloat(compound, 25, environment->AmbientOcclusionStrength);
    }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutFloat(compound, 26, environment->LightPollution); }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReferencePutColor(pool, compound, 27, environment->LightPollutionColor);
    }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutFloat(compound, 28, environment->MilkyWayBrightness); }
    if (Result.Code == ErrorCode_Success) { Result = ReferencePutFloat(compound, 29, environment->StarTwinkle); }
    return Result;
}

/* Sets every environment setting to a value that is not its default. */
static void FillEnvironment(WorldEnvironment* environment)
{
    environment->TimeOfDay = 0.3f;
    environment->Date = TEST_DUE_LATER;
    environment->SunAngle = 0.7f;
    environment->SkyTurbidity = 4.5f;
    environment->SkyGroundAlbedo = (Color){ 1, 2, 3, 4 };
    environment->SkyTint = (Color){ 10, 20, 30, 255 };
    environment->SunColor = (Color){ 250, 240, 230, 200 };
    environment->SunIntensity = 1.25f;
    environment->SunSizeMultiplier = 2.0f;
    environment->StarSeed = 0xFFFFFFFFFFFFFFF0ULL;
    environment->StarDensity = 0.5f;
    environment->StarBrightness = 3.0f;
    environment->StarTwinkle = 0.6f;
    environment->MilkyWayBrightness = 1.5f;
    environment->LightPollution = 0.9f;
    environment->LightPollutionColor = (Color){ 200, 100, 50, 255 };
    environment->AmbientSkylightColor = (Color){ 128, 128, 255, 255 };
    environment->AmbientSkylightIntensity = 0.8f;
    environment->FogColor = (Color){ 5, 6, 7, 8 };
    environment->FogStrength = 0.0f;
    environment->IsBloomEnabled = false;
    environment->BloomStrength = 0.2f;
    environment->AreSunshaftsEnabled = false;
    environment->SunshaftStrength = 0.3f;
    environment->AreShadowsEnabled = false;
    environment->ShadowStrength = 0.4f;
    environment->IsAmbientOcclusionEnabled = false;
    environment->AmbientOcclusionStrength = 16.0f;
}

/* The settings added after the hand-written encoder's time: T03's (the moon, the seasons, the sky texture names, ids 31
 * to 43) and the night vision's (44 and 45), every one off its default. The names go through the table, which clones
 * them and frees the old ones. */
static const uint16_t LaterSettingWireIds[] =
    { 31U, 32U, 33U, 34U, 35U, 36U, 37U, 38U, 39U, 40U, 41U, 42U, 43U, 44U, 45U };

static void FillLaterSettings(TestRun* run, WorldEnvironment* environment)
{
    environment->NightVisionStrength = 0.25f;
    environment->NightVisionBlueShift = 0.75f;
    environment->IsMoonEnabled = false;
    environment->MoonPhaseSource = WorldMoonPhaseSource_Fixed;
    environment->MoonPhase = 0.125f;
    environment->MoonPhaseOffsetDays = -3.5f;
    environment->MoonCycleDays = 27.25f;
    environment->MoonOrbitTilt = 0.2f;
    environment->MoonSizeMultiplier = 8.0f;
    environment->MoonBrightness = 0.5f;
    environment->MoonIntensity = 0.03f;
    environment->MoonColor = (Color){ 255, 200, 150, 255 };
    environment->AreSeasonsEnabled = true;
    const PropertyTable* Table = WorldEnvironment_GetPropertyTable();
    TEST_CHECK_SUCCESS(run, Properties_Set(Table, environment, u8"moon_texture",
        &(PropertyValue){ .Type = PropertyType_AssetName, .Data.StringValue = u8"pack.red_moon" }));
    TEST_CHECK_SUCCESS(run, Properties_Set(Table, environment, u8"milky_way_texture",
        &(PropertyValue){ .Type = PropertyType_AssetName, .Data.StringValue = u8"alien_galaxy" }));
}


// Static functions: tests.
static void TablesPassTheirCheck(TestRun* run)
{
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(&RecordTable));
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(&BigRecordTable));
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(WorldEnvironment_GetPropertyTable()));
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(GameClock_GetPropertyTable()));
    TEST_CHECK_ERROR(run, PropertyTable_Check(NULL), ErrorCode_IllegalArgument);
}

/* Copies the record table, breaks the descriptor at @p index with @p breaker, and expects the check to fail with a
 * message containing @p fragment. */
static void CheckBrokenDescriptor(TestRun* run, size_t index, void (*breaker)(PropertyDescriptor*), const char* fragment)
{
    PropertyDescriptor Broken[RECORD_PROPERTY_COUNT];
    Memory_Copy(RecordProperties, Broken, sizeof(Broken));
    breaker(&Broken[index]);
    PropertyTable Table = RecordTable;
    Table.Descriptors = Broken;
    const char* const Fragments[] = { "test_record", fragment, NULL };
    TestText_CheckErrorMessage(run, PropertyTable_Check(&Table), ErrorCode_IllegalArgument, Fragments);
}

static void BreakKeyToDuplicate(PropertyDescriptor* descriptor) { descriptor->Key = u8"count"; }
static void BreakWireIdToDuplicate(PropertyDescriptor* descriptor) { descriptor->WireId = 2U; }
static void BreakDefaultOutOfRange(PropertyDescriptor* descriptor) { descriptor->Default.Int32Value = 101; }
static void BreakWireIdToRetired(PropertyDescriptor* descriptor) { descriptor->WireId = 41U; }
static void BreakWireIdToZero(PropertyDescriptor* descriptor) { descriptor->WireId = 0U; }
static void BreakKeySyntax(PropertyDescriptor* descriptor) { descriptor->Key = u8"Weight"; }
static void BreakSize(PropertyDescriptor* descriptor) { descriptor->Size = sizeof(double); }
static void BreakOffset(PropertyDescriptor* descriptor) { descriptor->Offset = sizeof(TestRecord); }
static void BreakOverlap(PropertyDescriptor* descriptor) { descriptor->Offset = offsetof(TestRecord, Count); }
static void BreakStringLimit(PropertyDescriptor* descriptor) { descriptor->Rules.MaxLength = 0U; }
static void BreakNullableDefault(PropertyDescriptor* descriptor) { descriptor->Default.StringValue = u8"x"; }
static void BreakEnumTable(PropertyDescriptor* descriptor) { descriptor->EnumTable = NULL; }
static void BreakBoundsOnBool(PropertyDescriptor* descriptor) { descriptor->Rules.HasMinimum = true; }
static void BreakVersion(PropertyDescriptor* descriptor) { descriptor->SinceVersion = 2U; }
static void BreakFlags(PropertyDescriptor* descriptor) { descriptor->Flags |= 1U << 20; }
static void BreakEnumDefault(PropertyDescriptor* descriptor) { descriptor->Default.EnumValue = 9; }
static void BreakDateDefault(PropertyDescriptor* descriptor) { descriptor->Default.DateValue = -1; }

static void BrokenTablesFailTheirCheck(TestRun* run)
{
    CheckBrokenDescriptor(run, 4U, BreakKeyToDuplicate, "key is used twice");
    CheckBrokenDescriptor(run, 4U, BreakWireIdToDuplicate, "wire id is used twice");
    CheckBrokenDescriptor(run, 1U, BreakDefaultOutOfRange, "count (Count) must be in [0, 100]");
    CheckBrokenDescriptor(run, 4U, BreakWireIdToRetired, "retired");
    CheckBrokenDescriptor(run, 4U, BreakWireIdToZero, "must not be 0");
    CheckBrokenDescriptor(run, 4U, BreakKeySyntax, "snake_case");
    CheckBrokenDescriptor(run, 4U, BreakSize, "size");
    CheckBrokenDescriptor(run, 4U, BreakOffset, "outside the struct");
    CheckBrokenDescriptor(run, 4U, BreakOverlap, "overlaps");
    CheckBrokenDescriptor(run, 10U, BreakStringLimit, "MaxLength");
    CheckBrokenDescriptor(run, 11U, BreakNullableDefault, "default to null");
    CheckBrokenDescriptor(run, 13U, BreakEnumTable, "enum table");
    CheckBrokenDescriptor(run, 0U, BreakBoundsOnBool, "no minimum or maximum");
    CheckBrokenDescriptor(run, 4U, BreakVersion, "SinceVersion");
    CheckBrokenDescriptor(run, 4U, BreakFlags, "flag bits");
    CheckBrokenDescriptor(run, 13U, BreakEnumDefault, "one of \"both\", \"near\" or \"far\"");
    CheckBrokenDescriptor(run, 19U, BreakBoundsOnBool, "no minimum or maximum"); // a date takes no bounds
    CheckBrokenDescriptor(run, 19U, BreakDateDefault, "due (Due) must be a date");

    // A table-level fault: a wire id retired twice.
    static const uint16_t Twice[] = { 40U, 40U };
    PropertyTable Table = RecordTable;
    Table.RetiredWireIds = Twice;
    Table.RetiredWireIdCount = 2U;
    TEST_CHECK_ERROR(run, PropertyTable_Check(&Table), ErrorCode_IllegalArgument);
}

static void ConstructGivesTheDefaults(TestRun* run)
{
    TestRecord Record;
    memset(&Record, 0xA5, sizeof(Record)); // garbage, as new memory may hold
    if (!CheckConstruct(run, &Record))
    {
        return;
    }
    TEST_CHECK(run, Record.IsEnabled && (Record.Count == 5) && (Record.Mask == 0xFFU) && (Record.Seed == 7U));
    TEST_CHECK(run, (Record.Weight == 1.5f) && (Record.Heading == 0.25f));
    TEST_CHECK(run, (Record.Pivot.x == 0.5f) && (Record.Pivot.y == 0.0f) && (Record.Position.z == 0.0f));
    TEST_CHECK(run, (Record.Tint.r == 255) && (Record.Shade.Tint.g == 100) && (Record.Shade.Opacity == 1.0f));
    TEST_CHECK(run, AreStringsEqual(Record.Title, u8"untitled") && (Record.Title != RecordProperties[10].Default.StringValue));
    TEST_CHECK(run, (Record.Note == NULL) && (Record.Model == NULL));
    TEST_CHECK(run, (Record.Tier == TestTier_Near) && (Record.SmallTier == TestTier_Far) && (Record.Target == 0U));
    TEST_CHECK(run, (Record.Elapsed == 0.5) && (Record.Due == TEST_DUE_DEFAULT));
    TEST_CHECK(run, (Record.AfterSetCount == 0) && (Record.LastChanged == NULL)); // undescribed fields are zeroed
    TEST_CHECK_SUCCESS(run, Properties_Validate(&RecordTable, &Record));

    Properties_Deconstruct(&RecordTable, &Record);
    TEST_CHECK(run, Record.Title == NULL);
    Properties_Deconstruct(&RecordTable, &Record); // a second time does nothing
    Properties_Deconstruct(NULL, &Record);
    TEST_CHECK_ERROR(run, Properties_Construct(NULL, &Record), ErrorCode_IllegalArgument);
}

/* Breaks one field, validates, expects the fragments, and restores the record. */
static void CheckInvalidRecord(TestRun* run, const TestRecord* record, const char* const* fragments)
{
    TestText_CheckErrorMessage(run, Properties_Validate(&RecordTable, record), ErrorCode_ArgumentOutOfRange, fragments);
}

static void ValidationNamesTheKeyAndTheRule(TestRun* run)
{
    TestRecord Record;
    if (!CheckConstruct(run, &Record))
    {
        return;
    }

    Record.Weight = NAN;
    CheckInvalidRecord(run, &Record, (const char* const[]){ "test_record.weight (Weight) must be finite and >= 0", NULL });
    Record.Weight = -1.0f;
    CheckInvalidRecord(run, &Record, (const char* const[]){ "test_record.weight", ">= 0", NULL });
    Record.Weight = 0.0f; // the bound itself is allowed
    TEST_CHECK_SUCCESS(run, Properties_Validate(&RecordTable, &Record));

    Record.Count = 101;
    CheckInvalidRecord(run, &Record, (const char* const[]){ "test_record.count (Count) must be in [0, 100]", NULL });
    Record.Count = 100;
    TEST_CHECK_SUCCESS(run, Properties_Validate(&RecordTable, &Record));

    Record.Pivot.y = 1.5f;
    CheckInvalidRecord(run, &Record, (const char* const[]){ "pivot (Pivot) must have every component finite and in [-1, 1]",
        NULL });
    Record.Pivot.y = 0.0f;

    Record.Position.x = INFINITY;
    CheckInvalidRecord(run, &Record, (const char* const[]){ "position", "finite", NULL });
    Record.Position.x = 0.0f;

    Record.Shade.Brightness = 2.0f;
    CheckInvalidRecord(run, &Record, (const char* const[]){ "shade (Shade)", "brightness", NULL });
    Record.Shade.Brightness = 1.0f;

    Record.Tier = (TestTier)7;
    CheckInvalidRecord(run, &Record, (const char* const[]){ "tier (Tier) must be one of \"both\", \"near\" or \"far\"", NULL });
    Record.Tier = TestTier_Both;

    unsigned char* Title = Record.Title;
    Record.Title = NULL;
    CheckInvalidRecord(run, &Record, (const char* const[]){ "title (Title) must not be null", NULL });
    Record.Title = (unsigned char*)u8"a title that is far too long";
    CheckInvalidRecord(run, &Record, (const char* const[]){ "title", "at most 16 bytes", NULL });
    Record.Title = (unsigned char*)"\xC3\x28"; // a broken UTF-8 sequence
    CheckInvalidRecord(run, &Record, (const char* const[]){ "title", "UTF-8", NULL });
    Record.Title = Title;

    Record.Model = (unsigned char*)u8"Bad Name";
    CheckInvalidRecord(run, &Record, (const char* const[]){ "model (Model) must be an asset name", NULL });
    Record.Model = NULL;

    Record.Elapsed = 86400.0; // the half-open range excludes its maximum
    CheckInvalidRecord(run, &Record, (const char* const[]){ "test_record.elapsed (Elapsed) must be finite and in [0, 86400)",
        NULL });
    Record.Elapsed = NAN;
    CheckInvalidRecord(run, &Record, (const char* const[]){ "elapsed", "finite", NULL });
    Record.Elapsed = 86399.999999; // a double bound compares in double precision
    TEST_CHECK_SUCCESS(run, Properties_Validate(&RecordTable, &Record));

    Record.Due = -1;
    CheckInvalidRecord(run, &Record, (const char* const[]){ "test_record.due (Due) must be a date in the years 1 to 9999",
        NULL });
    Record.Due = GAME_CALENDAR_LAST_DAY_NUMBER + 1;
    CheckInvalidRecord(run, &Record, (const char* const[]){ "due (Due)", NULL });
    Record.Due = GAME_CALENDAR_LAST_DAY_NUMBER;

    TEST_CHECK_SUCCESS(run, Properties_Validate(&RecordTable, &Record));
    TEST_CHECK_ERROR(run, Properties_Validate(&RecordTable, NULL), ErrorCode_IllegalArgument);
    Properties_Deconstruct(&RecordTable, &Record);
}

static void JSONRoundTripKeepsEveryValue(TestRun* run)
{
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return;
    }
    TestRecord Source;
    TestRecord Restored;
    JSONCompound* Compound = NULL;
    bool IsReady = CheckConstruct(run, &Source) && CheckConstruct(run, &Restored)
        && TEST_CHECK_SUCCESS(run, JSONObjectPool_BorrowCompound(Pool, &Compound));
    if (IsReady)
    {
        FillRecord(run, &Source);
        TEST_CHECK_SUCCESS(run, Properties_WriteJSON(&RecordTable, &Source, Pool, Compound, NULL));
        // The runtime-only property is not written; every persistent one is (none is at its default).
        TEST_CHECK(run, JSONCompound_GetEntryCount(Compound) == (RECORD_PROPERTY_COUNT - 1U));

        JSONObjectValue Parsed;
        if (ReparseCompound(run, Pool, Compound, &Parsed))
        {
            TEST_CHECK_SUCCESS(run, Properties_ReadJSON(&RecordTable, Parsed.Value.Compound, &Restored, NULL));
            TEST_CHECK(run, Restored.Revision == 0); // not persistent: kept, not read
            Restored.Revision = Source.Revision;
            TEST_CHECK(run, Properties_Equals(&RecordTable, &Source, &Restored));
            TEST_CHECK(run, AreFloatsIdentical(Restored.Heading, Source.Heading) && AreFloatsIdentical(Restored.Weight, 0.1f));
            TEST_CHECK(run, (Restored.AfterSetCount == 1) && (Restored.LastChanged == NULL));
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Parsed));
        }

        // Every value at its default is left out, so a constructed record writes nothing.
        TestRecord Defaults;
        if (CheckConstruct(run, &Defaults))
        {
            JSONCompound* Empty = NULL;
            if (TEST_CHECK_SUCCESS(run, JSONObjectPool_BorrowCompound(Pool, &Empty)))
            {
                TEST_CHECK_SUCCESS(run, Properties_WriteJSON(&RecordTable, &Defaults, Pool, Empty, NULL));
                TEST_CHECK(run, JSONCompound_GetEntryCount(Empty) == 0U);
                PropertyWriteOptions Everything = { .AreDefaultsWritten = true };
                TEST_CHECK_SUCCESS(run, Properties_WriteJSON(&RecordTable, &Defaults, Pool, Empty, &Everything));
                TEST_CHECK(run, JSONCompound_GetEntryCount(Empty) == (RECORD_PROPERTY_COUNT - 1U));
                TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnCompound(Pool, Empty, true));
            }
            Properties_Deconstruct(&RecordTable, &Defaults);
        }
    }
    if (Compound != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnCompound(Pool, Compound, true));
    }
    Properties_Deconstruct(&RecordTable, &Source);
    Properties_Deconstruct(&RecordTable, &Restored);
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
}

static void MissingKeysReadAsDefaults(TestRun* run)
{
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return;
    }
    TestRecord Record;
    TestRecord Defaults;
    if (CheckConstruct(run, &Record) && CheckConstruct(run, &Defaults))
    {
        FillRecord(run, &Record);
        TEST_CHECK_SUCCESS(run, ReadRecordFromText(run, Pool, "{\"count\": 9}", &Record, NULL));
        TEST_CHECK(run, Record.Count == 9);
        TEST_CHECK(run, Record.Revision == 3); // a runtime-only property keeps its value
        Record.Count = Defaults.Count;
        Record.Revision = Defaults.Revision;
        TEST_CHECK(run, Properties_Equals(&RecordTable, &Record, &Defaults));
        TEST_CHECK(run, AreStringsEqual(Record.Title, u8"untitled") && (Record.Note == NULL));
    }
    Properties_Deconstruct(&RecordTable, &Record);
    Properties_Deconstruct(&RecordTable, &Defaults);
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
}

static void UnknownKeysFollowTheirMode(TestRun* run)
{
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return;
    }
    GenericBuffer Base;
    GenericBuffer_AllocateVariable(&Base, TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer LogText;
    GenericBuffer_AllocateVariable(&LogText, TEST_TEXT_CAPACITY, sizeof(unsigned char));
    TestRecord Record;
    if (CheckConstruct(run, &Record))
    {
        const char* Text = "{\"weight\": 2, \"bogus\": 1, \"revision\": 4}";

        // Error (the default): the read fails, names the keys and changes nothing.
        const char* const Fragments[] = { "test_record: unknown keys", "\"bogus\"", "\"revision\"", NULL };
        TestText_CheckErrorMessage(run, ReadRecordFromText(run, Pool, Text, &Record, NULL), ErrorCode_InvalidJSON,
            Fragments);
        TEST_CHECK(run, (Record.Weight == 1.5f) && (Record.AfterSetCount == 0));

        PropertyReadOptions Options = PropertyReadOptions_CreateDefault();
        Options.UnknownKeyMode = PropertyUnknownKeyMode_Ignore;
        TEST_CHECK_SUCCESS(run, ReadRecordFromText(run, Pool, Text, &Record, &Options));
        TEST_CHECK(run, (Record.Weight == 2.0f) && (Record.Revision == 0));

        Options.UnknownKeyMode = PropertyUnknownKeyMode_Warn; // no logger: like Ignore
        TEST_CHECK_SUCCESS(run, ReadRecordFromText(run, Pool, Text, &Record, &Options));

        Logger TestLogger;
        if (TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "unknown_keys_warn", &Base))
            && TEST_CHECK_SUCCESS(run, Logger_Construct(&TestLogger, Base._data)))
        {
            Options.Logger = &TestLogger;
            Record.Weight = 1.0f;
            TEST_CHECK_SUCCESS(run, ReadRecordFromText(run, Pool, Text, &Record, &Options));
            TEST_CHECK(run, Record.Weight == 2.0f);
            TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&TestLogger));
            if (TEST_CHECK_SUCCESS(run, ReadTextFile(Base._data, "logs/latest.log", &LogText)))
            {
                const char* Line = strstr((const char*)LogText._data, "test_record: ignoring unknown keys");
                TEST_CHECK(run, (Line != NULL) && (strstr(Line, "\"bogus\"") != NULL));
            }
        }
    }
    Properties_Deconstruct(&RecordTable, &Record);
    Memory_Free(LogText._data);
    Memory_Free(Base._data);
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
}

static void WrongValuesAreRejectedAndChangeNothing(TestRun* run)
{
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return;
    }
    TestRecord Record;
    TestRecord Snapshot;
    if (CheckConstruct(run, &Record) && CheckConstruct(run, &Snapshot))
    {
        FillRecord(run, &Record);
        TEST_CHECK_SUCCESS(run, Properties_CopyTo(&RecordTable, &Record, &Snapshot));
        int32_t AfterSetCount = Record.AfterSetCount;

        // Each document reads a good value first, so a failure must also undo that one.
        const char* const BadDocuments[][2] =
        {
            { "{\"count\": 1, \"weight\": \"abc\"}", "test_record.weight" },
            { "{\"count\": 1, \"is_enabled\": 3}", "test_record.is_enabled" },
            { "{\"count\": 1, \"tint\": [1, 2]}", "test_record.tint" },
            { "{\"count\": 1, \"tier\": \"sideways\"}", "one of \"both\", \"near\" or \"far\"" },
            { "{\"count\": 1, \"tier\": 1}", "test_record.tier" },
            { "{\"count\": 1, \"title\": 5}", "Expected a string or null" },
            { "{\"count\": 1, \"title\": null}", "title (Title) must not be null" },
            { "{\"count\": 1, \"title\": \"a title that is far too long\"}", "longer than the 16 bytes" },
            { "{\"count\": 1, \"title\": \"a\\u0000b\"}", "NUL" },
            { "{\"count\": 1, \"model\": \"Not A Name\"}", "asset name" },
            { "{\"count\": 1, \"position\": [1, 2]}", "exactly three" },
            { "{\"count\": 1, \"pivot\": [0, 0, 0]}", "exactly two" },
            { "{\"count\": 1.5}", "test_record.count" },
            { "{\"count\": 1000}", "test_record.count (Count) must be in [0, 100]" },
            { "{\"count\": 1, \"mask\": -1}", "test_record.mask" },
            { "{\"count\": 1, \"seed\": -5}", "test_record.seed" },
            { "{\"count\": 1, \"weight\": 1e300}", "test_record.weight" },
            { "{\"count\": 1, \"shade\": {\"tint\": \"#ffffff\", \"opacity\": 2}}", "test_record.shade" },
            { "{\"count\": 1, \"elapsed\": 86400}", "test_record.elapsed (Elapsed) must be finite and in [0, 86400)" },
            { "{\"count\": 1, \"elapsed\": \"soon\"}", "test_record.elapsed" },
            { "{\"count\": 1, \"due\": 739906}", "test_record.due: Expected a date \"yyyy-mm-dd\"" },
            { "{\"count\": 1, \"due\": \"2026-02-29\"}", "test_record.due" },
            { "{\"count\": 1, \"due\": \"2026-10-18T12:00\"}", "test_record.due" },
            { "{\"count\": 1, \"due\": \"0000-12-31\"}", "test_record.due" },
        };
        for (size_t Index = 0; Index < (sizeof(BadDocuments) / sizeof(BadDocuments[0])); Index++)
        {
            const char* const Fragments[] = { BadDocuments[Index][1], NULL };
            TestText_CheckErrorMessage(run, ReadRecordFromText(run, Pool, BadDocuments[Index][0], &Record, NULL),
                ErrorCode_InvalidJSON, Fragments);
            TEST_CHECK(run, Properties_Equals(&RecordTable, &Record, &Snapshot));
        }
        TEST_CHECK(run, Record.AfterSetCount == AfterSetCount);
    }
    Properties_Deconstruct(&RecordTable, &Record);
    Properties_Deconstruct(&RecordTable, &Snapshot);
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
}

static void JSONValuesUseTheGameConventions(TestRun* run)
{
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return;
    }
    TestRecord Record;
    if (CheckConstruct(run, &Record))
    {
        // Other accepted forms: colors as arrays and hex with alpha, vectors as compounds, numbers as strings, enums
        // by name, uint64 above INT64_MAX as a string.
        const char* Text = "{\"tint\": [1, 2, 3], \"shade\": \"#102030\", \"position\": {\"x\": 1, \"y\": 2, \"z\": 3},"
            " \"count\": \"7\", \"small_tier\": \"near\", \"seed\": \"18446744073709551615\", \"note\": null,"
            " \"heading\": 180, \"due\": \"2027-01-20\", \"elapsed\": 12.25}";
        if (TEST_CHECK_SUCCESS(run, ReadRecordFromText(run, Pool, Text, &Record, NULL)))
        {
            TEST_CHECK(run, (Record.Tint.r == 1) && (Record.Tint.b == 3) && (Record.Tint.a == 255));
            TEST_CHECK(run, (Record.Shade.Tint.r == 0x10) && (Record.Shade.Tint.a == 255) && (Record.Shade.Brightness == 1.0f));
            TEST_CHECK(run, (Record.Position.x == 1.0f) && (Record.Position.z == 3.0f) && (Record.Count == 7));
            TEST_CHECK(run, (Record.SmallTier == TestTier_Near) && (Record.Seed == UINT64_MAX) && (Record.Note == NULL));
            TEST_CHECK(run, fabsf(Record.Heading - (float)TEST_PI) < 1e-6f);
            TEST_CHECK(run, (Record.Due == TEST_DUE_LATER) && (Record.Elapsed == 12.25));
        }

        // Written forms: hex colors (alpha only when not opaque), the enum's name, the angle in degrees, a big uint64 as
        // a string.
        JSONCompound* Compound = NULL;
        if (TEST_CHECK_SUCCESS(run, JSONObjectPool_BorrowCompound(Pool, &Compound)))
        {
            Record.Tint = (Color){ 255, 0, 16, 128 };
            Record.Heading = (float)(TEST_PI / 2.0);
            TEST_CHECK_SUCCESS(run, Properties_WriteJSON(&RecordTable, &Record, Pool, Compound, NULL));
            JSONObjectValue Value;
            if (TEST_CHECK_SUCCESS(run, JSONCompound_GetVerified(Compound, u8"tint", JSONValueType_String, &Value)))
            {
                TEST_CHECK(run, (Value.Value.String->_count == 9U) && (memcmp(Value.Value.String->_data, "#ff001080", 9U) == 0));
            }
            if (TEST_CHECK_SUCCESS(run, JSONCompound_GetVerified(Compound, u8"shade", JSONValueType_String, &Value)))
            {
                TEST_CHECK(run, (Value.Value.String->_count == 7U) && (memcmp(Value.Value.String->_data, "#102030", 7U) == 0));
            }
            if (TEST_CHECK_SUCCESS(run, JSONCompound_GetVerified(Compound, u8"small_tier", JSONValueType_String, &Value)))
            {
                TEST_CHECK(run, (Value.Value.String->_count == 4U) && (memcmp(Value.Value.String->_data, "near", 4U) == 0));
            }
            if (TEST_CHECK_SUCCESS(run, JSONCompound_GetVerified(Compound, u8"heading", JSONValueType_RealNumber, &Value)))
            {
                TEST_CHECK(run, Value.Value.RealNumber == 90.0);
            }
            if (TEST_CHECK_SUCCESS(run, JSONCompound_GetVerified(Compound, u8"due", JSONValueType_String, &Value)))
            {
                TEST_CHECK(run, (Value.Value.String->_count == 10U) && (memcmp(Value.Value.String->_data, "2027-01-20", 10U) == 0));
            }
            TEST_CHECK_SUCCESS(run, JSONCompound_GetVerified(Compound, u8"seed", JSONValueType_String, &Value));
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnCompound(Pool, Compound, true));
        }
    }
    Properties_Deconstruct(&RecordTable, &Record);
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
}

/* Degrees in JSON and radians in GHDF, and every angle survives the degree conversion bit for bit. */
static void AnglesAreDegreesInJSONAndRadiansInGHDF(TestRun* run)
{
    JSONObjectPool* Pool = NULL;
    GHDFObjectPool* BinaryPool = NULL;
    bool ArePoolsReady = TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool))
        && TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&BinaryPool));
    TestRecord Record;
    TestRecord Restored;
    if (ArePoolsReady && CheckConstruct(run, &Record) && CheckConstruct(run, &Restored))
    {
        const float Angles[] = { 0.4f, 1.0f, -2.5f, 0.1f, 3.14159274f, 1e-6f, 123.456f, -0.7853982f, 6.2831855f };
        for (size_t Index = 0; Index < (sizeof(Angles) / sizeof(Angles[0])); Index++)
        {
            Record.Heading = Angles[Index];
            JSONCompound* Compound = NULL;
            if (!TEST_CHECK_SUCCESS(run, JSONObjectPool_BorrowCompound(Pool, &Compound)))
            {
                break;
            }
            JSONObjectValue Parsed;
            if (TEST_CHECK_SUCCESS(run, Properties_WriteJSON(&RecordTable, &Record, Pool, Compound, NULL))
                && ReparseCompound(run, Pool, Compound, &Parsed))
            {
                TEST_CHECK_SUCCESS(run, Properties_ReadJSON(&RecordTable, Parsed.Value.Compound, &Restored, NULL));
                TEST_CHECK(run, AreFloatsIdentical(Restored.Heading, Angles[Index]));
                TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Parsed));
            }
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnCompound(Pool, Compound, true));
        }

        Record.Heading = 0.5f;
        GHDFCompound* Binary = NULL;
        if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(BinaryPool, &Binary)))
        {
            TEST_CHECK_SUCCESS(run, Properties_WriteGHDF(&RecordTable, &Record, BinaryPool, Binary, NULL));
            GHDFObjectValue Entry;
            if (TEST_CHECK_SUCCESS(run, GHDFCompound_GetVerified(Binary, 6U, GHDF_CreateRegularType(GHDFValueType_Float),
                &Entry)))
            {
                TEST_CHECK(run, Entry.Value.Float == 0.5f); // radians, as in memory
            }
            TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(BinaryPool, Binary, true));
        }
    }
    Properties_Deconstruct(&RecordTable, &Record);
    Properties_Deconstruct(&RecordTable, &Restored);
    if (BinaryPool != NULL)
    {
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(BinaryPool));
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    }
}

static void GHDFRoundTripKeepsEveryValue(TestRun* run)
{
    GHDFObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Pool)))
    {
        return;
    }
    TestRecord Source;
    TestRecord Restored;
    GHDFCompound* Compound = NULL;
    if (CheckConstruct(run, &Source) && CheckConstruct(run, &Restored)
        && TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Pool, &Compound)))
    {
        FillRecord(run, &Source);
        TEST_CHECK_SUCCESS(run, Properties_WriteGHDF(&RecordTable, &Source, Pool, Compound, NULL));
        TEST_CHECK(run, GHDFCompound_GetEntryCount(Compound) == (RECORD_PROPERTY_COUNT - 1U));
        TEST_CHECK_SUCCESS(run, Properties_ReadGHDF(&RecordTable, Compound, &Restored, NULL));
        Restored.Revision = Source.Revision;
        TEST_CHECK(run, Properties_Equals(&RecordTable, &Source, &Restored));
        TEST_CHECK(run, (Restored.AfterSetCount == 1) && (Restored.Serial == 5U)); // read-only properties still load

        // Defaults are left out unless asked for; NULL strings are never written.
        TEST_CHECK_SUCCESS(run, GHDFCompound_Clear(Compound));
        TestRecord Defaults;
        if (CheckConstruct(run, &Defaults))
        {
            TEST_CHECK_SUCCESS(run, Properties_WriteGHDF(&RecordTable, &Defaults, Pool, Compound, NULL));
            TEST_CHECK(run, GHDFCompound_GetEntryCount(Compound) == 0U);
            PropertyWriteOptions Everything = { .AreDefaultsWritten = true };
            TEST_CHECK_SUCCESS(run, Properties_WriteGHDF(&RecordTable, &Defaults, Pool, Compound, &Everything));
            TEST_CHECK(run, GHDFCompound_GetEntryCount(Compound) == (RECORD_PROPERTY_COUNT - 3U));
            TEST_CHECK_SUCCESS(run, Properties_ReadGHDF(&RecordTable, Compound, &Restored, NULL));
            Restored.Revision = Defaults.Revision;
            TEST_CHECK(run, Properties_Equals(&RecordTable, &Defaults, &Restored));
            Properties_Deconstruct(&RecordTable, &Defaults);
        }
    }
    if (Compound != NULL)
    {
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, Compound, true));
    }
    Properties_Deconstruct(&RecordTable, &Source);
    Properties_Deconstruct(&RecordTable, &Restored);
    TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
}

static void GHDFSkipsUnknownIdsAndRejectsWrongTypes(TestRun* run)
{
    GHDFObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Pool)))
    {
        return;
    }
    TestRecord Record;
    TestRecord Snapshot;
    GHDFCompound* Compound = NULL;
    if (CheckConstruct(run, &Record) && CheckConstruct(run, &Snapshot)
        && TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Pool, &Compound)))
    {
        GHDFObjectValue Count = GHDFObjectValue_CreateInt32(9);
        GHDFObjectValue Unknown = GHDFObjectValue_CreateUInt8(1U);
        TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Compound, 2U, GHDF_CreateRegularType(GHDFValueType_Int32), &Count));
        TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Compound, 999U, GHDF_CreateRegularType(GHDFValueType_UInt8), &Unknown));
        TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Compound, 40U, GHDF_CreateRegularType(GHDFValueType_UInt8), &Unknown));
        TEST_CHECK_SUCCESS(run, Properties_ReadGHDF(&RecordTable, Compound, &Record, NULL));
        TEST_CHECK(run, Record.Count == 9);
        TEST_CHECK_SUCCESS(run, Properties_CopyTo(&RecordTable, &Record, &Snapshot));

        // The weight stored as an integer: a type mismatch fails the whole read.
        GHDFObjectValue Weight = GHDFObjectValue_CreateInt32(3);
        TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Compound, 5U, GHDF_CreateRegularType(GHDFValueType_Int32), &Weight));
        GHDFObjectValue NewCount = GHDFObjectValue_CreateInt32(11);
        TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Compound, 2U, GHDF_CreateRegularType(GHDFValueType_Int32), &NewCount));
        const char* const WeightFragments[] = { "test_record.weight", NULL };
        TestText_CheckErrorMessage(run, Properties_ReadGHDF(&RecordTable, Compound, &Record, NULL), ErrorCode_Deserialize,
            WeightFragments);
        TEST_CHECK(run, Properties_Equals(&RecordTable, &Record, &Snapshot));
        TEST_CHECK_SUCCESS(run, GHDFCompound_Remove(Compound, 5U));

        // A rule broken in binary data fails too, with the binary code.
        GHDFObjectValue TooMany = GHDFObjectValue_CreateInt32(1000);
        TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Compound, 2U, GHDF_CreateRegularType(GHDFValueType_Int32), &TooMany));
        TEST_CHECK_ERROR(run, Properties_ReadGHDF(&RecordTable, Compound, &Record, NULL), ErrorCode_Deserialize);
        TEST_CHECK(run, Properties_Equals(&RecordTable, &Record, &Snapshot));
    }
    if (Compound != NULL)
    {
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, Compound, true));
    }
    Properties_Deconstruct(&RecordTable, &Record);
    Properties_Deconstruct(&RecordTable, &Snapshot);
    TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
}

static void CopiesAreDeep(TestRun* run)
{
    TestRecord Source;
    TestRecord Copy;
    if (CheckConstruct(run, &Source) && CheckConstruct(run, &Copy))
    {
        FillRecord(run, &Source);
        TEST_CHECK_SUCCESS(run, Properties_CopyTo(&RecordTable, &Source, &Copy));
        TEST_CHECK(run, Properties_Equals(&RecordTable, &Source, &Copy));
        TEST_CHECK(run, (Copy.Title != Source.Title) && (Copy.AfterSetCount == 1) && (Copy.LastChanged == NULL));

        TEST_CHECK_SUCCESS(run, Properties_Set(&RecordTable, &Copy, u8"title", &(PropertyValue){
            .Type = PropertyType_String, .Data.StringValue = u8"changed" }));
        TEST_CHECK(run, AreStringsEqual(Source.Title, u8"Café") && AreStringsEqual(Copy.Title, u8"changed"));
        TEST_CHECK(run, !Properties_Equals(&RecordTable, &Source, &Copy));

        TEST_CHECK_SUCCESS(run, Properties_CopyTo(&RecordTable, &Copy, &Copy)); // onto itself: nothing changes
        TEST_CHECK(run, AreStringsEqual(Copy.Title, u8"changed"));
        TEST_CHECK_ERROR(run, Properties_CopyTo(&RecordTable, NULL, &Copy), ErrorCode_IllegalArgument);
    }
    Properties_Deconstruct(&RecordTable, &Source);
    Properties_Deconstruct(&RecordTable, &Copy);
}

static void GetAndSetByKey(TestRun* run)
{
    TestRecord Record;
    if (!CheckConstruct(run, &Record))
    {
        return;
    }

    PropertyValue Value;
    if (TEST_CHECK_SUCCESS(run, Properties_Get(&RecordTable, &Record, u8"weight", &Value)))
    {
        TEST_CHECK(run, (Value.Type == PropertyType_Float) && (Value.Data.FloatValue == 1.5f));
    }
    TEST_CHECK_SUCCESS(run, Properties_Set(&RecordTable, &Record, u8"weight", &(PropertyValue){ .Type = PropertyType_Float,
        .Data.FloatValue = 4.0f }));
    TEST_CHECK(run, (Record.Weight == 4.0f) && (Record.AfterSetCount == 1));
    TEST_CHECK(run, (Record.LastChanged != NULL) && AreStringsEqual(Record.LastChanged->Key, u8"weight"));

    // Out of range, the wrong type, read-only and unknown keys change nothing and call no hook.
    TEST_CHECK_ERROR(run, Properties_Set(&RecordTable, &Record, u8"weight", &(PropertyValue){
        .Type = PropertyType_Float, .Data.FloatValue = -1.0f }), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, Properties_Set(&RecordTable, &Record, u8"weight", &(PropertyValue){
        .Type = PropertyType_Int32, .Data.Int32Value = 1 }), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, Properties_Set(&RecordTable, &Record, u8"serial", &(PropertyValue){
        .Type = PropertyType_UInt64, .Data.UInt64Value = 1U }), ErrorCode_InvalidOperation);
    TEST_CHECK_ERROR(run, Properties_Set(&RecordTable, &Record, u8"nothing", &(PropertyValue){
        .Type = PropertyType_Float, .Data.FloatValue = 1.0f }), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, Properties_Get(&RecordTable, &Record, u8"nothing", &Value), ErrorCode_IllegalArgument);
    TEST_CHECK(run, (Record.Weight == 4.0f) && (Record.AfterSetCount == 1));

    // A set string is cloned; Get hands out the instance's own.
    unsigned char Buffer[] = u8"mutable";
    TEST_CHECK_SUCCESS(run, Properties_Set(&RecordTable, &Record, u8"note", &(PropertyValue){ .Type = PropertyType_String,
        .Data.StringValue = Buffer }));
    Buffer[0] = (unsigned char)'X';
    if (TEST_CHECK_SUCCESS(run, Properties_Get(&RecordTable, &Record, u8"note", &Value)))
    {
        TEST_CHECK(run, AreStringsEqual(Value.Data.StringValue, u8"mutable") && (Value.Data.StringValue == Record.Note));
    }
    TEST_CHECK_SUCCESS(run, Properties_Set(&RecordTable, &Record, u8"note", &(PropertyValue){ .Type = PropertyType_String,
        .Data.StringValue = NULL }));
    TEST_CHECK(run, Record.Note == NULL);

    TEST_CHECK_SUCCESS(run, Properties_Set(&RecordTable, &Record, u8"small_tier", &(PropertyValue){
        .Type = PropertyType_Enum, .Data.EnumValue = TestTier_Near }));
    TEST_CHECK(run, Record.SmallTier == TestTier_Near);
    TEST_CHECK_ERROR(run, Properties_Set(&RecordTable, &Record, u8"small_tier", &(PropertyValue){
        .Type = PropertyType_Enum, .Data.EnumValue = 3 }), ErrorCode_ArgumentOutOfRange);

    const PropertyDescriptor* Count = PropertyTable_FindByKey(&RecordTable, u8"count");
    TEST_CHECK(run, (Count != NULL) && (Count->WireId == 2U));
    TEST_CHECK(run, PropertyTable_FindByKey(&RecordTable, NULL) == NULL);
    if (Count != NULL)
    {
        TEST_CHECK_SUCCESS(run, Properties_CheckValue(&RecordTable, Count, &(PropertyValue){ .Type = PropertyType_Int32,
            .Data.Int32Value = 50 }));
        TEST_CHECK_ERROR(run, Properties_CheckValue(&RecordTable, Count, &(PropertyValue){ .Type = PropertyType_Int32,
            .Data.Int32Value = -1 }), ErrorCode_ArgumentOutOfRange);
    }
    Properties_Deconstruct(&RecordTable, &Record);
}

/* A struct bigger than the inline scratch reads through a heap scratch (run under --sanitize). */
static void BigStructsReadThroughAHeapScratch(TestRun* run)
{
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return;
    }
    TestBigRecord* Record = Memory_Allocate(sizeof(TestBigRecord));
    if (TEST_CHECK_SUCCESS(run, Properties_Construct(&BigRecordTable, Record)))
    {
        JSONObjectValue Value;
        if (TestText_ParseJSON(run, Pool, "{\"value\": 3.5, \"name\": \"bigger\"}", &Value))
        {
            TEST_CHECK_SUCCESS(run, Properties_ReadJSON(&BigRecordTable, Value.Value.Compound, Record, NULL));
            TEST_CHECK(run, (Record->Value == 3.5f) && AreStringsEqual(Record->Name, u8"bigger"));
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
        }
        if (TestText_ParseJSON(run, Pool, "{\"value\": \"x\", \"name\": \"lost\"}", &Value))
        {
            TEST_CHECK_ERROR(run, Properties_ReadJSON(&BigRecordTable, Value.Value.Compound, Record, NULL),
                ErrorCode_InvalidJSON);
            TEST_CHECK(run, AreStringsEqual(Record->Name, u8"bigger"));
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
        }
    }
    Properties_Deconstruct(&BigRecordTable, Record);
    Memory_Free(Record);
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
}

static Error MigrateOldWeight(uint32_t fromVersion, JSONCompound* compound, JSONObjectPool* pool)
{
    (void)pool;
    if (fromVersion >= 2U)
    {
        return Error_CreateSuccess();
    }
    JSONObjectValue Value;
    bool IsFound = false;
    Error Result = JSONCompound_GetOptional(compound, u8"old_weight", &Value, &IsFound);
    if ((Result.Code != ErrorCode_Success) || !IsFound)
    {
        return Result;
    }
    Result = JSONCompound_Set(compound, u8"weight", &Value); // a number: nothing borrowed to move
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_Remove(compound, u8"old_weight");
    }
    return Result;
}

static void MigrationRunsForOlderData(TestRun* run)
{
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return;
    }
    PropertyTable Table = RecordTable;
    Table.Version = 2U;
    Table.Migrate = MigrateOldWeight;
    TestRecord Record;
    if (TEST_CHECK_SUCCESS(run, PropertyTable_Check(&Table)) && CheckConstruct(run, &Record))
    {
        PropertyReadOptions Options = PropertyReadOptions_CreateDefault();
        Options.Pool = Pool;
        JSONObjectValue Value;
        Options.DataVersion = 1U;
        if (TestText_ParseJSON(run, Pool, "{\"old_weight\": 6}", &Value))
        {
            TEST_CHECK_SUCCESS(run, Properties_ReadJSON(&Table, Value.Value.Compound, &Record, &Options));
            TEST_CHECK(run, Record.Weight == 6.0f);
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
        }
        Options.DataVersion = 2U; // current data: no migration, so the old key is unknown
        if (TestText_ParseJSON(run, Pool, "{\"old_weight\": 7}", &Value))
        {
            TEST_CHECK_ERROR(run, Properties_ReadJSON(&Table, Value.Value.Compound, &Record, &Options), ErrorCode_InvalidJSON);
            TEST_CHECK(run, Record.Weight == 6.0f);
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
        }
        Properties_Deconstruct(&Table, &Record);
    }
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
}

/* The table-driven encoder writes the same GHDF tree as the hand-written one did, for defaults and for changed values,
 * except for T23's changes (no retired ids 2 and 3, the date as id 30) and the settings added since (ids 31 to 45, left
 * out of the comparison here; environment_ghdf_round_trip covers them). Data written before T23 still reads. */
static void EnvironmentGHDFMatchesTheOldEncoder(TestRun* run)
{
    GHDFObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Pool)))
    {
        return;
    }
    const PropertyTable* Table = WorldEnvironment_GetPropertyTable();
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    PropertyWriteOptions Everything = { .AreDefaultsWritten = true };
    for (int Pass = 0; Pass < 2; Pass++)
    {
        if (Pass == 1)
        {
            FillEnvironment(&Environment);
        }
        GHDFCompound* Expected = NULL;
        GHDFCompound* Actual = NULL;
        if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Pool, &Expected))
            && TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Pool, &Actual)))
        {
            TEST_CHECK_SUCCESS(run, ReferenceEncodeEnvironment(Pool, Expected, &Environment));
            TEST_CHECK(run, GHDFCompound_GetEntryCount(Expected) == 29U);

            // Old data reads: the retired ids are skipped, and the missing date reads as its default.
            WorldEnvironment Restored;
            WorldEnvironment_SetDefaults(&Restored);
            TEST_CHECK_SUCCESS(run, Properties_ReadGHDF(Table, Expected, &Restored, NULL));
            TEST_CHECK(run, Restored.Date == GAME_CLOCK_DEFAULT_DAY_NUMBER);
            Restored.Date = Environment.Date;
            TEST_CHECK(run, Properties_Equals(Table, &Environment, &Restored));
            WorldEnvironment_Deconstruct(&Restored);

            // The table writes the old entries without the retired ids, plus the date as an encoded day number.
            TEST_CHECK_SUCCESS(run, GHDFCompound_Remove(Expected, 2U));
            TEST_CHECK_SUCCESS(run, GHDFCompound_Remove(Expected, 3U));
            GHDFObjectValue Date = GHDFObjectValue_CreateEncodedInteger(Environment.Date);
            TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Expected, 30U,
                GHDF_CreateRegularType(GHDFValueType_EncodedInteger), &Date));
            TEST_CHECK_SUCCESS(run, Properties_WriteGHDF(Table, &Environment, Pool, Actual, &Everything));
            TEST_CHECK(run, GHDFCompound_GetEntryCount(Actual) == 43U);
            for (size_t Index = 0U; Index < (sizeof(LaterSettingWireIds) / sizeof(LaterSettingWireIds[0])); Index++)
            {
                TEST_CHECK_SUCCESS(run, GHDFCompound_Remove(Actual, LaterSettingWireIds[Index]));
            }
            TEST_CHECK(run, AreGHDFCompoundsEqual(Expected, Actual, 0U));
        }
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, Expected, true));
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, Actual, true));
    }
    WorldEnvironment_Deconstruct(&Environment);
    TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
}

static void EnvironmentJSONRoundTrip(TestRun* run)
{
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return;
    }
    WorldEnvironment Source;
    WorldEnvironment Restored;
    WorldEnvironment_SetDefaults(&Source);
    WorldEnvironment_SetDefaults(&Restored);
    FillEnvironment(&Source);
    FillLaterSettings(run, &Source);
    JSONCompound* Compound = NULL;
    if (TEST_CHECK_SUCCESS(run, JSONObjectPool_BorrowCompound(Pool, &Compound)))
    {
        TEST_CHECK_SUCCESS(run, WorldEnvironment_WriteJSON(&Source, Pool, Compound, NULL));
        TEST_CHECK(run, JSONCompound_GetEntryCount(Compound) == 43U);
        JSONObjectValue Parsed;
        if (ReparseCompound(run, Pool, Compound, &Parsed))
        {
            TEST_CHECK_SUCCESS(run, WorldEnvironment_ReadJSON(&Restored, Parsed.Value.Compound, NULL));
            TEST_CHECK(run, Properties_Equals(WorldEnvironment_GetPropertyTable(), &Source, &Restored));
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Parsed));
        }
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnCompound(Pool, Compound, true));
    }

    // The keys are the field names in snake_case, the sun angle is in degrees and the date is "yyyy-mm-dd".
    JSONObjectValue Value;
    if (TestText_ParseJSON(run, Pool, "{\"sun_angle\": 30, \"light_pollution\": 0.5, \"sky_tint\": \"#ff0000\", "
        "\"date\": \"2027-01-20\"}", &Value))
    {
        TEST_CHECK_SUCCESS(run, WorldEnvironment_ReadJSON(&Restored, Value.Value.Compound, NULL));
        TEST_CHECK(run, fabsf(Restored.SunAngle - (float)(TEST_PI / 6.0)) < 1e-6f);
        TEST_CHECK(run, (Restored.LightPollution == 0.5f) && (Restored.SkyTint.g == 0) && (Restored.StarTwinkle == 0.25f));
        TEST_CHECK(run, Restored.Date == TEST_DUE_LATER);
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
    }
    // The removed day-night settings are unknown keys now (no world file was written with them).
    if (TestText_ParseJSON(run, Pool, "{\"day_length_seconds\": 120}", &Value))
    {
        const char* const Fragments[] = { "world_environment: unknown key", "\"day_length_seconds\"", NULL };
        TestText_CheckErrorMessage(run, WorldEnvironment_ReadJSON(&Restored, Value.Value.Compound, NULL),
            ErrorCode_InvalidJSON, Fragments);
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
    }
    if (TestText_ParseJSON(run, Pool, "{\"light_pollution\": 2}", &Value))
    {
        const char* const Fragments[] = { "world_environment.light_pollution (LightPollution) must be finite and in [0, 1]",
            NULL };
        TestText_CheckErrorMessage(run, WorldEnvironment_ReadJSON(&Restored, Value.Value.Compound, NULL),
            ErrorCode_InvalidJSON, Fragments);
        TEST_CHECK(run, Restored.LightPollution == 0.5f);
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
    }

    // The moon's settings as a creator writes them: the phase source by name, the tilt in degrees, a texture by name.
    if (TestText_ParseJSON(run, Pool, "{\"moon_phase_source\": \"fixed\", \"moon_phase\": 0.5, \"moon_orbit_tilt\": 5, "
        "\"moon_texture\": \"stories.harvest_moon\", \"are_seasons_enabled\": true}", &Value))
    {
        TEST_CHECK_SUCCESS(run, WorldEnvironment_ReadJSON(&Restored, Value.Value.Compound, NULL));
        TEST_CHECK(run, (Restored.MoonPhaseSource == WorldMoonPhaseSource_Fixed) && (Restored.MoonPhase == 0.5f));
        TEST_CHECK(run, fabsf(Restored.MoonOrbitTilt - (float)(5.0 * TEST_PI / 180.0)) < 1e-6f);
        TEST_CHECK(run, AreStringsEqual(Restored.MoonTexture, u8"stories.harvest_moon") && Restored.AreSeasonsEnabled);
        TEST_CHECK(run, AreStringsEqual(Restored.MilkyWayTexture, u8"sky_milky_way")); // absent: the default
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
    }
    if (TestText_ParseJSON(run, Pool, "{\"moon_phase_source\": \"always\"}", &Value))
    {
        const char* const Fragments[] = { "moon_phase_source", "\"calendar\"", "\"fixed\"", NULL };
        TestText_CheckErrorMessage(run, WorldEnvironment_ReadJSON(&Restored, Value.Value.Compound, NULL),
            ErrorCode_InvalidJSON, Fragments);
        TEST_CHECK(run, Restored.MoonPhaseSource == WorldMoonPhaseSource_Fixed);
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
    }
    if (TestText_ParseJSON(run, Pool, "{\"moon_texture\": \"Moon.png\"}", &Value))
    {
        const char* const Fragments[] = { "world_environment.moon_texture (MoonTexture) must be an asset name", NULL };
        TestText_CheckErrorMessage(run, WorldEnvironment_ReadJSON(&Restored, Value.Value.Compound, NULL),
            ErrorCode_InvalidJSON, Fragments);
        TEST_CHECK(run, AreStringsEqual(Restored.MoonTexture, u8"stories.harvest_moon"));
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
    }
    WorldEnvironment_Deconstruct(&Source);
    WorldEnvironment_Deconstruct(&Restored);
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
}

/* Every setting, the moon's and the texture names included, survives GHDF. */
static void EnvironmentGHDFRoundTrip(TestRun* run)
{
    GHDFObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Pool)))
    {
        return;
    }
    const PropertyTable* Table = WorldEnvironment_GetPropertyTable();
    WorldEnvironment Source;
    WorldEnvironment Restored;
    WorldEnvironment_SetDefaults(&Source);
    WorldEnvironment_SetDefaults(&Restored);
    FillEnvironment(&Source);
    FillLaterSettings(run, &Source);
    GHDFCompound* Compound = NULL;
    if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Pool, &Compound)))
    {
        TEST_CHECK_SUCCESS(run, Properties_WriteGHDF(Table, &Source, Pool, Compound, NULL));
        TEST_CHECK(run, GHDFCompound_GetEntryCount(Compound) == 43U);
        TEST_CHECK_SUCCESS(run, Properties_ReadGHDF(Table, Compound, &Restored, NULL));
        TEST_CHECK(run, Properties_Equals(Table, &Source, &Restored));
        TEST_CHECK(run, (Restored.MoonTexture != Source.MoonTexture) && AreStringsEqual(Restored.MoonTexture, u8"pack.red_moon"));
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, Compound, true));
    }
    WorldEnvironment_Deconstruct(&Source);
    WorldEnvironment_Deconstruct(&Restored);
    TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
}


// Public functions.
void PropertyDescriptorTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "tables_pass_their_check", TablesPassTheirCheck },
        { "broken_tables_fail_their_check", BrokenTablesFailTheirCheck },
        { "construct_gives_the_defaults", ConstructGivesTheDefaults },
        { "validation_names_the_key_and_the_rule", ValidationNamesTheKeyAndTheRule },
        { "json_round_trip_keeps_every_value", JSONRoundTripKeepsEveryValue },
        { "missing_keys_read_as_defaults", MissingKeysReadAsDefaults },
        { "unknown_keys_follow_their_mode", UnknownKeysFollowTheirMode },
        { "wrong_values_are_rejected_and_change_nothing", WrongValuesAreRejectedAndChangeNothing },
        { "json_values_use_the_game_conventions", JSONValuesUseTheGameConventions },
        { "angles_are_degrees_in_json_and_radians_in_ghdf", AnglesAreDegreesInJSONAndRadiansInGHDF },
        { "ghdf_round_trip_keeps_every_value", GHDFRoundTripKeepsEveryValue },
        { "ghdf_skips_unknown_ids_and_rejects_wrong_types", GHDFSkipsUnknownIdsAndRejectsWrongTypes },
        { "copies_are_deep", CopiesAreDeep },
        { "get_and_set_by_key", GetAndSetByKey },
        { "big_structs_read_through_a_heap_scratch", BigStructsReadThroughAHeapScratch },
        { "migration_runs_for_older_data", MigrationRunsForOlderData },
        { "environment_ghdf_matches_the_old_encoder", EnvironmentGHDFMatchesTheOldEncoder },
        { "environment_json_round_trip", EnvironmentJSONRoundTrip },
        { "environment_ghdf_round_trip", EnvironmentGHDFRoundTrip },
    };
    TestRun_RunSuite(run, "PropertyDescriptor", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
