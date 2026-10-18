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

static void UnsignedIntegersReachTheFullRange(TestRun* run)
{
    uint64_t Result = 7U;
    JSONObjectValue Integer = JSONObjectValue_CreateInteger(42);
    TEST_CHECK_SUCCESS(run, GameJSON_ValueToUInt64(&Integer, &Result));
    TEST_CHECK(run, Result == 42U);

    JSONObjectValue Negative = JSONObjectValue_CreateInteger(-1);
    JSONObjectValue Fraction = JSONObjectValue_CreateRealNumber(1.5);
    JSONObjectValue TooBig = JSONObjectValue_CreateRealNumber(18446744073709551616.0);
    JSONObjectValue Whole = JSONObjectValue_CreateRealNumber(1e19);
    Result = 7U;
    TEST_CHECK_ERROR(run, GameJSON_ValueToUInt64(&Negative, &Result), ErrorCode_InvalidJSON);
    TEST_CHECK_ERROR(run, GameJSON_ValueToUInt64(&Fraction, &Result), ErrorCode_InvalidJSON);
    TEST_CHECK_ERROR(run, GameJSON_ValueToUInt64(&TooBig, &Result), ErrorCode_InvalidJSON);
    TEST_CHECK(run, Result == 7U);
    TEST_CHECK_SUCCESS(run, GameJSON_ValueToUInt64(&Whole, &Result));
    TEST_CHECK(run, Result == 10000000000000000000ULL);

    // Above INT64_MAX the writer makes a string, which the reader takes back.
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return;
    }
    const uint64_t Values[] = { 0U, 9223372036854775807ULL, 9223372036854775808ULL, UINT64_MAX };
    for (size_t Index = 0; Index < (sizeof(Values) / sizeof(Values[0])); Index++)
    {
        JSONObjectValue Value;
        if (TEST_CHECK_SUCCESS(run, GameJSON_CreateUInt64Value(Pool, Values[Index], &Value)))
        {
            TEST_CHECK(run, Value.Type == ((Values[Index] > (uint64_t)INT64_MAX) ? JSONValueType_String : JSONValueType_Integer));
            TEST_CHECK_SUCCESS(run, GameJSON_ValueToUInt64(&Value, &Result));
            TEST_CHECK(run, Result == Values[Index]);
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
        }
    }
    JSONObjectValue Text;
    if (TestText_ParseJSON(run, Pool, "\"0x10\"", &Text))
    {
        TEST_CHECK_SUCCESS(run, GameJSON_ValueToUInt64(&Text, &Result));
        TEST_CHECK(run, Result == 16U);
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Text));
    }
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
}

static void Vector3NeedsExactlyThreeComponents(TestRun* run)
{
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return;
    }
    const char* const Valid[] = { "[1, 2.5, -3]", "{\"x\": 1, \"y\": 2.5, \"z\": -3}", "[\"1\", 2.5, \"-3\"]" };
    const char* const Invalid[] = { "[1, 2]", "[1, 2, 3, 4]", "{\"x\": 1, \"y\": 2}", "[1, 2, 1e300]", "\"1 2 3\"", "[1, true, 3]" };
    for (size_t Index = 0; Index < (sizeof(Valid) / sizeof(Valid[0])); Index++)
    {
        JSONObjectValue Value;
        if (TestText_ParseJSON(run, Pool, Valid[Index], &Value))
        {
            Vector3 Parsed = { 0.0f, 0.0f, 0.0f };
            TEST_CHECK_SUCCESS(run, GameJSON_ParseVector3(&Value, &Parsed));
            TEST_CHECK(run, (Parsed.x == 1.0f) && (Parsed.y == 2.5f) && (Parsed.z == -3.0f));
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
        }
    }
    for (size_t Index = 0; Index < (sizeof(Invalid) / sizeof(Invalid[0])); Index++)
    {
        JSONObjectValue Value;
        if (TestText_ParseJSON(run, Pool, Invalid[Index], &Value))
        {
            Vector3 Parsed = { 7.0f, 7.0f, 7.0f };
            TEST_CHECK_ERROR(run, GameJSON_ParseVector3(&Value, &Parsed), ErrorCode_InvalidJSON);
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
        }
    }
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
}

