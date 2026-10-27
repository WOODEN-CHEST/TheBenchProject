#include "TestSuites.h"
#include "TestText.h"
#include "DataValue.h"
#include "GameCalendar.h"
#include "wr/WRMemoryStream.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** Initial capacity of text buffers; they grow as needed. */
#define DATA_TEST_TEXT_CAPACITY ((size_t)1024)
/** A string with two- and three-byte UTF-8 characters. */
#define DATA_TEST_NAME "Caf\xC3\xA9 \xE2\x98\x95"
/** 2026-10-18T09:30:00.25 in microseconds since 0001-01-01. */
#define DATA_TEST_DUE ((int64_t)((739906LL * 86400000000LL) + (34200LL * 1000000LL) + 250000LL))


// Static functions: building values.
static uint32_t Intern(TestRun* run, KeyRegistry* keys, const char* text)
{
    uint32_t Id = NAMESPACED_KEY_ID_NONE;
    TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(keys, (const unsigned char*)text, &Id));
    return Id;
}

/* Moves @p value into @p compound under @p name (released on failure). */
static void Put(TestRun* run, DataValue* compound, const char* name, DataValue value)
{
    if (!TEST_CHECK_SUCCESS(run, DataValue_CompoundSet(compound, (const unsigned char*)name, &value)))
    {
        DataValue_Deconstruct(&value);
    }
}

/* Moves @p element into @p list (released on failure). */
static void Add(TestRun* run, DataValue* list, DataValue element)
{
    if (!TEST_CHECK_SUCCESS(run, DataValue_ListAdd(list, &element)))
    {
        DataValue_Deconstruct(&element);
    }
}

static DataValue MakeString(TestRun* run, const char* text)
{
    DataValue Value = DataValue_CreateTag();
    TEST_CHECK_SUCCESS(run, DataValue_CreateString((const unsigned char*)text, &Value));
    return Value;
}

static DataValue MakeList(TestRun* run, DataType elementType)
{
    DataValue Value = DataValue_CreateTag();
    TEST_CHECK_SUCCESS(run, DataValue_CreateList(elementType, &Value));
    return Value;
}

static DataValue MakeCompound(TestRun* run)
{
    DataValue Value = DataValue_CreateTag();
    TEST_CHECK_SUCCESS(run, DataValue_CreateCompound(&Value));
    return Value;
}

/* A compound with a value of every type, lists of every element type (one empty), nested lists and compounds. */
static DataValue BuildEveryType(TestRun* run, KeyRegistry* keys)
{
    DataValue Root = MakeCompound(run);
    Put(run, &Root, "flag", DataValue_FromBool(true));
    Put(run, &Root, "count", DataValue_FromInt(-12345678901LL));
    Put(run, &Root, "small", DataValue_FromInt(7));
    Put(run, &Root, "lowest", DataValue_FromInt(INT64_MIN));
    Put(run, &Root, "highest", DataValue_FromInt(INT64_MAX));
    Put(run, &Root, "rate", DataValue_FromFloat(1.5));
    Put(run, &Root, "whole", DataValue_FromFloat(2.0));
    Put(run, &Root, "precise", DataValue_FromFloat(0.1));
    Put(run, &Root, "tiny", DataValue_FromFloat(1e-310)); // a subnormal double
    Put(run, &Root, "name", MakeString(run, DATA_TEST_NAME));
    Put(run, &Root, "kind", DataValue_FromKey(Intern(run, keys, "town:street_lamp")));
    Put(run, &Root, "owner", DataValue_FromObject(12U));
    Put(run, &Root, "spawn", DataValue_FromVector3(Vector3Double_Create(100000.123456789, -2.5, 3.25)));
    Put(run, &Root, "shade", DataValue_FromColor((Color){ 255, 204, 0, 128 }));
    Put(run, &Root, "due", DataValue_FromDateTime(DATA_TEST_DUE));
    Put(run, &Root, "$type", DataValue_FromKey(Intern(run, keys, "town:delivery")));

    DataValue Ints = MakeList(run, DataType_Int);
    Add(run, &Ints, DataValue_FromInt(1));
    Add(run, &Ints, DataValue_FromInt(300));
    Add(run, &Ints, DataValue_FromInt(-70000));
    Put(run, &Root, "ints", Ints);
    DataValue Floats = MakeList(run, DataType_Float);
    Add(run, &Floats, DataValue_FromFloat(1.5));
    Add(run, &Floats, DataValue_FromFloat(2.0));
    Put(run, &Root, "floats", Floats);
    DataValue Strings = MakeList(run, DataType_String);
    Add(run, &Strings, MakeString(run, "a"));
    Add(run, &Strings, MakeString(run, ""));
    Put(run, &Root, "strings", Strings);
    DataValue Keys = MakeList(run, DataType_Key);
    Add(run, &Keys, DataValue_FromKey(Intern(run, keys, "town:a")));
    Add(run, &Keys, DataValue_FromKey(Intern(run, keys, "game:b")));
    Put(run, &Root, "keys", Keys);
    DataValue Objects = MakeList(run, DataType_Object);
    Add(run, &Objects, DataValue_FromObject(1U));
    Add(run, &Objects, DataValue_FromObject(70000U));
    Put(run, &Root, "objects", Objects);
    DataValue Vectors = MakeList(run, DataType_Vector3);
    Add(run, &Vectors, DataValue_FromVector3(Vector3Double_Create(1.0, 2.0, 3.0)));
    Add(run, &Vectors, DataValue_FromVector3(Vector3Double_Create(0.1, -0.0, 1e-7)));
    Put(run, &Root, "vectors", Vectors);
    DataValue Colors = MakeList(run, DataType_Color);
    Add(run, &Colors, DataValue_FromColor((Color){ 1, 2, 3, 255 }));
    Put(run, &Root, "colors", Colors);
    DataValue Dates = MakeList(run, DataType_DateTime);
    Add(run, &Dates, DataValue_FromDateTime(0));
    Add(run, &Dates, DataValue_FromDateTime(GAME_CALENDAR_LAST_DATE_TIME));
    Put(run, &Root, "dates", Dates);
    DataValue Bools = MakeList(run, DataType_Bool);
    Add(run, &Bools, DataValue_FromBool(true));
    Add(run, &Bools, DataValue_FromBool(false));
    Put(run, &Root, "bools", Bools);
    Put(run, &Root, "empty", MakeList(run, DataType_String));

    DataValue Nested = MakeList(run, DataType_List);
    DataValue Inner = MakeList(run, DataType_Int);
    Add(run, &Inner, DataValue_FromInt(1));
    Add(run, &Inner, DataValue_FromInt(2));
    Add(run, &Nested, Inner);
    Add(run, &Nested, MakeList(run, DataType_Float));
    Put(run, &Root, "nested", Nested);

    DataValue Records = MakeList(run, DataType_Compound);
    DataValue Record = MakeCompound(run);
    Put(run, &Record, "with space", DataValue_FromInt(1));
    Add(run, &Records, Record);
    Add(run, &Records, MakeCompound(run));
    Put(run, &Root, "records", Records);

    DataValue Deep = MakeCompound(run);
    Put(run, &Deep, "x", DataValue_FromInt(1));
    DataValue Middle = MakeCompound(run);
    Put(run, &Middle, "deep", Deep);
    Put(run, &Root, "inner", Middle);
    return Root;
}

/* Nests @p levels lists inside each other (the innermost holds an int): depth @p levels + 1. */
static DataValue BuildNestedLists(TestRun* run, size_t levels)
{
    DataValue Value = MakeList(run, DataType_Int);
    Add(run, &Value, DataValue_FromInt(1));
    for (size_t Level = 1U; Level < levels; Level++)
    {
        DataValue Outer = MakeList(run, DataType_List);
        Add(run, &Outer, Value);
        Value = Outer;
    }
    return Value;
}

