#include "TestSuites.h"
#include "TestText.h"
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


// Converts the JSON string value text with each converter; every bad value must be ErrorCode_InvalidJSON.
static void StringNumbersFailAsInvalidJSON(TestRun* run)
{
    const char* const BadNumbers[] = { "abc", "1.5.2", "", "nan", "inf" };
    for (size_t Index = 0; Index < (sizeof(BadNumbers) / sizeof(BadNumbers[0])); Index++)
    {
        GenericBuffer Text;
        GenericBuffer_AllocateVariable(&Text, 8U, sizeof(unsigned char));
        GenericBuffer_AppendString(&Text, (const unsigned char*)BadNumbers[Index]);
        JSONObjectValue Value = JSONObjectValue_CreateString(&Text);
        int64_t Integer = 0;
        double Decimal = 0.0;
        TEST_CHECK_ERROR(run, GameJSON_ValueToInt64(&Value, &Integer), ErrorCode_InvalidJSON);
        TEST_CHECK_ERROR(run, GameJSON_ValueToDouble(&Value, &Decimal), ErrorCode_InvalidJSON);
        Memory_Free(Text._data);
    }
}

static void DecimalRejectsNonFiniteReals(TestRun* run)
{
    double Decimal = 7.0;
    JSONObjectValue Infinite = JSONObjectValue_CreateRealNumber((double)INFINITY);
    JSONObjectValue NotANumber = JSONObjectValue_CreateRealNumber((double)NAN);
    TEST_CHECK_ERROR(run, GameJSON_ValueToDouble(&Infinite, &Decimal), ErrorCode_InvalidJSON);
    TEST_CHECK_ERROR(run, GameJSON_ValueToDouble(&NotANumber, &Decimal), ErrorCode_InvalidJSON);
    TEST_CHECK(run, Decimal == 7.0);
}

// Parses json and checks one structured parser on it.
static void CheckStructuredValue(TestRun* run, const char* json, bool isRenderColor, bool isValid)
{
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return;
    }
    JSONObjectValue Value;
    if (TestText_ParseJSON(run, Pool, json, &Value))
    {
        RenderColor ParsedColor;
        Vector2 ParsedVector;
        Error Result = isRenderColor ? GameJSON_ParseRenderColor(&Value, &ParsedColor) : GameJSON_ParseVector2(&Value, &ParsedVector);
        if (isValid)
        {
            TEST_CHECK_SUCCESS(run, Result);
        }
        else
        {
            TEST_CHECK_ERROR(run, Result, ErrorCode_InvalidJSON);
        }
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
    }
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
}

static void RenderColorsAndVectorsAreRangeChecked(TestRun* run)
{
    CheckStructuredValue(run, "{\"tint\": \"#ff8800\", \"brightness\": 0.5, \"opacity\": 1}", true, true);
    CheckStructuredValue(run, "{\"tint\": \"#ff8800\", \"brightness\": 2}", true, false);
    CheckStructuredValue(run, "{\"tint\": \"#ff8800\", \"opacity\": -0.5}", true, false);
    CheckStructuredValue(run, "\"#zzzzzz\"", true, false);
    CheckStructuredValue(run, "[1.5, -2]", false, true);
    CheckStructuredValue(run, "[1e300, 0]", false, false);
}

// Channels outside [0, 255] and malformed hex are errors in every color form, never clamped or half-read.
static void ColorsRejectBadChannelsAndHexDigits(TestRun* run)
{
    const char* const Valid[] = { "\"#FFaa00\"", "\"0x00ff0080\"", "[0, 128, 255, 0]", "{\"r\": 255, \"g\": 0, \"b\": 7}" };
    const char* const Invalid[] =
    {
        "[256, 0, 0]", "[0, -1, 0]", "[0, 0, 0, 300]", "{\"r\": 0, \"g\": 300, \"b\": 0}", "{\"tint\": [0, 0, 999]}",
        "\"#-12345\"", "\"#+12345\"", "\"# 12345\"", "\"#12345g\"", "\"#12345\"", "\"#123456789\""
    };
    for (size_t Index = 0; Index < (sizeof(Valid) / sizeof(Valid[0])); Index++)
    {
        CheckStructuredValue(run, Valid[Index], true, true);
    }
    for (size_t Index = 0; Index < (sizeof(Invalid) / sizeof(Invalid[0])); Index++)
    {
        CheckStructuredValue(run, Invalid[Index], true, false);
    }

    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return;
    }
    JSONObjectValue Value;
    if (TestText_ParseJSON(run, Pool, "\"#FFaa0080\"", &Value))
    {
        Color Parsed = { 0, 0, 0, 0 };
        TEST_CHECK_SUCCESS(run, GameJSON_ParseColor(&Value, &Parsed));
        TEST_CHECK(run, (Parsed.r == 255U) && (Parsed.g == 170U) && (Parsed.b == 0U) && (Parsed.a == 128U));
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
    }
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
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
        { "string_numbers_fail_as_invalid_json", StringNumbersFailAsInvalidJSON },
        { "decimal_rejects_non_finite_reals", DecimalRejectsNonFiniteReals },
        { "render_colors_and_vectors_are_range_checked", RenderColorsAndVectorsAreRangeChecked },
        { "colors_reject_bad_channels_and_hex_digits", ColorsRejectBadChannelsAndHexDigits },
    };
    TestRun_RunSuite(run, "GameJSON", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