/* Whatever the writers build, the parsers read back exactly. */
static void WrittenValuesReadBackExactly(TestRun* run)
{
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return;
    }
    JSONObjectValue Value;
    const Color Colors[] = { { 255, 0, 16, 255 }, { 1, 2, 3, 4 } };
    const size_t HexLengths[] = { 7U, 9U };
    for (size_t Index = 0; Index < 2U; Index++)
    {
        if (TEST_CHECK_SUCCESS(run, GameJSON_CreateColorValue(Pool, Colors[Index], &Value)))
        {
            Color Parsed = { 0, 0, 0, 0 };
            TEST_CHECK(run, (Value.Type == JSONValueType_String) && (Value.Value.String->_count == HexLengths[Index]));
            TEST_CHECK_SUCCESS(run, GameJSON_ParseColor(&Value, &Parsed));
            TEST_CHECK(run, (Parsed.r == Colors[Index].r) && (Parsed.g == Colors[Index].g) && (Parsed.b == Colors[Index].b)
                && (Parsed.a == Colors[Index].a));
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
        }
    }

    const RenderColor Shades[] = { { { 9, 8, 7, 100 }, 1.0f, 1.0f }, { { 9, 8, 7, 255 }, 0.3f, 0.6f } };
    for (size_t Index = 0; Index < 2U; Index++)
    {
        if (TEST_CHECK_SUCCESS(run, GameJSON_CreateRenderColorValue(Pool, Shades[Index], &Value)))
        {
            RenderColor Parsed;
            TEST_CHECK(run, Value.Type == ((Index == 0U) ? JSONValueType_String : JSONValueType_Compound));
            TEST_CHECK_SUCCESS(run, GameJSON_ParseRenderColor(&Value, &Parsed));
            TEST_CHECK(run, (Parsed.Tint.r == 9U) && (Parsed.Tint.b == 7U) && (Parsed.Brightness == Shades[Index].Brightness)
                && (Parsed.Opacity == Shades[Index].Opacity));
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
        }
    }

    Vector3 Vector = { 0.1f, -1e-7f, 3.4e38f };
    if (TEST_CHECK_SUCCESS(run, GameJSON_CreateVector3Value(Pool, Vector, &Value)))
    {
        Vector3 Parsed = { 0.0f, 0.0f, 0.0f };
        TEST_CHECK_SUCCESS(run, GameJSON_ParseVector3(&Value, &Parsed));
        TEST_CHECK(run, (Parsed.x == Vector.x) && (Parsed.y == Vector.y) && (Parsed.z == Vector.z));
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
    }
    if (TEST_CHECK_SUCCESS(run, GameJSON_CreateVector2Value(Pool, (Vector2){ 0.5f, 2.0f }, &Value)))
    {
        Vector2 Parsed = { 0.0f, 0.0f };
        TEST_CHECK_SUCCESS(run, GameJSON_ParseVector2(&Value, &Parsed));
        TEST_CHECK(run, (Parsed.x == 0.5f) && (Parsed.y == 2.0f));
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
    }
    JSONObjectValue Shortest = GameJSON_CreateFloatValue(0.1f);
    TEST_CHECK(run, (Shortest.Type == JSONValueType_RealNumber) && (Shortest.Value.RealNumber == 0.1));
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
        { "unsigned_integers_reach_the_full_range", UnsignedIntegersReachTheFullRange },
        { "vector3_needs_exactly_three_components", Vector3NeedsExactlyThreeComponents },
        { "written_values_read_back_exactly", WrittenValuesReadBackExactly },
    };
    TestRun_RunSuite(run, "GameJSON", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