/* Writes @p value as JSON text. */
static bool WriteJSONText(TestRun* run, const DataValue* value, KeyRegistry* keys, GenericBuffer* outText)
{
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return false;
    }
    JSONObjectValue JSON;
    bool IsWritten = TEST_CHECK_SUCCESS(run, DataValue_WriteJSON(value, keys, Pool, &JSON))
        && TEST_CHECK_SUCCESS(run, JSON_Serialize(&JSON, outText, JSONSerializeFlags_NullTerminated));
    if (IsWritten)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &JSON));
    }
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    return IsWritten;
}

/* Parses @p text and reads it as a value (the read's error is returned). */
static Error ReadJSONText(TestRun* run, const char* text, KeyRegistry* keys, DataValue* outValue)
{
    *outValue = DataValue_CreateTag();
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
        Result = DataValue_ReadJSON(&JSON, keys, NULL, outValue);
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &JSON));
    }
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    return Result;
}

/* Checks that reading @p text fails with InvalidJSON naming every fragment, and leaves a tag. */
static void CheckJSONRefused(TestRun* run, KeyRegistry* keys, const char* text, const char* const* fragments)
{
    DataValue Value = DataValue_CreateTag();
    TestText_CheckErrorMessage(run, ReadJSONText(run, text, keys, &Value), ErrorCode_InvalidJSON, fragments);
    TEST_CHECK(run, Value.Type == DataType_Tag);
    DataValue_Deconstruct(&Value);
}

/* Writes @p value into a GHDF document, reads the bytes back and reads the value from them. */
static bool RoundTripGHDF(TestRun* run, const DataValue* value, KeyRegistry* keys, DataValue* outValue)
{
    *outValue = DataValue_CreateTag();
    GHDFObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Pool)))
    {
        return false;
    }
    bool IsRead = false;
    GHDFCompound* Compound = NULL;
    MemoryStream Stream;
    if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Pool, &Compound))
        && TEST_CHECK_SUCCESS(run, DataValue_WriteGHDF(value, keys, Pool, Compound))
        && TEST_CHECK_SUCCESS(run, MemoryStream_Construct1(&Stream, (IOStreamFlags)(IOStreamFlags_CanRead
            | IOStreamFlags_CanWrite))))
    {
        IOStream* StreamInterface = MemoryStream_AsIOStream(&Stream);
        GHDFCompound* ReadBack = NULL;
        IsRead = TEST_CHECK_SUCCESS(run, GHDF_Write(Compound, StreamInterface))
            && TEST_CHECK_SUCCESS(run, IOStream_SetPosition(StreamInterface, 0U))
            && TEST_CHECK_SUCCESS(run, GHDF_Read(StreamInterface, Pool, &ReadBack))
            && TEST_CHECK_SUCCESS(run, DataValue_ReadGHDF(ReadBack, keys, outValue));
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
    TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
    return IsRead;
}


// Static functions: values.
static void EqualityHoldsAcrossEveryType(TestRun* run)
{
    KeyRegistry Keys;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }
    DataValue Values[DATA_TYPE_COUNT] =
    {
        DataValue_CreateTag(), DataValue_FromBool(true), DataValue_FromInt(3), DataValue_FromFloat(3.0),
        MakeString(run, "3"), DataValue_FromKey(Intern(run, &Keys, "town:three")), DataValue_FromObject(3U),
        DataValue_FromVector3(Vector3Double_Create(3.0, 3.0, 3.0)), DataValue_FromColor((Color){ 3, 3, 3, 3 }),
        DataValue_FromDateTime(3), MakeList(run, DataType_Int), MakeCompound(run),
    };
    for (size_t First = 0U; First < DATA_TYPE_COUNT; First++)
    {
        TEST_CHECK(run, Values[First].Type == (DataType)First);
        for (size_t Second = 0U; Second < DATA_TYPE_COUNT; Second++)
        {
            TEST_CHECK(run, DataValue_Equals(&Values[First], &Values[Second]) == (First == Second));
        }
        DataValue Copy = DataValue_Clone(&Values[First]);
        TEST_CHECK(run, DataValue_Equals(&Copy, &Values[First]));
        DataValue_Deconstruct(&Copy);
    }

    // Values of one type: by value, 0 equals -0, empty lists of other element types differ.
    DataValue Zero = DataValue_FromFloat(0.0);
    DataValue NegativeZero = DataValue_FromFloat(-0.0);
    TEST_CHECK(run, DataValue_Equals(&Zero, &NegativeZero));
    DataValue Other = DataValue_FromInt(4);
    TEST_CHECK(run, !DataValue_Equals(&Values[DataType_Int], &Other));
    DataValue FloatList = MakeList(run, DataType_Float);
    TEST_CHECK(run, !DataValue_Equals(&Values[DataType_List], &FloatList));

    // Compounds compare by content, whatever order their entries were set in.
    DataValue First = MakeCompound(run);
    DataValue Second = MakeCompound(run);
    Put(run, &First, "a", DataValue_FromInt(1));
    Put(run, &First, "b", MakeString(run, "x"));
    Put(run, &Second, "b", MakeString(run, "x"));
    Put(run, &Second, "a", DataValue_FromInt(1));
    TEST_CHECK(run, DataValue_Equals(&First, &Second));
    Put(run, &Second, "a", DataValue_FromInt(2));
    TEST_CHECK(run, !DataValue_Equals(&First, &Second));

    DataValue_Deconstruct(&First);
    DataValue_Deconstruct(&Second);
    DataValue_Deconstruct(&FloatList);
    for (size_t Index = 0U; Index < DATA_TYPE_COUNT; Index++)
    {
        DataValue_Deconstruct(&Values[Index]);
    }
    TEST_CHECK(run, DataValue_Equals(&Values[DataType_String], &Values[DataType_Tag])); // released values are tags
    KeyRegistry_Deconstruct(&Keys);
}

static void DeepCopiesAreIndependent(TestRun* run)
{
    KeyRegistry Keys;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }
    DataValue Original = BuildEveryType(run, &Keys);
    DataValue Copy = DataValue_Clone(&Original);
    TEST_CHECK(run, DataValue_Equals(&Original, &Copy));
    TEST_CHECK(run, DataValue_GetDepth(&Copy) == DataValue_GetDepth(&Original));

    // Changing and then releasing the original leaves the copy as it was (the sanitizer run checks the memory).
    DataValue Snapshot = DataValue_Clone(&Copy);
    Put(run, &Original, "name", MakeString(run, "changed"));
    TEST_CHECK(run, DataValue_CompoundRemove(&Original, (const unsigned char*)u8"ints"));
    TEST_CHECK(run, !DataValue_Equals(&Original, &Copy));
    DataValue_Deconstruct(&Original);
    TEST_CHECK(run, (Original.Type == DataType_Tag) && DataValue_Equals(&Copy, &Snapshot));
    const DataValue* Name = DataValue_FindCompoundEntry(&Copy, (const unsigned char*)u8"name");
    TEST_CHECK(run, (Name != NULL) && (strcmp((const char*)Name->StringValue, DATA_TEST_NAME) == 0));

    DataValue_Deconstruct(&Snapshot);
    DataValue_Deconstruct(&Copy);
    KeyRegistry_Deconstruct(&Keys);
}

