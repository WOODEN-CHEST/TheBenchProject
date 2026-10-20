#include "TestSuites.h"
#include "TestText.h"
#include "GameGHDF.h"
#include "wr/WRGHDF.h"
#include "wr/WRMemory.h"
#include <string.h>


// Static functions.
static bool PutEntry(TestRun* run, GHDFCompound* compound, GHDFEntryID id, GHDFObjectValue value)
{
    return TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(value.Type), &value));
}


// Test cases.
static void IntegersTakeTheSmallestType(TestRun* run)
{
    TEST_CHECK(run, GameGHDF_GetSmallestUnsignedType(0U) == GHDFValueType_UInt8);
    TEST_CHECK(run, GameGHDF_GetSmallestUnsignedType(255U) == GHDFValueType_UInt8);
    TEST_CHECK(run, GameGHDF_GetSmallestUnsignedType(256U) == GHDFValueType_UInt16);
    TEST_CHECK(run, GameGHDF_GetSmallestUnsignedType(65535U) == GHDFValueType_UInt16);
    TEST_CHECK(run, GameGHDF_GetSmallestUnsignedType(65536U) == GHDFValueType_UInt32);
    TEST_CHECK(run, GameGHDF_GetSmallestUnsignedType(UINT32_MAX) == GHDFValueType_UInt32);
    TEST_CHECK(run, GameGHDF_GetSmallestUnsignedType((uint64_t)UINT32_MAX + 1U) == GHDFValueType_UInt64);
    TEST_CHECK(run, GameGHDF_GetSmallestUnsignedType(UINT64_MAX) == GHDFValueType_UInt64);
    TEST_CHECK(run, GameGHDF_GetSmallestSignedType(-128, 127) == GHDFValueType_Int8);
    TEST_CHECK(run, GameGHDF_GetSmallestSignedType(-129, 0) == GHDFValueType_Int16);
    TEST_CHECK(run, GameGHDF_GetSmallestSignedType(0, 128) == GHDFValueType_Int16);
    TEST_CHECK(run, GameGHDF_GetSmallestSignedType(INT16_MIN, INT16_MAX) == GHDFValueType_Int16);
    TEST_CHECK(run, GameGHDF_GetSmallestSignedType(0, (int64_t)INT16_MAX + 1) == GHDFValueType_Int32);
    TEST_CHECK(run, GameGHDF_GetSmallestSignedType(INT32_MIN, 0) == GHDFValueType_Int32);
    TEST_CHECK(run, GameGHDF_GetSmallestSignedType((int64_t)INT32_MIN - 1, 0) == GHDFValueType_Int64);
    TEST_CHECK(run, GameGHDF_GetSmallestSignedType(INT64_MIN, INT64_MAX) == GHDFValueType_Int64);
}

static void AnyIntegerTypeReadsWhenTheValueFits(TestRun* run)
{
    int64_t Signed = 0;
    uint64_t Unsigned = 0U;
    GHDFObjectValue Values[] =
    {
        GHDFObjectValue_CreateUInt8(200U), GHDFObjectValue_CreateInt8(-5), GHDFObjectValue_CreateInt16(-300),
        GHDFObjectValue_CreateUInt16(60000U), GHDFObjectValue_CreateInt32(-70000), GHDFObjectValue_CreateUInt32(4000000000U),
        GHDFObjectValue_CreateInt64(-5000000000LL), GHDFObjectValue_CreateUInt64(5000000000ULL),
        GHDFObjectValue_CreateEncodedInteger(-7)
    };
    const int64_t Expected[] = { 200, -5, -300, 60000, -70000, 4000000000LL, -5000000000LL, 5000000000LL, -7 };
    for (size_t Index = 0U; Index < (sizeof(Values) / sizeof(Values[0])); Index++)
    {
        TEST_CHECK(run, GameGHDF_IsIntegerType(Values[Index].Type));
        TEST_CHECK(run, GameGHDF_TryGetSigned(&Values[Index], &Signed) && (Signed == Expected[Index]));
        bool IsUnsigned = GameGHDF_TryGetUnsigned(&Values[Index], &Unsigned);
        TEST_CHECK(run, IsUnsigned == (Expected[Index] >= 0));
        TEST_CHECK(run, !IsUnsigned || (Unsigned == (uint64_t)Expected[Index]));
    }
    // A UInt64 above INT64_MAX is unsigned only; a float is no integer.
    GHDFObjectValue Huge = GHDFObjectValue_CreateUInt64(UINT64_MAX);
    TEST_CHECK(run, !GameGHDF_TryGetSigned(&Huge, &Signed));
    TEST_CHECK(run, GameGHDF_TryGetUnsigned(&Huge, &Unsigned) && (Unsigned == UINT64_MAX));
    GHDFObjectValue Real = GHDFObjectValue_CreateFloat(1.0f);
    TEST_CHECK(run, !GameGHDF_IsIntegerType(Real.Type) && !GameGHDF_TryGetSigned(&Real, &Signed));
}

