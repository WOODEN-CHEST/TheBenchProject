#include "TestSuites.h"
#include "GameJSON.h"
#include <math.h>
#include <stdint.h>


// Static functions.
/* Converts a JSON real with GameJSON_ValueToInt64; *outValue is left untouched on failure. */
static Error ConvertReal(double real, int64_t* outValue)
{
    JSONObjectValue Value = JSONObjectValue_CreateRealNumber(real);
    return GameJSON_ValueToInt64(&Value, outValue);
}

static void IntegerAcceptsJsonInteger(TestRun* run)
{
    JSONObjectValue Value = JSONObjectValue_CreateInteger(-42);
    int64_t Result = 0;
    TEST_CHECK_SUCCESS(run, GameJSON_ValueToInt64(&Value, &Result));
    TEST_CHECK(run, Result == -42);
}

static void IntegerAcceptsWholeReal(TestRun* run)
{
    int64_t Result = 0;
    TEST_CHECK_SUCCESS(run, ConvertReal(60.0, &Result));
    TEST_CHECK(run, Result == 60);
    TEST_CHECK_SUCCESS(run, ConvertReal(-3.0, &Result));
    TEST_CHECK(run, Result == -3);
}

static void IntegerRejectsFractionalReal(TestRun* run)
{
    int64_t Result = 7;
    TEST_CHECK_ERROR(run, ConvertReal(2.9, &Result), ErrorCode_InvalidJSON);
    TEST_CHECK_ERROR(run, ConvertReal(-0.5, &Result), ErrorCode_InvalidJSON);
    TEST_CHECK(run, Result == 7);
}

static void IntegerRejectsOutOfRangeReal(TestRun* run)
{
    int64_t Result = 7;
    TEST_CHECK_ERROR(run, ConvertReal(1e19, &Result), ErrorCode_InvalidJSON);
    TEST_CHECK_ERROR(run, ConvertReal(-1e19, &Result), ErrorCode_InvalidJSON);
    // 2^63 is the first value past INT64_MAX; -2^63 is exactly INT64_MIN and must still convert.
    TEST_CHECK_ERROR(run, ConvertReal(9223372036854775808.0, &Result), ErrorCode_InvalidJSON);
    TEST_CHECK(run, Result == 7);
    TEST_CHECK_SUCCESS(run, ConvertReal(-9223372036854775808.0, &Result));
    TEST_CHECK(run, Result == INT64_MIN);
}

static void IntegerRejectsNonFiniteReal(TestRun* run)
{
    int64_t Result = 7;
    TEST_CHECK_ERROR(run, ConvertReal((double)INFINITY, &Result), ErrorCode_InvalidJSON);
    TEST_CHECK_ERROR(run, ConvertReal(-(double)INFINITY, &Result), ErrorCode_InvalidJSON);
    TEST_CHECK_ERROR(run, ConvertReal((double)NAN, &Result), ErrorCode_InvalidJSON);
    TEST_CHECK(run, Result == 7);
}

static void IntegerParsesNumericString(TestRun* run)
{
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, 8U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Text, (const unsigned char*)u8"0x10");
    JSONObjectValue Value = JSONObjectValue_CreateString(&Text);

    int64_t Result = 0;
    TEST_CHECK_SUCCESS(run, GameJSON_ValueToInt64(&Value, &Result));
    TEST_CHECK(run, Result == 16);
    Memory_Free(Text._data);
}

static void IntegerRejectsBoolean(TestRun* run)
{
    JSONObjectValue Value = JSONObjectValue_CreateBoolean(true);
    int64_t Result = 7;
    TEST_CHECK_ERROR(run, GameJSON_ValueToInt64(&Value, &Result), ErrorCode_InvalidJSON);
    TEST_CHECK(run, Result == 7);
}


// Public functions.
void GameJSONTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "integer_accepts_json_integer", IntegerAcceptsJsonInteger },
        { "integer_accepts_whole_real", IntegerAcceptsWholeReal },
        { "integer_rejects_fractional_real", IntegerRejectsFractionalReal },
        { "integer_rejects_out_of_range_real", IntegerRejectsOutOfRangeReal },
        { "integer_rejects_non_finite_real", IntegerRejectsNonFiniteReal },
        { "integer_parses_numeric_string", IntegerParsesNumericString },
        { "integer_rejects_boolean", IntegerRejectsBoolean },
    };
    TestRun_RunSuite(run, "GameJSON", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