static void ListsHoldOneElementType(TestRun* run)
{
    DataValue List = MakeList(run, DataType_Int);
    DataValue Wrong = DataValue_FromFloat(1.0);
    TEST_CHECK_ERROR(run, DataValue_ListAdd(&List, &Wrong), ErrorCode_IllegalArgument);
    TEST_CHECK(run, (Wrong.Type == DataType_Float) && (DataValue_GetListCount(&List) == 0U)); // still the caller's

    DataValue Tags = DataValue_CreateTag();
    TEST_CHECK_ERROR(run, DataValue_CreateList(DataType_Tag, &Tags), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, DataValue_CreateList((DataType)DATA_TYPE_COUNT, &Tags), ErrorCode_IllegalArgument);

    // At most 4096 elements.
    for (size_t Index = 0U; Index < DATA_VALUE_MAX_ELEMENT_COUNT; Index++)
    {
        DataValue Element = DataValue_FromInt((int64_t)Index);
        Error Result = DataValue_ListAdd(&List, &Element);
        Error_Deconstruct(&Result);
    }
    TEST_CHECK(run, DataValue_GetListCount(&List) == DATA_VALUE_MAX_ELEMENT_COUNT);
    DataValue Extra = DataValue_FromInt(-1);
    TEST_CHECK_ERROR(run, DataValue_ListAdd(&List, &Extra), ErrorCode_InvalidOperation);
    TEST_CHECK(run, DataValue_GetListElement(&List, 4095U)->IntValue == 4095);

    // Removal moves the later elements up.
    TEST_CHECK_SUCCESS(run, DataValue_ListRemoveAt(&List, 0U));
    TEST_CHECK(run, (DataValue_GetListCount(&List) == 4095U) && (DataValue_GetListElement(&List, 0U)->IntValue == 1));
    TEST_CHECK_ERROR(run, DataValue_ListRemoveAt(&List, 4095U), ErrorCode_IndexOutOfBounds);
    TEST_CHECK(run, DataValue_GetListElement(&List, 4095U) == NULL);
    DataValue_Deconstruct(&List);
}

static void DepthSeventeenIsRefused(TestRun* run)
{
    DataValue Fifteen = BuildNestedLists(run, 15U); // depth 16
    TEST_CHECK(run, DataValue_GetDepth(&Fifteen) == DATA_VALUE_MAX_DEPTH);
    TEST_CHECK_SUCCESS(run, DataValue_Validate(&Fifteen, NULL));

    DataValue Outer = MakeList(run, DataType_List);
    TEST_CHECK_ERROR(run, DataValue_ListAdd(&Outer, &Fifteen), ErrorCode_InvalidOperation); // would be depth 17
    DataValue Compound = MakeCompound(run);
    TEST_CHECK_ERROR(run, DataValue_CompoundSet(&Compound, (const unsigned char*)u8"deep", &Fifteen),
        ErrorCode_InvalidOperation);
    TEST_CHECK(run, DataValue_GetDepth(&Fifteen) == 16U);

    // Removing the deepest element lowers the depth again.
    DataValue Mixed = MakeCompound(run);
    Put(run, &Mixed, "flat", DataValue_FromInt(1));
    Put(run, &Mixed, "deep", BuildNestedLists(run, 3U));
    TEST_CHECK(run, DataValue_GetDepth(&Mixed) == 5U);
    TEST_CHECK(run, DataValue_CompoundRemove(&Mixed, (const unsigned char*)u8"deep"));
    TEST_CHECK(run, DataValue_GetDepth(&Mixed) == 2U);

    DataValue_Deconstruct(&Mixed);
    DataValue_Deconstruct(&Compound);
    DataValue_Deconstruct(&Outer);
    DataValue_Deconstruct(&Fifteen);
}

static void StringsAreLimitedAndUTF8(TestRun* run)
{
    unsigned char Text[DATA_VALUE_MAX_STRING_LENGTH + 2U];
    memset(Text, 'a', sizeof(Text));
    Text[DATA_VALUE_MAX_STRING_LENGTH] = (unsigned char)0;
    DataValue Value = DataValue_CreateTag();
    TEST_CHECK_SUCCESS(run, DataValue_CreateString(Text, &Value));
    TEST_CHECK(run, DataValue_GetByteSize(&Value, NULL) == (DATA_VALUE_MAX_STRING_LENGTH + 1U)); // and its end
    DataValue_Deconstruct(&Value);

    Text[DATA_VALUE_MAX_STRING_LENGTH] = (unsigned char)'a';
    Text[DATA_VALUE_MAX_STRING_LENGTH + 1U] = (unsigned char)0;
    TEST_CHECK_ERROR(run, DataValue_CreateString(Text, &Value), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, Value.Type == DataType_Tag);
    TEST_CHECK_ERROR(run, DataValue_CreateString((const unsigned char*)"bad \xFF", &Value), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, DataValue_CreateStringFromBytes((const unsigned char*)"a\0b", 3U, &Value),
        ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_SUCCESS(run, DataValue_CreateStringFromBytes(NULL, 0U, &Value));
    TEST_CHECK(run, (Value.Type == DataType_String) && (Value.StringValue[0] == (unsigned char)0));
    DataValue_Deconstruct(&Value);
}

static void CompoundNamesFollowTheRules(TestRun* run)
{
    KeyRegistry Keys;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }
    DataValue Compound = MakeCompound(run);
    Put(run, &Compound, "zeta", DataValue_FromInt(1));
    Put(run, &Compound, "alpha", DataValue_FromInt(2));
    Put(run, &Compound, "Bob Smith", DataValue_FromInt(3));
    TEST_CHECK(run, DataValue_GetCompoundCount(&Compound) == 3U);
    TEST_CHECK(run, strcmp((const char*)DataValue_GetCompoundEntryName(&Compound, 0U), "Bob Smith") == 0); // byte order
    TEST_CHECK(run, strcmp((const char*)DataValue_GetCompoundEntryName(&Compound, 1U), "alpha") == 0);
    TEST_CHECK(run, DataValue_FindCompoundEntry(&Compound, (const unsigned char*)u8"zeta")->IntValue == 1);
    Put(run, &Compound, "zeta", DataValue_FromInt(9)); // replaces
    TEST_CHECK(run, (DataValue_GetCompoundCount(&Compound) == 3U)
        && (DataValue_FindCompoundEntry(&Compound, (const unsigned char*)u8"zeta")->IntValue == 9));

    DataValue Value = DataValue_FromInt(1);
    TEST_CHECK_ERROR(run, DataValue_CompoundSet(&Compound, (const unsigned char*)u8"", &Value),
        ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, DataValue_CompoundSet(&Compound, (const unsigned char*)u8"$key", &Value),
        ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, DataValue_CompoundSet(&Compound, (const unsigned char*)u8"$type", &Value),
        ErrorCode_ArgumentOutOfRange); // "$type" holds a key
    unsigned char Long[DATA_VALUE_MAX_NAME_LENGTH + 2U];
    memset(Long, 'n', sizeof(Long));
    Long[DATA_VALUE_MAX_NAME_LENGTH + 1U] = (unsigned char)0;
    TEST_CHECK_ERROR(run, DataValue_CompoundSet(&Compound, Long, &Value), ErrorCode_ArgumentOutOfRange);
    DataValue Tag = DataValue_CreateTag();
    TEST_CHECK_ERROR(run, DataValue_CompoundSet(&Compound, (const unsigned char*)u8"tag", &Tag),
        ErrorCode_IllegalArgument);
    DataValue Type = DataValue_FromKey(Intern(run, &Keys, "town:delivery"));
    TEST_CHECK_SUCCESS(run, DataValue_CompoundSet(&Compound, DATA_VALUE_TYPE_ENTRY_NAME, &Type));
    TEST_CHECK(run, DataValue_GetCompoundCount(&Compound) == 4U);

    TEST_CHECK(run, DataValue_CompoundRemove(&Compound, (const unsigned char*)u8"alpha"));
    TEST_CHECK(run, !DataValue_CompoundRemove(&Compound, (const unsigned char*)u8"alpha"));
    TEST_CHECK(run, DataValue_FindCompoundEntry(&Compound, (const unsigned char*)u8"alpha") == NULL);
    DataValue_Deconstruct(&Compound);
    KeyRegistry_Deconstruct(&Keys);
}