static void EntriesWriteSmallAndReadInRange(TestRun* run)
{
    GHDFObjectPool* Pool = NULL;
    GHDFCompound* Compound = NULL;
    if (!TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Pool)))
    {
        return;
    }
    if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Pool, &Compound)))
    {
        GHDFObjectValue Entry;
        TEST_CHECK_SUCCESS(run, GameGHDF_PutUnsigned(Compound, 1U, 300U));
        TEST_CHECK_SUCCESS(run, GameGHDF_PutSigned(Compound, 2U, -2));
        TEST_CHECK(run, (GHDFCompound_Get(Compound, 1U, &Entry).Code == ErrorCode_Success)
            && (Entry.Type == GHDFValueType_UInt16));
        TEST_CHECK(run, (GHDFCompound_Get(Compound, 2U, &Entry).Code == ErrorCode_Success)
            && (Entry.Type == GHDFValueType_Int8));

        uint64_t Unsigned = 0U;
        int64_t Signed = 0;
        bool IsFound = false;
        TEST_CHECK_SUCCESS(run, GameGHDF_GetOptionalUnsigned(Compound, 1U, 1000U, &Unsigned, &IsFound));
        TEST_CHECK(run, IsFound && (Unsigned == 300U));
        TEST_CHECK_SUCCESS(run, GameGHDF_GetOptionalSigned(Compound, 2U, -10, 10, &Signed, &IsFound));
        TEST_CHECK(run, IsFound && (Signed == -2));
        // Absent entries are no error and leave the value.
        Unsigned = 77U;
        TEST_CHECK_SUCCESS(run, GameGHDF_GetOptionalUnsigned(Compound, 9U, 1000U, &Unsigned, &IsFound));
        TEST_CHECK(run, !IsFound && (Unsigned == 77U));

        // Out of range, negative for unsigned, not an integer, or an array of integers: refused.
        const char* const Range[] = { "from 0 to 299", NULL };
        TestText_CheckErrorMessage(run, GameGHDF_GetOptionalUnsigned(Compound, 1U, 299U, &Unsigned, &IsFound),
            ErrorCode_Deserialize, Range);
        TEST_CHECK_ERROR(run, GameGHDF_GetOptionalUnsigned(Compound, 2U, 1000U, &Unsigned, &IsFound),
            ErrorCode_Deserialize);
        TEST_CHECK_ERROR(run, GameGHDF_GetOptionalSigned(Compound, 2U, 0, 10, &Signed, &IsFound), ErrorCode_Deserialize);
        PutEntry(run, Compound, 3U, GHDFObjectValue_CreateDouble(2.0));
        TEST_CHECK_ERROR(run, GameGHDF_GetOptionalSigned(Compound, 3U, 0, 10, &Signed, &IsFound), ErrorCode_Deserialize);
        GHDFArray* Numbers = NULL;
        if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowArray(Pool, GHDFValueType_UInt8, &Numbers)))
        {
            GHDFObjectValue Element = GHDFObjectValue_CreateUInt8(4U);
            TEST_CHECK_SUCCESS(run, GHDFArray_AddValue(Numbers, &Element));
            GHDFObjectValue ArrayValue = GHDFObjectValue_CreateArray(Numbers, GHDFValueType_UInt8);
            TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Compound, 4U, GHDF_CreateArrayType(GHDFValueType_UInt8),
                &ArrayValue));
            TEST_CHECK_ERROR(run, GameGHDF_GetOptionalUnsigned(Compound, 4U, 1000U, &Unsigned, &IsFound),
                ErrorCode_Deserialize);
        }
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, Compound, true));
    }
    TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
}

static void StringsReadBackWithoutNULs(TestRun* run)
{
    GHDFObjectPool* Pool = NULL;
    GHDFCompound* Compound = NULL;
    if (!TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Pool)))
    {
        return;
    }
    if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Pool, &Compound)))
    {
        const unsigned char* Text = NULL;
        size_t Length = 0U;
        bool IsFound = false;
        TEST_CHECK_SUCCESS(run, GameGHDF_PutString(Pool, Compound, 1U, (const unsigned char*)u8"Pilsēta"));
        TEST_CHECK_SUCCESS(run, GameGHDF_GetOptionalString(Compound, 1U, &Text, &Length, &IsFound));
        TEST_CHECK(run, IsFound && (Length == 8U) && (memcmp(Text, u8"Pilsēta", 9U) == 0));
        TEST_CHECK_SUCCESS(run, GameGHDF_GetOptionalString(Compound, 2U, &Text, &Length, &IsFound));
        TEST_CHECK(run, !IsFound);

        // A NUL inside would cut the text short; another type is no string.
        GenericBuffer* Inner = NULL;
        if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowString(Pool, &Inner)))
        {
            GenericBuffer_AppendRangeBytes(Inner, (unsigned char*)"a\0b", 3U);
            GenericBuffer_NullTerminate(Inner);
            GHDFObjectValue Value = GHDFObjectValue_CreateString(Inner);
            TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Compound, 3U, GHDF_CreateRegularType(GHDFValueType_String),
                &Value));
            TEST_CHECK_ERROR(run, GameGHDF_GetOptionalString(Compound, 3U, &Text, &Length, &IsFound),
                ErrorCode_Deserialize);
        }
        PutEntry(run, Compound, 4U, GHDFObjectValue_CreateUInt8(1U));
        TEST_CHECK_ERROR(run, GameGHDF_GetOptionalString(Compound, 4U, &Text, &Length, &IsFound), ErrorCode_Deserialize);
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, Compound, true));
    }
    TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
}


// Fields: the suite.
static const TestCase GameGHDFTestCases[] =
{
    { "integers_take_the_smallest_type", IntegersTakeTheSmallestType },
    { "any_integer_type_reads_when_the_value_fits", AnyIntegerTypeReadsWhenTheValueFits },
    { "entries_write_small_and_read_in_range", EntriesWriteSmallAndReadInRange },
    { "strings_read_back_without_nuls", StringsReadBackWithoutNULs },
};


// Public functions.
void GameGHDFTests_Run(TestRun* run)
{
    TestRun_RunSuite(run, "GameGHDF", GameGHDFTestCases, sizeof(GameGHDFTestCases) / sizeof(GameGHDFTestCases[0]));
}