static void ValidationNamesTheProblem(TestRun* run)
{
    KeyRegistry Keys;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }
    DataValue Root = MakeCompound(run);
    DataValue Floats = MakeList(run, DataType_Float);
    Add(run, &Floats, DataValue_FromFloat(1.0));
    Add(run, &Floats, DataValue_FromFloat(NAN));
    Put(run, &Root, "speeds", Floats);
    const char* const NaN[] = { "speeds[1]", "finite", NULL };
    TestText_CheckErrorMessage(run, DataValue_Validate(&Root, &Keys), ErrorCode_ArgumentOutOfRange, NaN);

    DataValue Key = DataValue_FromKey(42U);
    const char* const UnknownKey[] = { "key registry", NULL };
    TestText_CheckErrorMessage(run, DataValue_Validate(&Key, &Keys), ErrorCode_ArgumentOutOfRange, UnknownKey);
    TEST_CHECK_SUCCESS(run, DataValue_Validate(&Key, NULL)); // no registry: keys not checked
    DataValue Date = DataValue_FromDateTime(-1);
    TEST_CHECK_ERROR(run, DataValue_Validate(&Date, &Keys), ErrorCode_ArgumentOutOfRange);
    DataValue Spot = DataValue_FromVector3(Vector3Double_Create(0.0, INFINITY, 0.0));
    TEST_CHECK_ERROR(run, DataValue_Validate(&Spot, &Keys), ErrorCode_ArgumentOutOfRange);
    DataValue Corrupt = { .Type = (DataType)77, .IntValue = 0 };
    TEST_CHECK_ERROR(run, DataValue_Validate(&Corrupt, &Keys), ErrorCode_ArgumentOutOfRange);
    DataValue_Deconstruct(&Root);
    KeyRegistry_Deconstruct(&Keys);
}

static void ByteSizesCountTheData(TestRun* run)
{
    KeyRegistry Keys;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }
    DataValue Tag = DataValue_CreateTag();
    DataValue Flag = DataValue_FromBool(true);
    DataValue Count = DataValue_FromInt(1);
    DataValue Spot = DataValue_FromVector3(Vector3Double_Zero());
    DataValue Shade = DataValue_FromColor((Color){ 0, 0, 0, 0 });
    DataValue Key = DataValue_FromKey(Intern(run, &Keys, "town:lamp")); // 9 bytes of text
    TEST_CHECK(run, (DataValue_GetByteSize(&Tag, &Keys) == 0U) && (DataValue_GetByteSize(&Flag, &Keys) == 1U));
    TEST_CHECK(run, (DataValue_GetByteSize(&Count, &Keys) == 8U) && (DataValue_GetByteSize(&Spot, &Keys) == 24U));
    TEST_CHECK(run, (DataValue_GetByteSize(&Shade, &Keys) == 4U) && (DataValue_GetByteSize(&Key, &Keys) == 9U));
    TEST_CHECK(run, DataValue_GetByteSize(&Key, NULL) == NAMESPACED_KEY_MAX_LENGTH);

    // Every value but a tag counts at least one byte, so the byte limits also bound how many values there are: a string
    // counts its end, a list its own byte and its elements, a compound its own byte and per entry its name, a type byte
    // and the value.
    DataValue Empty = MakeString(run, "");
    DataValue NoElements = MakeList(run, DataType_String);
    DataValue NoEntries = MakeCompound(run);
    TEST_CHECK(run, (DataValue_GetByteSize(&Empty, &Keys) == 1U) && (DataValue_GetByteSize(&NoElements, &Keys) == 1U)
        && (DataValue_GetByteSize(&NoEntries, &Keys) == 1U));
    DataValue_Deconstruct(&Empty);
    DataValue_Deconstruct(&NoElements);
    DataValue_Deconstruct(&NoEntries);
    DataValue List = MakeList(run, DataType_Int);
    Add(run, &List, DataValue_FromInt(1));
    Add(run, &List, DataValue_FromInt(2));
    TEST_CHECK(run, DataValue_GetByteSize(&List, &Keys) == 17U);
    DataValue Compound = MakeCompound(run);
    Put(run, &Compound, "ab", MakeString(run, "xyz"));
    Put(run, &Compound, "list", List);
    TEST_CHECK(run, DataValue_GetByteSize(&Compound, &Keys) == (1U + (2U + 1U + 4U) + (4U + 1U + 17U)));
    DataValue_Deconstruct(&Compound);
    KeyRegistry_Deconstruct(&Keys);
}


// Static functions: JSON and GHDF.
static void JSONRoundTripKeepsEveryType(TestRun* run)
{
    KeyRegistry Keys;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }
    DataValue Original = BuildEveryType(run, &Keys);
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, DATA_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (WriteJSONText(run, &Original, &Keys, &Text))
    {
        const char* Written = (const char*)Text._data;
        TEST_CHECK(run, strstr(Written, "\"whole\":2.0") != NULL); // a whole float keeps its fraction
        TEST_CHECK(run, strstr(Written, "$float") == NULL);
        TEST_CHECK(run, strstr(Written, "\"rate\":1.5") != NULL);
        TEST_CHECK(run, strstr(Written, "\"lowest\":-9223372036854775808") != NULL); // reads back too (equality below)
        TEST_CHECK(run, strstr(Written, "\"highest\":9223372036854775807") != NULL);
        TEST_CHECK(run, strstr(Written, "\"tiny\":1e-310") != NULL);
        TEST_CHECK(run, strstr(Written, "\"kind\":{\"$key\":\"town:street_lamp\"}") != NULL);
        TEST_CHECK(run, strstr(Written, "\"$type\":\"town:delivery\"") != NULL);
        TEST_CHECK(run, strstr(Written, "\"due\":{\"$date_time\":\"2026-10-18T09:30:00.25\"}") != NULL);
        TEST_CHECK(run, strstr(Written, "\"shade\":{\"$color\":\"#ffcc0080\"}") != NULL);
        TEST_CHECK(run, strstr(Written, "\"owner\":{\"$object\":12}") != NULL);
        TEST_CHECK(run, strstr(Written, "\"empty\":{\"$list\":\"string\"}") != NULL);
        TEST_CHECK(run, strstr(Written, "\"floats\":[1.5,2.0]") != NULL);
        TEST_CHECK(run, strstr(Written, "\"spawn\":{\"$vector3\":[100000.123456789,-2.5,3.25]}") != NULL);

        DataValue ReadBack = DataValue_CreateTag();
        if (TEST_CHECK_SUCCESS(run, ReadJSONText(run, Written, &Keys, &ReadBack)))
        {
            TEST_CHECK(run, DataValue_Equals(&Original, &ReadBack));
        }
        DataValue_Deconstruct(&ReadBack);
    }

    // A tag has no JSON form, and invalid values are never written.
    JSONObjectPool* Pool = NULL;
    if (TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        JSONObjectValue JSON;
        DataValue Tag = DataValue_CreateTag();
        DataValue NaN = DataValue_FromFloat(NAN);
        TEST_CHECK_ERROR(run, DataValue_WriteJSON(&Tag, &Keys, Pool, &JSON), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, DataValue_WriteJSON(&NaN, &Keys, Pool, &JSON), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    }
    Memory_Free(Text._data);
    DataValue_Deconstruct(&Original);
    KeyRegistry_Deconstruct(&Keys);
}

static void JSONNumbersKeepTheirType(TestRun* run)
{
    KeyRegistry Keys;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }
    DataValue Value = DataValue_CreateTag();
    TEST_CHECK(run, TEST_CHECK_SUCCESS(run, ReadJSONText(run, "3", &Keys, &Value)) && (Value.Type == DataType_Int)
        && (Value.IntValue == 3));
    TEST_CHECK(run, TEST_CHECK_SUCCESS(run, ReadJSONText(run, "3.0", &Keys, &Value)) && (Value.Type == DataType_Float)
        && (Value.FloatValue == 3.0));
    TEST_CHECK(run, TEST_CHECK_SUCCESS(run, ReadJSONText(run, "3e2", &Keys, &Value)) && (Value.Type == DataType_Float)
        && (Value.FloatValue == 300.0));
    TEST_CHECK(run, TEST_CHECK_SUCCESS(run, ReadJSONText(run, "{\"$float\": 2}", &Keys, &Value))
        && (Value.Type == DataType_Float) && (Value.FloatValue == 2.0));
    TEST_CHECK(run, TEST_CHECK_SUCCESS(run, ReadJSONText(run, "[1.5, 2.5]", &Keys, &Value))
        && (DataValue_GetListElementType(&Value) == DataType_Float));
    DataValue_Deconstruct(&Value);

    // A key without a namespace gets the default one; "$type" alone is a compound, not a wrapper.
    if (TEST_CHECK_SUCCESS(run, ReadJSONText(run, "{\"$key\": \"office\"}", &Keys, &Value)))
    {
        char Expected[NAMESPACED_KEY_BUFFER_SIZE];
        snprintf(Expected, sizeof(Expected), "%s:office", (const char*)NAMESPACED_KEY_DEFAULT_NAMESPACE);
        TEST_CHECK(run, (Value.Type == DataType_Key)
            && (strcmp((const char*)KeyRegistry_GetText(&Keys, Value.KeyValue), Expected) == 0));
    }
    if (TEST_CHECK_SUCCESS(run, ReadJSONText(run, "{\"$type\": \"town:delivery\"}", &Keys, &Value)))
    {
        TEST_CHECK(run, (Value.Type == DataType_Compound) && (DataValue_GetCompoundCount(&Value) == 1U));
        DataValue_Deconstruct(&Value);
    }
    if (TEST_CHECK_SUCCESS(run, ReadJSONText(run, "{\"$list\": \"compound\"}", &Keys, &Value)))
    {
        TEST_CHECK(run, (DataValue_GetListElementType(&Value) == DataType_Compound)
            && (DataValue_GetListCount(&Value) == 0U));
        DataValue_Deconstruct(&Value);
    }
    if (TEST_CHECK_SUCCESS(run, ReadJSONText(run, "{\"$color\": [10, 20, 30]}", &Keys, &Value)))
    {
        TEST_CHECK(run, (Value.Type == DataType_Color) && (Value.ColorValue.a == 255U));
    }
    KeyRegistry_Deconstruct(&Keys);
}

static void JSONRefusesHostileInput(TestRun* run)
{
    KeyRegistry Keys;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }
    const char* const TooDeep[] = { "16 levels", NULL };
    CheckJSONRefused(run, &Keys, "[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]", TooDeep); // 16 lists around an int: depth 17
    DataValue Sixteen = DataValue_CreateTag();
    TEST_CHECK_SUCCESS(run, ReadJSONText(run, "[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]", &Keys, &Sixteen));
    TEST_CHECK(run, DataValue_GetDepth(&Sixteen) == DATA_VALUE_MAX_DEPTH);
    DataValue_Deconstruct(&Sixteen);

    const char* const WrongElement[] = { "inventory[2]", "string", "int", NULL };
    CheckJSONRefused(run, &Keys, "{\"inventory\": [1, 2, \"three\"]}", WrongElement);
    const char* const IntInFloats[] = { "[1]", "2.0", NULL };
    CheckJSONRefused(run, &Keys, "[1.5, 2]", IntInFloats);
    const char* const Empty[] = { "$list", NULL };
    CheckJSONRefused(run, &Keys, "[]", Empty);
    const char* const BadKey[] = { "lamp.owner", "uppercase", NULL };
    CheckJSONRefused(run, &Keys, "{\"lamp\": {\"owner\": {\"$key\": \"Town:Bob\"}}}", BadKey);
    const char* const Null[] = { "null", NULL };
    CheckJSONRefused(run, &Keys, "{\"x\": null}", Null);
    const char* const Unknown[] = { "unknown", NULL };
    CheckJSONRefused(run, &Keys, "{\"$quaternion\": [0, 0, 0, 1]}", Unknown);
    const char* const Reserved[] = { "reserved", NULL };
    CheckJSONRefused(run, &Keys, "{\"$key\": \"a:b\", \"other\": 1}", Reserved);
    const char* const BadVector[] = { "$vector3", NULL };
    CheckJSONRefused(run, &Keys, "{\"$vector3\": [1, 2]}", BadVector);
    const char* const BadDate[] = { "$date_time", NULL };
    CheckJSONRefused(run, &Keys, "{\"$date_time\": \"2026-02-30T00:00:00\"}", BadDate);
    const char* const BadListType[] = { "$list", NULL };
    CheckJSONRefused(run, &Keys, "{\"$list\": \"tag\"}", BadListType);

    // Too long a string and too many elements are refused before anything is built.
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, DATA_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer_AppendByte(&Text, (unsigned char)'"');
    for (size_t Index = 0U; Index <= DATA_VALUE_MAX_STRING_LENGTH; Index++)
    {
        GenericBuffer_AppendByte(&Text, (unsigned char)'s');
    }
    GenericBuffer_AppendString(&Text, (const unsigned char*)"\"");
    GenericBuffer_NullTerminate(&Text);
    const char* const LongString[] = { "4096 bytes", NULL };
    CheckJSONRefused(run, &Keys, (const char*)Text._data, LongString);
    GenericBuffer_Clear(&Text);
    GenericBuffer_AppendByte(&Text, (unsigned char)'[');
    for (size_t Index = 0U; Index <= DATA_VALUE_MAX_ELEMENT_COUNT; Index++)
    {
        GenericBuffer_AppendString(&Text, (const unsigned char*)((Index == 0U) ? "1" : ",1"));
    }
    GenericBuffer_AppendString(&Text, (const unsigned char*)"]");
    GenericBuffer_NullTerminate(&Text);
    const char* const LongList[] = { "4096 elements", NULL };
    CheckJSONRefused(run, &Keys, (const char*)Text._data, LongList);
    Memory_Free(Text._data);
    KeyRegistry_Deconstruct(&Keys);
}

static void GHDFRoundTripKeepsEveryType(TestRun* run)
{
    KeyRegistry Keys;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }
    DataValue Original = BuildEveryType(run, &Keys);
    DataValue ReadBack = DataValue_CreateTag();
    if (RoundTripGHDF(run, &Original, &Keys, &ReadBack))
    {
        TEST_CHECK(run, DataValue_Equals(&Original, &ReadBack));
    }
    DataValue_Deconstruct(&ReadBack);

    // Numbers go in the smallest type that holds them.
    GHDFObjectPool* Pool = NULL;
    GHDFCompound* Compound = NULL;
    if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Pool))
        && TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Pool, &Compound)))
    {
        const DataValue Values[] = { DataValue_FromInt(-100), DataValue_FromInt(40000), DataValue_FromInt(1LL << 40),
            DataValue_FromFloat(1.5), DataValue_FromFloat(0.1), DataValue_FromObject(200U) };
        const GHDFValueType Expected[] = { GHDFValueType_Int8, GHDFValueType_Int32, GHDFValueType_Int64,
            GHDFValueType_Float, GHDFValueType_Double, GHDFValueType_UInt8 };
        for (size_t Index = 0U; Index < (sizeof(Expected) / sizeof(Expected[0])); Index++)
        {
            GHDFObjectValue Entry;
            TEST_CHECK_SUCCESS(run, GHDFCompound_Clear(Compound));
            TEST_CHECK(run, TEST_CHECK_SUCCESS(run, DataValue_WriteGHDF(&Values[Index], &Keys, Pool, Compound))
                && TEST_CHECK_SUCCESS(run, GHDFCompound_Get(Compound, (GHDFEntryID)3, &Entry))
                && (Entry.Type == Expected[Index]));
        }
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, Compound, true));
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
    }
    DataValue_Deconstruct(&Original);
    KeyRegistry_Deconstruct(&Keys);
}

/* Builds a value compound {2: type, 3: value of @p valueType} with an int value, for hostile cases. */
static GHDFCompound* MakeIntValueCompound(TestRun* run, GHDFObjectPool* pool, uint8_t type, GHDFObjectValue value)
{
    GHDFCompound* Compound = NULL;
    GHDFObjectValue Type = GHDFObjectValue_CreateUInt8(type);
    if (!TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(pool, &Compound))
        || !TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Compound, (GHDFEntryID)2,
            GHDF_CreateRegularType(GHDFValueType_UInt8), &Type))
        || !TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Compound, (GHDFEntryID)3, GHDF_CreateRegularType(value.Type),
            &value)))
    {
        return Compound;
    }
    return Compound;
}

/* An array of Float elements holding @p values. */
static GHDFArray* MakeFloatArray(TestRun* run, GHDFObjectPool* pool, const float* values, size_t count)
{
    GHDFArray* Array = NULL;
    if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowArray(pool, GHDFValueType_Float, &Array)))
    {
        for (size_t Index = 0U; Index < count; Index++)
        {
            GHDFObjectValue Element = GHDFObjectValue_CreateFloat(values[Index]);
            TEST_CHECK_SUCCESS(run, GHDFArray_AddValue(Array, &Element));
        }
    }
    return Array;
}

/* An array of String elements holding @p texts. */
static GHDFArray* MakeStringArray(TestRun* run, GHDFObjectPool* pool, const char* const* texts, size_t count)
{
    GHDFArray* Array = NULL;
    if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowArray(pool, GHDFValueType_String, &Array)))
    {
        for (size_t Index = 0U; Index < count; Index++)
        {
            GenericBuffer* Text = NULL;
            if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowString(pool, &Text)))
            {
                TEST_CHECK(run, GenericBuffer_AppendString(Text, (const unsigned char*)texts[Index])
                    && GenericBuffer_NullTerminate(Text));
                GHDFObjectValue Element = GHDFObjectValue_CreateString(Text);
                TEST_CHECK_SUCCESS(run, GHDFArray_AddValue(Array, &Element));
            }
        }
    }
    return Array;
}

/* Builds a value compound {2: @p type, 3: @p array}, with {4: @p elementType} unless that is DataType_Tag, for hostile
 * cases; the compound owns the array. */
static GHDFCompound* MakeArrayValueCompound(TestRun* run, GHDFObjectPool* pool, DataType type, DataType elementType,
    GHDFArray* array)
{
    GHDFCompound* Compound = NULL;
    if ((array == NULL) || !TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(pool, &Compound)))
    {
        return Compound;
    }
    GHDFObjectValue Type = GHDFObjectValue_CreateUInt8((uint8_t)type);
    GHDFObjectValue ElementType = GHDFObjectValue_CreateUInt8((uint8_t)elementType);
    GHDFObjectValue ArrayValue = GHDFObjectValue_CreateArray(array, GHDFArray_GetElementType(array));
    TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Compound, (GHDFEntryID)2, GHDF_CreateRegularType(GHDFValueType_UInt8),
        &Type));
    if (elementType != DataType_Tag)
    {
        TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Compound, (GHDFEntryID)4,
            GHDF_CreateRegularType(GHDFValueType_UInt8), &ElementType));
    }
    TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Compound, (GHDFEntryID)3,
        GHDF_CreateArrayType(GHDFArray_GetElementType(array)), &ArrayValue));
    return Compound;
}

static void GHDFRefusesHostileInput(TestRun* run)
{
    KeyRegistry Keys;
    GHDFObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)) || !TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Pool)))
    {
        KeyRegistry_Deconstruct(&Keys);
        return;
    }
    DataValue Value = DataValue_CreateTag();

    // An unknown type, and a value of the wrong GHDF type.
    GHDFCompound* Unknown = MakeIntValueCompound(run, Pool, 99U, GHDFObjectValue_CreateInt8(1));
    const char* const UnknownType[] = { "type", NULL };
    TestText_CheckErrorMessage(run, DataValue_ReadGHDF(Unknown, &Keys, &Value), ErrorCode_Deserialize, UnknownType);
    GHDFCompound* WrongType = MakeIntValueCompound(run, Pool, (uint8_t)DataType_Bool, GHDFObjectValue_CreateInt8(1));
    TEST_CHECK_ERROR(run, DataValue_ReadGHDF(WrongType, &Keys, &Value), ErrorCode_Deserialize);
    GHDFCompound* NegativeObject = MakeIntValueCompound(run, Pool, (uint8_t)DataType_Object,
        GHDFObjectValue_CreateInt8(-1));
    TEST_CHECK_ERROR(run, DataValue_ReadGHDF(NegativeObject, &Keys, &Value), ErrorCode_Deserialize);
    GHDFCompound* WideInt = MakeIntValueCompound(run, Pool, (uint8_t)DataType_Int, GHDFObjectValue_CreateUInt64(UINT64_MAX));
    TEST_CHECK_ERROR(run, DataValue_ReadGHDF(WideInt, &Keys, &Value), ErrorCode_Deserialize);
    // Any integer type reads as an int.
    GHDFCompound* Encoded = MakeIntValueCompound(run, Pool, (uint8_t)DataType_Int, GHDFObjectValue_CreateEncodedInteger(-5));
    TEST_CHECK(run, TEST_CHECK_SUCCESS(run, DataValue_ReadGHDF(Encoded, &Keys, &Value)) && (Value.IntValue == -5));

    // A list whose array does not fit its element type, and nesting past the limit.
    DataValue Strings = MakeList(run, DataType_String);
    Add(run, &Strings, MakeString(run, "x"));
    GHDFCompound* List = NULL;
    if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Pool, &List))
        && TEST_CHECK_SUCCESS(run, DataValue_WriteGHDF(&Strings, &Keys, Pool, List)))
    {
        GHDFObjectValue ElementType = GHDFObjectValue_CreateUInt8((uint8_t)DataType_Int);
        TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(List, (GHDFEntryID)4, GHDF_CreateRegularType(GHDFValueType_UInt8),
            &ElementType));
        const char* const Mismatch[] = { "list of int", NULL };
        TestText_CheckErrorMessage(run, DataValue_ReadGHDF(List, &Keys, &Value), ErrorCode_Deserialize, Mismatch);
    }
    DataValue_Deconstruct(&Strings);

    GHDFCompound* Deep = NULL;
    TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Pool, &Deep));
    DataValue Fifteen = BuildNestedLists(run, 15U);
    if ((Deep != NULL) && TEST_CHECK_SUCCESS(run, DataValue_WriteGHDF(&Fifteen, &Keys, Pool, Deep)))
    {
        // Wrap the depth-16 value in one more list by hand: {2: list, 4: list, 3: [it]}.
        GHDFCompound* Outer = NULL;
        GHDFArray* Elements = NULL;
        GHDFObjectValue ListType = GHDFObjectValue_CreateUInt8((uint8_t)DataType_List);
        GHDFObjectValue Inner = GHDFObjectValue_CreateCompound(Deep);
        if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Pool, &Outer))
            && TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowArray(Pool, GHDFValueType_Compound, &Elements))
            && TEST_CHECK_SUCCESS(run, GHDFArray_AddValue(Elements, &Inner)))
        {
            Deep = NULL; // the array owns it now
            GHDFObjectValue ArrayValue = GHDFObjectValue_CreateArray(Elements, GHDFValueType_Compound);
            TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Outer, (GHDFEntryID)2, GHDF_CreateRegularType(GHDFValueType_UInt8),
                &ListType));
            TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Outer, (GHDFEntryID)4, GHDF_CreateRegularType(GHDFValueType_UInt8),
                &ListType));
            TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Outer, (GHDFEntryID)3, GHDF_CreateArrayType(GHDFValueType_Compound),
                &ArrayValue));
            const char* const TooDeep[] = { "16 levels", NULL };
            TestText_CheckErrorMessage(run, DataValue_ReadGHDF(Outer, &Keys, &Value), ErrorCode_Deserialize, TooDeep);
        }
        if (Outer != NULL)
        {
            TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, Outer, true));
        }
    }
    if (Deep != NULL)
    {
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, Deep, true));
    }
    DataValue_Deconstruct(&Fifteen);
    TEST_CHECK(run, Value.Type == DataType_Tag);

    GHDFCompound* const Compounds[] = { Unknown, WrongType, NegativeObject, WideInt, Encoded, List };
    for (size_t Index = 0U; Index < (sizeof(Compounds) / sizeof(Compounds[0])); Index++)
    {
        if (Compounds[Index] != NULL)
        {
            TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, Compounds[Index], true));
        }
    }
    TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
    KeyRegistry_Deconstruct(&Keys);
}


static void GHDFRefusesNonFiniteVectors(TestRun* run)
{
    KeyRegistry Keys;
    GHDFObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)) || !TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Pool)))
    {
        KeyRegistry_Deconstruct(&Keys);
        return;
    }
    DataValue Value = DataValue_CreateTag();

    // A GHDF Float can hold the infinities and NaN; a vector3 may not, alone or in a list.
    const float Infinite[] = { 1.0f, INFINITY, 3.0f };
    GHDFCompound* Single = MakeArrayValueCompound(run, Pool, DataType_Vector3, DataType_Tag,
        MakeFloatArray(run, Pool, Infinite, 3U));
    const char* const NotFinite[] = { "vector3", "finite", NULL };
    TestText_CheckErrorMessage(run, DataValue_ReadGHDF(Single, &Keys, &Value), ErrorCode_Deserialize, NotFinite);
    const float Pairs[] = { 1.0f, 2.0f, 3.0f, 0.0f, NAN, 0.0f };
    GHDFCompound* List = MakeArrayValueCompound(run, Pool, DataType_List, DataType_Vector3,
        MakeFloatArray(run, Pool, Pairs, 6U));
    const char* const InList[] = { "[1]", "finite", NULL };
    TestText_CheckErrorMessage(run, DataValue_ReadGHDF(List, &Keys, &Value), ErrorCode_Deserialize, InList);
    TEST_CHECK(run, Value.Type == DataType_Tag);

    const float Finite[] = { 1.0f, 2.0f, 3.0f };
    GHDFCompound* Fine = MakeArrayValueCompound(run, Pool, DataType_Vector3, DataType_Tag,
        MakeFloatArray(run, Pool, Finite, 3U));
    TEST_CHECK(run, TEST_CHECK_SUCCESS(run, DataValue_ReadGHDF(Fine, &Keys, &Value))
        && (Value.Type == DataType_Vector3) && (Value.Vector3Value.Y == 2.0));

    // Double arrays, which the writer writes, likewise.
    GHDFArray* InfiniteDoubles = NULL;
    if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowArray(Pool, GHDFValueType_Double, &InfiniteDoubles)))
    {
        const double Components[] = { 1.0, 2.0, -INFINITY };
        for (size_t Index = 0U; Index < 3U; Index++)
        {
            GHDFObjectValue Element = GHDFObjectValue_CreateDouble(Components[Index]);
            TEST_CHECK_SUCCESS(run, GHDFArray_AddValue(InfiniteDoubles, &Element));
        }
    }
    GHDFCompound* Doubles = MakeArrayValueCompound(run, Pool, DataType_Vector3, DataType_Tag, InfiniteDoubles);
    DataValue_Deconstruct(&Value);
    TestText_CheckErrorMessage(run, DataValue_ReadGHDF(Doubles, &Keys, &Value), ErrorCode_Deserialize, NotFinite);

    GHDFCompound* const Compounds[] = { Single, List, Fine, Doubles };
    for (size_t Index = 0U; Index < (sizeof(Compounds) / sizeof(Compounds[0])); Index++)
    {
        if (Compounds[Index] != NULL)
        {
            TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, Compounds[Index], true));
        }
    }
    TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
    KeyRegistry_Deconstruct(&Keys);
}

/* A vector3 is three doubles (it often holds a far position), written as a Double array; a Float array, and JSON whose
 * numbers are the shortest texts of floats, read as the same numbers. */
static void Vector3sAreDoublesAndOldFormsRead(TestRun* run)
{
    KeyRegistry Keys;
    GHDFObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)) || !TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Pool)))
    {
        KeyRegistry_Deconstruct(&Keys);
        return;
    }

    // Fifteen significant digits round-trip through both forms exactly.
    DataValue Far = DataValue_FromVector3(Vector3Double_Create(9876543.21098765, -0.000123456789012345,
        -9999999.99999999));
    DataValue ReadBack = DataValue_CreateTag();
    TEST_CHECK(run, RoundTripGHDF(run, &Far, &Keys, &ReadBack) && DataValue_Equals(&Far, &ReadBack));
    DataValue_Deconstruct(&ReadBack);
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, DATA_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (WriteJSONText(run, &Far, &Keys, &Text))
    {
        TEST_CHECK(run, strcmp((const char*)Text._data,
            "{\"$vector3\":[9876543.21098765,-0.000123456789012345,-9999999.99999999]}") == 0);
        TEST_CHECK(run, TEST_CHECK_SUCCESS(run, ReadJSONText(run, (const char*)Text._data, &Keys, &ReadBack))
            && DataValue_Equals(&Far, &ReadBack));
        DataValue_Deconstruct(&ReadBack);
    }

    // The writer's value entry is a Double array: of 3 for a vector3, of 3 per element for a list of them.
    GHDFCompound* Written = NULL;
    DataValue List = MakeList(run, DataType_Vector3);
    Add(run, &List, DataValue_FromVector3(Vector3Double_Create(1.0, 2.0, 3.0)));
    Add(run, &List, DataValue_FromVector3(Vector3Double_Create(0.1, 0.2, 0.3)));
    const DataValue* const Values[] = { &Far, &List };
    const size_t Counts[] = { 3U, 6U };
    for (size_t Index = 0U; Index < 2U; Index++)
    {
        if (!TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Pool, &Written)))
        {
            continue;
        }
        GHDFObjectValue Entry;
        bool IsFound = false;
        TEST_CHECK(run, TEST_CHECK_SUCCESS(run, DataValue_WriteGHDF(Values[Index], &Keys, Pool, Written))
            && TEST_CHECK_SUCCESS(run, GHDFCompound_GetOptionalVerified(Written, (GHDFEntryID)3,
                GHDF_CreateArrayType(GHDFValueType_Double), &Entry, &IsFound))
            && IsFound && (GHDFArray_GetElementCount(Entry.Value.Array) == Counts[Index]));
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, Written, true));
    }
    DataValue_Deconstruct(&List);

    // A Float array (a vector3 saved in single precision) reads as those floats widened exactly; alone and in a list.
    const float Old[] = { 0.1f, -2.5f, 16777217.0f };
    GHDFCompound* Single = MakeArrayValueCompound(run, Pool, DataType_Vector3, DataType_Tag,
        MakeFloatArray(run, Pool, Old, 3U));
    TEST_CHECK(run, TEST_CHECK_SUCCESS(run, DataValue_ReadGHDF(Single, &Keys, &ReadBack))
        && (ReadBack.Type == DataType_Vector3) && (ReadBack.Vector3Value.X == (double)0.1f)
        && (ReadBack.Vector3Value.Y == -2.5) && (ReadBack.Vector3Value.Z == (double)16777217.0f));
    DataValue_Deconstruct(&ReadBack);
    const float OldList[] = { 1.0f, 2.0f, 3.0f, 0.25f, 0.5f, 0.75f };
    GHDFCompound* Listed = MakeArrayValueCompound(run, Pool, DataType_List, DataType_Vector3,
        MakeFloatArray(run, Pool, OldList, 6U));
    if (TEST_CHECK_SUCCESS(run, DataValue_ReadGHDF(Listed, &Keys, &ReadBack))
        && TEST_CHECK(run, DataValue_GetListCount(&ReadBack) == 2U))
    {
        const DataValue* Second = DataValue_GetListElement(&ReadBack, 1U);
        TEST_CHECK(run, (Second->Vector3Value.X == 0.25) && (Second->Vector3Value.Z == 0.75));
    }
    DataValue_Deconstruct(&ReadBack);

    // JSON a float writer wrote (each number the shortest text of its float) reads as the doubles that print the same
    // text again.
    const char* OldText = "{\"$vector3\":[0.1,-2.5,123456.7]}";
    if (TEST_CHECK_SUCCESS(run, ReadJSONText(run, OldText, &Keys, &ReadBack)))
    {
        TEST_CHECK(run, (ReadBack.Vector3Value.X == 0.1) && (ReadBack.Vector3Value.Z == 123456.7));
        GenericBuffer_Clear(&Text);
        TEST_CHECK(run, WriteJSONText(run, &ReadBack, &Keys, &Text) && (strcmp((const char*)Text._data, OldText) == 0));
    }
    DataValue_Deconstruct(&ReadBack);

    GHDFCompound* const Compounds[] = { Single, Listed };
    for (size_t Index = 0U; Index < (sizeof(Compounds) / sizeof(Compounds[0])); Index++)
    {
        if (Compounds[Index] != NULL)
        {
            TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, Compounds[Index], true));
        }
    }
    Memory_Free(Text._data);
    TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
    KeyRegistry_Deconstruct(&Keys);
}

static void RefusedReadsForgetTheirKeys(TestRun* run)
{
    KeyRegistry Keys;
    GHDFObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)) || !TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Pool)))
    {
        KeyRegistry_Deconstruct(&Keys);
        return;
    }
    uint32_t Known = Intern(run, &Keys, "town:known");
    size_t Count = KeyRegistry_GetCount(&Keys);
    uint32_t Found = NAMESPACED_KEY_ID_NONE;

    // JSON: the list's first key is interned, its last refused; the refused read forgets the first.
    const char* const BadKey[] = { "[2]", "uppercase", NULL };
    CheckJSONRefused(run, &Keys, "[{\"$key\": \"town:fresh\"}, {\"$key\": \"town:known\"}, {\"$key\": \"Town:Bad\"}]",
        BadKey);
    TEST_CHECK(run, (KeyRegistry_GetCount(&Keys) == Count)
        && !KeyRegistry_TryFind(&Keys, (const unsigned char*)u8"town:fresh", &Found));

    // GHDF: the same list.
    const char* const Texts[] = { "town:fresh", "town:known", "Town:Bad" };
    GHDFCompound* List = MakeArrayValueCompound(run, Pool, DataType_List, DataType_Key,
        MakeStringArray(run, Pool, Texts, 3U));
    DataValue Value = DataValue_CreateTag();
    TestText_CheckErrorMessage(run, DataValue_ReadGHDF(List, &Keys, &Value), ErrorCode_Deserialize, BadKey);
    TEST_CHECK(run, (Value.Type == DataType_Tag) && (KeyRegistry_GetCount(&Keys) == Count)
        && !KeyRegistry_TryFind(&Keys, (const unsigned char*)u8"town:fresh", &Found));

    // A read that succeeds keeps its keys; a later refused read forgets only its own.
    TEST_CHECK_SUCCESS(run, ReadJSONText(run, "{\"$key\": \"town:fresh\"}", &Keys, &Value));
    TEST_CHECK(run, (Value.Type == DataType_Key) && (KeyRegistry_GetCount(&Keys) == (Count + 1U)));
    CheckJSONRefused(run, &Keys, "[{\"$key\": \"town:newer\"}, {\"$key\": \"town:fresh\"}, {\"$key\": \"Town:Bad\"}]",
        BadKey);
    TEST_CHECK(run, (KeyRegistry_GetCount(&Keys) == (Count + 1U))
        && (strcmp((const char*)KeyRegistry_GetText(&Keys, Value.KeyValue), "town:fresh") == 0)
        && (strcmp((const char*)KeyRegistry_GetText(&Keys, Known), "town:known") == 0)
        && !KeyRegistry_TryFind(&Keys, (const unsigned char*)u8"town:newer", &Found));

    if (List != NULL)
    {
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, List, true));
    }
    TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
    KeyRegistry_Deconstruct(&Keys);
}


// Public functions.
void DataValueTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "equality_holds_across_every_type", EqualityHoldsAcrossEveryType },
        { "deep_copies_are_independent", DeepCopiesAreIndependent },
        { "lists_hold_one_element_type", ListsHoldOneElementType },
        { "depth_seventeen_is_refused", DepthSeventeenIsRefused },
        { "strings_are_limited_and_utf8", StringsAreLimitedAndUTF8 },
        { "compound_names_follow_the_rules", CompoundNamesFollowTheRules },
        { "validation_names_the_problem", ValidationNamesTheProblem },
        { "byte_sizes_count_the_data", ByteSizesCountTheData },
        { "json_round_trip_keeps_every_type", JSONRoundTripKeepsEveryType },
        { "json_numbers_keep_their_type", JSONNumbersKeepTheirType },
        { "json_refuses_hostile_input", JSONRefusesHostileInput },
        { "ghdf_round_trip_keeps_every_type", GHDFRoundTripKeepsEveryType },
        { "ghdf_refuses_hostile_input", GHDFRefusesHostileInput },
        { "ghdf_refuses_non_finite_vectors", GHDFRefusesNonFiniteVectors },
        { "vector3s_are_doubles_and_old_forms_read", Vector3sAreDoublesAndOldFormsRead },
        { "refused_reads_forget_their_keys", RefusedReadsForgetTheirKeys },
    };
    TestRun_RunSuite(run, "DataValue", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
